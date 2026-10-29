#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>

namespace RuntimeProtection
{
    void Initialize();
    void StartProtectionThreads();
    void StopProtection();

    namespace AntiDump
    {
        void Enable();
        void Disable();
        bool IsProtected();
        bool ValidateIntegrity();
    }

    namespace IATProtection
    {
        void ValidateIAT();
        bool IsIATTampered();
    }

    namespace HookDetection
    {
        void StartMonitor();
        void StopMonitor();
        bool DetectMinHook();
        bool DetectIATHooks();
        bool DetectInlineHooks();
    }

    namespace AntiDebug
    {
        void Enable();
        bool IsDebuggerPresent();
        void CheckRemoteDebugger();
    }

    namespace MemoryProtection
    {
        void EncryptCriticalSections();
        void ValidateCodeIntegrity();
    }

    namespace SingleInstance
    {
        bool CheckAndAcquire();
    }

    namespace AntiVM
    {
        bool IsVirtualMachine();
    }

    namespace AntiAttach
    {
        void HideAllThreads();
    }

    namespace AntiBreakpoint
    {
        bool ScanForBreakpoints();
    }

    namespace HardwareBreakpoint
    {
        bool DetectHardwareBreakpoints();
    }

    namespace PEBCheck
    {
        bool IsBeingDebugged();
    }

    namespace TimingCheck
    {
        bool DetectTimingAnomaly();
    }

    namespace DLLInjection
    {
        bool DetectInjectedModules();
    }

    namespace AntiDumpAdvanced
    {
        void ErasePEHeader();
        void SetDebugPortZero();
    }

    namespace AntiDumpProtection
    {
        void Enable();
        void ScanAndKill();
    }

    namespace ProcessObfuscation
    {
        void RenameToDecoy();
    }

    namespace ThreadAutoHide
    {
        void Enable();
        void Disable();
    }

    namespace FileIntegrity
    {
        bool CheckExeIntegrity();
        void StoreOriginalHash();
    }
}
