#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>

namespace ResourcePacker
{
    static constexpr uint32_t PACK_MAGIC = 0x45585430;
    static constexpr uint8_t XOR_KEY = 0xA7;

    struct FileEntry
    {
        std::string name;
        uint64_t dataOffset;
        uint64_t dataSize;
        uint64_t originalSize;
    };

    inline std::string GetExePath()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        char utf8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
        return std::string(utf8);
    }

    inline std::string GetExeDir()
    {
        std::string path = GetExePath();
        size_t lastSlash = path.find_last_of("\\/");
        return (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : path;
    }

    inline std::string GetTempExtractDir()
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string dir = std::string(tempPath) + "system_cache";
        CreateDirectoryA(dir.c_str(), NULL);
        return dir;
    }

    inline bool ReadTOC(const std::string& exePath, std::vector<FileEntry>& entries)
    {
        std::ifstream f(exePath, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return false;

        uint64_t fileSize = (uint64_t)f.tellg();
        if (fileSize < 8) return false;

        f.seekg(fileSize - 4);
        uint32_t magic = 0;
        f.read((char*)&magic, 4);
        if (magic != PACK_MAGIC) return false;

        f.seekg(fileSize - 8);
        uint32_t tocCount = 0;
        f.read((char*)&tocCount, 4);

        uint64_t tocSize = 0;
        for (uint32_t i = 0; i < tocCount; i++)
        {
            FileEntry entry;
            uint64_t pos = fileSize - 8 - tocSize;

            pos -= sizeof(uint64_t);
            f.seekg(pos);
            f.read((char*)&entry.originalSize, 8);

            pos -= sizeof(uint64_t);
            f.seekg(pos);
            f.read((char*)&entry.dataSize, 8);

            pos -= sizeof(uint64_t);
            f.seekg(pos);
            f.read((char*)&entry.dataOffset, 8);

            uint16_t nameLen = 0;
            pos -= 2;
            f.seekg(pos);
            f.read((char*)&nameLen, 2);

            pos -= nameLen;
            f.seekg(pos);
            std::string name(nameLen, '\0');
            f.read(&name[0], nameLen);
            entry.name = name;

            tocSize += 2 + nameLen + 8 + 8 + 8;
            entries.push_back(entry);
        }

        std::reverse(entries.begin(), entries.end());
        return true;
    }

    inline bool HasPackedResources()
    {
        std::vector<FileEntry> entries;
        return ReadTOC(GetExePath(), entries) && !entries.empty();
    }

    inline bool ExtractAll(const std::string& exePath, const std::string& outDir)
    {
        std::vector<FileEntry> entries;
        if (!ReadTOC(exePath, entries)) return false;

        std::ifstream f(exePath, std::ios::binary);
        if (!f.is_open()) return false;

        CreateDirectoryA(outDir.c_str(), NULL);

        for (auto& entry : entries)
        {
            f.seekg(entry.dataOffset);
            std::vector<uint8_t> encrypted(entry.dataSize);
            f.read((char*)encrypted.data(), entry.dataSize);

            for (uint64_t i = 0; i < entry.dataSize; i++)
                encrypted[i] ^= XOR_KEY;

            std::string outPath = outDir + "\\" + entry.name;

            std::ofstream out(outPath, std::ios::binary);
            if (!out.is_open()) return false;
            out.write((char*)encrypted.data(), entry.originalSize);
            out.close();
        }

        return true;
    }

    inline bool ExtractAndSetupDllDir()
    {
        std::string exeDir = GetExeDir();
        std::string exePath = GetExePath();

        std::vector<FileEntry> entries;
        if (!ReadTOC(exePath, entries)) return true;

        if (!ExtractAll(exePath, exeDir)) return false;

        SetDllDirectoryA(exeDir.c_str());

        return true;
    }

    inline void CleanupExtractedFiles()
    {
        std::string dir = GetTempExtractDir();
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
                    RemoveDirectoryA((dir + "\\" + findData.cFileName).c_str());
                }
                else
                {
                    DeleteFileA((dir + "\\" + findData.cFileName).c_str());
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
        RemoveDirectoryA(dir.c_str());

        std::string exeDir = GetExeDir();
        std::vector<FileEntry> entries;
        if (ReadTOC(GetExePath(), entries))
        {
            for (auto& entry : entries)
            {
                std::string path = exeDir + "\\" + entry.name;
                DeleteFileA(path.c_str());
            }
        }
    }
}
