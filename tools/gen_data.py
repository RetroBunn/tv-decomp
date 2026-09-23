"""Extract the engine's constant data from the original DLL for the
standalone build.

Usage: python tools/gen_data.py <image> <srcdir> <out.s> <obj>...

The hook build resolves every annotated global to its address inside the
loaded DLL.  The standalone build has no DLL, so the data has to come from
somewhere: this reads it out of the image and writes an assembly file that
places the same bytes at the same relative offsets, with a label wherever an
annotated symbol sits.

Two whole sections come out at once (.rdata and .data, which are nothing but
data), because the engine indexes past the ends of individual tables and
expects to land on the next one.  Everything else -- the jump-table indices
the compiler left between functions in .text -- is cut to the size its C
declaration gives.

Absolute addresses stored inside the data are re-emitted as expressions the
assembler resolves against the new labels, so no run-time fix-up is needed.
Pointers into code (the C++ vtables in .rdata) keep their original values:
nothing the engine uses ever follows one.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe32 import Image  # noqa: E402
from gen_hookmap import ANNOT, base_name  # noqa: E402

ELEM = {
    "char": 1, "uint8_t": 1, "int8_t": 1, "signed": 1, "unsigned": 1,
    "uint16_t": 2, "int16_t": 2, "short": 2,
    "uint32_t": 4, "int32_t": 4, "int": 4, "long": 4, "float": 4,
}


def scan(srcdir):
    """name -> (addr, is_func, nbytes or None)"""
    out = {}
    for root, _, files in os.walk(srcdir):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            text = open(os.path.join(root, fn), encoding="utf-8").read()
            for m in ANNOT.finditer(text):
                addr = int(m.group(1), 16)
                rest = text[m.end():m.end() + 600]
                cut = re.search(r"[;{=]", rest)
                decl = rest[:cut.start()] if cut else rest
                decl = re.sub(r"/\*.*?\*/", " ", decl, flags=re.S)
                dims = re.findall(r"\[([^\]]*)\]", decl)
                flat = re.sub(r"\[[^\]]*\]", "", decl)
                fm = re.search(r"([A-Za-z_]\w*)\s*\(", flat)
                is_func = bool(fm) and not re.search(r"\(\s*\*", flat[:fm.end() + 2])
                if is_func:
                    name = fm.group(1)
                    out[name] = (addr, True, None)
                    continue
                ids = re.findall(r"[A-Za-z_]\w*", flat)
                if not ids:
                    continue
                name = ids[-1]
                size = None
                if dims and all(d.strip() for d in dims) and "*" not in flat:
                    for tok in ids:
                        if tok in ELEM:
                            n = 1
                            try:
                                for d in dims:
                                    n *= int(d.strip(), 0)
                            except ValueError:
                                n = None
                            if n is not None:
                                size = n * ELEM[tok]
                            break
                out[name] = (addr, False, size)
    return out


def nm_syms(objs):
    defined, undefined = set(), set()
    for obj in objs:
        out = subprocess.run(["nm", obj], capture_output=True, text=True,
                             check=True).stdout
        for line in out.splitlines():
            p = line.split()
            if len(p) == 2 and p[0] == "U":
                undefined.add(p[1])
            elif len(p) == 3 and p[1] in "TtDdBbRr":
                defined.add(p[2])
    return defined, undefined


def main():
    image, srcdir, out_s = sys.argv[1], sys.argv[2], sys.argv[3]
    objs = sys.argv[4:]
    img = Image(image)
    annots = scan(srcdir)
    defined, undefined = nm_syms(objs)

    need = {}
    for sym in sorted(undefined - defined):
        b = base_name(sym)
        if b in annots:
            need[b] = (sym,) + annots[b]

    funcs = [b for b in need if need[b][2]]
    if funcs:
        sys.exit("gen_data: these are code, not data, and the standalone "
                 "build must define them: " + " ".join(sorted(funcs)))

    # whole-section spans: .rdata and .data are pure data
    spans = []
    for s in img.sections:
        if s.name in (".rdata", ".data"):
            spans.append((s.va, s.va + s.vsize))
    for b, (sym, addr, _is_func, size) in sorted(need.items()):
        if any(lo <= addr < hi for lo, hi in spans):
            continue
        if size is None:
            sys.exit("gen_data: %s at 0x%08x is outside .rdata/.data and its "
                     "declaration gives no size" % (b, addr))
        spans.append((addr, addr + size))

    spans.sort()
    blobs = []
    for lo, hi in spans:
        if blobs and lo <= blobs[-1][1]:
            blobs[-1][1] = max(blobs[-1][1], hi)
        else:
            blobs.append([lo, hi])

    def find(va):
        for i, (lo, hi) in enumerate(blobs):
            if lo <= va < hi:
                return i, va - lo
        return None, 0

    labels = {}
    for b, (sym, addr, _f, _s) in sorted(need.items()):
        labels.setdefault(addr, []).append(sym)

    relocs = set()
    for r in img.relocs:
        i, _ = find(r)
        if i is not None and r + 4 <= blobs[i][1]:
            relocs.add(r)

    mem = img.mem
    base = img.base
    n_ext = 0
    with open(out_s, "w", newline="\n") as fp:
        fp.write("/* Generated by tools/gen_data.py -- do not edit. */\n")
        fp.write("\t.section .data\n")
        for i, (lo, hi) in enumerate(blobs):
            fp.write("\t.balign 16\n_tv_blob%d:\n" % i)
            va = lo
            run = []

            def flush(run=run, fp=fp):
                for k in range(0, len(run), 24):
                    fp.write("\t.byte " +
                             ",".join("0x%02x" % c for c in run[k:k + 24]) + "\n")
                del run[:]

            while va < hi:
                if va in labels:
                    flush()
                    for sym in labels[va]:
                        fp.write("\t.globl %s\n%s:\n" % (sym, sym))
                if va in relocs:
                    flush()
                    o = va - base
                    t = int.from_bytes(mem[o:o + 4], "little")
                    j, off = find(t)
                    if j is None:
                        fp.write("\t.long 0x%08x\n" % t)
                        n_ext += 1
                    else:
                        fp.write("\t.long _tv_blob%d+0x%x\n" % (j, off))
                    va += 4
                    continue
                run.append(mem[va - base])
                va += 1
            flush()
    total = sum(hi - lo for lo, hi in blobs)
    print("data: %d symbols, %d blobs, %d bytes, %d relocations (%d left as "
          "original addresses)" % (len(need), len(blobs), total, len(relocs), n_ext))


if __name__ == "__main__":
    main()
