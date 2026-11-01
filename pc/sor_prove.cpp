// Strumenti per le prove (spenti se le variabili non ci sono).
// SOR_TEMPI=1: ogni secondo scrive nel registro velocita' e consumo di ogni thread.
// SOR_TELECOMANDO=<file>: il programma legge i comandi aggiunti in fondo al file (una riga ognuno):
//   k <tasto> <ms>   tiene premuto un tasto al livello dove si legge la tastiera (ps2_pad.cpp),
//                    senza toccare la tastiera vera del PC
//   foto             salva il prossimo fotogramma (serve SOR_FOTO)
//   prof0, prof1     azzera il profilo / lo scrive (serve SOR_PROFILO)
//   esci             chiude il programma
// Con il telecomando acceso tastiera e controller veri non contano.
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

extern std::atomic<uint64_t> g_sorContaPad;     // Stubs/Pad.cpp
extern std::atomic<uint64_t> g_sorContaPresent; // ps2_runtime.cpp
extern std::atomic<bool> g_sorFotoSubito;       // ps2_runtime.cpp
extern uint64_t g_sorGsTipi[10], g_sorGsTipiUs[10], g_sorTrasferimenti[2], g_sorEeFermoUs, g_sorEeRitmoUs;
extern uint64_t g_sorBandaAttesa[8], g_sorBandaVuoto[8], g_sorBandaPixel[8];
extern uint64_t g_sorVu1Cicli, g_sorVu1Run, g_sorVu1Lenti, g_sorGsPrim, g_sorGsBarriere, g_sorGsSvuota, g_sorGsAttesaUs, g_sorGsVblankUs, g_sorMpg;
// Ogni thread preferisce un core suo: il sistema li sposta meno (arena da 48 a 49,9 immagini al secondo).
void sorThreadIdeale(int core)
{
    SetThreadIdealProcessor(GetCurrentThread(), static_cast<DWORD>(core));
}
void sorAlzaPrioritaThread()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
}
void sorProfiloAzzera();                        // sor_profilo.cpp
void sorProfiloScrivi();

namespace
{
    struct Tasto
    {
        const char *nome;
        int codice;
    };
    // Codici dei tasti di raylib (raylib.h non puo' stare insieme a windows.h).
    const Tasto kTasti[] = {
        {"su", 265}, {"giu", 264}, {"sx", 263}, {"dx", 262}, {"w", 87}, {"a", 65}, {"s", 83}, {"d", 68},
        {"i", 73}, {"j", 74}, {"k", 75}, {"l", 76}, {"x", 88}, {"spazio", 32}, {"c", 67}, {"esc", 256},
        {"z", 90}, {"v", 86}, {"q", 81}, {"e", 69}, {"lshift", 340}, {"rshift", 344},
        {"invio", 257}, {"tab", 258}, {"f", 70}, {"o", 79}};

    PS2Runtime *g_rt = nullptr;
    std::atomic<bool> g_telecomando{false};
    std::mutex g_mutex;
    std::map<int, uint64_t> g_premuti; // fino a quale lettura del pad (tempo del gioco, non dell'orologio)

