// Connects the black box (blackbox/, which knows nothing about this game) to Shadow of Rome:
// the switch, the game's progress counter, where the game's threads are, the key, and the test commands.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "raylib.h"
#include "blackbox/blackbox.h"
#include "../ps2recomp/ps2xRuntime/src/lib/Kernel/Stubs/Pad.h" // getPadDebugSnapshot: the buttons the game reads

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "sor_indirizzi.h" // SA(): the game's addresses, European or American build

// windows.h cannot stay together with raylib.h.
extern "C" __declspec(dllimport) int __stdcall VirtualProtect(void *address, size_t size, unsigned long protection, unsigned long *old);

// sor_scheletro.cpp: the skeleton rules (sor-cattivo, 16/11/2026)
void sorScheletroStart();
void sorScheletroFrame(PS2Runtime *rt);
void sorScheletroCommand(const char *what, int value);

extern std::atomic<uint64_t> g_sorContaPad; // Stubs/Pad.cpp: pad reads, they stop if the game's logic stops
// ps2_memory.cpp (sor-sfarfallio, 16/11/2026): the game's drawing chains (VIF1), and those cut short by a REFE tag
extern std::atomic<uint64_t> g_sorCateneTagliate;
extern std::atomic<uint64_t> g_sorCateneVif1;

namespace
{
    std::atomic<PS2Runtime *> g_runtime{nullptr};
    bool g_on = false;
    bool g_watch = false; // the sentinel is on
    // The drawing chain check (sor-sfarfallio, 16/11/2026). With the video card the sentinel sees one small picture
    // every 12 frames (sor-gs.dll), always on the same frame of a two-frame flicker, so it never saw the HUD blinking
    // in area 088. The cause of that flicker is visible here on every frame, without pictures: a chain of the game's
    // drawing that stops before its END loses everything drawn after that point (HUD, text, fog, menus).
    uint64_t g_chainCutSeen = 0, g_chainsSeen = 0;
    int g_chainFrames = 0;
    std::chrono::steady_clock::time_point g_chainNextSave{};

    // The game's low-life warning (sor-giro, 16/11/2026): with little life left the player's life bar (top left)
    // flashes ON PURPOSE, some frames full and bright red, then short again (12-16 frames in a row looked at, life 1
    // and 0 of 2048; the same in the build of the morning). The sentinel took it for flicker (folders in areas 050,
    // 053, 080, 082 of the tour and in a test with life 1: false alarms whenever the player is about to die). While
    // the player's life is at most kLowLifePercent of the max, flicker in that band (x 0-58%, y 0-12%: the bar ends
    // near 52% in 16:9) does not count; the rest of the screen is still checked (the HUD bug of sor-sfarfallio hit
    // all the 2D: S/S, menus, text, fog, not only the bar). Where the warning starts (Agrippa, 16 frames in a row
    // at each value): 50, 35, 25, 20% no flash; 15% and 10% flash. So 20% (a little margin over the real point).
    const uint32_t kLowLifePercent = 20;
    bool g_lowLife = false;
    void lowLifeQuiet() // once per shown frame; reads the player's life 6 times a second
    {
        static unsigned s_count = 0;
        if (++s_count % 10u)
            return;
        PS2Runtime *rt = g_runtime.load();
        const uint8_t *ram = rt ? rt->memory().getRDRAM() : nullptr;
        bool low = false;
        if (ram)
        {
            const auto r32 = [ram](uint32_t a) { uint32_t v = 0; std::memcpy(&v, ram + a, 4); return v; };
            const auto ok = [](uint32_t a) { return a >= 0x100000u && a + 0x400u <= 0x2000000u && !(a & 3u); };
            const uint32_t node = r32(SA(0x4E69E0u)); // WORK_MAN: first node of the player list {next, actor}
            if (ok(node))
            {
                const uint32_t actor = r32(node + 4u);
                if (ok(actor))
                {
                    const uint32_t v = r32(actor + 0x380u); // life (high half) / max life (low half)
                    const uint32_t life = v >> 16, most = v & 0xFFFFu;
                    low = most >= 64u && (life * 100u <= most * kLowLifePercent || life > most); // > max: 65535 = -1
                }
            }
        }
        if (low != g_lowLife)
        {
            g_lowLife = low;
            if (low)
                blackbox::quietFlicker(0.f, 0.f, 0.58f, 0.12f);
            else
                blackbox::quietFlicker(0.f, 0.f, 0.f, 0.f);
        }
    }

