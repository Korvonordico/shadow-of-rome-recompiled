// Profilo a campioni (solo per le prove): con SOR_PROFILO=<file> un thread ferma per un
// istante tutti gli altri ogni 2 ms e annota dove sono. sorProfiloScrivi() scrive il file;
// strumenti/profilo.py lo legge insieme a _build/sor.map.
// Light mode (10/11/2026, with SOR_TEMPI and no SOR_PROFILO: launcher SPEED TESTS > TIMINGS): only the 3 threads that
// used the most CPU in the last second (at least 25%), stopped for an instant every 2 ms, and the profile written by
// itself every 10 s to logs/profile-last.txt, with the loaded modules (code in the DLLs) at the top. To find what
// the game's busiest thread (about 98% of a core in heavy scenes) spends its time on.
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

void scriviSu(const char *nome);
namespace
{
    std::mutex g_mutex;
    std::map<DWORD, std::map<uint64_t, uint32_t>> g_campioni;
    // light mode: a sample outside sor.exe (Windows, the C library, a DLL) is also counted at the first return
    // address into sor.exe found on its stack, to see which part of the game was waiting or calling there.
    std::map<DWORD, std::map<uint64_t, uint32_t>> g_chiamanti;
    std::atomic<bool> g_acceso{false};
    bool g_leggero = false;

    uint64_t cpuThread(HANDLE h)
    {
        FILETIME a, b, k, u;
        if (!GetThreadTimes(h, &a, &b, &k, &u))
            return 0;
        return ((static_cast<uint64_t>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
               ((static_cast<uint64_t>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
    }

    struct Bersaglio
    {
        DWORD id;
        HANDLE h;
    };

    uint64_t g_exeDa = 0, g_exeA = 0; // sor.exe in memory
    void giro()
    {
        {
            const auto *base = reinterpret_cast<const uint8_t *>(GetModuleHandleA(nullptr));
            const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
            const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
            g_exeDa = reinterpret_cast<uint64_t>(base);
            g_exeA = g_exeDa + nt->OptionalHeader.SizeOfImage;
        }
        const DWORD io = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();
        std::map<DWORD, HANDLE> bersagli, tutti;
        std::map<DWORD, uint64_t> cpuPrima;
        DWORD ultimoElenco = 0, ultimoScritto = GetTickCount();
        if (g_leggero)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
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
                            if (te.th32OwnerProcessID != pid || te.th32ThreadID == io || tutti.count(te.th32ThreadID))
                                continue;
                            if (HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID))
                                tutti[te.th32ThreadID] = h;
                        } while (Thread32Next(snap, &te));
                    }
                    CloseHandle(snap);
                }
                if (!g_leggero)
                    bersagli = tutti;
                else
                {
                    // the 3 threads that used the most CPU in the last second, at least 25% of a core
                    std::multimap<uint64_t, DWORD, std::greater<uint64_t>> uso;
                    for (const auto &b : tutti)
                    {
                        const uint64_t c = cpuThread(b.second);
                        const auto p = cpuPrima.find(b.first);
                        if (p != cpuPrima.end() && c > p->second)
                            uso.emplace(c - p->second, b.first);
                        cpuPrima[b.first] = c;
                    }
                    bersagli.clear();
                    for (const auto &u : uso)
                    {
                        if (bersagli.size() >= 3 || u.first < 2500000) // 100 ns units: 250 ms in a second
                            break;
                        bersagli[u.second] = tutti[u.second];
                    }
                }
            }
            if (g_leggero && GetTickCount() - ultimoScritto > 10000)
            {
                ultimoScritto = GetTickCount();
                scriviSu("logs/profile-last.txt");
            }
            for (const auto &b : bersagli)
            {
                if (SuspendThread(b.second) != (DWORD)-1)
                {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_CONTROL;
                    const bool ok = GetThreadContext(b.second, &c) != 0;
                    // While it is stopped: only a raw read of its stack (no loader, no allocation).
                    uint64_t chiamante = 0;
                    if (ok && g_leggero && (c.Rip < g_exeDa || c.Rip >= g_exeA))
                    {
                        uint64_t pila[256];
                        SIZE_T letti = 0;
                        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(c.Rsp), pila, sizeof(pila), &letti);
                        for (SIZE_T i = 0; i < letti / 8; ++i)
                            if (pila[i] >= g_exeDa && pila[i] < g_exeA)
                            {
                                chiamante = pila[i];
                                break;
                            }
                    }
                    ResumeThread(b.second);
                    // Solo dopo aver fatto ripartire il thread: se e' fermo mentre tiene il blocco
                    // della memoria, allocare qui bloccherebbe tutto il programma.
                    if (ok)
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        ++g_campioni[b.first][c.Rip];
                        if (chiamante)
                            ++g_chiamanti[b.first][chiamante];
                    }
                }
            }
            Sleep(g_leggero ? 2 : 1);
        }
    }
}

void sorProfiloAvvia()
{
    if (!std::getenv("SOR_PROFILO"))
    {
        if (!std::getenv("SOR_TEMPI"))
            return;
        g_leggero = true;
    }
    g_acceso.store(true);
    std::thread(giro).detach();
}

