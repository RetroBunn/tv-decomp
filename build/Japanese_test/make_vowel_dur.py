# -*- coding: utf-8 -*-
"""Per-vowel duration targets, replacing a fitted constant.

Run from the repository root:  python build/Japanese_test/make_vowel_dur.py

Vowel length used to be a residual: the mora had a fixed budget, the consonant
took what it needed, and the vowel got the rest.  Nothing in that chain knew
which vowel it was, so every vowel came out the same length -- 68-73 ms where
Yazawa & Kondo's five run 63 to 74 -- and in close to the opposite order.  The
second mora of a long vowel was one constant, LONG_FRAMES, refitted four times
against a long/short RATIO.

The timing model now states durations in milliseconds, per vowel, from their
released per-token data, and derives frames from them.  A ratio is an outcome,
not an input.

                       ours        theirs
      /i/  short      59.5          63.2
      /e/             69.5          72.2
      /a/             77.1          74.4
      /o/             68.0          70.6
      /u/             61.0          63.7
      /i/  long      132.6         141.5
      /e/            142.4         148.4
      /a/            150.9         158.0
      /o/            141.0         147.5
      /u/            134.2         143.4

      ordering, short and long:  /a/ > /e/ > /o/ > /u/ > /i/ -- theirs exactly
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)


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


print("95  the five vowels, flat then intrinsic")
parts = []
for w in ("bipe", "bepe", "bape", "bope", "bupe"):
    S.V_INTRINSIC = False; parts.append(clip(w, 1))
    S.V_INTRINSIC = True;  parts.append(clip(w, 1))
S.V_INTRINSIC = True
write("95-vowel-duration.wav", parts, "pairs: flat, then per-vowel")

print("96  long vowels, where a constant became five targets")
parts = []
for w in ("biipe", "beepe", "baape", "boope", "buupe"):
    S.V_INTRINSIC = False; parts.append(clip(w, 1))
    S.V_INTRINSIC = True;  parts.append(clip(w, 1))
S.V_INTRINSIC = True
write("96-long-vowels.wav", parts, "pairs: LONG_FRAMES = 6, then per-vowel")

print("97  the contrast in real words")
parts = []
for w, a in (("obasan", 0), ("obaasan", 2), ("ojisan", 0), ("ojiisan", 2),
             ("kado", 1), ("kaado", 1), ("toru", 1), ("tooru", 1)):
    S.V_INTRINSIC = False; parts.append(clip(w, a))
    S.V_INTRINSIC = True;  parts.append(clip(w, a))
S.V_INTRINSIC = True
write("97-length-minimal-pairs.wav", parts, "pairs: before, then after")

print("98  connected speech")
S.V_INTRINSIC = True
write("98-sentences-vdur.wav",
      [clip(t, a) for t, a in (
          ("ohayou gozaimasu", 0),
          ("kyou wa | ii tenki desu ne", 0),
          ("obaasan wa | tooku ni | sunde imasu", 0),
          ("arigatou gozaimashita", 0))],
      "four utterances")
