// sor-vu.dll: kzvu (PCSX2's microVU VU1 recompiler plus PCSX2's VU interpreter, taken from KillzoneRecomp's ext/kzvu,
// commit in pc/kzvu/KILLZONE-COMMIT.txt) behind a plain C interface, so sor.exe, which is built with MinGW, can load it
// with LoadLibrary/GetProcAddress. kzvu is MSVC-only and its API is C++. The declarations for sor.exe are in
// sor_vu_api.h. GPL-3.0+ like kzvu (it links PCSX2 code).
//
// One VU1 per process. Every VU1 call must come from the thread that called sorvu_bind_vu1_thread() first (the game's
// VIF1 thread), see kzvu.h. sor-vu.dll is built with /arch:AVX2: sor.exe must check the CPU before loading it.
#include "kzvu.h"

#include <cstdint>
#include <cstdio>
#include <string>

#define SORVU_API extern "C" __declspec(dllexport)

typedef void (*SorVuXgkick)(void *user, const uint8_t *packet, uint32_t bytes, uint32_t startQw);

// Bumped when an export changes, so sor.exe can refuse an old DLL.
SORVU_API int sorvu_version() { return 1; }

// useJit: 1 = microVU, 0 = PCSX2's VU interpreter (slow, for comparisons). clampMode as PCSX2's vuClampMode: 0 none,
// 1 normal (PCSX2's default, and Shadow of Rome has no GameIndex entry for it), 2 extra, 3 extra + preserve sign.
// flagHack: PCSX2's "mVU flag hack", on by default in PCSX2. iBitHack: PCSX2's gamefix, off by default in PCSX2.
// xgkick: called once per complete GIF packet from inside sorvu_execute/sorvu_continue. Returns 1 on success; on
// failure the reason is written to err.
SORVU_API int sorvu_init(int useJit, int clampMode, int flagHack, int iBitHack, SorVuXgkick xgkick, void *user,
                         char *err, int errSize)
{
    KzvuConfig c;
    c.useJit = useJit != 0;
    c.clampMode = clampMode;
    c.flagHack = flagHack != 0;
    c.iBitHack = iBitHack != 0;
    c.xgkickHack = false;
    c.xgkick = xgkick;
    c.xgkickUser = user;
    std::string e;
    const bool ok = kzvuInit(c, &e);
    if (!ok && err && errSize > 0)
        std::snprintf(err, static_cast<size_t>(errSize), "%s", e.c_str());
    return ok ? 1 : 0;
}

SORVU_API void sorvu_shutdown() { kzvuShutdown(); }
SORVU_API void sorvu_bind_vu1_thread() { kzvuBindVu1Thread(); }
SORVU_API void sorvu_reset() { kzvuReset(); }

// Switches microVU <-> interpreter between programs (flushes nothing else).
SORVU_API void sorvu_set_jit(int useJit)
{
    KzvuConfig c = kzvuGetConfig();
    c.useJit = useJit != 0;
    kzvuSetConfig(c);
}

// kzvu owns VU1 micro and data memory (16 KB each, fixed for the life of the process): the VIF1 must write there.
SORVU_API uint8_t *sorvu_code_mem() { return kzvuCodeMem(); }
SORVU_API uint8_t *sorvu_data_mem() { return kzvuDataMem(); }
SORVU_API void sorvu_micro_written(uint32_t offset, uint32_t size) { kzvuMicroWritten(offset, size); }
SORVU_API void sorvu_write_micro(uint32_t offset, const void *src, uint32_t size) { kzvuWriteMicro(offset, src, size); }

SORVU_API void sorvu_set_top(uint32_t top, uint32_t itop) { kzvuSetTop(top, itop); }
SORVU_API void sorvu_set_fbrst(uint32_t fbrst) { kzvuSetFBRST(fbrst); }

// MSCAL: startPcBytes = byte address (multiple of 8); 0xFFFFFFFF = MSCNT (continue at TPC). Returns VU cycles used.
SORVU_API uint32_t sorvu_execute(uint32_t startPcBytes, uint32_t maxCycles) { return kzvuExecute(startPcBytes, maxCycles); }
// Resumes a program that ran out of budget.
SORVU_API uint32_t sorvu_continue(uint32_t maxCycles) { return kzvuContinue(maxCycles); }
// Changes the clamp mode between programs (see sorvu_init).
SORVU_API void sorvu_set_clamp(int clampMode)
{
    KzvuConfig c = kzvuGetConfig();
    c.clampMode = clampMode;
    kzvuSetConfig(c);
}

SORVU_API int sorvu_running() { return kzvuRunning() ? 1 : 0; }
// 1 once when a program raised the VU1 interrupt (I bit with FBRST/interrupts on), then cleared.
SORVU_API int sorvu_take_interrupt() { return kzvuTakeInterrupt() ? 1 : 0; }
SORVU_API uint32_t sorvu_vpu_stat() { return kzvuVpuStat(); }
SORVU_API uint64_t sorvu_cycles() { return kzvuCycles(); }
SORVU_API uint64_t sorvu_xgkick_count() { return kzvuXgkickCount(); }
SORVU_API uint64_t sorvu_cache_resets() { return kzvuCacheResets(1); }

// Registers. sorvu_get_vi/sorvu_set_vi take 0..15 for VI00..VI15 and kzvu's special indexes: 16 status, 17 MAC,
// 18 clip, 20 R, 21 I, 22 Q, 23 P, 26 TPC (raw 32-bit values; I, Q, P, R as IEEE bits). VF and ACC as raw IEEE bits,
// x y z w.
SORVU_API uint32_t sorvu_get_vi(int index) { return kzvuGetVI(index); }
SORVU_API void sorvu_set_vi(int index, uint32_t value) { kzvuSetVI(index, value); }
SORVU_API void sorvu_get_vf(int index, uint32_t out[4]) { kzvuGetVF(index, out); }
SORVU_API void sorvu_set_vf(int index, const uint32_t in[4]) { kzvuSetVF(index, in); }
SORVU_API void sorvu_get_acc(uint32_t out[4]) { kzvuGetACC(out); }
SORVU_API void sorvu_set_acc(const uint32_t in[4]) { kzvuSetACC(in); }
