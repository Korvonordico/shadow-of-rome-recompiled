// Sostituzioni di funzioni del gioco fatte a mano (Shadow of Rome con PS2Recomp).
// Si installano dopo loadELF con sorInstallaSostituzioni(): la tabella delle
// funzioni del runtime punta qui invece che al codice tradotto.
//
// 1. sceCdSearchFile (0x120190) e sceCdLayerSearchFile (0x11FE90): il gioco
//    cerca IMAGE0/1/2.IMG e poi legge con sceCdRead a "settore del file +
//    scostamento". Qui la ricerca legge l'indice ISO9660 dell'immagine vera
//    (IoPaths::cdImage) e restituisce il settore vero: cosi' sceCdRead del
//    runtime legge dall'immagine senza estrarre 4,6 GB di file.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    struct VoceIso
    {
        uint32_t lsn = 0;
        uint32_t size = 0;
        bool cartella = false;
    };

    std::mutex g_isoMutex;
    bool g_isoLetta = false;
    std::map<std::string, VoceIso> g_iso; // percorso in maiuscolo, "/IMAGE0.IMG"

    uint32_t le32(const uint8_t *p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
    }

    bool leggiSettori(std::ifstream &f, uint32_t lsn, uint32_t bytes, std::vector<uint8_t> &out)
    {
        out.assign(bytes, 0);
        f.clear();
        f.seekg(static_cast<std::streamoff>(lsn) * 2048, std::ios::beg);
        f.read(reinterpret_cast<char *>(out.data()), bytes);
        return f.gcount() == static_cast<std::streamsize>(bytes);
    }

    void leggiCartella(std::ifstream &f, uint32_t lsn, uint32_t size, const std::string &prefisso, int profondita)
    {
        if (profondita > 8)
            return;
        std::vector<uint8_t> d;
        if (!leggiSettori(f, lsn, size, d))
            return;
        size_t i = 0;
        while (i < d.size())
        {
            const uint8_t l = d[i];
            if (l == 0)
            {
                i = (i / 2048 + 1) * 2048;
                continue;
            }
            if (i + l > d.size() || l < 34)
                break;
            const uint8_t *r = &d[i];
            VoceIso v;
            v.lsn = le32(r + 2);
            v.size = le32(r + 10);
            v.cartella = (r[25] & 2) != 0;
            const uint8_t nl = r[32];
            std::string nome(reinterpret_cast<const char *>(r + 33), nl);
            i += l;
            if (nl == 1 && (nome[0] == 0 || nome[0] == 1))
                continue;
            const size_t pv = nome.find(';');
            if (pv != std::string::npos)
                nome.resize(pv);
            for (char &c : nome)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const std::string percorso = prefisso + "/" + nome;
            g_iso[percorso] = v;
            if (v.cartella)
                leggiCartella(f, v.lsn, v.size, percorso, profondita + 1);
        }
    }

    void leggiIso()
    {
        if (g_isoLetta)
            return;
        g_isoLetta = true;
        const std::filesystem::path img = PS2Runtime::getIoPaths().cdImage;
        if (img.empty())
        {
            std::fprintf(stderr, "[sor] nessuna immagine del disco: sceCdSearchFile non trova niente\n");
            return;
        }
        std::ifstream f(img, std::ios::binary);
        std::vector<uint8_t> pvd;
        if (!f || !leggiSettori(f, 16, 2048, pvd) || std::memcmp(&pvd[1], "CD001", 5) != 0)
        {
            std::fprintf(stderr, "[sor] immagine del disco non leggibile come ISO9660\n");
            return;
        }
        leggiCartella(f, le32(&pvd[156 + 2]), le32(&pvd[156 + 10]), "", 0);
        std::printf("[sor] indice del disco letto: %zu voci\n", g_iso.size());
    }

    std::string normalizza(std::string p)
    {
        const size_t dp = p.find(':');
        if (dp != std::string::npos)
            p = p.substr(dp + 1);
        const size_t pv = p.find(';');
        if (pv != std::string::npos)
            p.resize(pv);
        std::string out;
        for (char c : p)
        {
            if (c == '\\')
                c = '/';
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (c == '/' && !out.empty() && out.back() == '/')
                continue;
            out += c;
        }
        if (out.empty() || out[0] != '/')
            out = "/" + out;
        return out;
    }

    // int sceCdLayerSearchFile(sceCdlFILE *fp, const char *name, int layer)
    // int sceCdSearchFile(sceCdlFILE *fp, const char *name)
    // sceCdlFILE: lsn, size, name[16], date[8].
    void cercaFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        ctx->pc = GPR_U32(ctx, 31);
        const uint32_t fp = GPR_U32(ctx, 4);
        const uint32_t pn = GPR_U32(ctx, 5);
        std::string nome;
        for (uint32_t i = 0; i < 256; ++i)
        {
            const char c = static_cast<char>(rdram[(pn + i) & PS2_RAM_MASK]);
            if (!c)
                break;
            nome += c;
        }
        VoceIso v;
        bool trovato = false;
        {
            std::lock_guard<std::mutex> lock(g_isoMutex);
            leggiIso();
            const auto it = g_iso.find(normalizza(nome));
            if (it != g_iso.end() && !it->second.cartella)
            {
                v = it->second;
                trovato = true;
            }
        }
        static int traccia = 0;
        if (traccia < 64)
        {
            ++traccia;
            std::printf("[sor] sceCdSearchFile \"%s\" -> %s lsn=%u size=%u\n", nome.c_str(),
                        trovato ? "trovato" : "NON trovato", v.lsn, v.size);
        }
        if (!trovato || fp == 0)
        {
            SET_GPR_S32(ctx, 2, 0);
            return;
        }
        uint8_t res[32] = {};
        std::memcpy(res, &v.lsn, 4);
        std::memcpy(res + 4, &v.size, 4);
        std::string foglia = normalizza(nome);
        foglia = foglia.substr(foglia.find_last_of('/') + 1) + ";1";
        std::memcpy(res + 8, foglia.data(), std::min<size_t>(16, foglia.size()));
        for (int i = 0; i < 32; ++i)
            rdram[(fp + i) & PS2_RAM_MASK] = res[i];
        SET_GPR_S32(ctx, 2, 1);
    }
}

