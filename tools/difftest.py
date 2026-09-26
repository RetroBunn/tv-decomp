"""Differential test: decompiled (hooked) engine vs the original DLL.

Usage: python tools/difftest.py [--full] [--hooks SPEC] [-j N] [--only SUBSTR]
       python tools/difftest.py --ref

Runs every input of the corpus through build/check/tvh.exe (original code
only, results cached in work/difftest/ref) and build/check/tvh_hook.exe
(decompiled functions hooked in), and requires byte-identical WAV output.

Corpus: tests/corpus/*.txt (UTF-8, converted to cp1252 like SAPI's
WideCharToMultiByte would) plus TruVoice/*.TXT when present locally.
--full additionally runs a subset under all ten voices at 11025 and 8000 Hz.

--ref is a different check: it compares against audio captured from the real
installed engine through a SAPI client, which is the only thing that tests
the harness's own reconstruction of the engine thread rather than just the
decompiled code.  See ref_inputs() for the file layout.  It is skipped when
ref/ is empty, so it costs nothing to leave in a test run.
"""
import argparse
import concurrent.futures as cf
import glob
import hashlib
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "TruVoice", "CGRM_EN.DLL")
TVH = os.path.join(ROOT, "build", "check", "tvh.exe")
TVH_HOOK = os.path.join(ROOT, "build", "check", "tvh_hook.exe")
TV_PORT = os.path.join(ROOT, "build", "check", "tv.exe")
TV_PORT64 = os.path.join(ROOT, "build", "check", "tv64.exe")

#: The port is compared against the original, so every OpenTV extension
#: has to be off: the point of this test is that the decompiled engine
#: still does what the 1997 one did, bug for bug.  Improvements are
#: covered by tests/api_test.c instead.
CLASSIC = ["-C"]
WORK = os.path.join(ROOT, "work", "difftest")
REFDIR = os.path.join(ROOT, "ref")
CORPUS = os.path.join(ROOT, "tests", "corpus")

#: Extra inputs beyond the corpus: the sample texts the installer ships,
#: which are English.
EXTRA_GLOB = os.path.join(ROOT, "TruVoice", "*.TXT")

#: The inputs --full runs under all ten voices and both rates.  Naming
#: them keeps the configuration count stable as the corpus grows.
SUBSET = ("01_basic", "03_dates_times", "10_punct", "12_long")

#: One row per engine, so that adding French or German is a row here
#: rather than a branch in everything that drives the harness.
SUBSETS = {
    "en": SUBSET,
    "es": ("01_basico", "02_numeros", "03_acentos", "04_punt"),
}


def set_lang(lang):
    """Point the module at one engine's DLL, corpus, hook build and workdir.

    Everything that differs between engines is one of these six names.
    Callers that drive the harness -- difftest itself, covrun -- go through
    here so they cannot disagree about where an engine's files live.
    """
    global DLL, TVH_HOOK, CORPUS, WORK, EXTRA_GLOB, SUBSET
    if lang == "en":
        return
    DLL = os.path.join(ROOT, "TruVoice", "CGRM_%s.DLL" % lang.upper())
    TVH_HOOK = os.path.join(ROOT, "build", "check", "tvh_hook_%s.exe" % lang)
    CORPUS = os.path.join(ROOT, "tests", "corpus_%s" % lang)
    WORK = os.path.join(ROOT, "work", "difftest_%s" % lang)
    EXTRA_GLOB = None  # the shipped sample texts are English
    SUBSET = SUBSETS[lang]


