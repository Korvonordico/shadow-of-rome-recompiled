// SOR_VU1: which engine runs VU1, the PS2 vector unit whose microprograms turn the game's 3D models into triangles for
// the GS (the author, 10/11/2026: try the existing VU1 recompilers one at a time and keep the first that gives a fixed 60
// with no graphics errors). Step 1 is PCSX2's microVU, through kzvu (KillzoneRecomp's ext/kzvu, pc/kzvu) built into
// sor-vu.dll (pc/sorvu), which this file loads at run time:
//   unset or "nostro": ours (PS2Recomp's VU1 interpreter and our fast path), exactly as before; sor-vu.dll is not
//       even loaded.
//   "pcsx2": PCSX2's microVU runs every VU1 program; VU1 micro and data memory are the DLL's.
//   "confronto" or "confronto:N" (compare): ours drives the game; about one program in N (default 10), picked at
//       random so every kind of program gets its turn, is run again on microVU from the same starting state and the
//       two results (GIF packets, data memory, registers) are compared. One "[sor-vu] confronto" line every 5 s, the
//       first 20 differences in detail.
// Any problem (processor without AVX2/FMA3, DLL missing or of another version, microVU not starting): one "[sor-vu]"
// line with the reason, and ours runs. Never a crash.
// Adapted from the Cartoon Network Racing port's cnr_vu.cpp and KillzoneRecomp's src/kz_vu.cpp (both GPL-3.0, like
// this repository).
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/sor_vu1_host.h"
#include "sorvu/sor_vu_api.h"

#include <atomic>
#include <chrono>
#include <cpuid.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>
#include <xmmintrin.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern uint64_t g_sorVu1Cicli, g_sorVu1Run; // ps2_vu1_core.cpp: the [tempi] line of the tests counts VU1 work

namespace
{
struct Api
{
    SorVuVersion version;
    SorVuInit init;
    SorVuVoid shutdown;
    SorVuVoid bind;
    SorVuVoid reset;
    SorVuSetInt setJit;
    SorVuSetInt setClamp;
    SorVuMem codeMem;
    SorVuMem dataMem;
    SorVuMicroWritten microWritten;
    SorVuWriteMicro writeMicro;
    SorVuSetTop setTop;
    SorVuSetU32 setFbrst;
    SorVuExecute execute;
    SorVuContinue cont;
    SorVuInt running;
    SorVuInt takeInterrupt;
    SorVuU32 vpuStat;
    SorVuU64 cycles;
    SorVuU64 xgkickCount;
    SorVuU64 cacheResets;
    SorVuGetVi getVi;
    SorVuSetVi setVi;
    SorVuGetVf getVf;
    SorVuSetVf setVf;
    SorVuGetAcc getAcc;
    SorVuSetAcc setAcc;
};
Api g_api{};
PS2Memory *g_memoria = nullptr;

constexpr uint32_t kMemoria = 0x4000u;  // VU1 micro memory and data memory, 16 KB each
constexpr uint32_t kBudget = 1u << 24;  // VU cycles: run every program to its E bit (our VIF1 is synchronous), as cnr_vu.cpp
constexpr uint32_t kFermoDT = 0x0600u;  // VPU_STAT: VU1 stopped on a D (0x200) or T (0x400) bit

// ---- loading ------------------------------------------------------------------------------------------------------------

// sor-vu.dll is built with MSVC's /arch:AVX2: AVX2 and FMA3, which the compiler may mix with BMI1/BMI2/LZCNT (every
// AVX2 processor from Intel and AMD has them), and Windows must save the AVX registers (OSXSAVE + XCR0).
bool processoreAdatto(std::string &descrizione)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    const unsigned massimo = __get_cpuid_max(0, nullptr);
    bool fma = false, osxsave = false, avx = false, osAvx = false, avx2 = false, bmi1 = false, bmi2 = false, lzcnt = false;
    if (massimo >= 1 && __get_cpuid(1, &a, &b, &c, &d))
    {
        fma = (c & (1u << 12)) != 0u;
        osxsave = (c & (1u << 27)) != 0u;
        avx = (c & (1u << 28)) != 0u;
    }
    if (osxsave)
    {
        unsigned lo = 0, hi = 0;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0u));
        osAvx = (lo & 6u) == 6u; // XMM and YMM state saved by the OS
    }
    if (massimo >= 7)
    {
        __cpuid_count(7, 0, a, b, c, d);
        avx2 = (b & (1u << 5)) != 0u;
        bmi1 = (b & (1u << 3)) != 0u;
        bmi2 = (b & (1u << 8)) != 0u;
    }
    if (__get_cpuid_max(0x80000000u, nullptr) >= 0x80000001u && __get_cpuid(0x80000001u, &a, &b, &c, &d))
        lzcnt = (c & (1u << 5)) != 0u;
    char testo[160];
    std::snprintf(testo, sizeof(testo), "AVX %s, AVX2 %s, FMA3 %s, BMI1/BMI2/LZCNT %s, AVX saved by Windows %s",
                  avx ? "yes" : "no", avx2 ? "yes" : "no", fma ? "yes" : "no",
                  bmi1 && bmi2 && lzcnt ? "yes" : "no", osAvx ? "yes" : "no");
    descrizione = testo;
    return avx && avx2 && fma && bmi1 && bmi2 && lzcnt && osAvx;
}

