"""Print the per-voice parameter tables out of a TruVoice language DLL.

Usage: python tools/voicedump.py [TruVoice/CGRM_EN.DLL ...]
       python tools/voicedump.py --compare A.DLL B.DLL

--compare prints only what differs between two DLLs' voice blocks.  The block
has the same layout in all of them -- the same tables in the same order at the
same offsets from the adjustment table -- which is what makes comparing them
field by field meaningful, and it is how the 1995 engines were found to be
carrying the 1997 voice data very nearly unchanged.

The tables themselves are the original's data and are not in this repository
(see docs/VOICES.md for what the columns mean); this reads them from your own
copy of the binaries, the way tools/gen_data.py does for the build.

The tables sit at a different address in every language DLL, so rather than
hard-coding one they are found by signature: the voice adjustment table is a
run of `voices` x 15 int32 whose first row is all zero in the seven
percentage columns -- the base voice, which every other voice is expressed
as a deviation from -- and whose rows 7..9 are the resonator constants, in a
narrow range.  In every DLL shipped with TruVoice that matches exactly once.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe32 import Image

ADJ_COLS = ["F1%", "B1%", "F2%", "B2%", "F3%", "B3%", "F4%",
            "a7", "a8", "a9", "a10", "a11", "p2+", "p18", "p19"]

#: The tables after the adjustment table, in the order they are laid out:
#: (name, element size).  docs/VOICES.md says what each one does.
AFTER_ADJ = [("breath", 4), ("nasal_rate", 4), ("p18", 1), ("p19", 1),
             ("p20", 1), ("p21", 1), ("pitch", 4), ("rate_index", 4),
             ("speed", 4), ("f4", 4), ("f4max", 4), ("voice_c", 4),
             ("pitch_scale", 4)]


def voice_names(mem, base, relocs, text):
    """The speaker names, in voice order.

    Each is an ANSI string followed by the same name in UTF-16LE -- the pair
    the SAPI mode-info block is filled from -- but they do not sit in voice
    order.  The compiler emitted the literals for every name after the first
    in the reverse of the order the engine registers them, so the block
    reads Peter, Julia, Wanda, ... when the voices are Peter, Sidney, Eager
    Eddie, ...  The registration code is the authority, so the order comes
    from the sequence in which it refers to them.
    """
    pat = rb"([\x20-\x7e]{3,20})\x00{1,4}((?:[\x20-\x7e]\x00){3,20})\x00\x00"
    byaddr = {}
    for m in re.finditer(pat, mem):
        ansi = m.group(1).decode("latin1")
        wide = m.group(2).decode("utf-16-le")
        if wide and (ansi == wide or ansi.split()[-1] == wide):
            byaddr[m.start() + base] = ansi
    if not byaddr:
        return []
    seen, out = set(), []
    for r in sorted(relocs):
        if not (text.va <= r < text.va + text.vsize):
            continue
        t = struct.unpack_from("<I", mem, r - base)[0]
        if t in byaddr and t not in seen:
            seen.add(t)
            out.append((t, byaddr[t]))
    if len(out) != len(byaddr):
        print("warning: %d names but %d registered; falling back to the order"
              " they are stored in" % (len(byaddr), len(out)))
        out = sorted(byaddr.items())
    return out


def find_adjust(mem, base, voices):
    hits = []
    for off in range(0, len(mem) - voices * 60, 4):
        rows = [struct.unpack_from("<15i", mem, off + v * 60)
                for v in range(voices)]
        if any(rows[0][:7]):
            continue
        ok = all(all(-100 <= x <= 100 for x in r[:7])
                 and 200 <= r[7] <= 900 and 2000 <= r[8] <= 8000
                 and 50 <= r[9] <= 800 for r in rows)
        if ok:
            hits.append(off + base)
    return hits


def scalar_table(mem, base, start, voices, lo, hi, limit=0x400):
    """The first run of `voices` int32 after `start` that all fall in
    [lo, hi].  The tables after the adjustment block are laid out back to
    back, but their length follows the voice count, which differs between
    languages, so they are probed rather than assumed."""
    for off in range(start, start + limit, 4):
        v = struct.unpack_from("<%di" % voices, mem, off - base)
        if all(lo <= x <= hi for x in v):
            return off, list(v)
    return None, None


def block_layout(voices):
    """(offset from the adjustment table, name, element size), every table.

    The four byte-wide tables are each padded out to a 16-byte boundary; the
    int32 ones sit back to back.  With ten voices that puts the last table,
    pitch_scale, at +0x3d8, which is where both CGRM_EN and CGRM_ES have it.
    """
    out = [(0, "adjust", 4)]
    off = voices * 15 * 4
    for name, size in AFTER_ADJ:
        out.append((off, name, size))
        n = voices * size
        off += n if size == 4 else (n + 15) & ~15
    return out


def read_block(img, adj, voices):
    """Every per-voice value, keyed (table, voice, column)."""
    out = {}
    for off, name, size in block_layout(voices):
        cols = 15 if name == "adjust" else 1
        for v in range(voices):
            for c in range(cols):
                at = adj + off + (v * cols + c) * size
                out[(name, v, c)] = img.u8(at) if size == 1 else img.s32(at)
    return out


def load(path):
    """(name, adjustment table address, voice count, speakers, values)."""
    img = Image(path)
    mem, base = bytes(img.mem), img.base
    text = [sec for sec in img.sections if sec.name == ".text"][0]
    names = [n for _, n in voice_names(mem, base, img.relocs, text)]
    voices = len(names) or 10
    hits = find_adjust(mem, base, voices)
    if not hits:
        sys.exit("voicedump: no voice adjustment table in %s" % path)
    return (os.path.basename(path), hits[0], voices, names,
            read_block(img, hits[0], voices))


def compare(paths):
    """Print what differs between two DLLs' voice blocks."""
    (na, aa, nv, nma, a), (nb, ab, mv, nmb, b) = [load(p) for p in paths]
    if nv != mv:
        sys.exit("voicedump: %s has %d voices, %s has %d"
                 % (na, nv, nb, mv))
    print("%s @ 0x%08x   vs   %s @ 0x%08x" % (na, aa, nb, ab))
    print()
    diff = [k for k in a if a[k] != b[k]]
    for off, name, size in block_layout(nv):
        n = sum(1 for k in diff if k[0] == name)
        print("%-12s %3d values  %s"
              % (name, nv * (15 if name == "adjust" else 1),
                 "identical" if not n else "%d DIFFER" % n))
    print()
    print("%d of %d per-voice values identical" % (len(a) - len(diff), len(a)))
    if not diff:
        return
    print()
    print("the differences, %s then %s:" % (na, nb))
    for name, v, c in sorted(diff, key=lambda k: (k[1], k[0], k[2])):
        what = ADJ_COLS[c] if name == "adjust" else name
        who = "%s / %s" % (nma[v] if v < len(nma) else v,
                           nmb[v] if v < len(nmb) else v)
        print("   %-26s %-8s %6d  %6d"
              % (who, what, a[(name, v, c)], b[(name, v, c)]))


