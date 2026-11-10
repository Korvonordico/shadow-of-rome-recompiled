// Iris Insight: keeps the last images and log lines of a program in RAM, like a flight recorder, and saves
// them as contact sheets plus a short summary when something goes wrong. It knows nothing about the program.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace blackbox
{
    struct Config
    {
        const char *folder = "blackbox"; // relative to the current folder
        int keepFolders = 5;             // older saves are removed
        int ringImages = 200;            // memory used: ringImages * cellWidth * cellHeight * 3 bytes
        int imagesPerSecond = 25;
        int cellWidth = 312;  // 5 cells of 312 + 4 gaps = 1564 px, what a vision model reads at full size
        int cellHeight = 234; // 4:3
        int cellGap = 1;
        int columns = 5;      // a sheet holds columns * columns images at most
        int jpegQuality = 90;

        double freezeSeconds = 6.0;      // no new frame, or the progress counter stopped
        double firstImageSeconds = 60.0; // black or nothing at all since the start
        double blackSeconds = 15.0;      // black after the program has shown something
        double slowBelowPerSecond = 0.0; // 0 = 80% of the best rate seen
        double slowSeconds = 5.0;        // how long the rate must stay low
        double minSecondsBetweenSaves = 30.0; // for automatic saves of the same reason
        double minSecondsBetweenSlowSaves = 300.0; // a long slow fight must not push out the other saves

        bool captureOutput = true; // copy stdout and stderr lines into the log (they still reach their old target)
        bool catchCrashes = true;  // unhandled exceptions, std::terminate, abort, exit
        const char *const *ignoreLinesStartingWith = nullptr; // nullptr-terminated list, for very chatty lines
        // For summary.txt (nullptr-terminated lists): lines that mean "the program is loading" (file or disk reads),
        // and words that make a line an error (case does not matter; nullptr: error, fail, missing, exception, fatal).
        const char *const *loadingLinesStartingWith = nullptr;
        const char *const *errorWords = nullptr;
        // summary.txt says "loading" only for real, continuing reads: this many lines, over this many seconds.
        int loadingMinReads = 3;
        double loadingMinSeconds = 0.5;
        const char *progressName = "progress"; // what the progress counter counts, as summary.txt names it

        // Optional. All are called from threads of Iris Insight, never from the program's.
        uint64_t (*progress)() = nullptr;           // a counter that moves while the program's logic runs (for example main loop turns)
        void (*state)(char *out, size_t n) = nullptr; // one short line, about once a second: where the program is
        void (*report)(FILE *out) = nullptr;        // a longer dump, written to state.txt in each save except crashes
        void (*scene)(char *out, size_t n) = nullptr; // a few words, once a second: the program's screen or task; changes are logged
        void (*input)(char *out, size_t n) = nullptr; // the keys or buttons held now ("none" if none), 10 times a second; changes are logged
    };

    // Starts the threads and allocates the memory. Call once, as early as you can.
    bool start(const Config &config);
    bool running();

    // The only capture point: call it with every presented frame (RGBA, alpha ignored, tightly packed).
    void frame(const uint8_t *rgba, int width, int height);

    // Adds a line to the log, with the current frame number.
    void event(const char *format, ...);

    // Saves now, without waiting (for a key or a test command). images <= 0: 25.
    void save(const char *reason, int images = 0);
    // Saves and waits for the files to be written. For errors, just before the program closes.
    void fatal(const char *reason, const char *message = nullptr);
    // The program is closing normally: exit() after this is not an abnormal exit.
    void cleanExit();
}
