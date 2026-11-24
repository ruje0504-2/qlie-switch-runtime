#!/usr/bin/env python3
"""Build an unencrypted FilePackVer3.0 resource archive from a manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import tempfile

from qlie_pack import hash_v3


def checked_entries(root: Path, manifest: Path):
    seen = set()
    for item in sorted(json.loads(manifest.read_text())["files"], key=lambda it: it["path"]):
        name = item["path"]
        path = PurePosixPath(name)
        key = "".join(c.lower() if "A" <= c <= "Z" else c for c in name)
        if (not path.parts or path.is_absolute() or str(path) != name or ".." in path.parts
                or "\\" in name or "\0" in name or "pack_keyfile" in key or key in seen):
            raise ValueError(f"Invalid or duplicate resource path: {name}")
        seen.add(key)
        source = (root / name).resolve(strict=True)
        if not source.is_relative_to(root):
            raise ValueError(f"Resource escaped input directory: {name}")
        name_bytes = name.replace("/", "\\").encode("cp932")
        if not 0 < len(name_bytes) <= 256:
            raise ValueError(f"Invalid QLIE resource name length: {name}")
        yield item, source, name_bytes


def build(root: Path, manifest: Path, output: Path):
    entries = list(checked_entries(root.resolve(), manifest))
    if not 0 < len(entries) <= 100000:
        raise ValueError("Invalid resource count")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as target:
        temporary = Path(target.name)
        try:
            toc = bytearray()
            for item, path, name in entries:
                data = path.read_bytes()
                size = len(data)
                if size != item["bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
                    raise ValueError(f"Manifest does not match: {item['path']}")
                # A zero archive key block makes the name XOR depend only on length.
                mask = len(name) + 0x3E
                encoded = bytes(byte ^ ((((i + 1) ^ mask) + i + 1) & 0xFF)
                                for i, byte in enumerate(name))
                toc += struct.pack("<H", len(encoded)) + encoded
                toc += struct.pack("<Q5I", target.tell(), size, size, 0, 0, hash_v3(data))
                target.write(data)
            index_offset = target.tell()
            target.write(toc)
            target.write(bytes(0x400))
            target.write(struct.pack("<16sIQ", b"FilePackVer3.0", len(entries), index_offset))
            target.close()
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
    print(f"{len(entries)} entries, {output.stat().st_size} bytes: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build(args.root, args.manifest, args.output)


if __name__ == "__main__":
    main()
