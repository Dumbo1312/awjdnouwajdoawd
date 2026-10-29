#pragma once
#include <string>
#include <vector>
#include <array>
#include <cstdint>

struct SessionContext
{
    std::array<uint8_t, 32> aesKey;
    std::array<uint8_t, 16> aesIv;
    double aimOffsetX;
    double aimOffsetY;
    uint64_t noiseSeed;
    float confidenceBias;
    float strengthModifier;
    uint64_t serverTimestamp;
    uint32_t crcChecksum;
};

namespace ServerSecureClient
{
    bool Initialize(const std::string& licenseKey);
    const SessionContext& GetSession();
    void InvalidateSession();
    bool VerifySessionIntegrity();
    bool SendHeartbeat();
    void Shutdown();
}
