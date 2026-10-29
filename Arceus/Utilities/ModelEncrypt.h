#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace ModelEncrypt
{
    std::string GetHardwareKey();
    bool EncryptFile(const std::wstring& inputPath, const std::wstring& outputPath);
    bool DecryptFile(const std::wstring& inputPath, std::vector<uint8_t>& outputData);
    bool DecryptToBuffer(const std::vector<uint8_t>& encryptedData, std::vector<uint8_t>& decryptedData);
    uint32_t CalculateModelHash(const uint8_t* data, size_t size);
    void ProtectMemory(void* buffer, size_t size);
}
