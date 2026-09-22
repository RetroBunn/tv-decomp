"""Print the static call tree below a function.

Usage: python tools/calltree.py <image> <workdir> <addr> [maxdepth]
Each function is expanded once; later occurrences are marked with '^'.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import load_analysis  # noqa: E402
from listing import Lister  # noqa: E402


def main():
    image, workdir, root = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    maxdepth = int(sys.argv[4]) if len(sys.argv) > 4 else 99
    an = load_analysis(image, os.path.join(workdir, "analysis.pickle"))
    L = Lister(an, image)
    seen = set()

    def walk(e, depth):
        f = an.funcs.get(e)
        if f is None:
            print("  " * depth + "%s (not analysed)" % L.name(e))
            return
        mark = "^" if e in seen else ""
        imps = ",".join(sorted(x.split("!")[1] for x in f.imports))
        print("%s%s%s  [%d bytes%s%s]" % ("  " * depth, L.name(e), mark, f.size,
              ", icalls=%d" % len(f.icalls) if f.icalls else "",
              ", " + imps if imps else ""))
        if e in seen or depth >= maxdepth:
            return
        seen.add(e)
        # order callees by first call site
        order = []
        for a in f.insn_addrs:
            ins = an.insns[a]
            if ins.mnem == "call" and ins.operands and ins.operands[0][0] == "imm":
                t = ins.operands[0][1]
                if t not in order:
                    order.append(t)
        for t in sorted(f.tailcalls):
            if t not in order:
                order.append(t)
        for t in order:
            walk(t, depth + 1)

    walk(root, 0)


if __name__ == "__main__":
    main()
