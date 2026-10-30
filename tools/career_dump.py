"""Dump the NFSU2 career tables: GLOBALB.BUN container 0x80034a10 ("GCareer",
PC loader 0x512c00 / GC LoaderMostWantedCareerChunk 80123520).

usage: python career_dump.py [GLOBALB.BUN] [table ...]
Field layouts: E:/re-tools/out/CAREER.md."""
import struct, sys

GLOBALB = "E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN"

# chunk id -> (name, record size or 0 = not a plain array); sizes from the GC Create*/Load* loaders
TABLES = {
    0x34a11: ("events", 0x88), 0x34a12: ("shops", 0xa0), 0x34a14: ("brands", 0x44),
    0x34a15: ("perf_packages", 0x17c), 0x34a16: ("showcases", 0x40), 0x34a17: ("sms", 0x14),
    0x34a18: ("stages", 0x50), 0x34a19: ("sponsors", 0x10), 0x34a1a: ("tuning", 0x18),
    0x34a1b: ("world_challenges", 0x18), 0x34a1c: ("unlocks", 0x28), 0x34a1d: ("strings", 0),
    0x34a1e: ("banks", 0xc), 0x34a1f: ("car_unlocks", 0xc),
}
EVENT_CLASS = {0: "tutorial", 1: "sponsor", 2: "URL", 3: "world"}     # +0x34
RACE_TYPE = {0: "circuit", 1: "sprint", 2: "streetx", 3: "?3", 4: "drag", 5: "drift"}   # +0xf
SHOP_TYPE = {0: "crib", 1: "graphics", 2: "body", 3: "perf", 4: "carlot", 5: "specialty", 6: "uniques"}


