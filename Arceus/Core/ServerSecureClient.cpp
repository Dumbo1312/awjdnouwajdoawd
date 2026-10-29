#include "ServerSecureClient.h"
#include "SecurePayload.h"
#include "../Utilities/VirtualizerSDK.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <random>
#include <chrono>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <atomic>

#include "../Utilities/OfflineGrantKey.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
    static SessionContext g_session;
    static bool g_initialized = false;
    static std::atomic<int> g_heartbeatFailCount{ 0 };

    constexpr const int SECURE_SERVER_PORT = 8443;
    static const std::string g_secureHost = OBFUSCATE_STRING("127.0.0.1");

    // --- Cert pinning za secure server (SHA-256 serverovog certa) ---
    static constexpr std::array<uint8_t, 32> EXPECTED_CERT_PIN = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };

    // ------------------------------------------------------------------
    // Fallback sesija (samo kad server nije dostupan)
    // ------------------------------------------------------------------
    static void GenerateFallbackSession()
    {
        VIRTUALIZER_MUTATE_ONLY_START
        OBF_BOGUS_FLOW_LABYRINTH();
        std::random_device rd;
        std::mt19937_64 gen(rd());

        for (auto& b : g_session.aesKey)
            b = static_cast<uint8_t>(gen() & 0xFF);
        for (auto& b : g_session.aesIv)
            b = static_cast<uint8_t>(gen() & 0xFF);

        g_session.aimOffsetX = 0.0;
        g_session.aimOffsetY = 0.0;
        OBF_CALL_ANY_LOCAL_JUNK();
        g_session.noiseSeed = gen();
        g_session.confidenceBias = 0.0f;
        g_session.strengthModifier = 1.0f;
        g_session.serverTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        VIRTUALIZER_MUTATE_ONLY_END
    }

    // ------------------------------------------------------------------
    // Secure server (cert pinning + remote computation)
    // ------------------------------------------------------------------
    static bool VerifyCertificatePin(HINTERNET hRequest)
    {
        DWORD certSize = 0;
        WinHttpQueryOption(hRequest, WINHTTP_OPTION_SECURITY_CERTIFICATE_STRUCT, NULL, &certSize);
        if (!certSize) return false;

        std::vector<uint8_t> certBuf(certSize);
        if (!WinHttpQueryOption(hRequest, WINHTTP_OPTION_SECURITY_CERTIFICATE_STRUCT,
            certBuf.data(), &certSize))
            return false;

        auto pCertInfo = reinterpret_cast<CERT_CHAIN_CONTEXT*>(certBuf.data());
        if (!pCertInfo || !pCertInfo->rgpChain ||
            !pCertInfo->rgpChain[0] || !pCertInfo->rgpChain[0]->rgpElement ||
            !pCertInfo->rgpChain[0]->rgpElement[0])
            return false;

        PCCERT_CONTEXT pCert = pCertInfo->rgpChain[0]->rgpElement[0]->pCertContext;
        if (!pCert) return false;

        DWORD hashSize = 32;
        uint8_t hash[32];
        if (!CryptHashCertificate(NULL, CALG_SHA_256, 0,
            pCert->pbCertEncoded, pCert->cbCertEncoded, hash, &hashSize))
            return false;

        if (hashSize != 32) return false;

        return std::memcmp(hash, EXPECTED_CERT_PIN.data(), 32) == 0;
    }

    static std::string HttpPostSecure(const std::string& path, const std::string& body)
    {
        std::wstring wHost(g_secureHost.begin(), g_secureHost.end());
        std::wstring wPath(path.begin(), path.end());

        HINTERNET hSession = WinHttpOpen(OBFUSCATE_WSTRING(L"SecureClient/2.0").c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!hSession) return {};

        HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), SECURE_SERVER_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

        DWORD flags = WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH;
        if (SECURE_SERVER_PORT == 8443 && strcmp(g_secureHost.c_str(), OBFUSCATE_STRING("127.0.0.1").c_str()) == 0)
            flags = 0;

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, OBFUSCATE_WSTRING(L"POST").c_str(),
            wPath.c_str(), NULL, NULL, NULL, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

        if (flags & WINHTTP_FLAG_SECURE)
        {
            DWORD secureFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secureFlags, sizeof(secureFlags));

            if (!VerifyCertificatePin(hRequest))
            {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return {};
            }
        }

        std::wstring headers = OBFUSCATE_WSTRING(L"Content-Type: application/json\r\n"
            L"X-Session-Protocol: 2\r\n");

        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
            (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0))
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return {};
        }

        if (!WinHttpReceiveResponse(hRequest, NULL))
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return {};
        }

        std::string result;
        DWORD bytesAvail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0)
        {
            std::vector<char> buf(bytesAvail + 1);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, buf.data(), bytesAvail, &bytesRead))
                result.append(buf.data(), bytesRead);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // ------------------------------------------------------------------
    // HWID
    // ------------------------------------------------------------------
    static std::string GetHwid()
    {
        VIRTUALIZER_MUTATE_ONLY_START
        char volName[MAX_PATH + 1] = { 0 };
        DWORD serial = 0;
        DWORD maxCompLen = 0;
        DWORD fsFlags = 0;
        GetVolumeInformationA(OBFUSCATE_STRING("C:\\").c_str(), volName, MAX_PATH, &serial, &maxCompLen, &fsFlags, NULL, 0);

        char compName[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
        DWORD compSize = MAX_COMPUTERNAME_LENGTH + 1;
        GetComputerNameA(compName, &compSize);

        int cpuInfo[4] = { 0 };
        __cpuid(cpuInfo, 0);

        std::ostringstream oss;
        oss << std::hex << serial << OBFUSCATE_STRING("-") << compName << OBFUSCATE_STRING("-") << cpuInfo[1];
        VIRTUALIZER_MUTATE_ONLY_END
        return oss.str();
    }

    // ------------------------------------------------------------------
    // Offline grant — server-signed license proof.
    //
    // The fallback session (used when the secure server is unreachable) is
    // only unlocked by a FRESH SERVER SIGNATURE (RSA-2048 PKCS#1 v1.5 over a
    // compact JSON payload bound to hwid + license + issue time). Without it
    // the client keeps no session key -> ApplyCalibrationOffset returns
    // rawX*1000 -> the aim path is dead. Blocking the server therefore no
    // longer yields a working build; only the real server can mint grants
    // (the private key exists solely server-side).
    //
    // Grant format (see Tools/server_grant_signing.py):
    //   payload = {"ts":..., "hwid":..., "license":...}  (compact JSON)
    //   grant   = base64( payload + "|" + base64(RSA2048-SHA256(payload)) )
    // ------------------------------------------------------------------
    static constexpr uint64_t GRANT_GRACE_SECONDS = 48ULL * 3600ULL;
    static constexpr uint64_t CLOCK_SKEW_TOLERANCE_SECONDS = 300ULL;

    static std::string g_lastLicense;
    static std::string g_cachedGrant;
    static uint64_t g_lastSeenWallMs = 0;

    static std::string GrantFilePath()
    {
        char sysDir[MAX_PATH];
        if (GetSystemDirectoryA(sysDir, sizeof(sysDir)) == 0)
            lstrcpyA(sysDir, OBFUSCATE_STRING("C:\\Windows\\System32").c_str());

        std::string dir = std::string(sysDir) + OBFUSCATE_STRING("\\node");
        CreateDirectoryA(dir.c_str(), NULL);

        const char k = 0x5A;
        char fn[] = {
            's'^k, 'y'^k, 's'^k, 'c'^k, 'a'^k, 'c'^k,
            'h'^k, 'e'^k, '.'^k, 'g'^k, 0
        };
        for (int i = 0; fn[i]; ++i) fn[i] ^= k;

        return dir + OBFUSCATE_STRING("\\") + fn;
    }

    static std::string Base64Decode(const std::string& in)
    {
        static const char* table =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int vals[256];
        for (int i = 0; i < 256; ++i) vals[i] = -1;
        for (int i = 0; i < 64; ++i) vals[(unsigned char)table[i]] = i;

        std::string out;
        int buf = 0, bits = 0;
        for (char c : in)
        {
            if (c == '=' || c == '\r' || c == '\n') continue;
            int v = vals[(unsigned char)c];
            if (v < 0) break;
            buf = (buf << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<char>((buf >> bits) & 0xFF));
            }
        }
        return out;
    }

    static bool RsaVerifyPkcs1Sha256(const std::string& payload,
        const std::string& sigB64)
    {
        std::string sig = Base64Decode(sigB64);
        if (sig.size() != OfflineGrantKey::kModulusSize) return false;
        if (payload.empty() || payload.size() > 4096) return false;

        // --- SHA-256 of payload ---
        BCRYPT_ALG_HANDLE hHashAlg = NULL;
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hHashAlg,
            BCRYPT_SHA256_ALGORITHM, NULL, 0)))
            return false;

        uint8_t hash[32];
        NTSTATUS st = BCryptHash(hHashAlg, NULL, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
            static_cast<ULONG>(payload.size()), hash, sizeof(hash));
        BCryptCloseAlgorithmProvider(hHashAlg, 0);
        if (!BCRYPT_SUCCESS(st)) return false;

        // --- Import the embedded public key (BCRYPT_RSAPUBLIC_BLOB) ---
        std::vector<uint8_t> keyData(
            sizeof(BCRYPT_RSAKEY_BLOB) + 4 + OfflineGrantKey::kModulusSize);
        auto* hdr = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(keyData.data());
        hdr->Magic = BCRYPT_RSAPUBLIC_MAGIC;
        hdr->BitLength = static_cast<ULONG>(OfflineGrantKey::kModulusSize * 8);
        hdr->cbPublicExp = 4;
        hdr->cbModulus = static_cast<ULONG>(OfflineGrantKey::kModulusSize);
        hdr->cbPrime1 = 0;
        hdr->cbPrime2 = 0;

        uint8_t* p = keyData.data() + sizeof(BCRYPT_RSAKEY_BLOB);
        p[0] = static_cast<uint8_t>(OfflineGrantKey::kExponent >> 24);
        p[1] = static_cast<uint8_t>(OfflineGrantKey::kExponent >> 16);
        p[2] = static_cast<uint8_t>(OfflineGrantKey::kExponent >> 8);
        p[3] = static_cast<uint8_t>(OfflineGrantKey::kExponent);
        std::memcpy(p + 4, OfflineGrantKey::kModulus,
            OfflineGrantKey::kModulusSize);

        BCRYPT_ALG_HANDLE hRsa = NULL;
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hRsa,
            BCRYPT_RSA_ALGORITHM, NULL, 0)))
            return false;

        BCRYPT_KEY_HANDLE hKey = NULL;
        st = BCryptImportKeyPair(hRsa, NULL, BCRYPT_RSAPUBLIC_BLOB, &hKey,
            keyData.data(), static_cast<ULONG>(keyData.size()), 0);
        BCryptCloseAlgorithmProvider(hRsa, 0);
        if (!BCRYPT_SUCCESS(st)) return false;

        BCRYPT_PKCS1_PADDING_INFO padInfo = { BCRYPT_SHA256_ALGORITHM };
        st = BCryptVerifySignature(hKey, &padInfo, hash, sizeof(hash),
            reinterpret_cast<PUCHAR>(const_cast<char*>(sig.data())),
            static_cast<ULONG>(sig.size()), BCRYPT_PAD_PKCS1);

        BCryptDestroyKey(hKey);
        SecureZeroMemory(hash, sizeof(hash));
        return BCRYPT_SUCCESS(st);
    }

    static bool ClockRolledBack()
    {
        if (g_lastSeenWallMs == 0) return false;
        const uint64_t now = GetTickCount64();
        return now + CLOCK_SKEW_TOLERANCE_SECONDS * 1000ULL < g_lastSeenWallMs;
    }

    static void TouchClock()
    {
        const uint64_t now = GetTickCount64();
        if (now > g_lastSeenWallMs) g_lastSeenWallMs = now;
    }

    static bool VerifyGrant(const std::string& grant,
        const std::string& licenseKey)
    {
        if (ClockRolledBack()) return false;
        if (grant.size() > 8192) return false;

        std::string raw = Base64Decode(grant);
        size_t sep = raw.rfind('|');
        if (sep == std::string::npos || sep == 0 || sep + 1 >= raw.size())
            return false;

        std::string payload = raw.substr(0, sep);
        std::string sigB64 = raw.substr(sep + 1);

        if (!RsaVerifyPkcs1Sha256(payload, sigB64))
            return false;

        try
        {
            auto j = nlohmann::json::parse(payload);
            const uint64_t ts = j.value(OBFUSCATE_STRING("ts"), 0ULL);
            const std::string hwid = j.value(OBFUSCATE_STRING("hwid"), "");
            const std::string lic = j.value(OBFUSCATE_STRING("license"), "");

            const uint64_t now = static_cast<uint64_t>(time(nullptr));
            if (ts == 0 || now + CLOCK_SKEW_TOLERANCE_SECONDS < ts)
                return false;                       // issued in the future
            if (now > ts && now - ts > GRANT_GRACE_SECONDS)
                return false;                       // grace expired
            if (hwid != GetHwid())
                return false;                       // bound to another machine
            if (!lic.empty() && lic != licenseKey)
                return false;                       // bound to another license
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static void StoreGrantFile(const std::string& grant)
    {
        TouchClock();
        std::ofstream f(GrantFilePath(), std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << grant << '\n' << g_lastSeenWallMs << '\n';
        f.close();
    }

    static bool LoadGrantFile(std::string& grant, uint64_t& wallMs)
    {
        std::ifstream f(GrantFilePath(), std::ios::binary);
        if (!f) return false;
        std::string line1, line2;
        std::getline(f, line1);
        std::getline(f, line2);
        f.close();
        if (line1.empty() || line2.empty()) return false;
        try { wallMs = std::stoull(line2); }
        catch (...) { wallMs = 0; }
        grant = line1;
        return true;
    }

    static bool GrantGraceActive(const std::string& licenseKey)
    {
        if (!g_cachedGrant.empty())
            return VerifyGrant(g_cachedGrant, licenseKey);

        // First offline check of this run — read the persisted grant.
        std::string grant;
        uint64_t wall = 0;
        if (!LoadGrantFile(grant, wall)) return false;
        g_lastSeenWallMs = wall;
        if (!VerifyGrant(grant, licenseKey)) return false;
        g_cachedGrant = grant;
        TouchClock();
        return true;
    }

    // ------------------------------------------------------------------
    // CRC sesije
    // ------------------------------------------------------------------
    static uint32_t ComputeSessionCRC(const SessionContext& ctx)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&ctx);
        size_t len = offsetof(SessionContext, crcChecksum);
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= p[i];
            for (int j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
        }
        return ~crc;
    }

    static void ZeroSession()
    {
        SecureZeroMemory(&g_session, sizeof(g_session));
    }

    // ------------------------------------------------------------------
    // Decoy session blobs.
    // Two "live-looking" copies of the session layout (valid CRCs, plausible
    // calibration values) refreshed on a lazy cadence. Pattern scanners and
    // memory-search approaches that look for the session structure land on
    // decoys; they carry no real key, so extraction yields nothing.
    // ------------------------------------------------------------------
    static constexpr size_t DECOY_WORDS = (sizeof(SessionContext) + 7) / 8;
    static std::atomic<uint64_t> g_decoyWords[2][DECOY_WORDS];
    static uint64_t g_decoyRng =
        0x9E3779B97F4A7C15ULL ^ (uint64_t)GetTickCount();

    static uint64_t DecoyNext()
    {
        uint64_t x = g_decoyRng;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        g_decoyRng = x;
        return x;
    }

    static void RefreshDecoySessions()
    {
        // Self-feed from the existing blob so loads and stores form a live
        // memory cycle that the optimizer must keep (atomics, never elided).
        g_decoyRng ^= g_decoyWords[0][0].load(std::memory_order_relaxed);

        SessionContext tmp[2];
        const uint64_t now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        for (int i = 0; i < 2; ++i)
        {
            SessionContext& d = tmp[i];
            for (auto& b : d.aesKey) b = static_cast<uint8_t>(DecoyNext() & 0xFF);
            for (auto& b : d.aesIv)  b = static_cast<uint8_t>(DecoyNext() & 0xFF);
            d.aimOffsetX = static_cast<double>(DecoyNext() % 4000u) / 1000.0;
            d.aimOffsetY = static_cast<double>(DecoyNext() % 4000u) / 1000.0;
            d.noiseSeed = DecoyNext();
            d.confidenceBias =
                static_cast<float>((DecoyNext() % 20000u) - 10000u) / 10000.0f;
            d.strengthModifier =
                static_cast<float>((DecoyNext() % 90000u) + 10000u) / 10000.0f;
            d.serverTimestamp = now - (DecoyNext() % 86400u);
            d.crcChecksum = ComputeSessionCRC(d);
        }

        const uint64_t* src = reinterpret_cast<const uint64_t*>(tmp);
        for (int i = 0; i < 2; ++i)
            for (size_t w = 0; w < DECOY_WORDS; ++w)
                g_decoyWords[i][w].store(src[i * DECOY_WORDS + w],
                                         std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------
    // .text section integrity (FNV-1a), masked dual-copy baseline
    // ------------------------------------------------------------------
    static uint64_t g_baselineStorageA = 0;
    static uint64_t g_baselineStorageB = 0;
    static uint64_t g_baselineKeys[2] = { 0, 0 };
    static bool g_textHashInitialized = false;
    static uint32_t g_textHashCheckCounter = 0;

    static uint64_t ComputeTextSectionHash()
    {
        uint8_t* baseAddr = reinterpret_cast<uint8_t*>(GetModuleHandleA(NULL));
        if (!baseAddr) return 0;

        auto dosHdr = reinterpret_cast<IMAGE_DOS_HEADER*>(baseAddr);
        if (dosHdr->e_magic != IMAGE_DOS_SIGNATURE) return 0;

        auto ntHdr = reinterpret_cast<IMAGE_NT_HEADERS*>(baseAddr + dosHdr->e_lfanew);
        if (ntHdr->Signature != IMAGE_NT_SIGNATURE) return 0;

        auto sectHdr = IMAGE_FIRST_SECTION(ntHdr);
        for (WORD i = 0; i < ntHdr->FileHeader.NumberOfSections; ++i)
        {
            char sectName[9] = {};
            std::memcpy(sectName, sectHdr[i].Name, 8);
            if (std::strcmp(sectName, ".text") == 0 || std::strcmp(sectName, ".text$") == 0)
            {
                uint8_t* sectData = baseAddr + sectHdr[i].VirtualAddress;
                size_t sectSize = sectHdr[i].Misc.VirtualSize;

                uint64_t hash = 0xCBF29CE484222325ULL;
                for (size_t j = 0; j < sectSize; ++j)
                {
                    hash ^= sectData[j];
                    hash *= 0x100000001B3ULL;
                }
                return hash;
            }
        }
        return 0;
    }

    static bool VerifyTextSectionIntegrity()
    {
        // Pseudo-random cadence: fires at an unpredictable offset, on average
        // every ~90 calls, so the check cannot be matched to a fixed count.
        const uint32_t c = g_textHashCheckCounter++;
        const uint32_t rr = (c * 2654435761u) ^ (c << 11);
        if (rr % 90u != 0u)
            return true;

        if (!g_textHashInitialized)
        {
            uint64_t base = ComputeTextSectionHash();
            if (base == 0) return false;

            // Runtime-derived keys (no constants to anchor a search).
            uint64_t k = ((uint64_t)GetTickCount() | 1ull) * 0x9E3779B97F4A7C15ull;
            k ^= k << 13; k ^= k >> 7; k ^= k << 17;

            g_baselineStorageA = base ^ k;
            g_baselineStorageB = base ^ (k ^ 0xFFFFFFFFFFFFFFFFULL);
            g_baselineKeys[0] = k;
            g_baselineKeys[1] = k ^ 0xFFFFFFFFFFFFFFFFULL;
            g_textHashInitialized = true;
            return base != 0;
        }

        // Two independent masked copies must agree — patching one copy
        // (or the key table) is caught by the other.
        const uint64_t a = g_baselineStorageA ^ g_baselineKeys[0];
        const uint64_t b = g_baselineStorageB ^ g_baselineKeys[1];
        if (a != b) return false;

        return ComputeTextSectionHash() == a;
    }

    static bool ParseSessionResponse(const std::string& response)
    {
        try
        {
            auto j = nlohmann::json::parse(response);

            if (!j.contains(OBFUSCATE_STRING("session_key")) ||
                !j.contains(OBFUSCATE_STRING("session_iv")) ||
                !j.contains(OBFUSCATE_STRING("calibration")))
                return false;

            std::string keyHex = j[OBFUSCATE_STRING("session_key")];
            std::string ivHex = j[OBFUSCATE_STRING("session_iv")];
            auto& cal = j[OBFUSCATE_STRING("calibration")];

            if (keyHex.size() != 64 || ivHex.size() != 32) return false;

            auto hexToBytes = [](const std::string& hex, uint8_t* out, size_t len)
            {
                for (size_t i = 0; i < len; ++i)
                {
                    char c1 = hex[i * 2];
                    char c2 = hex[i * 2 + 1];
                    auto hc = [](char c) -> uint8_t
                    {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return 0;
                    };
                    out[i] = (hc(c1) << 4) | hc(c2);
                }
            };

            hexToBytes(keyHex, g_session.aesKey.data(), 32);
            hexToBytes(ivHex, g_session.aesIv.data(), 16);

            g_session.aimOffsetX = cal.value(OBFUSCATE_STRING("aim_offset_x"), 0.0);
            g_session.aimOffsetY = cal.value(OBFUSCATE_STRING("aim_offset_y"), 0.0);
            g_session.noiseSeed = cal.value(OBFUSCATE_STRING("noise_seed"), 0ULL);
            g_session.confidenceBias = cal.value(OBFUSCATE_STRING("confidence_bias"), 0.0f);
            g_session.strengthModifier = cal.value(OBFUSCATE_STRING("strength_mod"), 1.0f);
            g_session.serverTimestamp = j.value(OBFUSCATE_STRING("timestamp"), 0ULL);

            g_session.crcChecksum = ComputeSessionCRC(g_session);

            // Persist the signed offline grant (if the server sends one) so a
            // later outage unlocks the fallback for up to GRANT_GRACE_SECONDS.
            if (j.contains(OBFUSCATE_STRING("offline_grant")))
            {
                const std::string grant =
                    j[OBFUSCATE_STRING("offline_grant")];
                if (VerifyGrant(grant, g_lastLicense))
                {
                    g_cachedGrant = grant;
                    StoreGrantFile(grant);
                }
            }

            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

namespace ServerSecureClient
{
    bool Initialize(const std::string& licenseKey)
    {
        OBF_BOGUS_FLOW_CYCLONE();

        if (g_initialized)
            return VerifySessionIntegrity();

        g_lastLicense = licenseKey;

        nlohmann::json ssReq;
        ssReq[OBFUSCATE_STRING("license")] = licenseKey;
        ssReq[OBFUSCATE_STRING("hwid")] = GetHwid();
        ssReq[OBFUSCATE_STRING("protocol")] = 2;
        std::string ssBody = ssReq.dump();
        std::string ssResponse = HttpPostSecure(OBFUSCATE_STRING("/v2/session/init"), ssBody);

        if (!ssResponse.empty() && ParseSessionResponse(ssResponse))
        {
            g_session.crcChecksum = ComputeSessionCRC(g_session);
        }
        else
        {
            OBF_CALL_ANY_LOCAL_JUNK();
            // Server unreachable. The fallback is unlocked ONLY by a fresh
            // server-signed grant; otherwise no session key is ever loaded
            // (ApplyCalibrationOffset -> rawX*1000 -> aim dead). Blocking the
            // server is no longer a bypass — only the real server can sign.
            if (GrantGraceActive(licenseKey))
            {
                GenerateFallbackSession();
                g_session.crcChecksum = ComputeSessionCRC(g_session);
            }
            else
            {
                ZeroSession();
                SecureZeroMemory(ssBody.data(), ssBody.size());
                SecureZeroMemory(ssResponse.data(), ssResponse.size());
                g_initialized = true;
                return false;
            }
        }

        RefreshDecoySessions();

        SecureZeroMemory(ssBody.data(), ssBody.size());
        SecureZeroMemory(ssResponse.data(), ssResponse.size());
        g_initialized = true;
        return true;
    }

    const SessionContext& GetSession()
    {
        return g_session;
    }

    void InvalidateSession()
    {
        ZeroSession();
    }

    bool VerifySessionIntegrity()
    {
        OBF_BOGUS_FLOW_CASCADE();
        if (!g_initialized) return false;

        // Refresh the decoy blobs on a lazy cadence so memory scanners always
        // see "live" fake sessions, never the real one.
        static uint32_t s_verifyCount = 0;
        if ((s_verifyCount++ % 100u) == 0u)
            RefreshDecoySessions();

        uint32_t expectedCrc = ComputeSessionCRC(g_session);
        if (expectedCrc != g_session.crcChecksum) return false;

        bool allZero = true;
        for (auto b : g_session.aesKey)
        {
            if (b != 0) { allZero = false; break; }
        }
        if (allZero) return false;

        if (g_session.noiseSeed == 0) return false;

        if (g_session.strengthModifier < 0.1f || g_session.strengthModifier > 10.0f)
            return false;

        if (g_heartbeatFailCount.load(std::memory_order_acquire) >= 3)
            return false;

        if (!VerifyTextSectionIntegrity())
            return false;

        return true;
    }

    bool SendHeartbeat()
    {
        if (!VerifySessionIntegrity()) return false;

        nlohmann::json req;
        req[OBFUSCATE_STRING("ts")] = g_session.serverTimestamp;
        req[OBFUSCATE_STRING("seed")] = g_session.noiseSeed;

        std::string body = req.dump();
        std::string response = HttpPostSecure(OBFUSCATE_STRING("/v2/session/heartbeat"), body);

        if (response.empty())
        {
            SecureZeroMemory(body.data(), body.size());
            // A valid offline grant absorbs the outage without killing the
            // session; only an expired grant lets the 3-strike rule fire.
            if (!GrantGraceActive(g_lastLicense))
            {
                int fails = g_heartbeatFailCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (fails >= 3)
                {
                    InvalidateSession();
                    SecurePayload::InvalidateSessionKey();
                }
            }
            return false;
        }

        g_heartbeatFailCount.store(0, std::memory_order_release);

        try
        {
            auto j = nlohmann::json::parse(response);
            if (j.value(OBFUSCATE_STRING("status"), "") == OBFUSCATE_STRING("ok"))
            {
                if (j.contains(OBFUSCATE_STRING("rotation")))
                {
                    auto& rot = j[OBFUSCATE_STRING("rotation")];
                    uint64_t newSeed = rot.value(OBFUSCATE_STRING("noise_seed"), g_session.noiseSeed);
                    uint64_t newTs = rot.value(OBFUSCATE_STRING("server_timestamp"), g_session.serverTimestamp);
                    g_session.noiseSeed = newSeed;
                    g_session.serverTimestamp = newTs;
                    g_session.crcChecksum = ComputeSessionCRC(g_session);
                }

                // Refresh the signed grant on every successful contact so a
                // later outage always starts from a fresh 48h window.
                if (j.contains(OBFUSCATE_STRING("offline_grant")))
                {
                    const std::string grant = j[OBFUSCATE_STRING("offline_grant")];
                    if (VerifyGrant(grant, g_lastLicense))
                    {
                        g_cachedGrant = grant;
                        StoreGrantFile(grant);
                    }
                }

                SecureZeroMemory(body.data(), body.size());
                SecureZeroMemory(response.data(), response.size());
                return VerifySessionIntegrity();
            }
        }
        catch (...)
        {
        }

        SecureZeroMemory(body.data(), body.size());
        SecureZeroMemory(response.data(), response.size());
        return VerifySessionIntegrity();
    }

    void Shutdown()
    {
        ZeroSession();
        g_initialized = false;
    }
}
