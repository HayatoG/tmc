#!/usr/bin/env python3
"""
generate_rom_tables.py — (re)generate port/port_rom_tables*.c from a baserom.

This is the regenerated version of the generator referenced in the header of
port_rom_tables.c (the original was lost). It reads a Minish Cap GBA ROM and
emits the compile-time ROM data/offset tables the native port bakes in
(kFrameObjListsData, kSpritePtrEntries, kAreaRoomHeaderOffsets, ...).

The committed port_rom_tables.c is USA-only; the offset/pointer tables it holds
are USA ROM offsets, which the EU (BZMP) build was applying to the EU ROM ->
garbage function pointers -> Instruction Abort at AgbMain. This tool produces a
per-region copy so the EU build links EU-correct tables.

Usage:
    generate_rom_tables.py validate <usa_rom>          # logic check vs committed
    generate_rom_tables.py emit <region> <rom> <out.c>  # region = USA | EU

The blob SIZES and entry COUNTS are region-invariant (verified: the deltas
between consecutive font/text offsets are identical USA vs EU); only the base
offsets and the pointer values differ between regions.
"""
import sys, re

# ---- Per-region ROM base offsets (mirror kRomOffsets_USA / _EU in port_rom.c) ----
OFFS = {
    'USA': dict(
        frameObjLists=0x2F3D74, fixedTypeGfx=0x132B30, overlaySizeTable=0x0B2BE8,
        text09244=0x109244, text0926C=0x10926C, text092D4=0x1092D4,
        text0942E=0x10942E, text094CE=0x1094CE, uiData=0x0C9044,
        spritePtrs=0x0029B4, areaRoomHeaders=0x11E214, areaTileSets=0x10246C,
        areaRoomMaps=0x107988, areaTable=0x0D50FC, areaTiles=0x10309C,
        translations=0x109214, text09230=0x109230, text09248=0x109248,
        text092AC=0x1092AC, extraFrameOffsets=0x9FB770),
    'EU': dict(
        frameObjLists=0x2F3460, fixedTypeGfx=0x132180, overlaySizeTable=0x0B25E8,
        text09244=0x108998, text0926C=0x1089C0, text092D4=0x108A28,
        text0942E=0x108B82, text094CE=0x108C22, uiData=0x0C876C,
        spritePtrs=0x002A5C, areaRoomHeaders=0x11D95C, areaTileSets=0x101BC8,
        areaRoomMaps=0x1070E4, areaTable=0x0D4828, areaTiles=0x1027F8,
        translations=0x108968, text09230=0x108984, text09248=0x10899C,
        text092AC=0x108A00, extraFrameOffsets=None),  # found by blob search
}

# Blob tables: (c_name, c_type, offset_key, size_bytes)
BLOBS = [
    ('kFrameObjListsData',   'u8',  'frameObjLists',    200045),
    ('kFixedTypeGfxInitData', 'u8', 'fixedTypeGfx',     2108),
    ('kOverlaySizeData',     'u8',  'overlaySizeTable', 240),
    ('kFontText09244Data',   'u8',  'text09244',        4),
    ('kFontText0926CData',   'u8',  'text0926C',        64),
    ('kFontText092D4Data',   'u8',  'text092D4',        346),
    ('kFontText0942EData',   'u8',  'text0942E',        160),
    ('kFontText094CEData',   'u8',  'text094CE',        1378),
    ('kUiInitData',          'u8',  'uiData',           8),
    ('kExtraFrameOffsetsData', 'u8','extraFrameOffsets',4352),
]
# Offset tables: (c_name, offset_key, count)
OFFTABS = [
    ('kAreaRoomHeaderOffsets', 'areaRoomHeaders', 144),
    ('kAreaTileSetOffsets',    'areaTileSets',    144),
    ('kAreaRoomMapOffsets',    'areaRoomMaps',    144),
    ('kAreaTableOffsets',      'areaTable',       144),
    ('kAreaTilesOffsets',      'areaTiles',       144),
    ('kTranslationOffsets',    'translations',    7),
    ('kUnk09230Offsets',       'text09230',       5),
    ('kUnk09248Offsets',       'text09248',       9),
    ('kUnk092ACOffsets',       'text092AC',       10),
]
SPRITE_COUNT = 329  # kSpritePtrEntries[329][4]

def u32(rom, off):
    return int.from_bytes(rom[off:off+4], 'little')

def conv(p):
    """GBA pointer -> ROM offset; NULL / non-ROM -> 0xFFFFFFFF."""
    return (p & 0x00FFFFFF) if (p & 0xFF000000) == 0x08000000 else 0xFFFFFFFF

def extract(rom, off):
    """Return dict {table_name: list_of_ints (blobs as byte ints)}."""
    out = {}
    # extraFrameOffsets EU base is located by searching for the USA blob.
    efo = off['extraFrameOffsets']
    for name, _t, key, size in BLOBS:
        base = efo if key == 'extraFrameOffsets' else off[key]
        out[name] = list(rom[base:base+size])
    for name, key, count in OFFTABS:
        base = off[key]
        out[name] = [conv(u32(rom, base + i*4)) for i in range(count)]
    base = off['spritePtrs']
    spr = []
    for i in range(SPRITE_COUNT):
        e = base + i*16
        a = conv(u32(rom, e)); fr = conv(u32(rom, e+4)); pt = conv(u32(rom, e+8))
        pad = u32(rom, e+12)
        spr.append((a, fr, pt, pad))
    out['kSpritePtrEntries'] = spr
    return out

