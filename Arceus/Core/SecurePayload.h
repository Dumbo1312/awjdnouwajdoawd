#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace SecurePayload
{
    bool SetSessionKey(const uint8_t* key32, const uint8_t* iv16);
    void InvalidateSessionKey();

    bool DecryptBuffer(const uint8_t* input, size_t inputSize,
        std::vector<uint8_t>& output);

    std::string DecryptString(const uint8_t* encrypted, size_t len);

    uint64_t ComputeAimNoise(uint64_t seed, int frameIndex);
    double ApplyCalibrationOffset(double rawX, double rawY,
        double& outX, double& outY);

    bool IsKeyValid();
}
