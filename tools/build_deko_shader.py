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


def schedule_blizzard(data):
    """Drain Maxwell variable-latency operations before register reuse."""
    _, _, header_size, code_size, program, count = struct.unpack_from("<6I", data)
    if count != 1 or header_size + code_size != len(data):
        raise ValueError("unexpected blizzard DKSH sections")
    if struct.unpack_from("<I", data, program)[0] != 5:
        raise ValueError("expected a compute shader")
    code = bytearray(data[header_size:])
    instructions = {
        pc: struct.unpack_from("<Q", code, pc)[0]
        for pc in range(8, len(code), 8) if pc % 32
    }
    exits = [pc for pc, opcode in instructions.items()
             if opcode == 0xE30000000007000F]
    if not exits:
        raise ValueError("blizzard shader has no unconditional exit")
    code_end = max(exits) + 8

    def control(pc):
        bundle = pc & ~31
        shift = 21 * ((pc - bundle) // 8 - 1)
        word = struct.unpack_from("<Q", code, bundle)[0]
        return bundle, shift, word, (word >> shift) & 0x1FFFFF

    def write_control(pc, value):
        bundle, shift, word, _ = control(pc)
        mask = 0x1FFFFF << shift
        struct.pack_into("<Q", code, bundle, (word & ~mask) | (value << shift))

    controls = {pc: control(pc)[3] for pc in instructions if pc < code_end}
    for pc, old in controls.items():
        write_dependency = (old >> 5) & 7
        read_dependency = (old >> 8) & 7
        if write_dependency >= 6 and read_dependency >= 6:
            continue
        # LDG.E uses both address registers even when its result overwrites
        # the low register. Protect the high register from the following op.
        global_load = (instructions[pc] & 0xFFF8000000000000) == 0xEED0000000000000
        write_slot = 0 if write_dependency < 6 else 7
        read_slot = 1 if read_dependency < 6 or global_load else 7
        value = ((old & 0x1E0010) | max(2, old & 15) |
                 (write_slot << 5) | (read_slot << 8) | (0x3F << 11))
        write_control(pc, value)
        following = pc + (16 if pc % 32 == 24 else 8)
        if following >= len(code):
            raise ValueError("blizzard dependency extends beyond shader code")
        write_control(following, control(following)[3] | (0x3F << 11))
    for pc, opcode in instructions.items():
        if struct.unpack_from("<Q", code, pc)[0] != opcode:
            raise ValueError("blizzard scheduling changed an instruction")
    return data[:header_size] + bytes(code)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uam", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--whj6", action="store_true", help="enable the WHJ6 motion or NV12 shader features")
    variant = parser.add_mutually_exclusive_group()
    variant.add_argument("--motion", action="store_true", help="build the unmasked motion kernel")
    variant.add_argument("--restore", action="store_true", help="build the sparse display tile copy kernel")
    variant.add_argument("--base", action="store_true", help="build the fully covered opaque tile kernel")
    variant.add_argument("--video", action="store_true", help="build the native NV12 movie conversion kernel")
    variant.add_argument("--snow", action="store_true", help="build the WHJ6 snow compositor")
    variant.add_argument("--fog", action="store_true", help="build the WHJ6 fog compositor")
    variant.add_argument("--hardlight", action="store_true", help="build the WHJ6 grayscale hard-light compositor")
    variant.add_argument("--blizzard", action="store_true", help="build the WHJ6 layered snow compositor")
    variant.add_argument("--field", action="store_true", help="build the field presentation copy kernel")
    args = parser.parse_args()
    if args.whj6 and not (args.motion or args.video):
        parser.error("--whj6 requires --motion or --video")
    symbol = next((name for name in ("restore", "base", "video", "motion", "field", "snow", "fog", "hardlight", "blizzard") if getattr(args, name)), "raster")
    source_name = "deko_raster.comp" if symbol == "motion" else f"deko_{symbol}.comp"
    source = Path(__file__).resolve().parents[1] / "shaders" / source_name
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temp:
        stage = Path(temp) / "raster.comp"
        glsl = expand(source)
        defines = "\n#define DEKO_MOTION_KERNEL" if args.motion else ""
        if args.whj6:
            defines += "\n#define DEKO_WHJ6_MOTION" if args.motion else "\n#define DEKO_WHJ6_VIDEO"
        if defines:
            glsl = re.sub(r"(?m)^(#version [^\n]+)$", r"\1" + defines, glsl, count=1)
        stage.write_text(glsl)
        module = Path(temp) / "raster.dksh"
        subprocess.run([args.uam, "-s", "comp", "-o", str(module), str(stage)], check=True)
        data = module.read_bytes()
        if data[:4] != b"DKSH":
            raise ValueError("uam output has no DKSH header")
        _, _, control, code, offset, count = struct.unpack_from("<6I", data)
        if control + code != len(data) or count != 1 or offset + 64 > control:
            raise ValueError("unexpected DKSH sections")
        stage, _, _, _, _, scratch, x, y, z = struct.unpack_from("<9I", data, offset)
        expected_y = 8 if args.video else 16
        if (stage, x, y, z) != (5, 16, expected_y, 1):
            raise ValueError("unexpected DKSH compute workgroup")
        if args.blizzard:
            data = schedule_blizzard(data)
        # Store the control header and code as one module. ShaderMaker accepts both together.
        rows = [",".join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
        text = f"alignas(4) static constexpr unsigned char {symbol}Dksh[] = {{\n" + ",\n".join(rows) + "\n};\n"
        text += f"static constexpr unsigned {symbol}ScratchPerWarp = {scratch};\n"
        if not args.output.exists() or args.output.read_text() != text:
            header = Path(temp) / "raster.inc"
            header.write_text(text)
            header.replace(args.output)


if __name__ == "__main__":
    main()
