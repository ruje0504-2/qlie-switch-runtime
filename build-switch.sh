#!/bin/sh
set -eu
cd "$(dirname "$0")"
. ./games/profile.sh
dkp=${DEVKITPRO:-/opt/devkitpro}
cxx="$dkp/devkitA64/bin/aarch64-none-elf-g++"
cc="$dkp/devkitA64/bin/aarch64-none-elf-gcc"
export PKG_CONFIG_LIBDIR="$dkp/portlibs/switch/lib/pkgconfig"
pc="$dkp/tools/bin/pkg-config"
mkdir -p build-switch
. ./engine/sources.sh
flags="-O2 -Wall -Wextra -Werror -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -Iinclude -Isrc -isystem $dkp/libnx/include"
if [ "${QLIE_SWITCH_DIAGNOSTICS:-0}" = 1 ]; then
  flags="$flags -DQLIE_DIAGNOSTICS=1"
  qlie_binary="$qlie_binary-probe"
  qlie_nro_title="$qlie_nro_title (采样版)"
fi
revision=$(git rev-parse --verify HEAD 2>/dev/null || printf unversioned)
if [ "$revision" != unversioned ] && ! git diff --quiet HEAD -- src include engine games shaders build-host.sh build-switch.sh; then revision="$revision-dirty"; fi
flags="$flags -DWHJ2_BUILD_REV=\"$revision\""
codec_flags=$($pc --cflags sdl2 freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil)
build_qlie_archive build-switch "$cxx" "$cc" "$dkp/devkitA64/bin/aarch64-none-elf-ar" "$flags $codec_flags" "$flags $codec_flags -ffp-contract=off"
if [ "${1:-}" = --engine-only ]; then
  exit 0
fi
. ./games/runtime/sources.sh
flags="$flags -Igames -Igames/runtime"
if [ "${1:-}" = --diagnostic ]; then
  $cxx -std=c++17 $flags -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" games/runtime/diagnostic.cpp build-switch/libqlie-runtime.a -lpthread -lnx -o build-switch/qlie-runtime-diagnostic.elf
  "$dkp/tools/bin/nacptool" --create "QLIE Runtime Diagnostic" "QLIE Switch Runtime contributors" "0.0.1" build-switch/qlie-runtime-diagnostic.nacp
  "$dkp/tools/bin/elf2nro" build-switch/qlie-runtime-diagnostic.elf build-switch/qlie-runtime-diagnostic.nro --nacp=build-switch/qlie-runtime-diagnostic.nacp
  exit 0
fi
# Deko3d can be built without the optional Mesa Vulkan fallback.
vulkan_objects=
vulkan_flags=
case "${QLIE_SWITCH_VULKAN:-$qlie_switch_vulkan_default}" in
1)
  mesa=${WHJ2_MESA_SDK:-$PWD/local/mesa-sdk/opt/devkitpro/portlibs/switch}
  rust=${WHJ2_RUST_LIBDIR:-$PWD/local/rust/rust-std-nightly-aarch64-unknown-linux-gnu/rust-std-aarch64-unknown-linux-gnu/lib/rustlib/aarch64-unknown-linux-gnu/lib}
  if [ ! -f "$mesa/lib/libvulkan.a" ] || [ ! -d "$rust" ]; then
    echo "Missing Mesa 26.2.4/Rust SDK. Run python3 tools/fetch_graphics_sdk.py" >&2
    exit 1
  fi
  python3 tools/build_mesa_nak.py --sdk "$mesa" --devkitpro "$dkp" --output build-switch/mesa-scoped-nak.o
  $cc -std=c11 $flags -c src/switch_nak_cache.c -o build-switch/switch_nak_cache.o
  $cxx -std=c++17 $flags $qlie_profile_flags -ffp-contract=off -Wno-missing-field-initializers -Wno-misleading-indentation -I"$mesa/include" -c src/vulkan_raster.cpp -o build-switch/vulkan_raster.o
  vulkan_objects="build-switch/vulkan_raster.o build-switch/switch_nak_cache.o build-switch/mesa-scoped-nak.o"
  vulkan_flags=-DWHJ2_WITH_VULKAN
  ;;
0) ;;
*) echo "QLIE_SWITCH_VULKAN must be 0 or 1" >&2; exit 2 ;;
esac
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --output build-switch/deko_raster.inc
deko_game_profile=
if [ "$qlie_game" = whj6 ]; then
  deko_game_profile=--whj6
  python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --snow --output build-switch/deko_snow.inc
  python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --hardlight --output build-switch/deko_hardlight.inc
fi
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --motion $deko_game_profile --output build-switch/deko_motion.inc
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --restore --output build-switch/deko_restore.inc
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --field --output build-switch/deko_field.inc
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --base --output build-switch/deko_base.inc
python3 tools/build_deko_shader.py --uam "$dkp/tools/bin/uam" --video $deko_game_profile --output build-switch/deko_video.inc
$cxx -std=c++17 $flags $qlie_profile_flags -ffp-contract=off -Wno-misleading-indentation -c src/gpu_raster.cpp -o build-switch/gpu_raster.o
$cxx -std=c++17 $flags $qlie_profile_flags $codec_flags -ffp-contract=off -Ibuild-switch -c src/deko3d_raster.cpp -o build-switch/deko3d_raster.o
$cc -std=c11 $flags -c src/switch_stdio.c -o build-switch/switch_stdio.o
flags="$flags -ffp-contract=off -DWHJ2_WITH_DEKO3D $vulkan_flags"

link_runtime(){
 $cxx -std=c++17 $flags $qlie_profile_flags $($pc --cflags sdl2 freetype2 libpng libjpeg) -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" $qlie_scene_sources games/runtime/joycon.cpp games/runtime/main.cpp build-switch/gpu_raster.o build-switch/deko3d_raster.o build-switch/switch_stdio.o $vulkan_objects build-switch/libqlie-runtime.a -Wl,-u,qlie_build_revision -Wl,--start-group "$@" $($pc --libs sdl2 freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -lexpat -lzstd -ldeko3d -lpthread -lnx -Wl,--end-group -o "build-switch/$qlie_binary.elf"
}
if [ -n "$vulkan_objects" ]; then
  link_runtime -Wl,--wrap=nak_debug_flags -L"$mesa/lib" "$mesa/lib/libvulkan.a" "$rust"/*.rlib
else
  link_runtime
fi
# Installed packages consume the ELF directly.
if [ "${1:-}" = "--elf-only" ]; then
  exit 0
fi
set --
if [ -n "$qlie_nro_title_id" ]; then set -- "--titleid=$qlie_nro_title_id"; fi
"$dkp/tools/bin/nacptool" --create "$qlie_nro_title" "$qlie_nro_publisher" "$qlie_nro_version" "build-switch/$qlie_binary.nacp" "$@"
set -- "--nacp=build-switch/$qlie_binary.nacp"
if [ -n "${QLIE_NRO_ICON:-}" ]; then set -- "$@" "--icon=$QLIE_NRO_ICON"; fi
"$dkp/tools/bin/elf2nro" "build-switch/$qlie_binary.elf" "build-switch/$qlie_binary.nro" "$@"
