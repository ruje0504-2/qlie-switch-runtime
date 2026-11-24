#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc=${CC:-clang}
cxx=${CXX:-clang++}
pc=${PKG_CONFIG:-/opt/homebrew/bin/pkg-config}
mkdir -p build
common="-O2 -Wall -Wextra -Werror -Iinclude -Isrc"
revision=$(git rev-parse --verify HEAD 2>/dev/null || printf unversioned)
if [ "$revision" != unversioned ] && ! git diff --quiet HEAD -- src include shaders build-host.sh build-switch.sh; then revision="$revision-dirty"; fi
common="$common -DWHJ2_BUILD_REV=\"$revision\""
vulkan_source=
if [ "${WHJ2_HOST_VULKAN:-0}" = 1 ]; then
  mesa=${WHJ2_MESA_SDK:-$PWD/local/mesa-sdk/opt/devkitpro/portlibs/switch}
  common="$common -ffp-contract=off -Wno-missing-field-initializers -DWHJ2_WITH_VULKAN -I$mesa/include"
  vulkan_source=src/vulkan_raster.cpp
fi
runtime_flags=
mac_object=
mac_frameworks=
if [ "$(uname -s)" = Darwin ]; then
  mac_object=build/mac_game_access.o
  runtime_flags=-DWHJ2_MAC_GAME_PICKER
  mac_frameworks="-framework AppKit"
  if [ ! -f "$mac_object" ] || [ src/mac_game_access.mm -nt "$mac_object" ] || [ src/mac_game_access.h -nt "$mac_object" ]; then
    $cxx -std=c++17 -O2 -Wall -Wextra -Werror -fobjc-arc -c src/mac_game_access.mm -o "$mac_object"
  fi