    void chainCheck() // once per shown frame; looks at the counters once a second
    {
        if (++g_chainFrames < 60)
            return;
        g_chainFrames = 0;
        const uint64_t cut = g_sorCateneTagliate.load(std::memory_order_relaxed);
        const uint64_t chains = g_sorCateneVif1.load(std::memory_order_relaxed);
        const uint64_t newCut = cut - g_chainCutSeen, newChains = chains - g_chainsSeen;
        g_chainCutSeen = cut;
        g_chainsSeen = chains;
        if (newCut == 0)
            return;
        blackbox::event("[sentinel] flicker: %llu of the last %llu drawing chains of the game stopped before their end (on a REFE tag): "
                        "what is drawn after that point (HUD, text, fog, menus) is missing on those frames",
                        static_cast<unsigned long long>(newCut), static_cast<unsigned long long>(newChains));
        const auto now = std::chrono::steady_clock::now();
        if (now >= g_chainNextSave)
        {
            g_chainNextSave = now + std::chrono::seconds(120);
            blackbox::save("flicker");
        }
    }
    bool g_keyDown = false;
    // Test commands (sorBlackboxCommand)
    std::atomic<int> g_testStallSeconds{0};
    std::atomic<int> g_testSlowFrames{0};
    std::atomic<int> g_testBlackFrames{0};
    std::atomic<int> g_testCrashHere{0};
    std::atomic<int> g_testFlickerFrames{0}; // sor-cattivo: forced flicker for the sentinel test
    std::atomic<int> g_testColourFrames{0};  // sor-cattivo: forced colour flash
    std::vector<uint8_t> g_blackFrame;

    // The sentinel (Iris's checks on every frame) and the skeleton rules (sor_scheletro.cpp): SOR_SENTINEL=0 or 1
    // wins; then sentinel=0 or 1 in settings.ini; then on if the file "sentinel.on" is next to the game (in the
    // working folder); off otherwise, so the package (which has no sentinel.on) is not touched.
    bool sentinelEnabled()
    {
        if (const char *e = std::getenv("SOR_SENTINEL"))
            return e[0] != '0';
        const char *file = std::getenv("SOR_IMPOSTAZIONI");
        std::ifstream f(file && *file ? file : "settings.ini");
        std::string line;
        int set = -1;
        while (std::getline(f, line))
        {
            if (line.rfind("sentinel=0", 0) == 0)
                set = 0;
            else if (line.rfind("sentinel=1", 0) == 0)
                set = 1;
        }
        if (set >= 0)
            return set == 1;
        return std::ifstream("sentinel.on").good();
    }

    // SOR_BLACKBOX=0 or 1 wins; then blackbox=0 or 1 in settings.ini; on otherwise.
    bool enabled()
    {
        if (const char *e = std::getenv("SOR_BLACKBOX"))
            return e[0] != '0';
        const char *file = std::getenv("SOR_IMPOSTAZIONI");
        std::ifstream f(file && *file ? file : "settings.ini");
        std::string line;
        bool on = true;
        while (std::getline(f, line))
        {
            if (line.rfind("blackbox=0", 0) == 0)
                on = false;
            else if (line.rfind("blackbox=1", 0) == 0)
                on = true;
        }
        return on;
    }

    uint64_t progress() { return g_sorContaPad.load(std::memory_order_relaxed); }

    void state(char *out, size_t n)
    {
        PS2Runtime *rt = g_runtime.load();
        if (!rt)
            return;
        const EeKernelSnapshot ee = rt->eeScheduler().snapshot();
        int used = std::snprintf(out, n, "[state] EE thread %d, cycle %llu:", ee.runningThreadId, static_cast<unsigned long long>(ee.eeCycle));
        for (const auto &th : ee.threads)
            if (used > 0 && static_cast<size_t>(used) + 24 < n && (th.status == EeThreadStatus::Running || th.status == EeThreadStatus::Ready))
                used += std::snprintf(out + used, n - static_cast<size_t>(used), " %d pc=%x ra=%x", th.id, th.pc, th.ra);
    }

