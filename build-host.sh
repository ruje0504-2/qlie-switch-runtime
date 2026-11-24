#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc=${CC:-clang}
cxx=${CXX:-clang++}
pc=${PKG_CONFIG:-/opt/homebrew/bin/pkg-config}
mkdir -p build
. ./engine/sources.sh
common="-O2 -Wall -Wextra -Werror -Iinclude -Isrc"
if [ "${QLIE_HOST_DIAGNOSTICS:-0}" = 1 ]; then
  common="$common -DQLIE_DIAGNOSTICS=1"
fi
revision=$(git rev-parse --verify HEAD 2>/dev/null || printf unversioned)
if [ "$revision" != unversioned ] && ! git diff --quiet HEAD -- src include engine games shaders build-host.sh build-switch.sh; then revision="$revision-dirty"; fi
common="$common -DWHJ2_BUILD_REV=\"$revision\""
vulkan_source=
if [ "${WHJ2_HOST_VULKAN:-0}" = 1 ]; then
  mesa=${WHJ2_MESA_SDK:-$PWD/local/mesa-sdk/opt/devkitpro/portlibs/switch}
  common="$common -ffp-contract=off -Wno-missing-field-initializers -DWHJ2_WITH_VULKAN -I$mesa/include"
  vulkan_source="src/gpu_raster.cpp src/vulkan_raster.cpp"
fi
codec_flags=$($pc --cflags sdl2 freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil)
build_qlie_archive build "$cxx" "$cc" "${AR:-ar}" "$common $codec_flags" "$common $codec_flags"
if [ "${1:-}" = --engine-only ]; then
  exit 0
fi
. ./games/whj2/sources.sh
common="$common -Igames -Igames/whj2"
runtime_flags=
mac_object=
mac_frameworks=
if [ "$(uname -s)" = Darwin ]; then
  mac_object=build/mac_game_access.o
  runtime_flags=-DWHJ2_MAC_GAME_PICKER
  mac_frameworks="-framework AppKit"
  if [ ! -f "$mac_object" ] || [ games/whj2/mac_game_access.mm -nt "$mac_object" ] || [ games/whj2/mac_game_access.h -nt "$mac_object" ]; then
    $cxx -std=c++17 -O2 -Wall -Wextra -Werror -fobjc-arc -c games/whj2/mac_game_access.mm -o "$mac_object"
  fi
fi
build_runtime() {
  $cxx -std=c++17 $common $runtime_flags $codec_flags $whj2_scene_sources games/whj2/main.cpp $vulkan_source build/libqlie-runtime.a $mac_object $mac_frameworks $($pc --libs sdl2 freetype2 libavformat libavcodec libswscale libswresample libavutil libpng libjpeg) -o build/qlie-switch-runtime
}
if [ "${1:-}" = --runtime-only ]; then
  build_runtime
  exit 0
fi
$cxx -std=c++17 $common  tools/probe.cpp build/libqlie-runtime.a -o build/qlie-probe
$cxx -std=c++17 $common   tools/story_probe.cpp build/libqlie-runtime.a -o build/story-probe
$cxx -std=c++17 $common   games/whj2/progress.cpp games/whj2/replay_catalog.cpp tools/progress_probe.cpp build/libqlie-runtime.a -o build/progress-probe
$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)    tools/media_probe.cpp  build/libqlie-runtime.a $($pc --libs libavformat libavcodec libswscale libswresample libavutil libpng libjpeg) -o build/media-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)     tools/animation_probe.cpp build/libqlie-runtime.a $($pc --libs libpng libjpeg) -o build/animation-probe

build_runtime

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)      games/whj2/scene_images.cpp    tools/presentation_probe.cpp  build/libqlie-runtime.a $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/presentation-probe

$cxx -std=c++17 $common   games/whj2/voice.cpp tools/resource_probe.cpp build/libqlie-runtime.a -o build/resource-probe

$cxx -std=c++17 $common    tools/animation_stack_probe.cpp build/libqlie-runtime.a -o build/animation-stack-probe

