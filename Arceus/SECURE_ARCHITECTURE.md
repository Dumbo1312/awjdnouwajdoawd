# Secure Architecture Overview

## 1. Server-Side Execution / Remote Computation

**Problem solved:** Previously, all logic ran client-side. Patching a single `bool` (`g_licenseValid`) was enough to bypass the entire auth system.

**New approach:** The server computes per-session calibration parameters that the client *mathematically requires* to aim correctly.

### How it works

**File:** `Core/ServerSecureClient.cpp`
**File:** `Core/IOManagement.cpp` (lines in `ActHipFire` / `ActAds`)

1. Client sends `license + hwid` to `POST /v2/session/init`
2. Server validates license (KeyAuth) and HWID, then returns:
   ```json
   {
     "calibration": {
       "aim_offset_x": 1.3742,
       "aim_offset_y": -0.8913,
       "noise_seed": 182736451029384756,
       "confidence_bias": 0.023,
       "strength_mod": 0.97
     }
   }
   ```
3. In `IOManagement.cpp`, every aiming calculation applies:
   - `SecurePayload::ApplyCalibrationOffset()` — without a valid key, this returns `rawX * 1000` (garbage aim)
   - `SecurePayload::ComputeAimNoise(seed, frame)` — without a valid seed, returns 0 → no noise, aim feels "robotic" and detectable, but more importantly if seed is 0 the session is considered invalid

**Result:** If an attacker blocks the server or returns a fake response, the calibration offsets are garbage → aimbot aims at wrong coordinates. There is no `bool` to patch.

---

## 2. Secure Dynamic Payload / Memory Decryption

**Problem solved:** Model files (`.mdl`) and configuration strings were stored on disk, easy to extract or replaced.

**New approach:** After successful HWID + key validation, the server sends an AES-256 session key. All critical resources are encrypted and can only be decrypted in memory with this key.

### How it works

**File:** `Core/ServerSecureClient.cpp` — receives `session_key` (64 hex chars) and `session_iv` (32 hex chars)
**File:** `Core/SecurePayload.cpp` — manages AES-256 decryption via `BCrypt`

1. Server returns `session_key` and `session_iv` only after license+ HWID validation
2. `SecurePayload::SetSessionKey(key32, iv16)` loads the key into a static struct
3. `SecurePayload::DecryptBuffer()` decrypts .mdl model files in memory — the ONNX runtime reads the decrypted buffer directly; the encrypted file on disk is never fully decrypted
4. If the session key is garbage (attacker patched the response), `DecryptBuffer` returns empty → ONNX produces zero detections → aimbot does nothing

**Files involved:** `Utilities/ModelEncrypt.cpp` currently uses a static key. Integration path: `ModelEncrypt::DecryptFile()` should call `SecurePayload::DecryptBuffer()` instead of using the hardcoded key.

---

## 3. Removal of Client-Side Bool Checks

**Problem solved:** Old code had `bool g_licenseValid` with `if (!valid) break;` — trivial to NOP-patch.

**New approach:** There is no single `bool` to patch. The session integrity is verified through data-dependent checks across multiple subsystems.

### How it works

**File:** `Utilities/LicenseGuard.cpp` — all `CheckTokenAtPointX()` and `CrossValidateAll()` now call:
- `ServerSecureClient::VerifySessionIntegrity()` — verifies CRC of `SessionContext`, checks AES key is not all-zero, checks noiseSeed != 0
- `SecurePayload::IsKeyValid()` — verifies AES key is loaded and not all-zero

**Enforcement chain (no single point of failure):**

```
LicenseGuard::CheckTokenAtPoint1()
  └─ ServerSecureClient::VerifySessionIntegrity()
       └─ CRC check of SessionContext struct
       └─ AES key != all zeros
       └─ noiseSeed != 0
       └─ strengthModifier in valid range
  └─ SecurePayload::IsKeyValid()
       └─ cryptoState.valid == true
       └─ key != all zeros

LicenseGuard::CrossValidateAll()
  └─ Same checks + ValidationToken HWID match
  └─ g_encryptedToken non-empty
  └─ g_responseHash non-empty
  └─ Timestamp within 24h
```

