// Quick save for testing (European game SLES-52950). A tool for us, not for players: it is NOT a save state.
//
// On only when the file "quicksave.on" exists in the game's working folder (next to sor.exe). Without it nothing here
// runs and nothing is registered: the game is exactly as before.
//   F5: remembers the area, the entrance, the player's position, life and weapon in "quicksave.ini".
//   F4: goes back there. In the same area it moves the player at once; in another area it asks the game for an area
//       jump (the same jump the test scripts use, LAVORO-SHADOW-OF-ROME.md «Come si salta davvero»), waits for the
//       player to exist in the new area, then moves him and puts back life and weapon for a few frames.
// What it does not bring back: enemies, objects, doors, the story state, the inventory. They are as the area loads them.
// Addresses (DWARF of the PAL ELF and the test scripts _tmp_solido/lotta.py, _tmp_solido/fondo/foresta.py):
//   0x4BE2E8 Global.Area_no (ushort), 0x4BE2EA the next area (ushort), 0x4BE2F5 the entrance (byte),
//   0x4EA16C event bits read by game_clear_check: 0x01000000 = area jump (ev_area_jmp),
//   0x4E69E0 head of the player list: node = {next, actor}; actor +0x90 position (3 floats), +0x380 life (high 16
//   bits) and max life (low 16), +0x624 weapon (byte).
// It runs once per frame inside classSYSTEM::wbuf_swap (0x1E92D0), chained after the cheats and the free camera.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "sor_indirizzi.h" // SA(): the game's addresses, European or American build

bool sorTelecomandoAcceso(); // pc/sor_prove.cpp: the test remote control holds the keys
bool sorTastoIniettato(int codice);

namespace
{
    const uint32_t kSwap = SA(0x1E92D0u);      // classSYSTEM::wbuf_swap
    const uint32_t kArea = SA(0x4BE2E8u);      // ushort area, ushort next area
    const uint32_t kEntrance = SA(0x4BE2F5u);  // byte
    const uint32_t kEvent = SA(0x4EA16Cu);     // event bits
    const uint32_t kAreaJump = 0x01000000u;
    const uint32_t kPlayers = SA(0x4E69E0u);   // head of the player list
    const uint32_t kPos = 0x90u, kLife = 0x380u, kWeapon = 0x624u;

    PS2Runtime::RecompiledFunction g_swap = nullptr;
    std::string g_file = "quicksave.ini";
    bool g_prev4 = false, g_prev5 = false;

    struct Save
    {
        bool valid = false;
        uint16_t area = 0;
        uint8_t entrance = 0;
        float x = 0, y = 0, z = 0;
        uint32_t life = 0;
        uint8_t weapon = 0;
    } g_save;

    enum class Phase
    {
        idle,
        jumping,   // asking for the jump until the area changes
        waiting,   // area changed: waiting for the player
        placing    // writing position, life and weapon for a few frames
    } g_phase = Phase::idle;
    int g_count = 0;

