#pragma once

//
// VirtualizerSDK_CAT_BLACK compatibility layer.
//
// Original Themida marker stubs expanded to nothing. Now materializes real
// Obfusk8 protection at compile time. Falls back to inert markers in TUs
// without Obfusk8Core.hpp.
//
// OPTIONAL: define RIVEN_USE_VMP to switch these sites to REAL VMProtect
// markers (VMProtectBeginUltra / VMProtectEnd). See VirtualizerSDK.h.
//

#ifdef RIVEN_USE_VMP

extern "C" __declspec(dllexport) void __cdecl VMProtectBeginUltra(const char* markerName);
extern "C" __declspec(dllexport) void __cdecl VMProtectEnd(void);

#define VMP_TOSTR_(s)  #s
#define VMP_TOSTR(s)   VMP_TOSTR_(s)

#define VIRTUALIZER_CAT_BLACK_START \
    VMProtectBeginUltra(__FILE__ ":" VMP_TOSTR(__LINE__));
#define VIRTUALIZER_CAT_BLACK_END \
    VMProtectEnd();

#elif defined(OBF_OPAQUE_PREDICATE_TRUE_1)

#define VIRTUALIZER_CAT_BLACK_START \
    do { \
        volatile int _vms_seed = (int)_obf_global_opaque_seed ^ (int)__LINE__; \
        if (OBF_OPAQUE_PREDICATE_TRUE_1()) { \
            _obf_global_opaque_seed = OBF_MBA_XOR(_obf_global_opaque_seed, _vms_seed); \
        } \
        OBF_CALL_ANY_LOCAL_JUNK(); \
    } while (0);

#define VIRTUALIZER_CAT_BLACK_END \
    do { \
        NOP(); \
    } while (0);

#else

#define VIRTUALIZER_CAT_BLACK_START
#define VIRTUALIZER_CAT_BLACK_END

#endif