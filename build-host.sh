#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc=${CC:-clang}
cxx=${CXX:-clang++}
pc=${PKG_CONFIG:-/opt/homebrew/bin/pkg-config}
mkdir -p build
common="-O2 -Wall -Wextra -Werror -Iinclude -Isrc"
$cxx -std=c++17 $common src/qlie.cpp tools/probe.cpp -o build/qlie-probe
$cxx -std=c++17 $common src/qlie.cpp src/story.cpp tools/story_probe.cpp -o build/story-probe
$cc -std=c11 $common $($pc --cflags freetype2) -c src/font.c -o build/font.o
$cc -std=c11 $common $($pc --cflags libavformat libavcodec libswscale libswresample libavutil) -c src/video.c -o build/video.o
$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/model.cpp tools/media_probe.cpp build/video.o $($pc --libs libavformat libavcodec libswscale libswresample libavutil libpng libjpeg) -o build/media-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/animation.cpp src/particles.cpp tools/animation_probe.cpp $($pc --libs libpng libjpeg) -o build/animation-probe

$cxx -std=c++17 $common $($pc --cflags sdl2 freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/audio.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/gradient_mask.cpp src/render_queue.cpp src/voice.cpp src/movie.cpp src/message_text.cpp src/scene_transition.cpp src/sentence_wait.cpp src/upper_menu.cpp src/mac_runtime.cpp build/font.o build/video.o $($pc --libs sdl2 freetype2 libavformat libavcodec libswscale libswresample libavutil libpng libjpeg) -o build/whj2-runtime

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/gradient_mask.cpp src/render_queue.cpp tools/presentation_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/presentation-probe

$cxx -std=c++17 $common src/qlie.cpp src/story.cpp src/voice.cpp tools/resource_probe.cpp -o build/resource-probe

$cxx -std=c++17 $common src/qlie.cpp src/animation.cpp src/particles.cpp tools/animation_stack_probe.cpp -o build/animation-stack-probe

$cxx -std=c++17 $common src/render_queue.cpp tools/render_queue_probe.cpp -o build/render-queue-probe

$cxx -std=c++17 $common src/qlie.cpp src/image.cpp src/animation.cpp src/particles.cpp tools/particle_probe.cpp $($pc --cflags --libs libpng libjpeg) -o build/particle-probe

$cxx -std=c++17 $common $($pc --cflags sdl2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/gradient_mask.cpp src/render_queue.cpp src/audio.cpp tools/embedded_audio_probe.cpp build/video.o $($pc --libs sdl2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/embedded-audio-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/scene_transition.cpp src/sentence_wait.cpp src/gradient_mask.cpp src/render_queue.cpp tools/transition_probe.cpp build/video.o $($pc --libs libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/transition-probe

$cxx -std=c++17 $common src/qlie.cpp src/story.cpp src/scene_transition.cpp src/sentence_wait.cpp tools/wait_probe.cpp -o build/wait-probe

$cxx -std=c++17 $common $($pc --cflags freetype2 libpng libjpeg) src/qlie.cpp src/story.cpp src/image.cpp src/animation.cpp src/particles.cpp src/scene_images.cpp src/message_text.cpp src/gradient_mask.cpp src/render_queue.cpp tools/message_probe.cpp build/font.o build/video.o $($pc --libs freetype2 libpng libjpeg libavformat libavcodec libswscale libswresample libavutil) -o build/message-probe

$cxx -std=c++17 $common $($pc --cflags libpng libjpeg) src/qlie.cpp src/image.cpp src/render_queue.cpp src/upper_menu.cpp tools/upper_menu_probe.cpp $($pc --libs libpng libjpeg) -o build/upper-menu-probe
