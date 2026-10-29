#include "SecurePayload.h"
#include "../Utilities/VirtualizerSDK.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <cstdlib>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    // The AES key/IV are stored XOR-masked with a per-boot random mask so a
    // memory dump or live memory scan cannot recover the raw key bytes.
    static struct
    {
        uint8_t key[32];
        uint8_t iv[16];
        uint8_t keyMask[32];
        uint8_t ivMask[16];
        bool valid;
    } g_cryptoState;

    static void GenerateMasks()
    {
        NTSTATUS st1 = BCryptGenRandom(NULL, g_cryptoState.keyMask, 32,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        NTSTATUS st2 = BCryptGenRandom(NULL, g_cryptoState.ivMask, 16,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!BCRYPT_SUCCESS(st1)) SecureZeroMemory(g_cryptoState.keyMask, 32);
        if (!BCRYPT_SUCCESS(st2)) SecureZeroMemory(g_cryptoState.ivMask, 16);
    }

    static void UnmaskKey(uint8_t out[32])
    {
        for (int i = 0; i < 32; ++i)
            out[i] = g_cryptoState.key[i] ^ g_cryptoState.keyMask[i];
    }

    static void UnmaskIv(uint8_t out[16])
    {
        for (int i = 0; i < 16; ++i)
            out[i] = g_cryptoState.iv[i] ^ g_cryptoState.ivMask[i];
    }

    static bool SessionAESDecrypt(const uint8_t* input, size_t inputSize,
        uint8_t* output, size_t outputSize, DWORD* bytesWritten)
    {
        if (!g_cryptoState.valid) return false;

        uint8_t sessionKey[32];
        uint8_t sessionIv[16];
        UnmaskKey(sessionKey);
        UnmaskIv(sessionIv);

        BCRYPT_ALG_HANDLE hAlg = NULL;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg,
            BCRYPT_AES_ALGORITHM, NULL, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            SecureZeroMemory(sessionKey, sizeof(sessionKey));
            SecureZeroMemory(sessionIv, sizeof(sessionIv));
            return false;
        }

        DWORD objLen = 0;
        DWORD resultLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
            (PUCHAR)&objLen, sizeof(DWORD), &resultLen, 0);
        std::vector<uint8_t> keyObj(objLen);

        std::vector<uint8_t> blob(sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 32 + 16);
        auto blobHeader = reinterpret_cast<BCRYPT_KEY_DATA_BLOB_HEADER*>(blob.data());
        blobHeader->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
        blobHeader->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        blobHeader->cbKeyData = 32;
        std::memcpy(blob.data() + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER),
            sessionKey, 32);
        std::memcpy(blob.data() + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 32,
            sessionIv, 16);

        BCRYPT_HANDLE hKey = NULL;
        status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(),
            objLen, blob.data(), (ULONG)blob.size(), 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            SecureZeroMemory(sessionKey, sizeof(sessionKey));
            SecureZeroMemory(sessionIv, sizeof(sessionIv));
            SecureZeroMemory(keyObj.data(), keyObj.size());
            SecureZeroMemory(blob.data(), blob.size());
            return false;
        }

        DWORD cbResult = 0;
        status = BCryptDecrypt(hKey, (PUCHAR)input, (ULONG)inputSize,
            NULL, sessionIv, 16,
            (PUCHAR)output, (ULONG)outputSize, &cbResult,
            BCRYPT_BLOCK_PADDING);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        SecureZeroMemory(sessionKey, sizeof(sessionKey));
        SecureZeroMemory(sessionIv, sizeof(sessionIv));
        SecureZeroMemory(keyObj.data(), keyObj.size());
        SecureZeroMemory(blob.data(), blob.size());

        if (BCRYPT_SUCCESS(status) && bytesWritten)
            *bytesWritten = cbResult;

        return BCRYPT_SUCCESS(status);
    }
}

namespace SecurePayload
{
    bool SetSessionKey(const uint8_t* key32, const uint8_t* iv16)
    {
        VIRTUALIZER_MUTATE_ONLY_START
        if (!key32 || !iv16) return false;
        GenerateMasks();
        for (int i = 0; i < 32; ++i)
            g_cryptoState.key[i] = key32[i] ^ g_cryptoState.keyMask[i];
        for (int i = 0; i < 16; ++i)
            g_cryptoState.iv[i] = iv16[i] ^ g_cryptoState.ivMask[i];
        g_cryptoState.valid = true;
        VIRTUALIZER_MUTATE_ONLY_END
        return true;
    }

    void InvalidateSessionKey()
    {
        VIRTUALIZER_MUTATE_ONLY_START
        SecureZeroMemory(g_cryptoState.key, sizeof(g_cryptoState.key));
        SecureZeroMemory(g_cryptoState.iv, sizeof(g_cryptoState.iv));
        SecureZeroMemory(g_cryptoState.keyMask, sizeof(g_cryptoState.keyMask));
        SecureZeroMemory(g_cryptoState.ivMask, sizeof(g_cryptoState.ivMask));
        g_cryptoState.valid = false;
        VIRTUALIZER_MUTATE_ONLY_END
    }

    bool DecryptBuffer(const uint8_t* input, size_t inputSize,
        std::vector<uint8_t>& output)
    {
        OBF_BOGUS_FLOW_GRID();
        if (!input || inputSize == 0 || !g_cryptoState.valid)
        {
            output.clear();
            return false;
        }

        std::vector<uint8_t> temp(inputSize + 32);
        DWORD bytesWritten = 0;

        if (!SessionAESDecrypt(input, inputSize, temp.data(),
            temp.size(), &bytesWritten))
        {
            SecureZeroMemory(temp.data(), temp.size());
            output.clear();
            return false;
        }

        output.resize(bytesWritten);
        if (bytesWritten > 0)
            std::memcpy(output.data(), temp.data(), bytesWritten);

        SecureZeroMemory(temp.data(), temp.size());
        return true;
    }

    std::string DecryptString(const uint8_t* encrypted, size_t len)
    {
        std::vector<uint8_t> dec;
        if (!DecryptBuffer(encrypted, len, dec))
            return {};

        return std::string(dec.begin(), dec.end());
    }

    uint64_t ComputeAimNoise(uint64_t seed, int frameIndex)
    {
        if (!g_cryptoState.valid)
            return 0;

        uint64_t state = seed ^ (static_cast<uint64_t>(frameIndex) * 0x9E3779B97F4A7C15ULL);
        state ^= state >> 30;
        state *= 0xBF58476D1CE4E5B9ULL;
        state ^= state >> 27;
        state *= 0x94D049BB133111EBULL;
        state ^= state >> 31;
        return state;
    }

    double ApplyCalibrationOffset(double rawX, double rawY,
        double& outX, double& outY)
    {
        if (!g_cryptoState.valid)
        {
            outX = rawX * 1000.0;
            outY = rawY * 1000.0;
            return -1.0;
        }

        outX = rawX;
        outY = rawY;
        return 0.0;
    }

    bool IsKeyValid()
    {
        OBF_BOGUS_FLOW_LABYRINTH();
        if (!g_cryptoState.valid) return false;

        uint8_t sessionKey[32];
        UnmaskKey(sessionKey);
        bool allZero = true;
        for (int i = 0; i < 32; ++i)
        {
            if (sessionKey[i] != 0)
            {
                allZero = false;
                break;
            }
        }
        SecureZeroMemory(sessionKey, sizeof(sessionKey));
        return !allZero;
    }
}