# ---- Parse the committed USA file for value-level validation ----
def parse_committed(path):
    txt = open(path).read()
    res = {}
    def body(name, dim2=False):
        m = re.search(r'\b'+name+r'\s*\[[^\]]*\]'+(r'\[[^\]]*\]' if dim2 else '')+r'\s*=\s*\{(.*?)\n\};', txt, re.S)
        return m.group(1) if m else None
    for name, _t, _k, _s in BLOBS:
        b = body(name)
        res[name] = [int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', b)] if b is not None else None
    for name, _k, _c in OFFTABS:
        b = body(name)
        res[name] = [int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{8})', b)] if b is not None else None
    b = body('kSpritePtrEntries', dim2=True)
    if b is not None:
        nums = [int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{8})', b)]
        res['kSpritePtrEntries'] = [tuple(nums[i:i+4]) for i in range(0, len(nums), 4)]
    return res

def validate(rom):
    gen = extract(rom, OFFS['USA'])
    com = parse_committed('port/port_rom_tables.c')
    ok = True
    for name in list(g for g,*_ in BLOBS) + [o[0] for o in OFFTABS] + ['kSpritePtrEntries']:
        g = gen[name]; c = com.get(name)
        if c is None:
            print('  ?? %-26s no committed reference' % name); continue
        if g == c:
            print('  OK %-26s %d entries' % (name, len(g)))
        else:
            ok = False
            # first divergence
            n = min(len(g), len(c)); d = next((i for i in range(n) if g[i]!=c[i]), None)
            print('  XX %-26s MISMATCH len gen=%d com=%d first@%s gen=%r com=%r'
                  % (name, len(g), len(c), d, (g[d] if d is not None else None), (c[d] if d is not None else None)))
    return ok

# ---- Emit a port_rom_tables_<region>.c ----
def fmt_blob(name, data):
    out = ['const u8 %s[%d] = {' % (name, len(data))]
    for i in range(0, len(data), 16):
        out.append('    ' + ''.join('0x%02X,' % b for b in data[i:i+16]))
    out.append('};\n'); return '\n'.join(out)

def fmt_offtab(name, vals):
    out = ['const u32 %s[%d] = {' % (name, len(vals))]
    for i in range(0, len(vals), 8):
        out.append('    ' + ''.join('0x%08X,' % v for v in vals[i:i+8]))
    out.append('};\n'); return '\n'.join(out)

def fmt_sprite(vals):
    out = ['const u32 kSpritePtrEntries[%d][4] = {' % len(vals)]
    for i,(a,fr,pt,pad) in enumerate(vals):
        out.append('    {0x%08X,0x%08X,0x%08X,0x%08X}, /* [%d] */' % (a,fr,pt,pad,i))
    out.append('};\n'); return '\n'.join(out)

def emit(region, rom, outpath):
    off = dict(OFFS[region])
    efo_fallback = None
    if off['extraFrameOffsets'] is None:
        # Try to locate gExtraFrameOffsets in this ROM by matching the USA blob.
        with open(usa_rom_path,'rb') as f: usa = f.read()
        ub = OFFS['USA']['extraFrameOffsets']
        needle = bytes(usa[ub:ub+4352])
        idx = rom.find(needle)
        if idx >= 0:
            print('  located extraFrameOffsets @ 0x%X' % idx)
            off['extraFrameOffsets'] = idx
        else:
            # Not found by content (the EU table differs). gExtraFrameOffsets is
            # only used by physics.c for multi-part sprite positioning, off the
            # boot/critical path. Fall back to the USA blob so the build links;
            # multi-part sprites may glitch until the true EU address is found
            # (would need the EU decomp symbol map).
            print('  WARN: gExtraFrameOffsets not locatable in', region,
                  '- falling back to USA data (multi-part sprite positioning only)')
            off['extraFrameOffsets'] = ub
            efo_fallback = list(usa[ub:ub+4352])
    data = extract(rom, off)
    if efo_fallback is not None:
        data['kExtraFrameOffsetsData'] = efo_fallback
    parts = ['/* port_rom_tables_%s.c - AUTO-GENERATED by tools/generate_rom_tables.py' % region.lower(),
             ' * Region: %s. DO NOT EDIT MANUALLY. */' % region,
             '#include "common.h"', '']
    for name,_t,_k,_s in BLOBS:
        parts.append(fmt_blob(name, data[name]))
    parts.append(fmt_sprite(data['kSpritePtrEntries']))
    for name,_k,_c in OFFTABS:
        parts.append(fmt_offtab(name, data[name]))
    open(outpath,'w').write('\n'.join(parts))
    print('wrote', outpath)

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'validate':
        usa_rom_path = sys.argv[2]
        rom = open(usa_rom_path,'rb').read()
        print('Validating generator against committed USA port_rom_tables.c ...')
        sys.exit(0 if validate(rom) else 1)
    elif cmd == 'emit':
        region = sys.argv[2]; rompath = sys.argv[3]; out = sys.argv[4]
        usa_rom_path = sys.argv[5] if len(sys.argv) > 5 else 'baserom.gba'
        rom = open(rompath,'rb').read()
        emit(region, rom, out)
    else:
        print(__doc__); sys.exit(1)
