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

// windows.h cannot stay together with raylib.h.
extern "C" __declspec(dllimport) int __stdcall VirtualProtect(void *address, size_t size, unsigned long protection, unsigned long *old);

extern std::atomic<uint64_t> g_sorContaPad; // Stubs/Pad.cpp: pad reads, they stop if the game's logic stops

namespace
{
    std::atomic<PS2Runtime *> g_runtime{nullptr};
    bool g_on = false;
    bool g_keyDown = false;
    // Test commands (sorBlackboxCommand)
    std::atomic<int> g_testStallSeconds{0};
    std::atomic<int> g_testSlowFrames{0};
    std::atomic<int> g_testBlackFrames{0};
    std::atomic<int> g_testCrashHere{0};
    std::vector<uint8_t> g_blackFrame;

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
        std::memcpy(&area, ram + 0x4be2e8u, 2);
        std::memcpy(&number, ram + 0x4be2e0u, 2);
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
    g_on = blackbox::start(config);
}

void sorBlackboxSetRuntime(PS2Runtime *rt) { g_runtime.store(rt); }

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
    g_cdRead = rt.lookupFunction(0x120DD8u); // sceCdRead
    if (g_cdRead)
        rt.registerFunction(0x120DD8u, cdRead);
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
    blackbox::frame(rgba, static_cast<int>(width), static_cast<int>(height));
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
