// sor-gs: ReShade effects under the performance overlay (13/11/2026).
// ReShade normally applies its effects (sharpen, cartoon, ...) to the whole picture at Present, after PCSX2 has drawn
// the performance overlay, so the overlay text came out thin and broken. When ReShade64.dll is in the process (sor.exe
// loads it with RESHADE on), sor-gs registers as a ReShade add-on, keeps the effect runtime and installs
// g_sor_effects_before_overlay: the DX11/DX12 devices call it in EndPresent with the game picture already in the back
// buffer, before drawing the overlay. It renders the effects there, so the overlay is drawn on top, untouched.
// ReShade's own render_effects() then skips the usual pass at Present for that frame.
// Headers: crosire/reshade v6.8.0 include/ (BSD-3-Clause OR MIT, reshade/LICENSE.md).

#include <windows.h>
#include <cstdio>
#include "reshade/reshade.hpp"

extern void (*g_sor_effects_before_overlay)(void* back_buffer);

namespace
{
	reshade::api::effect_runtime* s_runtime = nullptr;

	void OnInitRuntime(reshade::api::effect_runtime* runtime) { s_runtime = runtime; }
	void OnDestroyRuntime(reshade::api::effect_runtime* runtime)
	{
		if (s_runtime == runtime)
			s_runtime = nullptr;
	}

	void EffectsBeforeOverlay(void* back_buffer)
	{
		reshade::api::effect_runtime* rt = s_runtime;
		if (!rt || !back_buffer)
			return;
		reshade::api::device* dev = rt->get_device();
		const reshade::api::resource res{reinterpret_cast<uint64_t>(back_buffer)};
		const reshade::api::format fmt = reshade::api::format_to_default_typed(dev->get_resource_desc(res).texture.format, 0);
		const reshade::api::format fmt_srgb = reshade::api::format_to_default_typed(fmt, 1);
		reshade::api::resource_view rtv{}, rtv_srgb{};
		if (!dev->create_resource_view(res, reshade::api::resource_usage::render_target,
				reshade::api::resource_view_desc(fmt), &rtv))
			return;
		if (!dev->create_resource_view(res, reshade::api::resource_usage::render_target,
				reshade::api::resource_view_desc(fmt_srgb), &rtv_srgb))
			rtv_srgb = rtv;
		reshade::api::command_queue* q = rt->get_command_queue();
		rt->render_effects(q->get_immediate_command_list(), rtv, rtv_srgb);
		q->flush_immediate_command_list();
		if (rtv_srgb != rtv)
			dev->destroy_resource_view(rtv_srgb);
		dev->destroy_resource_view(rtv);
	}
} // namespace

// Called once by sor-gs before the GS device is created. Returns true when the add-on is active.
bool SorReshadeInit(void* sorgs_module)
{
	if (!GetModuleHandleA("ReShade64.dll"))
		return false;
	if (!reshade::register_addon(sorgs_module))
	{
		std::fprintf(stderr, "[sor-gs] reshade: add-on not accepted, effects stay on the whole picture (overlay too)\n");
		return false;
	}
	reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitRuntime);
	reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyRuntime);
	g_sor_effects_before_overlay = EffectsBeforeOverlay;
	std::fprintf(stderr, "[sor-gs] reshade: effects drawn under the performance overlay\n");
	return true;
}
