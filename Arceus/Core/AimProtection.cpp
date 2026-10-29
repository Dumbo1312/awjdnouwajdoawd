#include "AimProtection.h"
#include <cstring>

namespace AimProtection
{
    static uint8_t g_originalBytes[32] = { 0 };
    static uint8_t g_mutatedBytes[32] = { 0 };
    static bool g_bytesSaved = false;

    void ApplyCodeMutation()
    {
        if (!g_initialized.load()) return;

        void* targetFunc = (void*)&VerifyCodeIntegrity;
        uint8_t* bytes = (uint8_t*)targetFunc;

        if (!g_bytesSaved)
        {
            memcpy(g_originalBytes, bytes, 32);
            g_bytesSaved = true;
        }

        uint64_t key = g_codeKey.load();
        for (int i = 0; i < 24; i++)
        {
            g_mutatedBytes[i] = g_originalBytes[i] ^ (uint8_t)((key >> (i % 8)) & 0xFF);
        }

        DWORD oldProtect;
        VirtualProtect(targetFunc, 32, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(bytes, g_mutatedBytes, 24);
        VirtualProtect(targetFunc, 32, oldProtect, &oldProtect);
    }

    bool VerifyCodeIntegrity()
    {
        if (!g_bytesSaved) return true;

        void* targetFunc = (void*)&VerifyCodeIntegrity;
        uint8_t* currentBytes = (uint8_t*)targetFunc;

        uint64_t key = g_codeKey.load();
        for (int i = 0; i < 24; i++)
        {
            uint8_t expected = g_originalBytes[i] ^ (uint8_t)((key >> (i % 8)) & 0xFF);
            if (currentBytes[i] != expected)
                return false;
        }
        return true;
    }
}