    void leggiComandi(const std::string &file)
    {
        long pos = 0;
        std::string resto;
        for (;;)
        {
            if (FILE *f = std::fopen(file.c_str(), "rb"))
            {
                std::fseek(f, pos, SEEK_SET);
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
                {
                    pos += static_cast<long>(n);
                    resto.append(buf, n);
                }
                std::fclose(f);
            }
            size_t a;
            while ((a = resto.find('\n')) != std::string::npos)
            {
                const std::string riga = resto.substr(0, a);
                resto.erase(0, a + 1);
                char cmd[32] = {}, nome[32] = {};
                int ms = 400;
                const int campi = std::sscanf(riga.c_str(), "%31s %31s %d", cmd, nome, &ms);
                if (campi >= 2 && !std::strcmp(cmd, "k"))
                {
                    bool trovato = false;
                    for (const Tasto &t : kTasti)
                        if (!std::strcmp(t.nome, nome))
                        {
                            std::lock_guard<std::mutex> lock(g_mutex);
                            g_premuti[t.codice] = g_sorContaPad.load(std::memory_order_relaxed) + static_cast<uint64_t>(std::max(ms, 20)) / 20u;
                            trovato = true;
                        }
                    std::printf("[sor] telecomando: tasto %s %d ms%s\n", nome, ms, trovato ? "" : " SCONOSCIUTO");
                    std::fflush(stdout);
                }
                else if (campi >= 2 && !std::strcmp(cmd, "ipeek"))
                {
                    // ipeek <ind> [quanti]: memoria dell'IOP, in esadecimale.
                    unsigned ind = 0, val = 1;
                    std::sscanf(riga.c_str(), "%*s %x %x", &ind, &val);
                    uint32_t p[64] = {};
                    if (g_rt && g_rt->readIopMemory(ind & ~3u, p, sizeof(p)))
                    {
                        std::printf("[ipeek] %08x:", ind);
                        for (unsigned i = 0; i < val && i < 64; ++i)
                            std::printf(" %08x", p[i]);
                        std::printf("%c", 10);
                        std::fflush(stdout);
                    }
                }
                else if (campi >= 2 && (!std::strcmp(cmd, "peek") || !std::strcmp(cmd, "poke") || !std::strcmp(cmd, "or32")))
                {
                    // peek <ind> [quanti] / poke <ind> <val> / or32 <ind> <val>: memoria del gioco, in esadecimale (solo per le prove).
                    unsigned ind = 0, val = 1;
                    std::sscanf(riga.c_str(), "%*s %x %x", &ind, &val);
                    if (g_rt && ind + 4 <= 0x2000000u)
                    {
                        uint32_t *p = reinterpret_cast<uint32_t *>(g_rt->memory().getRDRAM() + (ind & ~3u));
                        if (!std::strcmp(cmd, "poke"))
                            *p = val;
                        else if (!std::strcmp(cmd, "or32"))
                            *p |= val;
                        else
                        {
                            std::printf("[peek] %08x:", ind);
                            for (unsigned i = 0; i < val && i < 64 && (ind & ~3u) + (i + 1) * 4 <= 0x2000000u; ++i)
                                std::printf(" %08x", p[i]);
                            std::printf("\n");
                            std::fflush(stdout);
                        }
                    }
                }
                else if (campi >= 1 && !std::strcmp(cmd, "foto"))
                {
                    g_sorFotoSubito.store(true);
                }
                else if (campi >= 1 && !std::strcmp(cmd, "prof0"))
                {
                    sorProfiloAzzera();
                }
                else if (campi >= 1 && !std::strcmp(cmd, "prof1"))
                {
                    sorProfiloScrivi();
                }
                else if (campi >= 1 && !std::strcmp(cmd, "esci"))
                {
                    std::printf("[sor] telecomando: esci\n");
                    std::fflush(stdout);
                    std::_Exit(0);
                }
            }
            Sleep(10);
        }
    }

