# -*- coding: utf-8 -*-
"""Fricative against affricate: the time axis the papers are about.

Run from the repository root:  python build/Japanese_test/make_sibilants.py

Yamakawa & Amano (2015) and Amano & Yamakawa (2021) separate /s/, [C], /ts/
and [tC] on two orthogonal axes: TIME tells a fricative from an affricate, and
the intensity in a one-third-octave band at 3,150 Hz tells an alveolo-palatal
from an alveolar.  Their 2021 measurements, two independent sets of 63 words:

    /s/   total 134.6 ms      /ts/  73.3 ms      [tC]  78.7 ms

so a fricative is about 1.8 times an affricate.  The base here was 7 frames
against 5 and 6, a ratio of 1.27 -- the finding was in the comment and never in
the numbers.  10 frames gives 1.82.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)


def variant(name):
    n = 7 if name == 'before' else 10
    J.MANNER['s'] = ('fric', 0, n, 0)
    J.MANNER['sh'] = ('fric', 0, n, 0)
    S.FRIC_GROWN = 8 if name == 'before' else 4


def clip(text, accent=0):
    morae = M.to_morae(text)
    frames, ends, q_ends = S.build(morae)
    S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae)
    return S.render(frames)


def write(name, parts, note=""):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, GAP, p])
    with wave.open(os.path.join(HERE, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-30s %5.2f s  %s" % (name, len(x) / float(SR), note))


print("65  the four consonants, which should split two ways")
variant('after')
write("65-sibilant-grid.wav",
      [clip(t) for t in ("sakura", "shashin", "tsukue", "chikara")],
      "/s/ [C] /ts/ [tC] -- long/short by manner, dull/bright by place")

print("66  fricative length, before and after")
parts = []
for w, a in (("sakura", 0), ("shashin", 0), ("sensei", 3), ("desu", 0)):
    variant('before'); parts.append(clip(w, a))
    variant('after');  parts.append(clip(w, a))
variant('after')
write("66-fricative-length.wav", parts, "pairs: 7 frames, then 10")

print("67  fricative against affricate directly")
variant('after')
write("67-fric-vs-affric.wav",
      [clip(t) for t in ("sa", "cha", "sha", "tsu", "su", "chu")],
      "sa cha sha tsu su chu")
