#!/usr/bin/env python3
"""Build the patched GM20B NAK object against the exact Horizon SDK inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import venv

ROOT = Path(__file__).resolve().parents[1]
MODULE = "src/nouveau/compiler/nak_nir.c"
STOCK_SHA = "fa840f8124310ad9a89625c5348e9166dddbc82bdb9b3efc4e9d64643ecbe269"
FLAGS = [
    "-std=c11", "-O1", "-DNDEBUG", "-D__SWITCH__", "-D_GNU_SOURCE",
    "-DHAVE_TIMESPEC_GET", "-DHAVE_STRUCT_TIMESPEC", "-DHAVE_PTHREAD",
    "-DHAVE_FUNC_ATTRIBUTE_PACKED", "-DHAVE_FUNC_ATTRIBUTE_NORETURN",
    "-DHAVE_FUNC_ATTRIBUTE_FORMAT", "-DHAVE_FUNC_ATTRIBUTE_VISIBILITY",
    "-DHAVE_FUNC_ATTRIBUTE_PURE", "-DHAVE_FUNC_ATTRIBUTE_CONST",
    "-DHAVE___BUILTIN_FFS", "-DHAVE___BUILTIN_FFSLL", "-DHAVE___BUILTIN_CLZ",
    "-DHAVE___BUILTIN_CLZLL", "-DHAVE___BUILTIN_CTZ", "-DHAVE___BUILTIN_POPCOUNT",
    "-DHAVE___BUILTIN_POPCOUNTLL", "-DHAVE___BUILTIN_UNREACHABLE", "-DHAVE___BUILTIN_EXPECT",
    "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIC",
    "-ftls-model=local-exec", "-ffunction-sections", "-fdata-sections",
    "-fvisibility=hidden", "-fno-strict-aliasing", "-fno-math-errno", "-fno-trapping-math",
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def blob_id(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def prepare_sources(directory, manifest, mesa, work):
    missing = {}
    for line in manifest.read_text().splitlines():
        digest, name = line.split()
        target = directory / name
        if not target.is_file() or blob_id(target.read_bytes()) != digest:
            missing[name] = digest
    if not missing:
        return

    def install(name, data):
        if blob_id(data) != missing[name]:
            raise ValueError(f"Wrong upstream Mesa blob for {name}")
        staged = work / "input.tmp"
        staged.write_bytes(data)
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        os.replace(staged, target)
        del missing[name]

    checkout = os.environ.get("WHJ2_MESA_SOURCE")
    revision = mesa["commit"]
    if checkout:
        for name in list(missing):
            data = subprocess.check_output(["git", "-C", checkout, "show", f"{revision}:{name}"])
            install(name, data)
    else:
        url = mesa["repository"] + f"/archive/{revision}.tar.gz"
        with urllib.request.urlopen(url, timeout=90) as response:
            with tarfile.open(fileobj=response, mode="r|gz") as archive:
                for member in archive:
                    name = member.name.partition("/")[2]
                    if member.isfile() and name in missing:
                        install(name, archive.extractfile(member).read())
                    if not missing:
                        break
    if missing:
        raise RuntimeError(f"Missing Mesa inputs: {sorted(missing)}")


def generator_python(cache):
    python = os.environ.get("WHJ2_MESA_PYTHON", sys.executable)
    probe = [python, "-c", "import mako, yaml"]
    if subprocess.run(probe, stderr=subprocess.DEVNULL).returncode == 0:
        return python
    if "WHJ2_MESA_PYTHON" in os.environ:
        raise RuntimeError("WHJ2_MESA_PYTHON needs Mako and PyYAML")
    env = cache / "python"
    python = env / "bin/python"
    if not python.is_file():
        venv.EnvBuilder(with_pip=True).create(env)
    if subprocess.run([python, *probe[1:]], stderr=subprocess.DEVNULL).returncode:
        subprocess.run([python, "-m", "pip", "install", "Mako>=1.2,<2", "PyYAML>=6,<7"], check=True)
    return python


def generate_headers(python, source, gen):
    nir = source / "src/compiler/nir"
    for name in ("nir_opcodes", "nir_builder_opcodes", "nir_intrinsics", "nir_intrinsics_indices"):
        cmd = [python, nir / (name + "_h.py")]
        output = gen / (name + ".h")
        if name.startswith("nir_intrinsics"):
            subprocess.run([*cmd, "--out", output], check=True)
        else:
            with output.open("wb") as stream:
                subprocess.run(cmd, stdout=stream, check=True)
    subprocess.run([python, source / "src/compiler/builtin_types_h.py", gen / "builtin_types.h"], check=True)
    table = source / "src/util/format"
    target = gen / "util/format/u_format_gen.h"
    target.parent.mkdir(parents=True)
    with target.open("wb") as stream:
        subprocess.run([python, table / "u_format_table.py", table / "u_format.yaml", "--enums"],
                       stdout=stream, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("sdk", "devkitpro", "output"):
        parser.add_argument("--" + arg, required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    stamp = output.with_suffix(".stamp")
    dkp = args.devkitpro.resolve()
    prefix = str(dkp / "devkitA64/bin/aarch64-none-elf-")
    mesa = json.loads((ROOT / "config/graphics-dependencies.json").read_text())["mesa"]
    manifest = ROOT / "config/mesa-nak-inputs.txt"
    patch = ROOT / "patches/mesa-nak-scoped-interlock.patch"
    archive = args.sdk.resolve() / "lib/libvulkan.a"
    stock = subprocess.check_output([prefix + "ar", "p", archive, "nak_nir.c.o"])
    if sha(stock) != STOCK_SHA:
        raise ValueError("Mesa SDK NAK member does not match the pinned 26.2.4 build")
    version = subprocess.check_output([prefix + "gcc", "--version"])
    key = sha(b"\0".join([Path(__file__).read_bytes(), patch.read_bytes(), manifest.read_bytes(),
                          mesa["commit"].encode(), version, STOCK_SHA.encode()]))
    if output.is_file() and stamp.is_file() and stamp.read_text().split() == [key, sha(output.read_bytes())]:
        return

    cache = ROOT / "local/mesa-nak"
    source = cache / mesa["commit"]
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=cache) as scratch:
        work = Path(scratch)
        prepare_sources(source, manifest, mesa, work)
        gen = work / "generated"
        gen.mkdir()
        generate_headers(generator_python(cache), source, gen)
        unit = work / MODULE
        unit.parent.mkdir(parents=True)
        shutil.copyfile(source / MODULE, unit)
        subprocess.run(["patch", "-s", "-p1", "-i", str(patch)], cwd=work, check=True)
        includes = [gen, source / "include", source / "src", source / "src/compiler/nir",
                    source / "src/nouveau/compiler", source / "src/gallium/include"]
        with tempfile.TemporaryDirectory(dir=output.parent) as staging:
            obj = Path(staging) / "nak_nir.c.o"
            subprocess.run([prefix + "gcc", *FLAGS, "-isystem", dkp / "libnx/include",
                            "-isystem", dkp / "portlibs/switch/include", *["-I" + str(p) for p in includes],
                            "-c", unit, "-o", obj], check=True)
            digest = sha(obj.read_bytes())
            os.replace(obj, output)
            marker = Path(staging) / "stamp"
            marker.write_text(f"{key} {digest}\n")
            os.replace(marker, stamp)
    print(f"Built Horizon NAK scoped-interlock object {digest}")


if __name__ == "__main__":
    main()
