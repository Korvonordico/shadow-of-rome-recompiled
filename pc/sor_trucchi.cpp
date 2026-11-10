// Debug cheats for the European game (SLES-52950, ELF CRC B1995E29). A tool for testing, not for players.
//
// They are turned on by the environment variable SOR_CHEATS, a comma list (the launcher's hidden DEBUG page sets it):
//   SOR_CHEATS=invincible,weapons,onehit,salvo,gold,unlock,allsalvos,nofilms
// Without SOR_CHEATS nothing here runs and nothing is registered: the game is exactly as before.
// Every value is written again once per frame, before classSYSTEM::wbuf_swap (0x1E92D0, called once per frame by
// classSYSTEM::main_loop), the way a pnach "patch=1" line rewrites a value every frame.
// All addresses and member names come from the DWARF debug data of the PAL ELF (see LAVORO-SHADOW-OF-ROME.md, 09/11).
//
// 1. The game's own hidden cheats (the EXTRAS cheat menu, classOMAKE_TASK::move_main_cheat at 0x3F98F0, table
//    CHEAT_TBL at 0x494BB0): bits of Global.mCheat_use_flag (classGLOBAL Global at 0x4BE200, +0x364 = 0x4BE564).
//    Never saved: the game clears them at start-up (classPALSELECT_TASK::move_memcd_check) and the save code copies
//    only Configdata.mCheat_use_flag (0x551A24), which is not touched here.
//   invincible (alias health): bit 0x800. Pl_life_set (0x159420) returns without changing the player's life
//     (classCHARACTER::mLife, +0x382) when the bit is on.
//   weapons: bit 0x80. classWEAPON::life_set (0x1A20C0) skips the wear of weapons with attribute bit 37, and
//     Wep_break_able_check (0x1B3710) treats them as unbreakable.
//   onehit: bit 0x100. com_damage_life_set (0x1B8BC0) sets the life of enemies of kind 2 or 8 to -1 on any hit; the
//     same bit makes ANY hit on the player deadly, unless invincible is on too.
//
// 2. Converted from the public codes of the US game (SLUS-20902, CRC 57818AF6). The US data addresses of the
//    Configdata/Savedata/Audience objects are the PAL ones minus 0xA80: every one of the 400+ US data codes falls on
//    a member of the PAL classes with that single offset, and the values match the game's own limits.
//   salvo: Audience.mAud_point (classAUDIENCE Audience 0x4FE920, +0xCA0 = 0x4FF5C0) = 99,999,999. US "Max Salvo
//     Points" 0x4FEB40. classAUDIENCE::cheer_cnt_add (0x1FC8E8) caps the value at exactly 99,999,999; classAUDIENCE::move
//     copies it into Savedata.mCheer_point (0x552660): it ends up in the saved game.
//   gold: Savedata.mGold[0..3] (classSCENARIO, Savedata 0x551C00 +0x0, sint[4]) = 9999. Not a US code: the money of the
//     shop. classSHOP::goods_get (0x30A928) takes the price from mGold[character]; the shop counts it with a 16-bit
//     value (mGold_tmp, sshort), so 9999 stays in range. Saved in the game.
//   unlock: Configdata.mCheat_flag[0] (0x551400 +0x18 = 0x551418) = 0xFFFFFFFF. US "Unlock All Options/Cheats"
//     0x550998. Read by mc_difficulty_select, cheat_check and the title menu. Saved in the system data.
//   allsalvos: Configdata.mSalvo_flg (+0x654 = 0x551A54, 8 words) and mSalvo_new (+0x674 = 0x551A74, 8 words) all
//     ones; Savedata.mSalvo_num[0..251] (classSCENARIO +0x410 = 0x552010, 126 words) = 0xA70F each (count 9999 + the
//     "known" bit 15: the game masks 0x7FFF and caps the count at 9999, 0x3B091C). US "Have All Salvos", "Have All
//     Salvos Viewed", "Have All Salvo List (In-Game)" at 0x550FD4, 0x550FF4, 0x551590. Saved.
//   nofilms (alias nofmv): US "Disable FMV" makes sceMpegIsEnd (0x1099A8, libmpeg, the same address in the PAL ELF:
//     lw v1,0x40(a0); jr ra; lw v0,0(v1)) always answer 1. In this port the plain "is end" answer once left a black
//     screen at 60 Hz, so the port's own tested film skip is used (sorSaltaFilmati, pc/sor_hle.cpp, SOR_SENZAFILMATI).
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void sorSaltaFilmati(PS2Runtime &rt); // pc/sor_hle.cpp

namespace
{
    struct Write
    {
        uint32_t addr;  // guest address of the first 32-bit word
        uint32_t value; // value written (or bits set, with orBits)
        uint32_t words; // how many consecutive words
        bool orBits;    // true: set these bits and keep the others
    };

    struct Cheat
    {
        const char *name;
        const char *alias; // another accepted name, or nullptr
        const char *where; // for the log line
        const Write *writes;
        size_t count;
        bool films; // skip the films (no per-frame write)
    };