$cxx -std=c++17 $common  tools/render_queue_probe.cpp build/libqlie-runtime.a -o build/render-queue-probe
$cxx -std=c++17 $common  tools/triangle_probe.cpp build/libqlie-runtime.a -o build/triangle-probe

$cxx -std=c++17 $common     tools/particle_probe.cpp $($pc --cflags --libs libpng libjpeg) build/libqlie-runtime.a -o build/particle-probe

$cxx -std=c++17 $common $($pc --cflags sdl2 libpng libjpeg)      games/whj2/scene_images.cpp     tools/embedded_audio_probe.cpp  build/libqlie-runtime.a $($pc --libs sdl2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/embedded-audio-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)      games/whj2/scene_images.cpp  games/whj2/scene_transition.cpp    tools/transition_probe.cpp  build/libqlie-runtime.a $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/transition-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)   games/whj2/scene_images.cpp games/whj2/scene_transition.cpp tools/wait_probe.cpp build/libqlie-runtime.a $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/wait-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg)      games/whj2/scene_images.cpp  games/whj2/message_text.cpp   tools/message_probe.cpp   build/libqlie-runtime.a $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/message-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)    games/whj2/upper_menu.cpp tools/upper_menu_probe.cpp build/libqlie-runtime.a $($pc --libs libpng libjpeg) -o build/upper-menu-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)     games/whj2/settings.cpp games/whj2/game_menu.cpp tools/settings_probe.cpp build/libqlie-runtime.a $($pc --libs libpng libjpeg) -o build/settings-probe

$cxx -std=c++17 $common  tools/auto_probe.cpp build/libqlie-runtime.a -o build/auto-probe

$cxx -std=c++17 $common $($pc --cflags sdl2) tools/frame_pacer_probe.cpp $($pc --libs sdl2) -o build/frame-pacer-probe

$cxx -std=c++17 $common  tools/fps_overlay_probe.cpp build/libqlie-runtime.a -o build/fps-overlay-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg)      games/whj2/scene_images.cpp  games/whj2/message_text.cpp   games/whj2/title_screen.cpp games/whj2/choice_screen.cpp tools/ui_probe.cpp   build/libqlie-runtime.a $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/ui-probe

$cxx -std=c++17 $common $($pc --cflags sdl2 libpng libjpeg)    tools/audio_transport_probe.cpp  build/libqlie-runtime.a $($pc --libs sdl2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/audio-transport-probe

$cxx -std=c++17 $common   games/whj2/progress.cpp games/whj2/replay_catalog.cpp tools/replay_probe.cpp build/libqlie-runtime.a -o build/replay-probe
$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)     tools/title_phase_probe.cpp build/libqlie-runtime.a $($pc --libs libpng libjpeg) -o build/title-phase-probe
$cxx -std=c++17 $common  tools/texture_filter_probe.cpp build/libqlie-runtime.a -o build/texture-filter-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg)      games/whj2/scene_images.cpp  games/whj2/message_text.cpp   games/whj2/progress.cpp games/whj2/replay_catalog.cpp games/whj2/replay_screen.cpp tools/replay_ui_probe.cpp   build/libqlie-runtime.a $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/replay-ui-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)      games/whj2/scene_images.cpp    games/whj2/progress.cpp games/whj2/cg_catalog.cpp tools/cg_catalog_probe.cpp  build/libqlie-runtime.a $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-catalog-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg)      games/whj2/scene_images.cpp  games/whj2/message_text.cpp   games/whj2/progress.cpp games/whj2/cg_catalog.cpp games/whj2/cg_screen.cpp tools/cg_ui_probe.cpp   build/libqlie-runtime.a $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-ui-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)   games/whj2/cg_catalog.cpp  tools/cg_audio_probe.cpp  build/libqlie-runtime.a $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-audio-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)      games/whj2/scene_images.cpp    tools/cg_loop_probe.cpp  build/libqlie-runtime.a $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-loop-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg)   tools/container_audio_inventory.cpp build/libqlie-runtime.a $($pc --libs libpng libjpeg) -o build/container-audio-inventory
