// sor-gs: PCSX2's GS (the PS2 graphics chip, drawn by the video card) as a DLL for Shadow of Rome Recompiled.
// The host side (Host:: stubs, config) comes from pcsx2-gsrunner/Main.cpp, included below with its entry points
// renamed. The game (sor.exe, built with llvm-mingw) loads this DLL (built with clang-cl) and talks to it only
// through the plain C functions at the bottom, so the two compilers never share a C++ type.
//
// Like PCSX2's own MTGS, the GS runs on a thread of its own, which owns the video card device: the game only copies
// what it sends (GIF packets, the display registers at each vsync) into a frame and goes on, and the GS thread
// replays the frames in order. The game waits only when it is more than 3 frames ahead of the video card.
// SPDX-License-Identifier: GPL-3.0+

#define wmain sorgs_unused_wmain
#define main sorgs_unused_main
#include "../pcsx2-gsrunner/Main.cpp"
#undef main
#undef wmain

#include "pcsx2/GS/GS.h"
#include "pcsx2/GS/GSPerfMon.h"
#include "pcsx2/GS/Renderers/Common/GSRenderer.h"

#include <chrono>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>
#include <cmath>
#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")

namespace
{
	enum : u32
	{
		REC_GIF = 1,  // payload: GIF tags and their data (see Normalize)
		REC_SNAP = 2, // payload: path of a PNG to save, with its NUL
	};

	struct Frame
	{
		std::vector<u8> data; // records: [u32 kind][u32 bytes][8 zero bytes][payload, padded to 16 bytes]
		u8 regs[0x2000];      // the privileged registers at this vsync
		u32 field = 0;
		bool vsync = false;
		bool quit = false;
		s8 wide = -1;         // widescreen: 1 wide, 0 4:3 (menus, the game's scenes), -1 as before
		s64 pushed_ns = 0;    // when the game handed it over (steady clock, ns)
	};

	// Game side: any game thread, under s_in_lock.
	std::mutex s_in_lock;
	std::atomic<int> s_wide_req{-1}; // sorgs_wide
	int s_wide_cur = -1;              // GS thread
	float s_wide_custom = 0.0f;      // 21:9 / 32:9: PCSX2's custom ratio
	std::vector<u8> s_cur;
	alignas(64) u8 s_regs_in[0x2000] = {}; // the game writes the privileged registers here (sorgs_regs)
	bool s_open = false;
	u64 s_frames = 0, s_stat_packets = 0, s_stat_bytes = 0, s_stat_cut = 0, s_cut_told = 0, s_dropped = 0;

	// Between the two: under s_q_lock.
	std::mutex s_q_lock;
	std::condition_variable s_q_data, s_q_space, s_q_opened;
	std::deque<std::unique_ptr<Frame>> s_queue;
	std::vector<std::vector<u8>> s_spare; // buffers the GS thread is done with, for the game side to reuse
	int s_open_result = 0; // 0 waiting, 1 open, -1 failed
	std::thread* s_gs_thread = nullptr; // a pointer: a std::thread still running at exit would call terminate()

	// GS thread side.
	alignas(64) u8 s_regs[0x2000] = {}; // the GS privileged registers, as at 0x12000000 on the PS2
	GSRendererType s_type = GSRendererType::Auto;
	HWND s_parent = nullptr;
	HANDLE s_ui_ready = nullptr;
	int s_last_w = 0, s_last_h = 0;
	constexpr UINT WM_SORGS_QUIT = WM_APP + 0x200;

