# Technical notes

Iris Insight works like a flight recorder (a "black box", which is why the code and its folder carry that name). It keeps the last images and log lines of a program in RAM. When something goes wrong, it saves them as contact sheets: one picture with up to 25 numbered frames, plus a short text summary using the same numbers.

It knows nothing about the program it watches. It needs only C++17 and the files in `src/`. Windows is the only tested platform.

## What it saves, and when

| Reason | When | Images |
|---|---|---|
| `crash` | An unhandled exception, including a stack overflow | 100 |
| `abort`, `terminate`, `exit` | The program is closing on an error | 100 |
| `freeze` | No new frame, or the progress counter stopped, for 6 s | 50, then 75 at 18 s, 100 at 60 s |
| `black` | A black screen for 15 s (60 s if nothing has been shown yet) | 25, then 50 at 30 s, 100 at 60 s |
| `slow` | Frames per second under 80% of the best rate for 5 s, at most once every 5 minutes | 25 |
| any name you pass to `save()` | A key or a test command | 25, or what you ask |

Each save gets its own folder, `blackbox/2026-11-01_00-12-58_freeze/`. Only the last 5 folders are kept; a `slow` one goes before the others.

- `summary.txt`: read this with the sheet. About 400 tokens on what images alone cannot show: when the picture last changed, frames per second, whether the program's logic still runs (the progress counter), whether it is loading and for how long, the screen or task, the last inputs, the last errors, and each tile's frame and time. Times stop where the save began, because no image is stored while files are written.
- `frames_0000701-0000977.jpg`: a sheet, oldest image first. Each cell shows its frame number (top left) and its time in seconds since the start (bottom left).
- `#829x67` in a cell means that image stayed the same for 67 samples. Identical images and plain black ones are stored once, so a long black screen does not push out what came before it.
- `log.txt`: the last lines of the program's output and your events, each with its frame number. The first number on a line is the frame, the second is seconds since the start.
- `info.txt`: the reason, the sheets and, after a crash, the exception and the stack.
- `state.txt`: your `report` callback, if you set one. Not written after a crash.

Sheets are 1564 px wide at most, which vision models read without shrinking.

## Two lines of summary.txt worth knowing

**`speed:`** says the frame rate in words, compared with the best rate seen so far: `SLOW NOW` from 2 seconds under 80% of the best (or under `slowBelowPerSecond`), `normal now` with the last drop if there was one, `NO NEW FRAME` after 1 second without frames. On a still screen, or one that looks fine, the pictures cannot show a slowdown.

**`loading:`** says `YES` only for real, continuing reads: at least `loadingMinReads` lines (3) over at least `loadingMinSeconds` (0.5 s), the last one under 2 s ago. Reads more than 3 s apart do not count together. A single read during a black screen is reported as a single read. So if your loads are long, log a line at least every 0.25 s or so during them, not only when they start.

## Every option

`Config` in `blackbox.h` lists the thresholds and the callbacks:

- `progress`, `progressName`: a counter that moves while the logic runs, and what it counts. It tells a stuck program from one that only stopped drawing.
- `loadingLinesStartingWith`: the log lines that mean a file or disk read.
- `errorWords`: words that make a log line an error.
- `scene`: a few words on the current screen or task.
- `input`: the keys or buttons held now.
- `state`, `report`: one short line every second, and a longer dump written to `state.txt`.
- `captureOutput`: copies everything your program prints to stdout and stderr into the log. The lines still reach where they went before. Turn it off with `captureOutput = false`.

To turn Iris Insight off, do not call `start()`: `frame()` and `event()` then return at once.

## Memory and speed

200 images of 312 x 234 take 42 MB, plus 5 MB for building a sheet. Storing an image is a short loop on the thread that calls `frame()`: in the one program tested, 2 to 3.5 ms per second, about 0.3% of the time. Saving runs on its own thread at low priority.

## What can go wrong

- Moving the window with the mouse can stop the frames, and that can look like a freeze.
- Fades and loading screens: a black screen shorter than the threshold is ignored.
- If your program does not move the progress counter during videos or loading, raise `freezeSeconds` or leave `progress` unset.
- A crash can only be saved if the process can still run its threads. Killing it from the Task Manager saves nothing.

## Third-party code

`stb_image_write.h` by Sean Barrett (public domain or MIT, your choice), used to write the JPEG files.