template <typename T>
bool prendi(HMODULE dll, const char *nome, T &dove, std::string &mancanti)
{
    dove = reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(dll, nome)));
    if (!dove)
        mancanti += std::string(mancanti.empty() ? "" : ", ") + nome;
    return dove != nullptr;
}

bool caricaDll(std::string &perche, std::string &cpu)
{
    if (!processoreAdatto(cpu))
    {
        perche = "this processor or Windows lacks what sor-vu.dll needs: " + cpu;
        return false;
    }
    wchar_t percorso[MAX_PATH + 16] = {};
    const DWORD n = GetModuleFileNameW(nullptr, percorso, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        perche = "the folder of sor.exe is unknown";
        return false;
    }
    wchar_t *barra = std::wcsrchr(percorso, L'\\');
    if (!barra)
    {
        perche = "the folder of sor.exe is unknown";
        return false;
    }
    std::wcscpy(barra + 1, L"sor-vu.dll");
    HMODULE dll = LoadLibraryW(percorso);
    if (!dll)
    {
        const DWORD errore = GetLastError();
        perche = "sor-vu.dll did not load (Windows error " + std::to_string(errore) +
                 (errore == 126 ? ": the file, or the Microsoft Visual C++ 2015-2022 x64 runtime it needs, is missing)" : ")");
        return false;
    }
    std::string mancanti;
    prendi(dll, "sorvu_version", g_api.version, mancanti);
    prendi(dll, "sorvu_init", g_api.init, mancanti);
    prendi(dll, "sorvu_shutdown", g_api.shutdown, mancanti);
    prendi(dll, "sorvu_bind_vu1_thread", g_api.bind, mancanti);
    prendi(dll, "sorvu_reset", g_api.reset, mancanti);
    prendi(dll, "sorvu_set_jit", g_api.setJit, mancanti);
    prendi(dll, "sorvu_set_clamp", g_api.setClamp, mancanti);
    prendi(dll, "sorvu_code_mem", g_api.codeMem, mancanti);
    prendi(dll, "sorvu_data_mem", g_api.dataMem, mancanti);
    prendi(dll, "sorvu_micro_written", g_api.microWritten, mancanti);
    prendi(dll, "sorvu_write_micro", g_api.writeMicro, mancanti);
    prendi(dll, "sorvu_set_top", g_api.setTop, mancanti);
    prendi(dll, "sorvu_set_fbrst", g_api.setFbrst, mancanti);
    prendi(dll, "sorvu_execute", g_api.execute, mancanti);
    prendi(dll, "sorvu_continue", g_api.cont, mancanti);
    prendi(dll, "sorvu_running", g_api.running, mancanti);
    prendi(dll, "sorvu_take_interrupt", g_api.takeInterrupt, mancanti);
    prendi(dll, "sorvu_vpu_stat", g_api.vpuStat, mancanti);
    prendi(dll, "sorvu_cycles", g_api.cycles, mancanti);
    prendi(dll, "sorvu_xgkick_count", g_api.xgkickCount, mancanti);
    prendi(dll, "sorvu_cache_resets", g_api.cacheResets, mancanti);
    prendi(dll, "sorvu_get_vi", g_api.getVi, mancanti);
    prendi(dll, "sorvu_set_vi", g_api.setVi, mancanti);
    prendi(dll, "sorvu_get_vf", g_api.getVf, mancanti);
    prendi(dll, "sorvu_set_vf", g_api.setVf, mancanti);
    prendi(dll, "sorvu_get_acc", g_api.getAcc, mancanti);
    prendi(dll, "sorvu_set_acc", g_api.setAcc, mancanti);
    if (!mancanti.empty())
    {
        perche = "sor-vu.dll lacks " + mancanti;
        FreeLibrary(dll);
        return false;
    }
    if (const int v = g_api.version(); v != kSorVuVersion)
    {
        perche = "sor-vu.dll is version " + std::to_string(v) + ", this sor.exe needs version " + std::to_string(kSorVuVersion);
        FreeLibrary(dll);
        return false;
    }
    return true;
}

