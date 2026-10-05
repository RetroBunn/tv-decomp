# -*- coding: utf-8 -*-
"""Samples for the Japanese flap, from Arai's /ara/ perception study.

Run from the repository root:  python build/Japanese_test/make_r.py

Arai (LabPhon 14) synthesised 195 /ara/ stimuli varying four parameters and had
twenty listeners rate each for /r/-likeness.  The regions scoring 70% or better
split into three allophones, all of them good Japanese /r/.  These samples put
the three side by side, sweep the one parameter he left a wide range on, and
then run the default through real words.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
LONG_GAP = np.zeros(int(SR * 0.70), dtype=np.int16)


def clip(text, accent=0):
    frames, ends, q_ends = S.build(M.to_morae(text))
    S.pitch(frames, ends, accent, q_ends=q_ends)
    return S.render(frames)


def write(name, parts, gap=GAP):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, gap, p])
    path = os.path.join(HERE, name)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-28s %5.2f s" % (name, len(x) / float(SR)))


# Dictionary accents, naist-jdic field 13 as accent/mora_count.
WORDS = [("arigatou", 2), ("sakura", 0), ("chikara", 1), ("hiragana", 4),
         ("ringo", 0), ("midori", 1), ("kuruma", 0), ("sarasara", 1)]

print("41  Arai's three sub-phonemes, /ara/")
parts = []
for mode in ('flap', 'approx', 'latflap'):
    J.set_flap(mode)
    parts.append(clip("ara"))
write("41-r-subphonemes.wav", parts)

print("42  the flap, across Arai's AVd range")
parts = []
for d in (10, 25, 40):
    J.set_flap('flap', avd=d)
    parts.append(clip("ara"))
write("42-r-dip-depth.wav", parts)

print("43  before and after, per word: approximant then flap")
parts = []
for w, a in [("arigatou", 2), ("sakura", 0), ("hiragana", 4)]:
    J.set_flap('approx')
    parts.append(clip(w, a))
    J.set_flap('flap')
    parts.append(clip(w, a))
write("43-r-before-after.wav", parts)

print("44  the flap in all five vowel contexts")
J.set_flap('flap')
write("44-r-vowels.wav", [clip(t) for t in ("ara", "iri", "uru", "ere", "oro")])

print("45  words, default flap, dictionary accents")
J.set_flap('flap')
write("45-r-words.wav", [clip(w, a) for w, a in WORDS])

# /ry/ had no MANNER and no LOCUS entry, so rya/ryu/ryo came out with no flap
# in them at all -- 1.02% of consonant onsets over all 486,646 pronounced
# naist-jdic entries, silently not pronounced.  These are the words that were
# wrong.
print("46  the palatalised flap, which was missing entirely")
RY = [("ryokou", 0), ("ryouri", 1), ("ryokan", 1), ("ryuukou", 0),
      ("ryouhou", 0)]
write("46-ry-words.wav", [clip(w, a) for w, a in RY])

J.set_flap()
print("\nflap parameters: %s" % J.FLAP)
print("LOCUS['r']     : %s" % J.LOCUS['r'])
