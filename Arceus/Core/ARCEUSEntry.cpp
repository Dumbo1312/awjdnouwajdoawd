#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"
#include "App.h"
#include "CrashLog.h"
#include "ResourceExtract.h"
#include "ServerSecureClient.h"
#include "SecurePayload.h"
#include "../Utilities/FakeServer.h"
#include "../Utilities/LicenseGuard.h"
#include "../Utilities/RuntimeProtection.h"
#include "../Utilities/HardenedCore.h"
#include "../resource.h"
#include <windows.h>
#include <exception>

namespace
{
#ifdef ARCEUS_NO_LOG
    // Release: any uncaught exception ends the process silently. No abort(),
    // so no 0xC0000409 fastfail, no WER dump, no crash dialog, no log file.
    static void TerminateLog()
    {
        ExitProcess(0);
    }
#else
    static void LogTerminateHeader()
    {
        char tidBuf[32];
        sprintf_s(tidBuf, "%lu", GetCurrentThreadId());
        CrashLog::Log(("TERMINATE (uncaught exception) tid=" + std::string(tidBuf)).c_str());
    }

    static void TerminateLog()
    {
        // Uncaught exception in ANY thread ends up here (std::terminate).
        // The abort() that follows produces 0xC0000409 — identical to the
        // crash reports we have been chasing.
        LogTerminateHeader();
        CrashLog::LogStack("TERMINATE stack");

        // MSVC-specific: recover the live exception object for its what().
        // Guarded with SEH: the object may not be a std::exception.
        __try
        {
            std::exception* ex = reinterpret_cast<std::exception*>(__current_exception());
            if (ex)
            {
                CrashLog::Log("  what():");
                CrashLog::Log(ex->what());
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            CrashLog::Log("  what() unavailable");
        }
    }
#endif

    struct TerminateInstaller
    {
        TerminateInstaller()
        {
            std::set_terminate(TerminateLog);
        }
    };
    static TerminateInstaller s_terminateInstaller;

    // Log helpers live OUTSIDE the OBF_METHOD macro (preprocessor directives
    // are illegal inside macro arguments). All no-ops when ARCEUS_NO_LOG.
#ifndef ARCEUS_NO_LOG
    static void LogRunStart()
    {
        CrashLog::Log(("THREAD MAIN/RunArceus tid=" + std::to_string(GetCurrentThreadId())).c_str());
    }
    static void LogCatchExc(const std::exception& e)
    {
        CrashLog::Log("RunArceus CATCH std::exception (graceful shutdown)");
        CrashLog::Log(e.what());
        CrashLog::LogStack("RunArceus catch stack");
    }
    static void LogCatchUnknown()
    {
        CrashLog::Log("RunArceus CATCH unknown (graceful shutdown)");
        CrashLog::LogStack("RunArceus catch stack");
    }
#else
    static void LogRunStart() {}
    static void LogCatchExc(const std::exception&) {}
    static void LogCatchUnknown() {}
#endif
}

OBF_METHOD(void, RunArceus, (), {
    LogRunStart();
    CrashLog::Install();

    ResourceExtract::ForceExtractNow();

    FakeServer::Initialize();
    FakeServer::StartFakeServer();

    LicenseGuard::StartWatchdog();

    try { RuntimeProtection::Initialize(); } catch (...) {}
    try { RuntimeProtection::StartProtectionThreads(); } catch (...) {}
    try { HardenedCore::Start(); } catch (...) {}

    try
    {
        App Seny;
        Seny.Run();
    }
    catch (const std::exception& e)
    {
        // Top-level safety net: an exception in the run loop must never kill
        // the process (0xC0000409 fastfail = volume for crackers + no UX).
        LogCatchExc(e);
    }
    catch (...)
    {
        LogCatchUnknown();
    }

    SecurePayload::InvalidateSessionKey();
    ServerSecureClient::Shutdown();
    HardenedCore::Stop();
    LicenseGuard::StopWatchdog();
    FakeServer::StopFakeServer();
})