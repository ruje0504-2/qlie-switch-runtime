#!/usr/bin/env python3
"""Create a native macOS launcher bundle without duplicating the runtime."""
import argparse
import os
import plistlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game', required=True, type=Path)
parser.add_argument('--saves', required=True, type=Path)
parser.add_argument('--binary', type=Path, default=Path('build/whj2-runtime'))
parser.add_argument('--output', type=Path, default=Path('build/WHJ2-Mac.app'))
args = parser.parse_args()
binary = args.binary.resolve(strict=True)
game = args.game.resolve(strict=True)
saves = args.saves.resolve()
saves.mkdir(parents=True, exist_ok=True)
contents = args.output.resolve() / 'Contents'
executable = contents / 'MacOS' / 'whj2-runtime'
executable.parent.mkdir(parents=True, exist_ok=True)
if executable.exists():
    if not os.path.samefile(binary, executable):
        raise SystemExit('Existing app has another runtime; close it and remove its generated executable before repackaging.')
else:
    os.link(binary, executable)
info = {
    'CFBundleIdentifier': 'local.whj2.runtime',
    'CFBundleName': 'WHJ2 Mac',
    'CFBundleDisplayName': 'WHJ2 Mac',
    'CFBundleExecutable': 'whj2-runtime',
    'CFBundlePackageType': 'APPL',
    'CFBundleVersion': '3',
    'NSHighResolutionCapable': True,
    'LSEnvironment': {
        'WHJ2_GAME': str(game), 'WHJ2_SAVES': str(saves),
        'WHJ2_LOG': str(saves / 'app-runtime.log'),
    },
}
plist = contents / 'Info.plist'
if not plist.exists() or plistlib.loads(plist.read_bytes()) != info:
    plist.write_bytes(plistlib.dumps(info))
print(contents.parent)