    uint8_t *at(uint8_t *rdram, uint32_t a) { return rdram + (a & PS2_RAM_MASK); }
    uint32_t rd32(uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, at(rdram, a), 4);
        return v;
    }
    void wr32(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(at(rdram, a), &v, 4); }
    float rdF(uint8_t *rdram, uint32_t a)
    {
        float v;
        std::memcpy(&v, at(rdram, a), 4);
        return v;
    }
    void wrF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(at(rdram, a), &v, 4); }
    uint16_t area(uint8_t *rdram) { return static_cast<uint16_t>(rd32(rdram, kArea) & 0xFFFFu); }

    // The player's actor, or 0 when there is none (menus, loading).
    uint32_t player(uint8_t *rdram)
    {
        const uint32_t node = rd32(rdram, kPlayers);
        if (node <= 0x100000u || node >= 0x2000000u)
            return 0;
        const uint32_t actor = rd32(rdram, node + 4);
        if (actor <= 0x100000u || actor >= 0x2000000u)
            return 0;
        return actor;
    }

    bool pressed(int k, bool &prev)
    {
        const bool d = sorTelecomandoAcceso() ? sorTastoIniettato(k) : IsKeyDown(k);
        const bool hit = d && !prev;
        prev = d;
        return hit;
    }

    void log(const char *what)
    {
        std::printf("[sor] quick save: %s (area %03X entrance %u, at %.1f %.1f %.1f, life %d/%d, weapon %u)\n", what,
                    g_save.area, g_save.entrance, g_save.x, g_save.y, g_save.z, static_cast<int>(g_save.life >> 16),
                    static_cast<int>(g_save.life & 0xFFFFu), g_save.weapon);
        std::fflush(stdout);
    }

    void writeFile()
    {
        std::ofstream f(g_file, std::ios::trunc);
        f << "area=" << g_save.area << "\nentrance=" << unsigned(g_save.entrance) << "\nx=" << g_save.x
          << "\ny=" << g_save.y << "\nz=" << g_save.z << "\nlife=" << g_save.life << "\nweapon=" << unsigned(g_save.weapon)
          << "\n";
    }

    void readFile()
    {
        std::ifstream f(g_file);
        std::string line;
        int seen = 0;
        while (std::getline(f, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            try
            {
                if (k == "area") g_save.area = static_cast<uint16_t>(std::stoul(v)), ++seen;
                else if (k == "entrance") g_save.entrance = static_cast<uint8_t>(std::stoul(v)), ++seen;
                else if (k == "x") g_save.x = std::stof(v), ++seen;
                else if (k == "y") g_save.y = std::stof(v), ++seen;
                else if (k == "z") g_save.z = std::stof(v), ++seen;
                else if (k == "life") g_save.life = static_cast<uint32_t>(std::stoul(v)), ++seen;
                else if (k == "weapon") g_save.weapon = static_cast<uint8_t>(std::stoul(v)), ++seen;
            }
            catch (...)
            {
            }
        }
        g_save.valid = seen == 7;
    }

    void place(uint8_t *rdram, uint32_t actor)
    {
        wrF(rdram, actor + kPos, g_save.x);
        wrF(rdram, actor + kPos + 4, g_save.y);
        wrF(rdram, actor + kPos + 8, g_save.z);
        wr32(rdram, actor + kLife, g_save.life);
        uint32_t w = rd32(rdram, actor + kWeapon);
        w = (w & ~0xFFu) | g_save.weapon;
        wr32(rdram, actor + kWeapon, w);
    }

    void doSave(uint8_t *rdram)
    {
        const uint32_t actor = player(rdram);
        if (!actor || g_phase != Phase::idle)
        {
            std::printf("[sor] quick save: not now (no player on screen, or a load is running)\n");
            std::fflush(stdout);
            return;
        }
        g_save.area = area(rdram);
        g_save.entrance = *at(rdram, kEntrance);
        g_save.x = rdF(rdram, actor + kPos);
        g_save.y = rdF(rdram, actor + kPos + 4);
        g_save.z = rdF(rdram, actor + kPos + 8);
        g_save.life = rd32(rdram, actor + kLife);
        g_save.weapon = static_cast<uint8_t>(rd32(rdram, actor + kWeapon) & 0xFFu);
        g_save.valid = true;
        writeFile();
        log("saved (F5)");
    }

    void doLoad(uint8_t *rdram)
    {
        if (!g_save.valid)
        {
            std::printf("[sor] quick save: nothing saved yet, press F5 first\n");
            std::fflush(stdout);
            return;
        }
        if (area(rdram) == g_save.area && player(rdram))
        {
            g_phase = Phase::placing;
            g_count = 0;
            log("loading in the same area (F4)");
            return;
        }
        g_phase = Phase::jumping;
        g_count = 0;
        log("loading, area jump (F4)");
    }

    void step(uint8_t *rdram)
    {
        switch (g_phase)
        {
        case Phase::idle:
            return;
        case Phase::jumping:
            if (area(rdram) == g_save.area)
            {
                g_phase = Phase::waiting;
                g_count = 0;
                return;
            }
            if (g_count % 90 == 0) // ask again every 90 frames, as the test scripts do every 1.5 s
            {
                if (g_count >= 90 * 40)
                {
                    std::printf("[sor] quick save: the area jump did not happen, stopped\n");
                    std::fflush(stdout);
                    g_phase = Phase::idle;
                    return;
                }
                wr32(rdram, kArea, (static_cast<uint32_t>(g_save.area) << 16) | area(rdram));
                *at(rdram, kEntrance) = g_save.entrance;
                wr32(rdram, kEvent, rd32(rdram, kEvent) | kAreaJump);
            }
            ++g_count;
            return;
        case Phase::waiting:
            if (player(rdram))
                ++g_count;
            else
                g_count = 0;
            if (g_count >= 60) // the player has existed for a second: the area is playing
            {
                g_phase = Phase::placing;
                g_count = 0;
            }
            return;
        case Phase::placing:
            if (const uint32_t actor = player(rdram))
                place(rdram, actor);
            if (++g_count >= 10)
            {
                g_phase = Phase::idle;
                log("loaded");
            }
            return;
        }
    }

    void quickSwap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kSwap && IsWindowReady())
        {
            if (pressed(KEY_F5, g_prev5))
                doSave(rdram);
            if (pressed(KEY_F4, g_prev4))
                doLoad(rdram);
            step(rdram);
        }
        g_swap(rdram, ctx, rt);
    }
}

// Called once after loadELF (pc/main_sor.cpp), after the free camera (its hook on wbuf_swap is chained here).
void sorSalvaRapidoInstalla(PS2Runtime &rt)
{
    std::error_code ec;
    if (!std::filesystem::exists("quicksave.on", ec))
        return;
    g_swap = rt.lookupFunction(kSwap);
    if (!g_swap)
    {
        std::printf("[sor] quick save: wbuf_swap 0x%06X not found, it stays off\n", kSwap);
        std::fflush(stdout);
        return;
    }
    const std::filesystem::path full = std::filesystem::absolute(g_file, ec);
    if (!ec)
        g_file = full.string();
    readFile();
    rt.registerFunction(kSwap, quickSwap);
    std::printf("[sor] quick save ready: F5 saves, F4 loads (%s%s)\n", g_file.c_str(),
                g_save.valid ? ", a save is there" : "");
    std::fflush(stdout);
}