// 2. Traccia (solo per le prove): con SOR_TRACCIA=<mappa/funzioni.csv> ogni funzione
//    del gioco passa da un involucro che annota le ultime chiamate (indirizzo, ra, a0)
//    e quante volte e' stata chiamata. sorStampaTraccia() le stampa (all'uscita del
//    gioco, alla fine del tempo, o quando si chiama exit).
void sorStampaTraccia();

namespace
{
    struct Chiamata
    {
        uint32_t fn, ra, a0, a1;
    };
    constexpr size_t kAnello = 400;
    Chiamata g_anello[kAnello];
    uint64_t g_numChiamate = 0;
    std::vector<PS2Runtime::RecompiledFunction> g_originali;
    std::vector<uint32_t> g_conteggi;

    uint32_t slotDi(uint32_t pc)
    {
        return (pc - g_ps2RecompiledFunctionTableBase) / 4u;
    }

    // Sentinella: una parola di memoria del gioco che non deve cambiare (per trovare chi
    // la sovrascrive). SOR_SENTINELLA=<indirizzo funzione>: sorveglia la cella dove quella
    // funzione salva il suo ra (sp all'ingresso - SOR_SENTINELLA_OFF, esadecimale).
    uint32_t g_sentFn = 0, g_sentOff = 0, g_sentAddr = 0, g_sentVal = 0;
    bool g_sentArmata = false, g_sentScattata = false;

