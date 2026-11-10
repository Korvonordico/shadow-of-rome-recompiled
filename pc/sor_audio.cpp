// Smooth audio (launcher: OPTIONS > SMOOTH AUDIO; SOR_AUDIO_STRETCH=0 turns it off, anything else or nothing = on).
//
// The SPU2 makes its samples following the game's clock (iop_spu2.cpp, IopSpu2::orologio). When the game runs below
// full speed (56-57 fps in heavy places instead of 60), it makes fewer samples than the speakers play, the queue
// empties and the missing samples were zeros: crackles. Here, on the speakers' side only, the samples go through
// SoundTouch (pc/soundtouch, LGPL-2.1, the time stretcher PCSX2 uses too), which plays them a little slower or faster
// with the same pitch, so the queue stays near a target and there are no holes.
//
// The tempo control is PCSX2's (pcsx2/Host/AudioStream.cpp: StretchWriteBlock, AddAndGetAverageTempo,
// UpdateStretchTempo, StretchUnderrun/Overrun, with the same constants and SoundTouch settings), adapted:
// - here the stretcher sits after the queue, in the speakers' callback: SoundTouch is fed 64 frames at a time from the
//   SPU2 queue when the speakers need more, and the fullness is the SPU2 queue plus what SoundTouch holds;
// - tempo between 0.5 and 2 (PCSX2: 0.05 and 50, it also has fast forward). The game runs between about 45 and 60 fps;
// - when the game stops (a load: no new samples for 50 ms) the tempo goes back to 1, so the last sound is not drawn
//   out; after a hole the sound waits for the queue to be back at the target and the control starts again from zero.
// At full speed the control goes "inactive" and the tempo is exactly 1.0. With the stretcher on, the old rule of
// sorSpu2Prendi (skip what stays above 60 ms) is not used: sorSpu2PrendiLiscio is the same queue without it, and the
// tempo control keeps the queue near its target instead.
//
// It does not change what the SPU2 makes nor g_sorSpuFatti (the clock the films are paced by).
// Everything after sorAudioAvvia() runs on the audio callback thread: no locks, no memory allocations.
// The log gets one "[sor-audio]" line at the start and one every 5 s (also when off), to read a tester's log.
// The off-game test (_tmp_correzioni/prova_audio) links this same file with a fake SPU2 queue.
#include "SoundTouch.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

size_t sorSpu2PrendiLiscio(int16_t *coppie, size_t n); // iop_spu2.cpp: the queue without the skip rule (nullptr: drop)
size_t sorSpu2Coda();
extern std::atomic<uint64_t> g_sorSpuFatti; // iop_spu2.cpp: samples the SPU2 has made so far

namespace
{
    constexpr uint32_t kRate = 48000u;
    // PCSX2's constants (AudioStream.h / AudioStream.cpp).
    constexpr uint32_t kChunk = 64u;               // CHUNK_SIZE: frames fed to SoundTouch per tempo update
    constexpr uint32_t kAveragingBuffer = 256u;    // AVERAGING_BUFFER_SIZE
    constexpr uint32_t kAveragingWindow = 50u;     // AVERAGING_WINDOW
    constexpr uint32_t kResetThreshold = 5u;       // STRETCH_RESET_THRESHOLD
    constexpr float kInactiveGood = 1.04f;         // INACTIVE_GOOD_FACTOR
    constexpr float kInactiveBad = 1.2f;           // INACTIVE_BAD_FACTOR
    constexpr uint32_t kInactiveMinOk = 50u;       // INACTIVE_MIN_OK_COUNT
    constexpr float kCompensationDivider = 100.0f; // COMPENSATION_DIVIDER
    // PCSX2's default stretch settings (DEFAULT_STRETCH_SEQUENCE_LENGTH, _SEEKWINDOW, _OVERLAP; quick seek and the
    // anti-alias filter off).
    constexpr int kSequenceMs = 30, kSeekWindowMs = 20, kOverlapMs = 10;
    // Ours.
    constexpr float kMinTempo = 0.5f, kMaxTempo = 2.0f;
    // What SoundTouch must hold to make its next piece of sound, with the settings above at tempo 1: a sequence
    // (30 ms) plus the seek window (20 ms). PCSX2 measures the buffer after SoundTouch, which can go down to zero;
    // here the buffer is before it, so the control works on what is there beyond these 50 ms (the margin), which
    // goes down to zero when the sound would have a hole.
    constexpr uint32_t kStruttura = kRate * (kSequenceMs + kSeekWindowMs) / 1000u;
    constexpr uint32_t kTargetMsDefault = 15u; // margin target (SOR_AUDIO_STRETCH_MS to try others)
    constexpr uint32_t kOverrun = kRate / 4u;  // above 250 ms the excess is dropped (PCSX2: a full buffer)
    constexpr uint32_t kStall = kRate / 20u;   // 50 ms without new samples from the SPU2: the game has stopped
    constexpr uint32_t kOutMax = 1024u;        // frames received from SoundTouch at a time
    constexpr uint32_t kReport = kRate * 5u;   // one log line every 5 s of speaker time

