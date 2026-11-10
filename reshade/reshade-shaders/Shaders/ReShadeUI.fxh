// ReShadeUI.fxh for the Shadow of Rome port (written for it, MIT like SweetFX).
// The SweetFX effects include this file for the look of their settings in ReShade's menu: each macro only names the
// kind of control. ReShade 4.0 and later understand all of them.
#pragma once

#if !defined(__RESHADE__) || __RESHADE__ < 30000
	#error "ReShade 3.0+ is required to use this header file"
#endif

#define RESHADE_VERSION(major,minor,build) (10000 * (major) + 100 * (minor) + (build))
#define SUPPORTED_VERSION(major,minor,build) (__RESHADE__ >= RESHADE_VERSION(major,minor,build))

#define __UNIFORM_INPUT_ANY    ui_type = "input";
#define __UNIFORM_INPUT_BOOL1  __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_BOOL2  __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_BOOL3  __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_BOOL4  __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_INT1   __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_INT2   __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_INT3   __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_INT4   __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_FLOAT1 __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_FLOAT2 __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_FLOAT3 __UNIFORM_INPUT_ANY
#define __UNIFORM_INPUT_FLOAT4 __UNIFORM_INPUT_ANY

#define __UNIFORM_COLOR_ANY    ui_type = "color";
#define __UNIFORM_COLOR_FLOAT1 __UNIFORM_COLOR_ANY
#define __UNIFORM_COLOR_FLOAT3 __UNIFORM_COLOR_ANY
#define __UNIFORM_COLOR_FLOAT4 __UNIFORM_COLOR_ANY

#define __UNIFORM_SLIDER_ANY    ui_type = "slider";
#define __UNIFORM_SLIDER_INT1   __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_INT2   __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_INT3   __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_INT4   __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_FLOAT1 __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_FLOAT2 __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_FLOAT3 __UNIFORM_SLIDER_ANY
#define __UNIFORM_SLIDER_FLOAT4 __UNIFORM_SLIDER_ANY

#define __UNIFORM_DRAG_ANY    ui_type = "drag";
#define __UNIFORM_DRAG_INT1   __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_INT2   __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_INT3   __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_INT4   __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_FLOAT1 __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_FLOAT2 __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_FLOAT3 __UNIFORM_DRAG_ANY
#define __UNIFORM_DRAG_FLOAT4 __UNIFORM_DRAG_ANY

#define __UNIFORM_COMBO_ANY    ui_type = "combo";
#define __UNIFORM_COMBO_BOOL1  __UNIFORM_COMBO_ANY
#define __UNIFORM_COMBO_INT1   __UNIFORM_COMBO_ANY
#define __UNIFORM_LIST_ANY     ui_type = "list";
#define __UNIFORM_LIST_INT1    __UNIFORM_LIST_ANY
#define __UNIFORM_RADIO_ANY    ui_type = "radio";
#define __UNIFORM_RADIO_INT1   __UNIFORM_RADIO_ANY
