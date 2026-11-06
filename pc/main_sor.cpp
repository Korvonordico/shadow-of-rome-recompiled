// Avvio di Shadow of Rome (prova di fattibilita') con il runtime di PS2Recomp.
// Uso: sor.exe <SLES_529.50> [immagine.iso] [secondi]
// Come il main del runtime, piu':
// - l'immagine del disco: il lettore CD finto legge i settori direttamente dalla
//   ISO originale (sola lettura), senza estrarre i file;
// - un tempo massimo (default 60 s): poi il programma si chiude da solo (per le prove);
// - finestra nascosta, salvo SOR_FINESTRA=1 (per non disturbare chi usa il PC).
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "raylib.h"

// sor: il gioco non deve perdere il processore a favore dei programmi in sottofondo (windows.h non puo'
// stare insieme a raylib.h: qui bastano queste due dichiarazioni).
extern "C" __declspec(dllimport) void *__stdcall GetCurrentProcess();
extern "C" __declspec(dllimport) int __stdcall SetPriorityClass(void *processo, unsigned long classe);
// sor: Windows 11 slows down the timers of a program whose window is hidden, covered or silent: the 1 ms clock
// raylib asks for is then ignored, the game's clock wakes up to 15 ms late and pictures come late (small pauses).
// The game asks not to be slowed down (ProcessPowerThrottling: full speed, its timer resolution always honoured).
extern "C" __declspec(dllimport) int __stdcall SetProcessInformation(void *processo, int classe, void *dati, unsigned long misura);

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
void sorGpuMirrorClose(); // ps2xRuntime gs_pcsx2_mirror.cpp

void sorInstallaSostituzioni(PS2Runtime &rt); // sor_hle.cpp
void sorStampaTraccia();                       // sor_hle.cpp (con SOR_TRACCIA)
void sorProfiloAvvia();                        // sor_profilo.cpp (con SOR_PROFILO)
void sorProfiloScrivi();
void sorAlzaPrioritaThread();                   // sor_prove.cpp
void sorProveAvvia(PS2Runtime *rt);             // sor_prove.cpp (SOR_TEMPI, SOR_TELECOMANDO)
void sorGsRapporto();                          // gs_frontend.cpp (conteggi dei disegni)
void sorMonitorStart();                       // sor_monitor.cpp
void sorMonitorSetRuntime(PS2Runtime *rt);
void sorMonitorInstall(PS2Runtime &rt);
void sorMonitorFatal(const char *reason, const char *message);
bool sorMonitorClosedByUser();

// Widescreen 16:9 (SOR_WIDESCREEN=1, from the launcher): read by the translated camera code
// (pc/widescreen_gen.py), by the window size and by sor-gs.dll.
bool sorWidescreenOn()
{
    static const bool on = [] { const char *e = std::getenv("SOR_WIDESCREEN"); return e && e[0] >= '1' && e[0] <= '3'; }();
    return on;
}

// Widescreen 21:9 (SOR_WIDESCREEN=2): the same constants, wider (pc/widescreen_gen.py).
bool sorUltrawideOn()
{
    static const bool on = [] { const char *e = std::getenv("SOR_WIDESCREEN"); return e && (e[0] == '2' || e[0] == '3'); }();
    return on;
}

// The gameplay camera's widened constants were just read (pc/widescreen_gen.py): counted, so that the GS shows
// the wide picture only while the game is played (gs_pcsx2_mirror.cpp, sorWideCameraRecent).
std::atomic<unsigned> g_sorWideCamera{0};
bool sorWideCamera()
{
    g_sorWideCamera.fetch_add(1, std::memory_order_relaxed);
    return sorWidescreenOn();
}

// A view that stays 4:3 on a wide screen: the films (MPEG.cpp, classMOVIE_TASK), the pause menu and the title
// menus (pc/widescreen_gen.py) call this. The GS shows them 4:3 and the rest wide (gs_pcsx2_mirror.cpp).
std::atomic<unsigned> g_sorFourThree{0};
void sorFourThree()
{
    g_sorFourThree.fetch_add(1, std::memory_order_relaxed);
}

// The camera makes a view (classCAMERA::make_view_mat and make_view_matF, pc/widescreen_gen.py): with no
// sorWideCamera() in it for a quarter of a second, it is one of the game's own scenes, shown 4:3.
std::atomic<unsigned> g_sorCameraAny{0};
void sorCameraView()
{
    g_sorCameraAny.fetch_add(1, std::memory_order_relaxed);
}

