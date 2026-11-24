#!/usr/bin/env python3
"""Fetch the pinned, project-local Horizon graphics build dependencies."""
import hashlib,json,tarfile,zipfile
from pathlib import Path
from urllib.request import Request,urlopen
root=Path(__file__).resolve().parents[1]
lock=json.loads((root/'config/graphics-dependencies.json').read_text())
for name,folder in [('mesa','mesa-sdk'),('rust','rust')]:
    target=root/'local'/folder
    stamp=target/'.archive-sha256'
    wanted=lock[name]['sha256']
    if stamp.exists() and stamp.read_text().strip()==wanted:
        print(name, 'already available');continue
    target.mkdir(parents=True,exist_ok=True)
    archive=target/('download.zip' if name=='mesa' else 'download.tar.xz')
    with urlopen(Request(lock[name]['url'],headers={'User-Agent':'WHJ2-build'}),timeout=60) as source,archive.open('wb') as output:
        while chunk:=source.read(1024*1024):output.write(chunk)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=wanted:
        raise RuntimeError(name+' archive SHA-256 mismatch')
    if name=='mesa':
        with zipfile.ZipFile(archive) as package:package.extractall(target)
    else:
        with tarfile.open(archive) as package:package.extractall(target,filter='data')
    stamp.write_text(wanted+'\n');archive.unlink();print(name,'ready')
