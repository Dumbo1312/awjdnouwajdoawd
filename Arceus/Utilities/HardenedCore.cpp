#include "HardenedCore.h"
#include "../Core/CrashLog.h"
#include "RuntimeProtection.h"
#include "LicenseGuard.h"
#include "../Core/ServerSecureClient.h"
#include "../Core/SecurePayload.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstring>

//
// Poison digests â€” low bits feed the aim jitter:
//   benign  0x2545F4914F6CDD1D -> (x&3)-1 = 0, ((x>>4)&3)-1 = 0  (normal spread)
//   poison  0x9E3779B97F4A7C12 -> (x&3)-1 = +1                   (silent drift)
// Patching the canary's return value changes the aim pattern too.
//
static constexpr uint64_t CANARY_BENIGN  = 0x2545F4914F6CDD1DULL;
static constexpr uint64_t CANARY_POISON  = 0x9E3779B97F4A7C12ULL;

namespace
{
    std::atomic<bool> g_running{ false };
    std::atomic<bool> g_poisoned{ false };

    std::atomic<uint64_t> g_beatA{ 0 };
    std::atomic<uint64_t> g_beatB{ 0 };
    std::atomic<uint64_t> g_beatMonitor{ 0 };

    std::mutex g_spawnMutex;

    constexpr uint64_t BEAT_GRACE_MS = 15000;

    uint64_t NowMs()
    {
        return GetTickCount64();
    }

    bool BeatStale(const std::atomic<uint64_t>& beat)
    {
        uint64_t last = beat.load(std::memory_order_acquire);
        if (last == 0) return false;
        uint64_t now = NowMs();
        if (now < last) return false;
        return (now - last) > BEAT_GRACE_MS;
    }

    void Beat(std::atomic<uint64_t>& beat)
    {
        beat.store(NowMs(), std::memory_order_release);
    }

    struct RevToolNeedlesWin
    {
        const char* items[8];
    };

    BOOL CALLBACK EnumRevToolWindows(HWND hwnd, LPARAM lparam)
    {
        const RevToolNeedlesWin* nd =
            reinterpret_cast<const RevToolNeedlesWin*>(lparam);

        char title[256];
        if (GetWindowTextA(hwnd, title, 256) <= 0)
            return TRUE;
        CharLowerA(title);

        for (int i = 0; i < 8; ++i)
        {
            if (strstr(title, nd->items[i]) != nullptr)
                return FALSE;
        }
        return TRUE;
    }

    void WatchdogLoopA();
    void WatchdogLoopB();
    void SupervisorLoop();

    void SpawnA()
    {
        std::lock_guard<std::mutex> lock(g_spawnMutex);
        g_beatA.store(NowMs(), std::memory_order_release);
        std::thread(WatchdogLoopA).detach();
    }

    void SpawnB()
    {
        std::lock_guard<std::mutex> lock(g_spawnMutex);
        g_beatB.store(NowMs(), std::memory_order_release);
        std::thread(WatchdogLoopB).detach();
    }
}

namespace HardenedCore
{
    void OnTamperDetected()
    {
        if (g_poisoned.exchange(true, std::memory_order_acq_rel))
            return;

        // 1. Session CRC breaks -> calibration garbage (rawX * 1000).
        ServerSecureClient::InvalidateSession();

        // 2. AES key zeroed -> model decryption fails -> zero detections.
        SecurePayload::InvalidateSessionKey();

        // 3. Validation token / response hash poisoned.
        LicenseGuard::SetLicenseValid(false, "");
    }

    bool DebuggerDetectedSyscall()
    {
        OBF_BOGUS_FLOW_CASCADE();

        // --- ProcessDebugObjectHandle (0x1E): non-null handle = debugger attached.
        struct
        {
            HANDLE handle;
            BOOLEAN inherited;
        } dbgObj = {};

        NTSTATUS st = K8_SYSCALL("ZwQueryInformationProcess",
            (HANDLE)GetCurrentProcess(), (UINT)0x1E,
            &dbgObj, (ULONG)sizeof(dbgObj), (PULONG)nullptr);

        if (st != (NTSTATUS)0xC0000005)
        {
            if (st >= 0 && dbgObj.handle != nullptr)
                return true;
        }
        else
        {
            // syscall engine unavailable -> fall back to PEB checks
            return RuntimeProtection::PEBCheck::IsBeingDebugged();
        }

        // --- ProcessDebugFlags (0x1F): 0 = process is being debugged.
        ULONG noDebugInherit = 1;
        st = K8_SYSCALL("ZwQueryInformationProcess",
            (HANDLE)GetCurrentProcess(), (UINT)0x1F,
            &noDebugInherit, (ULONG)sizeof(noDebugInherit), (PULONG)nullptr);
        if (st >= 0 && noDebugInherit == 0)
            return true;

        // --- ProcessDebugPort (0x7): nonzero = debugger attached.
        ULONG_PTR debugPort = 0;
        st = K8_SYSCALL("ZwQueryInformationProcess",
            (HANDLE)GetCurrentProcess(), (UINT)0x7,
            &debugPort, (ULONG)sizeof(debugPort), (PULONG)nullptr);
        if (st >= 0 && debugPort != 0)
            return true;

        // --- Kernel debugger (SystemKernelDebuggerInformation = 0x23).
        struct
        {
            BOOLEAN DebuggerEnabled;
            BOOLEAN DebuggerNotPresent;
        } kdbg = {};

        st = K8_SYSCALL("ZwQuerySystemInformation",
            (UINT)0x23, &kdbg, (ULONG)sizeof(kdbg), (PULONG)nullptr);
        if (st >= 0 && kdbg.DebuggerEnabled && !kdbg.DebuggerNotPresent)
            return true;

        return false;
    }

