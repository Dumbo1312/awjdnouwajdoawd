#include "ResourceExtract.h"
#include "../resource.h"
#include "../Utilities/ResourceProtection.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

// TLS callback: extracts essential DLLs before CRT init
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:_xl_b")

struct EncHdr { uint32_t magic; uint32_t origSize; };

static void strcpy_w(wchar_t* d, const wchar_t* s)
{
    while ((*d++ = *s++));
}

static void strcat_w(wchar_t* d, const wchar_t* s)
{
    while (*d) d++;
    while ((*d++ = *s++));
}

static bool RawDecrypt(const uint8_t* key, const uint8_t* input, size_t is,
    uint8_t* output, DWORD* outLen)
{
    HCRYPTPROV hp = 0;
    if (!CryptAcquireContextW(&hp, 0, 0, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;

    struct { BLOBHEADER h; DWORD ks; uint8_t kd[32]; } kb;
    kb.h.bType = PLAINTEXTKEYBLOB;
    kb.h.bVersion = CUR_BLOB_VERSION;
    kb.h.reserved = 0;
    kb.h.aiKeyAlg = CALG_AES_256;
    kb.ks = 32;
    memcpy(kb.kd, key, 32);

    HCRYPTKEY hk = 0;
    if (!CryptImportKey(hp, (BYTE*)&kb, sizeof(kb), 0, 0, &hk))
    { CryptReleaseContext(hp, 0); return false; }

    memcpy(output, input, 16);
    memcpy(output + 16, input + 16, (DWORD)(is - 16));
    *outLen = (DWORD)(is - 16) + 16;

    BOOL ok = CryptDecrypt(hk, 0, TRUE, 0, output, outLen);
    CryptDestroyKey(hk);
    CryptReleaseContext(hp, 0);
    return ok != FALSE;
}

static bool ExtractRaw(int id, const wchar_t* dir, const wchar_t* name)
{
    HRSRC hr = FindResourceW(0, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hm = LoadResource(0, hr);
    if (!hm) return false;
    void* d = LockResource(hm);
    DWORD sz = SizeofResource(0, hr);
    if (!d || sz == 0) return false;

    EncHdr h;
    memcpy(&h, d, sizeof(h));
    if (h.magic != 0x45585230) return false;

    const uint8_t* p = (const uint8_t*)d + 8;
    size_t ps = sz - 8;

    HANDLE heap = GetProcessHeap();
    if (!heap) return false;

    DWORD bufLen = (DWORD)(ps) + 16;
    uint8_t* buf = (uint8_t*)HeapAlloc(heap, 0, bufLen);
    if (!buf) return false;

    bool ok = false;
    if (RawDecrypt(ResourceProtection::MASTER_KEY, p, ps, buf, &bufLen))
    {
        wchar_t path[MAX_PATH];
        strcpy_w(path, dir);
        strcat_w(path, L"\\");
        strcat_w(path, name);

        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
        if (f != INVALID_HANDLE_VALUE)
        {
            DWORD wn;
            ok = WriteFile(f, buf + 16, bufLen - 16, &wn, 0) && wn == bufLen - 16;
            CloseHandle(f);
        }
    }

    HeapFree(heap, 0, buf);
    return ok;
}

static void NTAPI TlsCB(void* h, DWORD reason, void*)
{
    if (reason != DLL_PROCESS_ATTACH) return;

    wchar_t dir[MAX_PATH];
    DWORD len = GetSystemDirectoryW(dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    strcat_w(dir, L"\\ls");
    CreateDirectoryW(dir, 0);

    ExtractRaw(IDR_DIRECTML_DLL, dir, L"DirectML.dll");
    ExtractRaw(IDR_ONNXRUNTIME_DLL, dir, L"onnxruntime.dll");
    ExtractRaw(IDR_ONNXRUNTIME_PROVIDERS_DLL, dir, L"onnxruntime_providers_shared.dll");
    ExtractRaw(IDR_GRAPHICS_HOOK_DLL, dir, L"graphics-hook64.dll");
    ExtractRaw(IDR_INJECT_HELPER_EXE, dir, L"inject-helper64.exe");
    ExtractRaw(IDR_FORTNITE_MDL, dir, L"fortnite.mdl");
    ExtractRaw(IDR_MAKCU_DLL, dir, L"makcu-cpp.dll");
    SetDllDirectoryW(dir);
    LoadLibraryW(L"DirectML.dll");
    LoadLibraryW(L"onnxruntime.dll");
}

#pragma section(".CRT$XLB", read)
extern "C" __declspec(allocate(".CRT$XLB")) const PIMAGE_TLS_CALLBACK _xl_b = TlsCB;

// --- CRT-era functions for post-init work ---
#include <string>
#include <vector>

static std::wstring s_runtimeDir;
static bool s_extracted = false;

void ResourceExtract::ForceExtractNow()
{
    if (s_extracted) return;

    wchar_t sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return;
    s_runtimeDir = std::wstring(sys) + L"\\ls";

    wchar_t sys2[MAX_PATH];
    GetSystemDirectoryW(sys2, MAX_PATH);
    std::wstring nd = std::wstring(sys2) + L"\\node";
    CreateDirectoryW(nd.c_str(), 0);
    std::wstring nj = nd + L"\\node2.json";
    if (GetFileAttributesW(nj.c_str()) == INVALID_FILE_ATTRIBUTES)
        ExtractRaw(IDR_NODE_JS, sys2, L"node\\node2.json");

    s_extracted = true;
}

bool ResourceExtract::ExtractAll()
{
    ForceExtractNow();
    return s_extracted;
}

const std::wstring& ResourceExtract::GetRuntimeDir()
{
    return s_runtimeDir;
}

bool ResourceExtract::ExtractEmbeddedModel()
{
    if (s_runtimeDir.empty()) return false;
    return ExtractRaw(IDR_FORTNITE_MDL, s_runtimeDir.c_str(), L"fortnite.mdl");
}

void ResourceExtract::CleanupFiles()
{
    if (s_runtimeDir.empty()) return;
    DeleteFileW((s_runtimeDir + L"\\fortnite.mdl").c_str());
}

void ResourceExtract::CleanupDir()
{
    if (s_runtimeDir.empty()) return;
    std::wstring args = L"/c ping -n 3 127.0.0.1 > nul & rmdir /s /q \"" + s_runtimeDir + L"\"";
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    ShellExecuteExW(&sei);
}
