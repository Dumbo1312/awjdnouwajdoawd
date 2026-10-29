#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>

namespace FakeServer
{
    struct FakeEndpoint
    {
        std::string path;
        std::string response;
        int statusCode;
    };

    void Initialize();
    void StartFakeServer();
    void StopFakeServer();
    bool IsRunning();

    namespace Redirector
    {
        void SetupRedirects();
        bool IsFakeDomain(const std::string& host);
        std::string GetFakeResponse(const std::string& host, const std::string& path);
    }

    namespace Honeypot
    {
        void CreateHoneypotFiles();
        bool IsHoneypotAccessed();
        void TriggerAlert();
    }

    namespace DecoyTraffic
    {
        void StartDecoyTraffic();
        void StopDecoyTraffic();
        bool IsDecoyActive();
    }
}
