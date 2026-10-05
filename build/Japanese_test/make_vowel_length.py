# -*- coding: utf-8 -*-
"""Vowel length: the long vowel's target and its duration.

Run from the repository root:  python build/Japanese_test/make_vowel_length.py

Two findings, from the two papers and the dataset in jp_res/acoustics/vowels/.

Hirata & Tsukada (2003): long vowels occupy a more peripheral part of the
F1-F2 space than short ones -- a short vowel undershoots, a long one has time
to reach its target.  This code used ONE target for both.

Yazawa & Kondo (2019), 16 speakers and 3,200 tokens: the long/short DURATION
ratio is 2.24.  This code gave the second mora of a long vowel a full
MORA_FRAMES, which made the ratio 3.00.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
NEW_DELTA = dict(J.LONG_DELTA)
FLAT = dict((v, (0, 0, 0)) for v in NEW_DELTA)


def variant(name):
    """'before' = one target for both lengths, and a full mora for the hold."""
    J.LONG_DELTA.clear()
    J.LONG_DELTA.update(FLAT if name == 'before' else NEW_DELTA)
    S.LONG_FRAMES = S.MORA_FRAMES if name == 'before' else 9


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


# Length minimal pairs, dictionary accents.
PAIRS = [(("obasan", 0), ("obaasan", 2)),
         (("ie", 1),     ("iie", 3)),
         (("toru", 1),   ("tooru", 0)),
         (("biru", 1),   ("biiru", 1)),
         (("kuki", 1),   ("kuuki", 1)),
         (("seki", 1),   ("seiki", 0))]

print("62  length minimal pairs, short then long")
variant('after')
write("62-length-pairs.wav",
      [clip(w, a) for p in PAIRS for w, a in p], "short, long, short, long ...")

print("63  the same pairs before and after both changes")
parts = []
for p in PAIRS[:4]:
    for w, a in p:
        variant('before'); parts.append(clip(w, a))
        variant('after');  parts.append(clip(w, a))
variant('after')
write("63-length-before-after.wav", parts,
      "each word twice: one target and a full mora, then corrected")

print("64  the five long vowels, isolated")
variant('after')
write("64-long-vowels.wav",
      [clip(t) for t in ("baa", "bii", "buu", "bee", "boo")])