fi
build_runtime() {
$cxx -std=c++17 $common $runtime_flags $($pc --cflags sdl2 freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/audio.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp src/voice.cpp src/movie.cpp src/message_text.cpp src/scene_transition.cpp src/sentence_wait.cpp src/upper_menu.cpp src/game_menu.cpp src/settings.cpp src/auto_play.cpp src/fps_overlay.cpp src/title_screen.cpp src/choice_screen.cpp src/progress.cpp src/music_screen.cpp src/replay_catalog.cpp src/replay_screen.cpp src/cg_catalog.cpp src/cg_screen.cpp src/mac_runtime.cpp $vulkan_source build/font.o build/video.o $mac_object $mac_frameworks $($pc --libs sdl2 freetype2 libavformat libavcodec libswscale libswresample libavutil libpng libjpeg) -o build/whj2-runtime
}
if [ "${1:-}" = --runtime-only ]; then
  if [ ! -f build/font.o ] || [ src/font.c -nt build/font.o ] || [ src/font.h -nt build/font.o ]; then
    $cc -std=c11 $common $($pc --cflags freetype2) -c src/font.c -o build/font.o
  fi
  if [ ! -f build/video.o ] || [ src/video.c -nt build/video.o ] || [ src/video.h -nt build/video.o ]; then
    $cc -std=c11 $common $($pc --cflags libavformat libavcodec libswscale libswresample libavutil) -c src/video.c -o build/video.o
  fi
  build_runtime
  exit 0
fi
$cxx -std=c++17 $common src/qlie.cpp tools/probe.cpp -o build/qlie-probe
$cxx -std=c++17 $common src/qlie.cpp src/story.cpp tools/story_probe.cpp -o build/story-probe
$cxx -std=c++17 $common src/qlie.cpp src/story.cpp src/progress.cpp src/replay_catalog.cpp tools/progress_probe.cpp -o build/progress-probe
$cc -std=c11 $common $($pc --cflags freetype2) -c src/font.c -o build/font.o
$cc -std=c11 $common $($pc --cflags libavformat libavcodec libswscale libswresample libavutil) -c src/video.c -o build/video.o
$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/model.cpp tools/media_probe.cpp build/video.o $($pc --libs libavformat libavcodec libswscale libswresample libavutil libpng libjpeg) -o build/media-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/animation.cpp src/particles.cpp tools/animation_probe.cpp $($pc --libs libpng libjpeg) -o build/animation-probe

build_runtime

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp tools/presentation_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/presentation-probe

$cxx -std=c++17 $common src/qlie.cpp src/story.cpp src/voice.cpp tools/resource_probe.cpp -o build/resource-probe

$cxx -std=c++17 $common src/qlie.cpp src/animation.cpp src/particles.cpp tools/animation_stack_probe.cpp -o build/animation-stack-probe

$cxx -std=c++17 $common src/render_queue.cpp tools/render_queue_probe.cpp -o build/render-queue-probe
$cxx -std=c++17 $common src/render_queue.cpp tools/triangle_probe.cpp -o build/triangle-probe

$cxx -std=c++17 $common src/qlie.cpp src/image.cpp src/animation.cpp src/particles.cpp tools/particle_probe.cpp $($pc --cflags --libs libpng libjpeg) -o build/particle-probe

$cxx -std=c++17 $common $($pc --cflags sdl2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp src/audio.cpp tools/embedded_audio_probe.cpp build/video.o $($pc --libs sdl2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/embedded-audio-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/scene_transition.cpp src/sentence_wait.cpp src/gradient_mask.cpp src/render_queue.cpp tools/transition_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/transition-probe

$cxx -std=c++17 $common src/qlie.cpp src/story.cpp src/scene_transition.cpp src/sentence_wait.cpp tools/wait_probe.cpp -o build/wait-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/message_text.cpp src/gradient_mask.cpp src/render_queue.cpp tools/message_probe.cpp build/font.o build/video.o $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/message-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/render_queue.cpp src/upper_menu.cpp tools/upper_menu_probe.cpp $($pc --libs libpng libjpeg) -o build/upper-menu-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/render_queue.cpp src/settings.cpp src/game_menu.cpp tools/settings_probe.cpp $($pc --libs libpng libjpeg) -o build/settings-probe

$cxx -std=c++17 $common src/auto_play.cpp tools/auto_probe.cpp -o build/auto-probe

$cxx -std=c++17 $common $($pc --cflags sdl2) tools/frame_pacer_probe.cpp $($pc --libs sdl2) -o build/frame-pacer-probe

$cxx -std=c++17 $common src/fps_overlay.cpp tools/fps_overlay_probe.cpp -o build/fps-overlay-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/message_text.cpp src/gradient_mask.cpp src/render_queue.cpp src/title_screen.cpp src/choice_screen.cpp tools/ui_probe.cpp build/font.o build/video.o $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/ui-probe

$cxx -std=c++17 $common $($pc --cflags sdl2 libpng libjpeg) src/qlie.cpp src/image.cpp src/audio.cpp tools/audio_transport_probe.cpp build/video.o $($pc --libs sdl2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/audio-transport-probe

$cxx -std=c++17 $common src/qlie.cpp src/story.cpp src/progress.cpp src/replay_catalog.cpp tools/replay_probe.cpp -o build/replay-probe
$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/animation.cpp src/particles.cpp tools/title_phase_probe.cpp $($pc --libs libpng libjpeg) -o build/title-phase-probe
$cxx -std=c++17 $common src/render_queue.cpp tools/texture_filter_probe.cpp -o build/texture-filter-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/message_text.cpp src/gradient_mask.cpp src/render_queue.cpp src/progress.cpp src/replay_catalog.cpp src/replay_screen.cpp tools/replay_ui_probe.cpp build/font.o build/video.o $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/replay-ui-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp src/progress.cpp src/cg_catalog.cpp tools/cg_catalog_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-catalog-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/message_text.cpp src/gradient_mask.cpp src/render_queue.cpp src/progress.cpp src/cg_catalog.cpp src/cg_screen.cpp tools/cg_ui_probe.cpp build/font.o build/video.o $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-ui-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/cg_catalog.cpp src/image.cpp tools/cg_audio_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-audio-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp tools/cg_loop_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/cg-loop-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp tools/container_audio_inventory.cpp $($pc --libs libpng libjpeg) -o build/container-audio-inventory
