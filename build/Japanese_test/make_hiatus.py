# -*- coding: utf-8 -*-
"""Vowel to vowel, where there was no transition at all.

Run from the repository root:  python build/Japanese_test/make_hiatus.py

A vowel-initial mora got no transition, because the loop that writes one keyed
on there being a consonant.  Where the mora before it also ended voiced -- a
vowel, a long vowel, or the moraic nasal -- nothing interrupts the voicing, so
the tongue travels continuously and there is no boundary for a step to hide at.
`aoi` stepped F2 1228 -> 860 -> 2068: 1208 Hz in one 10 ms frame.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
_orig = S._glide_len


def variant(name):
    """'before' puts the step back by giving a vowel-initial mora no glide."""
    S.HIATUS = (name != 'before')


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


WORDS = [("aoi", 0), ("ie", 1), ("ue", 0), ("iu", 1), ("ai", 1),
         ("kaeru", 1), ("shiai", 0), ("ookii", 3), ("omoshiroi", 4)]

print("68  vowel sequences, before and after")
parts = []
for w, a in WORDS[:5]:
    variant('before'); parts.append(clip(w, a))
    variant('after');  parts.append(clip(w, a))
variant('after')
write("68-hiatus-before-after.wav", parts, "pairs: step, then glide")

print("69  words with vowel sequences")
variant('after')
write("69-hiatus-words.wav", [clip(w, a) for w, a in WORDS])

print("70  the moraic nasal before a vowel, the same case")
variant('after')
write("70-n-before-vowel.wav",
      [clip(t) for t in ("ten'in", "zen'in", "kin'en", "hon'e")])
