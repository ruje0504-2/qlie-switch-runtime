#!/usr/bin/env python3
"""Extract the 256-byte QLIE icon key from a local game executable."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
data = args.executable.read_bytes()
start = data.rfind(bytes([5]) + b"TIcon")
if start < 0 or start + 262 > len(data):
    parser.error("QLIE icon key not found")
if args.output.exists():
    parser.error("output already exists")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(data[start + 6:start + 262])
print(f"Wrote 256 bytes to {args.output}")
