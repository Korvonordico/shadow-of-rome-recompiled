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
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/GS/SorGsTimes.h"
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

// Debug view (launcher, DEBUG page, WIREFRAME row: SORGS_WIREFRAME=1; F7 in the game): the game's triangles drawn
// as wires. Defined in pcsx2/GS/Renderers/HW/GSRendererHW.cpp, honoured by DX11 only (GSDevice11::RenderHW).
extern bool g_sor_wireframe;

// OSD without PCSX2's VM (launcher, GRAPHICS page, OSD row: SORGS_OSD=1). Defined in pcsx2/ImGui/ImGuiManager.cpp.
extern bool g_sor_overlays_without_vm;
extern float g_sor_overlay_target_fps;

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
	std::atomic<int> s_wire_req{0};   // sorgs_wireframe: 1 wires, read by the GS thread before each frame
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
	std::atomic<u64> s_shown{0}; // pictures shown, read by the game (black box, tests)

	// Small pictures for the game's black box (sorgs_grab, 11/11/2026: with the video card F9 saved no pictures,
	// because the picture never reaches the game). Only after the game has asked once (no cost otherwise), 5 a second:
	// the shown picture is scaled down on the video card and copied to a download texture without waiting; the
	// copy is read at the next grab, when the video card has long finished it, so the game's pace is not touched.
	std::atomic<bool> s_grab_on{false};
	std::mutex s_grab_lock;
	std::vector<u8> s_grab_px;   // RGBA, s_grab_w x s_grab_h
	u32 s_grab_w = 0, s_grab_h = 0;
	u64 s_grab_seq = 0;           // goes up with every new picture
	GSTexture* s_grab_rt = nullptr;
	std::unique_ptr<GSDownloadTexture> s_grab_dl;
	u32 s_grab_rt_w = 0, s_grab_rt_h = 0;
	bool s_grab_pending = false;
	u32 s_grab_tick = 0;
	bool s_grab_failed = false;

	void SorGrabRelease() // once, before GSclose: given back to PCSX2's pool, as its own screenshots do
	{
		s_grab_dl.reset();
		if (s_grab_rt && g_gs_device)
			g_gs_device->Recycle(s_grab_rt);
		s_grab_rt = nullptr;
		s_grab_rt_w = s_grab_rt_h = 0;
		s_grab_pending = false;
	}

	void SorGrab() // on the GS thread, right after GSvsync
	{
		if (!s_grab_on.load(std::memory_order_relaxed) || !g_gs_device)
			return;
		if (++s_grab_tick % 12 != 0) // 60 pictures a second: 5 grabs
			return;
		if (s_grab_pending && s_grab_dl)
		{   // the copy asked 12 pictures ago
			const GSVector4i rc(0, 0, static_cast<int>(s_grab_rt_w), static_cast<int>(s_grab_rt_h));
			s_grab_dl->Flush();
			if (s_grab_dl->Map(rc))
			{
				const u8* src = s_grab_dl->GetMapPointer();
				const u32 pitch = s_grab_dl->GetMapPitch();
				std::lock_guard<std::mutex> lk(s_grab_lock);
				s_grab_px.resize(static_cast<size_t>(s_grab_rt_w) * s_grab_rt_h * 4);
				for (u32 y = 0; y < s_grab_rt_h; ++y)
					std::memcpy(s_grab_px.data() + static_cast<size_t>(y) * s_grab_rt_w * 4, src + static_cast<size_t>(y) * pitch, s_grab_rt_w * 4);
				for (size_t i = 3; i < s_grab_px.size(); i += 4)
					s_grab_px[i] = 255;
				s_grab_w = s_grab_rt_w;
				s_grab_h = s_grab_rt_h;
				++s_grab_seq;
				s_grab_dl->Unmap();
			}
			s_grab_pending = false;
		}
		GSTexture* const cur = g_gs_device->GetCurrent();
		if (!cur || cur->GetWidth() <= 0 || cur->GetHeight() <= 0)
			return;
		// One fixed size, made once and kept until the end: never freed while the game runs (with DirectX 11 a size
		// that followed the picture's shape froze the picture at a scene change, 11/11). The black box scales every
		// picture to its own small cells anyway.
		const u32 w = 320, h = 240;
		if (!s_grab_rt)
		{
			if (s_grab_failed)
				return;
			s_grab_rt = g_gs_device->CreateRenderTarget(static_cast<int>(w), static_cast<int>(h), GSTexture::Format::Color, false, false);
			s_grab_dl = s_grab_rt ? g_gs_device->CreateDownloadTexture(w, h, GSTexture::Format::Color) : nullptr;
			if (!s_grab_rt || !s_grab_dl)
			{
				SorGrabRelease();
				s_grab_failed = true;
				std::fprintf(stderr, "[sor-gs] black box pictures: not available with this video card\n");
				return;
			}
			s_grab_rt_w = w;
			s_grab_rt_h = h;
		}
		const GSVector4i rc(0, 0, static_cast<int>(w), static_cast<int>(h));
		g_gs_device->StretchRect(cur, GSVector4(0.0f, 0.0f, 1.0f, 1.0f), s_grab_rt, GSVector4(rc), ShaderConvert::COPY, Biln);
		s_grab_dl->CopyFromTexture(rc, s_grab_rt, rc, 0);
		s_grab_pending = true;
	}
	// Timing, for the reports: the game waiting for the GS thread, and the GS thread's own pace.
	std::atomic<u64> s_wait_count{0}, s_wait_us{0};
	std::chrono::steady_clock::time_point s_gs_last_vsync{};
	double s_gs_worst_gap_ms = 0.0, s_gs_busy_ms = 0.0;
	// sor-gs timing (10/11/2026; SOR_TEMPI=1, the launcher's SPEED TESTS > TIMINGS, prints it): where each picture
	// spends its time on the GS thread. queue = waiting for the GS thread; work = the GS work of its GIF data on the
	// CPU; slot = waiting for its refresh (vsync pacing); vsync = GSvsync (last draws, post-processing, OSD, Present);
	// gpu wait = the CPU waiting for the GPU inside work and vsync (DX12); present = the Present call (DX12); gpu = the
	// GPU time PCSX2 read at this present (timestamp queries, it lags 1-3 pictures); new = pipelines (DX12) and
	// textures/targets made for the first time, with their ms. A slow GPU shows as gpu and gpu wait, a slow CPU side
	// as work or vsync without them.
	struct SorPic
	{
		u64 n = 0;
		float queue = 0, work = 0, slot = 0, late = 0, vsync = 0, gpu_wait = 0, present = 0, gpu = 0, new_ms = 0;
		u32 pipelines = 0, surfaces = 0;
	};
	constexpr u32 kPicRing = 6;
	SorPic s_pic_ring[kPicRing];
	u32 s_pic_pos = 0;
	bool s_times_on = false;
	float s_pic_slot_late = 0.0f; // how late the GS thread went on after this picture's refresh
	double s_slot_late_sum = 0.0, s_slot_late_max = 0.0;
	u32 s_slot_late_n = 0;
	double s_work_part_ms = 0.0; // the GS work of the frame's chunks before the one with its vsync
	SorGsTimes s_times_prev;
	struct SorSumMax
	{
		double sum = 0.0, max = 0.0;
		void Add(double v)
		{
			sum += v;
			max = std::max(max, v);
		}
	};
	SorSumMax s_t_queue, s_t_work, s_t_slot, s_t_vsync, s_t_gpu_wait, s_t_present, s_t_gpu;
	u32 s_t_n = 0, s_t_pipelines = 0, s_t_surfaces = 0, s_t_fence_waits = 0;
	double s_t_new_ms = 0.0;
	std::chrono::steady_clock::time_point s_t_since{};
	void SorNotePicture(s64 pushed_ns, s64 pop_ns, std::chrono::steady_clock::time_point busy0,
		std::chrono::steady_clock::time_point w0, std::chrono::steady_clock::time_point w1,
		std::chrono::steady_clock::time_point done)
	{
		using Ms = std::chrono::duration<double, std::milli>;
		const SorGsTimes& t = g_sor_gs_times;
		SorPic& p = s_pic_ring[s_pic_pos++ % kPicRing];
		p.n = s_gs_frames + 1;
		p.queue = pushed_ns ? static_cast<float>(static_cast<double>(pop_ns - pushed_ns) / 1e6) : 0.0f;
		p.work = static_cast<float>(s_work_part_ms + Ms(w0 - busy0).count());
		p.slot = static_cast<float>(Ms(w1 - w0).count());
		p.late = s_pic_slot_late;
		s_pic_slot_late = 0.0f;
		p.vsync = static_cast<float>(Ms(done - w1).count());
		p.gpu_wait = static_cast<float>(static_cast<double>(t.fence_wait_ns - s_times_prev.fence_wait_ns) / 1e6);
		p.present = static_cast<float>(static_cast<double>(t.present_ns - s_times_prev.present_ns) / 1e6);
		p.gpu = static_cast<float>(t.gpu_ms_total - s_times_prev.gpu_ms_total);
		p.pipelines = t.pipelines - s_times_prev.pipelines;
		p.surfaces = t.surfaces - s_times_prev.surfaces;
		p.new_ms = static_cast<float>(static_cast<double>((t.pipeline_ns - s_times_prev.pipeline_ns) +
			(t.surface_ns - s_times_prev.surface_ns)) / 1e6);
		s_t_fence_waits += t.fence_waits - s_times_prev.fence_waits;
		s_times_prev = t;
		s_work_part_ms = 0.0;
		if (s_t_since.time_since_epoch().count() == 0)
			s_t_since = done;
		s_t_queue.Add(p.queue);
		s_t_work.Add(p.work);
		s_t_slot.Add(p.slot);
		s_t_vsync.Add(p.vsync);
		s_t_gpu_wait.Add(p.gpu_wait);
		s_t_present.Add(p.present);
		s_t_gpu.Add(p.gpu);
		s_t_pipelines += p.pipelines;
		s_t_surfaces += p.surfaces;
		s_t_new_ms += p.new_ms;
		++s_t_n;
	}
	// With each long gap: the last pictures, oldest first.
	void SorPrintRing()
	{
		static bool s_legend = false;
		if (!s_legend)
		{
			s_legend = true;
			std::fprintf(stderr, "[sor-gs] before a long gap, the last pictures in ms: q = waiting for the GS thread, w = GS work on the CPU,"
				" s = waiting for its refresh, l = how late it went on after that refresh, v = GSvsync (last draws, OSD, Present), gw = the CPU waiting for the GPU (inside w and v),"
				" p = Present, gpu = GPU time (lags 1-3 pictures), new = pipelines/textures made for the first time and their ms\n");
		}
		char line[1024];
		int len = std::snprintf(line, sizeof(line), "[sor-gs]   before it:");
		for (u32 i = 0; i < kPicRing; ++i)
		{
			const SorPic& p = s_pic_ring[(s_pic_pos + i) % kPicRing];
			if (!p.n || len < 0 || len >= static_cast<int>(sizeof(line)) - 1)
				continue;
			len += std::snprintf(line + len, sizeof(line) - static_cast<size_t>(len),
				" | %llu q %.1f w %.1f s %.1f l %.1f v %.1f gw %.1f p %.1f gpu %.1f new %u/%u %.1f", static_cast<unsigned long long>(p.n),
				p.queue, p.work, p.slot, p.late, p.vsync, p.gpu_wait, p.present, p.gpu, p.pipelines, p.surfaces, p.new_ms);
		}
		std::fprintf(stderr, "%s\n", line);
	}
	void SorPrintTimes()
	{
		if (!s_t_n)
			return;
		const auto now = std::chrono::steady_clock::now();
		const double wall = std::chrono::duration<double, std::milli>(now - s_t_since).count();
		const double n = static_cast<double>(s_t_n);
		std::fprintf(stderr, "[sor-gs] times, ms a picture (average / worst) over %u pictures: queue %.1f / %.1f; GS work %.1f / %.1f;"
			" waiting for the refresh %.1f / %.1f; GSvsync %.1f / %.1f; CPU waiting for the GPU %.1f / %.1f (%u waits);"
			" Present %.1f / %.1f; GPU %.1f / %.1f, GPU busy %.0f%%; first-time pipelines %u, textures %u, %.1f ms\n",
			s_t_n, s_t_queue.sum / n, s_t_queue.max, s_t_work.sum / n, s_t_work.max, s_t_slot.sum / n, s_t_slot.max,
			s_t_vsync.sum / n, s_t_vsync.max, s_t_gpu_wait.sum / n, s_t_gpu_wait.max, s_t_fence_waits, s_t_present.sum / n,
			s_t_present.max, s_t_gpu.sum / n, s_t_gpu.max, wall > 0.0 ? s_t_gpu.sum * 100.0 / wall : 0.0, s_t_pipelines,
			s_t_surfaces, s_t_new_ms);
		s_t_queue = s_t_work = s_t_slot = s_t_vsync = s_t_gpu_wait = s_t_present = s_t_gpu = SorSumMax{};
		s_t_n = s_t_pipelines = s_t_surfaces = s_t_fence_waits = 0;
		s_t_new_ms = 0.0;
		s_t_since = now;
	}

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
	// The screen clock's own timing (10/11/2026, SOR_TEMPI=1 prints it after the pacing line): its wake-ups, the ones
	// later than 2 ms against the line through the others (median), the worst, refreshes counted after a late
	// wake-up, double wake-ups.
	std::atomic<u32> s_clk_wakes{0}, s_clk_late{0}, s_clk_missed{0}, s_clk_double{0};
	std::atomic<s64> s_clk_late_max_ns{0};
	// Variable refresh (G-SYNC / FreeSync, 10/11/2026): the screen refreshes when a picture arrives, so there is no
	// fixed refresh to wait for. Seen in the tests: WaitForVBlank woke about every 3.3 refreshes of 180 Hz, the swap
	// chain counted 60.6 refreshes a second, and the GS thread waited for a refresh that came only with the next
	// picture (waits of 16 or 30 ms, then a picture at once: the long gaps left). Seen from the clock thread: the
	// median time between refreshes over the last 64 more than 1.6 times the screen's set period (off again under
	// 1.25 times), 64 times in a row. Then each picture is shown as soon as it is ready, the game keeps its own clock.
	std::atomic<bool> s_vrr{false};

	// Variable refresh lost while playing (11/11/2026, a tester's 165 Hz G-SYNC screen: after about a minute the screen
	// went back to a fixed 165 Hz, so each of the 60 pictures a second stayed 3 or 2 refreshes, 18 or 12 ms: small
	// jerks at a steady 60; it came back right after "focus back", which hides and shows this window again). Logged
	// with what is in front, to learn the cause from the next logs; and when the game's window is the one in front,
	// this window is hidden and shown again once (at most every 15 seconds), as "focus back" does.
	void SorVrrLost()
	{
		HWND fg = GetForegroundWindow();
		wchar_t title[96] = {};
		DWORD pid = 0;
		if (fg)
			GetWindowThreadProcessId(fg, &pid);
		const bool ours = fg && pid == GetCurrentProcessId();
		if (fg && !ours) // never for the game's own windows: that sends a message to a thread that may be busy (it hung at exit)
			GetWindowTextW(fg, title, 95);
		char t8[256] = {};
		WideCharToMultiByte(CP_UTF8, 0, title, -1, t8, sizeof(t8) - 1, nullptr, nullptr);
		static std::chrono::steady_clock::time_point s_last{};
		const auto now = std::chrono::steady_clock::now();
		const bool retry = ours && s_hwnd && (s_last.time_since_epoch().count() == 0 || now - s_last > std::chrono::seconds(15));
		std::fprintf(stderr, "[sor-gs] vsync: variable refresh lost; in front: %s \"%s\"%s\n", ours ? "the game" : "another program",
			t8, retry ? "; the picture's window shown again to get it back" : "");
		if (retry)
		{
			s_last = now;
			ShowWindowAsync(s_hwnd, SW_HIDE);
			ShowWindowAsync(s_hwnd, SW_SHOWNA);
		}
	}
	std::atomic<s64> s_cfg_period_ns{0}; // the screen's set refresh period (EnumDisplaySettings), 0 = not known
	std::atomic<s64> s_clk_median_ns{0};
	u64 s_vb_ref_count = 0; // the latest refresh and when it really was (from the line); under s_vb_lock
	s64 s_vb_ref_ns = 0;
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

	// The set refresh period of the screen `mon` (its display settings), 0 if not known.
	s64 VsConfiguredPeriod(HMONITOR mon)
	{
		MONITORINFOEXW mi = {};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfoW(mon, &mi))
			return 0;
		DEVMODEW dm = {};
		dm.dmSize = sizeof(dm);
		if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) || dm.dmDisplayFrequency <= 1)
			return 0;
		return static_cast<s64>(1e9 / static_cast<double>(dm.dmDisplayFrequency));
	}
	void VsClockThread()
	{
		// 10/11/2026: above everything else of the game, so a busy PC does not hold back the count of refreshes (slot
		// waits of 27-31 ms instead of at most 16.5 in the test at 15:35); it only runs for a moment at each refresh.
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
		HMONITOR mon = MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTOPRIMARY);
		IDXGIOutput* out = VsFindOutput(mon);
		if (!out)
		{
			std::fprintf(stderr, "[sor-gs] vsync: no screen found, the pictures are not in step\n");
			return;
		}
		s_cfg_period_ns.store(VsConfiguredPeriod(mon), std::memory_order_relaxed);
		s64 gaps[64]; // the last times between refreshes, for variable refresh
		int gap_n = 0, gap_head = 0, vrr_votes = 0;
		constexpr int N = 128; // the period from the last 128 refreshes
		s64 times[N];
		u64 counts[N];
		int have = 0, head = 0, fails = 0, doubles = 0;
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
				{
					s_clk_double.fetch_add(1, std::memory_order_relaxed);
					if (++doubles >= 3) // never a busy loop at this thread's priority
					{
						Sleep(1);
						doubles = 0;
					}
					continue;
				}
				if (r > 1.5) // refreshes this thread missed (a busy PC): counted all the same
				{
					step = static_cast<u64>(std::llround(r));
					s_clk_missed.fetch_add(static_cast<u32>(step - 1), std::memory_order_relaxed);
				}
			}
			if (prev)
			{
				// variable refresh: the median time between refreshes against the screen's set period
				gaps[gap_head] = t - prev;
				gap_head = (gap_head + 1) % 64;
				if (gap_n < 64)
					++gap_n;
				const s64 cfg = s_cfg_period_ns.load(std::memory_order_relaxed);
				if (gap_n == 64 && cfg > 0)
				{
					s64 tmp[64];
					std::memcpy(tmp, gaps, sizeof(tmp));
					std::nth_element(tmp, tmp + 32, tmp + 64);
					const s64 med = tmp[32];
					s_clk_median_ns.store(med, std::memory_order_relaxed);
					const bool vrr = s_vrr.load(std::memory_order_relaxed);
					const bool other = vrr ? med * 4 < cfg * 5 : med * 5 > cfg * 8; // under 1.25 times / over 1.6 times
					vrr_votes = other ? vrr_votes + 1 : 0;
					if (vrr_votes >= 64)
					{
						vrr_votes = 0;
						s_vrr.store(!vrr, std::memory_order_relaxed);
						if (!vrr)
							std::fprintf(stderr, "[sor-gs] vsync: variable refresh (G-SYNC/FreeSync): the screen refreshes when a picture arrives, "
								"about every %.1f ms (set to %.1f Hz); each picture is shown as soon as it is ready, the game keeps its own clock\n",
								static_cast<double>(med) / 1e6, 1e9 / static_cast<double>(cfg));
						else
						{
							std::fprintf(stderr, "[sor-gs] vsync: fixed refresh again (about every %.1f ms, set to %.1f Hz): pictures in step with the screen\n",
								static_cast<double>(med) / 1e6, 1e9 / static_cast<double>(cfg));
							SorVrrLost();
						}
						std::fflush(stderr);
					}
				}
			}
			if (prev && !period)
				period = t - prev; // a rough first value, until there are enough
			prev = t;
			doubles = 0;
			const u64 c = s_vb_count.load(std::memory_order_relaxed) + step;
			times[head] = t;
			counts[head] = c;
			head = (head + 1) % N;
			if (have < N)
				++have;
			s64 ref_ns = 0; // when refresh c really was (0: not known yet)
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
					// 10/11/2026: how late this wake-up is against the line (its median: most wake-ups are on time),
					// and when the refresh just counted really was, for the slot wait's own lateness.
					const double lb = (have * sxy - sx * sy) / den;
					const double la = (sy - lb * sx) / have;
					double res[N];
					for (int k = 0; k < have; ++k)
					{
						const int i = (oldest + k) % N;
						res[k] = static_cast<double>(times[i] - times[oldest]) - (la + lb * static_cast<double>(counts[i] - counts[oldest]));
					}
					const double newest = res[have - 1];
					std::nth_element(res, res + have / 2, res + have);
					const double med = res[have / 2];
					const s64 late_ns = static_cast<s64>(newest - med);
					s_clk_wakes.fetch_add(1, std::memory_order_relaxed);
					if (late_ns > 2000000)
						s_clk_late.fetch_add(1, std::memory_order_relaxed);
					if (late_ns > s_clk_late_max_ns.load(std::memory_order_relaxed))
						s_clk_late_max_ns.store(late_ns, std::memory_order_relaxed);
					ref_ns = times[oldest] + static_cast<s64>(la + lb * static_cast<double>(c - counts[oldest]) + med);
				}
			}
			{
				std::lock_guard<std::mutex> lk(s_vb_lock);
				s_vb_count.store(c, std::memory_order_release);
				s_vb_ref_count = c;
				s_vb_ref_ns = ref_ns;
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
						s_cfg_period_ns.store(VsConfiguredPeriod(now_mon), std::memory_order_relaxed);
						gap_n = gap_head = vrr_votes = 0;
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
		if (s_vrr.load(std::memory_order_relaxed)) // variable refresh: shown as soon as it is ready
		{
			s_slot_step.store(0, std::memory_order_relaxed);
			return;
		}
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
		bool reached = false;
		u64 ref_c = 0;
		s64 ref_t = 0;
		{
			// Never longer than 3 of the game's frames (a stuck clock must not freeze the picture), and not at all
			// when the game's next picture is already here (the game is ahead of the screen).
			std::unique_lock<std::mutex> lk(s_vb_lock);
			s_vb_tick.wait_for(lk, std::chrono::microseconds(3 * game_us), [target] {
				return s_vb_count.load(std::memory_order_acquire) >= target ||
					s_vs_in.load(std::memory_order_relaxed) > s_vs_out.load(std::memory_order_relaxed) + 1;
			});
			reached = s_vb_count.load(std::memory_order_acquire) >= target;
			ref_c = s_vb_ref_count;
			ref_t = s_vb_ref_ns;
		}
		const s64 t1 = VsNowNs();
		const double waited = static_cast<double>(t1 - t0) / 1e6;
		// 10/11/2026: how late this thread went on after its refresh (the refresh's real time from the clock's line)
		if (const s64 per = s_vb_period_ns.load(std::memory_order_relaxed); reached && ref_t && per > 0)
		{
			const s64 due = ref_t + (static_cast<s64>(target) - static_cast<s64>(ref_c)) * per;
			if (due > t0)
			{
				const double late = static_cast<double>(t1 - due) / 1e6;
				s_slot_late_sum += late;
				s_slot_late_max = std::max(s_slot_late_max, late);
				++s_slot_late_n;
				s_pic_slot_late = static_cast<float>(late);
			}
		}
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
		if (s_vrr.load(std::memory_order_relaxed))
		{
			// variable refresh: the time since the picture before, in thirds of the game's frame (3 = on time)
			const u32 gus = s_game_us.load(std::memory_order_relaxed);
			if (s_last_shown_ns && gus)
				++s_pace_hist[std::min<s64>(std::llround(static_cast<double>(now - s_last_shown_ns) * 3.0 / (gus * 1000.0)), 5)];
		}
		else if (s_slot_last && c >= s_slot_last)
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
				if (s_times_on)
					SorPrintRing();
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
		else if (s_vrr.load(std::memory_order_relaxed))
			std::snprintf(mode, sizeof(mode), "variable refresh, each picture when ready");
		else if (step)
			std::snprintf(mode, sizeof(mode), "vsync in step, %d refreshes a picture", step);
		else
			std::snprintf(mode, sizeof(mode), "vsync on, the screen is not a multiple of the game");
		std::fprintf(stderr, "[sor-gs] pacing: refreshes per picture 0:%u 1:%u 2:%u 3:%u 4:%u 5+:%u; screen %.2f Hz; %s; late %u; waited %.1f ms a picture; game to screen %.1f ms (max %.1f)\n",
			s_pace_hist[0], s_pace_hist[1], s_pace_hist[2], s_pace_hist[3], s_pace_hist[4], s_pace_hist[5],
			p > 0 ? 1e9 / static_cast<double>(p) : 0.0, mode, s_pace_late, s_pace_waited_ms / 250.0, s_lat_n ? s_lat_sum_ms / s_lat_n : 0.0, s_lat_max_ms);
		if (s_times_on)
		{
			std::fprintf(stderr, "[sor-gs] screen clock: %u wake-ups, %u later than 2 ms (worst %.1f ms), %u refreshes counted after a"
				" late wake-up, %u double wake-ups; the GS thread after its refresh: %.1f ms on average, worst %.1f ms (%u waits)\n",
				s_clk_wakes.exchange(0), s_clk_late.exchange(0), static_cast<double>(s_clk_late_max_ns.exchange(0)) / 1e6,
				s_clk_missed.exchange(0), s_clk_double.exchange(0), s_slot_late_n ? s_slot_late_sum / s_slot_late_n : 0.0,
				s_slot_late_max, s_slot_late_n);
			s_slot_late_sum = s_slot_late_max = 0.0;
			s_slot_late_n = 0;
			const s64 cfg = s_cfg_period_ns.load(std::memory_order_relaxed), med = s_clk_median_ns.load(std::memory_order_relaxed);
			std::fprintf(stderr, "[sor-gs] screen: set to %.1f Hz, median time between refreshes %.1f ms, variable refresh %s\n",
				cfg > 0 ? 1e9 / static_cast<double>(cfg) : 0.0, static_cast<double>(med) / 1e6,
				s_vrr.load(std::memory_order_relaxed) ? "yes" : "no");
		}
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
		if (s_times_on)
			SorPrintTimes();
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
			const s64 pop_ns = VsNowNs(); // sor-gs timing: how long the frame waited for the GS thread
			const auto busy0 = std::chrono::steady_clock::now();
			g_sor_wireframe = s_wire_req.load(std::memory_order_relaxed) != 0; // debug view, a whole frame at a time

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
				if (g_sor_overlays_without_vm)
				{   // the OSD's FPS and frame-time graph: PCSX2's VM does this at every vsync, here there is no VM
					static bool s_metrics_started = false;
					if (!s_metrics_started)
					{
						s_metrics_started = true;
						PerformanceMetrics::Reset();
					}
					PerformanceMetrics::Update(false, true, false);
				}
				GSvsync(f->field, true);
				SorGrab(); // black box pictures, when the game asked for them
				const auto done = std::chrono::steady_clock::now();
				SorNotePicture(f->pushed_ns, pop_ns, busy0, w0, w1, done);
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
			else
				s_work_part_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - busy0).count();
			f->data.clear();
			std::lock_guard<std::mutex> lk(s_q_lock);
			if (s_spare.size() < 4)
				s_spare.push_back(std::move(f->data));
		}
		SorGrabRelease();
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
		bool new_picture = false;
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
			{
				s_vs_in.fetch_add(1, std::memory_order_relaxed);
				new_picture = true;
			}
			s_queue.push_back(std::move(f));
		}
		s_q_data.notify_one();
		if (new_picture)
		{
			// 10/11/2026: a GS thread waiting for its refresh goes on at once when the next picture is already here;
			// before, only the screen clock's next tick woke it, late when the PC was busy. The empty lock: the GS thread
			// checks and starts waiting under s_vb_lock, so this wake-up cannot fall in between.
			{
				std::lock_guard<std::mutex> vb(s_vb_lock);
			}
			s_vb_tick.notify_all();
		}
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
	if (const char* tm = std::getenv("SOR_TEMPI"); tm && tm[0] && tm[0] != '0') // the launcher's SPEED TESTS > TIMINGS
		s_times_on = true;
	if (const char* tm = std::getenv("SORGS_TIMES"); tm && tm[0] == '1')
		s_times_on = true;
	const char* osd = std::getenv("SORGS_OSD");
	const bool show = osd && osd[0] == '1';
	// PCSX2 draws these only with its VM running: g_sor_overlays_without_vm (pcsx2/ImGui/ImGuiManager.cpp) lets the DLL
	// draw them too, and the vsync loop feeds the numbers (PerformanceMetrics::Update before GSvsync). The FPS line is
	// red below 95% of the target.
	g_sor_overlays_without_vm = show;
	{
		const char* hz = std::getenv("SOR_60HZ");
		g_sor_overlay_target_fps = (hz && hz[0] == '1') ? 60.0f : 50.0f;
	}
	si.SetBoolValue("EmuCore/GS", "OsdShowFPS", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowResolution", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowGSStats", show);
	// With the statistics, the frame-time graph (every hitch is a spike) and the GPU load.
	si.SetBoolValue("EmuCore/GS", "OsdShowFrameTimes", show);
	si.SetBoolValue("EmuCore/GS", "OsdShowGPU", show);
	// Here every vsync is a new picture, so VPS would only repeat the FPS. Lines that need PCSX2's VM (speed, CPU
	// threads, indicators...) stay off.
	si.SetBoolValue("EmuCore/GS", "OsdShowVPS", false);
	for (const char* off : {"OsdShowSpeed", "OsdShowCPU", "OsdShowHardwareInfo", "OsdShowVersion", "OsdShowSettings",
			 "OsdShowInputs", "OsdShowIndicators", "OsdShowTextureReplacements"})
		si.SetBoolValue("EmuCore/GS", off, false);
	// No PCSX2 messages on the player's screen ("Saved screenshot..."): only in tests, with the statistics.
	si.SetIntValue("EmuCore/GS", "OsdMessagesPos", static_cast<int>(show ? OsdOverlayPos::TopLeft : OsdOverlayPos::None));
	// Test snapshots at the resolution the GS draws at, not the window's: they show the upscale as it is.
	si.SetIntValue("EmuCore/GS", "ScreenshotSize", static_cast<int>(GSScreenshotSize::InternalResolution));
	si.SetFloatValue("EmuCore/GS", "upscale_multiplier", upscale < 1.0f ? 1.0f : upscale);
	// PCSX2's own fixes for this game (GameIndex.yaml, SLES-52950): without them the upscaled picture has
	// thin lines and blurred, shifted post effects. halfPixelOffset 5 = align to native, nativeScaling 2 = aggressive.
	si.SetBoolValue("EmuCore/GS", "UserHacks", true);
	// 10/11/2026: PCSX2's game fixes (5 and 2) still left a soft, slightly shifted double outline at 4x and 8x, on
	// the characters and on far trees. the author tried them all (launcher SPEED TESTS > UPSCALE FIX): "Special"
	// (2) with native scaling off is sharp with no double outline. That is the default now.
	si.SetIntValue("EmuCore/GS", "UserHacks_HalfPixelOffset", 2);
	si.SetIntValue("EmuCore/GS", "UserHacks_native_scaling", 0);
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
	// Antialiasing (SORGS_FXAA=1, from the launcher's ANTIALIASING row): PCSX2's own FXAA on the finished picture
	// (GSRenderer::Merge, after the deinterlacer and before CAS), the same with DirectX 11, DirectX 12 and Vulkan.
	// The shader is read from resources\shaders\common\fxaa.fx next to the game (DX12 and Vulkan build it when
	// the device starts, FXAA or not; DX11 only at the first FXAA picture, and without the file it would show a
	// picture it never drew): if the file is missing FXAA stays off and the log says why. Without the variable,
	// nothing is set: PCSX2's default, off, as before.
	const char* fxaa_why = "";
	if (const char* fx = std::getenv("SORGS_FXAA"); fx && fx[0] == '1')
	{
		if (FileSystem::FileExists(Path::Combine(EmuFolders::Resources, "shaders/common/fxaa.fx").c_str()))
			si.SetBoolValue("EmuCore/GS", "fxaa", true);
		else
			fxaa_why = " (resources\\shaders\\common\\fxaa.fx is missing)";
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
	// Once in the game's log (sor-gs writes to the game's stderr), read back from the settings the GS opens with.
	static bool s_fxaa_logged = false;
	if (!s_fxaa_logged)
	{
		s_fxaa_logged = true;
		std::fprintf(stderr, "[sor] antialiasing: %s%s\n", EmuConfig.GS.FXAA ? "FXAA" : "off", fxaa_why);
	}

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

// Debug view: the game's triangles (the 3D world, some of the HUD) drawn as wires; points, lines and sprites (the
// screen clear, the copy to the display buffer, full-screen effects, most menus) stay as they are. on: 1 wires,
// 0 normal. Accepted only with SORGS_WIREFRAME=1 (the launcher's DEBUG page): returns -1 without it, 0 when the
// renderer cannot draw wires (only DX11 can), 1 when done. Read at the start of the next frame the GS thread draws.
SORGS_API int sorgs_wireframe(int on)
{
	const char* w = std::getenv("SORGS_WIREFRAME");
	if (!(w && w[0] == '1'))
		return -1;
	if (s_type != GSRendererType::DX11)
		return 0;
	s_wire_req.store(on ? 1 : 0, std::memory_order_relaxed);
	return 1;
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
	if (!s_vsync_on || s_vrr.load(std::memory_order_relaxed)) // variable refresh: the game's own clock
		return 0;
	const s64 period = s_vb_period_ns.load(std::memory_order_relaxed);
	const int step = VsStepFor(period, game_us);
	if (!step)
		return 0;
	const s64 own = static_cast<s64>(game_us) * 1000;
	const s64 ns = step * period + s_nudge_ns.exchange(0, std::memory_order_relaxed);
	return std::clamp<s64>(ns, own - own / 50, own + own / 50);
}

// A small copy of the picture shown (RGBA, about 320 wide), for the black box: copied to `out` (room for `room`
// bytes) when there is one newer than `seen`; returns its number (0: none yet). The first call turns the copies on.
SORGS_API unsigned long long sorgs_grab(unsigned long long seen, void* out, unsigned int room, unsigned int* width, unsigned int* height)
{
	s_grab_on.store(true, std::memory_order_relaxed);
	std::lock_guard<std::mutex> lk(s_grab_lock);
	if (s_grab_seq == 0 || s_grab_seq == seen || !out || s_grab_px.size() > room)
		return s_grab_seq == seen ? seen : 0;
	std::memcpy(out, s_grab_px.data(), s_grab_px.size());
	if (width)
		*width = s_grab_w;
	if (height)
		*height = s_grab_h;
	return s_grab_seq;
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
