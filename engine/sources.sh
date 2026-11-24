# SPDX-License-Identifier: GPL-2.0-or-later
qlie_engine_sources="src/qlie.cpp src/story.cpp src/audio.cpp src/image.cpp src/animation.cpp src/particles.cpp src/model.cpp src/gradient_mask.cpp src/render_queue.cpp src/movie.cpp src/sentence_wait.cpp src/auto_play.cpp src/fps_overlay.cpp"

build_qlie_archive() (
    output=$1
    cxx=$2
    cc=$3
    archive_tool=$4
    cflags=$5
    cxxflags=$6
    objects=$(mktemp -d "${output}/engine-objects.XXXXXX")
    trap 'rm -rf "$objects"' EXIT
    for source in $qlie_engine_sources; do
        object="$objects/${source##*/}.o"
        "$cxx" -std=c++17 $cxxflags -c "$source" -o "$object"
    done
    "$cc" -std=c11 $cflags -c src/font.c -o "$objects/font.o"
    "$cc" -std=c11 $cflags -c src/video.c -o "$objects/video.o"
    "$archive_tool" rcs "$objects/libqlie-runtime.a" "$objects"/*.o
    mv "$objects/libqlie-runtime.a" "$output/libqlie-runtime.a"
)