    uint32_t leggi32(uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, rdram + (a & PS2_RAM_MASK), 4);
        return v;
    }

    void controllaSentinella(uint8_t *rdram, uint32_t pc, uint32_t ra)
    {
        if (!g_sentAddr || g_sentScattata)
            return;
        const uint32_t v = leggi32(rdram, g_sentAddr);
        if (!g_sentArmata)
        {
            g_sentArmata = (v == g_sentVal);
            return;
        }
        if (v != g_sentVal)
        {
            g_sentScattata = true;
            std::printf("[sor] SENTINELLA: [0x%x] da 0x%x a 0x%x, prima della chiamata a 0x%x (ra 0x%x)\n",
                        g_sentAddr, g_sentVal, v, pc, ra);
            std::fflush(stdout);
        }
    }

    unsigned long long reg64(R5900Context *ctx, int i)
    {
        uint64_t v;
        std::memcpy(&v, &ctx->r[i], 8);
        return v;
    }

    void involucro(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t pc = ctx->pc;
        const uint32_t s = slotDi(pc);
        controllaSentinella(rdram, pc, GPR_U32(ctx, 31));
        if (pc == g_sentFn && !g_sentAddr)
        {
            g_sentAddr = GPR_U32(ctx, 29) - g_sentOff;
            g_sentVal = GPR_U32(ctx, 31);
            std::printf("[sor] sentinella su [0x%x] = ra 0x%x di 0x%x\n", g_sentAddr, g_sentVal, pc);
        }
        // SOR_SPIA=<fn hex>,<ra hex>[+<fn hex>,<ra hex>...]: stampa argomenti e risultato (64 bit)
        // delle prime 6 chiamate a fn da ra (ra 0 = da chiunque).
        struct Spia { uint32_t fn, ra; int quante; };
        static std::vector<Spia> s_spie = []()
        {
            std::vector<Spia> v;
            const char *e = std::getenv("SOR_SPIA");
            while (e && *e)
            {
                char *fine = nullptr;
                const uint32_t f = static_cast<uint32_t>(std::strtoul(e, &fine, 16));
                uint32_t r = 0;
                if (*fine == ',')
                    r = static_cast<uint32_t>(std::strtoul(fine + 1, &fine, 16));
                v.push_back({f, r, 0});
                e = (*fine == '+') ? fine + 1 : nullptr;
            }
            return v;
        }();
        // SOR_RAMDUMP=<fn hex>,<n>,<percorso>: copia tutta la RAM su file alla n-esima chiamata di fn.
        static uint32_t s_dumpFn = 0, s_dumpN = 0, s_dumpVisti = 0;
        static const char *s_dumpPath = []() -> const char *
        {
            const char *e = std::getenv("SOR_RAMDUMP");
            if (!e)
                return nullptr;
            char *fine = nullptr;
            s_dumpFn = static_cast<uint32_t>(std::strtoul(e, &fine, 16));
            s_dumpN = static_cast<uint32_t>(std::strtoul(fine + 1, &fine, 10));
            return fine + 1;
        }();
        if (s_dumpPath && pc == s_dumpFn && ++s_dumpVisti == s_dumpN)
        {
            std::ofstream d(s_dumpPath, std::ios::binary);
            d.write(reinterpret_cast<const char *>(rdram), PS2_RAM_SIZE);
            std::printf("[sor] RAM copiata in %s (0x%x, chiamata %u, a0=%llx ra=0x%x)\n", s_dumpPath, pc, s_dumpN, reg64(ctx, 4), GPR_U32(ctx, 31));
            std::fflush(stdout);
        }
        Spia *spia = nullptr;
        for (Spia &q : s_spie)
            if (pc == q.fn && (!q.ra || GPR_U32(ctx, 31) == q.ra) && q.quante < 12)
            {
                ++q.quante;
                spia = &q;
                std::printf("[sor] spia 0x%x da 0x%x (n.%d): a0=%llx a1=%llx a2=%llx a3=%llx sp=%x\n", pc, GPR_U32(ctx, 31), q.quante, reg64(ctx, 4),
                            reg64(ctx, 5), reg64(ctx, 6), reg64(ctx, 7), GPR_U32(ctx, 29));
            }
        // SOR_GUARDIA=<ind hex>[,<ind hex>...]: dice chi cambia quelle parole di RAM (controllate a
        // ogni chiamata di funzione; indirizzi allineati a 4). Al massimo 16 righe per parola.
        struct Guardia { uint32_t a, v; int n; };
        static std::vector<Guardia> s_guardie = []()
        {
            std::vector<Guardia> v;
            const char *e = std::getenv("SOR_GUARDIA");
            while (e && *e)
            {
                char *fine = nullptr;
                v.push_back({static_cast<uint32_t>(std::strtoul(e, &fine, 16)), 0, 0});
                e = (*fine == ',') ? fine + 1 : nullptr;
            }
            return v;
        }();
        for (Guardia &q : s_guardie)
        {
            const uint32_t v = leggi32(rdram, q.a);
            if (v != q.v)
            {
                if (q.n < 16)
                {
                    ++q.n;
                    std::printf("[sor] guardia [0x%x]: 0x%x -> 0x%x, prima di 0x%x (ra 0x%x, sp=%x, a0=%x a1=%x)\n", q.a, q.v, v, pc, GPR_U32(ctx, 31),
                                GPR_U32(ctx, 29), GPR_U32(ctx, 4), GPR_U32(ctx, 5));
                    std::fflush(stdout);
                }
                q.v = v;
            }
        }
        // Le interruzioni non entrano nell'anello: quando il gioco si blocca coprirebbero tutto.
        const bool diInterruzione = pc == 0x1e8f50u || pc == 0x281e30u || pc == 0x10fba8u || pc == 0x10ec20u || pc == 0x119c50u || pc == 0x119a00u || pc == 0x119690u;
        if (!diInterruzione)
        {
            g_anello[g_numChiamate % kAnello] = {pc, GPR_U32(ctx, 31), GPR_U32(ctx, 4), GPR_U32(ctx, 5)};
            ++g_numChiamate;
        }
        if (s < g_conteggi.size())
            ++g_conteggi[s];
        const uint32_t ra = GPR_U32(ctx, 31);
        const uint32_t sp = GPR_U32(ctx, 29);
        const uint32_t a0Entrata = GPR_U32(ctx, 4);
        g_originali[s](rdram, ctx, rt);
        if (spia)
        {
            std::printf("[sor] spia 0x%x -> v0=%llx\n", pc, reg64(ctx, 2));
            // SOR_SPIA_MAT=1: anche la matrice 4x4 scritta all'indirizzo a0 (per le funzioni sceVu0*).
            static const bool s_mat = std::getenv("SOR_SPIA_MAT") != nullptr;
            if (s_mat)
                for (uint32_t r = 0; r < 4; ++r)
                {
                    float f[4];
                    std::memcpy(f, rdram + ((a0Entrata + r * 16u) & PS2_RAM_MASK), 16);
                    std::printf("[sor] spia    %g %g %g %g\n", f[0], f[1], f[2], f[3]);
                }
            std::fflush(stdout);
        }
        // Ritorno anomalo: la funzione e' finita (sp tornato com'era) ma il pc non e' il ra
        // con cui era stata chiamata.
        static int anomali = 0;
        if (ctx->pc != ra && GPR_U32(ctx, 29) == sp && anomali < 40 && ctx->pc != pc)
        {
            ++anomali;
            std::printf("[sor] ritorno anomalo: 0x%x chiamata con ra=0x%x sp=0x%x -> pc=0x%x\n", pc, ra, sp, ctx->pc);
            std::fflush(stdout);
        }
    }

    void exitTracciato(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        std::printf("[sor] exit(%d) chiamata da ra=0x%x\n", static_cast<int>(GPR_U32(ctx, 4)), GPR_U32(ctx, 31));
        sorStampaTraccia();
        involucro(rdram, ctx, rt);
    }
}

