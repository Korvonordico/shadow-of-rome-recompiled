# Shadow of Rome con PS2Recomp: prova di fattibilita'.
# Collega il codice tradotto da ps2_recomp (SOR_GENERATED_DIR) al runtime di
# PS2Recomp (SOR_PS2RECOMP). Si costruisce da WSL, vedi pc/compila_pc.sh.
cmake_minimum_required(VERSION 3.24)
project(ShadowOfRomeRecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Copia di PS2Recomp (e5636981) dentro il progetto, con le nostre correzioni
# (elenco in pc/patch/README.md; differenze con "git diff" nella copia).
set(SOR_PS2RECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../ps2recomp" CACHE PATH "Checkout di PS2Recomp")
set(SOR_GENERATED_DIR "$ENV{HOME}/sor/gen/main" CACHE PATH "Uscita di ps2_recomp")
set(SOR_GENERATED_OPT "-O1" CACHE STRING "Ottimizzazione del codice tradotto")

set(PS2X_BUILD_RECOMP OFF)
set(PS2X_BUILD_ANALYZER OFF)
set(PS2X_BUILD_TEST OFF)
set(PS2X_BUILD_STUDIO OFF)
set(PS2X_BUILD_RUNTIME ON)
# ON: ImGui and rlImGui are fetched and the engine panel (ps2_debug_panel.cpp) is built into sor.exe; it starts only
# with SOR_DEBUG_PANEL=1 (the launcher's hidden DEBUG page). OFF: no panel, as before 9/11.
set(PS2X_ENABLE_DEBUG_UI ON CACHE BOOL "" FORCE)
set(SOR_FFMPEG_DIR "$ENV{HOME}/sor/ff/install" CACHE PATH "FFmpeg statico fatto da pc/compila_ffmpeg.sh")
set(PS2X_ENABLE_FFMPEG ON CACHE BOOL "" FORCE)
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)

add_subdirectory("${SOR_PS2RECOMP}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)
# Come rogue-galaxy-recomp: con clang su MinGW l'LTO del runtime rompe il TLS.
set_property(TARGET ps2_runtime PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
# Le stesse istruzioni del codice tradotto (SSE4.1): le usa la via veloce di VU1 e del disegno.
target_compile_options(ps2_runtime PRIVATE -march=x86-64-v2)

file(GLOB sor_sources CONFIGURE_DEPENDS "${SOR_GENERATED_DIR}/*.cpp")
list(LENGTH sor_sources sor_count)
message(STATUS "codice tradotto: ${sor_count} file")
add_library(sor_generated STATIC ${sor_sources})
target_include_directories(sor_generated BEFORE PRIVATE "${SOR_GENERATED_DIR}")
target_link_libraries(sor_generated PRIVATE ps2_runtime)
# -march=x86-64-v2: il codice tradotto usa istruzioni SSE4.1 e SSSE3 (MMI e VU0 della PS2).
target_compile_options(sor_generated PRIVATE ${SOR_GENERATED_OPT} -march=x86-64-v2 -g0 -fno-strict-aliasing -fwrapv
    -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)
set_target_properties(sor_generated PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 64)
target_precompile_headers(sor_generated PRIVATE
    <ps2_runtime_macros.h> <ps2_runtime.h>
    <ps2_recompiled_functions.h> <ps2_recompiled_stubs.h>
    <ps2_syscalls.h> <ps2_stubs.h>)
# La tabella delle funzioni (23 MB, un solo costruttore) senza ottimizzare.
set_source_files_properties("${SOR_GENERATED_DIR}/register_functions.cpp" PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "-O0")

add_executable(sor "${CMAKE_CURRENT_SOURCE_DIR}/main_sor.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sor_hle.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sor_profilo.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sor_prove.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sor_tasti.cpp"    # the keyboard and controller mapping (launcher: CONTROLS > KEYBOARD / CONTROLLER), read by ps2_pad.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/sor_mouse.cpp"    # mouse look (launcher: CONTROLS > MOUSE LOOK), the pointer, the picture after Alt+Tab
    "${CMAKE_CURRENT_SOURCE_DIR}/sor_film_hd.cpp"  # HD replacement films from the mods folder (a mod: no film is shipped)
    "${CMAKE_CURRENT_SOURCE_DIR}/sor_trucchi.cpp"  # debug cheats, only with SOR_CHEATS (the launcher's hidden DEBUG page)
    "${CMAKE_CURRENT_SOURCE_DIR}/sor_camera.cpp"   # debug free camera, only with SOR_FREECAM=1 (same page)
    "${CMAKE_CURRENT_SOURCE_DIR}/sor_vignetta.cpp")  # screen veil off, only with SOR_NO_VIGNETTE=1 (GRAPHICS page, VIGNETTE)
# The black box (Iris Insight: a picture and a short report when the game freezes, crashes or slows down) is
# built only into my local test builds: its licence is not the GPL, so it is not in this repository. Without
# it, or with -DSOR_NO_BLACKBOX=ON (the published package), sor_blackbox_off.cpp stands in and does nothing.
option(SOR_NO_BLACKBOX "Build without the black box, as the published package" OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/blackbox/blackbox.cpp" AND NOT SOR_NO_BLACKBOX)
    target_sources(sor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sor_blackbox.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/blackbox/blackbox.cpp")
    target_include_directories(sor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    target_sources(sor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sor_blackbox_off.cpp")
endif()
# ffmpeg: headers and libraries for sor_film_hd.cpp. user32/gdi32 come from the toolchain's defaults and raylib: named
# here they would come before raylib, and Windows' CloseWindow would win over raylib's (lld warns "duplicate symbol").
target_link_libraries(sor PRIVATE sor_generated ps2_runtime ffmpeg)
# Engine panel (F12, only with SOR_DEBUG_PANEL=1): PS2Recomp's "Runtime Debugger", ImGui drawn with raylib.
if(PS2X_ENABLE_DEBUG_UI AND TARGET rlImGui)
    target_sources(sor PRIVATE "${SOR_PS2RECOMP}/ps2xRuntime/src/lib/ps2_debug_panel.cpp")
    target_link_libraries(sor PRIVATE rlImGui imgui)
    target_compile_definitions(sor PRIVATE PS2X_ENABLE_DEBUG_UI=1)
endif()
target_link_options(sor PRIVATE -static -Wl,--allow-multiple-definition -Wl,-Map=sor.map)