If an attacker NOPs any check, the downstream subsystems (ONNX model decryption, aiming calibration) will silently fail due to missing/corrupted data.

---

## 4. Network Security (Anti-MITM / Certificate Pinning)

**Problem solved:** Old code ignored all SSL errors (`SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID`), making MITM trivial.

**New approach:** Hardcoded SHA-256 certificate pin + protocol-level challenge.

### How it works

**File:** `Core/ServerSecureClient.cpp` — `VerifyCertificatePin()`

```cpp
constexpr std::array<uint8_t, 32> EXPECTED_CERT_PIN = {
    0x00, 0x11, 0x22, ... // SHA-256 of server's leaf certificate
};

static bool VerifyCertificatePin(HINTERNET hRequest)
{
    // Query WINHTTP_OPTION_SECURITY_CERTIFICATE_STRUCT
    // Extract leaf certificate
    // CryptHashCertificate with CALG_SHA_256
    // Compare with EXPECTED_CERT_PIN
}
```

**Flow:**
1. Open WinHTTP connection with `WINHTTP_FLAG_SECURE`
2. Set minimal SSL ignore flags (only unknown CA + date issues for OS compatibility)
3. **Do NOT ignore CN mismatch** — this prevents simple host header manipulation
4. Extract the server's leaf certificate
5. Compute SHA-256 hash and compare against hardcoded pin
6. If pin doesn't match → fail the request

**Result:** Even if an attacker redirects DNS or installs a fake CA, the certificate pin will reject the connection. The attacker would need to compromise the server's private key to emulate it.

---

## 5. Resource Protection (AES-256 Encrypted Embedding)

**Problem solved:** All embedded resources (DLLs, .mdl model, helper EXEs) were stored as plain RCDATA in the .exe. Anyone with Resource Hacker could extract them.

**New approach:** Every resource is encrypted with AES-256-CBC using a master key at build time. At runtime, resources are decrypted on-the-fly only if a valid license session exists.

### How it works

**File:** `Core/ResourceExtract.cpp`
**File:** `Utilities/ResourceDecrypt.h`
**File:** `Utilities/ResourceProtection.h` (auto-generated)

1. **Build time:** `Tools/ResourceEncryptTool.ps1` encrypts all files in `Embedded/` with AES-256-CBC (PKCS7 padding). Output goes to `Embedded/encrypted/*.enc`. The master key is stored in `Tools/resource_key.bin` (do NOT commit this!) and a C++ header `Utilities/ResourceProtection.h` is generated with the key as a `constexpr` array.

2. **Resource script:** `RIVEN.rc` points to `Embedded/encrypted/*.enc` instead of raw files. The resources are embedded encrypted in the .exe's `.rdata` section — totally unreadable to static analysis.

3. **Runtime extraction:** `ResourceExtract::ExtractAll()` decrypts each resource in memory using `ResourceDecrypt::DecryptResource()` which calls `BCryptDecrypt` with AES-256-CBC.

4. **License gating:** Before extracting license-protected resources (graphics-hook64.dll, inject-helper64.exe, fortnite.mdl), `ResourceExtract` calls `LicenseGuard::CheckTokenAtPoint1()` and `CrossValidateAll()`. If no valid session exists, the files are written as garbage (0xBA) instead.

5. **Model file:** `fortnite.mdl` additionally requires the server's session key (from `ServerSecureClient`) to be usable by ONNX runtime — double protection.

### Added files

| File | Purpose |
|------|---------|
| `Tools/ResourceEncryptTool.ps1` | Build-time resource encryption |
| `Tools/resource_key.bin` | AES-256 master key (generated once, keep secure) |
| `Utilities/ResourceDecrypt.h` | Runtime AES-256-CBC decryption via BCrypt |
| `Utilities/ResourceProtection.h` | Auto-generated header with master key + SHA-256 hashes |

**Result:** Resources are encrypted at rest in the .exe. Extraction is gated by license validation. Even if someone dumps the .exe, all embedded files are ciphertext.

---

## 6. Code Preparation for Obfuscation

**Problem solved:** Old code used some obfuscation macros but inconsistently. Reverse engineer could identify patterns.

**New approach:** Every function uses Obfusk8 control-flow macros, string encryption, and opaque predicates. Dynamic API resolution hides import tables.