def dump(path):
    img = Image(path)
    mem, base = bytes(img.mem), img.base
    text = [sec for sec in img.sections if sec.name == ".text"][0]
    names = voice_names(mem, base, img.relocs, text)
    print("== %s (image base 0x%x) ==" % (os.path.basename(path), base))
    if names:
        print("speakers @ 0x%x: %s"
              % (names[0][0], ", ".join("%d=%s" % (i, n)
                                        for i, (_, n) in enumerate(names))))
    voices = len(names) or 10

    hits = find_adjust(mem, base, voices)
    if not hits:
        print("voice adjustment table: not found for %d voices" % voices)
        return
    if len(hits) > 1:
        print("warning: %d candidate tables, using the first" % len(hits))
    adj = hits[0]
    print()
    print("voice adjustment table @ 0x%x (%d voices x 15 int32)" % (adj, voices))
    print("%-14s%s" % ("", "".join("%6s" % c for c in ADJ_COLS)))
    for v in range(voices):
        row = struct.unpack_from("<15i", mem, adj + v * 60 - base)
        nm = names[v][1] if v < len(names) else str(v)
        print("%-14s%s" % (nm[:13], "".join("%6d" % x for x in row)))

    after = adj + voices * 15 * 4
    print()
    for label, lo, hi in [("breath", -100, 100), ("pitch", 40, 260),
                          ("speed", 80, 400)]:
        off, vals = scalar_table(mem, base, after, voices, lo, hi)
        if vals:
            print("%-8s @ 0x%x  %s" % (label, off, vals))
            after = off + voices * 4
        else:
            print("%-8s not located" % label)


def main():
    paths = sys.argv[1:]
    if paths[:1] == ["--compare"]:
        if len(paths) != 3:
            sys.exit("usage: voicedump.py --compare A.DLL B.DLL")
        compare(paths[1:])
        return
    if not paths:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        paths = sorted(f for f in
                       (os.path.join(root, "TruVoice", n)
                        for n in os.listdir(os.path.join(root, "TruVoice")))
                       if f.upper().endswith(".DLL") and "CGRM_" in f.upper())
    for i, p in enumerate(paths):
        if i:
            print()
        dump(p)


if __name__ == "__main__":
    main()
