"""Match functions between two TruVoice engines by the shape of their code.

The 1997 English engine and the 1995 engines are separate builds of related
source, so a function that survived between them has the same instruction
sequence but different field offsets, different call targets and different
data addresses.  This normalises those away -- every immediate or
displacement of 0x100 or more becomes "N", so struct offsets and absolute
addresses stop mattering while small shared constants (a 0x44 stride, a
type mask, a loop count of 5) still carry signal -- and then scores two
functions by the Jaccard overlap of their 4-gram token sets.

It is a ranking, not a proof.  A high score says "read these two together";
what settles a name is still the contents, as docs/SPANISH.md keeps
insisting.

Usage:
    python tools/xmatch.py <src.dll> <src-workdir> <dst.dll> <dst-workdir>
                           [--only FILE] [--min 0.35] [--top 3]

--only takes a file of "<va>" or coverage lines and restricts the source
functions to those, which is how it is pointed at what actually runs.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe32 import Image  # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402

BIG = re.compile(r"0x[0-9a-f]{3,}")


def tokens(md, mem, base, va, size):
    """Normalised instruction tokens for one function."""
    out = []
    for i in md.disasm(mem[va - base:va - base + size], va):
        ops = BIG.sub("N", i.op_str or "")
        out.append(i.mnemonic + "|" + ops)
    return out


def grams(toks, k=4):
    if len(toks) < k:
        return frozenset(["\x00".join(toks)])
    return frozenset("\x00".join(toks[i:i + k]) for i in range(len(toks) - k + 1))


def load_funcs(workdir):
    fns = {}
    with open(os.path.join(workdir, "functions.txt")) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                fns[int(p[0], 16)] = int(p[1])
    return fns


def load_names(srcdir="src"):
    """Address -> name, from the /* @0x... */ annotations in the C."""
    names = {}
    pat = re.compile(r"@(0x1[0-9a-f]{7})\s*\*/\s*\n[^\n]*?"
                     r"(?:TV_THISCALL|TV_CDECL)?\s*\**([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    for root, _dirs, files in os.walk(srcdir):
        for fn in files:
            if not fn.endswith(".c"):
                continue
            with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                text = f.read()
            for m in pat.finditer(text):
                names[int(m.group(1), 16)] = m.group(2)
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src_dll")
    ap.add_argument("src_work")
    ap.add_argument("dst_dll")
    ap.add_argument("dst_work")
    ap.add_argument("--only")
    ap.add_argument("--min", type=float, default=0.35)
    ap.add_argument("--top", type=int, default=3)
    ap.add_argument("--blocks", help="blocks.txt for --only coverage files")
    ap.add_argument("--near", type=lambda v: int(v, 0), default=0,
                    help="only consider candidates within this many bytes of the "
                         "source address.  The four 1995 engines are laid out "
                         "almost identically -- 97%% of confident ES/IT matches "
                         "land within 0x2000 -- so --near 0x2000 makes a much "
                         "lower --min safe between them.  Useless against the "
                         "1997 English build, which shares no layout with them.")
    args = ap.parse_args()

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    src, dst = Image(args.src_dll), Image(args.dst_dll)
    smem, dmem = bytes(src.mem), bytes(dst.mem)
    sf, df = load_funcs(args.src_work), load_funcs(args.dst_work)
    names = load_names()

    want = set(sf)
    if args.only:
        fn_of = {}
        if args.blocks:
            with open(args.blocks) as f:
                for line in f:
                    b, fu = line.split()
                    fn_of[int(b, 16)] = int(fu, 16)
        want = set()
        with open(args.only) as f:
            for line in f:
                line = line.split("#")[0].strip()
                if not line:
                    continue
                a = int(line.split()[0], 16)
                want.add(fn_of.get(a, a))
        want &= set(sf)

    # index the destination
    dg = {}
    for a, n in df.items():
        if n < 24:
            continue
        t = tokens(md, dmem, dst.base, a, n)
        if len(t) >= 6:
            dg[a] = (grams(t), len(t))
    sys.stderr.write("indexed %d destination functions\n" % len(dg))

    rows = []
    for a in sorted(want):
        n = sf[a]
        if n < 24:
            continue
        t = tokens(md, smem, src.base, a, n)
        if len(t) < 6:
            continue
        g = grams(t)
        best = []
        for b, (g2, l2) in dg.items():
            if args.near and abs(b - a) > args.near:
                continue
            # a length ratio guard keeps tiny functions from matching anything
            if not (0.5 <= len(t) / float(l2) <= 2.0):
                continue
            inter = len(g & g2)
            if not inter:
                continue
            sc = inter / float(len(g | g2))
            if sc >= args.min:
                best.append((sc, b))
        best.sort(reverse=True)
        if best:
            rows.append((best[0][0], a, n, best[:args.top]))

    rows.sort(reverse=True)
    print("%d of %d source functions matched at >= %.2f" % (len(rows), len(want), args.min))
    print()
    for sc, a, n, best in rows:
        cands = ", ".join("%s (%.2f)" % (names.get(b, "sub_%08x" % b), s) for s, b in best)
        print("  sub_%08x %5d bytes -> %s" % (a, n, cands))
    return 0


if __name__ == "__main__":
    sys.exit(main())