def md5(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def prepare_inputs():
    indir = os.path.join(WORK, "in")
    os.makedirs(indir, exist_ok=True)
    inputs = []
    for p in sorted(glob.glob(os.path.join(CORPUS, "*.txt"))):
        data = open(p, encoding="utf-8").read().encode("cp1252")
        name = os.path.splitext(os.path.basename(p))[0]
        dst = os.path.join(indir, name + ".txt")
        if not os.path.exists(dst) or open(dst, "rb").read() != data:
            open(dst, "wb").write(data)
        inputs.append((name, dst))
    for p in sorted(glob.glob(EXTRA_GLOB)) if EXTRA_GLOB else []:
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
    p = os.path.join(CORPUS, name + ".opts")
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
        subset = [x for x in inputs if x[0] in SUBSET]
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


def wav_data(path):
    """(format, PCM payload) of a WAV file.  The reference recordings come
    from whatever SAPI client made them and need not have the harness's
    44-byte header -- Balabolka writes an 18-byte `fmt ` chunk -- so the
    chunks have to be walked rather than assumed."""
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    fmt, pos = None, 12
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        size = struct.unpack_from("<I", d, pos + 4)[0]
        if cid == b"fmt " and size >= 16:
            tag, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", d, pos + 8)
            fmt = (tag, ch, rate, bits)
        elif cid == b"data":
            return fmt, d[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    raise ValueError("no data chunk")


def fmt_str(f):
    if not f:
        return "unknown"
    return "%d Hz %d-bit %s%s" % (f[2], f[3],
                                  "mono" if f[1] == 1 else "%d-channel" % f[1],
                                  "" if f[0] == 1 else " (not PCM)")


def ref_inputs():
    """Audio captured from the real installed engine, as
    ref/NAME.wav + ref/NAME.txt (+ optional ref/NAME.opts).

    Each *line* of the .txt is one utterance, rendered as its own TextData
    call, and the PCM is concatenated -- which is what a SAPI client that
    splits on sentences produces, and is how the recordings were made.  A
    client that speaks the lot in one call gets one line.  .opts pins the
    harness options, as in the corpus.
    """
    out = []
    for wav in sorted(glob.glob(os.path.join(REFDIR, "*.wav"))):
        base = os.path.splitext(wav)[0]
        if not os.path.exists(base + ".txt"):
            continue
        text = open(base + ".txt", encoding="utf-8").read()
        lines = [l.rstrip("\r") for l in text.split("\n")]
        lines = [l for l in lines if l.strip()]
        opts = []
        if os.path.exists(base + ".opts"):
            opts = open(base + ".opts").read().split()
        if lines:
            out.append((os.path.basename(base), wav, lines, opts))
    return out


def run_ref(exe, dll, tag, lines, opts, outdir):
    """Render each line as its own utterance; return the concatenated PCM."""
    os.makedirs(outdir, exist_ok=True)
    pcm, fmt = b"", None
    for i, line in enumerate(lines):
        src = os.path.join(outdir, "%s_%02d.txt" % (tag, i))
        out = os.path.join(outdir, "%s_%02d.wav" % (tag, i))
        open(src, "wb").write(line.encode("cp1252"))
        args = [exe] + list(opts) + ([DLL] if dll else []) + ["@" + src, out]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return None, None, (r.stderr or "no output file").strip()[-400:]
        fmt, data = wav_data(out)
        pcm += data
    return fmt, pcm, None


def ref_main(hooks):
    refs = ref_inputs()
    if not refs:
        print("ref/: no NAME.wav + NAME.txt pairs -- skipped")
        return 0
    engines = [("oracle", TVH, [], True),
               ("hooked", TVH_HOOK, ["-H", hooks], True)]
    if os.path.exists(TV_PORT):
        engines.append(("standalone", TV_PORT, CLASSIC, False))
    outdir = os.path.join(WORK, "refaudio")
    fails = checks = 0
    for name, wav, lines, opts in refs:
        try:
            want_fmt, want = wav_data(wav)
        except ValueError as e:
            print("REF-BAD  %s: %s" % (name, e))
            fails += 1
            continue
        print("%s: %d utterance(s), %d samples, %s%s"
              % (name, len(lines), len(want) // 2, fmt_str(want_fmt),
                 (", opts " + " ".join(opts)) if opts else ""))
        for label, exe, extra, dll in engines:
            checks += 1
            fmt, got, err = run_ref(exe, dll, "%s_%s" % (name, label), lines,
                                    list(extra) + list(opts), outdir)
            if got is None:
                print("  FAIL      %-11s crashed: %s" % (label, err))
                fails += 1
            elif fmt != want_fmt:
                print("  MISMATCH  %-11s format: got %s, reference is %s"
                      % (label, fmt_str(fmt), fmt_str(want_fmt)))
                fails += 1
            elif got != want:
                n = min(len(got), len(want))
                at = next((i for i in range(n) if got[i] != want[i]), n)
                print("  MISMATCH  %-11s at sample %d; %d vs %d samples"
                      % (label, at // 2, len(got) // 2, len(want) // 2))
                fails += 1
            else:
                print("  ok        %-11s %d samples" % (label, len(got) // 2))
    print("%d/%d reference checks identical" % (checks - fails, checks))
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true")
    ap.add_argument("--hooks", default="all")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--only", default="")
    ap.add_argument("--port", action="store_true",
                    help="test build/check/tv.exe, the standalone build, "
                         "which loads no DLL")
    ap.add_argument("--port64", action="store_true",
                    help="test build/check/tv64.exe, the 64-bit standalone "
                         "build")
    ap.add_argument("--lang", default="en", choices=("en", "es"),
                    help="which engine to test.  es uses the Spanish DLL, "
                         "tests/corpus_es and build/check/tvh_hook_es.exe; "
                         "see docs/SPANISH.md")
    ap.add_argument("--ref", action="store_true",
                    help="instead of the corpus, check every build against "
                         "the recordings in ref/, made with the real "
                         "installed engine")
    a = ap.parse_args()

    set_lang(a.lang)
    if a.lang != "en":
        if a.port or a.port64:
            print("there is no standalone Spanish build yet")
            return 2
        if a.ref:
            print("ref/ holds English recordings; use tools/es_reftest.py")
            return 2
        if not os.path.exists(TVH_HOOK):
            print("no %s: build it first (harness/build.sh builds it when "
                  "es/ has C in it)" % os.path.relpath(TVH_HOOK, ROOT))
            return 2

    # This test exists to compare against the original, so it is the one
    # thing in the project that genuinely needs a copy of it.  Building and
    # running OpenTV does not: see NOTICE and tools/extract_data.py.
    if not os.path.exists(DLL):
        print('no %s, so there is nothing to compare against.' %
              os.path.relpath(DLL, ROOT))
        print('The engine itself builds and runs without it; this check is')
        print('for anyone who has a copy and wants to verify the work.')
        return 0

    if a.ref:
        sys.exit(1 if ref_main(a.hooks) else 0)

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
        if a.port or a.port64:
            exe = TV_PORT64 if a.port64 else TV_PORT
            cands = dict((t, (o, e)) for t, o, e in ex.map(
                lambda r: run_one(exe, CLASSIC, r[0], r[1], r[2], r[3], canddir,
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
