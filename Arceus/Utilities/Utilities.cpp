#include "Utilities.h"
#include "Strings.hpp"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"
#include "../Core/KeyauxClient.h"
#include "../Core/SecurePayload.h"
#include "../Core/CrashLog.h"
#include "../KeyAux/storage.hpp"

#include <tlhelp32.h>
#include <thread>
#include <algorithm>
#include <fstream>
#include <comdef.h>
#include <WbemIdl.h>
#include <cstdio>
#include <memory>
#include <array>
#include <wininet.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "wininet.lib")

#include "VirtualizerSDK.h"
#include "StealthArea.hpp"
#include "RuntimeProtection.h"
#include "LicenseGuard.h"

namespace Utility 
{
    std::filesystem::path GetExecutableDirectory()
    {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) 
            return L"";

        return std::filesystem::path(path).parent_path();
    }

    void SaveFileToDisk(std::filesystem::path path, const std::string fileName, const std::vector<unsigned char>& bytes)
    {
        std::filesystem::path outputPath = path / fileName;
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
            return;

        outFile.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        outFile.close();
    }

    bool FileExists(const std::filesystem::path& dir, const std::string& filename)
    {
        std::filesystem::path full = dir / filename;

        if (!std::filesystem::exists(full))
            return false;
        if (!std::filesystem::is_regular_file(full))
            return false;
        if (std::filesystem::file_size(full) == 0)
            return false;

        return true;
    }

    bool LoadFileToVector(const std::wstring& path, std::vector<unsigned char>& outData)
    {
        HANDLE hFile = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0)
        {
            CloseHandle(hFile);
            return false;
        }

        outData.resize(static_cast<size_t>(fileSize.QuadPart));

        DWORD bytesRead = 0;
        BOOL success = ReadFile(hFile, outData.data(), static_cast<DWORD>(fileSize.QuadPart), &bytesRead, nullptr);

        CloseHandle(hFile);

        return success && bytesRead == fileSize.QuadPart;
    }

    bool DecryptBuffer(const void* encryptedBuffer, size_t encryptedSize, std::vector<BYTE>& decryptedBuffer)
    {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        HCRYPTKEY hKey = 0;

        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return false;

        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) 
        {
            CryptReleaseContext(hProv, 0);
            return false;
        }

        if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(SSecurity::EncPass.get().c_str()), (DWORD)SSecurity::EncPass.get().size(), 0)) 
        {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return false;
        }

        if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey))
        {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return false;
        }

        decryptedBuffer.resize(encryptedSize);
        std::memcpy(decryptedBuffer.data(), encryptedBuffer, encryptedSize);

        DWORD dataLen = static_cast<DWORD>(encryptedSize);
        if (!CryptDecrypt(hKey, 0, TRUE, 0, decryptedBuffer.data(), &dataLen))
        {
            CryptDestroyKey(hKey);
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return false;
        }

        decryptedBuffer.resize(static_cast<size_t>(dataLen));

        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return true;
    }

    float ComputeIoU(const Target& a, const Target& b)
    {
        const float areaA = static_cast<float>(a.BBox[W]) * static_cast<float>(a.BBox[H]);
        const float areaB = static_cast<float>(b.BBox[W]) * static_cast<float>(b.BBox[H]);

        if (areaA <= 0 || areaB <= 0)
            return 0.0f;

        const float intersectionX1 = (std::max)(static_cast<float>(a.BBox[X]), static_cast<float>(b.BBox[X]));
        const float intersectionY1 = (std::max)(static_cast<float>(a.BBox[Y]), static_cast<float>(b.BBox[Y]));
        const float intersectionX2 = (std::min)(static_cast<float>(a.BBox[X]) + static_cast<float>(a.BBox[W]), static_cast<float>(b.BBox[X]) + static_cast<float>(b.BBox[W]));
        const float intersectionY2 = (std::min)(static_cast<float>(a.BBox[Y]) + static_cast<float>(a.BBox[H]), static_cast<float>(b.BBox[Y]) + static_cast<float>(b.BBox[H]));

        const float intersectionWidth = (std::max)(0.0f, intersectionX2 - intersectionX1);
        const float intersectionHeight = (std::max)(0.0f, intersectionY2 - intersectionY1);
        const float intersectionArea = intersectionWidth * intersectionHeight;
        const float unionArea = areaA + areaB - intersectionArea;

        if (unionArea <= 0)
            return 0.0f;

        return intersectionArea / unionArea;
    }

    void PerformNMS(TargetVector& candidates, const float nms_threshold)
    {
        if (candidates.empty()) return;

        std::sort(candidates.begin(), candidates.end(), [](const Target& a, const Target& b)
            {
                return a.Confidence > b.Confidence;
            });

        bool suppressed[TargetVector::capacity()] = { false };

        TargetVector kept;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (suppressed[i]) continue;

            const Target& current = candidates[i];
            kept.push_back(current);

            for (size_t j = i + 1; j < candidates.size(); ++j)
            {
                if (suppressed[j]) continue;

                if (ComputeIoU(current, candidates[j]) > nms_threshold)
                    suppressed[j] = true;
            }
        }

        candidates = kept;
    }
}