void sorStampaTraccia()
{
    if (g_originali.empty())
        return;
    const uint64_t n = g_numChiamate;
    const uint64_t da = n > kAnello ? n - kAnello : 0;
    std::printf("[sor] traccia: %llu chiamate; ultime %llu:\n", (unsigned long long)n, (unsigned long long)(n - da));
    for (uint64_t i = da; i < n; ++i)
    {
        const Chiamata &c = g_anello[i % kAnello];
        std::printf("[sor]  T 0x%x da 0x%x a0=0x%x a1=0x%x\n", c.fn, c.ra, c.a0, c.a1);
    }
    std::vector<std::pair<uint32_t, uint32_t>> top;
    for (uint32_t s = 0; s < g_conteggi.size(); ++s)
        if (g_conteggi[s])
            top.push_back({g_conteggi[s], g_ps2RecompiledFunctionTableBase + s * 4u});
    std::sort(top.rbegin(), top.rend());
    std::printf("[sor] funzioni piu' chiamate (%zu diverse):\n", top.size());
    for (size_t i = 0; i < top.size() && i < 4000; ++i)
        std::printf("[sor]  C %u x 0x%x\n", top[i].first, top[i].second);
    std::fflush(stdout);
}

// 3. Filmati: il runtime e' senza FFmpeg, quindi sceMpegGetPicture aspetterebbe per sempre.
//    Cms_MovieGetPicture (0x30F9F0) dice subito "filmato finito" (bit 0x10 di [a0+0xA], come fa
//    l'originale quando sceMpegIsEnd risponde si'): il gioco salta il filmato e va avanti.
extern std::atomic<long long> g_sorAncoraMs; // Stubs/Pad.cpp

