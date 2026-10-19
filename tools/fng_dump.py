#!/usr/bin/env python3
"""fng_dump -- read an EA FEng (.fng) screen package out of a game bundle.

NFSU2 lays out every 2D screen (the in-game HUD included) as an FEng package:
chunk 0x00030203 (or 0x00030210 = JDLZ-compressed) inside GLOBAL/*.bun. This
tool lists its objects (type, texture, centre, size, colour, rotation, parent)
and can composite a preview PNG from the game's own textures, so a HUD layout
can be checked against the original before any engine code changes.

Local-only: the preview contains retail art -- never commit its output.

usage:
  fng_dump.py BUNDLE                      list FEng packages in BUNDLE
  fng_dump.py BUNDLE NAME.fng             dump that package's objects
  fng_dump.py BUNDLE NAME.fng --png OUT.png [--tex BUN ...] [--all] [--style N]
                                          [--fonts LANGUAGES_DIR]
        composite a 640x480 preview; textures are looked up in BUNDLE plus
        every --tex bundle. Mode-specific groups (flag 0x80000000) are skipped
        unless --all is given. --style picks the HUD_CustomTextures_NN gauge
        art (default 00).

Format notes (verified on GLOBAL/InGameRace.bun HUD_SingleRace.fng):
  'FEn\\xe7' container of 4-char chunks: PkHd, TypS, 'Res\\xcc'{RsNm, RsRq},
  'Obj\\xcc'{'FOb\\xea'{ObjD, Scrp...}}. ObjD/Scrp hold 2-char tags with u16
  sizes: Ot type, Oh hash, OP {guid, hash, flags, resource index}, PA parent
  guid, SA object data, St/Sb/Sj/Sl/Sw/SH string fields, If image flags.
  SA = colour (u32 b,g,r,a) pivot[3] position[3] rotation quat[4] size[3]
       (+ uv x0,y0,x1,y1 for images). Positions are object CENTRES in a
       640x480 space with the origin at screen centre, y down, relative to the
       parent group. RsRq rows are 24 bytes: id, name offset into RsNm, type.
  The game's TPK key for a resource is bStringHash of its upper-cased name
  without extension (hash = hash*33 + c from 0xffffffff).
"""
import math
import os
import struct
import sys

TYPES = {1: 'Image', 2: 'String', 3: 'Model', 4: 'List', 5: 'Group',
         6: 'CodeList', 7: 'Movie', 8: 'Effect', 9: 'ColoredImage',
         10: 'AnimImage', 11: 'SimpleImage', 12: 'MultiImage'}
IMAGE_TYPES = (1, 9, 10, 11, 12)
FLAG_MODE_HIDDEN = 0x80000000
# Art the game uses only to clip other sprites (an opaque black half-disc that
# limits the NOS bar), never drawn as itself.
STENCIL_ONLY = {'nos_mask.tga'}


def bhash(s):
    h = 0xffffffff
    for c in s.encode('latin1'):
        h = (h * 33 + c) & 0xffffffff
    return h


def tex_key(resname):
    return bhash(resname.rsplit('.', 1)[0].upper())


# ------------------------------------------------------------------ JDLZ

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jdlz import jdlz_decompress as jdlz  # noqa: E402


