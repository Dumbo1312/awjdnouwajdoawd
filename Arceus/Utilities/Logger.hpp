#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <windows.h>

#include "Strings.hpp"

namespace Logger
{
    namespace
    {
        std::wofstream logStream;
        std::mutex logMutex;
        bool initialized = false;

        void Init()
        {
            if (initialized)
                return;

            wchar_t exePath[MAX_PATH] = { 0 };
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::filesystem::path logPath = std::filesystem::path(exePath).remove_filename() / SUtility::LogFile.get();

            if (!std::filesystem::exists(logPath))
                return;

            auto fileSize = std::filesystem::file_size(logPath);
            if (fileSize > 0)
            {
                {
                    std::wofstream truncStream(logPath, std::ios::out | std::ios::trunc);
                }
            }

            logStream.open(logPath, std::ios::app);
            if (!logStream.is_open())
                return;

            initialized = true;
        }

        std::wstring GetTimestamp()
        {
            using namespace std::chrono;

            auto now = system_clock::now();
            auto now_time_t = system_clock::to_time_t(now);
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

            std::tm local_tm;
            localtime_s(&local_tm, &now_time_t);

            std::wstringstream wss;
            wss << std::put_time(&local_tm, L"%Y-%m-%d %H:%M:%S");
            wss << L"." << std::setfill(L'0') << std::setw(3) << ms.count();

            return wss.str();
        }

        void WriteLog(const std::wstring& level, const std::wstring& category, const std::wstring& message)
        {
            Init();

            if (!initialized)
                return;


            std::lock_guard<std::mutex> lock(logMutex);
            logStream << L"[" << GetTimestamp() << L"] [" << level << L"] [" << category << L"] " << message << std::endl;
            logStream.flush();
        }
    }

    inline void Progress(const std::wstring& category, const std::wstring& message)
    {
        WriteLog(SUtility::LogProgress, category, message);
    }

    inline void Warning(const std::wstring& category, const std::wstring& message)
    {
        WriteLog(SUtility::LogWarning, category, message);
    }
}