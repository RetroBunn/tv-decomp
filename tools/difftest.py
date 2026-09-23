"""Differential test: decompiled (hooked) engine vs the original DLL.

Usage: python tools/difftest.py [--full] [--hooks SPEC] [-j N] [--only SUBSTR]

Runs every input of the corpus through build/harness/tvh.exe (original code
only, results cached in work/difftest/ref) and build/harness/tvh_hook.exe
(decompiled functions hooked in), and requires byte-identical WAV output.

Corpus: tests/corpus/*.txt (UTF-8, converted to cp1252 like SAPI's
WideCharToMultiByte would) plus TruVoice/*.TXT when present locally.
--full additionally runs a subset under all ten voices at 11025 and 8000 Hz.
"""
import argparse
import concurrent.futures as cf
import glob
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "TruVoice", "CGRM_EN.DLL")
TVH = os.path.join(ROOT, "build", "harness", "tvh.exe")
TVH_HOOK = os.path.join(ROOT, "build", "harness", "tvh_hook.exe")
WORK = os.path.join(ROOT, "work", "difftest")


def md5(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def prepare_inputs():
    indir = os.path.join(WORK, "in")
    os.makedirs(indir, exist_ok=True)
    inputs = []
    for p in sorted(glob.glob(os.path.join(ROOT, "tests", "corpus", "*.txt"))):
        data = open(p, encoding="utf-8").read().encode("cp1252")
        name = os.path.splitext(os.path.basename(p))[0]
        dst = os.path.join(indir, name + ".txt")
        if not os.path.exists(dst) or open(dst, "rb").read() != data:
            open(dst, "wb").write(data)
        inputs.append((name, dst))
    for p in sorted(glob.glob(os.path.join(ROOT, "TruVoice", "*.TXT"))):
        name = "tv_" + os.path.splitext(os.path.basename(p))[0].lower()
        inputs.append((name, p))
    return inputs


# (tag suffix, harness args): parameter variations for --full, as if the
# application had called ITTSAttributes::PitchSet/SpeedSet/VolumeSet.
PARAM_VARIANTS = [
    ("_p40", ["-p", "40"]), ("_p250", ["-p", "250"]),
    ("_s80", ["-s", "80"]), ("_s260", ["-s", "260"]), ("_s400", ["-s", "400"]),
    ("_vol8000", ["-V", "0x8000"]), ("_vol1000", ["-V", "0x1000"]),
    ("_vol40", ["-V", "0x40"]), ("_volffffffff", ["-V", "0xffffffff"]),
]


# Registry option variants (HKCU\Software\Centigram\TextToSpeech\Control\
# English "PreFormat"/"TextIn" = off), run on every input for --full.
OPTION_VARIANTS = [
    ("_nopre", ["-P0"]), ("_notextin", ["-T0"]), ("_nopre_notextin", ["-P0", "-T0"]),
]


def fixed_opts(name):
    """tests/corpus/NAME.opts pins the harness options for an input (and
    excludes it from the variant matrix)."""
    p = os.path.join(ROOT, "tests", "corpus", name + ".opts")
    return open(p).read().split() if os.path.exists(p) else None


def configs(full, inputs):
    runs = []
    for name, path in inputs:
        opts = fixed_opts(name)
        runs.append((name, path, 0, False, "", opts or []))
    inputs = [x for x in inputs if fixed_opts(x[0]) is None]
    if full:
        for name, path in inputs:
            for suffix, args in OPTION_VARIANTS:
                runs.append((name, path, 0, False, suffix, args))
        subset = [x for x in inputs if x[0] in ("01_basic", "03_dates_times", "10_punct", "12_long")]
        for name, path in subset:
            for v in range(10):
                for phone in (False, True):
                    if v == 0 and not phone:
                        continue
                    runs.append((name, path, v, phone, "", []))
            for suffix, args in PARAM_VARIANTS:
                runs.append((name, path, 0, False, suffix, args))
    return runs


def run_one(exe, extra, name, path, voice, phone, outdir, suffix="", pargs=(),
            dll=True):
    os.makedirs(outdir, exist_ok=True)
    tag = "%s_v%d%s%s" % (name, voice, "_8k" if phone else "", suffix)
    out = os.path.join(outdir, tag + ".wav")
    args = [exe] + extra + list(pargs) + ["-v", str(voice)] + (["-8"] if phone else []) + \
        ([DLL] if dll else []) + ["@" + path, out]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        return tag, None, (r.stderr or "")[-400:]
    return tag, out, None


def first_diff(a, b):
    da, db = open(a, "rb").read(), open(b, "rb").read()
    n = min(len(da), len(db))
    for i in range(44, n):
        if da[i] != db[i]:
            return "first difference at byte %d (sample %d); sizes %d vs %d" % (
                i, (i - 44) // 2, len(da), len(db))
    return "sizes differ: %d vs %d" % (len(da), len(db))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true")
    ap.add_argument("--hooks", default="all")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--only", default="")
    ap.add_argument("--port", action="store_true",
                    help="test build/harness/tv.exe, the standalone build, "
                         "which loads no DLL")
    a = ap.parse_args()

    dll_id = md5(DLL)[:8]
    inputs = prepare_inputs()
    runs = [r for r in configs(a.full, inputs) if a.only in r[0]]
    refdir = os.path.join(WORK, "ref_" + dll_id)
    canddir = os.path.join(WORK, "cand")

    # References are cached per input content + config.
    def ref_job(r):
        name, path, v, phone, suffix, pargs = r
        key = hashlib.md5(open(path, "rb").read()).hexdigest()[:10]
        tag = "%s_v%d%s%s" % (name, v, "_8k" if phone else "", suffix)
        out = os.path.join(refdir, tag + "." + key + ".wav")
        if os.path.exists(out):
            return tag, out, None
        t, o, err = run_one(TVH, [], name, path, v, phone, refdir, suffix, pargs)
        if o:
            os.replace(o, out)
            return t, out, None
        return t, None, err

    fails = 0
    with cf.ThreadPoolExecutor(a.j) as ex:
        refs = dict((t, (o, e)) for t, o, e in ex.map(ref_job, runs))
        if a.port:
            exe = os.path.join(ROOT, "build", "harness", "tv.exe")
            cands = dict((t, (o, e)) for t, o, e in ex.map(
                lambda r: run_one(exe, [], r[0], r[1], r[2], r[3], canddir,
                                  r[4], r[5], dll=False), runs))
        else:
            cands = dict((t, (o, e)) for t, o, e in ex.map(
                lambda r: run_one(TVH_HOOK, ["-H", a.hooks], r[0], r[1], r[2], r[3],
                                  canddir, r[4], r[5]), runs))
    for tag in sorted(refs):
        ro, re_ = refs[tag]
        co, ce = cands.get(tag, (None, "missing"))
        if ro is None:
            print("REF-FAIL %s: %s" % (tag, re_))
            fails += 1
        elif co is None:
            print("FAIL     %s: candidate crashed: %s" % (tag, ce))
            fails += 1
        elif md5(ro) != md5(co):
            print("MISMATCH %s: %s" % (tag, first_diff(ro, co)))
            fails += 1
    print("%d/%d identical" % (len(refs) - fails, len(refs)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
