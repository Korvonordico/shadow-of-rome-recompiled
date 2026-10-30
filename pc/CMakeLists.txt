# Shadow of Rome on PS2Recomp.
# Links the code translated by ps2_recomp (SOR_GENERATED_DIR) with the PS2Recomp
# runtime (SOR_PS2RECOMP). Built from WSL, see scripts/setup.sh and pc/compila_pc.sh.
cmake_minimum_required(VERSION 3.24)
project(ShadowOfRomeRecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# PS2Recomp at commit e5636981 (submodule), with patches/ps2recomp applied
# (scripts/apply_patches.sh; the list is in patches/README.md).
set(SOR_PS2RECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/PS2Recomp" CACHE PATH "Checkout di PS2Recomp")
set(SOR_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gen/main" CACHE PATH "Uscita di ps2_recomp")
set(SOR_GENERATED_OPT "-O1" CACHE STRING "Ottimizzazione del codice tradotto")

set(PS2X_BUILD_RECOMP OFF)
set(PS2X_BUILD_ANALYZER OFF)
set(PS2X_BUILD_TEST OFF)
set(PS2X_BUILD_STUDIO OFF)
set(PS2X_BUILD_RUNTIME ON)
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)

add_subdirectory("${SOR_PS2RECOMP}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)
# Come rogue-galaxy-recomp: con clang su MinGW l'LTO del runtime rompe il TLS.
set_property(TARGET ps2_runtime PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)

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

add_executable(sor "${CMAKE_CURRENT_SOURCE_DIR}/main_sor.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sor_hle.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sor_profilo.cpp")
target_link_libraries(sor PRIVATE sor_generated ps2_runtime)
target_link_options(sor PRIVATE -static -Wl,--allow-multiple-definition -Wl,-Map=sor.map)
