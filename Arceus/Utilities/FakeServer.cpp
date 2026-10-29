#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "FakeServer.h"
#include "../Core/CrashLog.h"
#include <thread>
#include <vector>
#include <atomic>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace FakeServer
{
    static std::atomic<bool> g_running{ false };
    static std::thread g_serverThread;
    static SOCKET g_serverSocket = INVALID_SOCKET;

    static std::vector<FakeEndpoint> g_endpoints;
    static std::vector<std::string> g_fakeDomains;
    static std::vector<std::pair<std::string, std::string>> g_redirects;

    static void InitializeEndpoints()
    {
        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[0].path = "/api/validate";
        g_endpoints[0].response = "{\"status\":\"valid\",\"tier\":\"premium\",\"expires\":\"2099-12-31\"}";
        g_endpoints[0].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[1].path = "/api/license/check";
        g_endpoints[1].response = "{\"valid\":true,\"license_key\":\"XXXX-XXXX-XXXX-XXXX\"}";
        g_endpoints[1].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[2].path = "/api/heartbeat";
        g_endpoints[2].response = "{\"alive\":true,\"next_check\":3600}";
        g_endpoints[2].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[3].path = "/api/config";
        g_endpoints[3].response = "{\"version\":\"1.0.0\",\"features\":[\"aimbot\",\"esp\",\"triggerbot\"]}";
        g_endpoints[3].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[4].path = "/api/user/info";
        g_endpoints[4].response = "{\"user\":\"premium_user\",\"role\":\"admin\",\"credits\":999999}";
        g_endpoints[4].statusCode = 200;

        g_fakeDomains.push_back(std::string("api.example.com"));
        g_fakeDomains.push_back(std::string("auth.example.com"));
        g_fakeDomains.push_back(std::string("update.example.com"));
        g_fakeDomains.push_back(std::string("validate.example.com"));
        g_fakeDomains.push_back(std::string("license.example.com"));

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[5].path = "/api/v1/licenses/activate";
        g_endpoints[5].response = "{\"success\":true,\"message\":\"License activated\",\"expires\":\"2099-12-31\"}";
        g_endpoints[5].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[6].path = "/api/v1/heartbeat";
        g_endpoints[6].response = "{\"alive\":true,\"status\":\"active\"}";
        g_endpoints[6].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[7].path = "/api/v1/validate";
        g_endpoints[7].response = "{\"valid\":true,\"hwid\":\"0000-COMPUTER\",\"tier\":\"lifetime\"}";
        g_endpoints[7].statusCode = 200;

        g_fakeDomains.push_back(std::string("api.keyaux.com"));
        g_fakeDomains.push_back(std::string("auth.keyaux.com"));

        // Secure-server session endpoints. The client (ServerSecureClient)
        // POSTs /v2/session/init with {license,hwid,protocol} and requires
        // session_key (32B hex), session_iv (16B hex) and calibration. Without
        // these the app correctly refuses to run (Start -> clean shutdown).
        // Heartbeat keeps the 3-strike fail counter at 0.
        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[8].path = "/v2/session/init";
        g_endpoints[8].response = "{"
            "\"session_key\":\"00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF\","
            "\"session_iv\":\"0102030405060708090A0B0C0D0E0F10\","
            "\"calibration\":{\"aim_offset_x\":0.0,\"aim_offset_y\":0.0,"
            "\"noise_seed\":123456789,\"confidence_bias\":0.0,\"strength_mod\":1.0},"
            "\"timestamp\":4102444800,\"offline_grant\":\"\"}";
        g_endpoints[8].statusCode = 200;

        g_endpoints.push_back(FakeEndpoint());
        g_endpoints[9].path = "/v2/session/heartbeat";
        g_endpoints[9].response = "{\"status\":\"ok\",\"rotation\":{\"noise_seed\":123456789,\"server_timestamp\":4102444800}}";
        g_endpoints[9].statusCode = 200;
    }

    static std::string BuildHTTPResponse(int statusCode, const std::string& body)
    {
        std::string statusText;
        switch (statusCode)
        {
            case 200: statusText = "OK"; break;
            case 404: statusText = "Not Found"; break;
            case 500: statusText = "Internal Server Error"; break;
            default: statusText = "Unknown"; break;
        }

        std::string response;
        response.append("HTTP/1.1 ");
        response.append(std::to_string(statusCode));
        response.append(" ");
        response.append(statusText);
        response.append("\r\nContent-Type: application/json\r\nContent-Length: ");
        response.append(std::to_string(body.size()));
        response.append("\r\nServer: Apache/2.4.41 (Ubuntu)\r\nX-Powered-By: Express\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\n\r\n");
        response.append(body);
        return response;
    }

    static std::string FindMatchingResponse(const std::string& request)
    {
        for (size_t i = 0; i < g_endpoints.size(); ++i)
        {
            if (request.find(g_endpoints[i].path) != std::string::npos)
                return BuildHTTPResponse(g_endpoints[i].statusCode, g_endpoints[i].response);
        }
        return BuildHTTPResponse(404, "{\"error\":\"Not Found\"}");
    }

    static void HandleClient(SOCKET clientSocket)
    {
        CrashLog::Log(("THREAD FakeServer::HandleClient tid=" + std::to_string(GetCurrentThreadId())).c_str());
        try
        {
        char buffer[4096] = { 0 };
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead > 0)
        {
            std::string request(buffer, (size_t)bytesRead);
            std::string response = FindMatchingResponse(request);
            send(clientSocket, response.c_str(), (int)response.length(), 0);
        }
        }
        catch (const std::exception& ex)
        {
            CrashLog::Log("FakeServer::HandleClient exception");
            CrashLog::Log(ex.what());
        }
        catch (...)
        {
            CrashLog::Log("FakeServer::HandleClient unknown exception");
        }

        closesocket(clientSocket);
    }

    static void ServerThreadFunc()
    {
        CrashLog::Log(("THREAD FakeServer::ServerThreadFunc tid=" + std::to_string(GetCurrentThreadId())).c_str());
        while (g_running.load())
        {
            sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);

            SOCKET clientSocket = accept(g_serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
            if (clientSocket != INVALID_SOCKET)
            {
                std::thread clientThread(HandleClient, clientSocket);
                clientThread.detach();
            }
        }
    }

    void Initialize()
    {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        InitializeEndpoints();
    }

    void StartFakeServer()
    {
        if (g_running.load()) return;

	g_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_serverSocket == INVALID_SOCKET) return;

	// Dozvoli ponovno koriscenje porta (sprecava TIME_WAIT probleme)
	int reuse = 1;
	setsockopt(g_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(8443);

	if (bind(g_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
		{
			closesocket(g_serverSocket);
			return;
		}

        if (listen(g_serverSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            closesocket(g_serverSocket);
            return;
        }

        g_running.store(true);
        g_serverThread = std::thread(ServerThreadFunc);
        g_serverThread.detach();
    }

    void StopFakeServer()
    {
        g_running.store(false);
        if (g_serverSocket != INVALID_SOCKET)
        {
            closesocket(g_serverSocket);
            g_serverSocket = INVALID_SOCKET;
        }
    }

    bool IsRunning()
    {
        return g_running.load();
    }

    namespace Redirector
    {
        void SetupRedirects()
        {
            {
                char obf[] = { 'a'^0x55, 'p'^0x55, 'i'^0x55, '.'^0x55, 'k'^0x55, 'e'^0x55, 'y'^0x55, 'a'^0x55, 'u'^0x55, 't'^0x55, 'h'^0x55, '.'^0x55, 'w'^0x55, 'i'^0x55, 'n'^0x55, 0 };
                for (int i = 0; i < 15; i++) obf[i] ^= 0x55;
                g_redirects.push_back(std::make_pair(std::string(obf), std::string("127.0.0.1")));
            }
            g_redirects.push_back(std::make_pair(std::string("license.server.com"), std::string("127.0.0.1")));
            g_redirects.push_back(std::make_pair(std::string("update.check.com"), std::string("127.0.0.1")));

            {
                char obf2[] = { 'a'^0x33, 'p'^0x33, 'i'^0x33, '.'^0x33, 'k'^0x33, 'e'^0x33, 'y'^0x33, 'a'^0x33, 'u'^0x33, 'x'^0x33, '.'^0x33, 'c'^0x33, 'o'^0x33, 'm'^0x33, 0 };
                for (int i = 0; i < 14; i++) obf2[i] ^= 0x33;
                g_redirects.push_back(std::make_pair(std::string(obf2), std::string("127.0.0.1")));
            }
        }

        bool IsFakeDomain(const std::string& host)
        {
            for (size_t i = 0; i < g_fakeDomains.size(); ++i)
            {
                if (host.find(g_fakeDomains[i]) != std::string::npos)
                    return true;
            }
            return false;
        }

        std::string GetFakeResponse(const std::string& host, const std::string& path)
        {
            for (size_t i = 0; i < g_endpoints.size(); ++i)
            {
                if (path.find(g_endpoints[i].path) != std::string::npos)
                    return g_endpoints[i].response;
            }
            return "{\"status\":\"ok\"}";
        }
    }

    namespace Honeypot
    {
        static bool g_honeypotAccessed = false;

        void CreateHoneypotFiles()
        {
            std::string paths[] = {
                "C:\\Windows\\System32\\drivers\\etc\\hosts.bak",
                "C:\\temp\\debug.log",
                "C:\\Users\\Public\\Documents\\config.ini",
                "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\debug.bat"
            };

            std::string content = "DEBUG: Application configuration data\n";

            for (int i = 0; i < 4; ++i)
            {
                HANDLE hFile = CreateFileA(
                    paths[i].c_str(),
                    GENERIC_WRITE,
                    0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

                if (hFile != INVALID_HANDLE_VALUE)
                {
                    DWORD bytesWritten;
                    WriteFile(hFile, content.c_str(), (DWORD)content.size(), &bytesWritten, NULL);
                    CloseHandle(hFile);
                    SetFileAttributesA(paths[i].c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
                }
            }
        }

        bool IsHoneypotAccessed() { return g_honeypotAccessed; }

        void TriggerAlert()
        {
            g_honeypotAccessed = true;
        }
    }

    namespace DecoyTraffic
    {
        static std::atomic<bool> g_decoyActive{ false };
        static std::thread g_decoyThread;

        static void DecoyTrafficThreadFunc()
        {
            while (g_decoyActive.load())
            {
                SOCKET decoySocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (decoySocket != INVALID_SOCKET)
                {
                    sockaddr_in serverAddr;
                    serverAddr.sin_family = AF_INET;
                    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        serverAddr.sin_port = htons(8443);

                    connect(decoySocket, (sockaddr*)&serverAddr, sizeof(serverAddr));

                    const char* request =
                        "GET /api/heartbeat HTTP/1.1\r\n"
                        "Host: api.example.com\r\n"
                        "User-Agent: Mozilla/5.0\r\n"
                        "\r\n";

                    send(decoySocket, request, (int)strlen(request), 0);

                    char buffer[1024] = { 0 };
                    recv(decoySocket, buffer, sizeof(buffer), 0);

                    closesocket(decoySocket);
                }

                Sleep(5000 + (rand() % 10000));
            }
        }

        void StartDecoyTraffic()
        {
            if (g_decoyActive.load()) return;
            g_decoyActive.store(true);
            g_decoyThread = std::thread(DecoyTrafficThreadFunc);
            g_decoyThread.detach();
        }

        void StopDecoyTraffic() { g_decoyActive.store(false); }
        bool IsDecoyActive() { return g_decoyActive.load(); }
    }
}