    bool ReversingToolDetected()
    {
        // --- Known reversing/debugging tool processes (encrypted names).
        const std::string n1 = OBFUSCATE_STRING("x64dbg.exe");
        const std::string n2 = OBFUSCATE_STRING("x32dbg.exe");
        const std::string n3 = OBFUSCATE_STRING("x96dbg.exe");
        const std::string n4 = OBFUSCATE_STRING("ollydbg.exe");
        const std::string n5 = OBFUSCATE_STRING("ollyice.exe");
        const std::string n6 = OBFUSCATE_STRING("ida.exe");
        const std::string n7 = OBFUSCATE_STRING("ida64.exe");
        const std::string n8 = OBFUSCATE_STRING("idaq.exe");
        const std::string n9 = OBFUSCATE_STRING("idaq64.exe");
        const std::string n10 = OBFUSCATE_STRING("windbg.exe");
        const std::string n11 = OBFUSCATE_STRING("windbgx.exe");
        const std::string n12 = OBFUSCATE_STRING("cheatengine-x86_64.exe");
        const std::string n13 = OBFUSCATE_STRING("cheatengine-x86.exe");
        const std::string n14 = OBFUSCATE_STRING("processhacker.exe");
        const std::string n15 = OBFUSCATE_STRING("processhacker2.exe");
        const std::string n16 = OBFUSCATE_STRING("procmon.exe");
        const std::string n17 = OBFUSCATE_STRING("procmon64.exe");
        const std::string n18 = OBFUSCATE_STRING("importrec.exe");
        const std::string n19 = OBFUSCATE_STRING("dnspy.exe");
        const std::string n20 = OBFUSCATE_STRING("extremedumper.exe");
        const std::string n21 = OBFUSCATE_STRING("pe-sieve64.exe");
        const std::string n22 = OBFUSCATE_STRING("scylla.exe");
        const std::string n23 = OBFUSCATE_STRING("scyllax64.exe");
        const std::string n24 = OBFUSCATE_STRING("de4dot.exe");

        const std::string* needles[24] = {
            &n1, &n2, &n3, &n4, &n5, &n6, &n7, &n8, &n9, &n10,
            &n11, &n12, &n13, &n14, &n15, &n16, &n17, &n18, &n19, &n20,
            &n21, &n22, &n23, &n24
        };

        wchar_t wide[24][40];
        for (int i = 0; i < 24; ++i)
            MultiByteToWideChar(CP_ACP, 0, needles[i]->c_str(), -1, wide[i], 40);

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            bool hit = false;
            if (Process32FirstW(snap, &pe))
            {
                do
                {
                    for (int i = 0; i < 24; ++i)
                    {
                        if (_wcsicmp(pe.szExeFile, wide[i]) == 0)
                        {
                            hit = true;
                            break;
                        }
                    }
                    if (hit) break;
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
            if (hit)
                return true;
        }

        // --- Window-title scan (covers renamed executables, decoys the
        //     "no such process" check by matching the tool's UI too).
        const std::string w1 = OBFUSCATE_STRING("x64dbg");
        const std::string w2 = OBFUSCATE_STRING("x32dbg");
        const std::string w3 = OBFUSCATE_STRING("ollydbg");
        const std::string w4 = OBFUSCATE_STRING("ida - ");
        const std::string w5 = OBFUSCATE_STRING("cheat engine");
        const std::string w6 = OBFUSCATE_STRING("process hacker");
        const std::string w7 = OBFUSCATE_STRING("system informer");
        const std::string w8 = OBFUSCATE_STRING("windbg");

        RevToolNeedlesWin needlesWin = { { w1.c_str(), w2.c_str(), w3.c_str(),
            w4.c_str(), w5.c_str(), w6.c_str(), w7.c_str(), w8.c_str() } };

        if (!EnumWindows(EnumRevToolWindows,
                         reinterpret_cast<LPARAM>(&needlesWin)))
        {
            return true;
        }

        return false;
    }

    bool QuickSelfCheck()
    {
        if (g_poisoned.load(std::memory_order_acquire))
            return false;

        if (DebuggerDetectedSyscall())
            return false;

        if (RuntimeProtection::HardwareBreakpoint::DetectHardwareBreakpoints())
            return false;

        // Session checks are only meaningful once a real key is installed
        // (post-login). Before that the app sits in the Login state and the
        // watchdogs must not poison a legit user.
        if (SecurePayload::IsKeyValid() &&
            !ServerSecureClient::VerifySessionIntegrity())
            return false;

        return true;
    }

    uint64_t FrameCanary()
    {
        static std::atomic<uint32_t> s_frames{ 0 };
        const uint32_t f = s_frames.fetch_add(1, std::memory_order_relaxed);

        if (g_poisoned.load(std::memory_order_acquire))
            return CANARY_POISON;

        // Hash-progressed cadence: full re-verify fires at pseudo-random
        // frame numbers (~every 256) so the cadence can't be predicted or
        // matched to a fixed frame offset by a hook.
        const uint32_t r = (f * 2654435761u) ^ (f >> 7);
        if (r % 256u == 0u)
        {
            if (!QuickSelfCheck())
            {
                OnTamperDetected();
                return CANARY_POISON;
            }
        }

        return CANARY_BENIGN;
    }

    void HardenedSleep(unsigned baseMs)
    {
        if (baseMs == 0) return;

        const uint64_t tick = NowMs();
        unsigned delay = baseMs;

        // +/- ~37% jitter, plus occasional elongation.
        if ((tick % 3u) == 0)            delay += baseMs / 3u;
        else if ((tick % 3u) == 1)       delay -= baseMs / 4u;
        if ((tick & 0x1Fu) == 0)         delay += baseMs;     // ~ every 30s+ beat

        Sleep(delay);
    }

    void HeartbeatMonitorTick()
    {
        Beat(g_beatMonitor);
    }

    void Start()
    {
        if (g_running.exchange(true))
            return;

        g_poisoned.store(false, std::memory_order_release);
        idrct_sys::InitSyscallEngine();

        SpawnA();
        SpawnB();
        std::thread(SupervisorLoop).detach();
    }

    void Stop()
    {
        g_running.store(false, std::memory_order_release);
    }
}

namespace
{
    void WatchdogLoopA()
    {
        CrashLog::Log(("THREAD HardenedCore::WatchdogLoopA tid=" + std::to_string(GetCurrentThreadId())).c_str());
        Beat(g_beatA);

        while (g_running.load(std::memory_order_acquire) &&
               !g_poisoned.load(std::memory_order_acquire))
        {
            if (HardenedCore::DebuggerDetectedSyscall())
                { HardenedCore::OnTamperDetected(); break; }

            if (RuntimeProtection::PEBCheck::IsBeingDebugged() ||
                RuntimeProtection::HardwareBreakpoint::DetectHardwareBreakpoints())
                { HardenedCore::OnTamperDetected(); break; }

            if (SecurePayload::IsKeyValid() &&
                !ServerSecureClient::VerifySessionIntegrity())
                { HardenedCore::OnTamperDetected(); break; }

            // Partner watchdog or the RuntimeProtection monitor went dead.
            if (BeatStale(g_beatB) || BeatStale(g_beatMonitor))
                { HardenedCore::OnTamperDetected(); break; }

            Beat(g_beatA);
            HardenedCore::HardenedSleep(2500);
        }
    }