void sorProfiloScrivi()
{
    if (const char *nome = std::getenv("SOR_PROFILO"))
        scriviSu(nome);
}
void scriviSu(const char *nome)
{
    FILE *f = std::fopen(nome, "w");
    if (!f)
        return;
    std::fprintf(f, "base %llx\n", (unsigned long long)(uintptr_t)GetModuleHandleA(nullptr));
    // the loaded modules: code of the DLLs (sor-gs.dll, sor-vu.dll...) is read with their base and size
    HANDLE snapM = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snapM != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(snapM, &me))
            do
                std::fprintf(f, "module %ls %llx %lx\n", me.szModule, (unsigned long long)(uintptr_t)me.modBaseAddr, (unsigned long)me.modBaseSize);
            while (Module32NextW(snapM, &me));
        CloseHandle(snapM);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto &t : g_campioni)
    {
        std::fprintf(f, "thread %lu\n", (unsigned long)t.first);
        for (const auto &c : t.second)
            std::fprintf(f, "%llx %u\n", (unsigned long long)c.first, c.second);
    }
    for (const auto &t : g_chiamanti)
    {
        std::fprintf(f, "thread %lu-outside-sor.exe-by-caller\n", (unsigned long)t.first);
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
    g_chiamanti.clear();
}

// Engine panel (SOR_DEBUG_PANEL=1, F12; pc/main_sor.cpp): with the video card picture (graphics=gpu) sor-gs.dll draws
// in a child window that covers the whole game window, and the panel (drawn by raylib in the game window) would stay
// hidden under it. While the panel is open that child window is hidden (the picture comes back when it closes).
// ShowWindowAsync: the child window belongs to sor-gs.dll's own window thread, which handles its messages.
extern "C" void *GetWindowHandle(void); // raylib
static std::atomic<bool> g_sorPannello{false};
// pc/sor_mouse.cpp: the pointer is never taken while the engine panel is open.
bool sorPannelloAperto() { return g_sorPannello.load(); }
static const wchar_t *const kFinestraGs = L"Shadow of Rome - video card (PCSX2 GS)";
void sorPannelloSopraGs(bool pannelloAperto)
{
    g_sorPannello.store(pannelloAperto);
    HWND padre = static_cast<HWND>(GetWindowHandle());
    if (!padre)
        return;
    HWND gs = FindWindowExW(padre, nullptr, nullptr, kFinestraGs);
    if (!gs)
        return; // picture drawn by the processor (graphics=cpu): nothing covers the panel
    ShowWindowAsync(gs, pannelloAperto ? SW_HIDE : SW_SHOWNA);
}

// The game window got the focus back (Alt+Tab, or restored from the taskbar; called by sorTurnoFinestra in
// pc/sor_mouse.cpp). the author, 9/11: with the video card the picture was gone after Alt+Tab until the window was
// moved. Not proved from the code (see LAVORO-SHADOW-OF-ROME.md, 09/11, mouse): the video card's child window (sor-gs.dll)
// is drawn by Windows over the game's OpenGL window, and moving the window makes Windows put the two together again.
// Here the same is done without moving anything, in the two ways known to work:
// - the child window hidden and shown again (what F12 does with the engine panel, tested by the author);
// - fitted again to the game window: after a minimize sor-gs.dll leaves it at 0x0 (SorGS.cpp ResizeIfNeeded follows
//   the game window's client size, which is 0x0 while minimized, and after the restore the size is the same as before
//   the minimize, so it is never fitted again).
// Async calls: the child window belongs to sor-gs.dll's own window thread, which handles its messages.
void sorGsRiprendi()
{
    HWND padre = static_cast<HWND>(GetWindowHandle());
    if (!padre)
        return;
    HWND gs = FindWindowExW(padre, nullptr, nullptr, kFinestraGs);
    if (!gs)
    {
        std::printf("[sor] focus back: picture drawn by the processor, nothing to restore\n");
        std::fflush(stdout);
        return;
    }
    if (g_sorPannello.load())
    {
        std::printf("[sor] focus back: the engine panel is open, the video card's picture comes back when it closes\n");
        std::fflush(stdout);
        return;
    }
    RECT rc = {};
    GetClientRect(padre, &rc);
    ShowWindowAsync(gs, SW_HIDE);
    ShowWindowAsync(gs, SW_SHOWNA);
    if (rc.right > 0 && rc.bottom > 0)
        SetWindowPos(gs, nullptr, 0, 0, rc.right, rc.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    RedrawWindow(padre, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
    std::printf("[sor] focus back: picture restored (video card window shown again, %ldx%ld)\n",
                static_cast<long>(rc.right), static_cast<long>(rc.bottom));
    std::fflush(stdout);
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

// sor: the window icon = the launcher's icon (the gold coin). Read from ShadowOfRome.exe next to sor.exe, so
// there is no extra file; if the launcher is not there the window keeps the default icon.
static BOOL CALLBACK sorPrimaIcona(HMODULE, LPCWSTR, LPWSTR nome, LONG_PTR dove)
{
    *(LPWSTR *)dove = nome;
    return FALSE;
}
void sorIcona(void *finestra)
{
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t *b = wcsrchr(p, (wchar_t)92); // the last backslash
    if (!b || (b - p) + 18 >= MAX_PATH) return;
    wcscpy(b + 1, L"ShadowOfRome.exe");
    HMODULE m = LoadLibraryExW(p, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!m) return;
    LPWSTR nome = nullptr;
    EnumResourceNamesW(m, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */, sorPrimaIcona, (LONG_PTR)&nome);
    if (nome)
    {
        HICON grande = (HICON)LoadImageW(m, nome, IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        HICON piccola = (HICON)LoadImageW(m, nome, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (grande) SendMessageW((HWND)finestra, WM_SETICON, ICON_BIG, (LPARAM)grande);
        if (piccola) SendMessageW((HWND)finestra, WM_SETICON, ICON_SMALL, (LPARAM)piccola);
    }
    // the icons stay in use by the window: the module is not freed.
}