// Widescreen 32:9 (SOR_WIDESCREEN=3), for super-wide monitors.
bool sorSuperwideOn()
{
    static const bool on = [] { const char *e = std::getenv("SOR_WIDESCREEN"); return e && e[0] == '3'; }();
    return on;
}

// 60 Hz (SOR_60HZ, from the launcher): the game's video mode byte to start with, 0 = leave it to the game.
int sorVideoMode()
{
    static const int m = [] { const char *e = std::getenv("SOR_60HZ"); return e ? std::atoi(e) : 0; }();
    return m;
}

// Interlacing as on the PS2 (SOR_INTERLACE=1); off by default: the picture no longer shakes half a line.
bool sorInterlaceOn()
{
    static const bool on = [] { const char *e = std::getenv("SOR_INTERLACE"); return e && e[0] == '1'; }();
    return on;
}

namespace
{
    std::atomic<PS2Runtime *> g_runtime{nullptr};

    const char *statoThread(EeThreadStatus s)
    {
        switch (s)
        {
        case EeThreadStatus::Running: return "in corsa";
        case EeThreadStatus::Ready: return "pronto";
        case EeThreadStatus::Waiting: return "in attesa";
        case EeThreadStatus::WaitingSuspended: return "in attesa+sospeso";
        case EeThreadStatus::Suspended: return "sospeso";
        case EeThreadStatus::Dormant: return "dormiente";
        }
        return "?";
    }

    const char *motivoAttesa(EeWaitReason w)
    {
        switch (w)
        {
        case EeWaitReason::None: return "-";
        case EeWaitReason::Sleep: return "sleep";
        case EeWaitReason::Semaphore: return "semaforo";
        case EeWaitReason::EventFlag: return "eventflag";
        case EeWaitReason::VSync: return "vsync";
        case EeWaitReason::External: return "esterno";
        case EeWaitReason::Mpeg: return "mpeg";
        }
        return "?";
    }