    void WatchdogLoopB()
    {
        CrashLog::Log(("THREAD HardenedCore::WatchdogLoopB tid=" + std::to_string(GetCurrentThreadId())).c_str());
        Beat(g_beatB);

        while (g_running.load(std::memory_order_acquire) &&
               !g_poisoned.load(std::memory_order_acquire))
        {
            if (SecurePayload::IsKeyValid() &&
                !LicenseGuard::CrossValidateAll())
                { HardenedCore::OnTamperDetected(); break; }

            if (RuntimeProtection::AntiVM::IsVirtualMachine())
                { HardenedCore::OnTamperDetected(); break; }

            if (RuntimeProtection::TimingCheck::DetectTimingAnomaly())
                { HardenedCore::OnTamperDetected(); break; }

            // Reversing tools present (x64dbg/CheatEngine/IDA/...) â€” treat like
            // a debugger: poison. The tool scan is cheap enough at 3s cadence.
            if (HardenedCore::ReversingToolDetected())
                { HardenedCore::OnTamperDetected(); break; }

            if (BeatStale(g_beatA) || BeatStale(g_beatMonitor))
                { HardenedCore::OnTamperDetected(); break; }

            Beat(g_beatB);
            HardenedCore::HardenedSleep(3000);
        }
    }

    void SupervisorLoop()
    {
        CrashLog::Log(("THREAD HardenedCore::SupervisorLoop tid=" + std::to_string(GetCurrentThreadId())).c_str());
        while (g_running.load(std::memory_order_acquire) &&
               !g_poisoned.load(std::memory_order_acquire))
        {
            HardenedCore::HardenedSleep(3000);

            // Everything dead at once -> only the aim-path canary can still fire;
            // poison immediately so the process can't run unprotected.
            if (BeatStale(g_beatA) && BeatStale(g_beatB) &&
                BeatStale(g_beatMonitor))
            {
                HardenedCore::OnTamperDetected();
                break;
            }

            // Self-healing: respawn whatever died.
            if (BeatStale(g_beatA) && g_running.load(std::memory_order_acquire))
                SpawnA();
            if (BeatStale(g_beatB) && g_running.load(std::memory_order_acquire))
                SpawnB();
        }
    }
}