	LRESULT CALLBACK SorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_CLOSE)
			return 0; // the game closes it, not the user
		if (msg == WM_SORGS_QUIT)
		{
			DestroyWindow(hwnd);
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}

	// The window lives on its own thread, which only pumps its messages: the game's threads never have to.
	DWORD WINAPI UiThread(LPVOID)
	{
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = SorWndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = L"SorGSWindow";
		RegisterClassExW(&wc);
		const DWORD ex = WS_EX_NOPARENTNOTIFY | (s_parent ? 0 : WS_EX_NOACTIVATE);
		const DWORD style = s_parent ? (WS_CHILD | WS_VISIBLE | WS_DISABLED) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
		RECT rc = {0, 0, 960, 720};
		if (s_parent)
			GetClientRect(s_parent, &rc);
		else
			AdjustWindowRectEx(&rc, style, FALSE, ex);
		s_hwnd = CreateWindowExW(ex, wc.lpszClassName, L"Shadow of Rome - video card (PCSX2 GS)", style,
			s_parent ? 0 : CW_USEDEFAULT, s_parent ? 0 : CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, s_parent,
			nullptr, wc.hInstance, nullptr);
		SetEvent(s_ui_ready);
		if (!s_hwnd)
			return 1;
		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		return 0;
	}

	bool InitializeHost()
	{
		EmuFolders::SetAppRoot();
		if (!EmuFolders::SetResourcesDirectory())
			return false;
		// Everything PCSX2 would write (shader cache, logs) stays in gpu-data next to the game, never in the
		// player's own Documents\PCSX2.
		EmuFolders::DataRoot = Path::Combine(EmuFolders::AppRoot, "gpu-data");
		FileSystem::CreateDirectoryPath(EmuFolders::DataRoot.c_str(), false);
		CrashHandler::SetWriteDirectory(EmuFolders::DataRoot);

		const char* error;
		if (!VMManager::PerformEarlyHardwareChecks(&error))
			return false;

		const std::string roboto_path =
			EmuFolders::GetOverridableResourcePath("fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
		const auto roboto_data = FileSystem::MapBinaryFileForRead(roboto_path.c_str());
		if (!roboto_data.empty())
		{
			std::vector<ImGuiManager::FontInfo> fonts;
			ImGuiManager::FontInfo fi{};
			fi.data = roboto_data;
			fi.exclude_ranges = {};
			fi.face_name = nullptr;
			fi.is_emoji_font = false;
			fonts.push_back(fi);
			ImGuiManager::SetFonts(std::move(fonts));
		}

		MemorySettingsInterface& si = s_settings_interface;
		Host::Internal::SetBaseSettingsLayer(&si);
		VMManager::SetDefaultSettings(si, true, true, true, true, true);
		VMManager::Internal::LoadStartupSettings();
		return true;
	}

	void PutQW(std::vector<u8>& out, u64 lo, u64 hi)
	{
		const size_t n = out.size();
		out.resize(n + 16);
		std::memcpy(out.data() + n, &lo, 8);
		std::memcpy(out.data() + n + 8, &hi, 8);
	}

	// `bytes` from src, padded with zeros to a quadword.
	void PutBytes(std::vector<u8>& out, const u8* src, size_t bytes)
	{
		const size_t n = out.size();
		const size_t padded = (bytes + 15) & ~static_cast<size_t>(15);
		out.resize(n + padded);
		std::memcpy(out.data() + n, src, bytes);
		if (padded > bytes)
			std::memset(out.data() + n + bytes, 0, padded - bytes);
	}

	u64 WithLoops(u64 lo, u32 nloop, bool keep_pre)
	{
		lo = (lo & ~static_cast<u64>(0x7FFF)) | (nloop & 0x7FFFu);
		return keep_pre ? lo : (lo & ~(static_cast<u64>(1) << 46));
	}

	u64 WithRegs(u64 lo, u32 nreg) { return (lo & ~(static_cast<u64>(0xF) << 60)) | (static_cast<u64>(nreg & 0xFu) << 60); }

	// Appends one packet (GIF tags and their data) to `out`, cut the way the game's CPU drawing reads it: every packet
	// stands alone, and a tag whose data runs past the end of the packet is drawn only as far as the data goes. PCSX2's
	// GS would instead wait for the rest and read the next packet as that rest (and with no data left in the middle of
	// a vertex it reads past the end of the packet: its PACKED loop is a do-while). So a cut tag becomes a tag for the
	// loops that are all there plus one for the registers of the unfinished loop: the GS ends every packet with nothing
	// pending, exactly as the CPU drawing does. Returns the number of cut tags.
	u32 Normalize(const u8* in, u32 qwc, std::vector<u8>& out)
	{
		u32 cut = 0;
		u32 q = 0;
		while (q < qwc)
		{
			u64 lo, hi;
			std::memcpy(&lo, in + static_cast<size_t>(q) * 16, 8);
			std::memcpy(&hi, in + static_cast<size_t>(q) * 16 + 8, 8);
			++q;
			const u32 nloop = static_cast<u32>(lo & 0x7FFF);
			const u32 flg = static_cast<u32>((lo >> 58) & 3);
			u32 nreg = static_cast<u32>((lo >> 60) & 0xF);
			if (nreg == 0)
				nreg = 16;
			const u8* body = in + static_cast<size_t>(q) * 16;
			const u32 avail = qwc - q; // quadwords after the tag
			u64 need;                  // quadwords the tag wants
			if (flg == 0)
				need = static_cast<u64>(nloop) * nreg;
			else if (flg == 1)
				need = (static_cast<u64>(nloop) * nreg + 1) / 2;
			else
				need = nloop;
			if (need <= avail)
			{
				PutQW(out, lo, hi);
				PutBytes(out, body, static_cast<size_t>(need) * 16);
				q += static_cast<u32>(need);
				continue;
			}
			++cut;
			if (s_cut_told < 8)
			{
				++s_cut_told;
				std::fprintf(stderr, "[sor-gs] cut tag at quadword %u of %u: flg %u nloop %u nreg %u, %u of %llu quadwords there\n",
					q - 1, qwc, flg, nloop, nreg, avail, static_cast<unsigned long long>(need));
			}
			if (flg == 0 || flg == 1)
			{
				const u32 unit = flg == 0 ? 16u : 8u; // bytes per register
				const u32 have = flg == 0 ? avail : avail * 2; // registers there
				const u32 full = have / nreg, part = have % nreg;
				if (full)
				{
					PutQW(out, WithLoops(lo, full, true), hi);
					PutBytes(out, body, static_cast<size_t>(full) * nreg * unit);
				}
				if (part)
				{
					PutQW(out, WithRegs(WithLoops(lo, 1, full == 0), part), hi);
					PutBytes(out, body + static_cast<size_t>(full) * nreg * unit, static_cast<size_t>(part) * unit);
				}
				if (!full && !part && flg == 0 && ((lo >> 46) & 1) && nloop)
				{
					// Only the tag is there: the GS still sets PRIM on reading it. A+D write of PRIM.
					PutQW(out, 1ull | (1ull << 15) | (1ull << 60), 0xE);
					PutQW(out, (lo >> 47) & 0x7FF, 0x00);
				}
			}
			else if (avail)
			{
				PutQW(out, WithLoops(lo, avail, true), hi); // IMAGE: the quadwords that are there
				PutBytes(out, body, static_cast<size_t>(avail) * 16);
			}
			break; // the data ran out: the packet ends here
		}
		return cut;
	}

	void ResizeIfNeeded()
	{
		if (s_parent)
		{
			RECT rc = {};
			GetClientRect(s_parent, &rc);
			if (rc.right != s_last_w || rc.bottom != s_last_h)
				SetWindowPos(s_hwnd, nullptr, 0, 0, rc.right, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
		}
		RECT cr = {};
		GetClientRect(s_hwnd, &cr);
		if (cr.right > 0 && cr.bottom > 0 && (cr.right != s_last_w || cr.bottom != s_last_h))
		{
			s_last_w = cr.right;
			s_last_h = cr.bottom;
			GSResizeDisplayWindow(cr.right, cr.bottom, 1.0f);
		}
	}

	u64 s_gs_frames = 0;
	std::atomic<u64> s_shown{0}; // pictures shown, read by the game (tests)
	// Timing, for the reports: the game waiting for the GS thread, and the GS thread's own pace.
	std::atomic<u64> s_wait_count{0}, s_wait_us{0};
	std::chrono::steady_clock::time_point s_gs_last_vsync{};
	double s_gs_worst_gap_ms = 0.0, s_gs_busy_ms = 0.0;

	// VSYNC (SORGS_VSYNC=1, from the launcher): every picture in step with the screen. A thread wakes at each of
	// the screen's refreshes (DXGI WaitForVBlank) and counts them. When the screen's rate is a whole multiple of
	// the game's (60 FPS on a 60, 120, 180 or 240 Hz screen; 50 FPS on 100, 150 or 200 Hz), the GS thread shows
	// each picture right after the refresh that has kept the one before on screen for exactly that many refreshes,
	// and the game's clock follows the screen's (sorgs_pace): no picture stays on screen longer than the others.
	// On other screens, only the video card's own vsync. SORGS_PACING=1: the reports, with VSYNC off.
	std::atomic<bool> s_vsync_on{false};
	bool s_pacing_on = false;
	std::atomic<u64> s_vb_count{0};     // the screen's refreshes so far
	std::atomic<s64> s_vb_period_ns{0}; // their period, measured (0: not known yet)
	std::atomic<bool> s_vb_quit{false};
	std::thread* s_vb_thread = nullptr;
	std::mutex s_vb_lock;
	std::condition_variable s_vb_tick;
	std::atomic<u32> s_game_us{0};      // the game's own period (16667 at 60 FPS, 20000 at 50), from sorgs_pace
	std::atomic<int> s_slot_step{0};    // refreshes per picture when in step, 0 = not in step
	std::atomic<s64> s_nudge_ns{0};     // the next small change to the game's clock, from the GS thread
	std::atomic<u64> s_vs_in{0}, s_vs_out{0}; // pictures handed over by the game, and shown
	constexpr s64 kNudgeMaxNs = 200000; // at most 0.2 ms a picture (1.2%): the game's speed stays the same
	constexpr double kWaitAimMs = 4.0;  // each picture ready about 4 ms before its refresh: room for a heavier one
	// Reports, GS thread only.
	u64 s_slot_last = 0, s_long_told = 0;
	u32 s_pace_hist[6] = {}; // pictures that stayed on screen 0 (never seen), 1, 2, 3, 4, 5+ refreshes
	u32 s_pace_late = 0;
	double s_pace_waited_ms = 0.0;
	s64 s_last_shown_ns = 0, s_last_pushed_ns = 0;
	double s_lat_sum_ms = 0.0, s_lat_max_ms = 0.0; // game -> screen, for the pacing report
	u32 s_lat_n = 0;

	s64 VsNowNs()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// The DXGI output (screen) that shows `mon`; the first one if none does.
	IDXGIOutput* VsFindOutput(HMONITOR mon)
	{
		IDXGIFactory1* factory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
			return nullptr;
		IDXGIOutput* found = nullptr;
		IDXGIOutput* first = nullptr;
		IDXGIAdapter1* adapter = nullptr;
		for (UINT a = 0; !found && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a)
		{
			IDXGIOutput* o = nullptr;
			for (UINT i = 0; !found && adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				DXGI_OUTPUT_DESC desc = {};
				if (SUCCEEDED(o->GetDesc(&desc)) && desc.Monitor == mon)
					found = o;
				else if (!first)
					first = o;
				else
					o->Release();
			}
			adapter->Release();
		}
		factory->Release();
		if (!found)
			return first;
		if (first)
			first->Release();
		return found;
	}

	void VsClockThread()
	{
		HMONITOR mon = MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTOPRIMARY);
		IDXGIOutput* out = VsFindOutput(mon);
		if (!out)
		{
			std::fprintf(stderr, "[sor-gs] vsync: no screen found, the pictures are not in step\n");
			return;
		}
		constexpr int N = 128; // the period from the last 128 refreshes
		s64 times[N];
		u64 counts[N];
		int have = 0, head = 0, fails = 0;
		s64 prev = 0, period = 0;
		auto checked = std::chrono::steady_clock::now();
		while (!s_vb_quit.load(std::memory_order_relaxed))
		{
			if (FAILED(out->WaitForVBlank()))
			{
				if (++fails > 10) // the screen is off or gone: not in step until it is back
				{
					s_vb_period_ns.store(0, std::memory_order_relaxed);
					have = head = 0;
					prev = period = 0;
				}
				Sleep(5);
				continue;
			}
			fails = 0;
			const s64 t = VsNowNs();
			u64 step = 1;
			if (prev && period > 0)
			{
				const double r = static_cast<double>(t - prev) / static_cast<double>(period);
				if (r < 0.5) // woken twice for the same refresh
					continue;
				if (r > 1.5) // refreshes this thread missed (a busy PC): counted all the same
					step = static_cast<u64>(std::llround(r));
			}
			if (prev && !period)
				period = t - prev; // a rough first value, until there are enough
			prev = t;
			const u64 c = s_vb_count.load(std::memory_order_relaxed) + step;
			times[head] = t;
			counts[head] = c;
			head = (head + 1) % N;
			if (have < N)
				++have;
			if (have >= 16)
			{
				// The straight line through all of them (least squares): a late wake-up of this thread at either
				// end no longer moves the period (with only the two ends, 180 Hz was sometimes read as 179.5).
				const int oldest = (head - have + N) % N;
				double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
				for (int k = 0; k < have; ++k)
				{
					const int i = (oldest + k) % N;
					const double x = static_cast<double>(counts[i] - counts[oldest]);
					const double y = static_cast<double>(times[i] - times[oldest]);
					sx += x;
					sy += y;
					sxx += x * x;
					sxy += x * y;
				}
				const double den = have * sxx - sx * sx;
				if (den > 0.0)
				{
					period = static_cast<s64>((have * sxy - sx * sy) / den);
					s_vb_period_ns.store(period, std::memory_order_relaxed);
				}
			}
			{
				std::lock_guard<std::mutex> lk(s_vb_lock);
				s_vb_count.store(c, std::memory_order_release);
			}
			s_vb_tick.notify_all();
			// The game's window moved to another screen: follow it.
			const auto now = std::chrono::steady_clock::now();
			if (now - checked > std::chrono::seconds(2))
			{
				checked = now;
				const HMONITOR now_mon = MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTOPRIMARY);
				if (now_mon != mon)
				{
					if (IDXGIOutput* o = VsFindOutput(now_mon))
					{
						out->Release();
						out = o;
						mon = now_mon;
						have = head = 0;
						prev = period = 0;
						s_vb_period_ns.store(0, std::memory_order_relaxed);
					}
				}
			}
		}
		out->Release();
	}

	void VsStop()
	{
		if (!s_vb_thread)
			return;
		s_vb_quit.store(true);
		s_vb_thread->join();
		delete s_vb_thread;
		s_vb_thread = nullptr;
	}

	// Refreshes per picture that keep the game in step with this screen (3 for 60 FPS on 180 Hz); 0 if none.
	int VsStepFor(s64 period_ns, u32 game_us)
	{
		if (period_ns <= 0 || !game_us)
			return 0;
		const double r = game_us * 1000.0 / static_cast<double>(period_ns);
		const int n = static_cast<int>(std::lround(r));
		return n >= 1 && n <= 8 && std::fabs(r - n) <= 0.01 * n ? n : 0;
	}

	// VSYNC: waits for the refresh that has kept the picture before on screen exactly `step` refreshes, and tells
	// the game's clock how early this picture was (sorgs_pace).
	void VsWaitForSlot()
	{
		const u32 game_us = s_game_us.load(std::memory_order_relaxed);
		const int step = VsStepFor(s_vb_period_ns.load(std::memory_order_relaxed), game_us);
		s_slot_step.store(step, std::memory_order_relaxed);
		if (!step || !s_slot_last)
			return;
		const u64 target = s_slot_last + static_cast<u64>(step);
		if (s_vb_count.load(std::memory_order_acquire) > target)
		{
			// Its refresh has gone: the picture before stays on screen a refresh longer, and the game's clock
			// moves a little earlier.
			++s_pace_late;
			s_nudge_ns.store(-kNudgeMaxNs, std::memory_order_relaxed);
			return;
		}
		const s64 t0 = VsNowNs();
		{
			// Never longer than 3 of the game's frames (a stuck clock must not freeze the picture), and not at all
			// when the game's next picture is already here (the game is ahead of the screen).
			std::unique_lock<std::mutex> lk(s_vb_lock);
			s_vb_tick.wait_for(lk, std::chrono::microseconds(3 * game_us), [target] {
				return s_vb_count.load(std::memory_order_acquire) >= target ||
					s_vs_in.load(std::memory_order_relaxed) > s_vs_out.load(std::memory_order_relaxed) + 1;
			});
		}
		const double waited = static_cast<double>(VsNowNs() - t0) / 1e6;
		s_pace_waited_ms += waited;
		s_nudge_ns.store(std::clamp<s64>(static_cast<s64>((waited - kWaitAimMs) * 1e6) / 8, -kNudgeMaxNs, kNudgeMaxNs),
			std::memory_order_relaxed);
	}

	// After each picture: how many refreshes the one before stayed on screen, and the long gaps (from the game's
	// side, a picture handed over late; from the GS's, a picture slow to draw).
	void VsNotePresent(s64 pushed_ns)
	{
		const u64 c = s_vb_count.load(std::memory_order_acquire);
		const s64 now = VsNowNs();
		if (s_slot_last && c >= s_slot_last)
			++s_pace_hist[std::min<u64>(c - s_slot_last, 5)];
		s_slot_last = c;
		const u32 game_us = s_game_us.load(std::memory_order_relaxed);
		const double frame_ms = (game_us ? game_us : 16667u) / 1000.0;
		if (s_last_shown_ns && s_long_told < 300)
		{
			const double gap = static_cast<double>(now - s_last_shown_ns) / 1e6;
			if (gap > 1.7 * frame_ms)
			{
				++s_long_told;
				std::fprintf(stderr, "[sor-gs] long gap %.1f ms before picture %llu: the game handed it over %.1f ms after the one before, %.1f ms from the game to the screen\n",
					gap, static_cast<unsigned long long>(s_gs_frames + 1), static_cast<double>(pushed_ns - s_last_pushed_ns) / 1e6,
					static_cast<double>(now - pushed_ns) / 1e6);
			}
		}
		{
			const double l = static_cast<double>(now - pushed_ns) / 1e6;
			s_lat_sum_ms += l;
			s_lat_max_ms = std::max(s_lat_max_ms, l);
			++s_lat_n;
		}
		s_last_shown_ns = now;
		s_last_pushed_ns = pushed_ns;
	}

	void VsPrintPacing()
	{
		if (!s_vsync_on && !s_pacing_on)
			return;
		const s64 p = s_vb_period_ns.load(std::memory_order_relaxed);
		const int step = s_slot_step.load(std::memory_order_relaxed);
		char mode[64];
		if (!s_vsync_on)
			std::snprintf(mode, sizeof(mode), "vsync off");
		else if (step)
			std::snprintf(mode, sizeof(mode), "vsync in step, %d refreshes a picture", step);
		else
			std::snprintf(mode, sizeof(mode), "vsync on, the screen is not a multiple of the game");
		std::fprintf(stderr, "[sor-gs] pacing: refreshes per picture 0:%u 1:%u 2:%u 3:%u 4:%u 5+:%u; screen %.2f Hz; %s; late %u; waited %.1f ms a picture; game to screen %.1f ms (max %.1f)\n",
			s_pace_hist[0], s_pace_hist[1], s_pace_hist[2], s_pace_hist[3], s_pace_hist[4], s_pace_hist[5],
			p > 0 ? 1e9 / static_cast<double>(p) : 0.0, mode, s_pace_late, s_pace_waited_ms / 250.0, s_lat_n ? s_lat_sum_ms / s_lat_n : 0.0, s_lat_max_ms);
		s_lat_sum_ms = s_lat_max_ms = 0.0;
		s_lat_n = 0;
		std::fflush(stderr);
		std::memset(s_pace_hist, 0, sizeof(s_pace_hist));
		s_pace_late = 0;
		s_pace_waited_ms = 0.0;
	}

	// What the GS thinks it shows, and how much it draws (averages per frame from PCSX2's own counters).
	void PrintStats()
	{
		const auto& d = g_gs_renderer->PCRTCDisplays.PCRTCDisplays;
		const auto now = std::chrono::steady_clock::now();
		static auto s_prev = now;
		const double secs = std::chrono::duration<double>(now - s_prev).count();
		s_prev = now;
		const double fps = secs > 0.0 ? 250.0 / secs : 0.0;
		std::fprintf(stderr,
			"[sor-gs] gs frame %llu: video mode %d; circuit 1 %s fbp %x fbw %d psm %d screen %d,%d-%d,%d read %d,%d-%d,%d;"
			" circuit 2 %s fbp %x; per frame: draws %.1f prims %.1f uploads %.1f readbacks %.1f;"
			" GS thread %.1f frames/s, longest gap %.1f ms, busy %.1f ms/frame\n",
			static_cast<unsigned long long>(s_gs_frames), static_cast<int>(g_gs_renderer->GetVideoMode()),
			d[0].enabled ? "on" : "off", d[0].FBP, d[0].FBW, d[0].PSM, d[0].displayRect.x, d[0].displayRect.y,
			d[0].displayRect.z, d[0].displayRect.w, d[0].framebufferRect.x, d[0].framebufferRect.y,
			d[0].framebufferRect.z, d[0].framebufferRect.w, d[1].enabled ? "on" : "off", d[1].FBP,
			g_perfmon.Get(GSPerfMon::DrawCalls), g_perfmon.Get(GSPerfMon::Prim), g_perfmon.Get(GSPerfMon::TextureUploads),
			g_perfmon.Get(GSPerfMon::Readbacks), fps, s_gs_worst_gap_ms, s_gs_busy_ms / 250.0);
		std::fflush(stderr);
		s_gs_worst_gap_ms = 0.0;
		s_gs_busy_ms = 0.0;
		VsPrintPacing();
	}

	void GsThread()
	{
		std::memset(s_regs, 0, sizeof(s_regs));
		const bool ok = GSopen(EmuConfig.GS, s_type, s_regs, s_vsync_on ? GSVSyncMode::FIFO : GSVSyncMode::Disabled, false);
		{
			std::lock_guard<std::mutex> lk(s_q_lock);
			s_open_result = ok ? 1 : -1;
		}
		s_q_opened.notify_all();
		if (!ok)
			return;

		for (;;)
		{
			std::unique_ptr<Frame> f;
			{
				std::unique_lock<std::mutex> lk(s_q_lock);
				s_q_data.wait(lk, [] { return !s_queue.empty(); });
				f = std::move(s_queue.front());
				s_queue.pop_front();
			}
			s_q_space.notify_all();
			if (f->quit)
				break;
			const auto busy0 = std::chrono::steady_clock::now();

			const u8* p = f->data.data();
			const u8* const end = p + f->data.size();
			while (p + 16 <= end)
			{
				u32 kind, bytes;
				std::memcpy(&kind, p, 4);
				std::memcpy(&bytes, p + 4, 4);
				p += 16;
				const size_t padded = (static_cast<size_t>(bytes) + 15) & ~static_cast<size_t>(15);
				if (padded > static_cast<size_t>(end - p))
					break;
				if (kind == REC_GIF && bytes >= 16)
					GSgifTransfer(p, bytes / 16);
				else if (kind == REC_SNAP && bytes)
					GSQueueSnapshot(std::string(reinterpret_cast<const char*>(p)), 0);
				p += padded;
			}
			if (f->vsync)
			{
				std::memcpy(s_regs, f->regs, sizeof(s_regs));
				ResizeIfNeeded();
				const auto w0 = std::chrono::steady_clock::now();
				if (s_vsync_on)
					VsWaitForSlot();
				const auto w1 = std::chrono::steady_clock::now();
				if (f->wide >= 0 && f->wide != s_wide_cur)
				{   // the shape changes with the picture it belongs to; resolution, sharpness and VSYNC stay
					s_wide_cur = f->wide;
					EmuConfig.CurrentAspectRatio = f->wide ? EmuConfig.GS.AspectRatio : AspectRatioType::R4_3;
					EmuConfig.CurrentCustomAspectRatio = f->wide ? s_wide_custom : 0.0f;
				}
				GSvsync(f->field, true);
				const auto done = std::chrono::steady_clock::now();
				if (s_vsync_on || s_pacing_on)
					VsNotePresent(f->pushed_ns);
				s_vs_out.fetch_add(1, std::memory_order_relaxed);
				s_gs_busy_ms += std::chrono::duration<double, std::milli>((w0 - busy0) + (done - w1)).count();
				if (s_gs_last_vsync.time_since_epoch().count() != 0)
					s_gs_worst_gap_ms = std::max(s_gs_worst_gap_ms, std::chrono::duration<double, std::milli>(done - s_gs_last_vsync).count());
				s_gs_last_vsync = done;
				s_shown.fetch_add(1, std::memory_order_relaxed);
				if (++s_gs_frames % 250 == 0)
					PrintStats();
			}
			f->data.clear();
			std::lock_guard<std::mutex> lk(s_q_lock);
			if (s_spare.size() < 4)
				s_spare.push_back(std::move(f->data));
		}
		GSclose();
	}

	void AppendRecord(u32 kind, const void* src, u32 bytes)
	{
		const size_t n = s_cur.size();
		const size_t padded = (static_cast<size_t>(bytes) + 15) & ~static_cast<size_t>(15);
		s_cur.resize(n + 16 + padded);
		u8* d = s_cur.data() + n;
		std::memcpy(d, &kind, 4);
		std::memcpy(d + 4, &bytes, 4);
		std::memset(d + 8, 0, 8);
		std::memcpy(d + 16, src, bytes);
		if (padded > bytes)
			std::memset(d + 16 + bytes, 0, padded - bytes);
	}

	void Push(std::unique_ptr<Frame> f)
	{
		{
			std::unique_lock<std::mutex> lk(s_q_lock);
			// More than 3 frames ahead of the video card: wait. Never forever (a stuck GS must not freeze the game).
			if (s_queue.size() >= 3)
			{
				const auto t0 = std::chrono::steady_clock::now();
				const bool room = s_q_space.wait_for(lk, std::chrono::seconds(2), [] { return s_queue.size() < 3; });
				s_wait_count.fetch_add(1, std::memory_order_relaxed);
				s_wait_us.fetch_add(static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - t0).count()), std::memory_order_relaxed);
				if (!room)
				{
					if (s_dropped++ < 4)
						std::fprintf(stderr, "[sor-gs] the GS thread is not keeping up: frame dropped\n");
					return;
				}
			}
			if (f->vsync)
				s_vs_in.fetch_add(1, std::memory_order_relaxed);
			s_queue.push_back(std::move(f));
		}
		s_q_data.notify_one();
	}
}

