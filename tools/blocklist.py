"""Emit the basic-block start addresses of every analysed function.

Usage: python tools/blocklist.py <image> <workdir> [out]

Output lines: "<block va> <function entry va>".  Used by the harness
coverage mode, which plants one-shot INT3s at these addresses, so only
addresses that are real instruction starts inside real code are emitted.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import load_analysis  # noqa: E402


def main():
    image, workdir = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(workdir, "blocks.txt")
    an = load_analysis(image, os.path.join(workdir, "analysis.pickle"))
    table_bytes = set()
    for s, e in an.jumptable_ranges + an.indextable_ranges:
        table_bytes.update(range(s, e))
    seen = set()
    rows = []
    for entry in sorted(an.funcs):
        f = an.funcs[entry]
        starts = set(f.blocks)
        for tva, targets in f.jumptables.values():
            starts.update(targets)
        # also split after calls/jcc (fallthrough blocks)
        for a in f.insn_addrs:
            ins = an.insns[a]
            if ins.is_jcc() or ins.is_call():
                starts.add(ins.end)
        for b in sorted(starts):
            if b in seen or b not in an.insns or b in table_bytes:
                continue
            if an.owner.get(b) is None:
                continue
            seen.add(b)
            rows.append((b, entry))
    with open(out, "w") as fp:
        for b, e in rows:
            fp.write("%08x %08x\n" % (b, e))
    print("%d blocks in %d functions -> %s" % (len(rows), len(an.funcs), out))


if __name__ == "__main__":
    main()
