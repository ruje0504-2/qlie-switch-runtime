#!/usr/bin/env python3
"""Export the RESKEY entry from a local QLIE PE32 executable."""
import argparse
import os
import struct
from pathlib import Path


def extract_reskey(executable):
    data = executable.read_bytes()

    def values(fmt, offset):
        return struct.unpack_from("<" + fmt, data, offset)

    pe, = values("I", 0x3c)
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Not a PE executable")
    section_count, = values("H", pe + 6)
    optional_size, = values("H", pe + 20)
    optional = pe + 24
    if optional_size < 120 or values("H", optional)[0] != 0x10b:
        raise ValueError("Expected a PE32 resource directory")
    resource_rva, resource_size = values("II", optional + 112)
    sections = optional + optional_size
    ranges = [values("III", sections + i * 40 + 12) for i in range(section_count)]

    def file_offset(rva, size):
        for start, length, offset in ranges:
            if start <= rva <= start + length and size <= start + length - rva:
                position = offset + rva - start
                if position + size <= len(data):
                    return position
        raise ValueError("Unmapped PE resource data")

    base = file_offset(resource_rva, resource_size)

    def within(offset, size):
        if offset + size > resource_size:
            raise ValueError("Truncated PE resource directory")
        return base + offset

    def children(directory):
        named, ids = values("HH", within(directory + 12, 4))
        for i in range(named + ids):
            name, target = values("II", within(directory + 16 + i * 8, 8))
            if name & 0x80000000:
                offset = name & 0x7fffffff
                length, = values("H", within(offset, 2))
                position = within(offset + 2, length * 2)
                name = data[position:position + 2 * length].decode("utf-16le")
            yield name, target

    def directory(name, parent):
        for label, target in children(parent):
            if label == name and target & 0x80000000:
                return target & 0x7fffffff
        raise ValueError(f"Missing PE resource directory {name}")

    variants = set()
    for _, entry in children(directory("RESKEY", directory(10, 0))):
        if entry & 0x80000000:
            raise ValueError("Unexpected resource language directory")
        rva, length = values("II", within(entry, 8))
        position = file_offset(rva, length)
        variants.add(data[position:position + length])
    if len(variants) != 1:
        raise ValueError("Expected one distinct RESKEY payload")
    payload = variants.pop()
    if len(payload) < 128:
        raise ValueError("Truncated RESKEY")
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        payload = extract_reskey(args.executable)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    print(f"Wrote {len(payload)} bytes to {args.output}")


if __name__ == "__main__":
    main()
