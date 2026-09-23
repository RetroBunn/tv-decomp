"""Print the per-voice parameter tables out of a TruVoice language DLL.

Usage: python tools/voicedump.py [TruVoice/CGRM_EN.DLL ...]

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


def voice_names(mem, base):
    """The speaker names, as an ANSI string followed by the same name in
    UTF-16LE -- the pair the SAPI mode-info block is filled from."""
    out = []
    pat = rb"([\x20-\x7e]{3,20})\x00{1,4}((?:[\x20-\x7e]\x00){3,20})\x00\x00"
    for m in re.finditer(pat, mem):
        ansi = m.group(1).decode("latin1")
        wide = m.group(2).decode("utf-16-le")
        if wide and (ansi == wide or ansi.split()[-1] == wide):
            out.append((m.start() + base, ansi))
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


def dump(path):
    img = Image(path)
    mem, base = bytes(img.mem), img.base
    names = voice_names(mem, base)
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