#define SORGS_API extern "C" __declspec(dllexport)

// Opens the GS and its window. parent: the game's window to draw inside, or null for a window of its own.
// upscale: internal resolution multiplier (1 = the PS2's). renderer: 0 auto, 1 DX11, 2 DX12, 3 Vulkan, 4 OpenGL,
// 5 software. Returns 1 on success. SORGS_VSYNC=1: every picture in step with the screen. Tests: SORGS_OSD=1 shows
// speed, resolution and GS statistics; SORGS_PACING=1 reports how many refreshes each picture stays on screen.
SORGS_API int sorgs_open(void* parent, float upscale, int renderer)
{
	std::lock_guard<std::mutex> in(s_in_lock);
	if (s_open)
		return 1;
	if (!InitializeHost())
		return 0;
	GSRunner::SettingsOverride();
	MemorySettingsInterface& si = s_settings_interface;
	si.SetBoolValue("Logging", "EnableSystemConsole", false);
	si.SetBoolValue("Logging", "EnableVerbose", false);
	// Tests: SORGS_LOG=1 writes PCSX2's own messages (why a renderer did not start, and so on) to
	// gpu-data\logs\sor-gs.log.
	if (const char* lg = std::getenv("SORGS_LOG"); lg && lg[0] == '1')
		Log::SetFileOutputLevel(LOGLEVEL_DEBUG, Path::Combine(EmuFolders::DataRoot, "logs" FS_OSPATH_SEPARATOR_STR "sor-gs.log"));
	const char* osd = std::getenv("SORGS_OSD");
	const bool show = osd && osd[0] == '1';
	si.SetBoolValue("EmuCore/GS", "OsdShowFPS", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowResolution", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowGSStats", show);
	// With the statistics, the frame-time graph (every hitch is a spike) and the GPU load.
	si.SetBoolValue("EmuCore/GS", "OsdShowFrameTimes", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowGPU", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowVPS", show);
	// No PCSX2 messages on the player's screen ("Saved screenshot..."): only in tests, with the statistics.
	si.SetIntValue("EmuCore/GS", "OsdMessagesPos", static_cast<int>(show ? OsdOverlayPos::TopLeft : OsdOverlayPos::None));
	// Test snapshots at the resolution the GS draws at, not the window's: they show the upscale as it is.
	si.SetIntValue("EmuCore/GS", "ScreenshotSize", static_cast<int>(GSScreenshotSize::InternalResolution));
	si.SetFloatValue("EmuCore/GS", "upscale_multiplier", upscale < 1.0f ? 1.0f : upscale);
	// PCSX2's own fixes for this game (GameIndex.yaml, SLES-52950): without them the upscaled picture has
	// thin lines and blurred, shifted post effects. halfPixelOffset 5 = align to native, nativeScaling 2 = aggressive.
	si.SetBoolValue("EmuCore/GS", "UserHacks", true);
	si.SetIntValue("EmuCore/GS", "UserHacks_HalfPixelOffset", 5);
	si.SetIntValue("EmuCore/GS", "UserHacks_native_scaling", 2);
	if (const char* ns = std::getenv("SORGS_NATIVESCALING"); ns && ns[0] >= '0' && ns[0] <= '4') // tests
		si.SetIntValue("EmuCore/GS", "UserHacks_native_scaling", ns[0] - '0');
	if (const char* hp = std::getenv("SORGS_HALFPIXEL"); hp && hp[0] >= '0' && hp[0] <= '5') // tests
		si.SetIntValue("EmuCore/GS", "UserHacks_HalfPixelOffset", hp[0] - '0');
	// Interlacing off, as PCSX2's "No-Interlacing" patch for this game asks (gsinterlacemode=1): with the
	// game's half-line shift removed (pc/widescreen_gen.py) the automatic deinterlacer still made the picture
	// bob up and down by a hair. SOR_INTERLACE=1 keeps the PS2 behaviour.
	if (const char* il = std::getenv("SOR_INTERLACE"); !(il && il[0] == '1'))
		si.SetIntValue("EmuCore/GS", "deinterlace_mode", static_cast<int>(GSInterlaceMode::Off));
	// Sharpness (SORGS_SHARPNESS, from the launcher): 0 = as the PS2 (PCSX2's defaults), 1 = low, 2 = high:
	// anisotropic filtering (textures seen at an angle, floors and walls, stay sharp) and CAS sharpening after
	// the upscale. The game's own textures and models do not change.
	if (const char* sh = std::getenv("SORGS_SHARPNESS"); sh && (sh[0] == '1' || sh[0] == '2'))
	{
		const bool high = sh[0] == '2';
		si.SetIntValue("EmuCore/GS", "MaxAnisotropy", high ? 16 : 8);
		si.SetIntValue("EmuCore/GS", "CASMode", static_cast<int>(GSCASMode::SharpenOnly));
		si.SetIntValue("EmuCore/GS", "CASSharpness", high ? 80 : 40);
	}
	// Replaceable textures, as in PCSX2: PNGs in textures\SLES-52950\replacements next to the game are used in
	// place of the originals (same names as PCSX2's dumps). SORGS_TEXDUMP=1 saves every texture the game uses in
	// textures\SLES-52950\dumps, to find the ones to redo.
	_putenv_s("SORGS_SERIAL", "SLES-52950");
	// Widescreen 16:9 (SOR_WIDESCREEN=1): the game draws a wider view (patched camera), shown at 16:9; the
	// movies are 4:3 and stay so.
	// 21:9 (SOR_WIDESCREEN=2): PCSX2 has no 21:9 choice; "Auto" with a custom ratio is what its widescreen patches use.
	const char* wsm = std::getenv("SOR_WIDESCREEN");
	if (wsm && (wsm[0] == '2' || wsm[0] == '3'))
	{
		si.SetStringValue("EmuCore/GS", "AspectRatio", "Auto 4:3/3:2");
		si.SetStringValue("EmuCore/GS", "FMVAspectRatioSwitch", "4:3");
	}
	else if (wsm && wsm[0] == '1')
	{
		si.SetStringValue("EmuCore/GS", "AspectRatio", "16:9");
		si.SetStringValue("EmuCore/GS", "FMVAspectRatioSwitch", "4:3");
	}
	EmuFolders::Textures = Path::Combine(EmuFolders::AppRoot, "textures");
	// SORGS_MODS=0 (launcher, MODS: OFF): the game's own textures only.
	const char* mods = std::getenv("SORGS_MODS");
	si.SetBoolValue("EmuCore/GS", "LoadTextureReplacements", !(mods && mods[0] == '0'));
	si.SetBoolValue("EmuCore/GS", "LoadTextureReplacementsAsync", true);
	if (const char* td = std::getenv("SORGS_TEXDUMP"); td && td[0] == '1')
	{
		FileSystem::CreateDirectoryPath(EmuFolders::Textures.c_str(), false);
		si.SetBoolValue("EmuCore/GS", "DumpReplaceableTextures", true);
	}
	if (const char* dump = std::getenv("SORGS_DUMP")) // tests: "<first frame> <frames>" saves every draw of those frames
	{
		int first = 0, count = 1;
		std::sscanf(dump, "%d %d", &first, &count);
		const std::string dir = Path::Combine(EmuFolders::DataRoot, "dump");
		FileSystem::CreateDirectoryPath(dir.c_str(), false);
		for (const char* key : {"DumpGSData", "SaveRT", "SaveFrame", "SaveTexture", "SaveInfo", "SaveAlpha"})
			si.SetBoolValue("EmuCore/GS", key, true);
		si.SetIntValue("EmuCore/GS", "SaveDrawStart", 0);
		si.SetIntValue("EmuCore/GS", "SaveDrawCount", -1);
		si.SetIntValue("EmuCore/GS", "SaveDrawBy", 1);
		si.SetIntValue("EmuCore/GS", "SaveFrameStart", first);
		si.SetIntValue("EmuCore/GS", "SaveFrameCount", count);
		si.SetIntValue("EmuCore/GS", "SaveFrameBy", 1);
		si.SetStringValue("EmuCore/GS", "HWDumpDirectory", dir.c_str());
		si.SetStringValue("EmuCore/GS", "SWDumpDirectory", dir.c_str());
	}
	{
		SettingsLoadWrapper slw(si);
		EmuConfig.LoadSave(slw);
	}
	if (wsm && (wsm[0] == '2' || wsm[0] == '3'))
		EmuConfig.CurrentCustomAspectRatio = wsm[0] == '3' ? 32.0f / 9.0f : 64.0f / 27.0f;
	s_wide_custom = EmuConfig.CurrentCustomAspectRatio;
	s_wide_cur = -1;
	if (EmuConfig.GS.CASMode != GSCASMode::Disabled || EmuConfig.GS.MaxAnisotropy > 1)
		std::fprintf(stderr, "[sor-gs] sharpness: anisotropic %dx, CAS %d%%\n", static_cast<int>(EmuConfig.GS.MaxAnisotropy),
			static_cast<int>(EmuConfig.GS.CAS_Sharpness));

	s_parent = static_cast<HWND>(parent);
	s_ui_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	HANDLE th = CreateThread(nullptr, 0, UiThread, nullptr, 0, nullptr);
	if (!th)
		return 0;
	CloseHandle(th);
	WaitForSingleObject(s_ui_ready, 10000);
	if (!s_hwnd)
		return 0;

	// VSYNC (SORGS_VSYNC=1, from the launcher): every picture in step with the screen (VsClockThread).
	// SORGS_PACING=1: only the reports, with VSYNC off.
	const char* vs = std::getenv("SORGS_VSYNC");
	s_vsync_on.store(vs && vs[0] == '1');
	const char* pacing = std::getenv("SORGS_PACING");
	s_pacing_on = pacing && pacing[0] == '1';
	if ((s_vsync_on || s_pacing_on) && !s_vb_thread)
	{
		s_vb_quit.store(false);
		s_vb_thread = new std::thread(VsClockThread);
	}

	switch (renderer)
	{
		case 1: s_type = GSRendererType::DX11; break;
		case 2: s_type = GSRendererType::DX12; break;
		case 3: s_type = GSRendererType::VK; break;
		case 4: s_type = GSRendererType::OGL; break;
		case 5: s_type = GSRendererType::SW; break;
		default: s_type = GSRendererType::Auto; break;
	}
	s_open_result = 0;
	s_gs_thread = new std::thread(GsThread);
	{
		std::unique_lock<std::mutex> lk(s_q_lock);
		s_q_opened.wait(lk, [] { return s_open_result != 0; });
	}
	if (s_open_result < 0)
	{
		s_gs_thread->join();
		delete s_gs_thread;
		s_gs_thread = nullptr;
		VsStop();
		PostMessageW(s_hwnd, WM_SORGS_QUIT, 0, 0);
		s_hwnd = nullptr;
		return 0;
	}
	std::memset(s_regs_in, 0, sizeof(s_regs_in));
	s_cur.reserve(4u << 20);
	s_open = true;
	return 1;
}