// ---- running ------------------------------------------------------------------------------------------------------------

// kzvu (kzvu.h): every thread that runs VU1 is bound first. Binding only points a per-thread pointer at the one VU1
// register file, so it is cheap and the same for every thread; the FBRST word and the pending interrupt are per thread,
// so they are set and taken on the thread of the run.
thread_local bool t_legato = false;
inline void lega()
{
    if (!t_legato)
    {
        g_api.bind();
        t_legato = true;
    }
}

// XGKICK, from inside sorvu_execute: kzvu runs with the VU's rounding mode (PCSX2's VU1 FPCR); the GS path gets the
// game's own, as with our VU1. In compare mode microVU's packets are only collected.
thread_local unsigned t_mxcsrGioco = 0x1F80u;
thread_local std::vector<std::vector<uint8_t>> *t_pacchettiKz = nullptr;
void xgkick(void *, const uint8_t *packet, uint32_t bytes, uint32_t)
{
    if (t_pacchettiKz)
    {
        t_pacchettiKz->emplace_back(packet, packet + bytes);
        return;
    }
    if (!g_memoria)
        return;
    const unsigned vu = _mm_getcsr();
    _mm_setcsr(t_mxcsrGioco);
    g_memoria->submitGifPacket(GifPathId::Path1, packet, bytes);
    _mm_setcsr(vu);
}

// Runs a program (startPcBytes, or 0xFFFFFFFF = continue at TPC) to its end, as cnr_vu.cpp's onMscal + finish():
// continue while it is running and has not stopped on a D/T bit.
uint32_t esegui(uint32_t startPcBytes)
{
    t_mxcsrGioco = _mm_getcsr();
    uint32_t cicli = g_api.execute(startPcBytes, kBudget);
    for (int i = 0; i < 16 && g_api.running() && (g_api.vpuStat() & kFermoDT) == 0u; ++i)
        cicli += g_api.cont(kBudget);
    return cicli;
}

// Micro memory: PS2Memory's VU1 code generation that kzvu has seen. MPG reports the bytes it changed (codeWritten);
// any other change (EE stores) only moves the generation, and the next run tells kzvu the whole memory may have changed.
std::atomic<uint64_t> g_generazioneVista{~0ull};

void codiceScritto(uint32_t offset, uint32_t bytes, uint64_t generazione)
{
    lega();
    g_api.microWritten(offset, bytes);
    // Only when this write is the one change since the last one kzvu saw; otherwise the next run checks everything.
    uint64_t attesa = generazione - 1u;
    g_generazioneVista.compare_exchange_strong(attesa, generazione, std::memory_order_relaxed);
}

std::atomic<uint64_t> g_pcsx2Programmi{0};
std::atomic<uint64_t> g_pcsx2Interrupt{0};
std::atomic<int64_t> g_pcsx2Riga{0};
uint64_t g_pcsx2ProgrammiPrima = 0, g_pcsx2KickPrima = 0, g_pcsx2CicliPrima = 0;

