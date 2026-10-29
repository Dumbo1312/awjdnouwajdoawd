#include "WebConfigServer.h"
#include "CrashLog.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include "User.h"
#include "WebPanelPage.h"
#include "../Utilities/Utilities.h"

#include "../KeyAux/lib/nlohmann/json.hpp"

#include <dshow.h>
#pragma comment(lib, "strmiids.lib")

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>

// ============================================================================
//  WebConfigServer.cpp
//  ---------------------------------------------------------------------------
//  WinSock2 loopback HTTP server (127.0.0.1:4890) that exposes the FULL User
//  state (the same fields the desktop ImGui menu edits) as JSON:
//
//    GET  /                       -> dashboard page (HTML)
//    GET  /config                 -> complete User state as JSON
//    POST /config                 -> JSON body applied to the live User and
//                                     saved straight into config\<game>.usr
//    GET  /windows                -> visible window titles (OBS target picker)
//    GET  /capture-devices        -> video capture devices (capture card)
//    POST /action {"start":true}  -> starts the app from the start menu state
//
//  The server binds only to the loopback interface.
// ============================================================================

namespace
{
    constexpr int kBufferSize = 64 * 1024;

    std::atomic<bool> g_running{ false };
    std::atomic<bool> g_exitRequested{ false };
    std::thread g_thread;
    SOCKET g_listenSock = INVALID_SOCKET;
    std::mutex g_userMutex;     // guards g_User while the server touches it
    User* g_User = nullptr;     // set by App via WebConfigServer::SetUser()

    // ------------------------------------------------------------------------
    //  Native keybind capture
    //  ---------------------------------------------------------------------------
    //  Browsers deliberately do NOT deliver events for mouse buttons 4/5
    //  (Firefox navigates directly, Chrome's behavior depends on the mouse
    //  driver). So while the web panel waits for a bind, the app itself
    //  polls GetAsyncKeyState for the mouse buttons and reports the result
    //  through /config. This is exactly how the old desktop GUI worked.
    // ------------------------------------------------------------------------
    std::atomic<int> g_captureVk{ -1 };               // detected VK, -1 = none
    std::atomic<bool> g_captureActive{ false };       // native listening active
    std::chrono::steady_clock::time_point g_captureStart;

    // ------------------------------------------------------------------------
    //  Enumeration helpers (mirrors GUI.cpp combos)
    // ------------------------------------------------------------------------
    static std::vector<std::string> EnumerateCaptureDevices()
    {
        std::vector<std::string> devices;

        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        bool needUninit = SUCCEEDED(hr);

        ICreateDevEnum* pDevEnum = nullptr;
        hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
            IID_ICreateDevEnum, (void**)&pDevEnum);

        if (SUCCEEDED(hr))
        {
            IEnumMoniker* pEnum = nullptr;
            hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);