def huff(src):
    """EA "HUFF" blob (16-byte wrapper + EAC canonical-Huffman stream).
    Straight port of n2_huff in src/nfsu2.h."""
    s = src[16:]
    pos = [0]
    nbits = len(s) * 8

    def bits(n):
        v = 0
        for _ in range(n):
            if pos[0] >= nbits:
                v <<= 1
                continue
            v = v << 1 | ((s[pos[0] >> 3] >> (7 - (pos[0] & 7))) & 1)
            pos[0] += 1
        return v

    def varlen():
        num = 2
        while bits(1) == 0 and num < 24:
            num += 1
        return bits(num) + (1 << num) - 4

    method = bits(16)
    if (method & 0xF0FF) != 0x30FB:
        raise ValueError('not an EAC Huffman stream')
    if method & 0x100:
        bits(24)
    dsize = bits(24)
    esc = bits(8)
    numcodes, width, total, code = [0] * 17, 0, 0, 0
    while width < 16 and (code << (16 - width)) < 0x10000:
        n = varlen()
        width += 1
        numcodes[width] = n
        total += n
        code = code * 2 + n
    vals, defined, dat = [], [False] * 256, 0xFF
    for _ in range(total):
        n = varlen() + 1
        while n > 0:
            dat = (dat + 1) & 0xFF
            if not defined[dat]:
                n -= 1
        defined[dat] = True
        vals.append(dat)
    first_code, first_idx = [0] * 18, [0] * 18
    c = idx = 0
    for w in range(1, width + 1):
        first_code[w], first_idx[w] = c, idx
        c = (c + numcodes[w]) << 1
        idx += numcodes[w]
    out = bytearray()
    while len(out) < dsize and pos[0] < nbits:
        v = w = 0
        sym = -1
        while w < width:
            v = v << 1 | bits(1)
            w += 1
            if 0 <= v - first_code[w] < numcodes[w]:
                sym = vals[first_idx[w] + v - first_code[w]]
                break
        if sym < 0:
            raise ValueError('corrupt Huffman stream')
        if sym != esc:
            out.append(sym)
            continue
        n = varlen()
        if n == 0:
            if bits(1):
                break
            out.append(bits(8))
        else:
            prev = out[-1] if out else 0
            out += bytes([prev]) * min(n, dsize - len(out))
    if (method & 0xFEFF) == 0x32FB:
        x = 0
        for i in range(len(out)):
            x = (x + out[i]) & 0xFF
            out[i] = x
    elif (method & 0xFEFF) == 0x34FB:
        x = y = 0
        for i in range(len(out)):
            x = (x + out[i]) & 0xFF
            y = (y + x) & 0xFF
            out[i] = y
    return bytes(out)


# ------------------------------------------------------------------ FEng

def chunks(d, o, end):
    while o + 8 <= end:
        t = d[o:o + 4]
        sz = struct.unpack_from('<I', d, o + 4)[0]
        yield t, o + 8, sz
        o += 8 + sz


def tags(d, o, end):
    while o + 4 <= end:
        t = d[o:o + 2]
        sz = struct.unpack_from('<H', d, o + 2)[0]
        yield t, o + 4, sz
        o += 4 + sz


def list_packages(bun):
    """[(name, payload bytes)] for every FEng package chunk at top level."""
    out = []
    for t, o, sz in chunks(bun, 0, len(bun)):
        cid = struct.unpack('<I', t)[0]
        if cid not in (0x30203, 0x30210):
            continue
        body = bun[o:o + sz]
        if cid == 0x30210:
            j = body.find(b'JDLZ')
            if j < 0:
                continue
            body = jdlz(body[j:])
        f = body.find(b'FEn\xe7')
        if f < 0:
            continue
        body = body[f:]
        # PkHd payload: six u32 header words, then the package name string
        p = body.find(b'PkHd')
        name = body[p + 8 + 0x18:].split(b'\0', 1)[0].decode('latin1')
        out.append((name, body))
    return out


