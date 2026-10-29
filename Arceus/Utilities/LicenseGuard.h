#pragma once
#include <atomic>
#include <string>
#include <functional>
#include <Windows.h>
#include <array>

namespace LicenseGuard
{
    void StartWatchdog();
    void StopWatchdog();

    void SetLicenseValid(bool valid, const std::string& responseMessage);

    void Revalidate();
    uint32_t GetFunctionCRC(void* funcStart, size_t funcSize);
    void RegisterCriticalFunction(const char* name, void* addr, size_t size);
    void VerifyAllFunctions();
    void VerifyGlobalState();

    std::string GenerateHardwareFingerprint();
    std::string EncryptToken(const std::string& plaintext, const std::string& key);
    std::string DecryptToken(const std::string& ciphertext, const std::string& key);
    bool VerifyValidationToken();

    struct ValidationToken
    {
        std::array<uint8_t, 16> hardwareHash;
        uint64_t timestamp;
        uint32_t responseCrc;
        uint32_t nonce;
    };

    void StoreValidationToken(const ValidationToken& token);

    bool CheckTokenAtPoint1();
    bool CheckTokenAtPoint2();
    bool CheckTokenAtPoint3();
    bool CheckTokenAtPoint4();
    bool CheckTokenAtPoint5();
    bool CheckTokenAtPoint6();
    bool CheckTokenAtPoint7();
    bool CheckTokenAtPoint8();
    bool CheckTokenAtPoint9();
    bool CheckTokenAtPoint10();
    bool CrossValidateAll();
}
