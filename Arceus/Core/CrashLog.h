#pragma once
//
// CrashLog.h — minimal, allocation-free crash reporter.
// Writes the exception code, faulting address, module base and a raw stack
// backtrace to "<exe dir>\crash.log" so support issues (unhandled C++
// exceptions, AVs) can be diagnosed on customer machines.
//
// Pure Win32 only — safe to call from exception filters and terminate
// handlers even when the CRT heap is already in a bad state.
//

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <atomic>

#ifdef ARCEUS_NO_LOG
// Release builds: zero on-disk traces. All logging compiles out. The
// unhandled-exception filter is still installed so crashes are swallowed
// silently (no WER dump, no crash dialog) instead of being reported.
namespace CrashLog
{
    inline void Log(const char*) {}
    inline void LogStack(const char*) {}
    inline void LogException(const char*, DWORD, uint64_t, uint64_t*, int) {}

    inline LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS*)
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    inline void Install()
    {
        SetUnhandledExceptionFilter(UnhandledFilter);
    }
}
#else
namespace CrashLog
{
    namespace detail
    {
        inline void WriteLine(const char* line)
        {
            static std::atomic_flag s_lock = ATOMIC_FLAG_INIT;
            while (s_lock.test_and_set(std::memory_order_acquire)) {}

            char path[MAX_PATH];
            DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if (len == 0 || len >= MAX_PATH) { s_lock.clear(std::memory_order_release); return; }
            for (DWORD i = len; i > 0; --i)
            {
                if (path[i - 1] == '\\')
                {
                    path[i] = '\0';
                    break;
                }
            }
            strcat_s(path, "crash.log");

            HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) { s_lock.clear(std::memory_order_release); return; }

            char buf[1024];
            SYSTEMTIME st;
            GetLocalTime(&st);
            const int n = std::snprintf(buf, sizeof(buf),
                "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                st.wMilliseconds, line);
            if (n > 0)
            {
                DWORD written = 0;
                WriteFile(h, buf, static_cast<DWORD>(n), &written, nullptr);
            }
            CloseHandle(h);
            s_lock.clear(std::memory_order_release);
        }
    }

    // Logs a fixed message (from a terminate handler / caught exception).
    inline void Log(const char* msg)
    {
        detail::WriteLine(msg);
    }

    inline void LogStack(const char* tag)
    {
        char line[1024];
        const uint64_t base = reinterpret_cast<uint64_t>(GetModuleHandleA(nullptr));

        void* raw[32];
        const int frames = CaptureStackBackTrace(1, 32, raw, nullptr);
        std::snprintf(line, sizeof(line), "%s stack=%d frames", tag, frames);
        detail::WriteLine(line);

        char frame[160];
        for (int i = 0; i < frames; ++i)
        {
            const uint64_t a = reinterpret_cast<uint64_t>(raw[i]);
            std::snprintf(frame, sizeof(frame),
                "  #%02d 0x%llX (rel 0x%llX)",
                i, a, a >= base ? a - base : a);
            detail::WriteLine(frame);
        }
    }

    // Logs the raw exception info with a stack backtrace.
    inline void LogException(const char* tag, DWORD code, uint64_t faultAddr,
                             uint64_t stack[32], int frameCount)
    {
        char line[1024];
        const uint64_t base = reinterpret_cast<uint64_t>(GetModuleHandleA(nullptr));

        int n = std::snprintf(line, sizeof(line),
            "%s code=0x%08X addr=0x%llX moduleBase=0x%llX rel=0x%llX frames=%d",
            tag, code, faultAddr, base,
            faultAddr >= base ? faultAddr - base : faultAddr, frameCount);
        if (n > 0) detail::WriteLine(line);

        char frame[128];
        for (int i = 0; i < frameCount && i < 32; ++i)
        {
            uint64_t a = stack[i];
            std::snprintf(frame, sizeof(frame),
                "  #%02d 0x%llX (rel 0x%llX)",
                i, a, a >= base ? a - base : a);
            detail::WriteLine(frame);
        }
    }

    inline LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep)
    {
        uint64_t stack[32];
        int frames = CaptureStackBackTrace(0, 32, reinterpret_cast<void**>(stack), nullptr);
        LogException("UNHANDLED", ep->ExceptionRecord->ExceptionCode,
                     ep->ExceptionRecord->ExceptionAddress ? reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress) : 0,
                     stack, frames);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    inline void Install()
    {
        SetUnhandledExceptionFilter(UnhandledFilter);
    }
}
#endif

// Captures the current thread's raw exception pointer (MSVC).
extern "C" void* __cdecl __current_exception();
