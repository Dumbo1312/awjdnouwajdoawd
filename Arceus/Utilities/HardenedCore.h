#pragma once
#include <cstdint>

//
// HardenedCore — self-healing enforcement core.
//
// The tamper response is DATA CORRUPTION, never a patchable exit(0):
//   ServerSecureClient::InvalidateSession()   -> session CRC breaks -> garbage aim
//   SecurePayload::InvalidateSessionKey()     -> AES key zeroed -> model can't decrypt
//   g_poisoned flag                           -> FrameCanary() flips -> aim jitter corrupts
//
// Layers (no single point of failure):
//   1. Watchdog A  — syscall anti-debug + session integrity + monitors B + monitor tick
//   2. Watchdog B  — CrossValidateAll + anti-VM + timing + monitors A + monitor tick
//   3. Supervisor  — respawns dead watchdogs; poisons if everything is dead
//   4. FrameCanary — runs INSIDE the aim path every frame; full re-verify every 256 shots.
//      A cracker cannot kill the watchdogs without the canary still poisoning the build.
//

namespace HardenedCore
{
    void Start();
    void Stop();

    // Called from every tamper site / watchdog. Corruption-based, irreversible.
    void OnTamperDetected();

    // Folded into aim math each action. Cheap; deep verify every 256th call.
    // Returns a different digest when poisoned so NOP-ing it breaks aim subtly.
    uint64_t FrameCanary();

    // Jittered sleep — patching a fixed Sleep() argument no longer works.
    void HardenedSleep(unsigned baseMs);

    // Syscall-based debugger detection (bypasses hooked ntdll exports).
    bool DebuggerDetectedSyscall();

    // Process + window scan for known reversing tools (encrypted names).
    bool ReversingToolDetected();

    // Lightweight self-check used by FrameCanary.
    bool QuickSelfCheck();

    // Heartbeat tick from RuntimeProtection monitor loop.
    void HeartbeatMonitorTick();
}