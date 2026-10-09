#!/usr/bin/env python3
"""Import the user's local Android 1.03 resources.

This is an asset importer, not a build. All output stays inside this project.
RGBA bytes are preserved (including the terrain's collision-bearing alpha).
The data pack avoids a runtime PNG decoder and keeps ExFAT allocation overhead low.
Pillow is used only to prepare indexed LiveArea artwork for the Vita installer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import zlib


def png_rgba(path):
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Not a PNG: {path}')
    pos, stream, palette, transparency = 8, bytearray(), b'', b''
    while pos < len(data):
        n = struct.unpack_from('>I', data, pos)[0]
        tag, chunk = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if zlib.crc32(tag + chunk) & 0xffffffff != struct.unpack_from('>I', data, pos + 8 + n)[0]:
            raise ValueError(f'PNG CRC: {path}')
        if tag == b'IHDR':
            w, h, depth, kind, comp, filt, interlace = struct.unpack('>IIBBBBB', chunk)
            if depth != 8 or interlace or comp or filt:
                raise ValueError(f'Unsupported PNG format: {path}')
        elif tag == b'IDAT': stream.extend(chunk)
        elif tag == b'PLTE': palette = chunk
        elif tag == b'tRNS': transparency = chunk
        pos += n + 12
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[kind]
    stride = w * channels
    raw = zlib.decompress(stream)
    if len(raw) != (stride + 1) * h:
        raise ValueError(f'PNG length: {path}')
    previous, rgba = bytearray(stride), bytearray()
    for y in range(h):
        offset = y * (stride + 1)
        mode, row = raw[offset], bytearray(raw[offset + 1:offset + 1 + stride])
        for x in range(stride):
            a = row[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if mode == 0: delta = 0
            elif mode == 1: delta = a
            elif mode == 2: delta = b
            elif mode == 3: delta = (a + b) // 2
            elif mode == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                delta = a if pa <= pb and pa <= pc else b if pb <= pc else c
            else: raise ValueError(f'PNG filter: {path}')
            row[x] = (row[x] + delta) & 255
        if kind == 6: rgba.extend(row)
        else:
            for x in range(w):
                q = x * channels
                if kind == 3:
                    v = row[q]
                    rgba.extend(palette[v * 3:v * 3 + 3])
                    rgba.append(transparency[v] if v < len(transparency) else 255)
                elif kind == 2:
                    rgb = row[q:q + 3]
                    rgba.extend(rgb)
                    transparent = transparency and tuple(rgb) == struct.unpack('>HHH', transparency)
                    rgba.append(0 if transparent else 255)
                else:
                    v = row[q]
                    rgba.extend((v, v, v))
                    rgba.append(row[q + 1] if kind == 4 else
                                0 if transparency and v == struct.unpack('>H', transparency)[0] else 255)
        previous = row
    return w, h, bytes(rgba)


def livearea(textures, project):
    from PIL import Image

    # Match the working native Vita package: opaque, 8-bit indexed PNGs.
    # Quantization applies only to shell artwork, never game/collision textures.
    for source, target, size in (
        ('mdpi_icon.png', 'icon0.png', (128, 128)),
        ('mdpi_ititle.png', 'livearea/contents/bg.png', (840, 500)),
        ('mdpi_ititle.png', 'livearea/contents/startup.png', (280, 158)),
    ):
        with Image.open(textures / source) as original:
            rgba = original.convert('RGBA')
            opaque = Image.new('RGB', rgba.size, (0, 0, 0))
            opaque.paste(rgba, mask=rgba.getchannel('A'))
            indexed = opaque.resize(size, Image.Resampling.LANCZOS).quantize(
                colors=256, method=Image.Quantize.MEDIANCUT)
            output = project / 'sce_sys' / target
            output.parent.mkdir(parents=True, exist_ok=True)
            indexed.save(output, format='PNG', bits=8, optimize=True)


def tables(sources, output):
    text = (sources / 'com/c/d/c.java').read_text()
    terrain = [dict(a=(0, 0), b=(0, 0), c=[(0, 0)] * 2, d=[(0, 0)] * 2) for _ in range(20)]
    pattern = r'\(\(f\) this.a.get\((\d+)\)\)\.([abcd])(?:\[(\d+)\])?\.a\((-?[\d.]+)f, (-?[\d.]+)f\)'
    for i, field, index, x, y in re.findall(pattern, text):
        value = (int(float(x)), int(float(y)))
        if index: terrain[int(i)][field][int(index)] = value
        else: terrain[int(i)][field] = value
    nodes = [dict(tile=0, count=0, edges=[[0, 0] for _ in range(8)]) for _ in range(26)]
    for i, field, index, subfield, value in re.findall(
            r'\(\(g\) this.b.get\((\d+)\)\)\.([abc])(?:\[(\d+)\]\.([ab]))? = (-?\d+);', text):
        n = nodes[int(i)]
        if field == 'c': n['edges'][int(index)][0 if subfield == 'a' else 1] = int(value)
        else: n['tile' if field == 'a' else 'count'] = int(value)
    difficulty = re.search(r'int\[\] c = \{([^}]+)', text).group(1)
    manager = (sources / 'com/c/d/e.java').read_text()
    crop = re.search(r'a\[\] p = \{([^;]+)', manager).group(1)
    crops = [(int(float(x)), int(float(y))) for x, y in re.findall(r'new com.b.j.a\(([\d.]+)f, ([\d.]+)f\)', crop)]
    intro = (sources / 'com/c/g/i.java').read_text().split('} else {', 1)[1]
    positions = [(int(float(x)), int(float(y)) - 41) for x, y in re.findall(r'this.n.add\(new com.b.j.b\(([\d.]+)f, ([\d.]+)f\)\)', intro)]
    if len(crops) != 20 or len(positions) != 54 or any(n['count'] == 0 for n in nodes):
        raise ValueError('Unexpected original source layout; refusing to guess tables')
    point = lambda p: '{%s, %s}' % tuple(p)
    lines = ['// Imported directly from Android 1.03 com/c/d/{c,e}.java and com/c/g/i.java.',
             '// Regenerate with tools/import_original.py. Coordinates are original MDPI pixels.',
             '#pragma once', '#include <array>', 'namespace rua {',
             'struct Point { float x, y; };',
             'struct TerrainDef { Point start, end, stars[2], fairies[2], crop; };',
             'struct Edge { int node, rise; };',
             'struct Node { int tile, count; Edge edges[8]; };',
             'inline constexpr int difficulty[20] = {' + difficulty + '};',
             'inline constexpr TerrainDef terrainDefs[20] = {']
    for t, c in zip(terrain, crops):
        lines.append('  {' + ', '.join([point(t['a']), point(t['b']), '{' + ', '.join(map(point, t['c'])) + '}',
                                      '{' + ', '.join(map(point, t['d'])) + '}', point(c)]) + '},')
    lines += ['};', 'inline constexpr Node nodes[26] = {']
    for n in nodes:
        lines.append('  {%d, %d, {%s}},' % (n['tile'], n['count'], ', '.join(point(e) for e in n['edges'])))
    lines += ['};', 'inline constexpr Point introPositions[54] = {',
              ',\n'.join('  ' + point(p) for p in positions), '};', '}']
    output.write_text('\n'.join(lines) + '\n')


def main():
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument('--original', type=Path, default=project.parent / 'Robot Unicorn Attack-V1.03')
    parser.add_argument('--sources', type=Path, help='Optionally regenerate tables from decompiled Android sources')
    parser.add_argument('--livearea-only', action='store_true', help='Regenerate only Vita shell artwork')
    args = parser.parse_args()
    textures = args.original / 'res/drawable-nodpi'
    livearea(textures, project)
    if args.livearea_only:
        print('Prepared 8-bit indexed LiveArea PNGs.')
        return
    dest = project / 'data'
    dest.mkdir(exist_ok=True)
    selected = sorted([p for p in textures.glob('*.png') if p.name.startswith(('mdpi_', 'nodpi_'))])
    entries, manifest = [], {}
    for p in selected:
        w, h, rgba = png_rgba(p)
        name = p.stem.removeprefix('mdpi_').removeprefix('nodpi_')
        entries.append((name, w, h, rgba))
        manifest[p.name] = hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((args.original / 'res/raw').glob('*.wav')):
        entries.append((p.stem, 0, 0, p.read_bytes()))
        manifest[p.name] = hashlib.sha256(p.read_bytes()).hexdigest()
    # Fixed 88-byte directory: name[64], offset, packed size, raw size, w, h, CRC32.
    partial = dest / 'original.rup.partial'
    with partial.open('wb') as out:
        out.write(b'RUP1' + struct.pack('<I', len(entries)))
        out.write(bytes(88 * len(entries)))
        directory = bytearray()
        for name, w, h, data in entries:
            packed = zlib.compress(data, 6)
            directory.extend(name.encode().ljust(64, b'\0') + struct.pack('<6I', out.tell(), len(packed), len(data), w, h, zlib.crc32(data) & 0xffffffff))
            out.write(packed)
        out.seek(8)
        out.write(directory)
    partial.replace(dest / 'original.rup')
    for name in ('game.mp3', 'opening.mp3'):
        p = args.original / 'res/raw' / name
        shutil.copyfile(p, dest / name)
        manifest[name] = hashlib.sha256(p.read_bytes()).hexdigest()
    (dest / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if args.sources:
        tables(args.sources, project / 'src/original_data.hpp')
    print(f'Imported {len(entries)} resources to {dest}.')


if __name__ == '__main__':
    main()