    // The pad as the game last read it, whoever pressed (keyboard, controller, test script, remote control).
    void input(char *out, size_t n)
    {
        static const char *const kNames[16] = {"SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT",
                                               "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"};
        const ps2_stubs::PadDebugSnapshot pad = ps2_stubs::getPadDebugSnapshot();
        const ps2_stubs::PadDebugPortSnapshot &p = pad.ports[0][0];
        if (!p.lastReadOk)
            return;
        size_t used = 0;
        auto add = [&](const char *name)
        {
            if (used + 12 < n)
                used += static_cast<size_t>(std::snprintf(out + used, n - used, "%s%s", used ? "+" : "", name));
        };
        for (int b = 0; b < 16; ++b)
            if (!(p.lastButtons & (1u << b)))
                add(kNames[b]);
        if (p.lx < 0x40)
            add("STICK-LEFT");
        else if (p.lx > 0xC0)
            add("STICK-RIGHT");
        if (p.ly < 0x40)
            add("STICK-UP");
        else if (p.ly > 0xC0)
            add("STICK-DOWN");
        if (!used)
            std::snprintf(out, n, "none");
    }

    // Shadow of Rome's own variables: area (half word at 0x4be2e8) and scene (0x4be2e0).
    void scene(char *out, size_t n)
    {
        PS2Runtime *rt = g_runtime.load();
        if (!rt || !rt->memory().getRDRAM())
            return;
        const uint8_t *ram = rt->memory().getRDRAM();
        uint16_t area = 0, number = 0;
        std::memcpy(&area, ram + SA(0x4be2e8u), 2);
        std::memcpy(&number, ram + SA(0x4be2e0u), 2);
        std::snprintf(out, n, "area %03x scene %u", area, number);
    }

    void report(FILE *f)
    {
        PS2Runtime *rt = g_runtime.load();
        if (!rt)
            return;
        const EeKernelSnapshot ee = rt->eeScheduler().snapshot();
        std::fprintf(f, "EE cycle %llu, running thread %d\n", static_cast<unsigned long long>(ee.eeCycle), ee.runningThreadId);
        for (const auto &th : ee.threads)
            std::fprintf(f, "thread %d: status %d, wait %d/%d, pc=0x%x ra=0x%x entry=0x%x priority %d\n", th.id, static_cast<int>(th.status),
                         static_cast<int>(th.waitReason), th.waitId, th.pc, th.ra, th.entry, th.currentPriority);
        for (const auto &s : ee.semaphores)
            std::fprintf(f, "semaphore %d: count %d\n", s.id, s.count);
        const auto iop = rt->iopDebugSnapshot();
        std::fprintf(f, "IOP: cycles %llu, instructions %llu, modules %u, threads %u, RPC servers %u\n", static_cast<unsigned long long>(iop.emulatorCycles),
                     static_cast<unsigned long long>(iop.emulatorInstructions), iop.emulatorLoadedModules, iop.emulatorThreads, iop.emulatorRpcServers);
        for (const auto &d : iop.diagnostics)
            std::fprintf(f, "IOP: %s\n", d.c_str());
    }
}

void sorBlackboxStart()
{
    if (!enabled())
        return;
    // Test reports: chatty, and state.txt already has what matters of them.
    static const char *const kIgnored[] = {"[tempi]", "[ban]", "[bar]", "[peek]", "[audio]", "[sor] thread", "[sor] semaforo", "[sor] GS", "[sor] grafica",
                                           "[sor] IOP", "[sor] ===", "thread ", "rpc servers:", "modules:", "[spu]", "[sceDmaSend]", "[sor] telecomando",
                                           "[sor] foto", "INFO: FILEIO", nullptr};
    blackbox::Config config;
    config.ignoreLinesStartingWith = kIgnored;
    if (const char *keep = std::getenv("SOR_BLACKBOX_KEEP")) // tests with many marked steps
        config.keepFolders = std::atoi(keep);
    static const char *const kLoading[] = {"[load]", nullptr};
    config.loadingLinesStartingWith = kLoading;
    config.progressName = "pad reads";
    config.progress = progress;
    config.state = state;
    config.report = report;
    config.scene = scene;
    config.input = input;
    if (const char *folder = std::getenv("SOR_BLACKBOX_FOLDER")) // tests: their own folder, away from the player's
        if (*folder)
            config.folder = folder;
    const bool watch = sentinelEnabled();
    config.sentinel.black = config.sentinel.samePicture = config.sentinel.flicker = config.sentinel.colours = watch;
    g_on = blackbox::start(config);
    g_watch = g_on && watch;
    if (g_watch)
        sorScheletroStart(); // the skeleton rules: same switch (SOR_SKELETON=0 turns only them off)
}