    const uint32_t kCheatUseFlag = 0x4BE564u; // Global.mCheat_use_flag (classGLOBAL at 0x4BE200, +0x364)
    const Write kInvincible[] = {{kCheatUseFlag, 0x800u, 1, true}};
    const Write kWeapons[] = {{kCheatUseFlag, 0x080u, 1, true}};
    const Write kOneHit[] = {{kCheatUseFlag, 0x100u, 1, true}};
    const Write kSalvo[] = {{0x4FF5C0u, 99999999u, 1, false}};   // Audience.mAud_point
    const Write kGold[] = {{0x551C00u, 9999u, 4, false}};         // Savedata.mGold[4]
    const Write kUnlock[] = {{0x551418u, 0xFFFFFFFFu, 1, false}}; // Configdata.mCheat_flag[0]
    const Write kAllSalvos[] = {
        {0x551A54u, 0xFFFFFFFFu, 8, false},   // Configdata.mSalvo_flg
        {0x551A74u, 0xFFFFFFFFu, 8, false},   // Configdata.mSalvo_new
        {0x552010u, 0xA70FA70Fu, 126, false}, // Savedata.mSalvo_num[0..251]
    };
    const Cheat kCheats[] = {
        {"invincible", "health", "0x4BE564 bit 0x800", kInvincible, 1, false},
        {"weapons", nullptr, "0x4BE564 bit 0x80", kWeapons, 1, false},
        {"onehit", nullptr, "0x4BE564 bit 0x100", kOneHit, 1, false},
        {"salvo", nullptr, "0x4FF5C0 = 99999999, saved", kSalvo, 1, false},
        {"gold", nullptr, "0x551C00 x4 = 9999, saved", kGold, 1, false},
        {"unlock", nullptr, "0x551418 = 0xFFFFFFFF, saved", kUnlock, 1, false},
        {"allsalvos", nullptr, "0x551A54 x16 + 0x552010 x126, saved", kAllSalvos, 3, false},
        {"nofilms", "nofmv", "0x1099A8 sceMpegIsEnd: films skipped", nullptr, 0, true},
    };
    const char kKnown[] = "invincible, weapons, onehit, salvo, gold, unlock, allsalvos, nofilms";
    const uint32_t kPerFrameHook = 0x1E92D0u; // classSYSTEM::wbuf_swap

    std::vector<Write> g_writes; // what is written every frame
    PS2Runtime::RecompiledFunction g_swap = nullptr;

    void swapWithCheats(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        for (const Write &w : g_writes)
        {
            for (uint32_t k = 0; k < w.words; ++k)
            {
                uint8_t *p = rdram + ((w.addr + 4u * k) & PS2_RAM_MASK);
                uint32_t v;
                std::memcpy(&v, p, 4);
                const uint32_t nv = w.orBits ? (v | w.value) : w.value;
                if (nv != v)
                    std::memcpy(p, &nv, 4);
            }
        }
        g_swap(rdram, ctx, rt);
    }

    std::string lower(std::string s)
    {
        for (char &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}

// Called once after loadELF (pc/main_sor.cpp), after the other replacements.
void sorTrucchiInstalla(PS2Runtime &rt)
{
    const char *env = std::getenv("SOR_CHEATS");
    if (!env || !*env)
        return;
    std::vector<bool> on(sizeof(kCheats) / sizeof(kCheats[0]), false);
    std::string list(env);
    size_t start = 0;
    while (start <= list.size())
    {
        size_t end = list.find(',', start);
        if (end == std::string::npos)
            end = list.size();
        std::string word = lower(list.substr(start, end - start));
        const size_t a = word.find_first_not_of(" \t");
        const size_t b = word.find_last_not_of(" \t");
        word = (a == std::string::npos) ? std::string() : word.substr(a, b - a + 1);
        if (!word.empty())
        {
            bool known = false;
            for (size_t i = 0; i < on.size(); ++i)
            {
                const Cheat &c = kCheats[i];
                if (word == c.name || (c.alias && word == c.alias))
                    known = on[i] = true;
            }
            if (!known)
                std::printf("[sor] cheat unknown: %s (known: %s)\n", word.c_str(), kKnown);
        }
        start = end + 1;
    }
    std::vector<Write> writes;
    bool films = false;
    for (size_t i = 0; i < on.size(); ++i)
    {
        if (!on[i])
            continue;
        const Cheat &c = kCheats[i];
        for (size_t k = 0; k < c.count; ++k)
            writes.push_back(c.writes[k]);
        films = films || c.films;
    }
    if (!writes.empty() && !rt.lookupFunction(kPerFrameHook))
    {
        std::printf("[sor] cheats: per-frame point 0x%06X not found, the value cheats stay off\n", kPerFrameHook);
        writes.clear();
    }
    for (size_t i = 0; i < on.size(); ++i)
        if (on[i] && (kCheats[i].films || !writes.empty()))
            std::printf("[sor] cheat on: %s (%s)\n", kCheats[i].name, kCheats[i].where);
    std::fflush(stdout);
    if (films)
        sorSaltaFilmati(rt);
    if (writes.empty())
        return;
    g_swap = rt.lookupFunction(kPerFrameHook);
    g_writes = writes;
    rt.registerFunction(kPerFrameHook, swapWithCheats);
}