### Techniques used throughout new code

| Technique | Macro / Pattern | Location |
|-----------|----------------|----------|
| Control flow obfuscation | `OBF_BOGUS_FLOW_CYCLONE()`, `OBF_BOGUS_FLOW_CASCADE()` etc. | Top of every function |
| String encryption | `OBFUSCATE_STRING("text")`, `OBFUSCATE_WSTRING(L"text")` | All string literals |
| Opaque predicates | `OBF_OPAQUE_PREDICATE_TRUE_1()` | Mixed into conditionals |
| Junk code insertion | `OBF_CALL_ANY_LOCAL_JUNK()` | Between logical blocks |
| Function-level obfuscation | `OBF_METHOD(ret, name, (params), { body })` | Wraps entire functions |
| Dynamic API resolution | `STEALTH_API_OBFSTR("dll.dll", "ApiName")` | Via `Obfusk8Core.hpp` |
| Inline mutation | `VIRTUALIZER_MUTATE_ONLY_START/END` | Critical code sections |
| Syscall invocation | `K8_SYSCALL("NtXxx", ...)` | Via `k8_indsys.hpp` |

### String encryption example (from new files)

```cpp
std::wstring headers = OBFUSCATE_WSTRING(L"Content-Type: application/json\r\n"
    L"X-Session-Protocol: 2\r\n");
```

At compile time, these strings are encrypted with a rolling XOR. At runtime, they're decrypted on the stack and used immediately — never stored in plaintext in the `.rdata` section.

### Dynamic API loading

```cpp
// Instead of: GetProcAddress(GetModuleHandle("winhttp.dll"), "WinHttpOpen")
STEALTH_API_OBFSTR("winhttp.dll", "WinHttpOpen")
```

This resolves API addresses at runtime using hash-based lookup, making static analysis of import tables impossible.

### Compiler compatibility

All new code is compatible with:
- **OLLVM** (LLVM-based obfuscator) — the `OBF_METHOD` wrapper and control-flow macros translate well to LLVM passes
- **VMProtect** — sensitive blocks are wrapped in `VIRTUALIZER_MUTATE_ONLY_START/END`
- **Obfuscator-LLVM** — `OBF_BOGUS_FLOW_*` macros create real control-flow graphs that OLLVM can further flatten

---

## 7. HardenedCore — Self-Healing Enforcement (data-dependent kill)

**Problem solved:** Earlier protection had single watchdog threads sleeping a fixed
`Sleep(30000)` and responding with a patchable `exit(0)`. A pro cracker just:
1. patched the `Sleep` argument to a huge value, or
2. suspended/killed the monitor thread, or
3. NOP-patched the `exit(0)` call sites.

There was also a critical wiring bug: `RuntimeProtection::StartProtectionThreads()`
was **never called**, so the entire hook/integrity monitor never ran.

**New approach:** `Utilities/HardenedCore.h/.cpp` — a corruption-based kill chain with
no single point of failure:

### Layers (independent, redundant)

```
Layer 1  Watchdog A   — syscall anti-debug + session integrity, monitors B + monitor tick
Layer 2  Watchdog B   — CrossValidateAll + anti-VM + timing, monitors A + monitor tick
Layer 3  Supervisor   — respawns dead watchdogs; poisons if everything is dead at once
Layer 4  FrameCanary  — runs INSIDE the aim math every shot; deep verify every 256th
```

### Tamper response = data corruption, never `exit(0)`

`HardenedCore::OnTamperDetected()` poisons three independent data stores:

| Corruption | Effect |
|-----------|--------|
| `ServerSecureClient::InvalidateSession()` | session CRC breaks → `ApplyCalibrationOffset` returns `rawX * 1000` → garbage aim |
| `SecurePayload::InvalidateSessionKey()` | AES key zeroed → model can't decrypt → zero detections |
| `g_poisoned` flag | `FrameCanary()` flips digest → aim jitter silently drifts |

There is no bool to patch and no exit call to NOP — the product just stops aiming
correctly, and the poison is irreversible across all layers.

### FrameCanary in the aim path

