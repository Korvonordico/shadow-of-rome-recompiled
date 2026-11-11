// See blackbox.h and README.md. Windows is the tested platform; elsewhere crashes are caught with signals.
#include "blackbox.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h" // public domain; its JPEG writer does not use the heap, useful after a crash

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace blackbox
{
    namespace
    {
        struct Slot
        {
            uint64_t frame;   // number of the frame when this image first appeared
            double seconds;
            uint32_t repeats; // samples showing this same image (or plain black)
            bool black;
        };

        const int kLineChars = 160;
        const int kLogLines = 1024;
        const int kLastEventsSaved = 300;     // lines in log.txt, so that it stays cheap to read
        const double kPeriodicKeptSeconds = 30.0; // speed and state lines older than this are left out
        const int kMaxSelected = 4096;

        struct LogLine
        {
            uint64_t frame;
            double seconds;
            bool periodic;
            char text[kLineChars];
        };

        struct Request
        {
            char reason[32];
            int images;
            bool extend; // same problem still going on: add older images to the same folder
            bool crash;
        };

        struct Stage
        {
            double afterSeconds;
            int images;
        };
        // The longer a problem lasts, the more images are saved.
        const Stage kFreezeStages[] = {{1.0, 50}, {3.0, 75}, {10.0, 100}}; // multiples of freezeSeconds
        const Stage kBlackStages[] = {{1.0, 25}, {2.0, 50}, {4.0, 100}};   // multiples of blackSeconds
        const int kCrashImages = 100;
        const int kSlowImages = 25;
        const int kDefaultImages = 25;

        Config g_cfg;
        std::atomic<bool> g_running{false};
        std::chrono::steady_clock::time_point g_start;

        // Images
        std::vector<uint8_t> g_pixels;
        std::vector<Slot> g_slots;
        std::vector<int> g_xmap, g_ymap;
        std::vector<uint8_t> g_sheet;
        std::vector<int> g_selected;
        size_t g_cellBytes = 0;
        int g_mapWidth = 0, g_mapHeight = 0;
        std::atomic<uint64_t> g_stored{0}; // different images stored so far
        double g_nextSample = 0.0;
        std::atomic<float> g_worstGap{0.0f}; // longest wait between two frames in the current second
        std::atomic<double> g_bestRate{0.0};
        std::atomic<uint64_t> g_frames{0};
        std::atomic<double> g_lastFrameAt{0.0};
        std::atomic<double> g_blackSince{0.0};
        std::atomic<bool> g_shownSomething{false};
        std::atomic<bool> g_storing{false};
        std::atomic<bool> g_paused{false};
        std::atomic<double> g_captureSeconds{0.0}; // time spent storing images, for the [speed] lines

        // Log
        std::vector<LogLine> g_log, g_logCopy;
        uint64_t g_logCount = 0;
        std::atomic_flag g_logLock = ATOMIC_FLAG_INIT;

        // For summary.txt: one record per second, written by the monitor
        struct Second
        {
            double t; // end of the second
            float fps, progress, worstMs;
            uint32_t pictures, loads, errors; // different images stored, loading lines, error lines
        };
        const int kSeconds = 128;
        Second g_secondRing[kSeconds];
        uint64_t g_secondCount = 0;
        std::atomic_flag g_secondLock = ATOMIC_FLAG_INIT;
        std::atomic<uint64_t> g_loadLines{0}, g_errorLines{0};
        std::atomic<bool> g_progressMoved{false};
        std::string g_sheetsList, g_tilesText; // of the current folder
        double g_tilesFrom = -1.0;              // time of the first tile of the newest sheet
        // When the save began: no image is stored while a save is written (it can take seconds on a busy PC),
        // so "the same picture for N s" must stop there.
        double g_saveStartedAt = 0.0;
        uint64_t g_saveStartFrame = 0;

        // Saves
        std::atomic_flag g_requestLock = ATOMIC_FLAG_INIT;
        Request g_request{};
        std::atomic<bool> g_requestPending{false};
        std::atomic<uint64_t> g_savesDone{0}, g_savesAsked{0};
        std::string g_lastFolder;
        char g_lastReason[32] = {};
        uint64_t g_savedOldest = 0, g_savedNewest = 0;
        std::atomic<int> g_reportWanted{0};

        std::atomic<bool> g_clean{false};
        std::atomic<bool> g_crashing{false};
        char g_crashText[2048] = {};
#ifdef _WIN32
        std::atomic<DWORD> g_saverThread{0};
#endif

        double now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
        }

        void sleepMs(int ms)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }

        void lowPriority()
        {
#ifdef _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        }

        // Bounded: after a crash the owner may never let go.
        bool lockFlag(std::atomic_flag &f)
        {
            for (int i = 0; i < 2000; ++i)
            {
                if (!f.test_and_set(std::memory_order_acquire))
                    return true;
                if (i > 50)
                    sleepMs(1);
            }
            return false;
        }

        bool startsWithAny(const char *text, const char *const *list)
        {
            if (list)
                for (; *list; ++list)
                    if (std::strncmp(text, *list, std::strlen(*list)) == 0)
                        return true;
            return false;
        }

        bool isError(const char *text)
        {
            static const char *const kWords[] = {"error", "fail", "missing", "exception", "fatal", nullptr};
            if (std::strncmp(text, "[blackbox]", 10) == 0)
                return false;
            char lower[kLineChars], word[32];
            size_t i = 0;
            for (; text[i] && i < sizeof(lower) - 1; ++i)
                lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            lower[i] = 0;
            for (const char *const *w = g_cfg.errorWords ? g_cfg.errorWords : kWords; *w; ++w)
            {
                size_t j = 0;
                for (; (*w)[j] && j < sizeof(word) - 1; ++j)
                    word[j] = static_cast<char>(std::tolower(static_cast<unsigned char>((*w)[j])));
                word[j] = 0;
                // Every place the word appears, but not a count of nothing: "missing 0", "errors 0" (a report
                // line saying all is well, as [sor-audio]'s "given 240000, missing 0") is not an error.
                for (const char *at = j ? std::strstr(lower, word) : nullptr; at; at = std::strstr(at + 1, word))
                {
                    const char *after = at + j;
                    while (*after == ' ' || *after == ':' || *after == '=')
                        ++after;
                    if (!(after[0] == '0' && !std::isdigit(static_cast<unsigned char>(after[1])) && after[1] != '.'))
                        return true;
                }
            }
            return false;
        }

        void addLine(const char *text, bool periodic)
        {
            const bool locked = lockFlag(g_logLock);
            LogLine &l = g_log[static_cast<size_t>(g_logCount % kLogLines)];
            l.frame = g_frames.load(std::memory_order_relaxed);
            l.seconds = now();
            l.periodic = periodic;
            std::strncpy(l.text, text, kLineChars - 1);
            l.text[kLineChars - 1] = 0;
            if (!periodic)
            {
                if (startsWithAny(l.text, g_cfg.loadingLinesStartingWith))
                    g_loadLines.fetch_add(1, std::memory_order_relaxed);
                else if (isError(l.text))
                    g_errorLines.fetch_add(1, std::memory_order_relaxed);
            }
            ++g_logCount;
            if (locked)
                g_logLock.clear(std::memory_order_release);
        }

        // ---- Requests ----

        bool ask(const char *reason, int images, bool extend, bool crash)
        {
            const bool locked = lockFlag(g_requestLock);
            bool ok = false;
            if (crash || !g_requestPending.load())
            {
                std::snprintf(g_request.reason, sizeof(g_request.reason), "%s", reason);
                g_request.images = images;
                g_request.extend = extend;
                g_request.crash = crash;
                if (!g_requestPending.exchange(true))
                    g_savesAsked.fetch_add(1);
                ok = true;
            }
            if (locked)
                g_requestLock.clear(std::memory_order_release);
            return ok;
        }

        void askAndWait(const char *reason, int images)
        {
            // A save in progress finishes first: its end must not cancel this one.
            for (int i = 0; i < 300 && g_requestPending.load(); ++i)
                sleepMs(10);
            ask(reason, images, false, true);
            for (int i = 0; i < 2000 && g_savesDone.load() < g_savesAsked.load(); ++i)
                sleepMs(10);
        }

        // ---- Contact sheets ----

        // 5x7 digits, x, #, the point and s.
        const uint8_t kFont[14][7] = {
            {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},     {14, 17, 1, 2, 4, 8, 31},   {31, 2, 4, 2, 1, 17, 14},
            {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14},  {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
            {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12},  {0, 0, 17, 10, 4, 10, 17},  {10, 10, 31, 10, 31, 10, 10},
            {0, 0, 0, 0, 0, 12, 12},      {0, 0, 15, 16, 14, 1, 30}};

        void drawLabel(uint8_t *cell, int stride, const char *text)
        {
            const int scale = 2;
            const int n = static_cast<int>(std::strlen(text));
            const int w = n * 6 * scale + 4, h = 7 * scale + 4;
            for (int y = 0; y < h; ++y)
                std::memset(cell + y * stride, 0, static_cast<size_t>(w) * 3u);
            for (int i = 0; i < n; ++i)
            {
                const char c = text[i];
                const int g = c == 'x' ? 10 : (c == '#' ? 11 : (c == '.' ? 12 : (c == 's' ? 13 : c - '0')));
                if (g < 0 || g > 13)
                    continue;
                for (int y = 0; y < 7; ++y)
                    for (int x = 0; x < 5; ++x)
                        if (kFont[g][y] & (16 >> x))
                            for (int sy = 0; sy < scale; ++sy)
                                for (int sx = 0; sx < scale; ++sx)
                                    std::memset(cell + (2 + y * scale + sy) * stride + (2 + (i * 6 + x) * scale + sx) * 3, 255, 3);
            }
        }

        bool writeSheet(const std::string &folder, const int *slots, int count, std::string &name)
        {
            const int cols = g_cfg.columns;
            const int rows = (count + cols - 1) / cols;
            const int w = cols * g_cfg.cellWidth + (cols - 1) * g_cfg.cellGap;
            const int h = rows * g_cfg.cellHeight + (rows - 1) * g_cfg.cellGap;
            const int stride = w * 3;
            std::memset(g_sheet.data(), 90, static_cast<size_t>(stride) * h);
            for (int i = 0; i < count; ++i)
            {
                const Slot &s = g_slots[static_cast<size_t>(slots[i])];
                const uint8_t *src = g_pixels.data() + g_cellBytes * static_cast<size_t>(slots[i]);
                uint8_t *dst = g_sheet.data() + static_cast<size_t>((i / cols) * (g_cfg.cellHeight + g_cfg.cellGap)) * stride +
                               static_cast<size_t>((i % cols) * (g_cfg.cellWidth + g_cfg.cellGap)) * 3u;
                for (int y = 0; y < g_cfg.cellHeight; ++y)
                    std::memcpy(dst + y * stride, src + y * g_cfg.cellWidth * 3, static_cast<size_t>(g_cfg.cellWidth) * 3u);
                char label[40];
                if (s.repeats > 1)
                    std::snprintf(label, sizeof(label), "#%llux%u", static_cast<unsigned long long>(s.frame), s.repeats);
                else
                    std::snprintf(label, sizeof(label), "#%llu", static_cast<unsigned long long>(s.frame));
                drawLabel(dst, stride, label);
                // Seconds since the start, bottom left: the same clock as log.txt and summary.txt.
                std::snprintf(label, sizeof(label), "%.2fs", s.seconds);
                drawLabel(dst + static_cast<size_t>(g_cfg.cellHeight - 18) * stride, stride, label);
            }
            char file[64];
            std::snprintf(file, sizeof(file), "frames_%07llu-%07llu.jpg", static_cast<unsigned long long>(g_slots[static_cast<size_t>(slots[0])].frame),
                          static_cast<unsigned long long>(g_slots[static_cast<size_t>(slots[count - 1])].frame));
            name = file;
            return stbi_write_jpg((folder + "/" + file).c_str(), w, h, 3, g_sheet.data(), g_cfg.jpegQuality) != 0;
        }

        uint64_t copyLog()
        {
            const bool locked = lockFlag(g_logLock);
            const uint64_t count = g_logCount;
            std::memcpy(g_logCopy.data(), g_log.data(), sizeof(LogLine) * kLogLines);
            if (locked)
                g_logLock.clear(std::memory_order_release);
            return count;
        }

        // The frame rate in words: on a still screen, or one that looks fine, the pictures cannot show a slowdown.
        void writeSpeedLine(FILE *f, const std::vector<Second> &secs, double best, double sinceLastFrame)
        {
            if (sinceLastFrame >= 1.0)
            {
                std::fprintf(f, "speed: NO NEW FRAME for %.1f s (best %.0f/s)\n", sinceLastFrame, best);
                return;
            }
            const double limit = g_cfg.slowBelowPerSecond > 0.0 ? g_cfg.slowBelowPerSecond : best * 0.8;
            // The newest drop of at least 2 seconds in the last 30, and whether it is still going on.
            int low = 0;
            double sum = 0.0, until = 0.0;
            bool goingOn = false;
            for (auto it = secs.rbegin(); it != secs.rend() && it->t >= secs.back().t - 30.0; ++it)
            {
                if (it->fps < limit && it->fps >= 1.0f)
                {
                    if (!low)
                    {
                        until = it->t;
                        goingOn = it == secs.rbegin();
                    }
                    ++low;
                    sum += it->fps;
                }
                else if (low >= 2)
                    break;
                else
                {
                    low = 0;
                    sum = 0.0;
                }
            }
            if (low >= 2 && goingOn)
                std::fprintf(f, "speed: SLOW NOW, about %.0f frames/s for the last %d s, against a best of %.0f/s (%.0f%%). The pictures can look normal: this line is what tells it\n",
                             sum / low, low, best, 100.0 * sum / low / best);
            else if (low >= 2)
                std::fprintf(f, "speed: normal now, %.0f frames/s; it dropped to about %.0f/s for %d s, until %.1f s (best %.0f/s)\n", secs.back().fps, sum / low, low, until, best);
            else
                std::fprintf(f, "speed: normal, no drop in the last 30 s (best %.0f/s)\n", best);
        }

        // A few hundred tokens to read together with the sheets: what images alone cannot tell
        // (a long load or a freeze, a slowdown, the inputs, the errors).
        void writeSummary(const std::string &folder, const char *reason, uint64_t count)
        {
            FILE *f = std::fopen((folder + "/summary.txt").c_str(), "wb");
            if (!f)
                return;
            const double t = now();
            const uint64_t first = count > kLogLines ? count - kLogLines : 0;
            auto lineAt = [](uint64_t i) -> const LogLine & { return g_logCopy[static_cast<size_t>(i % kLogLines)]; };
            std::fprintf(f, "IRIS INSIGHT SUMMARY. Read it with the sheets. Times: seconds since the program started; #N: frame number, as on the tiles.\n");
            const double at = g_saveStartedAt > 0.0 ? g_saveStartedAt : t; // the moment described; later times are the save itself
            std::fprintf(f, "reason: %s; saved at #%llu, %.2f s (writing the files took %.1f s)\n", reason, static_cast<unsigned long long>(g_saveStartFrame), at, t - at);
            if (g_crashText[0])
                std::fprintf(f, "crash: %.*s\n", static_cast<int>(std::min<size_t>(std::strcspn(g_crashText, "\n"), 150)), g_crashText);

            std::vector<Second> secs;
            {
                const bool locked = lockFlag(g_secondLock);
                const uint64_t n = std::min<uint64_t>(g_secondCount, kSeconds);
                for (uint64_t i = g_secondCount - n; i < g_secondCount; ++i)
                    if (g_secondRing[static_cast<size_t>(i % kSeconds)].t <= at + 0.5) // not the seconds spent writing
                        secs.push_back(g_secondRing[static_cast<size_t>(i % kSeconds)]);
                if (locked)
                    g_secondLock.clear(std::memory_order_release);
            }
            const double lastFrameAt = g_lastFrameAt.load();
            const uint64_t stored = g_stored.load();
            if (stored > 0)
            {
                const Slot &s = g_slots[static_cast<size_t>((stored - 1) % static_cast<uint64_t>(g_cfg.ringImages))];
                std::fprintf(f, "picture: the newest different picture is #%llu at %.2f s%s; then the same picture for %.1f s\n",
                             static_cast<unsigned long long>(s.frame), s.seconds, s.black ? " (black)" : "", std::max(0.0, std::min(lastFrameAt, at) - s.seconds));
            }
            std::fprintf(f, "frames shown: %.1f/s in the last second, best %.1f/s; last frame %.1f s before the save\n",
                         secs.empty() ? 0.0 : secs.back().fps, g_bestRate.load(), std::max(0.0, at - lastFrameAt));
            writeSpeedLine(f, secs, g_bestRate.load(), at - lastFrameAt);
            if (g_cfg.progress)
            {
                int still = 0;
                float before = -1.0f;
                for (auto it = secs.rbegin(); it != secs.rend(); ++it)
                {
                    if (it->progress >= 0.5f)
                    {
                        before = it->progress;
                        break;
                    }
                    ++still;
                }
                if (!g_progressMoved.load())
                    std::fprintf(f, "logic (%s): the counter never moved\n", g_cfg.progressName);
                else if (still > 0)
                    std::fprintf(f, "logic (%s per second): STOPPED, 0 for the last %d s (%.0f/s before)\n", g_cfg.progressName, still, before);
                else
                    std::fprintf(f, "logic (%s per second): running, %.0f in the last second\n", g_cfg.progressName, before);
            }
            if (g_cfg.loadingLinesStartingWith)
            {
                double last = -1.0, from = -1.0;
                int reads = 0;
                for (uint64_t i = count; i > first; --i)
                {
                    const LogLine &l = lineAt(i - 1);
                    if (l.periodic || !startsWithAny(l.text, g_cfg.loadingLinesStartingWith))
                        continue;
                    if (last >= 0.0 && from - l.seconds > 3.0)
                        break;
                    if (last < 0.0)
                        last = l.seconds;
                    from = l.seconds;
                    ++reads;
                }
                if (last < 0.0)
                    std::fprintf(f, "loading: no read in the log\n");
                else if (at - last < 2.0 && reads >= g_cfg.loadingMinReads && last - from >= g_cfg.loadingMinSeconds)
                    std::fprintf(f, "loading: YES, reads going on for %.1f s (%d since %.2f s)\n", at - from, reads, from);
                else if (at - last < 2.0)
                    std::fprintf(f, "loading: no, only %d read%s around %.2f s (a load means reads that continue)\n", reads, reads == 1 ? "" : "s", last);
                else
                    std::fprintf(f, "loading: no, no read for the last %.1f s (the last group: %d read%s, %.2f-%.2f s)\n", at - last, reads, reads == 1 ? "" : "s", from, last);
            }
            if (g_cfg.scene)
            {
                const LogLine *a = nullptr, *b = nullptr;
                for (uint64_t i = count; i > first && !b; --i)
                    if (std::strncmp(lineAt(i - 1).text, "[scene] ", 8) == 0)
                        (a ? b : a) = &lineAt(i - 1);
                if (a)
                    std::fprintf(f, "scene: %s since %.2f s%s%s\n", a->text + 8, a->seconds, b ? "; before: " : "", b ? b->text + 8 : "");
            }
            if (!g_sheetsList.empty())
                std::fprintf(f, "sheets: %s\n", g_sheetsList.c_str());
            if (!g_tilesText.empty())
                std::fprintf(f, "newest sheet, tile by tile (xN: same picture for N samples; B: black):\n%s", g_tilesText.c_str());

            // Seconds since the first tile of the newest sheet; similar seconds in one line.
            const double from = g_tilesFrom >= 0.0 ? g_tilesFrom : t - 12.0;
            struct Run
            {
                double a, b, fps, progress, pictures;
                float worst;
                uint32_t loads, errors;
                int n, key[6];
            };
            std::vector<Run> runs;
            for (const Second &s : secs)
            {
                if (s.t < from)
                    continue;
                const int key[6] = {static_cast<int>(std::lround(s.fps / 5.0f)), static_cast<int>(std::lround(s.progress / 5.0f)),
                                    s.pictures == 0 ? 0 : (s.pictures <= 5 ? 1 : (s.pictures <= 15 ? 2 : 3)),
                                    s.worstMs < 40.0f ? 0 : (s.worstMs < 100.0f ? 1 : (s.worstMs < 1000.0f ? 2 : 3)), s.loads > 0, s.errors > 0};
                if (runs.empty() || std::memcmp(runs.back().key, key, sizeof(key)) != 0)
                {
                    runs.push_back(Run{s.t - 1.0, s.t, 0, 0, 0, 0.0f, 0, 0, 0, {}});
                    std::memcpy(runs.back().key, key, sizeof(key));
                }
                Run &r = runs.back();
                r.b = s.t;
                r.fps += s.fps;
                r.progress += s.progress;
                r.pictures += s.pictures;
                r.worst = std::max(r.worst, s.worstMs);
                r.loads += s.loads;
                r.errors += s.errors;
                ++r.n;
            }
            std::fprintf(f, "per second (fps: frames shown; logic: %s; new: different pictures among %d samples; worst: longest wait between two frames; reads: loading lines):\n",
                         g_cfg.progressName, g_cfg.imagesPerSecond);
            const size_t skip = runs.size() > 14 ? runs.size() - 14 : 0;
            if (skip)
                std::fprintf(f, "  (%zu earlier groups left out)\n", skip);
            for (size_t i = skip; i < runs.size(); ++i)
            {
                const Run &r = runs[i];
                std::fprintf(f, "  %.1f-%.1f s: fps %.0f", r.a, r.b, r.fps / r.n);
                if (g_cfg.progress)
                    std::fprintf(f, ", logic %.0f", r.progress / r.n);
                std::fprintf(f, ", new %.0f, worst %.0f ms", r.pictures / r.n, r.worst);
                if (r.loads)
                    std::fprintf(f, ", reads %u", r.loads);
                if (r.errors)
                    std::fprintf(f, ", errors %u", r.errors);
                std::fprintf(f, "\n");
            }
            if (g_cfg.input)
            {
                std::vector<const LogLine *> inputs;
                for (uint64_t i = count; i > first && inputs.size() < 6; --i)
                    if (std::strncmp(lineAt(i - 1).text, "[input] ", 8) == 0)
                        inputs.push_back(&lineAt(i - 1));
                std::fprintf(f, "inputs (last changes):");
                for (auto it = inputs.rbegin(); it != inputs.rend(); ++it)
                    std::fprintf(f, " %.2f %s;", (*it)->seconds, (*it)->text + 8);
                std::fprintf(f, "%s\n", inputs.empty() ? " none in the log" : "");
            }
            std::vector<const LogLine *> errors;
            for (uint64_t i = count; i > first && errors.size() < 3; --i)
                if (!lineAt(i - 1).periodic && isError(lineAt(i - 1).text))
                    errors.push_back(&lineAt(i - 1));
            std::fprintf(f, "errors (last 3):%s\n", errors.empty() ? " none in the log" : "");
            for (auto it = errors.rbegin(); it != errors.rend(); ++it)
                std::fprintf(f, "  #%llu %.2f %.110s\n", static_cast<unsigned long long>((*it)->frame), (*it)->seconds, (*it)->text);
            std::fprintf(f, "more: log.txt (last lines), state.txt (threads)\n");
            std::fclose(f);
        }

        void writeLog(const std::string &folder, uint64_t count)
        {
            FILE *f = std::fopen((folder + "/log.txt").c_str(), "wb");
            if (!f)
                return;
            const double t = now();
            const uint64_t first = count > kLogLines ? count - kLogLines : 0;
            // Walking back to find where the last kLastEventsSaved kept lines start.
            uint64_t start = count;
            int kept = 0;
            for (uint64_t i = count; i > first && kept < kLastEventsSaved; --i)
            {
                const LogLine &l = g_logCopy[static_cast<size_t>((i - 1) % kLogLines)];
                if (!l.periodic || t - l.seconds <= kPeriodicKeptSeconds)
                    ++kept;
                start = i - 1;
            }
            std::fprintf(f, "# frame number (same as on the images), seconds since the start, text\n");
            for (uint64_t i = start; i < count; ++i)
            {
                const LogLine &l = g_logCopy[static_cast<size_t>(i % kLogLines)];
                if (!l.periodic || t - l.seconds <= kPeriodicKeptSeconds)
                    std::fprintf(f, "#%llu %.2f %s\n", static_cast<unsigned long long>(l.frame), l.seconds, l.text);
            }
            std::fclose(f);
        }

        bool isSaveFolderName(const std::string &n)
        {
            // 2026-10-31_23-59-12_reason
            return n.size() > 20 && n[4] == '-' && n[7] == '-' && n[10] == '_' && n[13] == '-' && n[16] == '-' && n[19] == '_' &&
                   std::isdigit(static_cast<unsigned char>(n[0]));
        }

        void removeOldFolders()
        {
            std::error_code ec;
            std::vector<std::filesystem::path> found;
            for (const auto &e : std::filesystem::directory_iterator(g_cfg.folder, ec))
                if (e.is_directory(ec) && isSaveFolderName(e.path().filename().string()))
                    found.push_back(e.path());
            std::sort(found.begin(), found.end());
            // The oldest goes first, but a slowdown goes before a crash.
            while (found.size() > static_cast<size_t>(g_cfg.keepFolders))
            {
                auto victim = std::find_if(found.begin(), found.end() - 1, [](const std::filesystem::path &p)
                                           { return p.filename().string().find("_slow") != std::string::npos; });
                if (victim == found.end() - 1)
                    victim = found.begin();
                std::filesystem::remove_all(*victim, ec);
                found.erase(victim);
            }
        }

        void doSave(const Request &r)
        {
            const double t0 = now();
            g_saveStartedAt = t0;
            g_saveStartFrame = g_frames.load();
            g_paused.store(true);
            for (int i = 0; i < 200 && g_storing.load(); ++i)
                sleepMs(1);

            const bool extend = r.extend && !g_lastFolder.empty() && !std::strcmp(g_lastReason, r.reason);
            std::string folder = g_lastFolder;
            if (!extend)
            {
                char name[80];
                const std::time_t tt = std::time(nullptr);
                char when[32] = "unknown";
                if (const std::tm *lt = std::localtime(&tt))
                    std::strftime(when, sizeof(when), "%Y-%m-%d_%H-%M-%S", lt);
                std::snprintf(name, sizeof(name), "%s_%s", when, r.reason);
                folder = std::string(g_cfg.folder) + "/" + name;
                std::error_code ec;
                std::filesystem::create_directories(folder, ec);
                g_lastFolder = folder;
                std::snprintf(g_lastReason, sizeof(g_lastReason), "%s", r.reason);
                g_savedOldest = g_savedNewest = 0;
                g_sheetsList.clear();
                g_tilesText.clear();
                g_tilesFrom = -1.0;
            }

            // The newest images, oldest first; in the same folder, only those not saved yet.
            const uint64_t stored = g_stored;
            const int ring = g_cfg.ringImages;
            const int available = static_cast<int>(std::min<uint64_t>(stored, static_cast<uint64_t>(ring - 1)));
            int wanted = std::min(r.images, available);
            int chosen = 0;
            for (int i = 0; i < wanted; ++i)
            {
                const int slot = static_cast<int>((stored - 1 - static_cast<uint64_t>(i)) % static_cast<uint64_t>(ring));
                const uint64_t f = g_slots[static_cast<size_t>(slot)].frame;
                if (extend && f >= g_savedOldest && f <= g_savedNewest)
                    continue;
                g_selected[static_cast<size_t>(chosen++)] = slot;
            }
            std::reverse(g_selected.begin(), g_selected.begin() + chosen);

            std::string sheetsText;
            const int perSheet = g_cfg.columns * g_cfg.columns;
            int sheets = 0;
            for (int i = 0; i < chosen; i += perSheet)
            {
                const int n = std::min(perSheet, chosen - i);
                std::string name;
                if (writeSheet(folder, g_selected.data() + i, n, name))
                {
                    char line[160];
                    const Slot &a = g_slots[static_cast<size_t>(g_selected[static_cast<size_t>(i)])];
                    const Slot &b = g_slots[static_cast<size_t>(g_selected[static_cast<size_t>(i + n - 1)])];
                    std::snprintf(line, sizeof(line), "%s: %d images, from %.2f s to %.2f s\n", name.c_str(), n, a.seconds, b.seconds);
                    sheetsText += line;
                    ++sheets;
                    std::snprintf(line, sizeof(line), "%s%s (#%llu %.2f s to #%llu %.2f s)", g_sheetsList.empty() ? "" : "; ", name.c_str(),
                                  static_cast<unsigned long long>(a.frame), a.seconds, static_cast<unsigned long long>(b.frame), b.seconds);
                    g_sheetsList += line;
                    if (!extend && i + n == chosen) // the sheet with the newest image
                    {
                        g_tilesText.clear();
                        g_tilesFrom = a.seconds;
                        for (int k = 0; k < n; ++k)
                        {
                            const Slot &s = g_slots[static_cast<size_t>(g_selected[static_cast<size_t>(i + k)])];
                            int used = 0;
                            if (k % g_cfg.columns == 0)
                                used = std::snprintf(line, sizeof(line), "  %d-%d:", k + 1, std::min(n, k + g_cfg.columns));
                            used += std::snprintf(line + used, sizeof(line) - static_cast<size_t>(used), " #%llu %.2f", static_cast<unsigned long long>(s.frame), s.seconds);
                            if (s.repeats > 1)
                                used += std::snprintf(line + used, sizeof(line) - static_cast<size_t>(used), " x%u", s.repeats);
                            if (s.black)
                                used += std::snprintf(line + used, sizeof(line) - static_cast<size_t>(used), " B");
                            std::snprintf(line + used, sizeof(line) - static_cast<size_t>(used), "%s", (k % g_cfg.columns == g_cfg.columns - 1 || k == n - 1) ? "\n" : " |");
                            g_tilesText += line;
                        }
                    }
                }
            }
            if (chosen > 0)
            {
                const uint64_t oldest = g_slots[static_cast<size_t>(g_selected[0])].frame;
                const uint64_t newest = g_slots[static_cast<size_t>(g_selected[static_cast<size_t>(chosen - 1)])].frame;
                g_savedOldest = (g_savedOldest && g_savedOldest < oldest) ? g_savedOldest : oldest;
                g_savedNewest = std::max(g_savedNewest, newest);
            }
            g_paused.store(false);

            char head[320];
            std::snprintf(head, sizeof(head), "[blackbox] %s: %d images on %d sheets in %s, %.0f ms", r.reason, chosen, sheets, folder.c_str(), (now() - t0) * 1000.0);
            addLine(head, false);
            const uint64_t logCount = copyLog();
            writeLog(folder, logCount);
            writeSummary(folder, r.reason, logCount);
            if (FILE *f = std::fopen((folder + "/info.txt").c_str(), extend ? "ab" : "wb"))
            {
                std::fprintf(f, "reason: %s\nframe %llu, %.2f s since the start, up to %d images asked\n", r.reason,
                             static_cast<unsigned long long>(g_frames.load()), now(), r.images);
                if (g_crashText[0])
                    std::fprintf(f, "%s\n", g_crashText);
                std::fprintf(f, "%s", sheetsText.c_str());
                std::fclose(f);
            }
            removeOldFolders();
            std::printf("%s\n", head);
            std::fflush(stdout);
            if (g_cfg.report && !r.crash)
            {
                g_reportWanted.store(1);
            }
        }

        void formatCrash();

        void saverLoop()
        {
            lowPriority();
#ifdef _WIN32
            g_saverThread.store(GetCurrentThreadId());
#endif
            for (;;)
            {
                if (g_requestPending.load())
                {
                    Request r;
                    const bool locked = lockFlag(g_requestLock);
                    r = g_request;
                    if (locked)
                        g_requestLock.clear(std::memory_order_release);
                    if (r.crash)
                    {
#ifdef _WIN32
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
                        formatCrash();
                        sleepMs(30); // the last lines still in the pipe
                    }
                    doSave(r);
                    g_requestPending.store(false);
                    g_savesDone.store(g_savesAsked.load());
                }
                sleepMs(20);
            }
        }

        // ---- Watching ----

        // state() and report() may wait for a lock held by whatever is stuck: they run here, where that blocks nothing else.
        void samplerLoop()
        {
            lowPriority();
            char line[kLineChars], text[kLineChars - 16], lastScene[kLineChars - 16] = "", lastInput[kLineChars - 16] = "";
            for (int tick = 0;; ++tick)
            {
                if (tick % 10 == 0 && g_cfg.state)
                {
                    line[0] = 0;
                    g_cfg.state(line, sizeof(line));
                    if (line[0])
                        addLine(line, true);
                }
                if (tick % 10 == 0 && g_cfg.scene)
                {
                    text[0] = 0;
                    g_cfg.scene(text, sizeof(text));
                    if (text[0] && std::strcmp(text, lastScene) != 0)
                    {
                        std::memcpy(lastScene, text, sizeof(text));
                        std::snprintf(line, sizeof(line), "[scene] %s", text);
                        addLine(line, false);
                    }
                }
                if (g_cfg.input)
                {
                    text[0] = 0;
                    g_cfg.input(text, sizeof(text));
                    if (text[0] && std::strcmp(text, lastInput) != 0)
                    {
                        std::memcpy(lastInput, text, sizeof(text));
                        std::snprintf(line, sizeof(line), "[input] %s", text);
                        addLine(line, true); // periodic: old ones stay out of log.txt
                    }
                }
                if (g_reportWanted.exchange(0) && g_cfg.report)
                    if (FILE *f = std::fopen((g_lastFolder + "/state.txt").c_str(), "wb"))
                    {
                        g_cfg.report(f);
                        std::fclose(f);
                    }
                sleepMs(100);
            }
        }

        // One problem that can last: asks for a save when it starts and more images as it goes on.
        struct Watch
        {
            const char *reason;
            const Stage *stages;
            int stageCount;
            double unit;
            int stage = 0;
            double lastSaveAt = -1e9;

            void update(double duration, double t, double minGap)
            {
                if (duration <= 0.0)
                {
                    if (stage > 0)
                        event("[blackbox] %s is over", reason);
                    stage = 0;
                    return;
                }
                if (stage >= stageCount || duration < stages[stage].afterSeconds * unit)
                    return;
                const bool first = stage == 0;
                if (first && t - lastSaveAt < minGap)
                    return;
                event("[blackbox] %s for %.1f s", reason, duration);
                if (ask(reason, stages[stage].images, !first, false))
                {
                    ++stage;
                    lastSaveAt = t;
                }
            }
        };

        void monitorLoop()
        {
            lowPriority();
            Watch freeze{"freeze", kFreezeStages, 3, g_cfg.freezeSeconds};
            Watch black{"black", kBlackStages, 3, g_cfg.blackSeconds};
            uint64_t lastProgress = 0, secondFrames = 0, secondProgress = 0, secondStored = 0, secondLoads = 0, secondErrors = 0;
            double lastProgressAt = now(), lastLoop = now(), secondAt = now(), slowSince = 0.0, lastSlowSave = -1e9, best = 0.0;
            double lastBlackSince = 0.0;
            bool progressMoved = false;
            for (;;)
            {
                sleepMs(100);
                const double t = now();
                if (t - lastLoop > 2.0)
                {
                    // The whole program did not run (the PC was asleep, for example): nothing to judge.
                    const double lost = t - lastLoop;
                    event("[blackbox] the watcher did not run for %.1f s", lost);
                    lastProgressAt = t;
                    g_lastFrameAt.store(t);
                    secondAt = t;
                    secondFrames = g_frames.load();
                    if (g_blackSince.load() > 0.0)
                        g_blackSince.store(t);
                }
                lastLoop = t;

                const uint64_t frames = g_frames.load(std::memory_order_relaxed);
                const uint64_t progress = g_cfg.progress ? g_cfg.progress() : 0;
                if (progress != lastProgress)
                {
                    lastProgress = progress;
                    lastProgressAt = t;
                    progressMoved = true;
                    g_progressMoved.store(true, std::memory_order_relaxed);
                }

                double stalled = 0.0;
                if (frames > 0)
                    stalled = t - g_lastFrameAt.load();
                if (progressMoved)
                    stalled = std::max(stalled, t - lastProgressAt);
                if (frames == 0 && t > g_cfg.firstImageSeconds)
                    stalled = t;
                if (stalled < g_cfg.freezeSeconds)
                    stalled = 0.0;
                freeze.update(stalled, t, g_cfg.minSecondsBetweenSaves);

                double blackFor = 0.0;
                const double since = g_blackSince.load();
                if (since == 0.0 && lastBlackSince > 0.0 && t - lastBlackSince >= 2.0)
                    event("[blackbox] the screen was black for %.1f s", t - lastBlackSince);
                lastBlackSince = since;
                if (since > 0.0 && stalled == 0.0)
                {
                    blackFor = t - since;
                    const double limit = g_shownSomething.load() ? g_cfg.blackSeconds : g_cfg.firstImageSeconds;
                    if (blackFor < limit)
                        blackFor = 0.0;
                }
                black.update(blackFor, t, g_cfg.minSecondsBetweenSaves);

                if (t - secondAt >= 1.0)
                {
                    const double rate = static_cast<double>(frames - secondFrames) / (t - secondAt);
                    const double progressRate = static_cast<double>(progress - secondProgress) / (t - secondAt);
                    best = std::max(best, rate);
                    secondFrames = frames;
                    secondProgress = progress;
                    secondAt = t;
                    char line[kLineChars];
                    const double capture = g_captureSeconds.exchange(0.0);
                    std::snprintf(line, sizeof(line), "[speed] frames/s=%.1f progress/s=%.1f capture=%.2f ms/s", rate, progressRate, capture * 1000.0);
                    addLine(line, true);
                    g_bestRate.store(best, std::memory_order_relaxed);
                    const uint64_t stored = g_stored.load(), loads = g_loadLines.load(), errors = g_errorLines.load();
                    const float waiting = static_cast<float>(t - g_lastFrameAt.load());
                    const Second s{t, static_cast<float>(rate), static_cast<float>(progressRate), std::max(g_worstGap.exchange(0.0f), waiting) * 1000.0f,
                                   static_cast<uint32_t>(stored - secondStored), static_cast<uint32_t>(loads - secondLoads), static_cast<uint32_t>(errors - secondErrors)};
                    secondStored = stored;
                    secondLoads = loads;
                    secondErrors = errors;
                    const bool locked = lockFlag(g_secondLock);
                    g_secondRing[static_cast<size_t>(g_secondCount % kSeconds)] = s;
                    ++g_secondCount;
                    if (locked)
                        g_secondLock.clear(std::memory_order_release);

                    const double limit = g_cfg.slowBelowPerSecond > 0.0 ? g_cfg.slowBelowPerSecond : best * 0.8;
                    if (rate >= limit || stalled > 0.0 || rate < 1.0)
                        slowSince = 0.0;
                    else
                    {
                        if (slowSince == 0.0)
                            slowSince = t;
                        if (t - slowSince >= g_cfg.slowSeconds && t - lastSlowSave >= g_cfg.minSecondsBetweenSlowSaves)
                        {
                            event("[blackbox] slow: %.1f frames/s, under %.1f for %.1f s", rate, limit, t - slowSince);
                            if (ask("slow", kSlowImages, false, false))
                                lastSlowSave = t;
                        }
                    }
                }
            }
        }

        // ---- Output ----

        void outputLoop(int readFd, int originalFd)
        {
            std::string partial;
            char buffer[4096];
            for (;;)
            {
#ifdef _WIN32
                const int n = _read(readFd, buffer, sizeof(buffer));
#else
                const int n = static_cast<int>(read(readFd, buffer, sizeof(buffer)));
#endif
                if (n <= 0)
                    return;
                if (originalFd >= 0)
                {
#ifdef _WIN32
                    _write(originalFd, buffer, static_cast<unsigned>(n));
#else
                    (void)!write(originalFd, buffer, static_cast<size_t>(n));
#endif
                }
                partial.append(buffer, static_cast<size_t>(n));
                size_t end;
                while ((end = partial.find('\n')) != std::string::npos)
                {
                    std::string line = partial.substr(0, end);
                    partial.erase(0, end + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.empty() || line.rfind("[blackbox]", 0) == 0)
                        continue;
                    bool ignored = false;
                    if (g_cfg.ignoreLinesStartingWith)
                        for (const char *const *p = g_cfg.ignoreLinesStartingWith; *p && !ignored; ++p)
                            ignored = line.rfind(*p, 0) == 0;
                    if (!ignored)
                        addLine(line.c_str(), false);
                }
                if (partial.size() > 65536)
                    partial.clear();
            }
        }

        void captureStream(FILE *stream, int fd)
        {
            int pipeFds[2];
#ifdef _WIN32
            std::fflush(stream);
            if (_pipe(pipeFds, 65536, _O_BINARY) != 0)
                return;
            const int original = _dup(fd);
            if (original >= 0)
                _setmode(original, _O_BINARY);
            if (_dup2(pipeFds[1], fd) != 0)
                return;
            _close(pipeFds[1]);
#else
            std::fflush(stream);
            if (pipe(pipeFds) != 0)
                return;
            const int original = dup(fd);
            if (dup2(pipeFds[1], fd) < 0)
                return;
            close(pipeFds[1]);
#endif
            // Line by line: a crash must not leave the last lines in a buffer.
            setvbuf(stream, nullptr, _IONBF, 0);
            std::thread(outputLoop, pipeFds[0], original).detach();
        }

        // ---- Crashes ----

#ifdef _WIN32
        struct CrashInfo
        {
            DWORD code;
            void *address;
            ULONG_PTR accessKind, accessAddress;
            DWORD thread;
            USHORT depth;
            void *stack[32];
        };
        CrashInfo g_crash{};

        // No formatting in the handler itself: it may be running out of stack.
        LONG WINAPI onException(EXCEPTION_POINTERS *e)
        {
            if (g_crashing.exchange(true))
            {
                // Something else is already being saved (exit() can make other threads fail, for example).
                // This thread waits, so that the program does not close before the files are written.
                if (GetCurrentThreadId() != g_saverThread.load())
                    for (int i = 0; i < 2000 && g_savesDone.load() < g_savesAsked.load(); ++i)
                        Sleep(10);
                return EXCEPTION_CONTINUE_SEARCH;
            }
            g_crash.code = e->ExceptionRecord->ExceptionCode;
            g_crash.address = e->ExceptionRecord->ExceptionAddress;
            if (e->ExceptionRecord->NumberParameters >= 2)
            {
                g_crash.accessKind = e->ExceptionRecord->ExceptionInformation[0];
                g_crash.accessAddress = e->ExceptionRecord->ExceptionInformation[1];
            }
            g_crash.thread = GetCurrentThreadId();
            g_crash.depth = RtlCaptureStackBackTrace(0, 32, g_crash.stack, nullptr);
            askAndWait("crash", kCrashImages);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        void formatCrash()
        {
            if (!g_crash.code)
                return;
            const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            int n = std::snprintf(g_crashText, sizeof(g_crashText), "exception 0x%08lx, thread %lu, at image+0x%llx, access kind %llu to 0x%llx\nstack (image offsets, read them with the link map):",
                                  static_cast<unsigned long>(g_crash.code), static_cast<unsigned long>(g_crash.thread),
                                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_crash.address) - base),
                                  static_cast<unsigned long long>(g_crash.accessKind), static_cast<unsigned long long>(g_crash.accessAddress));
            for (USHORT i = 0; i < g_crash.depth && n > 0 && n < static_cast<int>(sizeof(g_crashText)) - 24; ++i)
                n += std::snprintf(g_crashText + n, sizeof(g_crashText) - static_cast<size_t>(n), " +0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_crash.stack[i]) - base));
            // Frames in other modules (a DLL such as sor-gs.dll, a driver): module name and offset inside it,
            // to read with that module's symbols.
            for (int i = -1; i < static_cast<int>(g_crash.depth) && n > 0 && n < static_cast<int>(sizeof(g_crashText)) - 200; ++i)
            {
                void *a = i < 0 ? g_crash.address : g_crash.stack[i];
                HMODULE mod = nullptr;
                if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCSTR>(a), &mod) ||
                    !mod || reinterpret_cast<uintptr_t>(mod) == base)
                    continue;
                char path[MAX_PATH] = {};
                GetModuleFileNameA(mod, path, MAX_PATH);
                const char *name = std::strrchr(path, '\\');
                n += std::snprintf(g_crashText + n, sizeof(g_crashText) - static_cast<size_t>(n), "\n  %s %s+0x%llx",
                                   i < 0 ? "at" : "from", name ? name + 1 : path,
                                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(a) - reinterpret_cast<uintptr_t>(mod)));
            }
            char line[kLineChars];
            std::snprintf(line, sizeof(line), "[blackbox] exception 0x%08lx at image+0x%llx", static_cast<unsigned long>(g_crash.code),
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_crash.address) - base));
            addLine(line, false);
        }
