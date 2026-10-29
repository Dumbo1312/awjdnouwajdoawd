#include "KeyauxClient.h"
#include "ServerSecureClient.h"
#include "SecurePayload.h"
#include "KeyauxSecret.gen.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"
#include "../Utilities/VirtualizerSDK.h"
#include "../Utilities/RuntimeProtection.h"
#include "../Core/CrashLog.h"

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace KeyauxClient
{
    static std::string g_lastMessage;
    static std::string g_expiry;

    static std::string GetHwid()
    {
        OBF_STACK_AND_PROLOGUE_JUNK(_hwid_stack_junk, 96);
        CALLER();
        OBF_BOGUS_FLOW_CASCADE();

        char volName[MAX_PATH + 1] = { 0 };
        DWORD serial = 0;
        DWORD maxCompLen = 0;
        DWORD fsFlags = 0;
        using pGetVolInfo_t = BOOL(WINAPI*)(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD);
        using pGetCompName_t = BOOL(WINAPI*)(LPSTR, LPDWORD);
        auto pGetVolInfo = reinterpret_cast<pGetVolInfo_t>(STEALTH_API_OBFSTR("kernel32.dll", "GetVolumeInformationA"));
        auto pGetCompName = reinterpret_cast<pGetCompName_t>(STEALTH_API_OBFSTR("kernel32.dll", "GetComputerNameA"));
        VIRTUALIZER_MUTATE_ONLY_START
        if (pGetVolInfo)
            pGetVolInfo(OBFUSCATE_STRING("C:\\").c_str(), volName, MAX_PATH, &serial, &maxCompLen, &fsFlags, NULL, 0);
        OBF_CALL_ANY_LOCAL_JUNK();

        char compName[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
        DWORD compSize = MAX_COMPUTERNAME_LENGTH + 1;
        if (pGetCompName)
            pGetCompName(compName, &compSize);
        VIRTUALIZER_MUTATE_ONLY_END

        CALLER();

        std::ostringstream oss;
        oss << serial;
        OBF_CALL_ANY_LOCAL_JUNK();
        oss << OBFUSCATE_STRING("-") << compName;
        OBF_BOGUS_FLOW_CASCADE();
        return oss.str();
    }

    static bool PassesProtectionGate()
    {
        volatile int _gate_junk = OBF_CALL_ANY_LOCAL_JUNK();
        CALLER();
        OBF_BOGUS_FLOW_CASCADE();

        if (RuntimeProtection::PEBCheck::IsBeingDebugged()) return false;
        if (RuntimeProtection::AntiDebug::IsDebuggerPresent()) return false;
        RuntimeProtection::AntiDebug::CheckRemoteDebugger();
        if (RuntimeProtection::HardwareBreakpoint::DetectHardwareBreakpoints()) return false;
        if (RuntimeProtection::TimingCheck::DetectTimingAnomaly()) return false;
        if (RuntimeProtection::IATProtection::IsIATTampered()) return false;
        if (RuntimeProtection::HookDetection::DetectInlineHooks()) return false;

        _gate_junk ^= OBF_CALL_ANY_LOCAL_JUNK();
        return _gate_junk == _gate_junk;
    }

    static std::string DecodeSecret()
    {
        if (!PassesProtectionGate())
            return std::string();

        CALLER();
        OBF_BOGUS_FLOW_SCRAMBLE();

        std::string out;
        out.reserve(KeyauxSecretGen::K8_SECRET_LEN);
        volatile unsigned int _obf_acc = 0;
        for (size_t i = 0; i < KeyauxSecretGen::K8_SECRET_LEN; ++i)
        {
            OBF_CALL_ANY_LOCAL_JUNK();
            unsigned char c = (unsigned char)(KeyauxSecretGen::K8_ENCODED_SECRET[i] ^ KeyauxSecretGen::K8_SECRET_XOR_KEY[i]);
            _obf_acc ^= c;
            out.push_back((char)c);
        }
        NOP();

        if ((_obf_acc & 0xFFu) != (unsigned int)KeyauxSecretGen::K8_SECRET_XOR_SUM)
        {
            SecureZeroMemory(out.data(), out.size());
            return std::string();
        }
        return out;
    }

    static bool HashSha256(const unsigned char* data, size_t len, unsigned char out[32])
    {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (st != 0) return false;
        st = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
        if (st != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
        st = BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
        if (st == 0) st = BCryptFinishHash(hHash, out, 32, 0);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return st == 0;
    }

    static bool ConstTimeEq(const unsigned char* a, const unsigned char* b)
    {
        unsigned char d = 0;
        for (int i = 0; i < 32; ++i) d |= (unsigned char)(a[i] ^ b[i]);
        return d == 0;
    }

    static bool VerifyServerCert(const CERT_CONTEXT* pCert)
    {
        if (!pCert || !pCert->pbCertEncoded || pCert->cbCertEncoded == 0) return false;

        unsigned char fullHash[32] = { 0 };
        unsigned char pkHash[32] = { 0 };
        if (!HashSha256(pCert->pbCertEncoded, pCert->cbCertEncoded, fullHash)) return false;

        const CRYPT_BIT_BLOB& pk = pCert->pCertInfo->SubjectPublicKeyInfo.PublicKey;
        if (!pk.pbData || pk.cbData == 0) return false;
        if (!HashSha256(pk.pbData, pk.cbData, pkHash)) return false;

        CALLER();
        return ConstTimeEq(fullHash, KeyauxSecretGen::K8_PIN_FULLCERT) ||
               ConstTimeEq(pkHash, KeyauxSecretGen::K8_PIN_PUBKEY);
    }

    static std::string HttpPost(const std::string& url, const std::string& path,
        const std::string& secret, const std::string& body)
    {
        OBF_STACK_AND_PROLOGUE_JUNK(_http_stack_junk, 128);
        CALLER();
        OBF_BOGUS_FLOW_GRID();

        using pWinHttpOpen_t = HINTERNET(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
        using pWinHttpConnect_t = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
        using pWinHttpOpenRequest_t = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
        using pWinHttpSendRequest_t = BOOL(WINAPI*)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
        using pWinHttpReceiveResponse_t = BOOL(WINAPI*)(HINTERNET, LPVOID);
        using pWinHttpQueryDataAvailable_t = BOOL(WINAPI*)(HINTERNET, LPDWORD);
        using pWinHttpReadData_t = BOOL(WINAPI*)(HINTERNET, LPVOID, DWORD, LPDWORD);
        using pWinHttpCloseHandle_t = BOOL(WINAPI*)(HINTERNET);
        using pWinHttpQueryOption_t = BOOL(WINAPI*)(HINTERNET, DWORD, LPVOID, LPDWORD);

        auto pWinHttpOpen = reinterpret_cast<pWinHttpOpen_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpOpen"));
        auto pWinHttpConnect = reinterpret_cast<pWinHttpConnect_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpConnect"));
        auto pWinHttpOpenRequest = reinterpret_cast<pWinHttpOpenRequest_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpOpenRequest"));
        auto pWinHttpSendRequest = reinterpret_cast<pWinHttpSendRequest_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpSendRequest"));
        auto pWinHttpReceiveResponse = reinterpret_cast<pWinHttpReceiveResponse_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpReceiveResponse"));
        auto pWinHttpQueryDataAvailable = reinterpret_cast<pWinHttpQueryDataAvailable_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpQueryDataAvailable"));
        auto pWinHttpReadData = reinterpret_cast<pWinHttpReadData_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpReadData"));
        auto pWinHttpCloseHandle = reinterpret_cast<pWinHttpCloseHandle_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpCloseHandle"));
        auto pWinHttpQueryOption = reinterpret_cast<pWinHttpQueryOption_t>(STEALTH_API_OBFSTR("winhttp.dll", "WinHttpQueryOption"));

        if (!pWinHttpOpen || !pWinHttpConnect || !pWinHttpOpenRequest || !pWinHttpSendRequest ||
            !pWinHttpReceiveResponse || !pWinHttpQueryDataAvailable || !pWinHttpReadData ||
            !pWinHttpCloseHandle || !pWinHttpQueryOption)
            return std::string();

        std::wstring wurl(url.begin(), url.end());
        std::wstring wpath(path.begin(), path.end());
        std::wstring wsecret(secret.begin(), secret.end());

        VIRTUALIZER_MUTATE_ONLY_START
        std::wstring wua;
        wua += OBFUSCATE_WSTRING(L"KeyAux ");
        wua += OBFUSCATE_WSTRING(L"Client/1.0");
        HINTERNET hSession = pWinHttpOpen(wua.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!hSession) return std::string();
        VIRTUALIZER_MUTATE_ONLY_END

        OBF_CALL_ANY_LOCAL_JUNK();
        CALLER();
        OBF_BOGUS_FLOW_WEAVER();

        VIRTUALIZER_MUTATE_ONLY_START
        HINTERNET hConnect = pWinHttpConnect(hSession, wurl.c_str(),
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            pWinHttpCloseHandle(hSession);
            return std::string();
        }
        VIRTUALIZER_MUTATE_ONLY_END

        OBF_CALL_ANY_LOCAL_JUNK();

        VIRTUALIZER_MUTATE_ONLY_START
        HINTERNET hRequest = pWinHttpOpenRequest(hConnect, OBFUSCATE_WSTRING(L"POST").c_str(), wpath.c_str(),
            NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            pWinHttpCloseHandle(hConnect);
            pWinHttpCloseHandle(hSession);
            return std::string();
        }

        VIRTUALIZER_MUTATE_ONLY_END

        OBF_CALL_ANY_LOCAL_JUNK();
        CALLER();

        VIRTUALIZER_MUTATE_ONLY_START
        std::wstring headers = OBFUSCATE_WSTRING(L"X-App-Secret: ") + wsecret +
            OBFUSCATE_WSTRING(L"\r\nContent-Type: application/json\r\n");

        pWinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
            (LPVOID)body.c_str(), body.length(), body.length(), 0);
        if (!pWinHttpReceiveResponse(hRequest, NULL))
        {
            pWinHttpCloseHandle(hRequest);
            pWinHttpCloseHandle(hConnect);
            pWinHttpCloseHandle(hSession);
            return std::string();
        }
        VIRTUALIZER_MUTATE_ONLY_END

        OBF_CALL_ANY_LOCAL_JUNK();

        {
            CERT_CONTEXT* pServerCert = nullptr;
            DWORD certSize = sizeof(pServerCert);
            bool certOk = false;
            if (pWinHttpQueryOption(hRequest, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &pServerCert, &certSize) && pServerCert)
            {
                certOk = VerifyServerCert(pServerCert);
                CertFreeCertificateContext(pServerCert);
            }
            OBF_CALL_ANY_LOCAL_JUNK();
            if (!certOk)
            {
                pWinHttpCloseHandle(hRequest);
                pWinHttpCloseHandle(hConnect);
                pWinHttpCloseHandle(hSession);
                return std::string();
            }
        }

        OBF_BOGUS_FLOW_CASCADE();

        VIRTUALIZER_MUTATE_ONLY_START
        std::string result;
        DWORD bytesAvailable = 0;
        while (pWinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
        {
            std::vector<char> buffer(bytesAvailable + 1);
            DWORD bytesRead = 0;
            if (pWinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
                result.append(buffer.data(), bytesRead);
        }
        VIRTUALIZER_MUTATE_ONLY_END

        OBF_CALL_ANY_LOCAL_JUNK();
        CALLER();

        pWinHttpCloseHandle(hRequest);
        pWinHttpCloseHandle(hConnect);
        pWinHttpCloseHandle(hSession);
        return result;
    }

    bool Activate(const std::string& key)
    {
        OBF_BOGUS_FLOW_CYCLONE();

        std::string url;
        url += OBFUSCATE_STRING("api.");
        url += OBFUSCATE_STRING("keyaux");
        url += OBFUSCATE_STRING(".com");

        std::string path;
        path += OBFUSCATE_STRING("/api/");
        path += OBFUSCATE_STRING("v1/");
        path += OBFUSCATE_STRING("licenses/");
        path += OBFUSCATE_STRING("activate");

        std::string secret = DecodeSecret();
        if (secret.empty())
        {
            g_lastMessage = OBFUSCATE_STRING("x");
            return false;
        }

        CALLER();

        {
            obf_vm_engine::VMState vm_s(_obf_global_opaque_seed);
            vm_s.r0 = OBF_MBA_XOR((unsigned int)_obf_global_opaque_seed, 0xABCDEF01U + (unsigned int)__LINE__);
            vm_s.r1 = (unsigned int)__TIME__[0] ^ (unsigned int)__COUNTER__;
            vm_s.r2 = OBF_MBA_ADD((unsigned int)__TIME__[2], (unsigned int)_obf_global_opaque_seed);
            vm_s.dispatch_key = (vm_s.r0 ^ vm_s.r1) | 1U;
            vm_s.pc = ((unsigned int)__TIME__[4] ^ (unsigned int)_obf_global_opaque_seed) % obf_vm_engine::HANDLER_COUNT;
            constexpr size_t VM_BYTECODE_LEN = 24;
            std::array<unsigned int, VM_BYTECODE_LEN> _obf_vm_bytecode;
            for (size_t i_bc = 0; i_bc < VM_BYTECODE_LEN; ++i_bc)
            {
                unsigned int bc_val = (unsigned int)(i_bc * 17U) + (unsigned int)__COUNTER__ + (unsigned int)_obf_global_opaque_seed;
                unsigned int encryption_key = OBF_MBA_ADD(0xDEADBEEFU, (i_bc * 0x101U) ^ (unsigned int)__LINE__);
                _obf_vm_bytecode[i_bc] = OBF_MBA_XOR(bc_val, encryption_key);
            }
            for (int iter_vm = 0; iter_vm < 12; ++iter_vm)
            {
                NOP();
                unsigned int handler_index = OBF_MBA_XOR(
                    _obf_vm_bytecode[vm_s.pc % VM_BYTECODE_LEN],
                    OBF_MBA_ADD(0xDEADBEEFU, ((vm_s.pc % VM_BYTECODE_LEN) * 0x101U) ^ (unsigned int)__LINE__)
                ) % obf_vm_engine::VM_HANDLER_TABLE_SIZE;
                obf_vm_engine::vm_handler_table[handler_index](vm_s, 0, nullptr);
                vm_s.pc = OBF_MBA_ADD(vm_s.pc, (vm_s.r0 & 3U) + 1U) % VM_BYTECODE_LEN;
            }
            obf_vm_engine::dsptch(vm_s, 0, nullptr, 8u);
            HANDLER_TABLE_MUTATE(obf_vm_engine::vm_handler_table, obf_vm_engine::VM_HANDLER_TABLE_SIZE, vm_s);
            OBF_HEAVY_JUNK_OP(vm_s.r0, vm_s.r1 ^ (unsigned int)__LINE__);
        }

        nlohmann::json j;
        j[OBFUSCATE_STRING("key")] = key;
        j[OBFUSCATE_STRING("hwid")] = GetHwid();
        std::string body = j.dump();

        OBF_BOGUS_FLOW_SCRAMBLE();

        std::string response = HttpPost(url, path, secret, body);
        SecureZeroMemory(secret.data(), secret.size());
        secret.clear();
        CrashLog::Log(("KeyAux activate response: " +
                       (response.empty() ? std::string("(empty)") : response.substr(0, 200))).c_str());
        if (response.empty())
        {
            SecureZeroMemory(body.data(), body.size());
            g_lastMessage = OBFUSCATE_STRING("x");
            return false;
        }

        CALLER();
        OBF_CALL_ANY_LOCAL_JUNK();

        bool success = (response.find(OBFUSCATE_STRING("\"success\":true")) != std::string::npos) ||
            (response.find(OBFUSCATE_STRING("\"success\": true")) != std::string::npos);

        if (!success)
        {
            SecureZeroMemory(body.data(), body.size());
            SecureZeroMemory(response.data(), response.size());
            g_lastMessage = OBFUSCATE_STRING("x");
            return false;
        }

        {
            auto expPos = response.find(OBFUSCATE_STRING("\"expires\":\""));
            if (expPos != std::string::npos)
            {
                auto start = expPos + 10;
                auto end = response.find(OBFUSCATE_STRING("\""), start);
                if (end != std::string::npos)
                    g_expiry = response.substr(start, end - start);
                else
                    g_expiry = OBFUSCATE_STRING("Unknown");
            }
            else
                g_expiry = OBFUSCATE_STRING("Unknown");
        }

        OBF_CALL_ANY_LOCAL_JUNK();
        CALLER();
        OBF_BOGUS_FLOW_CASCADE();

        g_lastMessage = OBFUSCATE_STRING("x");

        bool serverOk = ServerSecureClient::Initialize(key);

        if (serverOk && ServerSecureClient::VerifySessionIntegrity())
        {
            const auto& session = ServerSecureClient::GetSession();
            SecurePayload::SetSessionKey(session.aesKey.data(), session.aesIv.data());
        }

        SecureZeroMemory(body.data(), body.size());
        SecureZeroMemory(response.data(), response.size());
        return true;
    }

    std::string GetLastMessage()
    {
        CALLER();
        return g_lastMessage;
    }

    std::string GetSavePath()
    {
        OBF_STACK_AND_PROLOGUE_JUNK(_savepath_stack_junk, 64);
        CALLER();
        OBF_BOGUS_FLOW_GRID();

        char sysDir[MAX_PATH];
        using pGetSysDir_t = UINT(WINAPI*)(LPSTR, UINT);
        using pCreateDir_t = BOOL(WINAPI*)(LPCSTR, LPSECURITY_ATTRIBUTES);
        auto pGetSysDir = reinterpret_cast<pGetSysDir_t>(STEALTH_API_OBFSTR("kernel32.dll", "GetSystemDirectoryA"));
        auto pCreateDir = reinterpret_cast<pCreateDir_t>(STEALTH_API_OBFSTR("kernel32.dll", "CreateDirectoryA"));
        if (pGetSysDir)
            pGetSysDir(sysDir, MAX_PATH);
        std::string dir = std::string(sysDir) + OBFUSCATE_STRING("\\node");
        if (pCreateDir)
            pCreateDir(dir.c_str(), 0);
        OBF_CALL_ANY_LOCAL_JUNK();
        return dir + OBFUSCATE_STRING("\\node2.json");
    }

    std::string GetExpiry()
    {
        CALLER();
        return g_expiry.empty() ? OBFUSCATE_STRING("Unknown") : g_expiry;
    }
}
