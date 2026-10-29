#include "LicenseGuard.h"
#include "../Core/CrashLog.h"
#include "Utilities.h"
#include "HardenedCore.h"
#include "../Core/KeyauxClient.h"
#include "../Core/ServerSecureClient.h"
#include "../Core/SecurePayload.h"
#include "../KeyAux/storage.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <intrin.h>
#include <random>
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

namespace LicenseGuard
{
    static std::atomic<bool> g_watchdogRunning{ false };
    static std::thread g_watchdogThread;
    static std::string g_responseHash;
    static std::mutex g_hashMutex;
    static std::string g_encryptedToken;
    static std::mutex g_tokenMutex;
    static uint64_t g_validationTimestamp = 0;

    struct CriticalFunction
    {
        const char* name;
        void* address;
        size_t size;
        uint32_t originalCRC;
    };

    static std::vector<CriticalFunction> g_criticalFunctions;
    static std::mutex g_funcMutex;

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

    static void SimpleXORCrypt(std::vector<uint8_t>& data, const std::vector<uint8_t>& key)
    {
        for (size_t i = 0; i < data.size(); i++)
            data[i] ^= key[i % key.size()];
    }

    std::string GenerateHardwareFingerprint()
    {
        std::string fp;

        int cpuInfo[4] = { 0 };
        __cpuid(cpuInfo, 0);
        fp += std::to_string(cpuInfo[1]);
        fp += std::to_string(cpuInfo[3]);

        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, OBFUSCATE_STRING("SOFTWARE\\Microsoft\\Cryptography").c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            char volName[MAX_PATH];
            DWORD volSize = MAX_PATH;
            if (RegQueryValueExA(hKey, OBFUSCATE_STRING("MachineGuid").c_str(), NULL, NULL, (LPBYTE)volName, &volSize) == ERROR_SUCCESS)
                fp += volName;
            RegCloseKey(hKey);
        }

        DWORD serial = 0;
        GetVolumeInformationA(OBFUSCATE_STRING("C:\\").c_str(), NULL, 0, &serial, NULL, NULL, NULL, 0);
        fp += std::to_string(serial);

