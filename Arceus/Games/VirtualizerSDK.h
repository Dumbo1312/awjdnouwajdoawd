#pragma once

//
// VirtualizerSDK compatibility layer.
//
// The original Themida/CodeVirtualizer post-linker is NOT available in this
// build, so the SDK markers used to expand to NOTHING — the "protected"
// regions were plain machine code. To give those regions real protection at
// compile time (no external tool), the markers now materialize Obfusk8
// control-flow + opaque predicates + seeding.
//
// Files that don't pull in Obfusk8Core.hpp simply get inert markers (same
// behavior as the original stubs) — guaranteed safe in every TU.
//
// OPTIONAL: define RIVEN_USE_VMP to switch these sites to REAL VMProtect
// markers (VMProtectBegin* / VMProtectEnd). The post-link pass is then run by
// Tools/ApplyVMProtect.ps1 (VMProtect_Con.exe). Until that pass runs, the
// exports are no-op stubs defined in Utilities.cpp — the binary still builds
// and runs normally.
//

#ifdef RIVEN_USE_VMP

extern "C" __declspec(dllexport) void __cdecl VMProtectBegin(const char* markerName);
extern "C" __declspec(dllexport) void __cdecl VMProtectBeginVirtualization(const char* markerName);
extern "C" __declspec(dllexport) void __cdecl VMProtectBeginMutation(const char* markerName);
extern "C" __declspec(dllexport) void __cdecl VMProtectBeginUltra(const char* markerName);
extern "C" __declspec(dllexport) void __cdecl VMProtectEnd(void);

#define VMP_TOSTR_(s)  #s
#define VMP_TOSTR(s)   VMP_TOSTR_(s)

#define VIRTUALIZER_MUTATE_ONLY_START \
    VMProtectBeginMutation(__FILE__ ":" VMP_TOSTR(__LINE__));
#define VIRTUALIZER_MUTATE_ONLY_END \
    VMProtectEnd();
#define VIRTUALIZER_START \
    VMProtectBeginVirtualization(__FILE__ ":" VMP_TOSTR(__LINE__));
#define VIRTUALIZER_END \
    VMProtectEnd();

#elif defined(OBF_OPAQUE_PREDICATE_TRUE_1)

#define VIRTUALIZER_MUTATE_ONLY_START \
    do { \
        volatile int _vms_seed = (int)_obf_global_opaque_seed ^ (int)__LINE__; \
        if (OBF_OPAQUE_PREDICATE_TRUE_1()) { \
            _obf_global_opaque_seed = OBF_MBA_XOR(_obf_global_opaque_seed, _vms_seed); \
        } \
    } while (0);

#define VIRTUALIZER_MUTATE_ONLY_END \
    do { \
        NOP(); \
    } while (0);

#define VIRTUALIZER_START \
    do { \
        volatile int _vms_seed = OBF_MBA_ADD((unsigned int)__COUNTER__, (unsigned int)_obf_global_opaque_seed); \
        if (OBF_OPAQUE_PREDICATE_TRUE_1() && OBF_OPAQUE_PREDICATE_TRUE_2(_vms_seed)) { \
            _obf_global_opaque_seed = OBF_MBA_XOR(_obf_global_opaque_seed, _vms_seed); \
        } \
        OBF_CALL_ANY_LOCAL_JUNK(); \
    } while (0);

#define VIRTUALIZER_END \
    do { \
        NOP(); \
    } while (0);

#else

#define VIRTUALIZER_MUTATE_ONLY_START
#define VIRTUALIZER_MUTATE_ONLY_END
#define VIRTUALIZER_START
#define VIRTUALIZER_END

#endif