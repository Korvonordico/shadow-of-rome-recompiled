// Profilo a campioni (solo per le prove): con SOR_PROFILO=<file> un thread ferma per un
// istante tutti gli altri ogni 2 ms e annota dove sono. sorProfiloScrivi() scrive il file;
// strumenti/profilo.py lo legge insieme a _build/sor.map.
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>

namespace
{
    std::mutex g_mutex;
    std::map<DWORD, std::map<uint64_t, uint32_t>> g_campioni;
    std::atomic<bool> g_acceso{false};

    struct Bersaglio
    {
        DWORD id;
        HANDLE h;
    };

    void giro()
    {
        const DWORD io = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();
        std::map<DWORD, HANDLE> bersagli;
        DWORD ultimoElenco = 0;
        while (g_acceso.load())
        {
            // L'istantanea di sistema e' lenta: l'elenco dei thread si aggiorna una volta al secondo.
            if (GetTickCount() - ultimoElenco > 1000)
            {
                ultimoElenco = GetTickCount();
                HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snap != INVALID_HANDLE_VALUE)
                {
                    THREADENTRY32 te{};
                    te.dwSize = sizeof(te);
                    if (Thread32First(snap, &te))
                    {
                        do
                        {
                            if (te.th32OwnerProcessID != pid || te.th32ThreadID == io || bersagli.count(te.th32ThreadID))
                                continue;
                            if (HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID))
                                bersagli[te.th32ThreadID] = h;
                        } while (Thread32Next(snap, &te));
                    }
                    CloseHandle(snap);
                }
            }
            for (const auto &b : bersagli)
            {
                if (SuspendThread(b.second) != (DWORD)-1)
                {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_CONTROL;
                    const bool ok = GetThreadContext(b.second, &c) != 0;
                    ResumeThread(b.second);
                    // Solo dopo aver fatto ripartire il thread: se e' fermo mentre tiene il blocco
                    // della memoria, allocare qui bloccherebbe tutto il programma.
                    if (ok)
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        ++g_campioni[b.first][c.Rip];
                    }
                }
            }
            Sleep(1);
        }
    }
}

void sorProfiloAvvia()
{
    if (!std::getenv("SOR_PROFILO"))
        return;
    g_acceso.store(true);
    std::thread(giro).detach();
}

void sorProfiloScrivi()
{
    const char *nome = std::getenv("SOR_PROFILO");
    if (!nome)
        return;
    FILE *f = std::fopen(nome, "w");
    if (!f)
        return;
    std::fprintf(f, "base %llx\n", (unsigned long long)(uintptr_t)GetModuleHandleA(nullptr));
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto &t : g_campioni)
    {
        std::fprintf(f, "thread %lu\n", (unsigned long)t.first);
        for (const auto &c : t.second)
            std::fprintf(f, "%llx %u\n", (unsigned long long)c.first, c.second);
    }
    std::fclose(f);
}

// Comando "prof0" del telecomando: ricomincia a contare da adesso.
void sorProfiloAzzera()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_campioni.clear();
}

// Full screen (SOR_FULLSCREEN=1 and F11 / Alt+Enter, called from ps2_runtime.cpp): a borderless window over the
// whole monitor, made one pixel taller than the screen. A window exactly the size of the screen is taken by the
// driver as exclusive full screen and covers sor-gs.dll's picture (black screen); one pixel more avoids that.
void sorSchermoIntero(void *finestra)
{
    static bool pieno = false;
    static LONG_PTR stileVecchio = 0;
    static RECT vecchio = {};
    HWND h = static_cast<HWND>(finestra);
    if (!h)
        return;
    if (!pieno)
    {
        stileVecchio = GetWindowLongPtrW(h, GWL_STYLE);
        GetWindowRect(h, &vecchio);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT &m = mi.rcMonitor;
        SetWindowLongPtrW(h, GWL_STYLE, (stileVecchio & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top + 1, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        SetWindowLongPtrW(h, GWL_STYLE, stileVecchio);
        SetWindowPos(h, nullptr, vecchio.left, vecchio.top, vecchio.right - vecchio.left, vecchio.bottom - vecchio.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    }
    pieno = !pieno;
}
