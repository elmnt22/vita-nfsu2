#!/usr/bin/env python
"""Build the trophy pack: vita/sce_sys/trophy/NFSU20001_00/TROPHY.TRP

    python tools/trophy_trp.py [--out PATH] [--icons DIR]

An unsigned, unencrypted pack for Rinnegatamante's NoTrpDrm plugin (the same
container Vita TRP Builder writes): a 0x40 header (magic DCA24D00, version 2,
file size, entry count, entry size 0x40, SHA-1 of the file with the hash
field zeroed), 0x40-byte entries (name[32], offset u64, size u64, flags),
then the files on 16-byte boundaries. Big endian.

Files: TROPCONF.SFM + TROP.SFM (XML), ICON0.PNG (320x176), TROP000.PNG ..
TROPnnn.PNG (240x240). Trophy 000 is the platinum; trophy n (1..76) is
tools/trophies.json entry n-1, the order of src/trophy.c k_info (the game
unlocks pack id i + 1 for our trophy i, vita/sdl_vita.c).

Grades follow the RA points: 25+ gold, 10 silver, below that bronze.
Icons are drawn from the bubble/boot art the VPK already carries
(vita/sce_sys/pic0.png) with the trophy name on top.
"""
import argparse, hashlib, io, json, os, struct, sys
from xml.sax.saxutils import escape

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMID = "NFSU20001_00"       # the title id (vita/build_vita.sh TITLEID) + _00
TITLE = "Need for Speed Underground 2"
DETAIL = ("Trophies modelled on the RetroAchievements set by BigWeedSmokerMan.")
SIG = "<!--Sce-Np-Trophy-Signature: " + "x" * 320 + "-->\n"


def grade(points):
    return "G" if points >= 25 else "S" if points >= 10 else "B"


def sfm(trophies, full):
    out = [SIG, '<trophyconf version="1.1" platform="psp2" policy="large">\n',
           " <npcommid>%s</npcommid>\n" % COMMID,
           " <trophyset-version>01.00</trophyset-version>\n",
           ' <parental-level license-area="default">0</parental-level>\n']
    if full:
        out.append(" <title-name>%s</title-name>\n" % escape(TITLE))
        out.append(" <title-detail>%s</title-detail>\n" % escape(DETAIL))
    rows = [("000", "P", "-1", "Platinum Trophy", "Earn every other trophy.")]
    for n, t in enumerate(trophies, 1):
        rows.append(("%03d" % n, grade(t["points"]), "000", t["title"], t["desc"]))
    for tid, tt, pid, name, desc in rows:
        head = ' <trophy id="%s" hidden="no" ttype="%s" pid="%s"' % (tid, tt, pid)
        if full:
            out.append(head + ">\n  <name>%s</name>\n  <detail>%s</detail>\n </trophy>\n"
                       % (escape(name), escape(desc)))
        else:
            out.append(head + "/>\n")
    out.append("</trophyconf>\n")
    return "".join(out).encode("utf-8")


