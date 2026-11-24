#!/usr/bin/env python3
"""Package the fixed Switch ELF and local game data as an installed application.

RomFS/NPDM/NACP layout follows kisaku-switch-runtime at 95d71d9.
Game resources and generated packages are local artifacts, never source assets.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = [f"GameData/data{n}.pack" for n in (0, 1, 2, 3, 6, 7, 8)] + ["DLL/key.fkey"]
LANGUAGES = ["AmericanEnglish", "BritishEnglish", "Japanese", "French", "German",
             "LatAmSpanish", "Spanish", "Italian", "Dutch", "CanFrench",
             "Portuguese", "Russian", "Korean", "TradChinese", "SimpChinese", "Reserved"]

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def check_icon(data):
    if data[:2] != b'\xff\xd8':
        raise ValueError('Icon is not JPEG')
    offset = 2
    while offset + 4 <= len(data):
        if data[offset] != 255:
            raise ValueError('Malformed JPEG marker')
        marker = data[offset+1]
        if marker == 0xda:
            break
        size = int.from_bytes(data[offset+2:offset+4], 'big')
        if size < 2 or offset+2+size > len(data):
            raise ValueError('Malformed JPEG segment')
        if marker in (0xc0, 0xc1, 0xc2):
            height = int.from_bytes(data[offset+5:offset+7], 'big')
            width = int.from_bytes(data[offset+7:offset+9], 'big')
            if marker != 0xc0 or (width, height) != (256, 256):
                raise ValueError('HOME icon must be 256x256 baseline JPEG')
            return
        offset += 2 + size
    raise ValueError('JPEG baseline frame not found')

def pfs_entries(path):
    with path.open('rb') as f:
        head = f.read(16)
        if head[:4] != b'PFS0':
            raise ValueError('NSP PFS0 header missing')
        count, string_size, _ = struct.unpack_from('<III', head, 4)
        table = f.read(count*24)
        names = f.read(string_size)
        base = 16+count*24+string_size
        for i in range(count):
            offset, size, name_at, _ = struct.unpack_from('<QQII', table, i*24)
            name = names[name_at:].split(b'\0', 1)[0].decode()
            if base+offset+size > path.stat().st_size:
                raise ValueError('NSP entry outside package')
            yield name, base+offset, size

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', required=True, type=Path)
    ap.add_argument('--elf', type=Path, default=ROOT/'build-switch/whj2-runtime.elf')
    ap.add_argument('--config', type=Path, default=ROOT/'config/nsp.json')
    ap.add_argument('--output', type=Path)
    ap.add_argument('--icon', required=True, type=Path)
    ap.add_argument('--keyset', type=Path, default=Path.home()/'.switch/prod.keys')
    ap.add_argument('--hacbrewpack', type=Path, default=Path.home()/'bin/hacbrewpack')
    args = ap.parse_args()
    config = json.loads(args.config.read_text())
    title_id = config['title_id'].upper()
    if len(title_id) != 16 or not title_id.startswith('0100') or int(title_id, 16) & 0xfff:
        ap.error('Expected a base application Title ID with low12 bits clear')
    output = (args.output or ROOT/'delivery'/f'WHJ2-{title_id}-{config["version"]}.nsp').resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        ap.error('Output already exists; retain or remove the previous package explicitly')
    game, elf = args.game.resolve(), args.elf.resolve()
    for resource in RESOURCES:
        if not (game/resource).is_file():
            ap.error('Missing required game resource: '+resource)
    if not elf.is_file() or not args.keyset.is_file() or not args.hacbrewpack.is_file():
        ap.error('ELF, local keyset, or hacbrewpack is missing')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if revision.encode() not in elf.read_bytes():
        ap.error('ELF does not contain the current fixed source revision; build it first')
    report = {'config': config, 'commit': revision, 'elf_sha256': digest(elf), 'phase': 'building', 'icon_sha256': digest(args.icon), 'packaging_script_sha256': digest(Path(__file__))}
    report_path = output.with_suffix('.json')
    log_path = output.with_suffix('.log')
    def save():
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    save()
    try:
        with tempfile.TemporaryDirectory(prefix='.whj2-nsp-', dir=output.parent) as temp, log_path.open('w') as log:
            work = Path(temp)
            for name in ('exefs', 'control', 'romfs', 'output', 'verify'):
                (work/name).mkdir()
            def run(cmd):
                return subprocess.run([str(x) for x in cmd], cwd=work, stdout=log, stderr=subprocess.STDOUT, check=True)
            run(['/opt/devkitpro/devkitA64/bin/aarch64-none-elf-strip', elf, '-o', work/'main.elf'])
            run(['/opt/devkitpro/tools/bin/elf2nso', work/'main.elf', work/'exefs/main'])
            npdm = json.loads((ROOT/'config/nsp-npdm.json').read_text())
            for field in ('title_id', 'title_id_range_min', 'program_id', 'program_id_range_min'):
                npdm[field] = '0x'+title_id
            (work/'npdm.json').write_text(json.dumps(npdm, indent=2))
            run(['/opt/devkitpro/tools/bin/npdmtool', work/'npdm.json', work/'exefs/main.npdm'])
            nacp_path = work/'control/control.nacp'
            run(['/opt/devkitpro/tools/bin/nacptool', '--create', config['title'], config['publisher'], config['version'], nacp_path, '--titleid='+title_id])
            nacp = bytearray(nacp_path.read_bytes())
            title_bytes, author = config['title'].encode(), config['publisher'].encode()
            if len(nacp) != 0x4000 or len(title_bytes) >= 0x200 or len(author) >= 0x100:
                raise ValueError('Invalid NACP size or title text length')
            for i in range(16):
                nacp[i*0x300:i*0x300+0x200] = title_bytes.ljust(0x200, b'\0')
                nacp[i*0x300+0x200:(i+1)*0x300] = author.ljust(0x100, b'\0')
            struct.pack_into('<I', nacp, 0x302c, 0xffff)
            nacp[0x3025] = 1  # Require an account so SaveData has a selected user.
            struct.pack_into('<Q', nacp, 0x3078, int(title_id, 16))
            struct.pack_into('<QQ', nacp, 0x3080, config['save_size'], config['journal_size'])
            nacp_path.write_bytes(nacp)
            run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', args.icon.resolve(), '-frames:v', '1', '-vf', 'scale=256:256', '-c:v', 'mjpeg', '-q:v', '2', '-pix_fmt', 'yuvj420p', work/'icon.jpg'])
            icon_bytes = (work/'icon.jpg').read_bytes()
            check_icon(icon_bytes)
            report['icon_encoding'] = 'baseline JPEG SOF0, 256x256, yuvj420p'
            for language in LANGUAGES:
                os.link(work/'icon.jpg', work/'control'/('icon_'+language+'.dat'))
            files = list(RESOURCES)
            report['romfs'] = []
            for relative in files:
                source, target = game/relative, work/'romfs'/relative
                target.parent.mkdir(parents=True, exist_ok=True)
                try:
                    os.link(source, target)
                except OSError as error:
                    if error.errno != 18:  # Cross-device output location.
                        raise
                    shutil.copyfile(source, target)
                report['romfs'].append({'name': relative, 'bytes': source.stat().st_size})
            existing_key = game/'DLL/icon.key'
            if existing_key.is_file():
                icon_key = existing_key.read_bytes()
            else:
                source = (game/'美少女万華鏡２.exe').read_bytes()
                marker = source.rfind(bytes([5])+b'TIcon')
                if marker < 0 or marker+262 > len(source):
                    raise ValueError('QLIE icon key absent')
                icon_key = source[marker+6:marker+262]
            if len(icon_key) != 256:
                raise ValueError('QLIE icon key must be256 bytes')
            (work/'romfs/DLL/icon.key').write_bytes(icon_key)
            report['romfs'].append({'name': 'DLL/icon.key', 'bytes': len(icon_key)})
            run([args.hacbrewpack, '--keyset', args.keyset, '--titleid', title_id,
                 '--titlename', config['title'], '--titlepublisher', config['publisher'],
                 '--nspdir', work/'output', '--nologo'])
            results = list((work/'output').glob('*.nsp'))
            if len(results) != 1:
                raise ValueError('Expected one generated NSP')
            os.replace(results[0], output)
            report['phase'] = 'verifying'
            save()
            controls = []
            with output.open('rb') as package:
                for entry_name, offset, size in pfs_entries(output):
                    if not entry_name.endswith('.nca') or size > 8*1024*1024:
                        continue
                    candidate = work/'verify'/Path(entry_name).name
                    package.seek(offset)
                    candidate.write_bytes(package.read(size))
                    info = subprocess.run(['/opt/devkitpro/tools/bin/hactool', '-t', 'nca', '-k', str(args.keyset), str(candidate)], capture_output=True, text=True, check=True).stdout
                    if re.search(r'Content Type:\s+Control\b', info):
                        controls.append(candidate)
            if len(controls) != 1:
                raise ValueError('Expected one Control NCA in final NSP')
            control = work/'verify/control'
            control.mkdir()
            run(['/opt/devkitpro/tools/bin/hactool', '-t', 'nca', '-k', args.keyset, '--romfsdir='+str(control), controls[0]])
            final = (control/'control.nacp').read_bytes()
            for i in range(16):
                if final[i*0x300:i*0x300+0x200].split(b'\0')[0] != title_bytes or final[i*0x300+0x200:(i+1)*0x300].split(b'\0')[0] != author:
                    raise ValueError('Final NACP language metadata mismatch')
            if final[0x3060:0x3070].split(b'\0')[0].decode() != config['version']:
                raise ValueError('Final NACP version mismatch')
            if struct.unpack_from('<QQQ', final, 0x3078) != (int(title_id, 16), config['save_size'], config['journal_size']):
                raise ValueError('Final NACP title/save metadata mismatch')
            if struct.unpack_from('<I', final, 0x302c)[0] != 0xffff:
                raise ValueError('Final NACP language support mismatch')
            for language in LANGUAGES:
                actual_icon = (control/('icon_'+language+'.dat')).read_bytes()
                check_icon(actual_icon)
                if actual_icon != icon_bytes:
                    raise ValueError('Final Control icon mismatch')
            if final[0x3025] != 1:
                raise ValueError('Final NACP selected-user requirement missing')
            report.update(phase='complete', nsp=str(output), bytes=output.stat().st_size,
                          sha256=digest(output), final_nacp_sha256=hashlib.sha256(final).hexdigest())
            save()
    except Exception as error:
        report.update(phase='failed', error=str(error))
        save()
        raise
    print(output)
    print(report['sha256'])

if __name__ == '__main__':
    main()