void sorBlackboxSetRuntime(PS2Runtime *rt) { g_runtime.store(rt); }
bool sorBlackboxOn() { return g_on; } // the runtime asks sor-gs.dll for pictures only then

namespace
{
    PS2Runtime::RecompiledFunction g_cdRead = nullptr;
    uint32_t g_nextSector = 0;
    std::chrono::steady_clock::time_point g_lastLoadLine;

    // Every disc read of the game: one line for each run of reads that follow one another, and one every
    // 0.25 s inside a long run, so that a long load shows as reads that continue.
    void cdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t sector = GPR_U32(ctx, 4), count = GPR_U32(ctx, 5);
        const auto now = std::chrono::steady_clock::now();
        if (sector != g_nextSector || now - g_lastLoadLine >= std::chrono::milliseconds(250))
        {
            blackbox::event("[load] disc sector %u, %u sectors, into 0x%x (ra 0x%x)", sector, count, GPR_U32(ctx, 6), GPR_U32(ctx, 31));
            g_lastLoadLine = now;
        }
        g_nextSector = sector + count;
        g_cdRead(rdram, ctx, rt);
    }
}

// After the game is loaded.
void sorBlackboxInstall(PS2Runtime &rt)
{
    if (!g_on)
        return;
    g_cdRead = rt.lookupFunction(SA(0x120DD8u)); // sceCdRead
    if (g_cdRead)
        rt.registerFunction(SA(0x120DD8u), cdRead);
}