`IOManagement::ActHipFire` / `ActAds` fold `HardenedCore::FrameCanary()` into the
noise computation (`noise ^= FrameCanary()`). Killing every watchdog thread still
leaves the canary: every 256 shots it runs `QuickSelfCheck()` (syscall debugger
detection + hardware breakpoints + session integrity) and poisons on failure.
Patching the canary's return constant changes the aim jitter pattern — a telltale
sign the build is being tampered with.

### Syscall-based anti-debug (bypasses hooked ntdll)

`HardenedCore::DebuggerDetectedSyscall()` uses the Obfusk8 `K8_SYSCALL` engine
(`ZwQueryInformationProcess` ProcessDebugObjectHandle/ProcessDebugFlags/ProcessDebugPort
+ `ZwQuerySystemInformation` SystemKernelDebuggerInformation). Crackers who hook
`IsDebuggerPresent`/`NtQueryInformationProcess` in the IAT or inline can no longer
fake the result. Falls back to PEB checks if the syscall engine can't initialize.

### Jittered sleeps

`HardenedCore::HardenedSleep()` replaces every fixed `Sleep()` in protection loops
(fixed sleeps were prime patch targets). Watchdog heartbeats (`GetTickCount64` +
15s grace) make any frozen thread a tamper signal for its partner.

### What was wired up

| Component | Before | After |
|-----------|--------|-------|
| `RuntimeProtection::StartProtectionThreads()` | never called (dead code) | called in `RIVENEntry.cpp` |
| `RuntimeProtection::Initialize()` | basic init | + `ThreadAutoHide::Enable()`, `SetDebugPortZero()` |
| Monitor thread response | `exit(0)` (patchable) | `HardenedCore::OnTamperDetected()` (corruption) |
| Monitor loop | `Sleep(5000)` fixed | jittered sleep + heartbeat + syscall debug + HW breakpoints + session integrity |
| `SecurePayload` key storage | raw 32-byte key in memory (dumpable) | XOR-masked with per-boot BCrypt random mask; only unmasked transiently in the decrypt path |
| `LicenseGuard` watchdog | `Sleep(30000)` fixed, session checks pre-login | jittered sleep, syscall detection, session checks only post-login (`IsKeyValid` gate) |

### Note on pre-login safety

All session-dependent checks are gated on `SecurePayload::IsKeyValid()` so the
watchdogs never poison a legit user sitting at the Login screen (where no session
exists yet).

---

## 8. Deep-Hardening Pass (latest)

### 8.1 VIRTUALIZER markers now materialize real Obfusk8

The Themida/CodeVirtualizer post-linker was never available, so the old
`VIRTUALIZER_MUTATE_ONLY_START/END` stubs expanded to NOTHING — the "protected"
regions were plain machine code. All three `VirtualizerSDK.h` copies
(`Utilities/`, `Core/`, `Games/`) plus both `VirtualizerSDK_CAT_BLACK.h` now
expand the markers to real Obfusk8 materialization:

- opaque predicate + MBA-seeded global mutation (`OBF_OPAQUE_PREDICATE_TRUE_1/2`,
  `OBF_MBA_XOR/ADD`) so every marker site is a runtime state mutation, not a NOP
- `OBF_CALL_ANY_LOCAL_JUNK()` for the heavier markers
- automatically inert in TUs that don't include `Obfusk8Core.hpp`
  (Fortnite.cpp, ScreenCapture.cpp, Target.cpp) — guaranteed safe everywhere

### 8.2 VMProtect marker mode (opt-in)

Define `RIVEN_USE_VMP` to switch all marker sites to REAL VMProtect markers
(`VMProtectBeginMutation/Virtualization/Ultra` + `VMProtectEnd`, exported from
no-op stubs in `Utilities.cpp`). The post-link pass runs `Tools/ApplyVMProtect.ps1`
(`VMProtect_Con.exe`) — wired into the build as an opt-in PostBuildEvent
(env var `RIVEN_APPLY_VMP=1`). Without a licensed VMProtect the raw build stays
fully functional.

**Perf note:** VMProtect *Virtualization* is 10-50x slower — keep it off the
per-frame aim math (`VIRTUALIZER_START` sites); prefer Mutation/Ultra there.

### 8.3 Anti-reversing tool scan

`HardenedCore::ReversingToolDetected()` (in `WatchdogLoopB`, ~3s cadence):