    soundtouch::SoundTouch *g_st = nullptr;
    bool g_acceso = false;
    uint32_t g_obiettivo = kRate * kTargetMsDefault / 1000u; // margin target, in frames (PCSX2's target buffer)

    // PCSX2's control state (m_stretch_reset, m_stretch_inactive, m_stretch_ok_count, m_dynamic_target_usage,
    // m_average_fullness, m_average_position, m_average_available).
    uint32_t g_reset = kResetThreshold;
    bool g_inattivo = false;
    uint32_t g_okConta = 0u;
    float g_dinamico = 0.0f;
    float g_medie[kAveragingBuffer] = {};
    uint32_t g_mediaPos = 0u, g_mediaDisp = 0u;

    bool g_riempie = true;        // zeros until the queue is at the target (start, and after a hole)
    uint64_t g_ultimiFatti = 0u;  // g_sorSpuFatti at the last callback
    uint64_t g_fermo = 0u;        // speaker frames since the SPU2 last made samples

    int16_t g_in16[kChunk * 2u];
    float g_inF[kChunk * 2u];
    float g_outF[kOutMax * 2u];

    // Totals (sorAudioTotali, for the test) and the 5 s report.
    struct Rapporto
    {
        uint64_t dati = 0u, vuoti = 0u;
        float tempoMin = 1e9f, tempoMax = 0.0f;
        double tempoSomma = 0.0;
        uint64_t tempoConti = 0u;
        uint32_t pienoMin = UINT32_MAX;
        double pienoSomma = 0.0;
        uint64_t pienoConti = 0u;
    };
    Rapporto g_rap5, g_totale;
    uint64_t g_fattiInizio = 0u;
    bool g_fattiPreso = false;

    template <typename T>
    inline bool inRange(T v, T lo, T hi) { return lo <= v && v <= hi; }

    // Fullness: the SPU2 queue plus what SoundTouch holds (not yet processed and ready), in frames.
    uint32_t pieno()
    {
        return static_cast<uint32_t>(sorSpu2Coda()) + g_st->numUnprocessedSamples() + g_st->numSamples();
    }
    // The margin the control works on: measured at the start of each callback, minus the block the speakers are
    // taking now, so it is what will be left after this block. Measured in the middle of the block (while
    // SoundTouch is fed), it changed with where in the block it was taken, and with 1024-frame blocks the tempo
    // swung between 0.5 and 1.25 in the test.
    uint32_t g_margineDopo = 0u;

    void contaTempo(float t)
    {
        for (Rapporto *r : {&g_rap5, &g_totale})
        {
            r->tempoMin = std::min(r->tempoMin, t);
            r->tempoMax = std::max(r->tempoMax, t);
            r->tempoSomma += t;
            ++r->tempoConti;
        }
    }

