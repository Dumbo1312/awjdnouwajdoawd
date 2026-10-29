#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "bcrypt.lib")

namespace ResourceDecrypt
{
    static constexpr uint32_t RES_MAGIC = 0x45585230;

    struct EncryptedHeader
    {
        uint32_t magic;
        uint32_t originalSize;
    };

    static bool AES256CBCDecrypt(
        const uint8_t* key, size_t keySize,
        const uint8_t* input, size_t inputSize,
        std::vector<uint8_t>& output)
    {
        if (inputSize < 16) return false;

        const uint8_t* iv = input;
        const uint8_t* ciphertext = input + 16;
        size_t ciphertextSize = inputSize - 16;

        BCRYPT_ALG_HANDLE hAlg = NULL;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
        if (!BCRYPT_SUCCESS(status)) return false;

        status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        DWORD objLen = 0;
        DWORD resultLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(DWORD), &resultLen, 0);
        std::vector<uint8_t> keyObj(objLen);

        std::vector<uint8_t> blob(sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + keySize);
        auto blobHeader = reinterpret_cast<BCRYPT_KEY_DATA_BLOB_HEADER*>(blob.data());
        blobHeader->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
        blobHeader->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        blobHeader->cbKeyData = (ULONG)keySize;
        std::memcpy(blob.data() + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER), key, keySize);

        BCRYPT_HANDLE hKey = NULL;
        status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), objLen,
            blob.data(), (ULONG)blob.size(), 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        output.resize(ciphertextSize + 16);
        DWORD cbResult = 0;
        status = BCryptDecrypt(hKey, const_cast<uint8_t*>(ciphertext),
            (ULONG)ciphertextSize, NULL, const_cast<uint8_t*>(iv), 16,
            output.data(), (ULONG)output.size(), &cbResult,
            BCRYPT_BLOCK_PADDING);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        SecureZeroMemory(keyObj.data(), keyObj.size());
        SecureZeroMemory(blob.data(), blob.size());

        if (!BCRYPT_SUCCESS(status))
        {
            output.clear();
            return false;
        }

        output.resize(cbResult);
        return true;
    }

    static bool DecryptResource(
        const uint8_t* encryptedData, size_t dataSize,
        const uint8_t* key, size_t keySize,
        std::vector<uint8_t>& decryptedOutput)
    {
        if (!encryptedData || dataSize < sizeof(EncryptedHeader))
            return false;

        EncryptedHeader header;
        std::memcpy(&header, encryptedData, sizeof(header));

        if (header.magic != RES_MAGIC)
            return false;

        const uint8_t* payload = encryptedData + sizeof(header);
        size_t payloadSize = dataSize - sizeof(header);

        std::vector<uint8_t> tempDecrypted;
        if (!AES256CBCDecrypt(key, keySize, payload, payloadSize, tempDecrypted))
            return false;

        decryptedOutput.resize(header.originalSize);
        std::memcpy(decryptedOutput.data(), tempDecrypted.data(),
            (std::min)(tempDecrypted.size(), (size_t)header.originalSize));

        return true;
    }

    static bool DecryptResourceToFile(
        const uint8_t* encryptedData, size_t dataSize,
        const uint8_t* key, size_t keySize,
        const std::wstring& outputPath)
    {
        std::vector<uint8_t> decrypted;
        if (!DecryptResource(encryptedData, dataSize, key, keySize, decrypted))
            return false;

        HANDLE hFile = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        DWORD written;
        BOOL ok = WriteFile(hFile, decrypted.data(), (DWORD)decrypted.size(), &written, NULL);
        CloseHandle(hFile);

        SecureZeroMemory(decrypted.data(), decrypted.size());
        return ok && written == decrypted.size();
    }

    static bool DecryptResourceInMemory(
        const uint8_t* encryptedData, size_t dataSize,
        const uint8_t* key, size_t keySize,
        std::vector<uint8_t>& outputBuffer)
    {
        return DecryptResource(encryptedData, dataSize, key, keySize, outputBuffer);
    }
}
