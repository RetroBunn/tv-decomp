"""Lift the engine's constant data out of a TruVoice DLL into data/.

Usage: python tools/extract_data.py [TruVoice/CGRM_EN.DLL] [data/en/engine.tvdata]

This is run once, by someone who has the original, and the result is
committed.  An ordinary build then needs no Centigram binary at all: see
tools/gen_data.py, which reads either this or a DLL and cannot tell the
difference.

What comes out is the two data sections, .rdata and .data, whole -- they
have to be whole because the engine deliberately indexes past the end of one
table into the next -- plus the jump tables the compiler left between
functions in .text, each cut to the size its C declaration in src/ gives.
Nothing else from .text is copied: the engine's code is not wanted here and
is not taken.

The contents are Centigram's work, not this project's.  See NOTICE.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe32 import Image  # noqa: E402
from gen_data import scan  # noqa: E402
import tvdata  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_SECTIONS = (".rdata", ".data")


def main():
    args = sys.argv[1:]
    image = args[0] if args else os.path.join(ROOT, "TruVoice", "CGRM_EN.DLL")
    out = (args[1] if len(args) > 1
           else os.path.join(ROOT, "data", "en", "engine.tvdata"))
    if not os.path.isfile(image):
        sys.exit("extract_data: %s not found.  This step needs your own copy "
                 "of the original; an ordinary build does not." % image)

    img = Image(image)
    annots = scan(os.path.join(ROOT, "src"))

    spans = []
    sections = []
    for s in img.sections:
        if s.name in DATA_SECTIONS:
            sections.append((s.name, s.va, s.vsize))
            spans.append((s.va, s.va + s.vsize))

    # Every annotated datum that is not already in those sections -- the
    # jump tables sitting in .text.  Taken for all of them rather than the
    # ones one particular build happens to reference, so the file does not
    # depend on what was compiled the day it was made.
    extra = 0
    for name, (addr, is_func, size) in sorted(annots.items()):
        if is_func or any(lo <= addr < hi for lo, hi in spans):
            continue
        if size is None:
            continue          # gen_data will reject it later if it matters
        spans.append((addr, addr + size))
        extra += 1

    spans.sort()
    chunks = []
    for lo, hi in spans:
        if chunks and lo <= chunks[-1][1]:
            chunks[-1][1] = max(chunks[-1][1], hi)
        else:
            chunks.append([lo, hi])

    relocs = set()
    for r in img.relocs:
        if any(lo <= r and r + 4 <= hi for lo, hi in chunks):
            relocs.add(r)

    blobs = [(lo, bytes(img.mem[lo - img.base:hi - img.base]))
             for lo, hi in chunks]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tvdata.write(out, img.base, sections, blobs, relocs)

    total = sum(len(b) for _, b in blobs)
    print("extracted %d bytes in %d chunks (%d from .text), %d relocations"
          % (total, len(blobs), extra, len(relocs)))
    print("wrote %s (%d bytes on disk)"
          % (os.path.relpath(out, ROOT), os.path.getsize(out)))


if __name__ == "__main__":
    main()
