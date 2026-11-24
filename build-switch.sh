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
revision=$(git rev-parse --verify HEAD 2>/dev/null || printf unversioned)
if [ "$revision" != unversioned ] && ! git diff --quiet HEAD -- src include shaders build-host.sh build-switch.sh; then revision="$revision-dirty"; fi
flags="$flags -DWHJ2_BUILD_REV=\"$revision\""
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
mesa=${WHJ2_MESA_SDK:-$PWD/local/mesa-sdk/opt/devkitpro/portlibs/switch}
rust=${WHJ2_RUST_LIBDIR:-$PWD/local/rust/rust-std-nightly-aarch64-unknown-linux-gnu/rust-std-aarch64-unknown-linux-gnu/lib/rustlib/aarch64-unknown-linux-gnu/lib}
if [ ! -f "$mesa/lib/libvulkan.a" ] || [ ! -d "$rust" ]; then
  echo "Missing Mesa 26.2.4/Rust SDK. Run python3 tools/fetch_graphics_sdk.py" >&2
  exit 1
fi
python3 tools/build_mesa_nak.py --sdk "$mesa" --devkitpro "$dkp" --output build-switch/mesa-scoped-nak.o
$cc -std=c11 $flags -c src/switch_nak_cache.c -o build-switch/switch_nak_cache.o
$cxx -std=c++17 $flags -ffp-contract=off -Wno-missing-field-initializers -Wno-misleading-indentation -I"$mesa/include" -c src/vulkan_raster.cpp -o build-switch/vulkan_raster.o
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --output build-switch/deko_raster.inc
$cxx -std=c++17 $flags -ffp-contract=off -Wno-misleading-indentation -c src/gpu_raster.cpp -o build-switch/gpu_raster.o
$cxx -std=c++17 $flags -ffp-contract=off -Ibuild-switch -c src/deko3d_raster.cpp -o build-switch/deko3d_raster.o
$cc -std=c11 $flags -c src/switch_stdio.c -o build-switch/switch_stdio.o
flags="$flags -ffp-contract=off -DWHJ2_WITH_VULKAN -DWHJ2_WITH_DEKO3D"

$cxx -std=c++17 $flags $($pc --cflags sdl2 freetype2 libpng libjpeg) -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" src/qlie.cpp src/story.cpp src/audio.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp src/voice.cpp src/movie.cpp src/message_text.cpp src/scene_transition.cpp src/sentence_wait.cpp src/joycon.cpp src/upper_menu.cpp src/game_menu.cpp src/settings.cpp src/auto_play.cpp src/fps_overlay.cpp src/title_screen.cpp src/choice_screen.cpp src/progress.cpp src/music_screen.cpp src/replay_catalog.cpp src/replay_screen.cpp src/cg_catalog.cpp src/cg_screen.cpp src/mac_runtime.cpp build-switch/font.o build-switch/video.o build-switch/gpu_raster.o build-switch/deko3d_raster.o build-switch/vulkan_raster.o build-switch/switch_stdio.o build-switch/switch_nak_cache.o build-switch/mesa-scoped-nak.o -Wl,--wrap=nak_debug_flags -L"$mesa/lib" -Wl,--start-group "$mesa/lib/libvulkan.a" "$rust"/*.rlib $($pc --libs sdl2 freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -lexpat -lzstd -ldeko3d -lpthread -lnx -Wl,--end-group -o build-switch/whj2-runtime.elf
# Installed packages consume the ELF directly.
if [ "${1:-}" = "--elf-only" ]; then
  exit 0
fi
"$dkp/tools/bin/nacptool" --create "WHJ2 Runtime (Development)" "WHJ2 port contributors" "0.0.2" build-switch/whj2-runtime.nacp
"$dkp/tools/bin/elf2nro" build-switch/whj2-runtime.elf build-switch/whj2-runtime.nro --nacp=build-switch/whj2-runtime.nacp