// The privileged registers (PMODE, SMODE1, SMODE2, DISPFB1/2, DISPLAY1/2, BGCOLOR... CSR at +0x1000): the game
// writes them here before each vsync.
// Widescreen (sor.exe, gs_pcsx2_mirror.cpp): 1 while the gameplay camera is in use, 0 in menus and the game's own
// scenes, shown 4:3 with black bars. Read at the next vsync.
SORGS_API void sorgs_wide(int on)
{
	s_wide_req.store(on ? 1 : 0, std::memory_order_relaxed);
}

SORGS_API void* sorgs_regs()
{
	return s_regs_in;
}

// A GIF packet (PATH1, PATH2 or PATH3, as the game's GS received it): `qwc` quadwords of 16 bytes.
SORGS_API void sorgs_gif(const void* data, unsigned int qwc)
{
	std::lock_guard<std::mutex> in(s_in_lock);
	if (!s_open || !data || !qwc)
		return;
	const size_t head = s_cur.size();
	s_cur.resize(head + 16);
	const u32 cut = Normalize(static_cast<const u8*>(data), qwc, s_cur);
	const u32 bytes = static_cast<u32>(s_cur.size() - head - 16);
	if (!bytes)
	{
		s_cur.resize(head);
		return;
	}
	const u32 kind = REC_GIF;
	std::memcpy(s_cur.data() + head, &kind, 4);
	std::memcpy(s_cur.data() + head + 4, &bytes, 4);
	std::memset(s_cur.data() + head + 8, 0, 8);
	++s_stat_packets;
	s_stat_bytes += static_cast<u64>(qwc) * 16;
	s_stat_cut += cut;
}

