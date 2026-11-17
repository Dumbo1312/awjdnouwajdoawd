@echo off
setlocal

cd /d "%~dp0"
set "SRC=%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
    echo Run this from an x64 Visual Studio developer command prompt.
    exit /b 1
)

set "OUT=Work"
mkdir "%OUT%" 2>nul
del /q "%OUT%\*.obj" "%OUT%\*.efi" 2>nul

rem --- Compiler flags (identical to SmmMem) ---
rem   /GS-       no stack cookies (not available in pre-OS context)
rem   /GR-       no RTTI
rem   /EHs-c-    no exceptions
rem   /Zl        no default library
rem   /W4        all warnings
rem   /O1        optimize for size
set "EFI_CL=/nologo /c /O1 /Oi /GS- /GR- /EHs-c- /Zl /W4"
set "EFI_LD=/nologo /dll /nodefaultlib /machine:x64 /fixed:no /dynamicbase:no /nxcompat:no"

call cl %EFI_CL% /Fo:%OUT%\HeciPspProbe.obj "%SRC%HeciPspProbe.c" || exit /b 1

link %EFI_LD% /subsystem:EFI_BOOT_SERVICE_DRIVER /entry:ProbeEntry ^
     /out:%OUT%\HeciPspProbe.efi %OUT%\HeciPspProbe.obj || exit /b 1

echo.
echo Built: %OUT%\HeciPspProbe.efi
echo.
echo Flash instructions:
echo   1. Open your BIOS ROM in UEFITool
echo   2. Find the DXE volume (usually 2nd or 3rd volume)
echo   3. Insert as a new EFI file (DXE driver type, GUID auto-generated)
echo   4. Save and flash
echo.
echo Result readback (run after boot, in Windows admin shell):
echo   powershell -c "[System.Runtime.InteropServices.Marshal]::PtrToStringUni([System.Runtime.InteropServices.Marshal]::SecureStringToBSTR((New-Object System.Security.SecureString)))"
echo   -- or use the DbgRead.exe from SmmMem with the variable names HECIResult / PSPResult
