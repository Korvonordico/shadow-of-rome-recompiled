// HD replacement films (a mod: the files are the player's own, never part of the port).
//
// When the game starts a film, the port looks for
//     <folder of sor.exe>\textures\SLES-52950\replacements\movies\movie-<number>.mp4
// where <number> is the game's own number of the film (the opening film is movie-0; the log says the number of each
// film it plays). The old name film-<sector>.mp4 (the disc sector given to sceCdStStart) still works when the game
// streams a film that way, which Shadow of Rome does not seem to do: its films are inside IMAGE*.IMG.
// That folder is the launcher's MODS FOLDER. A pack can hold films too: replacements\<pack>\movies\ comes first
// (packs in name order; '_' packs are off). strumenti/film_hd.py makes these files.
//
// The game keeps playing its own film underneath, exactly as without the mod: its sound, its timing, the button
// that skips it and what comes after do not change. Each time the game asks for its next picture, the HD frame of
// the same moment is drawn over the film's band, at the window's real size, in a small borderless window that
// follows the game's window (owned by it, click-through, never focused). This works the same with the video card
// (sor-gs.dll) and with the processor drawing. The subtitles, drawn by the game below the band, stay visible.
//
// Anything wrong with the file (missing, unreadable, not H.264, broken while playing): the HD picture is not shown
// and the original film plays. Every decision is logged as "[sor] film HD: ...". SOR_FILMHD=0 turns the mod off.
//
// Only Windows (the port's only platform today); elsewhere the hooks do nothing.

#include <cstdint>

#if defined(_WIN32)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
    void *GetWindowHandle(void); // raylib: the game's window
}

namespace
{
    using Clock = std::chrono::steady_clock;