    void contaPieno(uint32_t p)
    {
        for (Rapporto *r : {&g_rap5, &g_totale})
        {
            r->pienoMin = std::min(r->pienoMin, p);
            r->pienoSomma += p;
            ++r->pienoConti;
        }
    }

    void contaUscita(size_t frames, size_t dati)
    {
        for (Rapporto *r : {&g_rap5, &g_totale})
        {
            r->dati += dati;
            r->vuoti += frames - dati;
        }
        const uint64_t fatti = g_sorSpuFatti.load(std::memory_order_relaxed);
        if (!g_fattiPreso)
        {
            g_fattiPreso = true;
            g_fattiInizio = fatti;
        }
        const uint64_t usciti = g_rap5.dati + g_rap5.vuoti;
        if (usciti < kReport)
            return;
        char tempo[64];
        if (g_acceso && g_rap5.tempoConti)
            std::snprintf(tempo, sizeof(tempo), "%.3f/%.3f/%.3f", g_rap5.tempoMin, g_rap5.tempoSomma / g_rap5.tempoConti, g_rap5.tempoMax);
        else
            std::snprintf(tempo, sizeof(tempo), "%s", g_acceso ? "-" : "off");
        const double pienoMin = g_rap5.pienoConti ? g_rap5.pienoMin * 1000.0 / kRate : 0.0;
        const double pienoMedio = g_rap5.pienoConti ? g_rap5.pienoSomma / g_rap5.pienoConti * 1000.0 / kRate : 0.0;
        std::printf("[sor-audio] 5 s: given %llu, missing %llu | tempo min/avg/max %s | queue min/avg %.0f/%.0f ms | spu2 made %.1f%% of real time\n",
                    static_cast<unsigned long long>(g_rap5.dati), static_cast<unsigned long long>(g_rap5.vuoti), tempo, pienoMin, pienoMedio,
                    100.0 * static_cast<double>(fatti - g_fattiInizio) / static_cast<double>(usciti));
        std::fflush(stdout);
        g_rap5 = Rapporto{};
        g_fattiInizio = fatti;
    }

    // PCSX2 AudioStream::AddAndGetAverageTempo.
    float mediaTempo(float val)
    {
        if (g_reset >= kResetThreshold)
            g_mediaDisp = 0u;
        if (g_mediaDisp < kAveragingBuffer)
            g_mediaDisp++;
        g_medie[g_mediaPos] = val;
        g_mediaPos = (g_mediaPos + 1u) % kAveragingBuffer;
        const uint32_t finestra = std::min(g_mediaDisp, kAveragingWindow);
        const uint32_t primo = (g_mediaPos - finestra + kAveragingBuffer) % kAveragingBuffer;
        float somma = 0.0f;
        for (uint32_t i = primo; i < primo + finestra; i++)
            somma += g_medie[i % kAveragingBuffer];
        somma = somma / finestra;
        return (somma != 0.0f) ? somma : 1.0f;
    }

