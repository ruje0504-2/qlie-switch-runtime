#!/bin/sh
set -eu
cd "$(dirname "$0")"
dkp=${DEVKITPRO:-/opt/devkitpro}
cxx="$dkp/devkitA64/bin/aarch64-none-elf-g++"
cc="$dkp/devkitA64/bin/aarch64-none-elf-gcc"
export PKG_CONFIG_LIBDIR="$dkp/portlibs/switch/lib/pkgconfig"
pc="$dkp/tools/bin/pkg-config"
mkdir -p build-switch
flags="-O2 -Wall -Wextra -Werror -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -Iinclude -Isrc -isystem $dkp/libnx/include"
$cxx -std=c++17 $flags -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" src/qlie.cpp src/story.cpp src/switch_diagnostic.cpp -lpthread -lnx -o build-switch/whj2-diagnostic.elf
$cc -std=c11 $flags $($pc --cflags freetype2) -c src/font.c -o build-switch/font.o
$cc -std=c11 $flags $($pc --cflags libavformat libavcodec libswscale libswresample libavutil) -c src/video.c -o build-switch/video.o
$cxx -std=c++17 $flags $($pc --cflags libpng libjpeg) -c src/image.cpp -o build-switch/image.o
"$dkp/tools/bin/nacptool" --create "WHJ2 Resource Diagnostic" "WHJ2 port contributors" "0.0.1" build-switch/whj2-diagnostic.nacp
"$dkp/tools/bin/elf2nro" build-switch/whj2-diagnostic.elf build-switch/whj2-diagnostic.nro --nacp=build-switch/whj2-diagnostic.nacp

$cxx -std=c++17 $flags -c src/model.cpp -o build-switch/model.o

$cxx -std=c++17 $flags -c src/animation.cpp -o build-switch/animation.o