#else
        void formatCrash() {}
#endif

        void onTerminate()
        {
            fatal("terminate", "std::terminate was called");
            std::abort();
        }

        void onSignal(int number)
        {
            std::signal(number, SIG_DFL);
            char message[40];
            std::snprintf(message, sizeof(message), "signal %d", number);
            fatal(number == SIGABRT ? "abort" : "crash", message);
            std::raise(number);
        }

        void onExit()
        {
            if (!g_clean.load())
                fatal("exit", "exit() was called while the program was running");
        }
    }

    bool start(const Config &config)
    {
        if (g_running.load())
            return true;
        g_cfg = config;
        g_start = std::chrono::steady_clock::now();
        g_cfg.ringImages = std::max(g_cfg.ringImages, 2);
        g_cellBytes = static_cast<size_t>(g_cfg.cellWidth) * g_cfg.cellHeight * 3u;
        g_pixels.assign(g_cellBytes * static_cast<size_t>(g_cfg.ringImages), 0u);
        g_slots.assign(static_cast<size_t>(g_cfg.ringImages), Slot{});
        g_xmap.assign(static_cast<size_t>(g_cfg.cellWidth), 0);
        g_ymap.assign(static_cast<size_t>(g_cfg.cellHeight), 0);
        const int sheetW = g_cfg.columns * g_cfg.cellWidth + (g_cfg.columns - 1) * g_cfg.cellGap;
        const int sheetH = g_cfg.columns * g_cfg.cellHeight + (g_cfg.columns - 1) * g_cfg.cellGap;
        g_sheet.assign(static_cast<size_t>(sheetW) * sheetH * 3u, 0u);
        g_selected.assign(static_cast<size_t>(std::min(kMaxSelected, g_cfg.ringImages)), 0);
        g_log.assign(kLogLines, LogLine{});
        g_logCopy.assign(kLogLines, LogLine{});
        g_running.store(true);

        if (g_cfg.captureOutput)
        {
            captureStream(stdout, 1);
            captureStream(stderr, 2);
        }
        std::thread(saverLoop).detach();
        std::thread(monitorLoop).detach();
        if (g_cfg.state || g_cfg.report || g_cfg.scene || g_cfg.input)
            std::thread(samplerLoop).detach();
        if (g_cfg.catchCrashes)
        {
            std::set_terminate(onTerminate);
            std::signal(SIGABRT, onSignal);
            std::atexit(onExit);
#ifdef _WIN32
            SetUnhandledExceptionFilter(onException);
#else
            std::signal(SIGSEGV, onSignal);
            std::signal(SIGFPE, onSignal);
            std::signal(SIGILL, onSignal);
#endif
        }
        std::printf("[blackbox] on: %d images in RAM (%.0f MB), %d per second, saves in %s/\n", g_cfg.ringImages,
                    static_cast<double>(g_pixels.size() + g_sheet.size()) / (1024.0 * 1024.0), g_cfg.imagesPerSecond, g_cfg.folder);
        std::fflush(stdout);
        return true;
    }

    bool running() { return g_running.load(std::memory_order_relaxed); }

    void frame(const uint8_t *rgba, int width, int height)
    {
        if (!g_running.load(std::memory_order_relaxed))
            return;
        const uint64_t number = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
        const double t = now();
        const float gap = static_cast<float>(t - g_lastFrameAt.load(std::memory_order_relaxed));
        if (number > 1 && gap > g_worstGap.load(std::memory_order_relaxed))
            g_worstGap.store(gap, std::memory_order_relaxed);
        g_lastFrameAt.store(t, std::memory_order_relaxed);
        if (!rgba) // shown by PCSX2's GS: the frame counts, but there is no picture here (so it is not black)
        {
            if (g_blackSince.load(std::memory_order_relaxed) != 0.0)
                g_blackSince.store(0.0);
            if (!g_shownSomething.load(std::memory_order_relaxed))
                g_shownSomething.store(true);
            return;
        }
        if (t < g_nextSample || !rgba || width <= 0 || height <= 0)
            return;
        // Even steps, so that 60 Hz gives 25 per second on average as well as 50 Hz.
        const double step = 1.0 / g_cfg.imagesPerSecond;
        g_nextSample += step;
        if (g_nextSample < t)
            g_nextSample = t + step;

        g_storing.store(true);
        if (g_paused.load())
        {
            g_storing.store(false);
            return;
        }
        if (width != g_mapWidth || height != g_mapHeight)
        {
            g_mapWidth = width;
            g_mapHeight = height;
            for (int x = 0; x < g_cfg.cellWidth; ++x)
                g_xmap[static_cast<size_t>(x)] = (x * 2 + 1) * width / (g_cfg.cellWidth * 2) * 4;
            for (int y = 0; y < g_cfg.cellHeight; ++y)
                g_ymap[static_cast<size_t>(y)] = (y * 2 + 1) * height / (g_cfg.cellHeight * 2) * width * 4;
        }
        // Written into the next slot; it only becomes part of the ring if it differs from the last image.
        const size_t ring = static_cast<size_t>(g_cfg.ringImages);
        const size_t slot = static_cast<size_t>(g_stored % ring);
        uint8_t *out = g_pixels.data() + g_cellBytes * slot;
        const int cw = g_cfg.cellWidth, ch = g_cfg.cellHeight;
        const int *xmap = g_xmap.data();
        uint32_t lit = 0;
        uint8_t *o = out;
        for (int y = 0; y < ch; ++y)
        {
            const uint8_t *row = rgba + g_ymap[static_cast<size_t>(y)];
            for (int x = 0; x < cw; ++x)
            {
                const uint8_t *p = row + xmap[x];
                o[0] = p[0];
                o[1] = p[1];
                o[2] = p[2];
                lit += static_cast<uint32_t>(p[0]) + p[1] + p[2] > 60u;
                o += 3;
            }
        }
        const bool black = lit * 500u <= static_cast<uint32_t>(cw * ch); // up to 0.2% of the points may be lit

        bool same = false;
        if (g_stored > 0)
        {
            const size_t last = static_cast<size_t>((g_stored - 1) % ring);
            Slot &previous = g_slots[last];
            same = (black && previous.black) || std::memcmp(g_pixels.data() + g_cellBytes * last, out, g_cellBytes) == 0;
            if (same)
                ++previous.repeats;
        }
        if (!same)
        {
            g_slots[slot] = Slot{number, t, 1u, black};
            ++g_stored;
        }
        g_storing.store(false);
        g_captureSeconds.store(g_captureSeconds.load(std::memory_order_relaxed) + (now() - t), std::memory_order_relaxed);

        if (black)
        {
            if (g_blackSince.load(std::memory_order_relaxed) == 0.0)
                g_blackSince.store(t);
        }
        else
        {
            if (g_blackSince.load(std::memory_order_relaxed) != 0.0)
                g_blackSince.store(0.0);
            if (!g_shownSomething.load(std::memory_order_relaxed))
                g_shownSomething.store(true);
        }
    }

    void event(const char *format, ...)
    {
        if (!g_running.load(std::memory_order_relaxed))
            return;
        char line[kLineChars];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        addLine(line, false);
    }

    void save(const char *reason, int images)
    {
        if (g_running.load())
            ask(reason, images > 0 ? images : kDefaultImages, false, false);
    }

    void fatal(const char *reason, const char *message)
    {
        if (!g_running.load() || g_crashing.exchange(true))
            return;
        if (message)
        {
            event("[blackbox] %s", message);
            std::snprintf(g_crashText, sizeof(g_crashText), "%s", message);
        }
        askAndWait(reason, kCrashImages);
    }

    void cleanExit() { g_clean.store(true); }
}
