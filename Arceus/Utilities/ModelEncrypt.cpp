#include "ModelEncrypt.h"
#include <bcrypt.h>
#include <intrin.h>
#include <fstream>
#include <cstring>
#include <random>
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#pragma comment(lib, "bcrypt.lib")

namespace ModelEncrypt
{
    static const DWORD AES_KEY_SIZE = 32;
    static const DWORD AES_BLOCK_SIZE = 16;
    static const DWORD SALT_SIZE = 16;
    static const DWORD HEADER_MAGIC = 0x4D444C32;


    struct ModelHeader
    {
        uint32_t magic;
        uint32_t originalSize;
        uint32_t hash;
        uint32_t reserved;
    };

    std::string GetHardwareKey()
    {
        OBF_BOGUS_FLOW_GRID();
        char key[] = { 0xC2^0x77, '3'^0x77, 'N'^0x77, 'A'^0x77, 'D'^0x77, '_'^0x77, 'M'^0x77, 'o'^0x77, 'd'^0x77, 'e'^0x77, 'l'^0x77, '_'^0x77, 'v'^0x77, '2'^0x77, '_'^0x77, 'S'^0x77, 'a'^0x77, 'l'^0x77, 't'^0x77, '_'^0x77, '2'^0x77, '0'^0x77, '2'^0x77, '4'^0x77, '!'^0x77, 0 };
        for (int i = 0; i < 25; i++) key[i] ^= 0x77;
        return std::string(key);
    }

    static bool DeriveKey(const std::string& password, const uint8_t* salt, uint8_t* key, uint8_t* iv)
    {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HANDLE hHash = NULL;
        BCRYPT_HANDLE hSecret = NULL;

        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
        if (!BCRYPT_SUCCESS(status)) return false;

        status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
        if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }

        BCryptHashData(hHash, (PUCHAR)salt, SALT_SIZE, 0);
        BCryptHashData(hHash, (PUCHAR)password.c_str(), (ULONG)password.size(), 0);

        uint8_t hash[32];
        BCryptFinishHash(hHash, hash, 32, 0);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        std::memcpy(key, hash, AES_KEY_SIZE);
        std::memcpy(iv, hash + 16, AES_BLOCK_SIZE);

