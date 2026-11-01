#!/bin/bash
# Builds the small part of FFmpeg the game's movies need (only the MPEG-2 video decoder and the image
# converter), as static libraries for Windows. It runs once: if the libraries are there, it does nothing.
# Usage: compila_ffmpeg.sh FOLDER   (the libraries end up in FOLDER/install). LLVM_MINGW is the compiler's folder.
set -euo pipefail
D="${1:?Usage: compila_ffmpeg.sh FOLDER}"
TAG=n7.1
M="${LLVM_MINGW:-$HOME/llvm-mingw}"
[ -x "$M/bin/x86_64-w64-mingw32-clang" ] || { echo "llvm-mingw not found: set LLVM_MINGW"; exit 1; }
[ -f "$D/install/lib/libavcodec.a" ] && exit 0
mkdir -p "$D"
[ -f "$D/src/configure" ] || git clone -q --depth 1 --branch "$TAG" -c advice.detachedHead=false https://github.com/FFmpeg/FFmpeg.git "$D/src"
mkdir -p "$D/build"
cd "$D/build"
PATH="$M/bin:$PATH" ../src/configure --prefix="$D/install" --arch=x86_64 --target-os=mingw32 --enable-cross-compile \
  --cross-prefix=x86_64-w64-mingw32- --cc=x86_64-w64-mingw32-clang --cxx=x86_64-w64-mingw32-clang++ \
  --ar=llvm-ar --ranlib=llvm-ranlib --nm=llvm-nm --strip=llvm-strip \
  --enable-static --disable-shared --disable-programs --disable-doc --disable-debug --disable-network \
  --disable-everything --enable-decoder=mpeg2video --enable-parser=mpegvideo --enable-swscale \
  --disable-avdevice --disable-avfilter --disable-avformat --disable-swresample --disable-x86asm --disable-autodetect > configure.log 2>&1 \
  || { tail -20 configure.log; echo "FFMPEG CONFIGURE FAILED"; exit 1; }
PATH="$M/bin:$PATH" make -j"${JOBS:-6}" install > make.log 2>&1 || { tail -20 make.log; echo "FFMPEG BUILD FAILED"; exit 1; }
ls "$D/install/lib"