namespace Security
{ 
    static std::atomic<bool> g_securityViolation{ false };
    static std::string g_securityMessage;

    void ThrowIfAntiCheats(const std::vector<std::wstring>& processNames)
    {
VIRTUALIZER_MUTATE_ONLY_START
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (h == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W e;
        e.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(h, &e)) {
            do {
                for (const auto& t : processNames) {
                    if (_wcsicmp(e.szExeFile, t.c_str()) == 0) {
                        CloseHandle(h);
                        exit(0);
                    }
                }
            } while (Process32NextW(h, &e));
        }
        CloseHandle(h);
VIRTUALIZER_MUTATE_ONLY_END
    }

    static void BlockPython()
    {
VIRTUALIZER_MUTATE_ONLY_START
        std::vector<std::wstring> py = { OBFUSCATE_WSTRING(L"python.exe"), OBFUSCATE_WSTRING(L"pythonw.exe") };
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (h == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W e;
        e.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(h, &e)) {
            do {
                for (const auto& t : py) {
                    if (_wcsicmp(e.szExeFile, t.c_str()) == 0) {
                        HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, e.th32ProcessID);
                        if (ph) {
                            TerminateProcess(ph, 0);
                            CloseHandle(ph);
                        }
                    }
                }
            } while (Process32NextW(h, &e));
        }
        CloseHandle(h);
VIRTUALIZER_MUTATE_ONLY_END
    }

    void CheckSecurity()
    {
        // NOTE: never throw here. This is called from the App::Run hot loop and
        // from Menu::Render; exceptions raised inside Obfusk8-VM code do NOT
        // reach any C++ catch (observed 0xC0000409 terminate on every Start).
        if (g_securityViolation.load(std::memory_order_acquire)) [[unlikely]]
        {
            CrashLog::Log("Security::CheckSecurity violation flagged");
            CrashLog::Log(g_securityMessage.c_str());
            // One-shot: clear so the caller can keep running and show the state.
            g_securityViolation.store(false, std::memory_order_release);
        }
    }

    static bool CheckForHooks(const char* moduleName, const char* functionName)
    {
        HMODULE hMod = GetModuleHandleA(moduleName);
        if (!hMod) return false;

        void* ptr = GetProcAddress(hMod, functionName);
        if (!ptr) return false;

        unsigned char* b = (unsigned char*)ptr;

        if (b[0] == 0xE9 || b[0] == 0xE8 || (b[0] == 0xFF && b[1] == 0x25))
            return true;

        return false;
    }

    static uint32_t CalculateCRC32(const void* data, size_t size)
    {
        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* p = (const uint8_t*)data;
        while (size--) {
            crc ^= *p++;
            for (int i = 0; i < 8; i++)
                crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
        }
        return ~crc;
    }

    static uint32_t g_originalTextCRC = 0;

    static void InitializeAntiPatch()
    {
        HMODULE hMod = GetModuleHandleA(NULL);
        if (!hMod) return;

        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

        for (int i = 0; i < (int)ntHeaders->FileHeader.NumberOfSections; i++) {
            if (strcmp((const char*)sectionHeader[i].Name, ".text") == 0) {
                g_originalTextCRC = CalculateCRC32((BYTE*)hMod + sectionHeader[i].VirtualAddress, (size_t)sectionHeader[i].Misc.VirtualSize);
                break;
            }
        }
    }

    static bool VerifyAntiPatch()
    {
        if (g_originalTextCRC == 0) return true;

        HMODULE hMod = GetModuleHandleA(NULL);
        if (!hMod) return true;

        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

        for (int i = 0; i < (int)ntHeaders->FileHeader.NumberOfSections; i++) {
            if (strcmp((const char*)sectionHeader[i].Name, ".text") == 0) {
                uint32_t currentCRC = CalculateCRC32((BYTE*)hMod + sectionHeader[i].VirtualAddress, (size_t)sectionHeader[i].Misc.VirtualSize);
                return currentCRC == g_originalTextCRC;
            }
        }
        return true;
    }

    static uint32_t ComputeTextCrcNow()
    {
        HMODULE hMod = GetModuleHandleA(NULL);
        if (!hMod) return 0;
        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
        for (int i = 0; i < (int)ntHeaders->FileHeader.NumberOfSections; i++) {
            if (strcmp((const char*)sectionHeader[i].Name, ".text") == 0) {
                return CalculateCRC32((BYTE*)hMod + sectionHeader[i].VirtualAddress, (size_t)sectionHeader[i].Misc.VirtualSize);
            }
        }
        return 0;
    }

    OBF_METHOD(void, SecurityLoop, (const std::vector<std::wstring>& processNames), {
        CrashLog::Log(("THREAD SecurityLoop tid=" + std::to_string(GetCurrentThreadId())).c_str());
        try
        {
            InitializeAntiPatch();
            CrashLog::Log("SecurityLoop initialized (baseline taken)");

            while (true)
            {
                try
                {
                BlockPython();
                
                if (CheckForHooks("ntdll.dll", "NtQuerySystemInformation") ||
                    CheckForHooks("ntdll.dll", "NtQueryInformationProcess") ||
                    CheckForHooks("kernel32.dll", "IsDebuggerPresent") ||
                    CheckForHooks("user32.dll", "GetWindowThreadProcessId") ||
                    CheckForHooks("user32.dll", "FindWindowA"))
                {
                    CrashLog::Log("SecurityLoop: HOOK DETECTED");
                    CrashLog::Log(("  ntdll.NtQuerySystemInformation       -> " + std::string(CheckForHooks("ntdll.dll", "NtQuerySystemInformation") ? "HOOKED" : "clean")).c_str());
                    CrashLog::Log(("  ntdll.NtQueryInformationProcess      -> " + std::string(CheckForHooks("ntdll.dll", "NtQueryInformationProcess") ? "HOOKED" : "clean")).c_str());
                    CrashLog::Log(("  kernel32.IsDebuggerPresent           -> " + std::string(CheckForHooks("kernel32.dll", "IsDebuggerPresent") ? "HOOKED" : "clean")).c_str());
                    CrashLog::Log(("  user32.GetWindowThreadProcessId      -> " + std::string(CheckForHooks("user32.dll", "GetWindowThreadProcessId") ? "HOOKED" : "clean")).c_str());
                    CrashLog::Log(("  user32.FindWindowA                   -> " + std::string(CheckForHooks("user32.dll", "FindWindowA") ? "HOOKED" : "clean")).c_str());
                    throw std::runtime_error("Hook detected!");
                }

                if (!VerifyAntiPatch())
                {
                    CrashLog::Log("SecurityLoop: PATCHING DETECTED (.text CRC mismatch)");
                    CrashLog::Log(("  baseline=" + std::to_string(g_originalTextCRC)).c_str());
                    CrashLog::Log(("  current =" + std::to_string(ComputeTextCrcNow())).c_str());
                    throw std::runtime_error("Patching detected!");
                }

                Sleep(5000); 
                ThrowIfAntiCheats(processNames);
                }
                catch (...)
                {
                    CrashLog::Log("SecurityLoop INNER catch (iteration survived)");
                }
            }
        }
        catch (const std::exception& ex)
        {
            g_securityMessage = ex.what();
            g_securityViolation.store(true, std::memory_order_release);
        }
    })

    void SetSecurity()
    {
        CrashLog::Log("Security::SetSecurity entered");
        try {
            try {
                if (RuntimeProtection::AntiVM::IsVirtualMachine())
                    exit(0);
            } catch (...) {}

            try {
                if (!RuntimeProtection::SingleInstance::CheckAndAcquire())
                    exit(0);
            } catch (...) {}

            BlockPython();

            std::vector<std::wstring> AntiCheats = {
                OBFUSCATE_WSTRING(L"x64dbg.exe"),
                OBFUSCATE_WSTRING(L"x32dbg.exe"),
                OBFUSCATE_WSTRING(L"ollydbg.exe"),
                OBFUSCATE_WSTRING(L"fiddler.exe"),
                OBFUSCATE_WSTRING(L"httpdebuggerui.exe"),
                OBFUSCATE_WSTRING(L"wireshark.exe"),
                OBFUSCATE_WSTRING(L"cheatengine-x86_64.exe"),
                OBFUSCATE_WSTRING(L"cheatengine-i386.exe"),
                OBFUSCATE_WSTRING(L"cheatengine-x86_64-SSE4-AVX2.exe"),
                OBFUSCATE_WSTRING(L"procmon64.exe"),
                OBFUSCATE_WSTRING(L"procmon32.exe"),
                OBFUSCATE_WSTRING(L"scylla.exe"),
                OBFUSCATE_WSTRING(L"scylla_x64.exe"),
                OBFUSCATE_WSTRING(L"scylla_x86.exe"),
                OBFUSCATE_WSTRING(L"importrec.exe"),
                OBFUSCATE_WSTRING(L"renderdocui.exe"),
                OBFUSCATE_WSTRING(L"hacker.exe"),
                OBFUSCATE_WSTRING(L"processhacker.exe")
            };

            std::thread securityThread(SecurityLoop, AntiCheats);
            securityThread.detach();
        }
        catch (const std::exception& ex)
        {
            CrashLog::Log("Security::SetSecurity exception (survived)");
            CrashLog::Log(ex.what());
        }
        catch (...)
        {
            CrashLog::Log("Security::SetSecurity unknown exception (survived)");
        }
    }
}

