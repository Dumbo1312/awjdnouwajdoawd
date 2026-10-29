#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <delayimp.h>
#pragma comment(lib, "delayimp.lib")

static HMODULE LoadMakcuFromSystemLs()
{
	wchar_t path[MAX_PATH];
	UINT len = GetSystemDirectoryW(path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) return NULL;
	wcscat_s(path, L"\\ls\\makcu-cpp.dll");
	return LoadLibraryW(path);
}

static bool IsMakcuDll(const PDelayLoadInfo pdli)
{
	return pdli && pdli->szDll &&
		(pdli->szDll[0] == 'm' || pdli->szDll[0] == 'M') &&
		_stricmp(pdli->szDll, "makcu-cpp.dll") == 0;
}

extern "C" FARPROC WINAPI DelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (IsMakcuDll(pdli) && (dliNotify == dliNotePreLoadLibrary || dliNotify == dliFailLoadLib))
	{
		HMODULE h = LoadMakcuFromSystemLs();
		if (h) return (FARPROC)h;
	}
	return NULL;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DelayLoadHook;
extern "C" const PfnDliHook __pfnDliFailureHook2 = DelayLoadHook;