def u8(b, o): return b[o]
def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def s16(b, o): return struct.unpack_from("<h", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def f32(b, o): return struct.unpack_from("<f", b, o)[0]


def bhash(s):
    h = 0xFFFFFFFF
    for c in s.encode("latin1"):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def containers(d):
    """every 0x80034a10 container: {chunk id: payload bytes}"""
    out = []
    i = 0
    while i + 8 <= len(d):
        cid, sz = struct.unpack_from("<II", d, i)
        if cid == 0x80034A10:
            subs, j = {}, i + 8
            while j + 8 <= i + 8 + sz:
                sid, ssz = struct.unpack_from("<II", d, j)
                subs.setdefault(sid, d[j + 8:j + 8 + ssz])
                j += 8 + ssz
            out.append(subs)
            i += 8 + sz
            continue
        if cid & 0x80000000:          # other containers: walk inside
            i += 8
            continue
        i += 8 + sz
    return out


class Career:
    def __init__(self, path=GLOBALB, which=0):
        d = open(path, "rb").read()
        self.all = containers(d)
        self.c = self.all[which]
        self.strtab = self.c.get(0x34a1d, b"")

    def s(self, off):
        """a string in the career string table (0x34a1d)"""
        if off >= len(self.strtab):
            return "?%x" % off
        e = self.strtab.index(b"\0", off)
        return self.strtab[off:e].decode("latin1")

    def cstr(self, b):
        return b.split(bytes(1))[0].decode("latin1")

    def records(self, cid):
        b = self.c.get(cid, b"")
        n = TABLES[cid][1]
        return [b[k:k + n] for k in range(0, len(b) - n + 1, n)] if n else []


def dump_events(C):
    for r in C.records(0x34a11):
        tr = " ".join("%d%s/%d" % (s16(r, 0x18 + 4 * k), "r" if r[0x1a + 4 * k] & 1 else "", r[0x1b + 4 * k])
                      for k in range(r[0x7e]))
        opp = " ".join("%s(%08x m%d l%d s%d.%d.%d b%d)" % ((C.s(u16(r, o)), u32(r, o + 4)) + tuple(r[o + 2:o + 4]) + tuple(r[o + 8:o + 12]))
                       for o in (0x40 + 12 * k for k in range(r[0x7c])))
        print("%-22s %08x stg%d %-8s %-7s%s%s unl%d:%08x $%d zone %s/%08x tracks[%s] t%d x14=%d x36=%d  %s%s%s" % (
            C.s(u16(r, 0)), u32(r, 8), r[0x37], EVENT_CLASS.get(r[0x34], r[0x34]), RACE_TYPE.get(r[0xf], r[0xf]),
            " SUV" if r[0xd] else "", " hidden" if r[0x7f] else "", r[0xc], u32(r, 0x10), u32(r, 0x30),
            C.s(u16(r, 6)), u32(r, 0x28), tr, r[0x7d], u32(r, 0x14), r[0x36], opp,
            " pre=" + C.s(u16(r, 2)) if u16(r, 2) else "", " post=" + C.s(u16(r, 4)) if u16(r, 4) else ""))


def dump_shops(C):
    for r in C.records(0x34a12):
        print("%-20s %08x %-10s %-16s stg%d enter%d unl%d:%08x" % (C.cstr(r[:0x38]), u32(r, 0x38),
              SHOP_TYPE.get(r[0x50], r[0x50]), C.cstr(r[0x40:0x50]), r[0x9d], r[0x51], r[0x9c], u32(r, 0x74)))


def dump_stages(C):
    for r in C.records(0x34a18):
        print("stage %d sponsors %d [%s] x2=%d x4=%d x6=%d final %08x pile %s maxhidden %d x42=%d x44=%g x48=%g x4c=%g" % (
            r[0], r[1], " ".join("%08x" % u32(r, 8 + 4 * k) for k in range(5)), u16(r, 2), u16(r, 4), u16(r, 6),
            u32(r, 0x28), r[0x2c:0x41].hex(), s16(bytes([r[0x41], 0xff if r[0x41] & 0x80 else 0]), 0),
            s16(r, 0x42), f32(r, 0x44), f32(r, 0x48), f32(r, 0x4c)))


def dump_misc(C):
    for r in C.records(0x34a19):
        print("sponsor %-16s %08x x2=%d b4=%s xc=%d xe=%d" % (C.s(u16(r, 0)), u32(r, 8), u16(r, 2), r[4:8].hex(), u16(r, 0xc), u16(r, 0xe)))
    for r in C.records(0x34a16):
        print("showcase %-24s %08x stg%d x24=%d x26=%d zone %08x %08x req%d:%08x x35=%d x3c=%g" % (
            C.cstr(r[:0x20]), u32(r, 0x20), r[0x25], r[0x24], u16(r, 0x26), u32(r, 0x28), u32(r, 0x2c), r[0x34], u32(r, 0x38), r[0x35], f32(r, 0x3c)))
    for r in C.records(0x34a1b):
        print("challenge %-22s at %-30s stg%d %s goal %08x/%08x time %d %s" % (C.s(u16(r, 0)), C.s(u16(r, 2)), s16(r, 4), r[6:8].hex(),
              u32(r, 8), u32(r, 0xc), u32(r, 0x10), r[0x14:0x18].hex()))
    for r in C.records(0x34a17):
        print("sms %-28s trig%2d b3=%d %s args %08x %08x from %08x" % (C.s(u16(r, 0)), r[2], r[3], r[4:8].hex(), u32(r, 8), u32(r, 0xc), u32(r, 0x10)))
    for r in C.records(0x34a1e):
        print("bank $%d b2=%d #%d x4=%d zone %08x" % (u16(r, 0), r[2], r[3], u32(r, 4), u32(r, 8)))
    for r in C.records(0x34a1f):
        print("car_unlock %08x %08x %08x" % (u32(r, 0), u32(r, 4), u32(r, 8)))


PATHS = "E:/NFS Underground 2 Vita/data/nfsug2/TRACKS/ROUTESL4RA/PathsFreeRoam.bin"


def zones(path=PATHS):
    """PathsFreeRoam.bin chunk 0x3414a (inside container 0x80034147): {name hash: [(type, x, y)]}.
    Record: +0 type, +4/+8 position, +0x30 name hash, +0x40 u16 points, +0x42 u16 record size.
    Career types: 13 event / showcase / challenge start, 14 shop, 18 SMS, 19 bank."""
    d = open(path, "rb").read()
    o, e, out = 16, 16 + u32(d, 12), {}
    while o < e and u16(d, o + 0x42):
        out.setdefault(u32(d, o + 0x30), []).append((u32(d, o), f32(d, o + 4), f32(d, o + 8)))
        o += u16(d, o + 0x42)
    return out


def dump_zones(C):
    Z = zones()
    def at(h):
        z = Z.get(h)
        return "t%d (%.0f %.0f)" % z[0] if z else "no zone"
    for r in C.records(0x34a11):
        print("event    %-22s %s" % (C.s(u16(r, 0)), at(u32(r, 0x28))))
    for r in C.records(0x34a12):
        print("shop     %-22s %s" % (C.cstr(r[:0x38]), at(u32(r, 0x3c))))
    for r in C.records(0x34a16):
        print("showcase %-22s %s" % (C.cstr(r[:0x20]), at(u32(r, 0x2c))))
    for r in C.records(0x34a1b):
        print("challenge %-21s %s" % (C.s(u16(r, 0)), at(bhash(C.s(u16(r, 2))))))
    for r in C.records(0x34a1e):
        print("bank     #%-21d %s" % (r[3], at(u32(r, 8))))


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].endswith(".BUN") else GLOBALB
    what = [a for a in sys.argv[1:] if not a.endswith(".BUN")] or ["summary"]
    C = Career(path)
    if "summary" in what:
        print("%d GCareer containers" % len(C.all))
        for k, c in enumerate(C.all):
            print(" container %d: " % k + ", ".join("%x:%d" % (cid, len(b)) for cid, b in sorted(c.items())))
        for cid, (nm, n) in sorted(TABLES.items()):
            if n:
                print(" %-16s %4d records" % (nm, len(C.c.get(cid, b"")) // n))
    for w, f in (("events", dump_events), ("shops", dump_shops), ("stages", dump_stages), ("misc", dump_misc), ("zones", dump_zones)):
        if w in what:
            f(C)