    uint64_t tempoThread(DWORD id)
    {
        HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, id);
        if (!h)
            return 0;
        FILETIME a, b, k, u;
        uint64_t t = 0;
        if (GetThreadTimes(h, &a, &b, &k, &u))
            t = ((static_cast<uint64_t>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
                ((static_cast<uint64_t>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
        CloseHandle(h);
        return t; // unita' da 100 ns
    }

    void giroTempi(PS2Runtime **rtPtr)
    {
        std::map<DWORD, uint64_t> prima;
        uint64_t ultimoDispfb = 0, flip = 0;
        uint64_t vb0 = 0, ee0 = 0, pad0 = 0, pr0 = 0, fl0 = 0;
        auto t0 = std::chrono::steady_clock::now();
        auto inizio = t0;
        const DWORD pid = GetCurrentProcessId();
        for (;;)
        {
            Sleep(1);
            PS2Runtime *rt = *rtPtr;
            if (!rt)
                continue;
            const uint64_t d = rt->memory().gs().dispfb1;
            if (d != ultimoDispfb)
            {
                ultimoDispfb = d;
                ++flip;
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(t1 - t0).count();
            if (dt < 1.0)
                continue;
            t0 = t1;
            const uint64_t vb = rt->eeScheduler().currentVSyncTick();
            const uint64_t ee = rt->eeScheduler().snapshot().eeCycle;
            const uint64_t pad = g_sorContaPad.load();
            const uint64_t pr = g_sorContaPresent.load();
            static uint64_t c0[9] = {};
            const uint64_t c1[9] = {g_sorVu1Cicli, g_sorVu1Run, g_sorMpg, g_sorGsPrim, g_sorGsBarriere, g_sorVu1Lenti, g_sorGsSvuota, g_sorGsAttesaUs, g_sorGsVblankUs};
            const double nvb = (vb > vb0) ? static_cast<double>(vb - vb0) : 1.0;
            std::printf("[tempi] t=%.0f vblank/s=%.1f cicliEE(M)/s=%.0f pad/s=%.1f presentati/s=%.1f ogni-vblank: vu1cicli(k)=%.0f vu1run=%.0f lenti=%.0f mpg=%.0f prim(k)=%.1f barriere=%.0f svuota=%.1f attesaMs/s=%.0f vblankMs/s=%.0f cpu:",
                        std::chrono::duration<double>(t1 - inizio).count(), (vb - vb0) / dt, (ee - ee0) / dt / 1e6, (pad - pad0) / dt,
                        (pr - pr0) / dt, (c1[0] - c0[0]) / nvb / 1e3, (c1[1] - c0[1]) / nvb, (c1[5] - c0[5]) / nvb, (c1[2] - c0[2]) / nvb, (c1[3] - c0[3]) / nvb / 1e3, (c1[4] - c0[4]) / nvb, (c1[6] - c0[6]) / nvb, (c1[7] - c0[7]) / dt / 1e3, (c1[8] - c0[8]) / dt / 1e3);
            std::memcpy(c0, c1, sizeof(c0));
            vb0 = vb;
            ee0 = ee;
            pad0 = pad;
            pr0 = pr;
            fl0 = flip;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap != INVALID_HANDLE_VALUE)
            {
                THREADENTRY32 te{};
                te.dwSize = sizeof(te);
                if (Thread32First(snap, &te))
                    do
                    {
                        if (te.th32OwnerProcessID != pid)
                            continue;
                        const uint64_t c = tempoThread(te.th32ThreadID);
                        const double perc = (c - prima[te.th32ThreadID]) / 1e7 / dt * 100.0;
                        prima[te.th32ThreadID] = c;
                        if (perc >= 3.0)
                            std::printf(" %lu=%.0f%%", static_cast<unsigned long>(te.th32ThreadID), perc);
                    } while (Thread32Next(snap, &te));
                CloseHandle(snap);
            }
            std::printf("\n");
            static uint64_t t0[10] = {}, u0[10] = {};
            static uint64_t ba0[8] = {}, bv0[8] = {}, bp0[8] = {};
            std::printf("[ban]");
            for (int i = 0; i < 4; ++i)
            {
                std::printf(" %d: barriera %.0f vuoto %.0f pixel(M) %.2f |", i, (g_sorBandaAttesa[i] - ba0[i]) / dt / 1e3, (g_sorBandaVuoto[i] - bv0[i]) / dt / 1e3, (g_sorBandaPixel[i] - bp0[i]) / dt / 1e6);
                ba0[i] = g_sorBandaAttesa[i];
                bv0[i] = g_sorBandaVuoto[i];
                bp0[i] = g_sorBandaPixel[i];
            }
            std::printf("\n");
            static uint64_t tr0 = 0;
            static uint64_t fe0 = 0, ri0 = 0;
            std::printf("[bar] fermoMs/s=%.0f ritmoMs/s=%.0f scambiThread/s=%.0f", (g_sorEeFermoUs - fe0) / dt / 1e3, (g_sorEeRitmoUs - ri0) / dt / 1e3, (g_sorTrasferimenti[0] - tr0) / dt);
            fe0 = g_sorEeFermoUs;
            ri0 = g_sorEeRitmoUs;
            tr0 = g_sorTrasferimenti[0];
            for (int i = 0; i < 10; ++i)
            {
                std::printf(" %d:%.1f/%.1fms", i, (g_sorGsTipi[i] - t0[i]) / nvb, (g_sorGsTipiUs[i] - u0[i]) / nvb / 1e3);
                t0[i] = g_sorGsTipi[i];
                u0[i] = g_sorGsTipiUs[i];
            }
            std::printf("\n");
            std::fflush(stdout);
        }
    }

    // SOR_TRAPPOLA=<indirizzo hex>,<secondi>: dopo quei secondi la pagina di RAM con quell'indirizzo diventa di sola
    // lettura. Ogni scrittura della pagina passa con un passo solo; se quella parola e' cambiata si stampa chi e' stato
    // (dove stava il programma: si legge con sor.map), al massimo 40 volte.
    uint8_t *g_trappolaDa = nullptr;
    uint8_t *g_trappolaParola = nullptr;
    uint32_t g_trappolaValore = 0;
    int g_trappolaVolte = 0;
    thread_local bool t_trappolaPasso = false;
    LONG WINAPI suEccezione(EXCEPTION_POINTERS *e)
    {
        DWORD vecchia;
        if (e->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && t_trappolaPasso)
        {
            t_trappolaPasso = false;
            uint32_t v;
            std::memcpy(&v, g_trappolaParola, 4);
            if (v != g_trappolaValore && g_trappolaVolte < 40)
            {
                ++g_trappolaVolte;
                const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
                void *pila[24] = {};
                const USHORT n = RtlCaptureStackBackTrace(0, 24, pila, nullptr);
                std::printf("[trappola] 0x%x -> 0x%x, thread %lu, rip=+0x%llx pila:", g_trappolaValore, v, GetCurrentThreadId(), static_cast<unsigned long long>(static_cast<uintptr_t>(e->ContextRecord->Rip) - base));
                for (USHORT i = 0; i < n; ++i)
                    std::printf(" +0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pila[i]) - base));
                std::printf("\n");
                std::fflush(stdout);
            }
            g_trappolaValore = v;
            VirtualProtect(g_trappolaDa, 4096, PAGE_READONLY, &vecchia);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_trappolaDa)
            return EXCEPTION_CONTINUE_SEARCH;
        const uint8_t *a = reinterpret_cast<const uint8_t *>(e->ExceptionRecord->ExceptionInformation[1]);
        if (a < g_trappolaDa || a >= g_trappolaDa + 4096)
            return EXCEPTION_CONTINUE_SEARCH;
        VirtualProtect(g_trappolaDa, 4096, PAGE_READWRITE, &vecchia);
        e->ContextRecord->EFlags |= 0x100;
        t_trappolaPasso = true;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    void giroTrappola(unsigned long indirizzo, int secondi)
    {
        Sleep(static_cast<DWORD>(secondi) * 1000u);
        while (!g_rt)
            Sleep(100);
        uint8_t *pagina = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(g_rt->memory().getRDRAM() + indirizzo) & ~static_cast<uintptr_t>(4095));
        AddVectoredExceptionHandler(1, suEccezione);
        g_trappolaParola = g_rt->memory().getRDRAM() + indirizzo;
        std::memcpy(&g_trappolaValore, g_trappolaParola, 4);
        g_trappolaDa = pagina;
        DWORD vecchia;
        const BOOL ok = VirtualProtect(pagina, 4096, PAGE_READONLY, &vecchia);
        std::printf("[trappola] armata su RAM+0x%lx: %d\n", indirizzo, ok ? 1 : 0);
        std::fflush(stdout);
    }
}

// Chiamata da ps2_pad.cpp: vero se il telecomando tiene premuto quel tasto.
bool sorTelecomandoAcceso() { return g_telecomando.load(); }
bool sorTastoIniettato(int codice)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_premuti.find(codice);
    return it != g_premuti.end() && g_sorContaPad.load(std::memory_order_relaxed) < it->second;
}

void sorProveAvvia(PS2Runtime *rt)
{
    g_rt = rt;
    if (std::getenv("SOR_TEMPI"))
        std::thread(giroTempi, &g_rt).detach();
    if (const char *t = std::getenv("SOR_TRAPPOLA"))
    {
        unsigned long indirizzo = 0;
        int secondi = 20;
        std::sscanf(t, "%lx,%d", &indirizzo, &secondi);
        std::thread(giroTrappola, indirizzo, secondi).detach();
    }
    if (const char *f = std::getenv("SOR_TELECOMANDO"))
    {
        g_telecomando.store(true);
        std::thread(leggiComandi, std::string(f)).detach();
    }
}
