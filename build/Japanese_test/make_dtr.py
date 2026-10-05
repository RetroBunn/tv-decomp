# -*- coding: utf-8 -*-
"""/d/ against /t/, and /d/ against /r/.

Run from the repository root:  python build/Japanese_test/make_dtr.py

Reported: "our Ds and Ts ... sound the same".  They did, and it was a
regression.  When /t/ was given the high band-6 alveolar burst from Kitazawa &
Doshita, /d/ was given the same BURST_SRC entry with track 0 at zero -- so a
VOICED stop released as voiceless noise, which is acoustically a /t/.  /b/ and
/g/ were never in that table and kept their voiced release all along, which is
why only /d/ lost the contrast.

Liu Chiu-Yen (2002), J. Phonetic Society of Japan 6(3) 69-78, is about the
neighbouring contrast, /d/ against the flap /r/, and finds that Japanese native
listeners identify them categorically using CLOSURE DURATION as the major cue
-- her continuum runs from 0 to 100 ms -- where Taiwanese learners fall back on
spike duration and spike strength as well.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
FIXED = dict(J.BURST_SRC)
BROKEN = {'t': {1: 80, 8: 40, 6: 66, 0: 0}, 'd': {1: 80, 8: 34, 6: 60, 0: 0}}


def variant(name):
    J.BURST_SRC.clear()
    J.BURST_SRC.update(BROKEN if name == 'before' else FIXED)


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


print("79  /d/ against /t/, before and after the voicing fix")
parts = []
for w in ("ada", "ata", "dada", "tata", "kodomo", "mado"):
    variant('before'); parts.append(clip(w))
    variant('after');  parts.append(clip(w))
variant('after')
write("79-d-vs-t.wav", parts, "pairs: voiceless release, then voiced")

print("80  the three-way contrast on Liu's major cue, closure duration")
variant('after')
write("80-d-t-r.wav", [clip(t) for t in ("ara", "ada", "ata",
                                         "kirei", "kidei", "kitei")],
      "ara 0 ms / ada 40 / ata 70")

print("81  words that turn on it")
variant('after')
write("81-dtr-words.wav",
      [clip(w, a) for w, a in (("kodomo", 0), ("mado", 1), ("tomodachi", 0),
                               ("daidokoro", 0), ("tatemono", 2),
                               ("dare", 1), ("tare", 1), ("are", 0))])
