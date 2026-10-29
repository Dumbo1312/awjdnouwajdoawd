#include "RuntimeProtection.h"
#include "HardenedCore.h"
#include "LicenseGuard.h"
#include "../Core/ServerSecureClient.h"
#include "../Core/CrashLog.h"
#include "../Core/SecurePayload.h"
#include <Windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <winnt.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <thread>
#include <atomic>
#include <vector>
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace RuntimeProtection
{
    static std::atomic<bool> g_protectionActive{ false };
    static std::atomic<bool> g_dumpProtected{ false };
    static std::atomic<bool> g_iatTampered{ false };
    static std::thread g_hookMonitorThread;

    namespace
    {
        DWORD CalculatePEChecksum(PVOID baseAddress, DWORD size)
        {
            DWORD checksum = 0;
            DWORD* data = (DWORD*)baseAddress;
            DWORD dwSize = size / sizeof(DWORD);

            for (DWORD i = 0; i < dwSize; i++)
            {
                checksum += data[i];
                checksum = (checksum >> 1) | (checksum << 31);
            }

            return checksum;
        }
    }

    namespace AntiDump
    {
        static DWORD g_originalTextChecksum = 0;

        void Enable()
        {
            HMODULE hMod = GetModuleHandleA(NULL);
            if (!hMod) return;

            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
            PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

            for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
            {
                if (sectionHeader[i].Name[0] == '.' && 
                    sectionHeader[i].Name[1] == 't' &&
                    sectionHeader[i].Name[2] == 'e' &&
                    sectionHeader[i].Name[3] == 'x' &&
                    sectionHeader[i].Name[4] == 't')
                {
                    g_originalTextChecksum = CalculatePEChecksum(
                        (BYTE*)hMod + sectionHeader[i].VirtualAddress,
                        sectionHeader[i].Misc.VirtualSize);
                    break;
                }
            }

            g_dumpProtected.store(true);
        }

        void Disable() { g_dumpProtected.store(false); }
        bool IsProtected() { return g_dumpProtected.load(); }

        bool ValidateIntegrity()
        {
            OBF_BOGUS_FLOW_CYCLONE();
            if (!g_dumpProtected.load()) return true;
            if (g_originalTextChecksum == 0) return true;

            HMODULE hMod = GetModuleHandleA(NULL);
            if (!hMod) return true;

            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
            PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

            for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
            {
                if (strcmp((const char*)sectionHeader[i].Name, ".text") == 0)
                {
                    DWORD currentChecksum = CalculatePEChecksum(
                        (BYTE*)hMod + sectionHeader[i].VirtualAddress,
                        sectionHeader[i].Misc.VirtualSize);
                    return currentChecksum == g_originalTextChecksum;
                }
            }
            OBF_BOGUS_FLOW_SCRAMBLE();
            return true;
        }
    }

    namespace IATProtection
    {
        void ValidateIAT()
        {
            OBF_BOGUS_FLOW_CASCADE();
            HMODULE hMod = GetModuleHandleA(NULL);
            if (!hMod) return;

            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);

            IMAGE_DATA_DIRECTORY importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (importDir.Size == 0) return;

            BYTE* importEnd = (BYTE*)hMod + importDir.VirtualAddress + importDir.Size;
            PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod + importDir.VirtualAddress);

            while (importDesc->Name && (BYTE*)(importDesc + 1) <= importEnd)
            {
                char* moduleName = (char*)((BYTE*)hMod + importDesc->Name);
                HMODULE hModExpected = GetModuleHandleA(moduleName);
                if (!hModExpected) { importDesc++; continue; }

                PIMAGE_THUNK_DATA thunkILT = (PIMAGE_THUNK_DATA)((BYTE*)hMod + importDesc->OriginalFirstThunk);
                PIMAGE_THUNK_DATA thunkIAT = (PIMAGE_THUNK_DATA)((BYTE*)hMod + importDesc->FirstThunk);

                while (thunkILT->u1.AddressOfData)
                {
                    if ((thunkILT->u1.AddressOfData & 0x8000000000000000ui64) == 0)
                    {
                        PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hMod + thunkILT->u1.AddressOfData);
                        FARPROC expectedAddr = GetProcAddress(hModExpected, importByName->Name);
                        FARPROC currentAddr = (FARPROC)thunkIAT->u1.Function;

                        if (expectedAddr && currentAddr != expectedAddr)
                        {
                            g_iatTampered.store(true);
                            return;
                        }
                    }

                    thunkILT++;
                    thunkIAT++;
                }
                importDesc++;
            }
        }

        bool IsIATTampered() { return g_iatTampered.load(); }
    }

    namespace HookDetection
    {
        static bool DetectHook(const char* moduleName, const char* functionName)
        {
            HMODULE hMod = GetModuleHandleA(moduleName);
            if (!hMod) return false;

            void* ptr = GetProcAddress(hMod, functionName);
            if (!ptr) return false;

            unsigned char* b = (unsigned char*)ptr;

            if (b[0] == 0xE9 || b[0] == 0xE8 ||
                (b[0] == 0xFF && b[1] == 0x25) ||
                (b[0] == 0x48 && b[1] == 0x89 && b[2] == 0x05))
                return true;

            return false;
        }

        bool DetectMinHook()
        {
            const char* modules[] = { "ntdll.dll", "kernel32.dll", "kernelbase.dll", "user32.dll", NULL };
            const char* functions[] = {
                "NtQuerySystemInformation", "NtQueryInformationProcess",
                "IsDebuggerPresent", "CheckRemoteDebuggerPresent",
                "OpenProcess", "ReadProcessMemory",
                "WriteProcessMemory", "VirtualProtectEx", NULL
            };

            std::string mods[4] = {
                OBFUSCATE_STRING("ntdll.dll"),
                OBFUSCATE_STRING("kernel32.dll"),
                OBFUSCATE_STRING("kernelbase.dll"),
                OBFUSCATE_STRING("user32.dll")
            };
            std::string funcs[8] = {
                OBFUSCATE_STRING("NtQuerySystemInformation"),
                OBFUSCATE_STRING("NtQueryInformationProcess"),
                OBFUSCATE_STRING("IsDebuggerPresent"),
                OBFUSCATE_STRING("CheckRemoteDebuggerPresent"),
                OBFUSCATE_STRING("OpenProcess"),
                OBFUSCATE_STRING("ReadProcessMemory"),
                OBFUSCATE_STRING("WriteProcessMemory"),
                OBFUSCATE_STRING("VirtualProtectEx")
            };

            for (int m = 0; modules[m]; m++)
            {
                HMODULE hMod = GetModuleHandleA(mods[m].c_str());
                if (!hMod) continue;

                for (int f = 0; functions[f]; f++)
                {
                    void* ptr = GetProcAddress(hMod, funcs[f].c_str());
                    if (!ptr) continue;

                    unsigned char* b = (unsigned char*)ptr;
                    if (b[0] == 0xE9 || b[0] == 0xE8 ||
                        (b[0] == 0xFF && b[1] == 0x25) ||
                        (b[0] == 0x48 && b[1] == 0x89 && b[2] == 0x05))
                        return true;
                }
            }
            return false;
        }

        bool DetectIATHooks() { return IATProtection::IsIATTampered(); }

        bool DetectInlineHooks()
        {
            const char* criticalFunctions[] = {
                "NtQuerySystemInformation", "NtQueryInformationProcess",
                "IsDebuggerPresent", "CheckRemoteDebuggerPresent",
                "OpenProcess", "ReadProcessMemory",
                "WriteProcessMemory", "VirtualProtectEx", NULL
            };
            const char* modules[] = {
                "ntdll.dll", "kernel32.dll", "kernelbase.dll", NULL
            };

            std::string mods[3] = {
                OBFUSCATE_STRING("ntdll.dll"),
                OBFUSCATE_STRING("kernel32.dll"),
                OBFUSCATE_STRING("kernelbase.dll")
            };
            std::string funcs[8] = {
                OBFUSCATE_STRING("NtQuerySystemInformation"),
                OBFUSCATE_STRING("NtQueryInformationProcess"),
                OBFUSCATE_STRING("IsDebuggerPresent"),
                OBFUSCATE_STRING("CheckRemoteDebuggerPresent"),
                OBFUSCATE_STRING("OpenProcess"),
                OBFUSCATE_STRING("ReadProcessMemory"),
                OBFUSCATE_STRING("WriteProcessMemory"),
                OBFUSCATE_STRING("VirtualProtectEx")
            };

            for (int m = 0; modules[m]; m++)
                for (int f = 0; criticalFunctions[f]; f++)
                    if (DetectHook(mods[m].c_str(), funcs[f].c_str()))
                        return true;
            return false;
        }

        void MonitorThread()
        {
            CrashLog::Log(("THREAD RuntimeProtection::MonitorThread tid=" + std::to_string(GetCurrentThreadId())).c_str());
            while (g_protectionActive.load())
            {
                // Keep the HardenedCore supervisor alive; staleness = dead = tamper.
                HardenedCore::HeartbeatMonitorTick();

                if (DetectMinHook() || DetectIATHooks() || DetectInlineHooks())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (!AntiDump::ValidateIntegrity())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (PEBCheck::IsBeingDebugged())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (HardenedCore::DebuggerDetectedSyscall())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (TimingCheck::DetectTimingAnomaly())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (DLLInjection::DetectInjectedModules())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (!FileIntegrity::CheckExeIntegrity())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (HardwareBreakpoint::DetectHardwareBreakpoints())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (!AntiDump::ValidateIntegrity())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }
                if (SecurePayload::IsKeyValid() && !ServerSecureClient::VerifySessionIntegrity())
                {
                    HardenedCore::OnTamperDetected();
                    break;
                }

                // Jittered sleep: patching a fixed Sleep(5000) argument no longer helps.
                HardenedCore::HardenedSleep(4000);
            }
        }

        void StartMonitor()
        {
            g_protectionActive.store(true);
            g_hookMonitorThread = std::thread(MonitorThread);
            g_hookMonitorThread.detach();
        }

        void StopMonitor() { g_protectionActive.store(false); }
    }

    namespace AntiDebug
    {
        static std::atomic<bool> g_debuggerDetected{ false };

        void Enable() 
        { 
            g_debuggerDetected.store(::IsDebuggerPresent() != 0);
        }
        bool IsDebuggerPresent() { return g_debuggerDetected.load() || ::IsDebuggerPresent() != 0; }
        void CheckRemoteDebugger()
        {
            BOOL isDebugged = FALSE;
            CheckRemoteDebuggerPresent(GetCurrentProcess(), &isDebugged);
            if (isDebugged) g_debuggerDetected.store(true);
        }
    }

    namespace MemoryProtection
    {
        void EncryptCriticalSections()
        {
            HMODULE hMod = GetModuleHandleA(NULL);
            if (!hMod) return;

            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
            PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

            for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
            {
                if (strcmp((const char*)sectionHeader[i].Name, ".text") == 0)
                {
                    DWORD oldProtect;
                    VirtualProtect(
                        (BYTE*)hMod + sectionHeader[i].VirtualAddress,
                        sectionHeader[i].Misc.VirtualSize,
                        PAGE_EXECUTE_READ, &oldProtect);
                }
            }
        }

        void ValidateCodeIntegrity()
        {
            if (!AntiDump::ValidateIntegrity())
                g_iatTampered.store(true);
        }
    }

    namespace SingleInstance
    {
        static HANDLE g_hMutex = NULL;
        static bool g_acquired = false;

        bool CheckAndAcquire()
        {
            char mu[] = { '{'^0x33,'E'^0x33,'0'^0x33,'B'^0x33,'2'^0x33,'E'^0x33,'E'^0x33,'4'^0x33,'9'^0x33,'-'^0x33,'C'^0x33,'6'^0x33,'8'^0x33,'6'^0x33,'-'^0x33,'4'^0x33,'4'^0x33,'3'^0x33,'8'^0x33,'-'^0x33,'8'^0x33,'6'^0x33,'5'^0x33,'7'^0x33,'-'^0x33,'A'^0x33,'C'^0x33,'8'^0x33,'B'^0x33,'8'^0x33,'E'^0x33,'5'^0x33,'0'^0x33,'A'^0x33,'2'^0x33,'F'^0x33,'F'^0x33,'}'^0x33, 0 };
            for (int i = 0; i < 38; i++) mu[i] ^= 0x33;
            g_hMutex = CreateMutexA(NULL, FALSE, mu);
            if (g_hMutex == NULL) return true;
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                CloseHandle(g_hMutex);
                g_hMutex = NULL;
                return false;
            }
            g_acquired = true;
            return true;
        }
    }

    namespace AntiVM
    {
        static bool CheckRegistry()
        {
            const std::string keys[] = {
                OBFUSCATE_STRING("SOFTWARE\\VMware, Inc.\\VMware Tools"),
                OBFUSCATE_STRING("SOFTWARE\\Oracle\\VirtualBox Guest Additions"),
                OBFUSCATE_STRING("SYSTEM\\CurrentControlSet\\Services\\VBoxGuest"),
                OBFUSCATE_STRING("SYSTEM\\CurrentControlSet\\Services\\vmci"),
                OBFUSCATE_STRING("SYSTEM\\CurrentControlSet\\Services\\vmhgfs"),
            };
            for (int i = 0; i < 5; i++)
            {
                HKEY hKey;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keys[i].c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
                {
                    RegCloseKey(hKey);
                    return true;
                }
            }
            return false;
        }

        static bool CheckFiles()
        {
            const std::string files[] = {
                OBFUSCATE_STRING("C:\\Windows\\System32\\vmGuestLib.dll"),
                OBFUSCATE_STRING("C:\\Windows\\System32\\vboxdisp.dll"),
                OBFUSCATE_STRING("C:\\Windows\\System32\\vboxhook.dll"),
                OBFUSCATE_STRING("C:\\Windows\\System32\\vboxogl.dll"),
                OBFUSCATE_STRING("C:\\Windows\\System32\\vm3dmp.dll"),
                OBFUSCATE_STRING("C:\\Windows\\System32\\vmhgfs.dll"),
                OBFUSCATE_STRING("\\\\.\\VBoxMiniRdrDN"),
                OBFUSCATE_STRING("\\\\.\\VBoxGuest"),
                OBFUSCATE_STRING("\\\\.\\VBoxTrayIPC"),
                OBFUSCATE_STRING("\\\\.\\VBoxSVGA"),
            };
            for (int i = 0; i < 10; i++)
            {
                DWORD attr = GetFileAttributesA(files[i].c_str());
                if (attr != INVALID_FILE_ATTRIBUTES)
                    return true;
            }
            return false;
        }

        static bool CheckProcesses()
        {
            const std::string procs[] = {
                OBFUSCATE_STRING("vmtoolsd.exe"), OBFUSCATE_STRING("vmwaretray.exe"),
                OBFUSCATE_STRING("vmwareuser.exe"), OBFUSCATE_STRING("VGService.exe"),
                OBFUSCATE_STRING("VGClient.exe"), OBFUSCATE_STRING("vmacthlp.exe"),
                OBFUSCATE_STRING("vboxservice.exe"), OBFUSCATE_STRING("vboxtray.exe"),
            };
            HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (h == INVALID_HANDLE_VALUE) return false;
            PROCESSENTRY32W e;
            e.dwSize = sizeof(PROCESSENTRY32W);
            bool found = false;
            if (Process32FirstW(h, &e))
            {
                do
                {
                    for (int i = 0; i < 8; i++)
                    {
                        wchar_t wproc[64];
                        MultiByteToWideChar(CP_ACP, 0, procs[i].c_str(), -1, wproc, 64);
                        if (_wcsicmp(e.szExeFile, wproc) == 0)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                } while (Process32NextW(h, &e));
            }
            CloseHandle(h);
            return found;
        }

        static bool CheckCPUID()
        {
            int cpuInfo[4] = { 0 };
            __cpuid(cpuInfo, 0x40000000);
            char vendor[13] = { 0 };
            *(int*)(vendor) = cpuInfo[1];
            *(int*)(vendor + 4) = cpuInfo[2];
            *(int*)(vendor + 8) = cpuInfo[3];
            if (strstr(vendor, OBFUSCATE_STRING("VMwareVMware").c_str()) ||
                strstr(vendor, OBFUSCATE_STRING("VBoxVBoxVBox").c_str()) ||
                strstr(vendor, OBFUSCATE_STRING("KVMKVMKVM").c_str()))
                return true;
            return false;
        }

        static bool CheckMACAddress()
        {
            IP_ADAPTER_INFO adapterInfo[16];
            DWORD bufLen = sizeof(adapterInfo);
            DWORD status = GetAdaptersInfo(adapterInfo, &bufLen);
            if (status != ERROR_SUCCESS) return false;
            PIP_ADAPTER_INFO pAdapter = adapterInfo;
            while (pAdapter)
            {
                if (pAdapter->AddressLength == 6)
                {
                    BYTE* mac = pAdapter->Address;
                    if (mac[0] == 0x00 && mac[1] == 0x0C && mac[2] == 0x29) return true;
                    if (mac[0] == 0x00 && mac[1] == 0x50 && mac[2] == 0x56) return true;
                    if (mac[0] == 0x08 && mac[1] == 0x00 && mac[2] == 0x27) return true;
                }
                pAdapter = pAdapter->Next;
            }
            return false;
        }

        bool IsVirtualMachine()
        {
            OBF_BOGUS_FLOW_GRID();
            if (CheckRegistry()) return true;
            if (CheckFiles()) return true;
            if (CheckProcesses()) return true;
            if (CheckCPUID()) return true;
            if (CheckMACAddress()) return true;

            MEMORYSTATUSEX memStat;
            memStat.dwLength = sizeof(memStat);
            if (GlobalMemoryStatusEx(&memStat))
            {
                DWORDLONG totalRAM = memStat.ullTotalPhys / (1024 * 1024 * 1024);
                if (totalRAM < 4) return true;
            }

            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            if (sysInfo.dwNumberOfProcessors < 2) return true;

            ULARGE_INTEGER totalDisk;
            if (GetDiskFreeSpaceExA(OBFUSCATE_STRING("C:\\").c_str(), NULL, &totalDisk, NULL))
            {
                DWORDLONG totalGB = totalDisk.QuadPart / (1024ULL * 1024 * 1024);
                if (totalGB < 80) return true;
            }

            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                OBFUSCATE_STRING("SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}").c_str(),
                0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                char driverDesc[256] = { 0 };
                DWORD size = sizeof(driverDesc);
                if (RegQueryValueExA(hKey, OBFUSCATE_STRING("DriverDesc").c_str(), NULL, NULL, (LPBYTE)driverDesc, &size) == ERROR_SUCCESS)
                {
                    if (strstr(driverDesc, OBFUSCATE_STRING("VMware").c_str()) ||
                        strstr(driverDesc, OBFUSCATE_STRING("Virtual").c_str()) ||
                        strstr(driverDesc, OBFUSCATE_STRING("Hyper-V").c_str()))
                    {
                        RegCloseKey(hKey);
                        return true;
                    }
                }
                RegCloseKey(hKey);
            }

            return false;
        }
    }

    namespace AntiAttach
    {
        typedef NTSTATUS(NTAPI* pNtSetInformationThread)(HANDLE, UINT, PVOID, ULONG);
        typedef NTSTATUS(NTAPI* pNtGetContextThread)(HANDLE, PVOID);
        static const UINT ThreadHideFromDebugger = 0x11;

        void HideAllThreads()
        {
            OBF_BOGUS_FLOW_LABYRINTH();
            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return;

            auto NtSetInformationThread = (pNtSetInformationThread)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtSetInformationThread").c_str());
            if (!NtSetInformationThread) return;

            NtSetInformationThread(GetCurrentThread(), ThreadHideFromDebugger, NULL, 0);

            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (hSnap == INVALID_HANDLE_VALUE) return;

            DWORD myPid = GetCurrentProcessId();
            THREADENTRY32 te;
            te.dwSize = sizeof(THREADENTRY32);

            if (Thread32First(hSnap, &te))
            {
                do
                {
                    if (te.th32OwnerProcessID == myPid)
                    {
                        HANDLE hThread = OpenThread(THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                        if (hThread)
                        {
                            NtSetInformationThread(hThread, ThreadHideFromDebugger, NULL, 0);
                            CloseHandle(hThread);
                        }
                    }
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }

        void HideNewThread(HANDLE hThread)
        {
            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return;

            auto NtSetInformationThread = (pNtSetInformationThread)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtSetInformationThread").c_str());
            if (NtSetInformationThread)
                NtSetInformationThread(hThread, ThreadHideFromDebugger, NULL, 0);
        }
    }

    namespace AntiBreakpoint
    {
        static std::atomic<bool> g_breakpointsDetected{ false };
        static std::atomic<bool> g_scanActive{ false };

        bool ScanForBreakpoints()
        {
            HMODULE hMod = GetModuleHandleA(NULL);
            if (!hMod) return false;

            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
            PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

            for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
            {
                if (strcmp((const char*)sectionHeader[i].Name, ".text") == 0)
                {
                    BYTE* code = (BYTE*)hMod + sectionHeader[i].VirtualAddress;
                    DWORD size = sectionHeader[i].Misc.VirtualSize;
                    for (DWORD j = 0; j < size; j++)
                    {
                        if (code[j] == 0xCC)
                        {
                            g_breakpointsDetected.store(true);
                            return true;
                        }
                    }
                    break;
                }
            }
            return false;
        }

        bool IsBreakpointDetected() { return g_breakpointsDetected.load(); }

        void ScanThread()
        {
            CrashLog::Log(("THREAD RuntimeProtection::ScanThread tid=" + std::to_string(GetCurrentThreadId())).c_str());
            g_scanActive.store(true);
            while (g_scanActive.load())
            {
                ScanForBreakpoints();
                Sleep(10000);
            }
        }

        void StartScan()
        {
            std::thread t(ScanThread);
            t.detach();
        }

        void StopScan() { g_scanActive.store(false); }
    }

    namespace HardwareBreakpoint
    {
        typedef NTSTATUS(NTAPI* pNtGetContextThread)(HANDLE, PVOID);

        bool DetectHardwareBreakpoints()
        {
            OBF_BOGUS_FLOW_WEAVER();
            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return false;

            auto NtGetContextThread = (pNtGetContextThread)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtGetContextThread").c_str());
            if (!NtGetContextThread) return false;

            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            NTSTATUS status = NtGetContextThread(GetCurrentThread(), &ctx);
            if (status >= 0)
            {
                if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0)
                    return true;
            }
            return false;
        }
    }

    namespace PEBCheck
    {
        typedef struct _PEB {
            BYTE Reserved1[2];
            BYTE BeingDebugged;
            BYTE Reserved2[1];
            PVOID Mutant;
        } PEB, *PPEB;

        bool IsBeingDebugged()
        {
            OBF_BOGUS_FLOW_WEAVER();
            PPEB peb = (PPEB)__readgsqword(0x60);
            if (peb && peb->BeingDebugged)
                return true;

            DWORD* ntGlobalFlag = (DWORD*)((BYTE*)peb + 0xBC);
            if (*ntGlobalFlag & 0x70)
                return true;

            return false;
        }
    }

    namespace TimingCheck
    {
        bool DetectTimingAnomaly()
        {
            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);

            volatile int x = 0;
            for (volatile int i = 0; i < 1000; i++)
                x += i;

            QueryPerformanceCounter(&end);

            double elapsed = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
            if (elapsed > 0.5)
                return true;

            return false;
        }
    }

    namespace DLLInjection
    {
        bool DetectInjectedModules()
        {
            OBF_BOGUS_FLOW_GRID();
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            wchar_t* lastSlash = wcsrchr(exePath, L'\\');
            if (lastSlash) *lastSlash = L'\0';

            wchar_t sysDir[MAX_PATH];
            GetSystemDirectoryW(sysDir, MAX_PATH);

            wchar_t winDir[MAX_PATH];
            GetWindowsDirectoryW(winDir, MAX_PATH);

            wchar_t progFiles[MAX_PATH];
            GetEnvironmentVariableW(L"ProgramFiles", progFiles, MAX_PATH);

            wchar_t progFilesX86[MAX_PATH];
            GetEnvironmentVariableW(L"ProgramFiles(x86)", progFilesX86, MAX_PATH);

            wchar_t localAppData[MAX_PATH];
            GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);

            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
            if (hSnap == INVALID_HANDLE_VALUE) return false;

            MODULEENTRY32W me;
            me.dwSize = sizeof(MODULEENTRY32W);
            bool injected = false;

            if (Module32FirstW(hSnap, &me))
            {
                do
                {
                    if (me.modBaseAddr == (BYTE*)GetModuleHandleA(NULL))
                        continue;

                    bool safe = false;

                    if (_wcsnicmp(me.szExePath, sysDir, wcslen(sysDir)) == 0)
                        safe = true;
                    else if (_wcsnicmp(me.szExePath, winDir, wcslen(winDir)) == 0)
                        safe = true;
                    else if (_wcsnicmp(me.szExePath, exePath, wcslen(exePath)) == 0)
                        safe = true;
                    else if (_wcsnicmp(me.szExePath, progFiles, wcslen(progFiles)) == 0)
                        safe = true;
                    else if (wcslen(progFilesX86) > 0 && _wcsnicmp(me.szExePath, progFilesX86, wcslen(progFilesX86)) == 0)
                        safe = true;
                    else if (_wcsnicmp(me.szExePath, localAppData, wcslen(localAppData)) == 0)
                        safe = true;

                    if (!safe)
                    {
                        injected = true;
                        break;
                    }
                } while (Module32NextW(hSnap, &me));
            }
            CloseHandle(hSnap);
            return injected;
        }
    }

    namespace AntiDumpProtection
    {
        static void DenyProcessAccess()
        {
            HANDLE hToken;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
                return;

            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!LookupPrivilegeValueA(NULL, OBFUSCATE_STRING("SeDebugPrivilege").c_str(), &tp.Privileges[0].Luid))
            {
                CloseHandle(hToken);
                return;
            }
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
            CloseHandle(hToken);

            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return;

            typedef LONG NTSTATUS;
            typedef struct _UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } UNICODE_STRING;

            typedef NTSTATUS(NTAPI* pRtlCreateSecurityDescriptor)(PVOID, ULONG, ULONG, PVOID, PVOID);
            typedef NTSTATUS(NTAPI* pRtlSetDaclSecurityDescriptor)(PVOID, BOOLEAN, PVOID, BOOLEAN);
            typedef NTSTATUS(NTAPI* pNtSetInformationProcess)(HANDLE, UINT, PVOID, ULONG);

            auto RtlCreateSecurityDescriptor = (pRtlCreateSecurityDescriptor)GetProcAddress(hNtdll, OBFUSCATE_STRING("RtlCreateSecurityDescriptor").c_str());
            auto RtlSetDaclSecurityDescriptor = (pRtlSetDaclSecurityDescriptor)GetProcAddress(hNtdll, OBFUSCATE_STRING("RtlSetDaclSecurityDescriptor").c_str());
            auto NtSetInformationProcess = (pNtSetInformationProcess)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtSetInformationProcess").c_str());
            if (!RtlCreateSecurityDescriptor || !RtlSetDaclSecurityDescriptor || !NtSetInformationProcess) return;

            BYTE sd[64]{};
            RtlCreateSecurityDescriptor(sd, sizeof(sd), 1, NULL, NULL);

            BYTE acl[256]{};
            USHORT aclSize = sizeof(acl);

            typedef struct _ACL { BYTE AclRevision; BYTE Sbz1; USHORT AclSize; USHORT AceCount; WORD Sbz2; } ACL;
            typedef struct _ACE_HEADER { BYTE AceType; BYTE AceFlags; WORD AceSize; } ACE_HEADER;
            typedef struct _ACCESS_ALLOWED_ACE { ACE_HEADER Header; ACCESS_MASK Mask; DWORD SidStart; } ACCESS_ALLOWED_ACE;

            ACL* pAcl = (ACL*)acl;
            pAcl->AclRevision = 2;
            pAcl->Sbz1 = 0;
            pAcl->AceCount = 1;
            pAcl->Sbz2 = 0;

            BYTE sidEveryone[12]{};
            sidEveryone[0] = 1;
            sidEveryone[1] = 1;
            *(DWORD*)(sidEveryone + 2) = 0;

            ACCESS_ALLOWED_ACE* pAce = (ACCESS_ALLOWED_ACE*)(acl + sizeof(ACL));
            pAce->Header.AceType = 0;
            pAce->Header.AceFlags = 0;
            pAce->Header.AceSize = sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + 12;
            pAce->Mask = PROCESS_TERMINATE | PROCESS_CREATE_THREAD | PROCESS_CREATE_PROCESS |
                         PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION |
                         PROCESS_SUSPEND_RESUME;
            memcpy(&pAce->SidStart, sidEveryone, 12);

            pAcl->AclSize = sizeof(ACL) + pAce->Header.AceSize;

            RtlSetDaclSecurityDescriptor(sd, FALSE, acl, FALSE);

            NtSetInformationProcess(GetCurrentProcess(), 32, sd, sizeof(sd));
        }

        static void RestrictProcessPolicy()
        {
            typedef BOOL(WINAPI* pSetProcessMitigationPolicy)(DWORD, PVOID, SIZE_T);
            HMODULE hKernel = GetModuleHandleA(OBFUSCATE_STRING("kernel32.dll").c_str());
            if (!hKernel) return;

            auto SetProcessMitigationPolicy = (pSetProcessMitigationPolicy)GetProcAddress(hKernel, OBFUSCATE_STRING("SetProcessMitigationPolicy").c_str());
            if (!SetProcessMitigationPolicy) return;

            struct {
                DWORD Flags;
                BOOL AllowRemoteDowngrade;
                DWORD AuditNonMicrosoftBinaries;
            } dynamicCodePolicy{};
            dynamicCodePolicy.Flags = 1;
            dynamicCodePolicy.AllowRemoteDowngrade = FALSE;
            dynamicCodePolicy.AuditNonMicrosoftBinaries = 1;
            SetProcessMitigationPolicy(0, &dynamicCodePolicy, sizeof(dynamicCodePolicy));

            struct {
                DWORD Flags;
            } restrictIndirectBranchPrediction{};
            restrictIndirectBranchPrediction.Flags = 1;
            SetProcessMitigationPolicy(10, &restrictIndirectBranchPrediction, sizeof(restrictIndirectBranchPrediction));
        }

        void Enable()
        {
            OBF_BOGUS_FLOW_LABYRINTH();
            DenyProcessAccess();
            // RestrictProcessPolicy - OVO BLOKIRA OBFUSK8 DINAMICKI KOD!
            // RestrictProcessPolicy();
        }

        void ScanAndKill() {}
    }

    void Initialize()
    {
        AntiDump::Enable();
        AntiDebug::Enable();
        AntiAttach::HideAllThreads();
        MemoryProtection::EncryptCriticalSections();
        FileIntegrity::StoreOriginalHash();
        AntiDumpProtection::Enable();

        // Wire the previously-dead defensive layers.
        ThreadAutoHide::Enable();
        AntiDumpAdvanced::SetDebugPortZero();
    }

    void StartProtectionThreads()
    {
        HookDetection::StartMonitor();
    }

    namespace AntiDumpAdvanced
    {
        typedef NTSTATUS(NTAPI* pNtSetInformationProcess)(HANDLE, UINT, PVOID, ULONG);
        static const UINT ProcessDebugPort = 7;

        void ErasePEHeader()
        {
            HMODULE hMod = GetModuleHandleA(NULL);
            if (!hMod) return;

            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
            DWORD oldProtect;
            VirtualProtect(hMod, sizeof(IMAGE_DOS_HEADER), PAGE_READWRITE, &oldProtect);
            SecureZeroMemory(hMod, sizeof(IMAGE_DOS_HEADER));
        }

        void SetDebugPortZero()
        {
            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return;

            auto NtSetInformationProcess = (pNtSetInformationProcess)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtSetInformationProcess").c_str());
            if (!NtSetInformationProcess) return;

            DWORD debugPort = 0;
            NtSetInformationProcess(GetCurrentProcess(), ProcessDebugPort, &debugPort, sizeof(debugPort));
        }
    }

    namespace ProcessObfuscation
    {
        void RenameToDecoy()
        {
            OBF_BOGUS_FLOW_GRID();
            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return;

            typedef LONG NTSTATUS;
            typedef struct _UNICODE_STRING {
                USHORT Length;
                USHORT MaximumLength;
                PWSTR  Buffer;
            } UNICODE_STRING;

            typedef struct _PROCESS_BASIC_INFORMATION {
                NTSTATUS ExitStatus;
                PVOID PebBaseAddress;
                ULONG_PTR AffinityMask;
                LONG BasePriority;
                ULONG_PTR UniqueProcessId;
                ULONG_PTR InheritedFromUniqueProcessId;
            } PROCESS_BASIC_INFORMATION;

            typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(HANDLE, UINT, PVOID, ULONG, PULONG);
            auto NtQueryInformationProcess = (pNtQueryInformationProcess)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtQueryInformationProcess").c_str());
            if (!NtQueryInformationProcess) return;

            PROCESS_BASIC_INFORMATION pbi{};
            if (NtQueryInformationProcess(GetCurrentProcess(), 0, &pbi, sizeof(pbi), NULL) != 0) return;
            if (!pbi.PebBaseAddress) return;

            BYTE* peb = (BYTE*)pbi.PebBaseAddress;
            DWORD_PTR processParams = *(DWORD_PTR*)(peb + 0x20);
            if (!processParams) return;

            UNICODE_STRING* imagePath = (UNICODE_STRING*)(processParams + 0x60);
            UNICODE_STRING* cmdLine = (UNICODE_STRING*)(processParams + 0x70);

            std::wstring decoy = OBFUSCATE_WSTRING(L"svchost.exe");
            SIZE_T decoyLen = decoy.size() * sizeof(wchar_t);

            if (decoyLen <= imagePath->MaximumLength && imagePath->Buffer)
            {
                DWORD oldProt;
                if (VirtualProtect(imagePath->Buffer, decoyLen + sizeof(wchar_t), PAGE_READWRITE, &oldProt))
                {
                    memset(imagePath->Buffer, 0, imagePath->Length);
                    memcpy(imagePath->Buffer, decoy.c_str(), decoyLen);
                    imagePath->Length = (USHORT)decoyLen;
                    VirtualProtect(imagePath->Buffer, decoyLen + sizeof(wchar_t), oldProt, &oldProt);
                }
            }

            std::wstring decoyCmd = OBFUSCATE_WSTRING(L"svchost.exe -k netsvcs");
            SIZE_T cmdLen = decoyCmd.size() * sizeof(wchar_t);
            if (cmdLen <= cmdLine->MaximumLength && cmdLine->Buffer)
            {
                DWORD oldProt;
                if (VirtualProtect(cmdLine->Buffer, cmdLen + sizeof(wchar_t), PAGE_READWRITE, &oldProt))
                {
                    memset(cmdLine->Buffer, 0, cmdLine->Length);
                    memcpy(cmdLine->Buffer, decoyCmd.c_str(), cmdLen);
                    cmdLine->Length = (USHORT)cmdLen;
                    VirtualProtect(cmdLine->Buffer, cmdLen + sizeof(wchar_t), oldProt, &oldProt);
                }
            }
        }
    }

    namespace ThreadAutoHide
    {
        typedef NTSTATUS(NTAPI* pNtSetInformationThread)(HANDLE, UINT, PVOID, ULONG);
        static const UINT ThreadHideFromDebugger = 0x11;
        static std::atomic<bool> g_active{ false };
        static std::atomic<bool> g_monitorRunning{ false };

        static void HideNewThreads()
        {
            HMODULE hNtdll = GetModuleHandleA(OBFUSCATE_STRING("ntdll.dll").c_str());
            if (!hNtdll) return;
            auto NtSetInformationThread = (pNtSetInformationThread)GetProcAddress(hNtdll, OBFUSCATE_STRING("NtSetInformationThread").c_str());
            if (!NtSetInformationThread) return;

            static std::vector<DWORD> knownThreads;

            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (hSnap == INVALID_HANDLE_VALUE) return;

            DWORD myPid = GetCurrentProcessId();
            THREADENTRY32 te;
            te.dwSize = sizeof(THREADENTRY32);

            if (Thread32First(hSnap, &te))
            {
                do
                {
                    if (te.th32OwnerProcessID == myPid)
                    {
                        bool isNew = true;
                        for (DWORD tid : knownThreads)
                        {
                            if (tid == te.th32ThreadID) { isNew = false; break; }
                        }
                        if (isNew)
                        {
                            knownThreads.push_back(te.th32ThreadID);
                            HANDLE hThread = OpenThread(THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                            if (hThread)
                            {
                                NtSetInformationThread(hThread, ThreadHideFromDebugger, NULL, 0);
                                CloseHandle(hThread);
                            }
                        }
                    }
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }

        static void MonitorFunc()
        {
            CrashLog::Log(("THREAD RuntimeProtection::MonitorFunc tid=" + std::to_string(GetCurrentThreadId())).c_str());
            g_monitorRunning.store(true);
            while (g_active.load())
            {
                HideNewThreads();
                Sleep(1000);
            }
            g_monitorRunning.store(false);
        }

        void Enable()
        {
            OBF_BOGUS_FLOW_LABYRINTH();
            g_active.store(true);
            HideNewThreads();
            std::thread(MonitorFunc).detach();
        }

        void Disable()
        {
            g_active.store(false);
            while (g_monitorRunning.load()) Sleep(10);
        }
    }

    namespace FileIntegrity
    {
        static DWORD g_originalCRC = 0;

        void StoreOriginalHash()
        {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);

            HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile == INVALID_HANDLE_VALUE) return;

            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize == INVALID_FILE_SIZE || fileSize > 100 * 1024 * 1024)
            {
                CloseHandle(hFile);
                return;
            }

            std::vector<BYTE> fileData(fileSize);
            DWORD bytesRead;
            ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
            CloseHandle(hFile);

            uint32_t crc = 0xFFFFFFFF;
            for (DWORD i = 0; i < bytesRead; i++)
            {
                crc ^= fileData[i];
                for (int j = 0; j < 8; j++)
                    crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
            }
            g_originalCRC = ~crc;
        }

        bool CheckExeIntegrity()
        {
            if (g_originalCRC == 0) return true;

            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);

            HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile == INVALID_HANDLE_VALUE) return true;

            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize == INVALID_FILE_SIZE || fileSize > 100 * 1024 * 1024)
            {
                CloseHandle(hFile);
                return true;
            }

            std::vector<BYTE> fileData(fileSize);
            DWORD bytesRead;
            ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
            CloseHandle(hFile);

            uint32_t crc = 0xFFFFFFFF;
            for (DWORD i = 0; i < bytesRead; i++)
            {
                crc ^= fileData[i];
                for (int j = 0; j < 8; j++)
                    crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
            }
            DWORD currentCRC = ~crc;
            return currentCRC == g_originalCRC;
        }
    }

    void StopProtection()
    {
        g_protectionActive.store(false);
        AntiDump::Disable();
        HookDetection::StopMonitor();
        if (SingleInstance::g_hMutex)
        {
            ReleaseMutex(SingleInstance::g_hMutex);
            CloseHandle(SingleInstance::g_hMutex);
            SingleInstance::g_hMutex = NULL;
        }
    }
}
