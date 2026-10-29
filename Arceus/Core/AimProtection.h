#pragma once
#include <Windows.h>
#include <cstdint>
#include <intrin.h>
#include <atomic>

namespace AimProtection
{
    static std::atomic<uint64_t> g_codeKey{ 0 };
    static std::atomic<bool> g_initialized{ false };

    __forceinline uint64_t GenerateRuntimeKey()
    {
        int cpuInfo[4] = { 0 };
        __cpuid(cpuInfo, 0);
        uint64_t t = __rdtsc();
        return t ^ ((uint64_t)cpuInfo[0] << 32) ^ (uint64_t)cpuInfo[3];
    }

    __forceinline void InitializeProtection()
    {
        if (g_initialized.load(std::memory_order_acquire)) return;
        g_codeKey.store(GenerateRuntimeKey(), std::memory_order_release);
        g_initialized.store(true, std::memory_order_release);
    }

    __forceinline uint64_t GetKey() { return g_codeKey.load(std::memory_order_acquire); }

    __declspec(noinline) void ApplyCodeMutation();
    __declspec(noinline) bool VerifyCodeIntegrity();

    typedef void(*AimFunc)(int&, int&, float, int, int, int&, int&);
    typedef bool(*TriggerFunc)(void*, int, int);
    typedef bool(*FovCheckFunc)(void*, int);
    typedef void(*ConvertFunc)(int&, int&, int);
    typedef void(*PredictionFunc)(void*, int&, int&, float);

    struct AimFunctionTable
    {
        AimFunc smoothFunc;
        TriggerFunc triggerCheck;
        FovCheckFunc fovCheck;
        ConvertFunc convertFunc;
        PredictionFunc predictionFunc;
        uint64_t checksum;
    };

    static AimFunctionTable g_funcTable = { nullptr, nullptr, nullptr, nullptr, nullptr, 0 };

    __forceinline void StoreFunctionTable(const AimFunctionTable& table)
    {
        g_funcTable = table;
    }

    __forceinline const AimFunctionTable& GetFunctionTable()
    {
        return g_funcTable;
    }
}

#define AIM_PROTECT_SECTION __declspec(code_seg(".text"))
#define AIM_PROTECT_FORCEINLINE __forceinline
#define AIM_OPAQUE_PREDICATE(x) ([]() -> bool { \
    volatile uint64_t _k = AimProtection::GetKey(); \
    return ((x) ^ (_k & 1)) != 0; \
}())