// From ps2_runtime.cpp, with every frame shown. This is the one place that changes when drawing moves to the GPU.
void sorBlackboxFrame(const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (!g_on)
        return;
    // F9: F12 is raylib's own screenshot key. Held state, since this runs once per game frame, not per window frame.
    const bool down = IsKeyDown(KEY_F9);
    if (down && !g_keyDown)
        blackbox::save("key");
    g_keyDown = down;

    if (const int seconds = g_testStallSeconds.exchange(0))
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    if (g_testSlowFrames.load(std::memory_order_relaxed) > 0)
    {
        g_testSlowFrames.fetch_sub(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (g_testCrashHere.exchange(0))
        *static_cast<volatile int *>(nullptr) = 1;
    if (g_testBlackFrames.load(std::memory_order_relaxed) > 0)
    {
        g_testBlackFrames.fetch_sub(1);
        g_blackFrame.assign(static_cast<size_t>(width) * height * 4u, 0u);
        rgba = g_blackFrame.data();
    }
    // sor-cattivo tests (what the black box sees, not the screen): flicker = the top left quarter black on every
    // other frame; colours = a block of strong colours over the middle half of the screen.
    if (rgba && width && height)
    {
        const int flicker = g_testFlickerFrames.load(std::memory_order_relaxed);
        const int colours = g_testColourFrames.load(std::memory_order_relaxed);
        if (flicker > 0 || colours > 0)
        {
            const size_t row = static_cast<size_t>(width) * 4u;
            if (rgba != g_blackFrame.data())
                g_blackFrame.assign(rgba, rgba + row * height);
            uint8_t *px = g_blackFrame.data();
            if (flicker > 0)
            {
                g_testFlickerFrames.fetch_sub(1);
                if (flicker & 1)
                    for (uint32_t y = 0; y < height / 2; ++y)
                        std::memset(px + y * row, 0, row / 2);
            }
            if (colours > 0)
            {
                g_testColourFrames.fetch_sub(1);
                for (uint32_t y = height / 4; y < height * 3 / 4; ++y)
                    for (uint32_t x = width / 4; x < width * 3 / 4; ++x)
                    {
                        uint8_t *p = px + y * row + x * 4u;
                        const int k = static_cast<int>((x / 16 + y / 16) % 3);
                        p[0] = k == 0 ? 255 : 0;
                        p[1] = k == 1 ? 255 : 0;
                        p[2] = k == 2 ? 255 : (k == 0 ? 255 : 0);
                    }
            }
            rgba = px;
        }
    }
    if (g_watch)
        lowLifeQuiet();
    blackbox::frame(rgba, static_cast<int>(width), static_cast<int>(height));
    sorScheletroFrame(g_runtime.load());
    if (g_watch)
        chainCheck();
}

// From ps2_runtime.cpp, when the player closes the window: run() ending without this is not normal.
static std::atomic<bool> g_closedByUser{false};
void sorBlackboxClosing()
{
    g_closedByUser.store(true);
    blackbox::cleanExit();
}
bool sorBlackboxClosedByUser() { return g_closedByUser.load(); }
void sorBlackboxFatal(const char *reason, const char *message) { blackbox::fatal(reason, message); }

namespace
{
    int __attribute__((noinline)) overflowStack(int depth)
    {
        volatile char filler[4096];
        filler[0] = static_cast<char>(depth);
        return overflowStack(depth + 1) + filler[0];
    }
}

// Remote control of the tests (sor_prove.cpp):
//   blackbox [images]          saves now
//   bbtest stall <seconds>     the presenting thread stops: no new frames
//   bbtest slow <frames>       the next frames take 50 ms more each
//   bbtest black <frames>      the black box sees black frames (the screen does not change)
//   bbtest flicker <frames>    the black box sees the top left quarter black on every other frame (sentinel test)
//   bbtest colours <frames>    the black box sees a block of strong colours (sentinel test)
//   bbtest bone <frames>       the skeleton rules see one broken joint of the player (sor_scheletro.cpp test)
//   bbtest explode <frames>    the skeleton rules see every joint of the player 1.8x farther out (many-bones rule test)
//   bbtest skeleton <0|1>      a line with every joint of the player now (1: all actors)
//   bbtest crash               a bad write on the presenting thread
//   bbtest noaccess <hex>      the game's RAM page with that address becomes unreachable: whoever touches it next crashes
//   bbtest crashhere           a bad write on this thread
//   bbtest overflow            a stack overflow on this thread
//   bbtest stop                the game stops without the window being closed
//   bbtest abort, terminate, exit
void sorBlackboxCommand(const char *line)
{
    char command[32] = {}, what[32] = {};
    int value = 0;
    const int fields = std::sscanf(line, "%31s %31s %d", command, what, &value);
    if (!std::strcmp(command, "blackbox"))
    {
        blackbox::save("remote", fields >= 2 ? std::atoi(what) : 0);
        return;
    }
    if (!std::strcmp(what, "stall"))
        g_testStallSeconds.store(value);
    else if (!std::strcmp(what, "slow"))
        g_testSlowFrames.store(value);
    else if (!std::strcmp(what, "black"))
        g_testBlackFrames.store(value);
    else if (!std::strcmp(what, "flicker"))
        g_testFlickerFrames.store(value);
    else if (!std::strcmp(what, "colours"))
        g_testColourFrames.store(value);
    else if (!std::strcmp(what, "bone") || !std::strcmp(what, "explode") || !std::strcmp(what, "skeleton"))
        sorScheletroCommand(what, value);
    else if (!std::strcmp(what, "crash"))
        g_testCrashHere.store(1);
    else if (!std::strcmp(what, "noaccess"))
    {
        unsigned address = 0;
        std::sscanf(line, "%*s %*s %x", &address);
        unsigned long old = 0;
        if (PS2Runtime *rt = g_runtime.load())
            VirtualProtect(rt->memory().getRDRAM() + (address & ~4095u), 4096, 1u /* PAGE_NOACCESS */, &old);
    }
    else if (!std::strcmp(what, "crashhere"))
        *static_cast<volatile int *>(nullptr) = 1;
    else if (!std::strcmp(what, "overflow"))
        std::printf("%d\n", overflowStack(0));
    else if (!std::strcmp(what, "stop"))
    {
        if (PS2Runtime *rt = g_runtime.load())
            rt->requestStop();
    }
    else if (!std::strcmp(what, "abort"))
        std::abort();
    else if (!std::strcmp(what, "terminate"))
        std::terminate();
    else if (!std::strcmp(what, "exit"))
        std::exit(7);
}