// The end of a frame: the GS thread replays it and shows it. field: 0 or 1 (interlace).
SORGS_API void sorgs_vsync(unsigned int field)
{
	auto f = std::make_unique<Frame>();
	std::vector<u8> next;
	{
		std::lock_guard<std::mutex> lk(s_q_lock);
		if (!s_spare.empty())
		{
			next = std::move(s_spare.back());
			s_spare.pop_back();
		}
	}
	if (next.capacity() < (1u << 20))
		next.reserve(4u << 20);
	{
		std::lock_guard<std::mutex> in(s_in_lock);
		if (!s_open)
			return;
		f->data.swap(s_cur);
		s_cur.swap(next);
		std::memcpy(f->regs, s_regs_in, sizeof(s_regs_in));
		if (++s_frames % 250 == 0)
		{
			std::fprintf(stderr, "[sor-gs] frame %llu: in the last 250, %llu packets, %llu KB, %llu cut tags; waited for the GS thread %llu times, %llu ms\n",
				static_cast<unsigned long long>(s_frames), static_cast<unsigned long long>(s_stat_packets),
				static_cast<unsigned long long>(s_stat_bytes >> 10), static_cast<unsigned long long>(s_stat_cut),
				static_cast<unsigned long long>(s_wait_count.exchange(0)), static_cast<unsigned long long>(s_wait_us.exchange(0) / 1000));
			std::fflush(stderr);
			s_stat_packets = s_stat_bytes = s_stat_cut = 0;
		}
	}
	f->field = field;
	f->vsync = true;
	f->wide = static_cast<s8>(s_wide_req.load(std::memory_order_relaxed));
	f->pushed_ns = VsNowNs();
	Push(std::move(f));
}

