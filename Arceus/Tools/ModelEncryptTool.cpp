#include "../Utilities/ModelEncrypt.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::wcout << L"Usage: ModelEncrypt.exe <input.onnx> [output.mdl]" << std::endl;
        std::wcout << L"  If output is not specified, creates <input>.mdl next to input" << std::endl;
        return 1;
    }

    std::wstring inputPath;
    for (int i = 0; argv[i]; i++)
    {
        wchar_t wbuf[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wbuf, MAX_PATH);
        if (i == 1) inputPath = wbuf;
    }

    std::wstring outputPath;
    if (argc >= 3)
    {
        wchar_t wbuf[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wbuf, MAX_PATH);
        outputPath = wbuf;
    }
    else
    {
        outputPath = inputPath;
        size_t dotPos = outputPath.rfind(L'.');
        if (dotPos != std::wstring::npos)
            outputPath = outputPath.substr(0, dotPos);
        outputPath += L".mdl";
    }

    std::wcout << L"Encrypting: " << inputPath << std::endl;
    std::wcout << L"Output: " << outputPath << std::endl;

    if (ModelEncrypt::EncryptFile(inputPath, outputPath))
    {
        std::wcout << L"Encryption successful!" << std::endl;
        std::wcout << L"Hardware-bound key: Only works on this machine" << std::endl;
        return 0;
    }
    else
    {
        std::wcout << L"Encryption failed!" << std::endl;
        return 1;
    }
}
