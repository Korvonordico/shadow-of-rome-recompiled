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

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

void sorInstallaSostituzioni(PS2Runtime &rt); // sor_hle.cpp
void sorStampaTraccia();                       // sor_hle.cpp (con SOR_TRACCIA)
void sorProfiloAvvia();                        // sor_profilo.cpp (con SOR_PROFILO)
void sorProfiloScrivi();
void sorGsRapporto();                          // gs_frontend.cpp (conteggi dei disegni)

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
    sorProfiloAvvia();
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
        if (!runtime.initialize("Shadow of Rome"))
        {
            std::cerr << "[sor] runtime non inizializzato" << std::endl;
            return 1;
        }
        // Esc e' il tasto cerchio (ps2_pad.cpp): non deve chiudere la finestra.
        SetExitKey(KEY_NULL);
        if (!runtime.loadELF(argv[1]))
        {
            std::cerr << "[sor] ELF non caricato: " << argv[1] << std::endl;
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
        if (argc >= 3 && argv[2][0] != '\0')
        {
            PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
            paths.cdImage = std::filesystem::path(argv[2]);
            PS2Runtime::setIoPaths(paths);
            std::cout << "[sor] immagine del disco: " << PS2Runtime::getIoPaths().cdImage.string() << std::endl;
        }
        runtime.run();
        std::cout << "[sor] run() finito" << std::endl;
        rapporto(-1);
        sorStampaTraccia();
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(0);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[sor] eccezione: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[sor] eccezione sconosciuta" << std::endl;
    }
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(1);
}