namespace
{
    void filmatoFinito(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        const uint32_t film = GPR_U32(ctx, 4);
        const uint32_t a = (film + 0xAu) & PS2_RAM_MASK;
        uint16_t flag;
        std::memcpy(&flag, rdram + a, 2);
        flag |= 0x10u;
        std::memcpy(rdram + a, &flag, 2);
        static int quante = 0;
        if (quante == 0)
            g_sorAncoraMs = (long long)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (quante < 4)
        {
            ++quante;
            std::printf("[sor] filmato saltato (stream 0x%x)\n", film);
            std::fflush(stdout);
        }
        SET_GPR_S32(ctx, 2, 1);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Il compito dei filmati finisce subito, senza aprire il filmato. Prima il filmato partiva e si diceva solo "finito":
    // - lasciando il gioco fermo sul titolo parte il filmato di presentazione, e i suoi dati continuavano ad arrivare in
    //   memoria sopra il modello caricato dopo New Game: Mod_mapping scriveva dentro una tabella di salti e il gioco si bloccava;
    // - a 60 Hz classMOVIE_TASK::task_move non passava mai dal punto che controlla "finito" e lo schermo restava nero.
    // Stato 3: Movie_task_main chiude da solo il compito. Il bit "filmato visto" e' quello di classMOVIE_TASK::task_move_wait.
    void filmatoNiente(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        const uint32_t compito = GPR_U32(ctx, 4) & PS2_RAM_MASK;
        rdram[compito] = 3u;
        rdram[compito + 1u] = 0u;
        rdram[compito + 2u] = 0u;
        rdram[compito + 3u] = 0u;
        const uint32_t film = rdram[0x4BE2DAu];
        uint32_t visti;
        const uint32_t dove = 0x551A44u + (film >> 5) * 4u;
        std::memcpy(&visti, rdram + dove, 4);
        visti |= 1u << (film & 31u);
        std::memcpy(rdram + dove, &visti, 4);
        g_sorAncoraMs = (long long)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        static int quanti = 0;
        if (quanti < 8)
        {
            ++quanti;
            std::printf("[sor] filmato %u saltato\n", film);
            std::fflush(stdout);
        }
        ctx->pc = GPR_U32(ctx, 31);
    }

    // 4. La scelta dei 60 Hz resta fra un avvio e l'altro. Il gioco la tiene nel byte 0x551402 (1 = 60 Hz, 2 = 50 Hz) e
    //    classPALSELECT_TASK::move_init la applica: basta metterla li' prima del primo move_init. Si scrive quando il
    //    giocatore ha premuto Start Game. Senza il file e senza scegliere 60 Hz non cambia niente e il file non nasce.
    const uint32_t kByteFrequenza = 0x551402u;
    PS2Runtime::RecompiledFunction g_palInit = nullptr, g_palAvviso = nullptr, g_palCard = nullptr;
    int g_videoNelFile = 0; // 0 = niente file, 1 = 60 Hz, 2 = 50 Hz

    std::string fileImpostazioni()
    {
        const char *e = std::getenv("SOR_IMPOSTAZIONI");
        return e && *e ? e : "settings.ini"; // accanto a sor.exe: launcher e script partono da quella cartella
    }

    void leggiImpostazioni()
    {
        std::ifstream f(fileImpostazioni());
        std::string riga;
        while (std::getline(f, riga))
        {
            if (riga.rfind("video=60", 0) == 0)
                g_videoNelFile = 1;
            else if (riga.rfind("video=50", 0) == 0)
                g_videoNelFile = 2;
        }
    }

    void palInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        static bool fatto = false;
        if (!fatto)
        {
            fatto = true;
            if (g_videoNelFile)
            {
                rdram[kByteFrequenza] = static_cast<uint8_t>(g_videoNelFile);
                std::printf("[sor] impostazioni: si parte a %d Hz\n", g_videoNelFile == 1 ? 60 : 50);
                std::fflush(stdout);
            }
        }
        g_palInit(rdram, ctx, rt);
    }

