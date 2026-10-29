@echo off
rem grant_smoke_build.bat — build Tools\grant_smoke_test.cpp with MSVC + bcrypt.lib
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
cl /nologo /W4 /EHsc "%~dp0grant_smoke_test.cpp" /link bcrypt.lib /out:"%~dp0grant_smoke_test.exe"