// Saves what the GS shows as a PNG (for tests), at the next vsync.
SORGS_API unsigned long long sorgs_frames_shown()
{
	return s_shown.load(std::memory_order_relaxed);
}

// The game's clock (EeScheduler, at each vblank). With VSYNC on and the screen's rate a whole multiple of the
// game's: the time to the game's next vblank, in ns, following the screen (that many of its refreshes, plus the
// small change that keeps each picture ready just before its refresh). 0: the game's own period, game_us.
SORGS_API long long sorgs_pace(unsigned int game_us)
{
	s_game_us.store(game_us, std::memory_order_relaxed);
	if (!s_vsync_on)
		return 0;
	const s64 period = s_vb_period_ns.load(std::memory_order_relaxed);
	const int step = VsStepFor(period, game_us);
	if (!step)
		return 0;
	const s64 own = static_cast<s64>(game_us) * 1000;
	const s64 ns = step * period + s_nudge_ns.exchange(0, std::memory_order_relaxed);
	return std::clamp<s64>(ns, own - own / 50, own + own / 50);
}

SORGS_API void sorgs_snapshot(const char* path)
{
	std::lock_guard<std::mutex> in(s_in_lock);
	if (s_open && path)
		AppendRecord(REC_SNAP, path, static_cast<u32>(std::strlen(path) + 1));
}

SORGS_API void sorgs_close()
{
	{
		std::lock_guard<std::mutex> in(s_in_lock);
		if (!s_open)
			return;
		s_open = false;
	}
	auto f = std::make_unique<Frame>();
	f->quit = true;
	Push(std::move(f));
	if (s_gs_thread)
	{
		s_gs_thread->join();
		delete s_gs_thread;
		s_gs_thread = nullptr;
	}
	s_vsync_on.store(false);
	VsStop();
	if (s_hwnd)
		PostMessageW(s_hwnd, WM_SORGS_QUIT, 0, 0);
	s_hwnd = nullptr;
}