int64_t adessoNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void rigaPcsx2()
{
    const int64_t ora = adessoNs();
    int64_t prima = g_pcsx2Riga.load(std::memory_order_relaxed);
    if (ora - prima < 5000000000LL || !g_pcsx2Riga.compare_exchange_strong(prima, ora))
        return;
    const double secondi = (ora - prima) / 1e9;
    const uint64_t programmi = g_pcsx2Programmi.load(std::memory_order_relaxed);
    const uint64_t kick = g_api.xgkickCount();
    const uint64_t cicli = g_api.cycles();
    std::printf("[sor-vu] pcsx2: last %.1f s: %.0f VU1 programs a second, %.0f XGKICK packets a second, %.1f M VU1 cycles a "
                "second, microVU cache resets %llu (since start)\n",
                secondi, (programmi - g_pcsx2ProgrammiPrima) / secondi, (kick - g_pcsx2KickPrima) / secondi,
                (cicli - g_pcsx2CicliPrima) / secondi / 1e6, static_cast<unsigned long long>(g_api.cacheResets()));
    std::fflush(stdout);
    g_pcsx2ProgrammiPrima = programmi;
    g_pcsx2KickPrima = kick;
    g_pcsx2CicliPrima = cicli;
}

// SOR_VU1=pcsx2: MSCAL / MSCNT. Returns the VPU_STAT D/T bits, which the runtime writes as it does for ours.
uint32_t eseguiPcsx2(uint32_t startPcBytes, uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t generazione)
{
    lega();
    if (g_generazioneVista.load(std::memory_order_relaxed) != generazione)
    {
        g_api.microWritten(0u, kMemoria);
        g_generazioneVista.store(generazione, std::memory_order_relaxed);
    }
    g_api.setFbrst(fbrst);
    g_api.setTop(top, itop);
    const uint32_t cicli = esegui(startPcBytes);
    // Our VU1 does nothing with the VU1 interrupt (a D/T-bit stop with FBRST's enables): neither does this, but the
    // first one is written in the log.
    if (g_api.takeInterrupt() && g_pcsx2Interrupt.fetch_add(1u) == 0u)
    {
        std::printf("[sor-vu] VU1 raised its interrupt (D/T bit stop, program started at 0x%x); ignored, as our VU1 does "
                    "(written once)\n", startPcBytes);
        std::fflush(stdout);
    }
    g_sorVu1Cicli += cicli;
    ++g_sorVu1Run;
    if ((g_pcsx2Programmi.fetch_add(1u, std::memory_order_relaxed) & 63u) == 63u)
        rigaPcsx2();
    return g_api.vpuStat() & kFermoDT;
}

// ---- compare mode -------------------------------------------------------------------------------------------------------

uint32_t g_ogni = 10u;                        // about one program in g_ogni is compared
uint64_t g_caso = 0x9E3779B97F4A7C15ull;     // the random pick (an LCG; VU1 runs one program at a time)
VU1State g_inizio{};                          // ours' state before the compared program
uint32_t g_inizioPc = 0, g_inizioTop = 0, g_inizioItop = 0, g_inizioFbrst = 0;
std::vector<uint8_t> g_datiInizio(kMemoria);  // data memory before it

struct Conto
{
    uint64_t programmi = 0, confrontati = 0, uguali = 0, diversi = 0;
    uint64_t pacchetti = 0, pacchettiPiccoli = 0, memoria = 0, memoriaPiccoli = 0, registri = 0, registriPiccoli = 0;
    uint64_t flag = 0;
};
Conto g_conto5s, g_contoTotale;
int64_t g_confrontoRiga = 0;
int g_dettagli = 0; // detail lines written (at most 20)
constexpr int kDettagliMassimi = 20;

uint32_t bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

