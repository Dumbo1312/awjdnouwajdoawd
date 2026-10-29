#include "Core/App.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern void RunArceus();

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    __try
    {
        RunArceus();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DWORD code = GetExceptionCode();
        char msg[128];
        sprintf_s(msg, "Exception: 0x%08X\n\nCheck:\n1. Smart App Control off\n2. Core Isolation off\n3. Windows Defender excluded exe", code);
        MessageBoxA(NULL, msg, "Fatal Error", MB_ICONERROR | MB_SETFOREGROUND | MB_OK);
    }
}
