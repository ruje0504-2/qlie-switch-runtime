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
if [ "${1:-}" = "--diagnostic" ]; then
  $cxx -std=c++17 $flags -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" src/qlie.cpp src/story.cpp src/switch_diagnostic.cpp -lpthread -lnx -o build-switch/whj2-diagnostic.elf
  "$dkp/tools/bin/nacptool" --create "WHJ2 Resource Diagnostic" "WHJ2 port contributors" "0.0.1" build-switch/whj2-diagnostic.nacp
  "$dkp/tools/bin/elf2nro" build-switch/whj2-diagnostic.elf build-switch/whj2-diagnostic.nro --nacp=build-switch/whj2-diagnostic.nacp
  exit 0
fi
if [ ! -f build-switch/font.o ] || [ src/font.c -nt build-switch/font.o ] || [ src/font.h -nt build-switch/font.o ] || [ src/rmt.h -nt build-switch/font.o ]; then
  $cc -std=c11 $flags $($pc --cflags freetype2) -c src/font.c -o build-switch/font.o
fi
if [ ! -f build-switch/video.o ] || [ src/video.c -nt build-switch/video.o ] || [ src/video.h -nt build-switch/video.o ] || [ src/rmt.h -nt build-switch/video.o ]; then
  $cc -std=c11 $flags $($pc --cflags libavformat libavcodec libswscale libswresample libavutil) -c src/video.c -o build-switch/video.o
fi
# Graphical runtime candidate; hardware acceptance remains separate.
$cxx -std=c++17 $flags $($pc --cflags sdl2 freetype2 libpng libjpeg) -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" src/qlie.cpp src/story.cpp src/audio.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp src/voice.cpp src/movie.cpp src/message_text.cpp src/scene_transition.cpp src/sentence_wait.cpp src/joycon.cpp src/upper_menu.cpp src/game_menu.cpp src/settings.cpp src/auto_play.cpp src/fps_overlay.cpp src/title_screen.cpp src/choice_screen.cpp src/mac_runtime.cpp build-switch/font.o build-switch/video.o $($pc --libs sdl2 freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -lpthread -lnx -o build-switch/whj2-runtime.elf
"$dkp/tools/bin/nacptool" --create "WHJ2 Runtime (Development)" "WHJ2 port contributors" "0.0.2" build-switch/whj2-runtime.nacp
"$dkp/tools/bin/elf2nro" build-switch/whj2-runtime.elf build-switch/whj2-runtime.nro --nacp=build-switch/whj2-runtime.nacp
