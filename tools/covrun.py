"""Run the oracle with block coverage over the difftest matrix and report.

Usage: python tools/covrun.py [--full] [-j N] [--decompiled] [--lang en|es]

Writes work/cov_merged.txt (work/cov_es_merged.txt for --lang es).  With
--decompiled, lists the decompiled functions that still have unexecuted
blocks, with the uncovered block addresses.
"""
import argparse
import concurrent.futures as cf
import os
import re
import subprocess
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import difftest  # noqa: E402

ROOT = difftest.ROOT
WORK = os.path.join(ROOT, "work")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--decompiled", action="store_true")
    ap.add_argument("--lang", default="en", choices=("en", "es"),
                    help="which engine to measure; see docs/SPANISH.md")
    a = ap.parse_args()

    difftest.set_lang(a.lang)
    BLOCKS = os.path.join(WORK, "cgrm_" + a.lang, "blocks.txt")
    tail = "" if a.lang == "en" else "_" + a.lang
    hookdir = "hook" if a.lang == "en" else "hook_" + a.lang
    outdir = os.path.join(WORK, "cov" + tail)
    os.makedirs(outdir, exist_ok=True)
    runs = difftest.configs(a.full, difftest.prepare_inputs())

    def job(r):
        name, path, v, phone, suffix, pargs = r
        tag = "%s_v%d%s%s" % (name, v, "_8k" if phone else "", suffix)
        hits = os.path.join(outdir, tag + ".txt")
        args = [difftest.TVH, "-c", BLOCKS, "-C", hits] + list(pargs) + \
            ["-v", str(v)] + (["-8"] if phone else []) + \
            [difftest.DLL, "@" + path, os.path.join(outdir, tag + ".wav")]
        subprocess.run(args, capture_output=True)
        return hits

    with cf.ThreadPoolExecutor(a.j) as ex:
        hitfiles = [h for h in ex.map(job, runs) if os.path.exists(h)]

    total = defaultdict(set)
    for line in open(BLOCKS):
        b, f = line.split()
        total[int(f, 16)].add(int(b, 16))
    hit = set()
    for hf in hitfiles:
        for line in open(hf):
            b, f = line.split()
            hit.add(int(b, 16))
    with open(os.path.join(WORK, "cov%s_merged.txt" % tail), "w") as fp:
        for f in sorted(total):
            for b in sorted(total[f] & hit):
                fp.write("%08x %08x\n" % (b, f))
    touched = [f for f in total if total[f] & hit]
    print("runs: %d; functions touched: %d of %d; blocks hit %d of %d" % (
        len(hitfiles), len(touched), len(total), len(hit),
        sum(len(v) for v in total.values())))

    if a.decompiled:
        gen = open(os.path.join(ROOT, "build", "obj", hookdir,
                                "hooks_gen.c")).read()
        done = [(int(m.group(1), 16), m.group(2))
                for m in re.finditer(r'\{0x([0-9a-f]+), hk_\d+, "(\w+)"\}', gen)]
        for addr, name in sorted(done):
            blocks = total.get(addr, set())
            miss = sorted(blocks - hit)
            if miss:
                print("%-28s %08x  %3d/%-3d blocks; missing: %s" % (
                    name, addr, len(blocks) - len(miss), len(blocks),
                    " ".join("%08x" % b for b in miss[:64]) + (" ..." if len(miss) > 64 else "")))


if __name__ == "__main__":
    main()
