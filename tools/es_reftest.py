"""Reproduce spanish_test.wav from CGRM_ES.DLL, sample for sample.

The recording was made with Balabolka at its defaults, and it turns out to be
two SAPI TextData items rather than one:

    item 1   "Hola."
    item 2   "\\r\\nEste es un ejemplo de sintesis de voz en espanol con TruVoice."

The break falls at the end of the first line's *text*, so the line terminator
leads the second item instead of ending the first.  That is not a detail: the
leading CRLF is worth 4730 frames of silence, and it shifts the pitch contour
of everything after it, which is why feeding the same words split the obvious
way does not reproduce the file.

Each item was spoken by an engine in its starting state -- two separate tvh
runs concatenated match the recording exactly, while feeding both items to one
engine does not, because the engine carries state between items.

Usage:  python tools/es_reftest.py [--dll TruVoice/CGRM_ES.DLL]
"""
import argparse
import io
import os
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TVH = os.path.join(ROOT, "build", "check", "tvh.exe")
TVH_HOOK = os.path.join(ROOT, "build", "check", "tvh_hook_es.exe")


def split_items(text):
    """Split the way the recording was made: cut at the end of each line's
    text, so every item after the first begins with the terminator that
    preceded it."""
    items = []
    a = 0
    pos = 0
    while pos < len(text):
        b = text.find("\n", pos)
        if b < 0:
            b = len(text)
        e = b
        while e > a and text[e - 1] in "\r ":
            e -= 1
        if e > a:
            items.append(text[a:e])
            a = e
        pos = b + 1
    return items


def pcm(path):
    w = wave.open(path, "rb")
    try:
        return w.readframes(w.getnframes()), w.getframerate()
    finally:
        w.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=os.path.join(ROOT, "TruVoice", "CGRM_ES.DLL"))
    ap.add_argument("--voice", type=int, default=0)
    ap.add_argument("--text", default=os.path.join(ROOT, "spanish_test.txt"))
    ap.add_argument("--ref", default=os.path.join(ROOT, "spanish_test.wav"))
    args = ap.parse_args()

    for p in (TVH, args.dll, args.text, args.ref):
        if not os.path.exists(p):
            print("missing %s" % p)
            return 0 if p == args.dll else 1

    text = io.open(args.text, encoding="utf-8", newline="").read()
    items = split_items(text)
    print("%d items:" % len(items))
    for it in items:
        print("    %r" % it)

    tmp = tempfile.mkdtemp(prefix="es_reftest")
    ref, sr = pcm(args.ref)

    # The oracle drives the original DLL; the hook build patches every
    # function written in es/ over it first.  Both have to produce the
    # recording, so the second run is what tests the decompilation.
    runs = [("original engine", TVH)]
    if os.path.exists(TVH_HOOK):
        runs.append(("with es/ hooked in", TVH_HOOK))
    else:
        print("note: no tvh_hook_es.exe, skipping the decompiled-C check")

    bad = 0
    for label, exe in runs:
        out = b""
        for i, it in enumerate(items):
            # the engine is a 1995 Windows DLL: it wants the text in cp1252
            tin = os.path.join(tmp, "item%d.txt" % i)
            twav = os.path.join(tmp, "%s_item%d.wav" % (os.path.basename(exe), i))
            with open(tin, "wb") as f:
                f.write(it.encode("cp1252"))
            cmd = [exe, "-v", str(args.voice), args.dll, "@" + tin, twav]
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if r.returncode != 0:
                print("%s failed on item %d:\n%s" % (exe, i, r.stderr.decode("latin1")))
                return 1
            d, _ = pcm(twav)
            out += d
        if out == ref:
            print("  %-20s identical: %d frames (%.2f s @ %d Hz)"
                  % (label, len(ref) // 2, len(ref) / 2.0 / sr, sr))
            continue
        bad = 1
        print("  %-20s DIFFERS: produced %d frames, reference %d"
              % (label, len(out) // 2, len(ref) // 2))
        k = 0
        while k < min(len(out), len(ref)) and out[k] == ref[k]:
            k += 1
        print("  %-20s identical prefix %d frames (%.3f s)"
              % ("", k // 2, (k // 2) / float(sr)))
    return bad


if __name__ == "__main__":
    sys.exit(main())
