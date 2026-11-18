// Iris Insight sentinel: cheap checks that run on EVERY frame, with no AI and no tokens. It looks for
//   - a black or near-black screen,
//   - the same picture while frames keep coming,
//   - flicker: a region that changes every other frame and comes back (or every two frames),
//   - a sudden flash of strong colours over a large part of the screen.
// Each frame is shrunk to 96 x 64 points (4 samples each) and compared with the two before it, in 12 x 8 regions,
// so a flickering menu is found even if the rest of the screen is still.
// Header-only, C++17, no memory allocated after the first frame. Used by blackbox.cpp (inside the program) and by
// tools/iris-sentinel.cpp (offline, on a folder of frames or a video). Not thread-safe: one caller.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sentinel
{
    struct Settings
    {
        // Each check can be turned off.
        bool black = true;
        bool samePicture = true;
        bool flicker = true;
        bool colours = true;

        // Black: a point is dark if its brightest channel is at most blackLevel (0-255); the screen is black when at
        // least blackShare of its points are dark. As ffmpeg's blackdetect (pix_th 0.10, picture_black_ratio_th 0.98).
        // Reported after blackSeconds: 15 as Iris's black save, because loads and fades are black for a few seconds
        // (4.8 s in a real Shadow of Rome video). ffmpeg's default is 2. Inside the program, Config::blackSeconds wins.
        int blackLevel = 24;
        double blackShare = 0.98;
        double blackSeconds = 15.0;

        // Same picture: the mean difference with the first picture of the run (0-255 per channel) stays at most
        // samePictureNoise for samePictureSeconds (ffmpeg's freezedetect: -60 dB, about 0.26). Menus and pauses can be
        // still for a long time: keep it long. Not reported while the screen is black (black already says it).
        double samePictureNoise = 0.5;
        double samePictureSeconds = 20.0;

        // Flicker: frame N looks like frame N-2 but not like frame N-1, in a region (12 x 8 regions). A region
        // flickers in a frame when any of these holds:
        //  - points: at least flickerShare of its points changed since the previous frame (one channel moved by more
        //    than flickerLevel) AND are back as they were two frames ago, and those are at least flickerBackShare of
        //    the points that changed (the rest of the region may move: a menu over a moving fight);
        //  - difference: its mean difference with the previous frame is at least flickerDiffLevel and the one with
        //    two frames ago is at most flickerDiffBack times that;
        //  - brightness: its mean brightness jumps by flickerMeanLevel and comes back (within 30% of the jump).
        // Reported when a region does it in at least flickerFrames - flickerMisses of the last flickerFrames frames.
        // Natural motion goes the other way (a frame looks more like the one just before it), so on real videos
        // (60 s of play, 75 s of film) "every other frame" never fired.
        int flickerLevel = 24;
        double flickerShare = 0.08;
        double flickerBackShare = 0.5;
        double flickerDiffLevel = 6.0;
        double flickerDiffBack = 0.5;
        double flickerMeanLevel = 8.0;
        int flickerFrames = 16; // at most 32
        int flickerMisses = 2;
        // Also A A B B A A B B: programs that show each picture twice. Only the difference and brightness rules
        // here: in a 15 frames/s film shown at 30, a waving crowd passes the points rule (a false alarm we saw).
        bool flickerEveryTwo = true;

        // Colours. A point is strong if its saturation is at least coloursSaturation and its brightest channel at
        // least coloursValue. A flash: the share of strong points jumps by at least coloursJump from one frame to the
        // next, and drops back within coloursMaxFrames (a jump that stays is a new scene, not a flash).
        double coloursSaturation = 0.6;
        int coloursValue = 100;
        double coloursJump = 0.20;
        int coloursMaxFrames = 30;
    };

    enum Kind
    {
        kBlack,
        kSamePicture,
        kFlicker,
        kColours,
        kKinds
    };

    inline const char *kindName(int kind)
    {
        static const char *const names[kKinds] = {"black", "same-picture", "flicker", "colours"};
        return kind >= 0 && kind < kKinds ? names[kind] : "?";
    }

    struct Finding
    {
        int kind = kBlack;
        bool over = false;             // false: found now; true: it ended
        uint64_t fromFrame = 0;        // where it began
        uint64_t atFrame = 0;          // the frame that confirmed it (or ended it)
        double fromSeconds = 0.0, atSeconds = 0.0;
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1; // the main area, as shares of the screen (0-1)
        int regions = 0;               // flicker: how many regions (of 12 x 8) flicker in it
        char text[320] = {};           // one line in words, for summaries
        char brief[110] = {};          // a shorter one, for logs with short lines
    };

    class Sentinel
    {
    public:
        static const int TW = 96, TH = 64;           // points of the shrunk frame
        static const int BW = 8, BH = 8;             // points of a region
        static const int BX = TW / BW, BY = TH / BH; // 12 x 8 regions
        static const int kBlocks = BX * BY, kPoints = TW * TH, kHistory = 5, kTimes = 64;

        void configure(const Settings &s)
        {
            m_set = s;
            m_set.flickerFrames = std::max(2, std::min(32, m_set.flickerFrames));
            m_set.flickerMisses = std::max(0, std::min(m_set.flickerFrames - 1, m_set.flickerMisses));
            reset();
        }
        const Settings &settings() const { return m_set; }

        // A part of the screen that blinks ON PURPOSE now (fractions 0..1 of the width and height; x1 <= x0: none),
        // e.g. a game's low-life warning on its life bar: the regions it touches do not count as flickering. The
        // rest of the screen is still checked. Call it on the thread of push(), before the frame.
        void setQuiet(double x0, double y0, double x1, double y1)
        {
            for (int b = 0; b < kBlocks; ++b)
            {
                const double bx0 = static_cast<double>(b % BX) / BX, by0 = static_cast<double>(b / BX) / BY;
                m_quiet[b] = x1 > x0 && y1 > y0 && bx0 < x1 && bx0 + 1.0 / BX > x0 && by0 < y1 && by0 + 1.0 / BY > y0;
            }
        }

        void reset()
        {
            m_count = m_base = 0;
            m_width = m_height = m_stride = 0;
            for (int k = 0; k < kKinds; ++k)
                m_has[k] = false;
            resetRuns();
        }

    private:
        // What compares a frame with the ones before it.
        void resetRuns()
        {
            m_black = m_blackRun = m_blackTold = false;
            m_sameValid = m_sameTold = false;
            m_lastDiff = 0.0;
            std::memset(m_bits, 0, sizeof(m_bits));
            std::memset(m_pointBack, 0, sizeof(m_pointBack));
            std::memset(m_flickerSeen, 0, sizeof(m_flickerSeen));
            m_flickerNow = 0;
            m_flickerOn = false;
            m_flickerLag = 1;
            m_flash = false;
            m_prevStrong = 0.0;
            m_strong = m_dark = 0.0;
        }

    public:
        // One frame: RGBA (alpha ignored), rows of `stride` bytes (0: width * 4). Writes up to `max` findings to
        // `out` and returns how many.
        int push(const uint8_t *rgba, int width, int height, int stride, uint64_t frame, double seconds, Finding *out, int max)
        {
            if (!rgba || width <= 0 || height <= 0)
                return 0;
            if (stride <= 0)
                stride = width * 4;
            if (width != m_width || height != m_height || stride != m_stride)
            {
                // A new size: the history no longer compares.
                m_width = width;
                m_height = height;
                m_stride = stride;
                for (int x = 0; x < TW; ++x)
                    for (int k = 0; k < 2; ++k)
                        m_xs[x][k] = static_cast<int>((static_cast<int64_t>(4 * x + 1 + 2 * k) * width) / (4 * TW)) * 4;
                for (int y = 0; y < TH; ++y)
                    for (int k = 0; k < 2; ++k)
                        m_ys[y][k] = static_cast<size_t>((static_cast<int64_t>(4 * y + 1 + 2 * k) * height) / (4 * TH)) * static_cast<size_t>(stride);
                m_base = m_count;
                resetRuns();
            }
            m_out = out;
            m_outMax = out ? max : 0;
            m_outCount = 0;
            m_frame = frame;
            m_seconds = seconds;

            uint8_t *cur = m_thumb[m_count % kHistory];
            shrink(rgba, cur);
            m_times[m_count % kTimes] = Time{frame, seconds};
            measure(cur);
            if (m_set.black)
                checkBlack();
            else
                m_black = false;
            if (m_set.samePicture)
                checkSame(cur);
            if (m_set.flicker)
                checkFlicker(cur);
            if (m_set.colours)
                checkColours();
            ++m_count;
            return m_outCount;
        }

        bool black() const { return m_black; }       // this frame is black or near-black
        double darkShare() const { return m_dark; }  // share of dark points in this frame
        double strongShare() const { return m_strong; }
        double lastDifference() const { return m_lastDiff; } // mean difference with the previous frame (0-255)
        // Something may be starting (flicker or a flash): worth keeping every frame for a moment.
        bool suspicious() const { return m_suspicious; }
        // The last finding of a kind (nullptr if none yet).
        const Finding *last(int kind) const { return kind >= 0 && kind < kKinds && m_has[kind] ? &m_last[kind] : nullptr; }

        // One line on the current state of every check.
        void status(char *out, size_t n) const
        {
            if (!out || n == 0)
                return;
            const double same = m_sameValid ? m_seconds - m_sameFromSeconds : 0.0;
            char flicker[64] = "none";
            if (m_flickerOn)
                std::snprintf(flicker, sizeof(flicker), "NOW (every %s, %d region%s)", m_flickerLag == 1 ? "other frame" : "two frames",
                              m_flickerNow, m_flickerNow == 1 ? "" : "s");
            std::snprintf(out, n, "dark points %.0f%%%s; same picture for %.1f s; flicker %s; strong colours %.0f%%%s",
                          m_dark * 100.0, m_black ? " (BLACK)" : "", same, flicker, m_strong * 100.0, m_flash ? " (FLASH)" : "");
        }

    private:
        struct Time
        {
            uint64_t frame;
            double seconds;
        };

        Settings m_set;
        int m_width = 0, m_height = 0, m_stride = 0;
        int m_xs[TW][2] = {};
        size_t m_ys[TH][2] = {};
        uint8_t m_thumb[kHistory][kPoints * 3] = {};
        Time m_times[kTimes] = {};
        uint64_t m_count = 0, m_base = 0; // frames pushed; first frame of the current size
        uint64_t m_frame = 0;
        double m_seconds = 0.0;
        Finding *m_out = nullptr;
        int m_outMax = 0, m_outCount = 0;
        Finding m_last[kKinds];
        bool m_has[kKinds] = {};
        bool m_suspicious = false;

        // Per frame
        double m_dark = 0.0, m_strong = 0.0;
        uint16_t m_blockStrong[kBlocks] = {}, m_prevBlockStrong[kBlocks] = {};
        float m_blockMean[kHistory][kBlocks] = {};
        uint16_t m_hue[6] = {};

        // Black
        bool m_black = false, m_blackRun = false, m_blackTold = false;
        uint64_t m_blackFrom = 0;
        double m_blackFromSeconds = 0.0, m_blackDarkMin = 1.0;

        // Same picture
        uint8_t m_same[kPoints * 3] = {};
        bool m_sameValid = false, m_sameTold = false;
        uint64_t m_sameFrom = 0;
        double m_sameFromSeconds = 0.0, m_lastDiff = 0.0;

        // Flicker: one bit per frame and region, for the two periods (every other frame, every two frames)
        uint32_t m_bits[2][kBlocks] = {};
        bool m_quiet[kBlocks] = {}; // setQuiet(): regions that blink on purpose now
        bool m_pointBack[kPoints] = {};  // every other frame: this point was back as two frames ago, in the last frame
        bool m_flickerSeen[kBlocks] = {}; // regions that flickered during the current run
        int m_flickerNow = 0;            // regions flickering now
        int m_flickerMost = 0;           // the most regions flickering at once during the current run
        bool m_flickerOn = false;
        int m_flickerLag = 1;
        uint64_t m_flickerFrom = 0;
        double m_flickerFromSeconds = 0.0;
        int m_flickerQuiet = 0;

        // Colours
        bool m_flash = false;
        int m_flashFrames = 0;
        uint64_t m_flashFrom = 0;
        double m_flashFromSeconds = 0.0, m_flashBase = 0.0, m_flashPeak = 0.0, m_prevStrong = 0.0;
        uint16_t m_flashBaseBlocks[kBlocks] = {};
        bool m_flashBlocks[kBlocks] = {};
        uint16_t m_flashHue[6] = {};

        bool have(int framesBack) const { return m_count - m_base >= static_cast<uint64_t>(framesBack); }
        const uint8_t *back(int framesBack) const { return m_thumb[(m_count - static_cast<uint64_t>(framesBack)) % kHistory]; }
        Time timeBack(int framesBack) const
        {
            const int n = static_cast<int>(std::min<uint64_t>(m_count - m_base, static_cast<uint64_t>(kTimes - 1)));
            return m_times[(m_count - static_cast<uint64_t>(std::min(framesBack, n))) % kTimes];
        }

        void shrink(const uint8_t *rgba, uint8_t *out) const
        {
            for (int y = 0; y < TH; ++y)
            {
                const uint8_t *r0 = rgba + m_ys[y][0], *r1 = rgba + m_ys[y][1];
                for (int x = 0; x < TW; ++x)
                {
                    const int a = m_xs[x][0], b = m_xs[x][1];
                    for (int c = 0; c < 3; ++c)
                        *out++ = static_cast<uint8_t>((r0[a + c] + r0[b + c] + r1[a + c] + r1[b + c] + 2) / 4);
                }
            }
        }

        static int hueSector(int r, int g, int b)
        {
            // 0 red, 1 yellow, 2 green, 3 cyan, 4 blue, 5 magenta (60 degrees each, centred on the name)
            const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
            const int d = mx - mn;
            if (d == 0)
                return 0;
            double h;
            if (mx == r)
                h = 60.0 * std::fmod((g - b) / static_cast<double>(d) + 6.0, 6.0);
            else if (mx == g)
                h = 60.0 * ((b - r) / static_cast<double>(d) + 2.0);
            else
                h = 60.0 * ((r - g) / static_cast<double>(d) + 4.0);
            return static_cast<int>(std::fmod(h + 30.0, 360.0) / 60.0) % 6;
        }

        void measure(const uint8_t *cur)
        {
            int dark = 0, strong = 0;
            float *means = m_blockMean[m_count % kHistory];
            std::memset(m_blockStrong, 0, sizeof(m_blockStrong));
            std::memset(m_hue, 0, sizeof(m_hue));
            for (int b = 0; b < kBlocks; ++b)
                means[b] = 0.0f;
            for (int y = 0; y < TH; ++y)
                for (int x = 0; x < TW; ++x)
                {
                    const uint8_t *p = cur + (y * TW + x) * 3;
                    const int mx = std::max(p[0], std::max(p[1], p[2])), mn = std::min(p[0], std::min(p[1], p[2]));
                    const int block = (y / BH) * BX + x / BW;
                    dark += mx <= m_set.blackLevel;
                    means[block] += static_cast<float>(p[0] * 2 + p[1] * 5 + p[2]) * 0.125f;
                    if (mx >= m_set.coloursValue && (mx - mn) >= m_set.coloursSaturation * mx)
                    {
                        ++strong;
                        ++m_blockStrong[block];
                        ++m_hue[hueSector(p[0], p[1], p[2])];
                    }
                }
            for (int b = 0; b < kBlocks; ++b)
                means[b] /= static_cast<float>(BW * BH);
            m_dark = dark / static_cast<double>(kPoints);
            m_strong = strong / static_cast<double>(kPoints);
            m_suspicious = false;
        }

        Finding *emit(int kind, bool over, uint64_t fromFrame, double fromSeconds)
        {
            Finding &f = m_last[kind];
            f = Finding{};
            f.kind = kind;
            f.over = over;
            f.fromFrame = fromFrame;
            f.fromSeconds = fromSeconds;
            f.atFrame = m_frame;
            f.atSeconds = m_seconds;
            m_has[kind] = true;
            return &f;
        }

        void publish(const Finding &f)
        {
            if (m_outCount < m_outMax)
                m_out[m_outCount++] = f;
        }

        // ---- Where on the screen ----

        struct Area
        {
            int bx0, by0, bx1, by1, blocks; // regions, inclusive
        };

        // Up to `max` groups of touching regions, the biggest first.
        static int groups(const bool *on, Area *areas, int max)
        {
            int label[kBlocks];
            for (int i = 0; i < kBlocks; ++i)
                label[i] = -1;
            Area found[kBlocks];
            int count = 0;
            int stack[kBlocks];
            for (int i = 0; i < kBlocks; ++i)
            {
                if (!on[i] || label[i] >= 0)
                    continue;
                Area a{i % BX, i / BX, i % BX, i / BX, 0};
                int top = 0;
                stack[top++] = i;
                label[i] = count;
                while (top)
                {
                    const int j = stack[--top];
                    const int x = j % BX, y = j / BX;
                    a.bx0 = std::min(a.bx0, x);
                    a.bx1 = std::max(a.bx1, x);
                    a.by0 = std::min(a.by0, y);
                    a.by1 = std::max(a.by1, y);
                    ++a.blocks;
                    const int next[4] = {x > 0 ? j - 1 : -1, x < BX - 1 ? j + 1 : -1, y > 0 ? j - BX : -1, y < BY - 1 ? j + BX : -1};
                    for (int k : next)
                        if (k >= 0 && on[k] && label[k] < 0)
                        {
                            label[k] = count;
                            stack[top++] = k;
                        }
                }
                found[count++] = a;
            }
            std::sort(found, found + count, [](const Area &l, const Area &r) { return l.blocks > r.blocks; });
            const int n = std::min(count, max);
            for (int i = 0; i < n; ++i)
                areas[i] = found[i];
            return count;
        }

        // "top centre (x 40-55%, y 10-20%; pixels 637-876 x 90-180 of 1592x896)"; level 1: without the pixels;
        // level 2: only "top centre".
        int describe(const Area &a, char *out, size_t n, int level = 0) const
        {
            const double x0 = a.bx0 / static_cast<double>(BX), x1 = (a.bx1 + 1) / static_cast<double>(BX);
            const double y0 = a.by0 / static_cast<double>(BY), y1 = (a.by1 + 1) / static_cast<double>(BY);
            const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
            char where[32];
            if (x1 - x0 >= 0.8 && y1 - y0 >= 0.8)
                std::snprintf(where, sizeof(where), "the whole screen");
            else
            {
                const char *v = cy < 1.0 / 3.0 ? "top" : (cy < 2.0 / 3.0 ? "middle" : "bottom");
                const char *h = cx < 1.0 / 3.0 ? "left" : (cx < 2.0 / 3.0 ? "centre" : "right");
                std::snprintf(where, sizeof(where), "%s %s", v, h);
            }
            if (level >= 2)
                return std::snprintf(out, n, "%s", where);
            if (level == 1)
                return std::snprintf(out, n, "%s (x %.0f-%.0f%%, y %.0f-%.0f%%)", where, x0 * 100.0, x1 * 100.0, y0 * 100.0, y1 * 100.0);
            return std::snprintf(out, n, "%s (x %.0f-%.0f%%, y %.0f-%.0f%%; pixels %d-%d x %d-%d of %dx%d)", where, x0 * 100.0, x1 * 100.0,
                                 y0 * 100.0, y1 * 100.0, static_cast<int>(x0 * m_width), static_cast<int>(x1 * m_width) - 1,
                                 static_cast<int>(y0 * m_height), static_cast<int>(y1 * m_height) - 1, m_width, m_height);
        }

        void setArea(Finding &f, const Area &a) const
        {
            f.x0 = a.bx0 / static_cast<float>(BX);
            f.x1 = (a.bx1 + 1) / static_cast<float>(BX);
            f.y0 = a.by0 / static_cast<float>(BY);
            f.y1 = (a.by1 + 1) / static_cast<float>(BY);
        }

        // ---- Checks ----

        void checkBlack()
        {
            m_black = m_dark >= m_set.blackShare;
            if (m_black)
            {
                if (!m_blackRun)
                {
                    m_blackRun = true;
                    m_blackTold = false;
                    m_blackFrom = m_frame;
                    m_blackFromSeconds = m_seconds;
                    m_blackDarkMin = 1.0;
                }
                m_blackDarkMin = std::min(m_blackDarkMin, m_dark);
                if (!m_blackTold && m_seconds - m_blackFromSeconds >= m_set.blackSeconds)
                {
                    m_blackTold = true;
                    Finding *f = emit(kBlack, false, m_blackFrom, m_blackFromSeconds);
                    std::snprintf(f->text, sizeof(f->text),
                                  "BLACK: the screen is black or near-black (at least %.1f%% of it at brightness %d or less) for %.1f s, since #%llu (%.2f s)",
                                  m_blackDarkMin * 100.0, m_set.blackLevel, m_seconds - m_blackFromSeconds,
                                  static_cast<unsigned long long>(m_blackFrom), m_blackFromSeconds);
                    std::snprintf(f->brief, sizeof(f->brief), "black (%.1f%% dark) for %.1f s since #%llu", m_blackDarkMin * 100.0,
                                  m_seconds - m_blackFromSeconds, static_cast<unsigned long long>(m_blackFrom));
                    publish(*f);
                }
            }
            else if (m_blackRun)
            {
                m_blackRun = false;
                if (m_blackTold)
                {
                    Finding *f = emit(kBlack, true, m_blackFrom, m_blackFromSeconds);
                    std::snprintf(f->text, sizeof(f->text), "black is over at #%llu (%.2f s), after %.1f s",
                                  static_cast<unsigned long long>(m_frame), m_seconds, m_seconds - m_blackFromSeconds);
                    std::snprintf(f->brief, sizeof(f->brief), "black is over, after %.1f s", m_seconds - m_blackFromSeconds);
                    publish(*f);
                }
            }
        }

        void checkSame(const uint8_t *cur)
        {
            if (have(1))
            {
                const uint8_t *prev = back(1);
                uint64_t sum = 0;
                for (int i = 0; i < kPoints * 3; ++i)
                    sum += static_cast<uint64_t>(std::abs(cur[i] - prev[i]));
                m_lastDiff = sum / static_cast<double>(kPoints * 3);
            }
            if (m_sameValid)
            {
                uint64_t sum = 0;
                for (int i = 0; i < kPoints * 3; ++i)
                    sum += static_cast<uint64_t>(std::abs(cur[i] - m_same[i]));
                if (sum / static_cast<double>(kPoints * 3) <= m_set.samePictureNoise)
                {
                    const double lasted = m_seconds - m_sameFromSeconds;
                    if (!m_sameTold && lasted >= m_set.samePictureSeconds && !(m_set.black && m_black))
                    {
                        m_sameTold = true;
                        Finding *f = emit(kSamePicture, false, m_sameFrom, m_sameFromSeconds);
                        std::snprintf(f->text, sizeof(f->text),
                                      "SAME PICTURE: frames keep coming but the picture has not changed for %.1f s, since #%llu (%.2f s); if keys were pressed meanwhile, it does not react",
                                      lasted, static_cast<unsigned long long>(m_sameFrom), m_sameFromSeconds);
                        std::snprintf(f->brief, sizeof(f->brief), "same picture for %.1f s since #%llu", lasted, static_cast<unsigned long long>(m_sameFrom));
                        publish(*f);
                    }
                    return;
                }
                if (m_sameTold)
                {
                    Finding *f = emit(kSamePicture, true, m_sameFrom, m_sameFromSeconds);
                    std::snprintf(f->text, sizeof(f->text), "the picture changes again at #%llu (%.2f s), after %.1f s",
                                  static_cast<unsigned long long>(m_frame), m_seconds, m_seconds - m_sameFromSeconds);
                    std::snprintf(f->brief, sizeof(f->brief), "the picture changes again, after %.1f s", m_seconds - m_sameFromSeconds);
                    publish(*f);
                }
            }
            std::memcpy(m_same, cur, sizeof(m_same));
            m_sameValid = true;
            m_sameTold = false;
            m_sameFrom = m_frame;
            m_sameFromSeconds = m_seconds;
        }

        // Is a region flickering in this frame, with period 2 * lag? (cur against lag and 2 * lag frames back)
        void flickerBits(const uint8_t *cur, int lag, uint32_t *bits)
        {
            if (!have(2 * lag))
            {
                for (int b = 0; b < kBlocks; ++b)
                    bits[b] <<= 1;
                return;
            }
            const uint8_t *a = back(lag), *c = back(2 * lag);
            uint16_t changed[kBlocks] = {}, back2[kBlocks] = {};
            uint32_t sum1[kBlocks] = {}, sum2[kBlocks] = {};
            const int level = m_set.flickerLevel;
            for (int y = 0; y < TH; ++y)
                for (int x = 0; x < TW; ++x)
                {
                    const int point = y * TW + x, i = point * 3;
                    int d1 = 0, d2 = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        d1 = std::max(d1, std::abs(cur[i + k] - a[i + k]));
                        d2 = std::max(d2, std::abs(cur[i + k] - c[i + k]));
                    }
                    const int block = (y / BH) * BX + x / BW;
                    sum1[block] += static_cast<uint32_t>(d1);
                    sum2[block] += static_cast<uint32_t>(d2);
                    if (d1 > level)
                    {
                        ++changed[block];
                        if (lag == 1)
                        {
                            // Back as two frames ago, here and in the frame before too: a small thing that moves
                            // fast (a star, a spark) passes a point once and does not count.
                            const bool now = d2 <= level;
                            back2[block] += now && m_pointBack[point];
                            m_pointBack[point] = now;
                        }
                    }
                    else if (lag == 1)
                        m_pointBack[point] = false;
                }
            const float *m0 = m_blockMean[m_count % kHistory];
            const float *m1 = m_blockMean[(m_count - static_cast<uint64_t>(lag)) % kHistory];
            const float *m2 = m_blockMean[(m_count - static_cast<uint64_t>(2 * lag)) % kHistory];
            const double points = BW * BH, need = m_set.flickerShare * points;
            for (int b = 0; b < kBlocks; ++b)
            {
                // points (only every other frame: see Settings)
                bool hit = lag == 1 && back2[b] >= need && back2[b] >= m_set.flickerBackShare * changed[b];
                // difference
                const double diff1 = sum1[b] / points, diff2 = sum2[b] / points;
                if (!hit && diff1 >= m_set.flickerDiffLevel && diff2 <= m_set.flickerDiffBack * diff1)
                    hit = true;
                // brightness
                const double jump = std::fabs(m0[b] - m1[b]), jumpBack = std::fabs(m0[b] - m2[b]);
                if (!hit && jump >= m_set.flickerMeanLevel && jumpBack <= 0.3 * jump)
                    hit = true;
                bits[b] = (bits[b] << 1) | (hit && !m_quiet[b] ? 1u : 0u);
            }
        }

        static int ones(uint32_t v)
        {
            int n = 0;
            for (; v; v &= v - 1)
                ++n;
            return n;
        }

        void checkFlicker(const uint8_t *cur)
        {
            flickerBits(cur, 1, m_bits[0]);
            if (m_set.flickerEveryTwo)
                flickerBits(cur, 2, m_bits[1]);
            const uint32_t window = m_set.flickerFrames >= 32 ? 0xFFFFFFFFu : ((1u << m_set.flickerFrames) - 1u);
            const int need = m_set.flickerFrames - m_set.flickerMisses;
            bool on[2][kBlocks];
            int firing[2] = {0, 0};
            bool recent = false;
            for (int l = 0; l < (m_set.flickerEveryTwo ? 2 : 1); ++l)
                for (int b = 0; b < kBlocks; ++b)
                {
                    const int n = ones(m_bits[l][b] & window);
                    on[l][b] = n >= need;
                    firing[l] += on[l][b];
                    recent = recent || (m_bits[l][b] & 0xFFu) != 0;
                    if (ones(m_bits[l][b] & 0xFu) >= 3)
                        m_suspicious = true;
                }
            if (!m_set.flickerEveryTwo)
                firing[1] = 0;
            m_flickerNow = std::max(firing[0], firing[1]);
            if (m_flickerOn)
                for (int b = 0; b < kBlocks; ++b)
                    m_flickerSeen[b] = m_flickerSeen[b] || on[m_flickerLag - 1][b];
            if (!m_flickerOn && (firing[0] || firing[1]))
            {
                const int l = firing[0] >= firing[1] ? 0 : 1;
                m_flickerOn = true;
                m_flickerLag = l + 1;
                m_flickerQuiet = 0;
                m_flickerMost = firing[l];
                std::memcpy(m_flickerSeen, on[l], sizeof(m_flickerSeen));
                const Time from = timeBack(m_set.flickerFrames - 1);
                m_flickerFrom = from.frame;
                m_flickerFromSeconds = from.seconds;
                flickerFound(on[l], firing[l], l, need, false);
            }
            else if (m_flickerOn)
            {
                // A run that began small (one region: a torch or a brazier fire animates every other frame, sor-giro
                // 16/11/2026, areas 011, 064, 0c5) and then spreads is told again, so the regions count of its finding
                // grows (the black box may save only from 2 regions up: Finding::regions).
                const int l = m_flickerLag - 1;
                if (firing[l] >= 2 && m_flickerMost < 2)
                    flickerFound(on[l], firing[l], l, need, true);
                m_flickerMost = std::max(m_flickerMost, firing[l]);
                m_flickerQuiet = recent ? 0 : m_flickerQuiet + 1;
                if (!recent)
                    flickerOver("is over");
            }
        }

        // A flicker finding: where, how many regions; grew = a run that began with one region and now has more.
        void flickerFound(const bool *on, int firing, int l, int need, bool grew)
        {
            Area areas[3];
            const int count = groups(on, areas, 3);
            Finding *f = emit(kFlicker, false, m_flickerFrom, m_flickerFromSeconds);
            setArea(*f, areas[0]);
            f->regions = firing;
            int used = std::snprintf(f->text, sizeof(f->text), "FLICKER%s: %d region%s change%s every %s and come%s back, in %d+ of the last %d frames, since about #%llu (%.2f s): ",
                                     grew ? " (it spread)" : "", firing, firing == 1 ? "" : "s", firing == 1 ? "s" : "", l == 0 ? "other frame" : "two frames",
                                     firing == 1 ? "s" : "", need, m_set.flickerFrames, static_cast<unsigned long long>(m_flickerFrom), m_flickerFromSeconds);
            for (int i = 0; i < std::min(count, 3) && used > 0 && used < static_cast<int>(sizeof(f->text)); ++i)
            {
                if (i)
                    used += std::snprintf(f->text + used, sizeof(f->text) - static_cast<size_t>(used), "; ");
                if (used < static_cast<int>(sizeof(f->text)))
                    used += describe(areas[i], f->text + used, sizeof(f->text) - static_cast<size_t>(used), i ? 1 : 0);
            }
            if (count > 3 && used > 0 && used < static_cast<int>(sizeof(f->text)))
                std::snprintf(f->text + used, sizeof(f->text) - static_cast<size_t>(used), "; and %d more places", count - 3);
            char where[40];
            describe(areas[0], where, sizeof(where), 2);
            std::snprintf(f->brief, sizeof(f->brief), "flicker every %s, %d region%s%s, mostly %s, since #%llu", l == 0 ? "other frame" : "two frames",
                          firing, firing == 1 ? "" : "s", grew ? " (spread)" : "", where, static_cast<unsigned long long>(m_flickerFrom));
            publish(*f);
        }

        // The end of a flicker run, with every place that flickered during it.
        void flickerOver(const char *how)
        {
            m_flickerOn = false;
            Area areas[3];
            const int count = groups(m_flickerSeen, areas, 3);
            int regions = 0;
            for (int b = 0; b < kBlocks; ++b)
                regions += m_flickerSeen[b];
            Finding *f = emit(kFlicker, true, m_flickerFrom, m_flickerFromSeconds);
            if (count > 0)
                setArea(*f, areas[0]);
            int used = std::snprintf(f->text, sizeof(f->text), "flicker %s at #%llu (%.2f s), after about %.1f s; %d region%s flickered in all: ", how,
                                     static_cast<unsigned long long>(m_frame), m_seconds, m_seconds - m_flickerFromSeconds, regions, regions == 1 ? "" : "s");
            for (int i = 0; i < std::min(count, 3) && used > 0 && used < static_cast<int>(sizeof(f->text)); ++i)
            {
                if (i)
                    used += std::snprintf(f->text + used, sizeof(f->text) - static_cast<size_t>(used), "; ");
                if (used < static_cast<int>(sizeof(f->text)))
                    used += describe(areas[i], f->text + used, sizeof(f->text) - static_cast<size_t>(used), 1);
            }
            if (count > 3 && used > 0 && used < static_cast<int>(sizeof(f->text)))
                std::snprintf(f->text + used, sizeof(f->text) - static_cast<size_t>(used), "; and %d more places", count - 3);
            char where[40] = "";
            if (count > 0)
                describe(areas[0], where, sizeof(where), 2);
            std::snprintf(f->brief, sizeof(f->brief), "flicker %s after about %.1f s, %d regions in all, mostly %s", how, m_seconds - m_flickerFromSeconds, regions, where);
            publish(*f);
        }

    public:
        // The frames are over (end of a file, the program closes): reports what is still going on, as "over" findings.
        int finish(Finding *out, int max)
        {
            m_out = out;
            m_outMax = out ? max : 0;
            m_outCount = 0;
            if (m_flickerOn)
                flickerOver("still going at the end");
            if (m_blackRun && m_blackTold)
            {
                Finding *f = emit(kBlack, true, m_blackFrom, m_blackFromSeconds);
                std::snprintf(f->text, sizeof(f->text), "black still going at the end, #%llu (%.2f s), after %.1f s",
                              static_cast<unsigned long long>(m_frame), m_seconds, m_seconds - m_blackFromSeconds);
                std::snprintf(f->brief, sizeof(f->brief), "black still going at the end, after %.1f s", m_seconds - m_blackFromSeconds);
                publish(*f);
            }
            if (m_sameValid && m_sameTold)
            {
                Finding *f = emit(kSamePicture, true, m_sameFrom, m_sameFromSeconds);
                std::snprintf(f->text, sizeof(f->text), "same picture still going at the end, #%llu (%.2f s), after %.1f s",
                              static_cast<unsigned long long>(m_frame), m_seconds, m_seconds - m_sameFromSeconds);
                std::snprintf(f->brief, sizeof(f->brief), "same picture still going at the end, after %.1f s", m_seconds - m_sameFromSeconds);
                publish(*f);
            }
            return m_outCount;
        }

    private:

        void checkColours()
        {
            if (!have(1))
            {
                m_prevStrong = m_strong;
                std::memcpy(m_prevBlockStrong, m_blockStrong, sizeof(m_blockStrong));
                return;
            }
            if (!m_flash)
            {
                if (m_strong - m_prevStrong >= m_set.coloursJump)
                {
                    m_flash = true;
                    m_flashFrames = 1;
                    m_flashFrom = m_frame;
                    m_flashFromSeconds = m_seconds;
                    m_flashBase = m_prevStrong;
                    m_flashPeak = m_strong;
                    std::memcpy(m_flashBaseBlocks, m_prevBlockStrong, sizeof(m_flashBaseBlocks));
                    std::memcpy(m_flashHue, m_hue, sizeof(m_hue));
                    for (int b = 0; b < kBlocks; ++b)
                        m_flashBlocks[b] = false;
                    markFlashBlocks();
                    m_suspicious = true;
                }
            }
            else
            {
                ++m_flashFrames;
                m_suspicious = true;
                if (m_strong > m_flashPeak)
                {
                    m_flashPeak = m_strong;
                    std::memcpy(m_flashHue, m_hue, sizeof(m_hue));
                }
                if (m_strong <= m_flashBase + m_set.coloursJump * 0.5)
                {
                    // Over: a flash if it was short.
                    m_flash = false;
                    if (m_flashFrames - 1 <= m_set.coloursMaxFrames)
                        reportFlash(m_flashFrames - 1);
                }
                else
                {
                    markFlashBlocks();
                    if (m_flashFrames > m_set.coloursMaxFrames)
                        m_flash = false; // it stays: a new scene, not a flash
                }
            }
            m_prevStrong = m_strong;
            std::memcpy(m_prevBlockStrong, m_blockStrong, sizeof(m_blockStrong));
        }

        void markFlashBlocks()
        {
            for (int b = 0; b < kBlocks; ++b)
                if (m_blockStrong[b] >= m_flashBaseBlocks[b] + BW * BH / 4)
                    m_flashBlocks[b] = true;
        }

        void reportFlash(int frames)
        {
            static const char *const kHues[6] = {"red", "yellow", "green", "cyan", "blue", "magenta"};
            Area areas[1];
            const int count = groups(m_flashBlocks, areas, 1);
            Finding *f = emit(kColours, false, m_flashFrom, m_flashFromSeconds);
            int total = 0, order[6] = {0, 1, 2, 3, 4, 5};
            for (int h = 0; h < 6; ++h)
                total += m_flashHue[h];
            std::sort(order, order + 6, [this](int a, int b) { return m_flashHue[a] > m_flashHue[b]; });
            char hues[96];
            int used = 0, kinds = 0;
            hues[0] = 0;
            for (int i = 0; i < 6 && total > 0; ++i)
            {
                const double share = m_flashHue[order[i]] / static_cast<double>(total);
                if (share < 0.1)
                    break;
                ++kinds;
                used += std::snprintf(hues + used, sizeof(hues) - static_cast<size_t>(used), "%s%s %.0f%%", i ? ", " : "", kHues[order[i]], share * 100.0);
            }
            char where[160] = "spread over the screen";
            if (count > 0)
            {
                setArea(*f, areas[0]);
                describe(areas[0], where, sizeof(where));
            }
            std::snprintf(f->text, sizeof(f->text), "COLOURS: a sudden flash of strong colours (%s%s) over up to %.0f%% of the screen, %s, for %d frame%s, #%llu (%.2f s) to #%llu",
                          kinds >= 3 ? "many colours: " : "", hues, m_flashPeak * 100.0, where, frames, frames == 1 ? "" : "s",
                          static_cast<unsigned long long>(m_flashFrom), m_flashFromSeconds, static_cast<unsigned long long>(timeBack(1).frame));
            char brief[40] = "spread";
            if (count > 0)
                describe(areas[0], brief, sizeof(brief), 2);
            std::snprintf(f->brief, sizeof(f->brief), "colour flash, %s%.0f%% of the screen, %s, %d frame%s from #%llu", kinds >= 3 ? "many colours, " : "",
                          m_flashPeak * 100.0, brief, frames, frames == 1 ? "" : "s", static_cast<unsigned long long>(m_flashFrom));
            publish(*f);
        }
    };
}