    // PCSX2 AudioStream::UpdateStretchTempo (nominal rate 1.0), once per chunk fed to SoundTouch.
    void aggiornaTempo()
    {
        float base = static_cast<float>(g_obiettivo);
        if (g_reset >= kResetThreshold)
        {
            g_inattivo = false;
            g_okConta = 0u;
            g_dinamico = base;
        }
        float tempo = static_cast<float>(g_margineDopo) / g_dinamico;
        tempo = mediaTempo(tempo);
        if (tempo < 2.0f) // dampening near the target
            tempo = std::sqrt(tempo);
        tempo = std::clamp(tempo, kMinTempo, kMaxTempo);
        // Ours: while inactive (tempo 1) the dynamic target stays at the base target. In PCSX2 it keeps moving, and
        // in the test with small timing jitter at full speed a small offset left by the last active stretch made it
        // run away until the tempo jumped to 1.2 or 0.83, and so on every few seconds.
        if (!g_inattivo)
        {
            if (tempo < 1.0f)
                base /= std::sqrt(tempo);
            g_dinamico += (base / tempo - g_dinamico) / kCompensationDivider;
            if (inRange(tempo, 0.9f, 1.1f) && inRange(g_dinamico, base * 0.9f, base * 1.1f))
                g_dinamico = base;
            if (inRange(tempo, 1.0f / kInactiveGood, kInactiveGood))
                g_okConta++;
            else
                g_okConta = 0u;
            if (g_okConta >= kInactiveMinOk)
            {
                g_inattivo = true;
                g_dinamico = static_cast<float>(g_obiettivo);
            }
        }
        else if (!inRange(tempo, 1.0f / kInactiveBad, kInactiveBad))
        {
            g_inattivo = false;
            g_okConta = 0u;
        }
        if (g_inattivo || g_fermo >= kStall)
            tempo = 1.0f;
        g_st->setTempo(tempo);
        contaTempo(tempo);
        if (g_reset >= kResetThreshold)
            g_reset = 0u;
    }

    // Drops frames from the SPU2 queue (the oldest), up to n.
    void butta(size_t n)
    {
        sorSpu2PrendiLiscio(nullptr, n);
    }
}

// Reads SOR_AUDIO_STRETCH, prepares SoundTouch, writes the first log line. Called once, before the audio stream
// starts. Returns true when the smooth audio is on (then the callback calls sorAudioLiscio).
bool sorAudioAvvia()
{
    const char *e = std::getenv("SOR_AUDIO_STRETCH");
    g_acceso = !(e && e[0] == '0' && e[1] == '\0');
    if (const char *ms = std::getenv("SOR_AUDIO_STRETCH_MS"))
    {
        const int v = std::atoi(ms);
        if (v >= 5 && v <= 200)
            g_obiettivo = kRate * static_cast<uint32_t>(v) / 1000u;
    }
    if (g_acceso && !g_st)
    {
        g_st = new soundtouch::SoundTouch();
        // PCSX2 AudioStream::StretchAllocate.
        g_st->setSampleRate(kRate);
        g_st->setChannels(2);
        g_st->setSetting(SETTING_USE_QUICKSEEK, 0);
        g_st->setSetting(SETTING_USE_AA_FILTER, 0);
        g_st->setSetting(SETTING_SEQUENCE_MS, kSequenceMs);
        g_st->setSetting(SETTING_SEEKWINDOW_MS, kSeekWindowMs);
        g_st->setSetting(SETTING_OVERLAP_MS, kOverlapMs);
        // SoundTouch's buffers grow with new[] when they need more room: make them big now, at the slowest and
        // fastest tempo, so that the callback never allocates (they never shrink; clear() keeps them).
        static float s_zeri[8192u * 2u];
        for (const float t : {kMaxTempo, kMinTempo, 1.0f})
        {
            g_st->setTempo(t);
            g_st->putSamples(s_zeri, 8192u);
            while (g_st->receiveSamples(g_outF, kOutMax) != 0u)
            {
            }
        }
        g_st->clear();
        g_st->setTempo(1.0f);
    }
    if (g_acceso)
        std::printf("[sor-audio] smooth audio on (SoundTouch %s, queue target %u ms + %u ms held by SoundTouch)\n",
                    soundtouch::SoundTouch::getVersionString(), g_obiettivo * 1000u / kRate, kStruttura * 1000u / kRate);
    else
        std::printf("[sor-audio] smooth audio off (SOR_AUDIO_STRETCH=0): the old path, holes are zeros\n");
    std::fflush(stdout);
    return g_acceso;
}