- process snapshot matching 24 known tools (x64dbg/x32dbg/x96dbg, OllyDbg, IDA,
  WinDbg, Cheat Engine, Process Hacker, procmon, Scylla, ImportREC, dnSpy,
  ExtremeDumper, pe-sieve, de4dot...) — names encrypted with `OBFUSCATE_STRING`
- window-title scan covering renamed executables (x64dbg/ollydbg/IDA/Cheat
  Engine/Process Hacker/WinDbg)

Any hit → `OnTamperDetected()` (corruption chain).

### 8.4 .text baseline — masked dual-copy + randomized cadence

`ServerSecureClient::VerifyTextSectionIntegrity()`:

- baseline stored as **two XOR-masked copies** with runtime-derived keys
  (no constants to anchor a search); copies must agree on every check
- check fires at a **pseudo-random offset** (avg every ~90 calls) instead of a
  fixed counter — the check can't be matched to a predictable frame/call number
- the `.text` FNV-1a scan itself is unchanged

### 8.5 Decoy session blobs

`ServerSecureClient.cpp` keeps two "live-looking" `SessionContext` copies
(valid CRCs, plausible calibration values, random AES keys) in atomic storage,
refreshed on a lazy cadence from inside `VerifySessionIntegrity()`. Memory
pattern scanners / struct hunters land on decoys; extraction yields nothing.

### 8.6 String encryption sweep (`RuntimeProtection.cpp`, `App.cpp`)

- all `ntdll.dll`/`kernel32.dll` resolves and API names
  (`NtSetInformationThread`, `NtSetInformationProcess`, `NtQueryInformationProcess`,
  `NtGetContextThread`, `RtlCreateSecurityDescriptor`, `RtlSetDaclSecurityDescriptor`,
  `SetProcessMitigationPolicy`, `SeDebugPrivilege`) → `OBFUSCATE_STRING`
- AntiVM registry keys, file paths, process names, CPUID vendor strings,
  DriverDesc keywords, svchost decoy strings → encrypted
- `App.cpp` single-instance mutex name → `OBFUSCATE_WSTRING`

### 8.7 Randomized canary cadence

`HardenedCore::FrameCanary()` now fires its deep re-verify at hash-progressed
pseudo-random frame numbers (~every 256) instead of every 256th frame — the
cadence can't be predicted or anchored to a fixed frame offset.

### 8.8 Offline grant — server-signed fallback unlock

Closes the license bypass where a dead server (e.g. blocked `127.0.0.1:8443`)
produced a fallback session: previously a random key became "valid" and aim
kept working without payment.

- **Policy:** a fallback session is only materialized if a valid offline grant
  exists; otherwise `Initialize()` returns `false`, no session key is set, and
  aim stays dead (calibration returns raw `rawX*1000`).
- **Wire format:** `grant = base64(payload + "|" + base64(signature))`
  - `payload` = compact JSON: `{"ts":<unix>,"hwid":<client HWID>,"license":<key>}`
  - `signature` = RSA-2048 PKCS#1 v1.5 (SHA-256) over the exact payload bytes
- **Keys:** client embeds the public key only (`Utilities/OfflineGrantKey.h`);
  the private key lives solely on the server (`Tools/server_offline_private.pem`,
  never shipped). Verification uses BCrypt: `BCRYPT_RSAPUBLIC_BLOB` +
  `BCRYPT_PKCS1_PADDING_INFO{BCRYPT_SHA256_ALGORITHM}`.
- **Grace:** grant valid 48h from `ts`; `ts` must not be in the future beyond
  300s skew. **Clock-rollback guard:** `g_lastSeenWallMs` (GetTickCount64) is
  persisted next to the grant file (`%SystemRoot%\node\syscache.g`, XOR-obfuscated
  name); if the wall clock steps backwards the grant is rejected until the
  server sends a fresh one.
- **Flow:** server emits `offline_grant` in `/v2/session/init` and
  `/v2/session/heartbeat` responses; `ParseSessionResponse` verifies + stores it;
  `SendHeartbeat` refreshes the stored grant on healthy heartbeats and (3-strike)
  kills the session when the server is unreachable *and* no grant grace remains.