float comeFloat(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// A difference "only in the last bits": the two 32-bit words differ by at most 4 as integers (for floats of the same
// sign: at most 4 units in the last place), or they are +0 and -0.
bool soloUltimiBit(uint32_t a, uint32_t b)
{
    if (((a | b) & 0x7FFFFFFFu) == 0u)
        return true;
    const int64_t d = static_cast<int64_t>(static_cast<int32_t>(a)) - static_cast<int32_t>(b);
    return d >= -4 && d <= 4;
}

// PCSX2's mVUallocSFLAGd: the status flag in microVU's own layout, for its four flag instances (sorvu_set_vi(16)
// stores the value as given; PCSX2's VU1 normal layout would be read wrong by microVU).
uint32_t statoMicroVu(uint32_t stato)
{
    return ((stato >> 3) & 0x18u) | ((stato << 11) & 0x1800u) | ((stato << 14) & 0x3cf0000u);
}

void dettaglio(const char *testo)
{
    if (g_dettagli >= kDettagliMassimi)
        return;
    ++g_dettagli;
    std::printf("[sor-vu] confronto difference %d/%d: program at 0x%04x (%s): %s\n", g_dettagli, kDettagliMassimi,
                g_inizioPc == 0xFFFFFFFFu ? g_inizio.pc : g_inizioPc, g_inizioPc == 0xFFFFFFFFu ? "MSCNT" : "MSCAL", testo);
    std::fflush(stdout);
}

std::string valori(uint32_t nostro, uint32_t kz)
{
    char t[128];
    std::snprintf(t, sizeof(t), "ours 0x%08x (%.9g), microVU 0x%08x (%.9g)", nostro, comeFloat(nostro), kz, comeFloat(kz));
    return t;
}

// Word-by-word comparison of two byte ranges of the same length: how many 32-bit words differ, whether all of them
// only in the last bits, and the first one.
struct Parole
{
    uint32_t diverse = 0;
    bool grandi = false;
    uint32_t prima = 0, nostra = 0, kz = 0;
};

Parole confrontaParole(const uint8_t *nostri, const uint8_t *kz, uint32_t bytes)
{
    Parole p;
    if (std::memcmp(nostri, kz, bytes) == 0)
        return p;
    for (uint32_t i = 0; i + 4u <= bytes; i += 4u)
    {
        uint32_t a, b;
        std::memcpy(&a, nostri + i, 4);
        std::memcpy(&b, kz + i, 4);
        if (a == b)
            continue;
        if (p.diverse++ == 0u)
        {
            p.prima = i / 4u;
            p.nostra = a;
            p.kz = b;
        }
        if (!soloUltimiBit(a, b))
            p.grandi = true;
    }
    return p;
}

void rigaConfronto(bool forza)
{
    const int64_t ora = adessoNs();
    if (g_confrontoRiga == 0)
        g_confrontoRiga = ora;
    if (!forza && ora - g_confrontoRiga < 5000000000LL)
        return;
    const double secondi = (ora - g_confrontoRiga) / 1e9;
    g_confrontoRiga = ora;
    const Conto &c = g_conto5s;
    std::printf("[sor-vu] confronto: last %.1f s: %llu VU1 programs, %llu compared (about 1 in %u), %llu identical, "
                "%llu different: GIF packets %llu (%llu only in the last bits), data memory %llu (%llu only in the last "
                "bits), registers %llu (%llu only in the last bits); flags differ in %llu (not counted: PCSX2's flag hack "
                "skips flag updates nobody reads). Since start: %llu compared, %llu identical, %llu different\n",
                secondi, (unsigned long long)c.programmi, (unsigned long long)c.confrontati, g_ogni,
                (unsigned long long)c.uguali, (unsigned long long)c.diversi, (unsigned long long)c.pacchetti,
                (unsigned long long)c.pacchettiPiccoli, (unsigned long long)c.memoria,
                (unsigned long long)c.memoriaPiccoli, (unsigned long long)c.registri,
                (unsigned long long)c.registriPiccoli, (unsigned long long)c.flag,
                (unsigned long long)g_contoTotale.confrontati, (unsigned long long)g_contoTotale.uguali,
                (unsigned long long)g_contoTotale.diversi);
    std::fflush(stdout);
    g_conto5s = Conto{};
}

bool primaDelNostro(const VU1State &stato, const uint8_t *codice, const uint8_t *dati, uint64_t generazione,
                    uint32_t startPcBytes, uint32_t top, uint32_t itop, uint32_t fbrst)
{
    ++g_conto5s.programmi;
    if ((g_conto5s.programmi & 63u) == 0u)
        rigaConfronto(false);
    g_caso = g_caso * 6364136223846793005ull + 1442695040888963407ull;
    if (g_ogni > 1u && (g_caso >> 33) % g_ogni != 0u)
        return false;
    lega();
    // Micro memory: microVU keeps its own copy; it is copied when ours changed (sorvu_write_micro only recompiles the
    // programs whose bytes really changed).
    if (g_generazioneVista.load(std::memory_order_relaxed) != generazione)
    {
        g_api.writeMicro(0u, codice, kMemoria);
        g_generazioneVista.store(generazione, std::memory_order_relaxed);
    }
    g_inizio = stato;
    g_inizioPc = startPcBytes;
    g_inizioTop = top;
    g_inizioItop = itop;
    g_inizioFbrst = fbrst;
    std::memcpy(g_datiInizio.data(), dati, kMemoria);
    return true;
}

void dopoIlNostro(const VU1State &nostro, const uint8_t *datiNostri, const std::vector<std::vector<uint8_t>> &pacchettiNostri)
{
    static bool s_avvisoReset = false;
    lega();
    // microVU from ours' starting state.
    if (g_api.running())
    {
        // A program left running (out of budget): kzvu would finish it first. Never expected; reset (it also forgets
        // the compiled programs).
        if (!s_avvisoReset)
        {
            s_avvisoReset = true;
            std::printf("[sor-vu] confronto: microVU still had a program running; reset (written once)\n");
            std::fflush(stdout);
        }
        g_api.reset();
    }
    g_api.takeInterrupt();
    const VU1State &p = g_inizio;
    std::memcpy(g_api.dataMem(), g_datiInizio.data(), kMemoria);
    for (int r = 1; r < 16; ++r)
        g_api.setVi(r, static_cast<uint32_t>(p.vi[r]) & 0xFFFFu);
    for (int r = 1; r < 32; ++r)
    {
        uint32_t v[4];
        std::memcpy(v, p.vf[r], 16);
        g_api.setVf(r, v);
    }
    {
        uint32_t v[4];
        std::memcpy(v, p.acc, 16);
        g_api.setAcc(v);
    }
    g_api.setVi(kSorVuQ, bits(p.q));
    g_api.setVi(kSorVuP, bits(p.p));
    g_api.setVi(kSorVuI, bits(p.i));
    g_api.setVi(kSorVuR, 0x3F800000u | (p.r & 0x007FFFFFu)); // ours' RNEXT leaves the exponent out
    g_api.setVi(kSorVuStatus, statoMicroVu(p.status & 0xFFFu));
    g_api.setVi(kSorVuMac, p.mac & 0xFFFFu);
    g_api.setVi(kSorVuClip, p.clip & 0xFFFFFFu);
    g_api.setTop(g_inizioTop, g_inizioItop);
    g_api.setFbrst(g_inizioFbrst);

    std::vector<std::vector<uint8_t>> pacchettiKz;
    t_pacchettiKz = &pacchettiKz;
    esegui(g_inizioPc == 0xFFFFFFFFu ? p.pc : g_inizioPc);
    t_pacchettiKz = nullptr;
    g_api.takeInterrupt();

    ++g_conto5s.confrontati;
    ++g_contoTotale.confrontati;
    char t[256];

    // GIF packets: how many, their sizes, their bytes.
    bool pacchettiDiversi = false, pacchettiGrandi = false;
    if (pacchettiNostri.size() != pacchettiKz.size())
    {
        pacchettiDiversi = pacchettiGrandi = true;
        std::snprintf(t, sizeof(t), "GIF packets: ours %zu, microVU %zu", pacchettiNostri.size(), pacchettiKz.size());
        dettaglio(t);
    }
    else
    {
        for (size_t i = 0; i < pacchettiNostri.size(); ++i)
        {
            const std::vector<uint8_t> &a = pacchettiNostri[i], &b = pacchettiKz[i];
            if (a.size() != b.size())
            {
                if (!pacchettiDiversi)
                {
                    std::snprintf(t, sizeof(t), "GIF packet %zu of %zu: ours %zu bytes, microVU %zu bytes", i + 1,
                                  pacchettiNostri.size(), a.size(), b.size());
                    dettaglio(t);
                }
                pacchettiDiversi = pacchettiGrandi = true;
                continue;
            }
            const Parole w = confrontaParole(a.data(), b.data(), static_cast<uint32_t>(a.size()));
            if (w.diverse == 0u)
                continue;
            if (!pacchettiDiversi)
            {
                std::snprintf(t, sizeof(t), "GIF packet %zu of %zu, qword %u word %u (%u words differ in it): %s", i + 1,
                              pacchettiNostri.size(), w.prima / 4u, w.prima % 4u, w.diverse,
                              valori(w.nostra, w.kz).c_str());
                dettaglio(t);
            }
            pacchettiDiversi = true;
            pacchettiGrandi = pacchettiGrandi || w.grandi;
        }
    }

    // Data memory after the program.
    const Parole m = confrontaParole(datiNostri, g_api.dataMem(), kMemoria);
    if (m.diverse)
    {
        std::snprintf(t, sizeof(t), "data memory qword 0x%03x word %u (%u words differ): %s", m.prima / 4u, m.prima % 4u,
                      m.diverse, valori(m.nostra, m.kz).c_str());
        dettaglio(t);
    }

    // Registers: VI (16 bits), VF, ACC, Q, P, I, R (its 23 random bits), the D/T stop.
    bool registriDiversi = false, registriGrandi = false;
    const auto registro = [&](const char *nome, uint32_t a, uint32_t b, bool intero)
    {
        if (a == b)
            return;
        if (!registriDiversi)
        {
            if (intero)
                std::snprintf(t, sizeof(t), "%s: ours 0x%x, microVU 0x%x", nome, a, b);
            else
                std::snprintf(t, sizeof(t), "%s: %s", nome, valori(a, b).c_str());
            dettaglio(t);
        }
        registriDiversi = true;
        if (!soloUltimiBit(a, b))
            registriGrandi = true;
    };
    char nome[16];
    for (int r = 1; r < 16; ++r)
    {
        std::snprintf(nome, sizeof(nome), "VI%02d", r);
        registro(nome, static_cast<uint32_t>(nostro.vi[r]) & 0xFFFFu, g_api.getVi(r) & 0xFFFFu, true);
    }
    static const char kCampo[4] = {'x', 'y', 'z', 'w'};
    for (int r = 1; r < 32; ++r)
    {
        uint32_t v[4];
        g_api.getVf(r, v);
        for (int c = 0; c < 4; ++c)
        {
            std::snprintf(nome, sizeof(nome), "VF%02d.%c", r, kCampo[c]);
            registro(nome, bits(nostro.vf[r][c]), v[c], false);
        }
    }
    {
        uint32_t v[4];
        g_api.getAcc(v);
        for (int c = 0; c < 4; ++c)
        {
            std::snprintf(nome, sizeof(nome), "ACC.%c", kCampo[c]);
            registro(nome, bits(nostro.acc[c]), v[c], false);
        }
    }
    registro("Q", bits(nostro.q), g_api.getVi(kSorVuQ), false);
    registro("P", bits(nostro.p), g_api.getVi(kSorVuP), false);
    registro("I", bits(nostro.i), g_api.getVi(kSorVuI), false);
    registro("R", nostro.r & 0x007FFFFFu, g_api.getVi(kSorVuR) & 0x007FFFFFu, true);
    registro("D/T stop", (nostro.stoppedByD ? 0x200u : 0u) | (nostro.stoppedByT ? 0x400u : 0u),
             g_api.vpuStat() & kFermoDT, true);

    // Flags: written down, not counted (microVU's flag hack, on as in PCSX2, skips the updates no instruction reads).
    const bool flag = (nostro.status & 0xFFFu) != (g_api.getVi(kSorVuStatus) & 0xFFFu) ||
                      (nostro.mac & 0xFFFFu) != (g_api.getVi(kSorVuMac) & 0xFFFFu) ||
                      (nostro.clip & 0xFFFFFFu) != (g_api.getVi(kSorVuClip) & 0xFFFFFFu);

    const bool diverso = pacchettiDiversi || m.diverse != 0u || registriDiversi;
    for (Conto *c : {&g_conto5s, &g_contoTotale})
    {
        if (!diverso)
            ++c->uguali;
        else
            ++c->diversi;
        if (pacchettiDiversi)
        {
            ++c->pacchetti;
            c->pacchettiPiccoli += pacchettiGrandi ? 0u : 1u;
        }
        if (m.diverse)
        {
            ++c->memoria;
            c->memoriaPiccoli += m.grandi ? 0u : 1u;
        }
        if (registriDiversi)
        {
            ++c->registri;
            c->registriPiccoli += registriGrandi ? 0u : 1u;
        }
        if (flag)
            ++c->flag;
    }
}
} // namespace

