#!/usr/bin/env python3
"""Convert locally supplied Ogg video to hash-addressed H.264/Matroska sidecars."""
import argparse
import hashlib
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
    parser.add_argument("video", type=Path, nargs="+", help="Ogg file or unwrapped Ogg video member")
    args = parser.parse_args()
    encoder = shutil.which("ffmpeg")
    if not encoder:
        parser.error("ffmpeg with libx264 must be on PATH")
    for source in args.video:
        if not source.is_file():
            parser.error(f"input is not a file: {source}")
        with source.open("rb") as stream:
            if stream.read(4) != b"OggS":
                parser.error(f"expected an Ogg video: {source}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for source in args.video:
        output = args.output_dir / (sha256(source) + ".mkv")
        if output.exists():
            print(f"{output} (existing)")
            continue
        with tempfile.TemporaryDirectory(prefix=".convert-", dir=args.output_dir) as tmp:
            encoded = Path(tmp) / "video.mkv"
            subprocess.run([
                encoder, "-nostdin", "-hide_banner", "-loglevel", "error",
                "-i", str(source), "-map", "0:v:0", "-map", "0:a:0?",
                "-c:v", "libx264", "-preset", "fast", "-crf", "16",
                "-bf", "0", "-pix_fmt", "yuv420p", "-fps_mode", "passthrough",
                "-c:a", "copy", "-map_metadata", "-1", "-map_chapters", "-1", str(encoded)
            ], check=True)
            encoded.replace(output)
        print(output)


if __name__ == "__main__":
    main()
