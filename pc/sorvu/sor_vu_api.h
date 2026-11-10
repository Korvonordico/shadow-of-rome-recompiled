// sor-vu.dll's exports (pc/sorvu/sor_vu.cpp) as function pointer types, for sor.exe, which loads the DLL at run time
// with LoadLibrary/GetProcAddress (pc/sor_vu1.cpp). Keep in step with sor_vu.cpp; bump sorvu_version there when an
// export changes. GPL-3.0+.
#pragma once

#include <cstdint>

extern "C"
{
    typedef void (*SorVuXgkick)(void *user, const uint8_t *packet, uint32_t bytes, uint32_t startQw);

    typedef int (*SorVuVersion)();
    typedef int (*SorVuInit)(int useJit, int clampMode, int flagHack, int iBitHack, SorVuXgkick xgkick, void *user,
                             char *err, int errSize);
    typedef void (*SorVuVoid)();
    typedef void (*SorVuSetInt)(int value);
    typedef uint8_t *(*SorVuMem)();
    typedef void (*SorVuMicroWritten)(uint32_t offset, uint32_t size);
    typedef void (*SorVuWriteMicro)(uint32_t offset, const void *src, uint32_t size);
    typedef void (*SorVuSetTop)(uint32_t top, uint32_t itop);
    typedef void (*SorVuSetU32)(uint32_t value);
    typedef uint32_t (*SorVuExecute)(uint32_t startPcBytes, uint32_t maxCycles);
    typedef uint32_t (*SorVuContinue)(uint32_t maxCycles);
    typedef int (*SorVuInt)();
    typedef uint32_t (*SorVuU32)();
    typedef uint64_t (*SorVuU64)();
    typedef uint32_t (*SorVuGetVi)(int index);
    typedef void (*SorVuSetVi)(int index, uint32_t value);
    typedef void (*SorVuGetVf)(int index, uint32_t out[4]);
    typedef void (*SorVuSetVf)(int index, const uint32_t in[4]);
    typedef void (*SorVuGetAcc)(uint32_t out[4]);
    typedef void (*SorVuSetAcc)(const uint32_t in[4]);
}

// The version of the exports this sor.exe expects (sorvu_version()).
constexpr int kSorVuVersion = 1;
// Indexes of sorvu_get_vi / sorvu_set_vi beyond VI00..VI15 (kzvu's, PCSX2's CFC2 numbering).
enum : int
{
    kSorVuStatus = 16,
    kSorVuMac = 17,
    kSorVuClip = 18,
    kSorVuR = 20,
    kSorVuI = 21,
    kSorVuQ = 22,
    kSorVuP = 23,
    kSorVuTpc = 26,
};