// pc/main_sor.cpp, right after PS2Runtime is built and before runtime.initialize() (PS2Memory takes microVU's memory
// there in "pcsx2" mode).
void sorVu1Installa(PS2Memory &memoria)
{
    const char *e = std::getenv("SOR_VU1");
    if (!e || !*e)
        return; // ours, as always
    const std::string modo(e);
    const bool pcsx2 = modo == "pcsx2";
    const bool confronto = modo == "confronto" || modo.rfind("confronto:", 0) == 0;
    if (modo == "nostro" || (!pcsx2 && !confronto))
    {
        std::printf(modo == "nostro" ? "[sor-vu] VU1 engine: ours (SOR_VU1=%s)\n"
                                     : "[sor-vu] SOR_VU1=%s not understood (nostro, pcsx2, confronto, confronto:N): ours runs\n",
                    e);
        std::fflush(stdout);
        return;
    }
    if (confronto && modo.size() > 10)
    {
        const int n = std::atoi(modo.c_str() + 10);
        if (n >= 1)
            g_ogni = static_cast<uint32_t>(n);
    }
    std::string perche, cpu;
    if (!caricaDll(perche, cpu))
    {
        std::printf("[sor-vu] PCSX2's VU1 not used (%s): ours runs\n", perche.c_str());
        std::fflush(stdout);
        return;
    }
    // PCSX2's defaults: microVU, clamp mode 1 (normal), flag hack on, I-bit hack off. Shadow of Rome has no VU entry in
    // PCSX2's GameIndex. This thread is bound too, so every thread that touches VU1 sees the same register file.
    g_memoria = &memoria;
    lega();
    char errore[256] = {};
    if (!g_api.init(1, 1, 1, 0, &xgkick, nullptr, errore, static_cast<int>(sizeof(errore))))
    {
        std::printf("[sor-vu] PCSX2's VU1 not used (microVU did not start: %s): ours runs\n", errore[0] ? errore : "no reason given");
        std::fflush(stdout);
        g_memoria = nullptr;
        return;
    }
    SorVu1Host ospite;
    if (pcsx2)
    {
        ospite.codeMem = g_api.codeMem();
        ospite.dataMem = g_api.dataMem();
        ospite.run = &eseguiPcsx2;
        ospite.codeWritten = &codiceScritto;
        if (!ospite.codeMem || !ospite.dataMem)
        {
            std::printf("[sor-vu] PCSX2's VU1 not used (sor-vu.dll gave no VU1 memory): ours runs\n");
            std::fflush(stdout);
            g_memoria = nullptr;
            return;
        }
    }
    else
    {
        ospite.compareBefore = &primaDelNostro;
        ospite.compareAfter = &dopoIlNostro;
    }
    sorSetVu1Host(ospite);
    g_pcsx2Riga.store(adessoNs());
    if (pcsx2)
        std::printf("[sor-vu] VU1 engine: PCSX2 microVU (sor-vu.dll version %d; processor: %s; clamp mode normal, flag hack "
                    "on, I-bit hack off)\n", g_api.version(), cpu.c_str());
    else
        std::printf("[sor-vu] VU1 engine: ours, compared with PCSX2 microVU on about 1 program in %u (sor-vu.dll version %d; "
                    "processor: %s). Slower than normal: for checks only\n", g_ogni, g_api.version(), cpu.c_str());
    std::fflush(stdout);
}
