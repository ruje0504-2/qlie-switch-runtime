#!/usr/bin/env python3
"""Build the embedded deko3d compute shader from the shared QLIE tile rasterizer."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def expand(path):
    def include(match):
        return expand(path.parent / match.group(1))
    return re.sub(r'^#include "([^"]+)"$', include, path.read_text(), flags=re.MULTILINE)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uam", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "shaders/deko_raster.comp"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temp:
        stage = Path(temp) / "raster.comp"
        stage.write_text(expand(source))
        module = Path(temp) / "raster.dksh"
        subprocess.run([args.uam, "-s", "comp", "-o", str(module), str(stage)], check=True)
        data = module.read_bytes()
        if data[:4] != b"DKSH":
            raise ValueError("uam output has no DKSH header")
        _, _, control, code, offset, count = struct.unpack_from("<6I", data)
        if control + code != len(data) or count != 1 or offset + 64 > control:
            raise ValueError("unexpected DKSH sections")
        stage, _, _, _, _, scratch, x, y, z = struct.unpack_from("<9I", data, offset)
        if (stage, x, y, z) != (5, 16, 16, 1):
            raise ValueError("unexpected DKSH compute workgroup")
        # Store the control header and code as one module. ShaderMaker accepts both together.
        rows = [",".join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
        text = "alignas(4) static constexpr unsigned char rasterDksh[] = {\n" + ",\n".join(rows) + "\n};\n"
        text += f"static constexpr unsigned rasterScratchPerWarp = {scratch};\n"
        if not args.output.exists() or args.output.read_text() != text:
            header = Path(temp) / "raster.inc"
            header.write_text(text)
            header.replace(args.output)


if __name__ == "__main__":
    main()
