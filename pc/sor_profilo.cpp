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

    void giro()
    {
        const DWORD io = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();
        while (g_acceso.load())
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap != INVALID_HANDLE_VALUE)
            {
                THREADENTRY32 te{};
                te.dwSize = sizeof(te);
                if (Thread32First(snap, &te))
                {
                    do
                    {
                        if (te.th32OwnerProcessID != pid || te.th32ThreadID == io)
                            continue;
                        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                        if (!h)
                            continue;
                        if (SuspendThread(h) != (DWORD)-1)
                        {
                            CONTEXT c{};
                            c.ContextFlags = CONTEXT_CONTROL;
                            if (GetThreadContext(h, &c))
                            {
                                std::lock_guard<std::mutex> lock(g_mutex);
                                ++g_campioni[te.th32ThreadID][c.Rip];
                            }
                            ResumeThread(h);
                        }
                        CloseHandle(h);
                    } while (Thread32Next(snap, &te));
                }
                CloseHandle(snap);
            }
            Sleep(2);
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
    g_acceso.store(false);
    Sleep(10);
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