def parse_package(pk):
    d = pk
    end = 8 + struct.unpack_from('<I', d, 4)[0]
    names, res, objs = b'', [], []
    for t, o, sz in chunks(d, 8, end):
        if t == b'Res\xcc':
            for t2, o2, s2 in chunks(d, o, o + sz):
                if t2 == b'RsNm':
                    names = d[o2:o2 + s2]
            for t2, o2, s2 in chunks(d, o, o + sz):
                if t2 == b'RsRq':
                    n = struct.unpack_from('<I', d, o2)[0]
                    for i in range(n):
                        rid, noff, typ = struct.unpack_from('<III', d, o2 + 4 + i * 24)
                        nm = names[noff:names.index(b'\0', noff)].decode('latin1')
                        res.append({'name': nm, 'type': typ, 'id': rid})
        if t != b'Obj\xcc':
            continue
        for t2, o2, s2 in chunks(d, o, o + sz):
            if t2 != b'FOb\xea':
                continue
            ob = {'scripts': [], 'parent': None, 'order': len(objs)}
            for t3, o3, s3 in chunks(d, o2, o2 + s2):
                if t3 == b'Scrp':
                    for t4, o4, s4 in tags(d, o3, o3 + s3):
                        if t4 == b'Sh':
                            ob['scripts'].append(struct.unpack_from('<I', d, o4)[0])
                if t3 != b'ObjD':
                    continue
                for t4, o4, s4 in tags(d, o3, o3 + s3):
                    v = d[o4:o4 + s4]
                    if t4 == b'Ot':
                        ob['type'] = struct.unpack('<I', v)[0]
                    elif t4 == b'OP':
                        ob['guid'], ob['hash'], ob['flags'], ob['res'] = struct.unpack('<IIII', v)
                    elif t4 == b'PA':
                        ob['parent'] = struct.unpack('<I', v)[0]
                    elif t4 == b'SA':
                        b, g, r, a = struct.unpack('<4I', v[:16])
                        f = struct.unpack('<%df' % ((s4 - 16) // 4), v[16:])
                        ob['rgba'] = (r, g, b, a)
                        ob['pivot'], ob['pos'] = f[0:3], f[3:6]
                        ob['rot'], ob['size'] = f[6:10], f[10:13]
                        ob['uv'] = f[13:17] if len(f) >= 17 else (0, 0, 1, 1)
                    elif t4 == b'St':
                        ob['text'] = v.decode('utf-16-le', 'replace').rstrip('\0')
                    elif t4 == b'Sj':
                        ob['just'] = struct.unpack('<I', v)[0]
            objs.append(ob)
    # Parents are referenced by guid; guids are only unique among the objects
    # that precede a child, so resolve each parent to the NEAREST earlier group.
    for i, ob in enumerate(objs):
        ob['parent_obj'] = None
        if ob['parent'] is None:
            continue
        for j in range(i - 1, -1, -1):
            if objs[j].get('guid') == ob['parent'] and objs[j].get('type') == 5:
                ob['parent_obj'] = objs[j]
                break
    for ob in objs:
        if ob.get('type') in IMAGE_TYPES and ob.get('res', 1 << 30) < len(res):
            ob['resname'] = res[ob['res']]['name']
        if ob.get('type') == 2 and ob.get('res', 1 << 30) < len(res):
            ob['font'] = res[ob['res']]['name']
        ob['abs'] = absolute(ob)
    return res, objs


def quat_angle(q):
    """Rotation about the screen axis, radians (FEng quats are pure z here)."""
    return 2.0 * math.atan2(q[2], q[3])


def absolute(ob):
    """Screen-space centre, rotation and z, composing parent groups."""
    x, y, z = ob['pos']
    ang = quat_angle(ob['rot'])
    p = ob['parent_obj']
    hidden = bool(ob.get('flags', 0) & FLAG_MODE_HIDDEN)
    while p is not None:
        pa = quat_angle(p['rot'])
        if pa:
            c, s = math.cos(pa), math.sin(pa)
            x, y = x * c - y * s, x * s + y * c
        x += p['pos'][0]
        y += p['pos'][1]
        z += p['pos'][2]
        ang += pa
        hidden = hidden or bool(p.get('flags', 0) & FLAG_MODE_HIDDEN)
        p = p['parent_obj']
    return {'x': x, 'y': y, 'z': z, 'ang': ang, 'mode_hidden': hidden}


def depth(ob):
    n, p = 0, ob['parent_obj']
    while p is not None:
        n, p = n + 1, p['parent_obj']
    return n


def dump(res, objs):
    print('resources:')
    for i, r in enumerate(res):
        k = ' key %08x' % tex_key(r['name']) if r['type'] == 1 else ''
        print('  %2d %-34s type %d%s' % (i, r['name'], r['type'], k))
    print('objects (pos = centre, 640x480 space, origin at screen centre):')
    for ob in objs:
        a = ob['abs']
        line = '%s%08x %-6s %-28s pos(%g,%g) abs(%.1f,%.1f,z%g) size(%g,%g) rgba%s' % (
            '  ' * depth(ob), ob.get('hash', 0), TYPES.get(ob.get('type'), ob.get('type')),
            ob.get('resname', ''), ob['pos'][0], ob['pos'][1], a['x'], a['y'], a['z'],
            ob['size'][0], ob['size'][1], ob['rgba'])
        if a['ang']:
            line += ' rot%.1f' % math.degrees(a['ang'])
        if ob['pivot'][:2] != (0, 0):
            line += ' pivot(%g,%g)' % ob['pivot'][:2]
        if ob['uv'] != (0, 0, 1, 1):
            line += ' uv(%.3f,%.3f,%.3f,%.3f)' % ob['uv']
        if ob.get('flags', 0) & FLAG_MODE_HIDDEN:
            line += ' MODE-HIDDEN'
        if 'text' in ob:
            line += ' text=%r' % ob['text']
        print(line)


# ------------------------------------------------------------------ TPK

def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def dxt_colour(src, o, w, h, out, bx, by, punch):
    c0, c1, bits = struct.unpack_from('<HHI', src, o)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or not punch:
        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1))
        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
        pal = (p0 + (255,), p1 + (255,), p2 + (255,), p3 + (255,))
    else:
        p2 = tuple((a + b) // 2 for a, b in zip(p0, p1))
        pal = (p0 + (255,), p1 + (255,), p2 + (255,), (0, 0, 0, 0))
    for py in range(4):
        for px in range(4):
            x, y = bx + px, by + py
            if x < w and y < h:
                out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(pal[(bits >> (2 * (py * 4 + px))) & 3])


def decode_record(d, rec, dbase):
    off, paloff = struct.unpack_from('<II', d, rec + 0x24)
    palsz = struct.unpack_from('<I', d, rec + 0x30)[0]
    w, h = struct.unpack_from('<HH', d, rec + 0x38)
    fmt = d[rec + 0x3e]
    out = bytearray(w * h * 4)
    if fmt == 0x08 and palsz >= 1024:
        pal = d[dbase + paloff:dbase + paloff + 1024]
        ix = d[dbase + off:dbase + off + w * h]
        for p in range(w * h):
            out[p * 4:p * 4 + 4] = pal[ix[p] * 4:ix[p] * 4 + 4]
    elif fmt == 0x20:
        px = d[dbase + off:dbase + off + w * h * 4]
        for p in range(w * h):
            b, g, r, a = px[p * 4:p * 4 + 4]
            out[p * 4:p * 4 + 4] = bytes((r, g, b, a))
    elif fmt in (0x22, 0x24, 0x26):
        o = dbase + off
        for by in range(0, h, 4):
            for bx in range(0, w, 4):
                if fmt == 0x22:
                    dxt_colour(d, o, w, h, out, bx, by, True)
                    o += 8
                    continue
                dxt_colour(d, o + 8, w, h, out, bx, by, False)
                if fmt == 0x24:
                    for n in range(16):
                        x, y = bx + (n & 3), by + (n >> 2)
                        if x < w and y < h:
                            out[(y * w + x) * 4 + 3] = ((d[o + n // 2] >> ((n & 1) * 4)) & 15) * 17
                else:
                    a0, a1 = d[o], d[o + 1]
                    bits = int.from_bytes(d[o + 2:o + 8], 'little')
                    if a0 > a1:
                        al = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
                    else:
                        al = [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]
                    for n in range(16):
                        x, y = bx + (n & 3), by + (n >> 2)
                        if x < w and y < h:
                            out[(y * w + x) * 4 + 3] = al[(bits >> (3 * n)) & 7]
                o += 16
    else:
        return None
    return w, h, bytes(out), fmt


def tpk_textures(d):
    """{key: (name offset, pixel base, buffer)} for every TPK in a bundle.

    Plain TPKs list 0x7c-byte records in sub-chunk 0x33310004 and keep pixels
    in the following 0x33320002 chunk. Compressed TPKs (HUD_CustomTextures_*)
    instead carry an index 0x33310003 of {key, offset, packed, unpacked, ...};
    each entry is one JDLZ blob = pixel data followed by that texture's own
    0x7c record (+0x20 trailer), so the blob is its own pixel base.
    """
    found = {}
    i = 0
    while True:
        a = d.find(b'\x00\x00\x31\xb3', i)
        if a < 0:
            break
        hbeg, hsize = a + 8, struct.unpack_from('<I', d, a + 4)[0]
        i = hbeg + hsize
        subs = list(chunks(d, hbeg, hbeg + hsize))
        for t, o, sz in subs:
            if t != b'\x03\x0013':
                continue
            for e in range(o, o + sz - 0x17, 0x18):
                key, off = struct.unpack_from('<II', d, e)
                # offsets are relative to the TPK container (header - 8)
                for base in (a - 8, 0):
                    magic = d[base + off:base + off + 4]
                    if magic in (b'JDLZ', b'HUFF'):
                        blob = bytearray((jdlz if magic == b'JDLZ' else huff)(d[base + off:]))
                        j = len(blob) - 0x9c + 0x0c
                        # pixels sit at blob start; keep the palette's
                        # distance from them (P8 records)
                        po, pp = struct.unpack_from('<II', blob, j + 0x24)
                        struct.pack_into('<II', blob, j + 0x24, 0, (pp - po) & 0xffffffff)
                        found.setdefault(key, (j, 0, bytes(blob)))
                        break
        p = d.find(b'\x02\x0023', i)
        tables = [(o, sz) for t, o, sz in subs if t == b'\x04\x0013']
        if p < 0 or not tables:
            continue
        dbase = p + 8
        while dbase < len(d) and d[dbase] == 0x11:
            dbase += 1
        # record table: 0x7c-byte rows, name at +0x0c
        for o, sz in tables:
            for r in range(o, o + sz - 0x7b, 0x7c):
                j = r + 0x0c          # decode_record offsets are name-relative
                found.setdefault(struct.unpack_from('<I', d, j + 0x18)[0], (j, dbase, d))
    return found


# ------------------------------------------------------------------ fonts

def load_fonts(langdir):
    """{font key: (glyphs, line height, atlas RGBA image)} from
    LANGUAGES/English.bin (FNTF glyph tables, chunk 0x00030201) and
    LANGUAGES/LanguageTextures.bin (atlases). The key is the lower-cased
    resource name without extension, so 'Font_ConduitMdITCTT38bi.ffn' and the
    chunk name 'font_conduitmditctt38bi' meet.

    FNTF (at +0x200 of the chunk): +0x0a u16 glyph count, +0x12 u16 line
    height, +0x14 u32 glyph table offset (FNTF-relative). Glyph = 16 bytes:
    u16 char, u8 w, u8 h, u16 u, u16 v, u8 -, s8 x offset, u8 y offset,
    u8 -, u16 -, u16 advance (atlas pixels = 640x480 FEng units)."""
    from PIL import Image
    d = open(os.path.join(langdir, 'English.bin'), 'rb').read()
    tex = tpk_textures(open(os.path.join(langdir, 'LanguageTextures.bin'), 'rb').read())
    fonts = {}
    for t, o, sz in chunks(d, 0, len(d)):
        if struct.unpack('<I', t)[0] != 0x30201:
            continue
        name = d[o:o + 0x40].split(b'\0', 1)[0].decode('latin1').lower()
        f = o + 0x200
        if d[f:f + 4] != b'FNTF':
            continue
        count, = struct.unpack_from('<H', d, f + 0x0a)
        line, = struct.unpack_from('<H', d, f + 0x12)
        table, = struct.unpack_from('<I', d, f + 0x14)
        glyphs = {}
        for i in range(count):
            c, w, h, u, v, _, xo, yo, _, _, adv = struct.unpack_from('<HBBHHBbBBHH', d, f + table + i * 16)
            glyphs[c] = (w, h, u, v, xo, yo, adv)
        k = bhash(name.upper())
        if k not in tex:
            continue
        rec, dbase, buf = tex[k]
        r = decode_record(buf, rec, dbase)
        if r:
            fonts[name] = (glyphs, line, Image.frombytes('RGBA', (r[0], r[1]), r[2]))
    return fonts


def text_width(font, txt):
    glyphs = font[0]
    return sum(glyphs[ord(c)][6] for c in txt if ord(c) in glyphs)


def draw_text(img, font, txt, x, y, scale, just, rgba):
    """FEng string: (x, y) is the anchor; Sj bits 1 = h-centre, 2 = right,
    4 = v-centre, 8 = bottom (otherwise left / top)."""
    from PIL import Image, ImageChops
    glyphs, line, atlas = font
    w = text_width(font, txt) * scale
    if just & 1:
        x -= w / 2
    elif just & 2:
        x -= w
    if just & 4:
        y -= line * scale / 2
    elif just & 8:
        y -= line * scale
    tint = None
    for c in txt:
        g = glyphs.get(ord(c))
        if not g:
            continue
        gw, gh, u, v, xo, yo, adv = g
        if gw and gh:
            spr = atlas.crop((u, v, u + gw, v + gh))
            spr = spr.resize((max(1, round(gw * scale)), max(1, round(gh * scale))), Image.BILINEAR)
            tint = Image.new('RGBA', spr.size, rgba)
            _paste_clipped(img, ImageChops.multiply(spr, tint), x + xo * scale, y + yo * scale)
        x += adv * scale


# ------------------------------------------------------------------ preview

def render(objs, texbuns, out_path, show_all, style=0, fonts=None):
    from PIL import Image, ImageDraw, ImageFont
    lib = {}
    for path in texbuns:
        d = open(path, 'rb').read()
        for k, v in tpk_textures(d).items():
            lib.setdefault(k, v)
    cache = {}

    def tex(name):
        k = tex_key(name)
        if k not in lib:
            # player-selectable gauge art: HUD_CustomTextures_NN.bin names it
            # <NAME>_CUSTOM_NN, the package names it plain
            k = bhash('%s_CUSTOM_%02d' % (name.rsplit('.', 1)[0].upper(), style))
        if k not in cache:
            cache[k] = None
            if k in lib:
                rec, dbase, d = lib[k]
                r = decode_record(d, rec, dbase)
                if r:
                    w, h, px, _ = r
                    # +0x4a on HUD art: 1 normal, 2 alpha-mask shapes (also
                    # drawn as themselves: the minimap disc, the N2O track),
                    # 6 additive (Tach_NOS, opaque white-on-black)
                    cache[k] = (Image.frombytes('RGBA', (w, h), px), d[rec + 0x4a] == 6)
        return cache[k]

    W, H = 640, 480
    img = Image.new('RGBA', (W, H), (70, 78, 90, 255))
    dr = ImageDraw.Draw(img)
    for y in range(0, H, 40):          # faint grid so placement is readable
        dr.line([(0, y), (W, y)], fill=(80, 88, 100, 255))
    for x in range(0, W, 40):
        dr.line([(x, 0), (x, H)], fill=(80, 88, 100, 255))
    missing = set()
    # painter's order: farthest (largest z) first, list order breaks ties
    order = sorted(objs, key=lambda o: (-o['abs']['z'], o['order']))
    for ob in order:
        a = ob['abs']
        if ob.get('type') == 5 or ob['rgba'][3] == 0:
            continue
        if a['mode_hidden'] and not show_all:
            continue
        cx, cy = a['x'] + W / 2, a['y'] + H / 2
        r, g, b, al = ob['rgba']
        if ob.get('type') == 2 and fonts:
            key = ob.get('font', '').rsplit('.', 1)[0].lower()
            font = fonts.get(key)
            if font:
                draw_text(img, font, ob.get('text', ''), cx, cy, ob['size'][0],
                          ob.get('just', 0), (r, g, b, al))
                continue
        if ob.get('type') == 2:
            txt = ob.get('text', '')
            sz = max(8, int(18 * ob['size'][1]))
            try:
                font = ImageFont.truetype('arialbi.ttf', sz)
            except OSError:
                font = ImageFont.load_default()
            dr.text((cx, cy), txt, fill=(r, g, b, al), font=font, anchor='mm')
            continue
        name = ob.get('resname')
        if not name or name.lower() in STENCIL_ONLY:
            continue
        sw, sh = abs(ob['size'][0]), abs(ob['size'][1])
        if sw < 1 or sh < 1:
            continue
        t = tex(name) if not name.startswith('(') else None
        if t is None:
            if name.lower().startswith('white16x16') or name.startswith('('):
                t = (Image.new('RGBA', (16, 16), (255, 255, 255, 255)), False)
            else:
                missing.add(name)
                continue
        t, additive = t
        u0, v0, u1, v1 = ob['uv']
        tw, th = t.size
        crop = t.crop((int(min(u0, u1) * tw), int(min(v0, v1) * th),
                       max(1, int(max(u0, u1) * tw)), max(1, int(max(v0, v1) * th))))
        if u0 > u1:
            crop = crop.transpose(Image.FLIP_LEFT_RIGHT)
        if v0 > v1:
            crop = crop.transpose(Image.FLIP_TOP_BOTTOM)
        crop = crop.resize((max(1, int(round(sw))), max(1, int(round(sh)))), Image.BILINEAR)
        tint = Image.new('RGBA', crop.size, (r, g, b, al))
        crop = _multiply(crop, tint)
        ang = a['ang']
        px, py = ob['pivot'][0], ob['pivot'][1]
        if ang:
            # rotate the sprite about (centre + pivot); y is down on screen
            deg = -math.degrees(ang)
            big = Image.new('RGBA', (int(sw + 2 * abs(px)) * 2 + 4, int(sh + 2 * abs(py)) * 2 + 4))
            bx = big.size[0] // 2 - int(px) - crop.size[0] // 2
            by = big.size[1] // 2 - int(py) - crop.size[1] // 2
            big.paste(crop, (bx, by))
            crop = big.rotate(deg, resample=Image.BICUBIC)
            ox, oy = cx + px - crop.size[0] / 2, cy + py - crop.size[1] / 2
        else:
            ox, oy = cx - crop.size[0] / 2, cy - crop.size[1] / 2
        _paste_clipped(img, crop, ox, oy, additive)
    img.convert('RGB').save(out_path)
    if missing:
        print('textures not found in given bundles:', ', '.join(sorted(missing)))


def _multiply(src, tint):
    from PIL import ImageChops
    return ImageChops.multiply(src, tint)


def _paste_clipped(img, spr, ox, oy, additive=False):
    ox, oy = int(round(ox)), int(round(oy))
    W, H = img.size
    x0, y0 = max(0, ox), max(0, oy)
    x1, y1 = min(W, ox + spr.size[0]), min(H, oy + spr.size[1])
    if x1 <= x0 or y1 <= y0:
        return
    spr = spr.crop((x0 - ox, y0 - oy, x1 - ox, y1 - oy))
    if not additive:
        img.alpha_composite(spr, (x0, y0))
        return
    from PIL import Image, ImageChops
    a = spr.getchannel('A')
    add = Image.merge('RGB', [ImageChops.multiply(c, a) for c in spr.split()[:3]])
    box = (x0, y0, x1, y1)
    img.paste(ImageChops.add(img.crop(box).convert('RGB'), add).convert('RGBA'), box)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    bun = open(argv[1], 'rb').read()
    pkgs = list_packages(bun)
    if len(argv) == 2:
        for name, body in pkgs:
            print('%-28s %d bytes' % (name, len(body)))
        return 0
    want = argv[2].lower()
    body = next((b for n, b in pkgs if n.lower() == want), None)
    if body is None:
        print('no package %s; have: %s' % (argv[2], ', '.join(n for n, _ in pkgs)))
        return 1
    res, objs = parse_package(body)
    if '--png' not in argv:
        dump(res, objs)
        return 0
    out = argv[argv.index('--png') + 1]
    texbuns = [argv[1]] + [argv[i + 1] for i, a in enumerate(argv) if a == '--tex']
    style = int(argv[argv.index('--style') + 1]) if '--style' in argv else 0
    fonts = load_fonts(argv[argv.index('--fonts') + 1]) if '--fonts' in argv else None
    render(objs, texbuns, out, '--all' in argv, style, fonts)
    print('wrote', out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