    long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }

    void say(const char *fmt, const std::string &name, const std::string &more = std::string())
    {
        std::printf("[sor] film HD: %s ", name.c_str());
        std::printf(fmt, more.c_str());
        std::printf("\n");
        std::fflush(stdout);
    }

    std::wstring exeFolder()
    {
        wchar_t buf[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
        std::wstring p(buf, n);
        const size_t cut = p.find_last_of(L"\\/");
        return cut == std::wstring::npos ? std::wstring(L".") : p.substr(0, cut);
    }

    std::string utf8(const std::wstring &w)
    {
        if (w.empty())
            return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    // One film being replaced. Lives on its own thread (decoding, and the window that shows the pictures);
    // the game's thread only hands over "picture N is due now".
    struct Session
    {
        std::string name;
        std::string path; // UTF-8, as FFmpeg wants it on Windows
        std::atomic<bool> quit{false};
        std::atomic<long long> target{0};      // the game's picture time, in 1/90000 s from its first picture
        std::atomic<long long> lastPicture{0}; // when the game last asked for a picture (ms)
        std::atomic<unsigned> pictures{0};
        HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr); // set when its thread has closed its window
        long long firstPts = -1;
        ~Session()
        {
            if (wake)
                CloseHandle(wake);
            if (done)
                CloseHandle(done);
        }
    };

    std::mutex g_lock;
    std::shared_ptr<Session> g_session;
    std::atomic<uint32_t> g_streamSector{0};
    std::set<uint32_t> g_toldMissing; // "not found" is said once per film
    std::set<uint32_t> g_toldLate;    // "joined late" is said once per film

    bool isFile(const std::wstring &path)
    {
        const DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    // The film's file: first in the mod packs (replacements\<pack>\movies\, packs in name order; a pack whose name
    // starts with '_' is off, as for the textures), then in replacements\movies\. Empty if there is none.
    std::wstring findFilm(const std::wstring &file)
    {
        const std::wstring rep = exeFolder() + L"\\textures\\SLES-52950\\replacements\\";
        std::vector<std::wstring> packs;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((rep + L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::wstring n = fd.cFileName;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L".." && n[0] != L'_' &&
                    _wcsicmp(n.c_str(), L"movies") != 0)
                    packs.push_back(n);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(packs.begin(), packs.end(),
                  [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        for (const std::wstring &pk : packs)
            if (isFile(rep + pk + L"\\movies\\" + file))
                return rep + pk + L"\\movies\\" + file;
        if (isFile(rep + L"movies\\" + file))
            return rep + L"movies\\" + file;
        return std::wstring();
    }

    // ---- the window ------------------------------------------------------------------------------------------

    const wchar_t *kClass = L"SorFilmHd";
    struct Picture
    {
        std::vector<uint8_t> bgra;
        int w = 0, h = 0;
    };

    void paint(HWND wnd, const Picture &pic)
    {
        if (pic.bgra.empty())
            return;
        RECT r;
        GetClientRect(wnd, &r);
        HDC dc = GetDC(wnd);
        if (!dc)
            return;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = pic.w;
        bi.bmiHeader.biHeight = -pic.h; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        const int dw = r.right - r.left, dh = r.bottom - r.top;
        if (dw == pic.w && dh == pic.h) // the usual case: the frame was scaled to the band's size when decoded
            SetDIBitsToDevice(dc, 0, 0, pic.w, pic.h, 0, 0, 0, pic.h, pic.bgra.data(), &bi, DIB_RGB_COLORS);
        else
        {
            SetStretchBltMode(dc, HALFTONE); // the band changed size since that frame: stretch it until the next one
            SetBrushOrgEx(dc, 0, 0, nullptr);
            StretchDIBits(dc, 0, 0, dw, dh, 0, 0, pic.w, pic.h, pic.bgra.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        ReleaseDC(wnd, dc);
    }

    LRESULT CALLBACK wndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(wnd, &ps);
            if (const auto *pic = reinterpret_cast<const Picture *>(GetWindowLongPtrW(wnd, GWLP_USERDATA)))
                paint(wnd, *pic);
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        }
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    // Where the film's band is on the screen: the game's picture is 4:3, centred in the window (as both the
    // processor drawing and sor-gs.dll show films), and the band fills its width, centred, at the film's own shape.
    bool bandOnScreen(HWND game, double aspect, RECT &out)
    {
        if (!game || !IsWindow(game) || IsIconic(game) || !IsWindowVisible(game))
            return false;
        RECT cr;
        if (!GetClientRect(game, &cr))
            return false;
        POINT tl{0, 0};
        ClientToScreen(game, &tl);
        const double cw = cr.right - cr.left, ch = cr.bottom - cr.top;
        if (cw < 16 || ch < 16)
            return false;
        const double w43 = std::min(cw, ch * 4.0 / 3.0);
        const double h43 = w43 * 3.0 / 4.0;
        const double bh = std::min(h43, w43 / aspect);
        const int x = tl.x + static_cast<int>(std::lround((cw - w43) * 0.5));
        const int y = tl.y + static_cast<int>(std::lround((ch - bh) * 0.5));
        out = RECT{x, y, x + static_cast<int>(std::lround(w43)), y + static_cast<int>(std::lround(bh))};
        return true;
    }

    // ---- decoding ----------------------------------------------------------------------------------------------

    struct Decoder
    {
        AVFormatContext *fmt = nullptr;
        AVCodecContext *ctx = nullptr;
        AVPacket *pkt = nullptr;
        AVFrame *next = nullptr; // decoded, not yet due
        AVFrame *cand = nullptr; // the latest one due
        SwsContext *sws = nullptr;
        int stream = -1;
        double tb = 0.0, start = 0.0, frameDur = 1.0 / 29.97, aspect = 1.0;
        bool eofSent = false, ended = false;
        int badPackets = 0;

        ~Decoder()
        {
            sws_freeContext(sws);
            av_frame_free(&next);
            av_frame_free(&cand);
            av_packet_free(&pkt);
            avcodec_free_context(&ctx);
            avformat_close_input(&fmt);
        }

        std::string open(const std::string &path)
        {
            if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
                return "the file cannot be opened as a video";
            if (avformat_find_stream_info(fmt, nullptr) < 0)
                return "the file has no readable streams";
            stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (stream < 0)
                return "the file has no picture";
            AVStream *st = fmt->streams[stream];
            const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
            if (!codec)
                return std::string("its picture is ") + avcodec_get_name(st->codecpar->codec_id) + ", only H.264 is supported";
            const int w = st->codecpar->width, h = st->codecpar->height;
            if (w < 16 || h < 16 || w > 4096 || h > 4096)
                return "its picture size is not usable";
            ctx = avcodec_alloc_context3(codec);
            if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0)
                return "the decoder cannot be set up";
            const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
            ctx->thread_count = static_cast<int>(std::min(4u, std::max(1u, cores / 2)));
            ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
            if (avcodec_open2(ctx, codec, nullptr) < 0)
                return "the decoder does not start";
            pkt = av_packet_alloc();
            next = av_frame_alloc();
            cand = av_frame_alloc();
            if (!pkt || !next || !cand)
                return "out of memory";
            tb = av_q2d(st->time_base);
            start = st->start_time != AV_NOPTS_VALUE ? st->start_time * tb : 0.0;
            const AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
            if (fr.num > 0 && fr.den > 0)
                frameDur = av_q2d(av_inv_q(fr));
            AVRational sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : st->codecpar->sample_aspect_ratio;
            const double par = sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1.0;
            aspect = w * par / h;
            return std::string();
        }

        // The next frame into `next`. False at the end of the file or on an error (`ended`).
        bool decodeNext(double &t)
        {
            for (;;)
            {
                const int r = avcodec_receive_frame(ctx, next);
                if (r == 0)
                {
                    const int64_t ts = next->best_effort_timestamp != AV_NOPTS_VALUE ? next->best_effort_timestamp : next->pts;
                    t = ts != AV_NOPTS_VALUE ? ts * tb - start : 0.0;
                    return true;
                }
                if (r != AVERROR(EAGAIN))
                {
                    ended = true;
                    return false;
                }
                if (eofSent)
                {
                    ended = true;
                    return false;
                }
                if (av_read_frame(fmt, pkt) < 0)
                {
                    avcodec_send_packet(ctx, nullptr);
                    eofSent = true;
                    continue;
                }
                if (pkt->stream_index == stream && avcodec_send_packet(ctx, pkt) < 0 && ++badPackets > 30)
                {
                    av_packet_unref(pkt);
                    ended = true;
                    return false;
                }
                av_packet_unref(pkt);
            }
        }

        // The frame in 32-bit colour at the band's size (outW x outH), scaled here once (bicubic) so that drawing it
        // is a plain copy.
        bool toBgra(const AVFrame *f, Picture &pic, int outW, int outH)
        {
            outW = std::clamp(outW, 16, 8192);
            outH = std::clamp(outH, 16, 8192);
            sws = sws_getCachedContext(sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), outW, outH,
                                       AV_PIX_FMT_BGRA, SWS_BICUBIC, nullptr, nullptr, nullptr);
            if (!sws)
                return false;
            const int *coefs = sws_getCoefficients(f->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601);
            sws_setColorspaceDetails(sws, coefs, f->color_range == AVCOL_RANGE_JPEG ? 1 : 0, coefs, 1, 0, 1 << 16, 1 << 16);
            pic.w = outW;
            pic.h = outH;
            pic.bgra.resize(static_cast<size_t>(pic.w) * pic.h * 4u);
            uint8_t *dst[4] = {pic.bgra.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {pic.w * 4, 0, 0, 0};
            return sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride) == outH;
        }
    };

    void run(std::shared_ptr<Session> s)
    {
        struct SignalDone // on every way out, after the window and the decoder are gone (declared first: runs last)
        {
            HANDLE h;
            ~SignalDone() { SetEvent(h); }
        } signalDone{s->done};
        Decoder dec;
        const std::string why = dec.open(s->path);
        if (!why.empty())
        {
            say("refused (%s): the original film plays", s->name, why);
            return;
        }
        Picture pic;
        // The first frame must decode, or nothing of this file is shown.
        double tn = 0.0;
        bool haveNext = dec.decodeNext(tn);
        if (!haveNext)
        {
            say("refused (%s): the original film plays", s->name, "its first picture does not decode");
            return;
        }
        {
            char info[160];
            std::snprintf(info, sizeof(info), "%dx%d, %.2f frames/s, %s", dec.ctx->width, dec.ctx->height, 1.0 / dec.frameDur,
                          avcodec_get_name(dec.ctx->codec_id));
            say("used (%s) in place of the original picture", s->name, info);
        }

        static std::once_flag registered;
        std::call_once(registered, []
                       {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = wndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW (the ANSI macro here)
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wc.lpszClassName = kClass;
            RegisterClassExW(&wc); });
        HWND game = static_cast<HWND>(GetWindowHandle());
        HWND wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClass, L"Shadow of Rome HD film",
                                   WS_POPUP, 0, 0, 16, 16, game, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!wnd)
        {
            say("refused (%s): the original film plays", s->name, "no window for the picture");
            return;
        }
        SetLayeredWindowAttributes(wnd, 0, 255, LWA_ALPHA);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&pic));

        bool shown = false, ranOut = false;
        unsigned frames = 0;
        double shownAt = -1.0;
        long long lastPlace = 0;
        RECT placed{0, 0, 0, 0};
        auto hide = [&]
        {
            if (shown)
                ShowWindow(wnd, SW_HIDE);
            shown = false;
        };
        auto place = [&](bool force)
        {
            RECT r;
            if (!bandOnScreen(game, dec.aspect, r))
            {
                hide();
                return false;
            }
            if (force || !EqualRect(&r, &placed) || !shown)
            {
                SetWindowPos(wnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
                placed = r;
                shown = true;
            }
            return true;
        };

        while (!s->quit.load())
        {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            if (!IsWindow(game))
                break; // the game's window is closing
            const long long now = nowMs();
            const long long last = s->lastPicture.load();
            // The game stopped asking for pictures (film skipped or over, game paused by the PC): its own picture shows.
            // The film's end and the next film stop this session at once (sorFilmHdStop); the time limit is only for
            // a film left without either.
            if (now - last > 300)
                hide();
            if (now - last > 30000)
                break;
            const double due = s->target.load() / 90000.0;
            bool fresh = false;
            if (!ranOut && now - last <= 300)
            {
                while (haveNext && tn <= due + dec.frameDur * 0.5)
                {
                    av_frame_unref(dec.cand);
                    av_frame_move_ref(dec.cand, dec.next);
                    fresh = true;
                    shownAt = tn;
                    haveNext = dec.decodeNext(tn);
                }
                if (!haveNext && due > shownAt + 1.0)
                {
                    // The file is shorter than the game's film (or broken): from here the original shows.
                    ranOut = true;
                    hide();
                    char info[96];
                    std::snprintf(info, sizeof(info), "%.1f s", due);
                    say("stopped at %s: the file has no more pictures, the original film goes on", s->name, info);
                }
            }
            if (fresh)
            {
                RECT band;
                if (!bandOnScreen(game, dec.aspect, band))
                    hide(); // minimised or hidden: nothing to draw
                else if (dec.toBgra(dec.cand, pic, band.right - band.left, band.bottom - band.top))
                {
                    ++frames;
                    if (place(false))
                        paint(wnd, pic);
                }
                lastPlace = now;
            }
            else if (shown && now - lastPlace > 100)
            {
                // The player may move or resize the window while nothing new comes.
                place(false);
                lastPlace = now;
            }
            MsgWaitForMultipleObjects(1, &s->wake, FALSE, 20, QS_ALLINPUT);
        }
        hide();
        SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        DestroyWindow(wnd);
        char info[96];
        std::snprintf(info, sizeof(info), "%u of the game's pictures, %u HD frames shown", s->pictures.load(), frames);
        say("finished (%s)", s->name, info);
    }

    void stopLocked()
    {
        if (g_session)
        {
            g_session->quit.store(true);
            SetEvent(g_session->wake);
            g_session.reset();
        }
    }

    bool enabled()
    {
        static const bool on = []
        {
            const char *e = std::getenv("SOR_FILMHD");
            return !(e && e[0] == '0');
        }();
        return on;
    }
}

// sceCdStStart (CD.cpp): the game starts reading a stream at this disc sector. A film starts with one.
void sorFilmHdStreamStart(uint32_t sector)
{
    g_streamSector.store(sector);
}

// sceMpegGetPicture (MPEG.cpp): the game takes its picture number `index` (0 = the first of a film); `pts90k` is its
// time stamp (-1 if none); `filmId` is the game's own number of the film (byte 0x4BE2DA, classMOVIE_TASK; the opening
// film is 0), 0xFFFFFFFF if unknown.
void sorFilmHdPicture(uint32_t index, long long pts90k, uint32_t filmId)
{
    if (!enabled())
        return;
    std::lock_guard<std::mutex> lk(g_lock);
    if (index == 0 || !g_session)
    {
        stopLocked();
        // The film's name: movie-<the game's film number>.mp4 (the opening film is movie-0). The films sit inside the
        // disc's big IMAGE*.IMG archives and are read with plain reads, so the stream sector (film-<sector>.mp4, the
        // first way) is usually unknown; it is still tried when the game gave one.
        const uint32_t key = filmId <= 0xFFu ? filmId : 0x10000u + g_streamSector.load();
        if (index != 0)
        {
            // a film already running when nothing was found for it, or one whose first picture never came here
            if (g_toldMissing.count(key) == 0 && g_toldLate.insert(key).second)
                say("joined at picture %s, not at its start: the original film plays",
                    filmId <= 0xFFu ? "movie-" + std::to_string(filmId) : std::string("(a film)"), std::to_string(index));
            return;
        }
        std::string name;
        std::wstring wpath;
        if (filmId <= 0xFFu)
        {
            name = "movie-" + std::to_string(filmId);
            wpath = findFilm(std::wstring(name.begin(), name.end()) + L".mp4");
        }
        const uint32_t sector = g_streamSector.load();
        if (wpath.empty() && sector)
        {
            const std::string old = "film-" + std::to_string(sector);
            wpath = findFilm(std::wstring(old.begin(), old.end()) + L".mp4");
            if (!wpath.empty() || name.empty())
                name = old;
        }
        if (wpath.empty())
        {
            if (g_toldMissing.insert(key).second)
            {
                if (name.empty())
                    say("%s", "(a film)", "started, but the game gave neither its number nor a disc sector: the original plays");
                else
                    say("not found, the original film plays (to replace it: %s)", name,
                        "textures\\SLES-52950\\replacements\\<pack>\\movies\\" + name + ".mp4");
            }
            return;
        }
        say("found: %s", name, utf8(wpath));
        auto s = std::make_shared<Session>();
        s->name = name;
        s->path = utf8(wpath);
        s->firstPts = pts90k;
        s->lastPicture.store(nowMs());
        g_session = s;
        std::thread(run, s).detach();
    }
    Session &s = *g_session;
    const long long t = (pts90k >= 0 && s.firstPts >= 0 && pts90k >= s.firstPts)
                            ? pts90k - s.firstPts
                            : static_cast<long long>(index) * 3003; // 29.97 frames/s
    s.target.store(t);
    s.pictures.store(index + 1);
    s.lastPicture.store(nowMs());
    SetEvent(s.wake);
}

// The film is over or its player is gone (sceMpegDelete, sceMpegCreate of the next one, end of the stream).
void sorFilmHdStop()
{
    std::lock_guard<std::mutex> lk(g_lock);
    stopLocked();
}

// The game is closing (ps2_runtime.cpp, before the game's window goes): the film's thread closes its window first.
// Waits at most 300 ms; meanwhile messages sent to this thread's windows are handled, so that thread never waits on us.
void sorFilmHdShutdown()
{
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        s = g_session;
        stopLocked();
    }
    if (!s)
        return;
    const long long until = nowMs() + 300;
    for (;;)
    {
        const long long left = until - nowMs();
        if (left <= 0)
            break;
        const DWORD r = MsgWaitForMultipleObjects(1, &s->done, FALSE, static_cast<DWORD>(left), QS_SENDMESSAGE);
        if (r != WAIT_OBJECT_0 + 1)
            break; // done, or the time is up
        MSG m;
        PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE); // runs the sent messages only
    }
}

#else

void sorFilmHdStreamStart(uint32_t) {}
void sorFilmHdPicture(uint32_t, long long, uint32_t) {}
void sorFilmHdStop() {}
void sorFilmHdShutdown() {}

#endif
