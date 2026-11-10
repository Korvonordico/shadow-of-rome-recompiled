# sor-vu.dll: kzvu (PCSX2's microVU, pc/kzvu) behind a C interface for sor.exe. Built by _vu-build.bat (MSVC x64,
# the only toolchain kzvu supports), against the PCSX2 tree in _pcsx2. GPL-3.0+.
cmake_minimum_required(VERSION 3.24)
project(sorvu LANGUAGES C CXX)

set(KZVU_BUILD_TEST OFF CACHE BOOL "kzvu's own tests" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../kzvu ${CMAKE_BINARY_DIR}/kzvu)

add_library(sor-vu SHARED sor_vu.cpp)
target_link_libraries(sor-vu PRIVATE kzvu)
target_compile_features(sor-vu PRIVATE cxx_std_20)
target_compile_options(sor-vu PRIVATE /utf-8 /W3)
set_target_properties(sor-vu PROPERTIES PREFIX "" OUTPUT_NAME "sor-vu")
