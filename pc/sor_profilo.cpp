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