namespace LicenseServer
{
    static std::atomic<bool> g_shouldExit{ false };
    static std::string g_exitMessage;
    static std::string g_lastMessage;
    static bool g_initialized = false;

    void Authenticate()
    {
        OBF_BOGUS_FLOW_CYCLONE();
        if (!g_initialized)
        {
            g_initialized = true;
        }
    }

    void CheckHeartBeat()
    {
        OBF_BOGUS_FLOW_CYCLONE();
        if (g_shouldExit.load(std::memory_order_acquire)) [[unlikely]]
        {
            ServerSecureClient::InvalidateSession();
            SecurePayload::InvalidateSessionKey();
            g_shouldExit.store(false);
            return;
        }
    }

    std::vector<unsigned char> DownloadFileToMemory(std::string fileName)
    {
        OBF_BOGUS_FLOW_CYCLONE();
        return std::vector<unsigned char>();
    }

    OBF_METHOD(bool, License, (std::string key), {
        OBF_CALL_ANY_LOCAL_JUNK();
        bool success = KeyauxClient::Activate(key);
        g_lastMessage = KeyauxClient::GetLastMessage();
        if (success)
        {
            LicenseGuard::SetLicenseValid(true, g_lastMessage);
            LicenseGuard::CheckTokenAtPoint3();
        }
        OBF_CALL_ANY_LOCAL_JUNK();
        return success;
    })