    void ricordaVideo(uint8_t *rdram)
    {
        static bool fatto = false;
        if (fatto)
            return;
        fatto = true;
        const int scelta = rdram[kByteFrequenza];
        if ((scelta != 1 && scelta != 2) || scelta == g_videoNelFile || (scelta == 2 && g_videoNelFile == 0))
            return;
        std::ofstream f(fileImpostazioni(), std::ios::trunc);
        f << "video=" << (scelta == 1 ? 60 : 50) << "\n";
        std::printf("[sor] impostazioni: scelta ricordata, %d Hz\n", scelta == 1 ? 60 : 50);
        std::fflush(stdout);
    }

    void palAvviso(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        ricordaVideo(rdram);
        g_palAvviso(rdram, ctx, rt);
    }

    void palCard(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        ricordaVideo(rdram);
        g_palCard(rdram, ctx, rt);
    }
}

void sorInstallaSostituzioni(PS2Runtime &rt)
{
    rt.registerFunction(0x120190u, cercaFile); // sceCdSearchFile
    rt.registerFunction(0x11FE90u, cercaFile); // sceCdLayerSearchFile
    if (std::getenv("SOR_SENZAFILMATI")) // for tests: the movies are skipped, the same keys always land on the same screens
    {
        rt.registerFunction(0x30F9F0u, filmatoFinito); // Cms_MovieGetPicture
        rt.registerFunction(0x309620u, filmatoNiente); // classMOVIE_TASK::task_init
    }
    leggiImpostazioni();
    g_palInit = rt.lookupFunction(0x32B7D0u);
    g_palAvviso = rt.lookupFunction(0x32B170u);
    g_palCard = rt.lookupFunction(0x32B330u);
    rt.registerFunction(0x32B7D0u, palInit);   // classPALSELECT_TASK::move_init
    rt.registerFunction(0x32B170u, palAvviso); // classPALSELECT_TASK::move_alert_disp
    rt.registerFunction(0x32B330u, palCard);   // classPALSELECT_TASK::move_memcd_check

    const char *mappa = std::getenv("SOR_TRACCIA");
    if (!mappa || !*mappa)
        return;
    std::ifstream f(mappa);
    if (!f)
    {
        std::fprintf(stderr, "[sor] SOR_TRACCIA: mappa %s non trovata\n", mappa);
        return;
    }
    if (const char *s = std::getenv("SOR_SENTINELLA"))
        g_sentFn = static_cast<uint32_t>(std::strtoul(s, nullptr, 16));
    if (const char *s = std::getenv("SOR_SENTINELLA_OFF"))
        g_sentOff = static_cast<uint32_t>(std::strtoul(s, nullptr, 16));
    g_originali.assign(g_ps2RecompiledFunctionTableSlotCount, nullptr);
    g_conteggi.assign(g_ps2RecompiledFunctionTableSlotCount, 0u);
    std::string riga;
    std::getline(f, riga);
    size_t avvolte = 0;
    while (std::getline(f, riga))
    {
        const size_t v1 = riga.find(',');
        if (v1 == std::string::npos)
            continue;
        const uint32_t a = static_cast<uint32_t>(std::strtoul(riga.c_str() + v1 + 1, nullptr, 16));
        if (a < g_ps2RecompiledFunctionTableBase || a >= g_ps2RecompiledFunctionTableEnd)
            continue;
        const uint32_t s = slotDi(a);
        PS2Runtime::RecompiledFunction fn = rt.lookupFunction(a);
        if (!fn || fn == involucro)
            continue;
        g_originali[s] = fn;
        rt.registerFunction(a, a == 0x124028u ? exitTracciato : involucro);
        ++avvolte;
    }
    std::printf("[sor] traccia attiva su %zu funzioni\n", avvolte);
}