// The speakers' callback with the smooth audio on: fills `frames` stereo frames, returns how many are sound
// (the rest are zeros, only when even SoundTouch has nothing).
size_t sorAudioLiscio(int16_t *out, size_t frames)
{
    const uint64_t fatti = g_sorSpuFatti.load(std::memory_order_relaxed);
    if (fatti != g_ultimiFatti)
    {
        g_ultimiFatti = fatti;
        g_fermo = 0u;
    }
    else
        g_fermo += frames;

    const uint32_t p = pieno();
    contaPieno(p);
    const uint32_t blocco = static_cast<uint32_t>(frames);
    const uint32_t pieno0 = kStruttura + g_obiettivo + blocco; // the fullness at the target
    g_margineDopo = p > kStruttura + blocco ? p - kStruttura - blocco : 0u;
    if (g_riempie)
    {
        if (p < pieno0)
        {
            std::memset(out, 0, frames * 4u);
            contaUscita(frames, 0u);
            return 0u;
        }
        // Clean start: what is above the target is dropped (it would only add delay), and the control starts again.
        butta(p - pieno0);
        g_margineDopo = g_obiettivo;
        g_riempie = false;
        g_reset = kResetThreshold;
    }
    else if (p > kOverrun)
    {
        // PCSX2 StretchOverrun (there: two chunks dropped). Here the queue only gets this full when the game runs
        // ahead of real time: back to the target at once.
        g_reset++;
        butta(p - pieno0);
        g_margineDopo = g_obiettivo;
    }

    size_t dati = 0u;
    while (dati < frames)
    {
        const uint32_t pronti = g_st->numSamples();
        if (pronti != 0u)
        {
            const uint32_t n = g_st->receiveSamples(g_outF, static_cast<uint32_t>(std::min<size_t>({pronti, frames - dati, kOutMax})));
            int16_t *o = out + dati * 2u;
            for (uint32_t i = 0; i < n * 2u; ++i)
            {
                const float v = std::clamp(g_outF[i] * 32768.0f, -32768.0f, 32767.0f);
                o[i] = static_cast<int16_t>(std::lrintf(v));
            }
            dati += n;
            continue;
        }
        // PCSX2 StretchWriteBlock: one chunk in, then the tempo update.
        const size_t presi = sorSpu2PrendiLiscio(g_in16, kChunk);
        if (presi == 0u)
            break;
        for (size_t i = 0; i < presi * 2u; ++i)
            g_inF[i] = static_cast<float>(g_in16[i]) * (1.0f / 32768.0f);
        g_st->putSamples(g_inF, static_cast<uint32_t>(presi));
        aggiornaTempo();
    }
    if (dati < frames)
    {
        // A hole: PCSX2 StretchUnderrun, and wait until the queue is back at the target.
        std::memset(out + dati * 2u, 0, (frames - dati) * 4u);
        g_reset++;
        g_riempie = true;
    }
    contaUscita(frames, dati);
    return dati;
}

// The speakers' callback with the smooth audio off (the old path, unchanged): only counts, for the 5 s line.
void sorAudioContaVecchio(size_t frames, size_t presi)
{
    contaPieno(static_cast<uint32_t>(sorSpu2Coda() + presi));
    contaUscita(frames, presi);
}

// For the off-game test: totals since the start.
void sorAudioTotali(uint64_t *dati, uint64_t *vuoti, double *tempoMin, double *tempoMedio, double *tempoMax, double *pienoMedioMs)
{
    *dati = g_totale.dati;
    *vuoti = g_totale.vuoti;
    *tempoMin = g_totale.tempoConti ? g_totale.tempoMin : 1.0;
    *tempoMedio = g_totale.tempoConti ? g_totale.tempoSomma / g_totale.tempoConti : 1.0;
    *tempoMax = g_totale.tempoConti ? g_totale.tempoMax : 1.0;
    *pienoMedioMs = g_totale.pienoConti ? g_totale.pienoSomma / g_totale.pienoConti * 1000.0 / kRate : 0.0;
}
void sorAudioAzzeraTotali() { g_totale = Rapporto{}; }