- **Server tooling:** `Tools/server_grant_signing.py` (`make_offline_grant()`)
  produces grants; `Tools/GenOfflineGrantKey.ps1` regenerates the key pair;
  `Tools/make_test_grant.ps1` + `Tools/grant_smoke_test.cpp` are an end-to-end
  test harness (pure-BigInteger PKCS#1 v1.5 signing → BCrypt verification —
  validates tamper rejection and key pairing without shipping the private key).
- Verified end-to-end: good signature PASS, tampered payload/signature and
  truncated signature all correctly FAIL.

## File Map

```
Core/
├── ServerSecureClient.h/.cpp   # Secure HTTPS + cert pinning + session init
├── SecurePayload.h/.cpp        # AES-256 memory decryption + calibration
├── KeyauxClient.h/.cpp         # Refactored: uses ServerSecureClient, no bool returns
├── IOManagement.cpp            # Remote computation injection (calibration + noise)
├── App.cpp                     # Heartbeat integration + session management
├── SenyEntry.cpp               # Clean startup/shutdown with key invalidation

Utilities/
├── LicenseGuard.h/.cpp         # Refactored: no g_licenseValid bool, delegates to session
├── RuntimeProtection.h/.cpp    # Anti-debug/anti-dump/hook detection + wired monitor + encrypted strings
├── HardenedCore.h/.cpp         # Self-healing watchdogs + syscall anti-debug + canary + tool scan
├── VirtualizerSDK.h            # Obfusk8 materialization (or VMProtect markers with RIVEN_USE_VMP)

server_example.py               # Reference Python server implementation
SECURE_ARCHITECTURE.md          # This file

Tools/
├── GenOfflineGrantKey.ps1      # Server key pair generation (RFC 8017 DER, self-validated)
├── server_offline_private.pem  # SERVER ONLY — never ship to clients
├── server_offline_public.pem   # Regenerate Utilities/OfflineGrantKey.h from this
├── server_grant_signing.py     # Server reference: make_offline_grant(hwid, license)
├── make_test_grant.ps1         # Test-vector generator (pure BigInteger signing)
├── grant_smoke_test.cpp        # End-to-end BCrypt verification test
└── grant_smoke_build.bat       # Smoke-test build (VsDevCmd + cl + bcrypt.lib)
```

## Threat Model Summary

| Attack | Mitigation |
|--------|-----------|
| Patch `bool` check | No single bool — session integrity verified via CRC, key validation, and data flow |
| Fake server / DNS redirect | Certificate pinning rejects any non-server cert |
| Debugger / breakpoint | RuntimeProtection anti-debug + timing checks + HardenedCore syscall detection |
| Kill / suspend watchdog threads | Mutual watchdogs (A↔B) + supervisor respawn + FrameCanary in aim path |
| Patch fixed `Sleep()` in protection loops | Jittered `HardenedCore::HardenedSleep()` — heartbeat staleness = tamper |
| Patch `exit(0)` tamper response | No exit calls — `OnTamperDetected()` corrupts session + key + canary flag |
| Dump AES key from memory | Per-boot XOR mask over key storage; raw key only transient in decrypt path |
| Pattern-scan session struct in memory | Decoy session blobs with valid CRCs + plausible values |
| Patch .text baseline hash (fixed counter) | Masked dual-copy baseline + pseudo-random check cadence |
| Predict the canary check frame | Hash-progressed cadence (avg every 256, unanchored) |
| Run reversing tools (renamed exes) | Process + window-title scan → corruption |
| Reconstruct protected regions | Markers materialize real Obfusk8; `RIVEN_USE_VMP` switches them to VMProtect markers |
| Hook `IsDebuggerPresent` / ntdll exports | Syscall-based detection via Obfusk8 `K8_SYSCALL` engine |
| Dump decrypted model | Model decrypted in memory with session key, buffers protected |
| Replay / MITM | Session key per-authentication, timestamp validation on server |
| Static analysis | Obfusk8 control flow + encrypted strings + dynamic API resolution |
| Bypass server by returning "true" | Server must return valid calibration data; garbage data = broken aim |
| Block server / force fallback session | Fallback unlocked only by RSA-2048-signed offline grant (48h grace, clock-rollback guarded) |