    // Rapporto sullo stato: thread del gioco (EE) e processore IOP.
    void rapporto(int t)
    {
        PS2Runtime *rt = g_runtime.load();
        if (!rt)
            return;
        const EeKernelSnapshot ee = rt->eeScheduler().snapshot();
        std::printf("\n[sor] === stato a %d s: ciclo EE %llu, thread in corsa %d ===\n", t,
                    (unsigned long long)ee.eeCycle, ee.runningThreadId);
        for (const auto &th : ee.threads)
            std::printf("[sor] thread %d: %s, attesa %s %d, pc=0x%x ra=0x%x entry=0x%x prio=%d\n", th.id,
                        statoThread(th.status), motivoAttesa(th.waitReason), th.waitId, th.pc, th.ra, th.entry,
                        th.currentPriority);
        for (const auto &s : ee.semaphores)
            std::printf("[sor] semaforo %d: conto %d\n", s.id, s.count);
        std::printf("[sor] grafica: DMA avviati %llu, copie GIF %llu, scritture GS %llu, scritture VIF %llu\n",
                    (unsigned long long)rt->memory().dmaStartCount(), (unsigned long long)rt->memory().gifCopyCount(),
                    (unsigned long long)rt->memory().gsWriteCount(), (unsigned long long)rt->memory().vifWriteCount());
        {
            const GSRegisters &g = rt->memory().gs();
            std::printf("[sor] GS registri: pmode=0x%llx smode2=0x%llx dispfb1=0x%llx display1=0x%llx dispfb2=0x%llx display2=0x%llx bgcolor=0x%llx\n",
                        (unsigned long long)g.pmode, (unsigned long long)g.smode2, (unsigned long long)g.dispfb1,
                        (unsigned long long)g.display1, (unsigned long long)g.dispfb2, (unsigned long long)g.display2,
                        (unsigned long long)g.bgcolor);
            sorGsRapporto();
        }
        const auto iop = rt->iopDebugSnapshot();
        std::printf("[sor] IOP: cicli %llu, istruzioni %llu, moduli caricati %u, thread %u, server RPC %u\n",
                    (unsigned long long)iop.emulatorCycles, (unsigned long long)iop.emulatorInstructions,
                    iop.emulatorLoadedModules, iop.emulatorThreads, iop.emulatorRpcServers);
        for (const auto &d : iop.diagnostics)
            std::printf("[sor] IOP diagnostica: %s\n", d.c_str());
        std::fflush(stdout);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "uso: sor.exe <SLES_529.50> [immagine.iso] [secondi]" << std::endl;
        return 2;
    }
    sorMonitorStart();
    sorProfiloAvvia();
    // Solo il thread del gioco (vedi ps2_runtime.cpp) e questo, che mostra le immagini, stanno sopra il normale:
    // con tutto il processo sopra il normale gli altri programmi del PC restavano fermi anche per decine di secondi.
    // SOR_PRIORITA_ALTA=1: tutto il processo sopra il normale, per misurare senza il disturbo di altri programmi.
    sorAlzaPrioritaThread();
    if (std::getenv("SOR_PRIORITA_ALTA"))
        SetPriorityClass(GetCurrentProcess(), 0x00008000ul); // ABOVE_NORMAL_PRIORITY_CLASS
    {
        // Not slowed down by Windows when hidden or silent (see SetProcessInformation above): Version 1,
        // ControlMask EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION, StateMask 0 = neither of the two.
        unsigned long stato[3] = {1ul, 0x1ul | 0x4ul, 0ul};
        SetProcessInformation(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, stato, sizeof(stato));
    }
    const int secondi = argc >= 4 ? std::atoi(argv[3]) : 60;
    if (secondi > 0)
    {
        std::thread([secondi]()
                    {
                        for (int t = 10; t < secondi; t += 10)
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(10));
                            rapporto(t);
                        }
                        std::this_thread::sleep_for(std::chrono::seconds(secondi % 10 ? secondi % 10 : 10));
                        rapporto(secondi);
                        sorProfiloScrivi();
                        sorStampaTraccia();
                        std::cout << "\n[sor] tempo massimo di " << secondi << " s raggiunto: chiudo" << std::endl;
                        std::cout.flush();
                        std::cerr.flush();
                        sorGpuMirrorClose();
                        std::_Exit(3); })
            .detach();
    }
    const char *finestra = std::getenv("SOR_FINESTRA");
    if (!finestra || std::string(finestra) != "1")
    {
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
    }
    try
    {
        PS2Runtime runtime;
        g_runtime.store(&runtime);
        sorMonitorSetRuntime(&runtime);
        sorProveAvvia(&runtime);
        if (!runtime.initialize("Shadow of Rome"))
        {
            std::cerr << "[sor] runtime non inizializzato" << std::endl;
            sorMonitorFatal("fatal", "the runtime did not start");
            return 1;
        }
        // Esc e' il tasto cerchio (ps2_pad.cpp): non deve chiudere la finestra.
        SetExitKey(KEY_NULL);
        if (!runtime.loadELF(argv[1]))
        {
            std::cerr << "[sor] ELF non caricato: " << argv[1] << std::endl;
            sorMonitorFatal("fatal", "the game file did not load");
            return 1;
        }
        // Il gioco tiene lo stack del thread principale in 0x1FF8000..0x2000000 e altri
        // stack subito sotto: gli stack delle callback del runtime (vsync, allarmi, ...)
        // partirebbero dalla cima della RAM e lo sovrascriverebbero. Li mettiamo in
        // 0x80000..0x100000 (memoria del kernel PS2, che il gioco non usa).
        runtime.setAsyncCallbackStackRegion(0x00080000u, 0x00100000u);
        // Il heap del runtime sta nella memoria del kernel PS2 (0x10000..0x80000): SetupHeap
        // del gioco lo metterebbe subito dopo il programma, dove il gioco tiene i suoi dati.
        runtime.pinGuestHeap(0x00010000u, 0x00080000u);
        sorInstallaSostituzioni(runtime);
        sorMonitorInstall(runtime);
        if (argc >= 3 && argv[2][0] != '\0')
        {
            PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
            paths.cdImage = std::filesystem::path(argv[2]);
            PS2Runtime::setIoPaths(paths);
            std::cout << "[sor] immagine del disco: " << PS2Runtime::getIoPaths().cdImage.string() << std::endl;
        }
        runtime.run();
        std::cout << "[sor] run() finito" << std::endl;
        if (!sorMonitorClosedByUser())
            sorMonitorFatal("stopped", "the game stopped by itself, the window was not closed");
        rapporto(-1);
        sorStampaTraccia();
        std::cout.flush();
        std::cerr.flush();
        sorGpuMirrorClose();
        std::_Exit(0);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[sor] eccezione: " << e.what() << std::endl;
        sorMonitorFatal("fatal", e.what());
    }
    catch (...)
    {
        std::cerr << "[sor] eccezione sconosciuta" << std::endl;
        sorMonitorFatal("fatal", "unknown C++ exception");
    }
    std::cout.flush();
    std::cerr.flush();
    sorGpuMirrorClose();
    std::_Exit(1);
}