    std::string GetMessage()
    {
        return g_lastMessage;
    }

    OBF_METHOD(std::string, GetSubscription, (), {
        OBF_CALL_ANY_LOCAL_JUNK();
        return KeyauxClient::GetExpiry();
    })

    bool TryAutoLogin(std::string& username, std::string& password, std::string& key)
    {
        OBF_BOGUS_FLOW_LABYRINTH();
        const std::string savePath = KeyauxClient::GetSavePath();

        if (!std::filesystem::exists(savePath))
            return false;

        // Only auto-login when the user explicitly chose "Remember me".
        const auto rememberMe = ReadFromJson(savePath, OBFUSCATE_STRING("rememberMe"));
        if (rememberMe != OBFUSCATE_STRING("true"))
            return false;

        const auto saved_license = ReadFromJson(savePath, OBFUSCATE_STRING("license"));

        if (!saved_license.empty())
        {
            key = saved_license;
            if (License(key))
                return true;
        }

        return false;
    }

    void ClearSavedCredentials()
    {
        OBF_BOGUS_FLOW_WEAVER();
        try
        {
            if (std::filesystem::exists(KeyauxClient::GetSavePath()))
                std::filesystem::remove(KeyauxClient::GetSavePath());
        }
        catch (...)
        {
        }
    }

    void SaveCredentials(bool save, const std::string& username, const std::string& password, const std::string& key)
    {
        OBF_BOGUS_FLOW_CYCLONE();
        std::string rememberVal = save ? "true" : "false";
        WriteToJson(KeyauxClient::GetSavePath(), OBFUSCATE_STRING("license"), key, true,
                    OBFUSCATE_STRING("rememberMe"), rememberVal);
    }
}

namespace Timer
{
    static std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

    void Start()
    {
        startTime = std::chrono::high_resolution_clock::now();
    }

    void Measure(int& milliseconds)
    {
        auto currentTime = std::chrono::high_resolution_clock::now();
        milliseconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count()
            );
        startTime = currentTime;
    }
}

//
// VMProtect marker stubs.
//
// When ARCEUS_USE_VMP is defined, every VIRTUALIZER_* site compiles to calls of
// these exports. VMProtect replaces the function bodies with VM handler entry
// points during the post-link pass (Tools/ApplyVMProtect.ps1). Until that
// pass runs they are no-op calls, so the unprocessed binary stays functional.
//
#ifdef ARCEUS_USE_VMP
extern "C" __declspec(dllexport) void __cdecl VMProtectBegin(const char*) {}
extern "C" __declspec(dllexport) void __cdecl VMProtectBeginVirtualization(const char*) {}
extern "C" __declspec(dllexport) void __cdecl VMProtectBeginMutation(const char*) {}
extern "C" __declspec(dllexport) void __cdecl VMProtectBeginUltra(const char*) {}
extern "C" __declspec(dllexport) void __cdecl VMProtectEnd(void) {}
#endif