def icons(trophies, icon_dir):
    """{name: png bytes}. A ready-made icon in icon_dir (TROPnnn.PNG / ICON0.PNG) wins."""
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    art = Image.open(os.path.join(ROOT, "vita/sce_sys/pic0.png")).convert("RGB")   # 960x544

    def font(px):
        for f in ("C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/arialbd.ttf",
                  "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
            if os.path.exists(f):
                return ImageFont.truetype(f, px)
        return ImageFont.load_default()

    def png(im):   # 256-colour PNG: a third of the size, no visible loss at 240 px
        b = io.BytesIO()
        im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(b, "PNG", optimize=True)
        return b.getvalue()

    def own(name):
        p = icon_dir and os.path.join(icon_dir, name)
        return open(p, "rb").read() if p and os.path.exists(p) else None

    out = {}
    out["ICON0.PNG"] = own("ICON0.PNG") or png(art.crop((0, 8, 960, 536)).resize((320, 176), Image.LANCZOS))
    # crops of the boot art: logo, wheel, skyline, fender
    crops = [(232, 120, 712, 600), (100, 280, 540, 544 + 176), (600, 150, 960, 510), (0, 0, 420, 420)]
    ring = {"P": (150, 200, 230), "G": (222, 180, 70), "S": (200, 206, 214), "B": (190, 120, 70)}
    rows = [("Platinum Trophy", "P")] + [(t["title"], grade(t["points"])) for t in trophies]
    for n, (title, g) in enumerate(rows):
        name = "TROP%03d.PNG" % n
        b = own(name)
        if b:
            out[name] = b; continue
        x0, y0, x1, y1 = crops[0 if n == 0 else n % len(crops)]
        bg = art.crop((x0, y0, min(x1, 960), min(y1, 544))).resize((240, 240), Image.LANCZOS)
        bg = bg.filter(ImageFilter.GaussianBlur(1.2))
        im = Image.new("RGB", (240, 240))
        im.paste(bg)
        d = ImageDraw.Draw(im, "RGBA")
        d.rectangle((0, 132, 240, 240), fill=(0, 0, 0, 170))
        d.rectangle((4, 4, 235, 235), outline=ring[g] + (255,), width=6)
        # the name, wrapped, shrunk until it fits under the art
        for px in (30, 26, 23, 20, 18):
            f = font(px); words = title.split(); lines = [""]
            for w in words:
                t = (lines[-1] + " " + w).strip()
                if d.textlength(t, font=f) <= 212: lines[-1] = t
                else: lines.append(w)
            if len(lines) * (px + 4) <= 92 and all(d.textlength(l, font=f) <= 212 for l in lines):
                break
        y = 138 + (92 - len(lines) * (px + 4)) // 2
        for l in lines:
            w = d.textlength(l, font=f)
            d.text(((240 - w) / 2, y), l, font=f, fill=(255, 255, 255, 255))
            y += px + 4
        out[name] = png(im)
    return out


def trp(files):
    """files: [(name, bytes)] -> the container"""
    n = len(files)
    off = 0x40 + n * 0x40
    ents, body = [], bytearray()
    for name, data in files:
        pad = (-off) % 16
        body += b"\0" * pad; off += pad
        ents.append(struct.pack(">32sQQI12x", name.encode(), off, len(data), 0))
        body += data; off += len(data)
    size = 0x40 + n * 0x40 + len(body)
    head = struct.pack(">IIQIII", 0xDCA24D00, 2, size, n, 0x40, 0) + b"\0" * 20 + b"\0" * 16
    blob = bytearray(head + b"".join(ents) + body)
    blob[0x1C:0x30] = hashlib.sha1(blob).digest()
    return bytes(blob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "vita/sce_sys/trophy", COMMID, "TROPHY.TRP"))
    ap.add_argument("--icons", default=os.path.join(ROOT, "img/trophies"),
                    help="optional folder of hand-made TROPnnn.PNG / ICON0.PNG that replace the drawn ones")
    a = ap.parse_args()
    trophies = json.load(open(os.path.join(ROOT, "tools/trophies.json"), encoding="utf-8"))["trophies"]
    if len(trophies) != 76:
        sys.exit("trophies.json: %d trophies, src/trophy.c has 76" % len(trophies))
    ic = icons(trophies, a.icons)
    files = [("TROPCONF.SFM", sfm(trophies, False)), ("TROP.SFM", sfm(trophies, True)), ("ICON0.PNG", ic["ICON0.PNG"])]
    files += [("TROP%03d.PNG" % i, ic["TROP%03d.PNG" % i]) for i in range(len(trophies) + 1)]
    blob = trp(files)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    open(a.out, "wb").write(blob)
    g = [grade(t["points"]) for t in trophies]
    print("trophy pack: %s (%d KB, %d trophies + platinum: %d gold, %d silver, %d bronze)"
          % (a.out, len(blob) // 1024, len(trophies), g.count("G"), g.count("S"), g.count("B")))


if __name__ == "__main__":
    main()