        return fp;
    }

    std::string EncryptToken(const std::string& plaintext, const std::string& key)
    {
        std::vector<uint8_t> data(plaintext.begin(), plaintext.end());
        std::vector<uint8_t> keyVec(key.begin(), key.end());

        for (size_t round = 0; round < 3; round++)
        {
            for (size_t i = 0; i < data.size(); i++)
            {
                data[i] ^= keyVec[i % keyVec.size()];
                data[i] = (data[i] >> 3) | (data[i] << 5);
            }
            for (size_t i = 0; i < keyVec.size(); i++)
                keyVec[i] = (keyVec[i] << 1) | (keyVec[i] >> 7);
        }

        std::string result(data.begin(), data.end());
        SecureZeroMemory(data.data(), data.size());
        SecureZeroMemory(keyVec.data(), keyVec.size());
        return result;
    }

    std::string DecryptToken(const std::string& ciphertext, const std::string& key)
    {
        std::vector<uint8_t> data(ciphertext.begin(), ciphertext.end());
        std::vector<uint8_t> keyVec(key.begin(), key.end());

        std::vector<std::vector<uint8_t>> keyStates;
        std::vector<uint8_t> currentKey = keyVec;
        for (size_t round = 0; round < 3; round++)
        {
            keyStates.push_back(currentKey);
            for (size_t i = 0; i < currentKey.size(); i++)
                currentKey[i] = (currentKey[i] << 1) | (currentKey[i] >> 7);
        }

        for (int round = 2; round >= 0; round--)
        {
            for (size_t i = 0; i < data.size(); i++)
            {
                data[i] = (data[i] << 3) | (data[i] >> 5);
                data[i] ^= keyStates[round][i % keyStates[round].size()];
            }
        }

        std::string result(data.begin(), data.end());
        SecureZeroMemory(data.data(), data.size());
        SecureZeroMemory(keyVec.data(), keyVec.size());
        SecureZeroMemory(currentKey.data(), currentKey.size());
        for (auto& ks : keyStates)
            SecureZeroMemory(ks.data(), ks.size());
        return result;
    }

    uint32_t GetFunctionCRC(void* funcStart, size_t funcSize)
    {
        if (!funcStart || funcSize == 0) return 0;
        return CalculateCRC32(funcStart, funcSize);
    }

    void RegisterCriticalFunction(const char* name, void* addr, size_t size)
    {
        std::lock_guard<std::mutex> lock(g_funcMutex);
        CriticalFunction cf;
        cf.name = name;
        cf.address = addr;
        cf.size = size;
        cf.originalCRC = CalculateCRC32(addr, size);
        g_criticalFunctions.push_back(cf);
    }

    void VerifyAllFunctions()
    {
        std::lock_guard<std::mutex> lock(g_funcMutex);
        for (auto& cf : g_criticalFunctions)
        {
            uint32_t currentCRC = CalculateCRC32(cf.address, cf.size);
            if (currentCRC != cf.originalCRC)
            {
                ServerSecureClient::InvalidateSession();
                SecurePayload::InvalidateSessionKey();
                return;
            }
        }
    }

    static bool DetectDebugger()
    {
        if (IsDebuggerPresent())
            return true;

        BOOL isRemote = FALSE;
        if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &isRemote) && isRemote)
            return true;

        return false;
    }

    void VerifyGlobalState()
    {
        OBF_BOGUS_FLOW_SCRAMBLE();
        if (!ServerSecureClient::VerifySessionIntegrity())
        {
            SecurePayload::InvalidateSessionKey();
            return;
        }
        if (!SecurePayload::IsKeyValid())
        {
            ServerSecureClient::InvalidateSession();
            return;
        }
        if (!VerifyValidationToken())
        {
            SecurePayload::InvalidateSessionKey();
            return;
        }
        if (DetectDebugger())
        {
            ServerSecureClient::InvalidateSession();
            SecurePayload::InvalidateSessionKey();
            return;
        }
    }

    void StoreValidationToken(const ValidationToken& token)
    {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        std::string data(sizeof(ValidationToken), '\0');
        std::memcpy(&data[0], &token, sizeof(ValidationToken));
        g_encryptedToken = data;
        g_validationTimestamp = token.timestamp;
        SecureZeroMemory(data.data(), data.size());
    }

    bool VerifyValidationToken()
    {
        OBF_BOGUS_FLOW_CASCADE();
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        if (g_encryptedToken.empty()) return false;
        if (g_encryptedToken.size() != sizeof(ValidationToken)) return false;

        ValidationToken token;
        std::memcpy(&token, g_encryptedToken.data(), sizeof(ValidationToken));

        if (token.timestamp != g_validationTimestamp) return false;

        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - token.timestamp > 86400) return false;

        std::string currentFp = GenerateHardwareFingerprint();
        for (size_t i = 0; i < sizeof(token.hardwareHash) && i < currentFp.size(); i++)
        {
            if (token.hardwareHash[i] != (uint8_t)currentFp[i])
            {
                SecureZeroMemory(currentFp.data(), currentFp.size());
                return false;
            }
        }

        SecureZeroMemory(currentFp.data(), currentFp.size());
        return true;
    }

    static std::string HashResponse(const std::string& response)
    {
        uint32_t hash = CalculateCRC32(response.c_str(), response.size());
        return std::to_string(hash);
    }

    void SetLicenseValid(bool valid, const std::string& responseMessage)
    {
        std::lock_guard<std::mutex> lock(g_hashMutex);
        g_responseHash = HashResponse(responseMessage);

        if (valid)
        {
            ValidationToken token;
            std::string hwFp = GenerateHardwareFingerprint();
            std::memset(&token.hardwareHash, 0, sizeof(token.hardwareHash));
            for (size_t i = 0; i < hwFp.size() && i < 16; i++)
                token.hardwareHash[i] = (uint8_t)hwFp[i];

            token.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            token.responseCrc = CalculateCRC32(responseMessage.c_str(), responseMessage.size());

            std::random_device rd;
            token.nonce = rd();

            SecureZeroMemory(hwFp.data(), hwFp.size());
            StoreValidationToken(token);
        }
    }

    bool CheckTokenAtPoint1()
    {
        return ServerSecureClient::VerifySessionIntegrity() &&
               SecurePayload::IsKeyValid();
    }

    bool CheckTokenAtPoint2()
    {
        if (!ServerSecureClient::VerifySessionIntegrity()) return false;
        return SecurePayload::IsKeyValid();
    }

    bool CheckTokenAtPoint3()
    {
        if (!SecurePayload::IsKeyValid()) return false;
        return ServerSecureClient::VerifySessionIntegrity();
    }

    bool CheckTokenAtPoint4()
    {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - g_validationTimestamp > 86400) return false;
        return CheckTokenAtPoint1();
    }

    bool CheckTokenAtPoint5()
    {
        if (g_encryptedToken.empty()) return false;
        return CheckTokenAtPoint1();
    }

    bool CheckTokenAtPoint6()
    {
        if (!ServerSecureClient::VerifySessionIntegrity()) return false;
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - g_validationTimestamp > 86400) return false;
        return true;
    }

    bool CheckTokenAtPoint7()
    {
        std::lock_guard<std::mutex> lock(g_hashMutex);
        if (g_responseHash.empty()) return false;
        uint32_t hashVal = static_cast<uint32_t>(std::stoull(g_responseHash));
        if (hashVal == 0) return false;
        return CheckTokenAtPoint1();
    }

    bool CheckTokenAtPoint8()
    {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        if (g_encryptedToken.size() < 16) return false;
        uint8_t checkByte = g_encryptedToken[3] ^ g_encryptedToken[7] ^ g_encryptedToken[11];
        if (checkByte == 0) return false;
        return ServerSecureClient::VerifySessionIntegrity();
    }

    bool CheckTokenAtPoint9()
    {
        if (!CheckTokenAtPoint1()) return false;
        std::lock_guard<std::mutex> lock(g_hashMutex);
        return !g_responseHash.empty();
    }

    bool CheckTokenAtPoint10()
    {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t elapsed = now - g_validationTimestamp;
        if (elapsed > 86400) return false;
        if (elapsed < 0) return false;
        return CheckTokenAtPoint1();
    }

    bool CrossValidateAll()
    {
        OBF_BOGUS_FLOW_CYCLONE();
        if (DetectDebugger())
        {
            ServerSecureClient::InvalidateSession();
            SecurePayload::InvalidateSessionKey();
            return false;
        }
        bool a = ServerSecureClient::VerifySessionIntegrity();
        bool b = SecurePayload::IsKeyValid();
        bool c = VerifyValidationToken();
        bool d = !g_encryptedToken.empty();
        bool e = !g_responseHash.empty();
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        bool f = (now - g_validationTimestamp <= 86400);
        return a && b && c && d && e && f;
    }

    static void WatchdogFunc()
    {
        CrashLog::Log(("THREAD LicenseGuard::WatchdogFunc tid=" + std::to_string(GetCurrentThreadId())).c_str());
        OBF_BOGUS_FLOW_LABYRINTH();
        while (g_watchdogRunning.load(std::memory_order_acquire))
        {
            // Jittered sleep: patching a fixed Sleep(30000) argument no longer helps.
            HardenedCore::HardenedSleep(15000);

            if (!g_watchdogRunning.load(std::memory_order_acquire)) break;

            if (DetectDebugger() || HardenedCore::DebuggerDetectedSyscall())
            {
                ServerSecureClient::InvalidateSession();
                SecurePayload::InvalidateSessionKey();
                break;
            }

            VerifyAllFunctions();

            // Session-dependent checks only apply once a real key is installed
            // (post-login). Before that, only the debugger sweep runs.
            if (!SecurePayload::IsKeyValid()) continue;

            if (!ServerSecureClient::VerifySessionIntegrity()) break;

            if (!VerifyValidationToken()) break;

            VerifyGlobalState();
        }
    }

    void Revalidate()
    {
        VerifyAllFunctions();
        ServerSecureClient::VerifySessionIntegrity();
        SecurePayload::IsKeyValid();
        VerifyValidationToken();
    }

    void StartWatchdog()
    {
        if (g_watchdogRunning.load(std::memory_order_acquire)) return;
        g_watchdogRunning.store(true, std::memory_order_release);
        g_watchdogThread = std::thread(WatchdogFunc);
        g_watchdogThread.detach();
    }

    void StopWatchdog()
    {
        g_watchdogRunning.store(false, std::memory_order_release);
    }
}