            if (hr == S_OK && pEnum)
            {
                IMoniker* pMoniker = nullptr;
                while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
                {
                    IPropertyBag* pPropBag = nullptr;
                    hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);

                    if (SUCCEEDED(hr))
                    {
                        VARIANT varName;
                        VariantInit(&varName);
                        hr = pPropBag->Read(L"FriendlyName", &varName, 0);

                        if (SUCCEEDED(hr))
                        {
                            int len = WideCharToMultiByte(CP_UTF8, 0, varName.bstrVal, -1, NULL, 0, NULL, NULL);
                            std::string name(len - 1, 0);
                            WideCharToMultiByte(CP_UTF8, 0, varName.bstrVal, -1, &name[0], len, NULL, NULL);
                            devices.push_back(name);
                        }
                        VariantClear(&varName);
                        pPropBag->Release();
                    }
                    pMoniker->Release();
                }
                pEnum->Release();
            }
            pDevEnum->Release();
        }

        if (needUninit)
            CoUninitialize();

        if (devices.empty())
            devices.push_back("No capture devices found");

        return devices;
    }

    struct CaptureWindow { HWND hwnd; std::string title; };

    static std::vector<CaptureWindow> EnumerateCaptureWindows()
    {
        std::vector<CaptureWindow> windows;

        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL
        {
            auto* out = reinterpret_cast<std::vector<CaptureWindow>*>(lp);

            if (!IsWindowVisible(hwnd))                 return TRUE;
            if (GetWindow(hwnd, GW_OWNER) != nullptr)   return TRUE;

            LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOOLWINDOW)             return TRUE;

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId())           return TRUE;

            char title[256];
            int len = GetWindowTextA(hwnd, title, sizeof(title));
            if (len <= 0)                               return TRUE;

            RECT rc{};
            if (GetWindowRect(hwnd, &rc)) {
                if ((rc.right - rc.left) < 64 || (rc.bottom - rc.top) < 64)
                    return TRUE;
            }

            out->push_back({ hwnd, std::string(title, len) });
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));

        return windows;
    }

    // ------------------------------------------------------------------------
    //  User <-> JSON
    // ------------------------------------------------------------------------
    nlohmann::json UserToJson(const User& u)
    {
        nlohmann::json j;

        // Binds
        j["Binds"]["aim"]            = u.Binds.aim;
        j["Binds"]["toggleAim"]      = u.Binds.toggleAim;
        j["Binds"]["ads"]            = u.Binds.ads;
        j["Binds"]["toggleMenu"]     = u.Binds.toggleMenu;

        // Network / AI
        j["Network"]["modelChoice"]  = u.Network.modelChoice;
        j["Network"]["modelPath"]    = u.Network.modelPath;
        j["Network"]["availableModels"] = u.Network.availableModels;
        j["Network"]["confidence"]   = u.Network.confidence;
        j["Network"]["size"]         = u.Network.size;
        j["Network"]["brutalMode"]   = u.Network.brutalMode;
        j["Network"]["thirdPersonMultiplier"] = u.Network.thirdPersonMultiplier;

        // Input / setup
        j["Input"]["method"]         = static_cast<int>(u.Input.method);
        j["Input"]["game"]           = static_cast<int>(u.Input.game);
        j["Input"]["capture"]        = static_cast<int>(u.Input.capture);
        j["Input"]["captureCardDeviceIndex"] = u.Input.captureCardDeviceIndex;
        j["Input"]["customGameName"] = u.Input.customGameName;

        // HipFire
        j["HipFire"]["strength"]     = u.HipFire.strength;
        j["HipFire"]["fov"]          = u.HipFire.fov;
        j["HipFire"]["sens"]         = u.HipFire.sens;
        j["HipFire"]["deadzone"]     = u.HipFire.deadzone;
        j["HipFire"]["aimHeightChoice"] = static_cast<int>(u.HipFire.aimHeightChoice);

        // ADS
        j["Ads"]["strength"]         = u.Ads.strength;
        j["Ads"]["fov"]              = u.Ads.fov;
        j["Ads"]["sens"]             = u.Ads.sens;
        j["Ads"]["deadzone"]         = u.Ads.deadzone;
        j["Ads"]["aimHeightChoice"]  = static_cast<int>(u.Ads.aimHeightChoice);

        // Trigger
        j["Trigger"]["enabled"]      = u.Trigger.enabled;
        j["Trigger"]["proximity"]    = u.Trigger.proximity;
        j["Trigger"]["bind"]         = u.Trigger.bind;

        // Recoil
        j["Recoil"]["enabled"]       = u.Recoil.enabled;
        j["Recoil"]["strength"]      = u.Recoil.strength;
        j["Recoil"]["bind"]          = u.Recoil.bind;

        // Appearance
        j["Appearance"]["accentHex"] = std::string(u.Appearance.accentHex);

        // Internal info (read-only-ish)
        j["Internal"]["screenshotMs"] = u.Internal.screenshotMs;
        j["Internal"]["inferenceMs"]  = u.Internal.inferenceMs;
        j["Internal"]["processingMs"] = u.Internal.processingMs;
        j["Internal"]["ResolutionW"]  = u.Internal.ResolutionW;
        j["Internal"]["ResolutionH"]  = u.Internal.ResolutionH;
        j["Internal"]["appState"]     = static_cast<int>(u.Internal.appState);
        j["Internal"]["captureVk"]    = g_captureVk.load();
        j["Internal"]["captureActive"] = g_captureActive.load();

        // Combo lists so the page can render select boxes.
        // NOTE: windows / captureDevices are intentionally empty here; they are
        // enumerated on demand via GET /windows and GET /capture-devices so a
        // config poll never has to walk every top-level window.
        j["Lists"]["games"]          = { "Fortnite", "Valorant", "CS2", "Rainbow Six", "COD", "Warzone", "Rust", "Custom Game" };
        j["Lists"]["inputMethods"]   = { "Driver Input", "Win32", "GHub", "Razer", "RPBox", "Makcu" };
        j["Lists"]["aimHeights"]     = { "Chest", "Neck", "Head" };
        j["Lists"]["models"]         = u.Network.availableModels;
        j["Lists"]["windows"]        = nlohmann::json::array();
        j["Lists"]["captureDevices"] = nlohmann::json::array();
        j["Lists"]["captureMethods"] = { "Desktop Duplication", "WinRT Capture", "Capture Card" };

        return j;
    }

    // Applies known fields from `j` (nested, namespaced) onto `u`.
    // Unknown keys are ignored so partial payloads stay compatible.
    void JsonToUser(const nlohmann::json& j, User& u)
    {
        if (j.is_object() && j.value("reset", false))
        {
            u.ResetToDefaults();
            return;
        }
        if (!j.is_object()) return;

        auto has = [&j](const char* ns, const char* key) {
            return j.contains(ns) && j[ns].is_object() && j[ns].contains(key);
        };

        // Binds
        if (has("Binds", "aim")) u.Binds.aim = j["Binds"]["aim"].get<int>();
        if (has("Binds", "toggleAim")) u.Binds.toggleAim = j["Binds"]["toggleAim"].get<int>();
        if (has("Binds", "ads")) u.Binds.ads = j["Binds"]["ads"].get<int>();
        if (has("Binds", "toggleMenu")) u.Binds.toggleMenu = j["Binds"]["toggleMenu"].get<int>();

        // Network / AI
        if (has("Network", "modelChoice"))
        {
            int mc = j["Network"]["modelChoice"].get<int>();
            if (mc >= 0 && mc < static_cast<int>(u.Network.availableModels.size()))
            {
                u.Network.modelChoice = mc;
                u.Network.modelPath = u.Network.availableModels[mc];
            }
        }
        if (has("Network", "confidence"))
        {
            float c = j["Network"]["confidence"].get<float>();
            if (c >= 0.0f && c <= 1.0f) u.Network.confidence = c;
        }
        // brutalMode is always enabled (hidden option) - ignore any payload
        u.Network.brutalMode = true;
        {
        }
        if (has("Network", "thirdPersonMultiplier"))
        {
            float t = j["Network"]["thirdPersonMultiplier"].get<float>();
            if (t >= 1.0f && t <= 50.0f) u.Network.thirdPersonMultiplier = t;
        }

        // Input / setup
        if (has("Input", "method"))
        {
            int m = j["Input"]["method"].get<int>();
            if (m >= 0 && m <= static_cast<int>(InputMethod::Makcu)) u.Input.method = static_cast<InputMethod>(m);
        }
        if (has("Input", "capture"))
        {
            int c = j["Input"]["capture"].get<int>();
            if (c >= 0 && c <= static_cast<int>(CaptureMethod::CaptureCard)) u.Input.capture = static_cast<CaptureMethod>(c);
        }
        if (has("Input", "captureCardDeviceIndex"))
        {
            int idx = j["Input"]["captureCardDeviceIndex"].get<int>();
            if (idx >= 0) u.Input.captureCardDeviceIndex = idx;
        }
        if (has("Input", "customGameName"))
        {
            std::string name = j["Input"]["customGameName"].get<std::string>();
            if (name.size() < 128) u.Input.customGameName = name;
        }

        // HipFire
        if (has("HipFire", "strength"))
        {
            float s = j["HipFire"]["strength"].get<float>();
            if (s >= 0.0f && s <= 1.0f) u.HipFire.strength = s;
        }
        if (has("HipFire", "fov"))
        {
            int f = j["HipFire"]["fov"].get<int>();
            if (f >= 1) u.HipFire.fov = f;
        }
        if (has("HipFire", "sens"))
        {
            float s = j["HipFire"]["sens"].get<float>();
            if (s >= 1.0f && s <= 100.0f) u.HipFire.sens = s;
        }
        if (has("HipFire", "deadzone"))
        {
            int d = j["HipFire"]["deadzone"].get<int>();
            if (d >= 0 && d <= 10) u.HipFire.deadzone = d;
        }
        if (has("HipFire", "aimHeightChoice"))
        {
            int h = j["HipFire"]["aimHeightChoice"].get<int>();
            if (h >= 0 && h <= static_cast<int>(AimHeight::Head))
            {
                u.HipFire.aimHeightChoice = static_cast<AimHeight>(h);
                u.HipFire.aimHeight = AIMH2FLOAT.at(u.HipFire.aimHeightChoice);
            }
        }

        // ADS
        if (has("Ads", "strength"))
        {
            float s = j["Ads"]["strength"].get<float>();
            if (s >= 0.0f && s <= 1.0f) u.Ads.strength = s;
        }
        if (has("Ads", "fov"))
        {
            int f = j["Ads"]["fov"].get<int>();
            if (f >= 1) u.Ads.fov = f;
        }
        if (has("Ads", "sens"))
        {
            float s = j["Ads"]["sens"].get<float>();
            if (s >= 1.0f && s <= 100.0f) u.Ads.sens = s;
        }
        if (has("Ads", "deadzone"))
        {
            int d = j["Ads"]["deadzone"].get<int>();
            if (d >= 0 && d <= 10) u.Ads.deadzone = d;
        }
        if (has("Ads", "aimHeightChoice"))
        {
            int h = j["Ads"]["aimHeightChoice"].get<int>();
            if (h >= 0 && h <= static_cast<int>(AimHeight::Head))
            {
                u.Ads.aimHeightChoice = static_cast<AimHeight>(h);
                u.Ads.aimHeight = AIMH2FLOAT.at(u.Ads.aimHeightChoice);
            }
        }

        // Trigger
        if (has("Trigger", "enabled")) u.Trigger.enabled = j["Trigger"]["enabled"].get<bool>();
        if (has("Trigger", "proximity"))
        {
            int p = j["Trigger"]["proximity"].get<int>();
            if (p >= 0 && p <= 50) u.Trigger.proximity = p;
        }
        if (has("Trigger", "bind")) u.Trigger.bind = j["Trigger"]["bind"].get<int>();

        // Recoil
        if (has("Recoil", "enabled")) u.Recoil.enabled = j["Recoil"]["enabled"].get<bool>();
        if (has("Recoil", "strength"))
        {
            float s = j["Recoil"]["strength"].get<float>();
            if (s >= 0.01f && s <= 10.0f) u.Recoil.strength = s;
        }
        if (has("Recoil", "bind")) u.Recoil.bind = j["Recoil"]["bind"].get<int>();

        // Appearance
        if (has("Appearance", "accentHex"))
        {
            std::string hex = j["Appearance"]["accentHex"].get<std::string>();
            if (!hex.empty())
                strncpy_s(u.Appearance.accentHex, hex.c_str(), sizeof(u.Appearance.accentHex) - 1);
        }

        // Game switch runs LAST: SwitchGame() loads the target game's profile
        // from disk, so the fields applied above must not clobber the freshly
        // loaded per-game settings.
        if (has("Input", "game"))
        {
            int g = j["Input"]["game"].get<int>();
            if (g >= 0 && g <= static_cast<int>(GameChoice::Custom))
                u.SwitchGame(static_cast<GameChoice>(g));
        }
    }

    // ------------------------------------------------------------------------
    //  HTTP plumbing
    // ------------------------------------------------------------------------
    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::string body;
    };

    bool ParseRequestLine(const std::string& head, HttpRequest& req)
    {
        size_t eol = head.find("\r\n");
        std::string line = (eol == std::string::npos) ? head : head.substr(0, eol);

        size_t s1 = line.find(' ');
        size_t s2 = line.rfind(' ');
        if (s1 == std::string::npos || s2 == std::string::npos || s2 <= s1) return false;

        req.method = line.substr(0, s1);
        req.path = line.substr(s1 + 1, s2 - s1 - 1);
        if (req.path.find('?') != std::string::npos)
            req.path = req.path.substr(0, req.path.find('?'));
        return true;
    }

    void SendResponse(SOCKET sock, int status, const char* statusText,
                      const char* contentType, const std::string& body)
    {
        char head[512];
        std::snprintf(head, sizeof(head),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %llu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-store\r\n"
            "\r\n",
            status, statusText, contentType, static_cast<unsigned long long>(body.size()));

        send(sock, head, static_cast<int>(std::strlen(head)), 0);
        if (!body.empty())
            send(sock, body.c_str(), static_cast<int>(body.size()), 0);
    }

    bool TryReadExternalPage(std::string& out)
    {
        wchar_t exePath[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return false;

        std::wstring dir(exePath, len);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return false;
        dir.resize(slash + 1);
        dir += L"webpanel\\index.html";

        std::ifstream f(dir, std::ios::binary);
        if (!f) return false;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.empty()) return false;
        out = std::move(content);
        return true;
    }

    bool ReadRequestEx(SOCKET sock, HttpRequest& req)
    {
        char buf[4096];
        std::string acc;
        size_t contentLength = 0;

        for (;;)
        {
            int n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) return false;
            acc.append(buf, static_cast<size_t>(n));

            size_t he = acc.find("\r\n\r\n");
            if (he != std::string::npos)
            {
                size_t pos = acc.find("Content-Length:");
                if (pos == std::string::npos)
                    pos = acc.find("content-length:");
                if (pos != std::string::npos && pos < he)
                    contentLength = static_cast<size_t>(std::strtoul(acc.c_str() + pos + 15, nullptr, 10));

                size_t haveBody = acc.size() - (he + 4);
                if (haveBody >= contentLength)
                {
                    if (!ParseRequestLine(acc.substr(0, he), req)) return false;
                    req.body = acc.substr(he + 4, contentLength);
                    return true;
                }
            }
            if (acc.size() > kBufferSize) return false;
        }
    }

    void HandleClient2(SOCKET sock)
    {
        HttpRequest req;
        if (!ReadRequestEx(sock, req)) return;

        if (req.method == "GET" || req.method == "HEAD")
        {
            if (req.path == "/" || req.path == "/index.html")
            {
                std::string page;
                if (TryReadExternalPage(page))
                    SendResponse(sock, 200, "OK", "text/html; charset=utf-8", page);
                else
                    SendResponse(sock, 200, "OK", "text/html; charset=utf-8", kWebPanelHtml);
                return;
            }

            if (req.path == "/config")
            {
                std::lock_guard<std::mutex> lock(g_userMutex);
                if (!g_User)
                {
                    SendResponse(sock, 503, "Service Unavailable", "application/json; charset=utf-8",
                                 "{\"error\":\"no user\"}");
                    return;
                }
                std::string json = UserToJson(*g_User).dump();
                SendResponse(sock, 200, "OK", "application/json; charset=utf-8", json);
                return;
            }

            if (req.path == "/windows")
            {
                auto wins = EnumerateCaptureWindows();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& w : wins) arr.push_back(w.title);
                SendResponse(sock, 200, "OK", "application/json; charset=utf-8", arr.dump());
                return;
            }

            if (req.path == "/capture-devices")
            {
                auto devs = EnumerateCaptureDevices();
                SendResponse(sock, 200, "OK", "application/json; charset=utf-8",
                             nlohmann::json(devs).dump());
                return;
            }
        }
        else if (req.method == "POST")
        {
            if (req.path == "/config")
            {
                std::lock_guard<std::mutex> lock(g_userMutex);
                if (!g_User)
                {
                    SendResponse(sock, 503, "Service Unavailable", "application/json; charset=utf-8",
                                 "{\"error\":\"no user\"}");
                    return;
                }
                try
                {
                    nlohmann::json j = nlohmann::json::parse(req.body);
                    JsonToUser(j, *g_User);
                    g_User->SaveSettingsNow();
                    std::string json = UserToJson(*g_User).dump();
                    SendResponse(sock, 200, "OK", "application/json; charset=utf-8", json);
                }
                catch (const std::exception&)
                {
                    SendResponse(sock, 400, "Bad Request", "application/json; charset=utf-8",
                                 "{\"error\":\"invalid JSON\"}");
                }
                return;
            }

            if (req.path == "/action")
            {
                std::lock_guard<std::mutex> lock(g_userMutex);
                if (!g_User)
                {
                    SendResponse(sock, 503, "Service Unavailable", "application/json; charset=utf-8",
                                 "{\"error\":\"no user\"}");
                    return;
                }
                try
                {
                    nlohmann::json j = nlohmann::json::parse(req.body);
                    if (j.contains("license"))
                    {
                        // KeyAux license activation from the web login screen.
                        std::string key = j["license"].get<std::string>();
                        bool remember = j.value("remember", false);
                        if (key.empty())
                        {
                            SendResponse(sock, 200, "OK", "application/json; charset=utf-8",
                                         "{\"ok\":false,\"message\":\"empty key\"}");
                            return;
                        }
                        if (LicenseServer::License(key))
                        {
                            if (remember)
                                LicenseServer::SaveCredentials(true, "", "", key);
                            else
                                LicenseServer::ClearSavedCredentials();
                            if (g_User->Internal.appState == AppState::Login)
                                g_User->Internal.appState = AppState::StartMenu;
                            SendResponse(sock, 200, "OK", "application/json; charset=utf-8",
                                         "{\"ok\":true,\"message\":\"valid\",\"expiry\":\"" +
                                         LicenseServer::GetSubscription() + "\"}");
                        }
                        else
                        {
                            SendResponse(sock, 200, "OK", "application/json; charset=utf-8",
                                         "{\"ok\":false,\"message\":\"invalid\"}");
                        }
                        return;
                    }
                    if (j.value("start", false))
                    {
                        if (g_User->Internal.appState == AppState::StartMenu ||
                            g_User->Internal.appState == AppState::Login)
                        {
                            g_User->Internal.appState = AppState::Loading;
                        }
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                        return;
                    }
                    if (j.value("stop", false))
                    {
                        // STOP = shut the app down completely. The panel tab
                        // shows a "closed" notice; the user reopens the exe
                        // to start again.
                        g_exitRequested = true;
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                        return;
                    }
                    if (j.value("discord", false))
                    {
                        // Open the Discord invite in the default browser.
                        ShellExecuteA(nullptr, "open", "https://discord.gg/FntYdwbm8m",
                                      nullptr, nullptr, SW_SHOWNORMAL);
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8",
                                     "{\"ok\":true}");
                        return;
                    }
                    if (j.value("exit", false))
                    {
                        g_exitRequested = true;
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                        return;
                    }
                    if (j.value("capture", false))
                    {
                        // start native mouse-button capture (browser can't see
                        // X1/X2, so the app watches GetAsyncKeyState itself)
                        g_captureVk = -1;
                        g_captureActive = true;
                        g_captureStart = std::chrono::steady_clock::now();
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                        return;
                    }
                    if (j.value("captureDone", false))
                    {
                        g_captureActive = false;
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                        return;
                    }
                    if (j.value("captureClear", false))
                    {
                        g_captureVk = -1;
                        g_captureActive = false;
                        SendResponse(sock, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                        return;
                    }
                }
                catch (const std::exception&)
                {
                }
                SendResponse(sock, 400, "Bad Request", "application/json; charset=utf-8",
                             "{\"error\":\"bad action\"}");
                return;
            }
        }

        SendResponse(sock, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
    }

    void ServerThread(unsigned short port)
    {
        CrashLog::Log(("THREAD WebConfigServer::ServerThread tid=" + std::to_string(GetCurrentThreadId())).c_str());
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET)
        {
            WSACleanup();
            return;
        }
        g_listenSock = listenSock;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
            listen(listenSock, SOMAXCONN) == SOCKET_ERROR)
        {
            closesocket(listenSock);
            g_listenSock = INVALID_SOCKET;
            WSACleanup();
            return;
        }

        g_running = true;
        while (g_running)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listenSock, &readfds);

            timeval tv{ 0, 200000 };
            int sel = select(0, &readfds, nullptr, nullptr, &tv);
            if (sel <= 0)
            {
                // No client - use the idle time to watch for native captures
                if (g_captureActive)
                {
                    auto now = std::chrono::steady_clock::now();
                    auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_captureStart).count();
                    if (since > 6000)
                    {
                        g_captureActive = false;
                    }
                    else if (since > 300)
                    {
                        // grace period so the click that started the capture
                        // is not mistaken for the bound button
                        static const int mouseVks[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
                        for (int vk : mouseVks)
                        {
                            if (GetAsyncKeyState(vk) & 0x8000)
                            {
                                g_captureVk = vk;
                                g_captureActive = false;
                                break;
                            }
                        }
                    }
                }
                continue;
            }
            if (!g_running) break;

            SOCKET client = accept(listenSock, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;

            DWORD timeout = 3000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            HandleClient2(client);
            closesocket(client);
        }

        if (listenSock != INVALID_SOCKET)
        {
            closesocket(listenSock);
            g_listenSock = INVALID_SOCKET;
        }
        g_running = false;
        WSACleanup();
    }
}

namespace WebConfigServer
{
    void SetUser(User* user)
    {
        std::lock_guard<std::mutex> lock(g_userMutex);
        g_User = user;
    }

    bool Start(unsigned short port)
    {
        if (g_running) return true;

        g_thread = std::thread(ServerThread, port);
        for (int i = 0; i < 100 && !g_running && g_thread.joinable(); ++i)
            Sleep(10);

        return g_running;
    }

    void Stop()
    {
        if (!g_running) return;
        g_running = false;
        if (g_thread.joinable()) g_thread.join();
    }

    bool IsRunning()
    {
        return g_running;
    }

    void RequestExit()
    {
        g_exitRequested = true;
    }

    bool IsExitRequested()
    {
        return g_exitRequested;
    }
}