        return true;
    }

    static bool AESCrypt(const uint8_t* input, size_t inputSize, uint8_t* output, size_t outputSize, DWORD* bytesWritten, const uint8_t* key, const uint8_t* iv, bool encrypt)
    {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HANDLE hKey = NULL;

        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
        if (!BCRYPT_SUCCESS(status)) return false;

        DWORD objLen = 0;
        DWORD resultLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(DWORD), &resultLen, 0);
        std::vector<uint8_t> keyObj(objLen);

        std::vector<uint8_t> blob(sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + AES_KEY_SIZE + AES_BLOCK_SIZE);
        BCRYPT_KEY_DATA_BLOB_HEADER* blobHeader = reinterpret_cast<BCRYPT_KEY_DATA_BLOB_HEADER*>(blob.data());
        blobHeader->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
        blobHeader->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        blobHeader->cbKeyData = AES_KEY_SIZE;
        std::memcpy(blob.data() + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER), key, AES_KEY_SIZE);
        std::memcpy(blob.data() + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + AES_KEY_SIZE, iv, AES_BLOCK_SIZE);

        status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), objLen, blob.data(), (ULONG)blob.size(), 0);
        if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }

        DWORD cbResult = 0;
        DWORD flags = BCRYPT_BLOCK_PADDING;
        if (encrypt) {
            status = BCryptEncrypt(hKey, (PUCHAR)input, (ULONG)inputSize, NULL, (PUCHAR)iv, AES_BLOCK_SIZE, (PUCHAR)output, (ULONG)outputSize, &cbResult, flags);
        } else {
            status = BCryptDecrypt(hKey, (PUCHAR)input, (ULONG)inputSize, NULL, (PUCHAR)iv, AES_BLOCK_SIZE, (PUCHAR)output, (ULONG)outputSize, &cbResult, flags);
        }

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (bytesWritten) *bytesWritten = cbResult;
        return BCRYPT_SUCCESS(status);
    }

    static uint32_t SimpleCRC32(const void* data, size_t size)
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

    uint32_t CalculateModelHash(const uint8_t* data, size_t size)
    {
        return SimpleCRC32(data, size);
    }

    void ProtectMemory(void* buffer, size_t size)
    {
        DWORD oldProtect;
        VirtualProtect(buffer, size, PAGE_READONLY, &oldProtect);
    }

    bool EncryptFile(const std::wstring& inputPath, const std::wstring& outputPath)
    {
        std::ifstream inFile(inputPath, std::ios::binary | std::ios::ate);
        if (!inFile.is_open()) return false;

        size_t fileSize = (size_t)inFile.tellg();
        inFile.seekg(0);

        std::vector<uint8_t> fileData(fileSize);
        inFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        inFile.close();

        std::string hwKey = GetHardwareKey();

        uint8_t salt[SALT_SIZE];
        std::random_device rd;
        for (int i = 0; i < SALT_SIZE; i++)
            salt[i] = (uint8_t)(rd() & 0xFF);

        uint8_t key[AES_KEY_SIZE], iv[AES_BLOCK_SIZE];
        if (!DeriveKey(hwKey, salt, key, iv)) return false;

        std::vector<uint8_t> encrypted(fileSize + AES_BLOCK_SIZE * 2, 0);
        DWORD cbWritten = 0;
        if (!AESCrypt(fileData.data(), fileSize, encrypted.data(), encrypted.size(), &cbWritten, key, iv, true)) return false;

        ModelHeader header;
        header.magic = HEADER_MAGIC;
        header.originalSize = (uint32_t)fileSize;
        header.hash = CalculateModelHash(fileData.data(), fileSize);
        header.reserved = (uint32_t)cbWritten;

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) return false;

        outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        outFile.write(reinterpret_cast<const char*>(salt), SALT_SIZE);
        outFile.write(reinterpret_cast<const char*>(encrypted.data()), cbWritten);

        const size_t fakeSize = 35ULL * 1024 * 1024;
        size_t currentSize = sizeof(ModelHeader) + SALT_SIZE + cbWritten;
        if (fakeSize > currentSize)
        {
            size_t padBytes = fakeSize - currentSize;
            std::vector<uint8_t> pad(padBytes, 0x5A);
            outFile.write(reinterpret_cast<const char*>(pad.data()), padBytes);
        }
        outFile.close();

        return true;
    }

    bool DecryptFile(const std::wstring& inputPath, std::vector<uint8_t>& outputData)
    {
        std::ifstream inFile(inputPath, std::ios::binary | std::ios::ate);
        if (!inFile.is_open()) return false;

        size_t fileSize = (size_t)inFile.tellg();
        inFile.seekg(0);

        if (fileSize < sizeof(ModelHeader) + SALT_SIZE) return false;

        std::vector<uint8_t> fileData(fileSize);
        inFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        inFile.close();

        ModelHeader header;
        std::memcpy(&header, fileData.data(), sizeof(header));

        if (header.magic != HEADER_MAGIC) return false;
        if (header.originalSize > fileSize) return false;

        const uint8_t* salt = fileData.data() + sizeof(header);
        const uint8_t* encrypted = fileData.data() + sizeof(header) + SALT_SIZE;
        size_t encryptedSize = (header.reserved > 0 && header.reserved < fileSize)
            ? (size_t)header.reserved
            : (fileSize - sizeof(header) - SALT_SIZE);

        std::string hwKey = GetHardwareKey();

        uint8_t key[AES_KEY_SIZE], iv[AES_BLOCK_SIZE];
        if (!DeriveKey(hwKey, salt, key, iv)) return false;

        std::vector<uint8_t> decrypted(encryptedSize);
        DWORD cbWritten = 0;
        if (!AESCrypt(encrypted, encryptedSize, decrypted.data(), decrypted.size(), &cbWritten, key, iv, false)) return false;

        outputData.resize(header.originalSize);
        std::memcpy(outputData.data(), decrypted.data(), header.originalSize);

        uint32_t currentHash = CalculateModelHash(outputData.data(), header.originalSize);
        if (currentHash != header.hash) return false;

        return true;
    }

    bool DecryptToBuffer(const std::vector<uint8_t>& encryptedData, std::vector<uint8_t>& decryptedData)
    {
        if (encryptedData.size() < sizeof(ModelHeader) + SALT_SIZE) return false;

        ModelHeader header;
        std::memcpy(&header, encryptedData.data(), sizeof(header));

        if (header.magic != HEADER_MAGIC) return false;

        const uint8_t* salt = encryptedData.data() + sizeof(header);
        const uint8_t* encrypted = encryptedData.data() + sizeof(header) + SALT_SIZE;
        size_t totalEncSize = encryptedData.size() - sizeof(header) - SALT_SIZE;
        size_t encryptedSize = (header.reserved > 0 && header.reserved < totalEncSize)
            ? (size_t)header.reserved
            : totalEncSize;

        std::string hwKey = GetHardwareKey();

        uint8_t key[AES_KEY_SIZE], iv[AES_BLOCK_SIZE];
        if (!DeriveKey(hwKey, salt, key, iv)) return false;

        std::vector<uint8_t> decrypted(encryptedSize);
        DWORD cbWritten = 0;
        if (!AESCrypt(encrypted, encryptedSize, decrypted.data(), decrypted.size(), &cbWritten, key, iv, false)) return false;

        decryptedData.resize(header.originalSize);
        std::memcpy(decryptedData.data(), decrypted.data(), header.originalSize);

        uint32_t currentHash = CalculateModelHash(decryptedData.data(), header.originalSize);
        if (currentHash != header.hash) return false;

        return true;
    }
}
