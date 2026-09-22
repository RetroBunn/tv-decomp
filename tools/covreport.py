"""Merge coverage hit files and summarise per function.

Usage: python tools/covreport.py <image> <workdir> <hits.txt>... [-o merged.txt]

Prints, per function, blocks hit / total and its size, sorted by address,
plus totals split into "touched" and "never executed" functions.
"""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main():
    args = sys.argv[1:]
    out = None
    if "-o" in args:
        i = args.index("-o")
        out = args[i + 1]
        del args[i:i + 2]
    image, workdir, hitfiles = args[0], args[1], args[2:]
    total = defaultdict(int)
    for line in open(os.path.join(workdir, "blocks.txt")):
        b, f = line.split()
        total[int(f, 16)] += 1
    hit = set()
    for hf in hitfiles:
        for line in open(hf):
            b, f = line.split()
            hit.add((int(b, 16), int(f, 16)))
    per = defaultdict(int)
    for b, f in hit:
        per[f] += 1
    if out:
        with open(out, "w") as fp:
            for b, f in sorted(hit):
                fp.write("%08x %08x\n" % (b, f))
    sizes = {}
    for line in open(os.path.join(workdir, "functions.txt")):
        parts = line.split()
        sizes[int(parts[0], 16)] = int(parts[1])
    touched = [f for f in total if per.get(f)]
    print("functions touched: %d of %d; blocks hit %d of %d" % (
        len(touched), len(total), len(hit), sum(total.values())))
    print("bytes in touched functions: %d; untouched: %d" % (
        sum(sizes.get(f, 0) for f in touched),
        sum(sizes.get(f, 0) for f in total if not per.get(f))))
    for f in sorted(total):
        print("%08x %6d  %4d/%-4d %s" % (f, sizes.get(f, 0), per.get(f, 0), total[f],
                                         "" if per.get(f) else "UNTOUCHED"))


if __name__ == "__main__":
    main()
