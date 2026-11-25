#!/usr/bin/env python3
"""Convert locally supplied Ogg video to hash-addressed H.264/Matroska sidecars."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--whj6-dynamic-cg", action="store_true",
                        help="Scale the WHJ6 authored 1080p canvas to 720p and cap H.264 at 6 Mbit/s")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("video", type=Path, nargs="+", help="Ogg file or unwrapped Ogg video member")
    args = parser.parse_args()
    encoder = shutil.which("ffmpeg")
    if not encoder:
        parser.error("ffmpeg with libx264 must be on PATH")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    probe = shutil.which("ffprobe") if args.whj6_dynamic_cg else None
    if args.whj6_dynamic_cg and not probe:
        parser.error("WHJ6 conversion also needs ffprobe")
    for source in args.video:
        if not source.is_file():
            parser.error(f"input is not a file: {source}")
        with source.open("rb") as stream:
            if stream.read(4) != b"OggS":
                parser.error(f"expected an Ogg video: {source}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    def encode(source):
        output = args.output_dir / (sha256(source) + ".mkv")
        if output.exists() and not args.whj6_dynamic_cg:
            print(f"{output} (existing)", flush=True)
            return
        input_options = []
        picture_options = []
        encoding_metadata = []
        rate_options = ["-crf", "16"]
        if args.whj6_dynamic_cg:
            source_info = json.loads(subprocess.check_output([
                probe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                "-show_entries", "format=duration",
                "-of", "json", str(source)
            ]))
            stream, = source_info["streams"]
            duration = source_info["format"]["duration"]
            if output.exists():
                encoded_info = json.loads(subprocess.check_output([
                    probe, "-v", "error", "-show_entries", "format=duration:format_tags=QLIE_THEORA_THREADS",
                    "-of", "json", str(output)
                ]))["format"]
                if (encoded_info.get("tags", {}).get("QLIE_THEORA_THREADS") == "1"
                        and abs(float(encoded_info["duration"]) - float(duration)) <= 0.05):
                    print(f"{output} (existing)", flush=True)
                    return
            width, height = stream["width"], stream["height"]
            filters = []
            if width > 1280 or height > 720:
                # Keep tall footage tall for scripted camera pans.
                out_width, out_height = max(2, width // 3 * 2), max(2, height // 3 * 2)
                filters += [f"scale={out_width}:{out_height}:flags=lanczos", "setsar=1"]
            # Theora's empty end packets repeat the last picture. FFmpeg can
            # drop them during transcode, so pad the picture to the source clock.
            filters.append(f"tpad=stop_mode=clone:stop_duration={duration}")
            picture_options = ["-vf", ",".join(filters), "-t", duration]
            # Some Theora interframes lose both chroma planes with frame
            # threading. Match the serial decoder used by the runtime.
            input_options = ["-threads", "1", "-filter_threads", "1"]
            rate_options = ["-crf", "18", "-maxrate", "6M", "-bufsize", "12M", "-threads", "2"]
            encoding_metadata = ["-metadata", "QLIE_THEORA_THREADS=1"]
        with tempfile.TemporaryDirectory(prefix=".convert-", dir=args.output_dir) as tmp:
            encoded = Path(tmp) / "video.mkv"
            subprocess.run([
                encoder, "-nostdin", "-hide_banner", "-loglevel", "error",
                *input_options, "-i", str(source), "-map", "0:v:0", "-map", "0:a:0?",
                *picture_options, "-c:v", "libx264", "-preset", "fast", *rate_options,
                "-bf", "0", "-pix_fmt", "yuv420p", "-fps_mode", "passthrough",
                "-c:a", "copy", "-map_metadata", "-1", "-map_chapters", "-1",
                *encoding_metadata, str(encoded)
            ], check=True)
            encoded.replace(output)
        print(output, flush=True)

    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        list(workers.map(encode, dict.fromkeys(args.video)))


if __name__ == "__main__":
    main()
