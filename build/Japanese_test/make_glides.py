# -*- coding: utf-8 -*-
"""/w/ and /y/, the last two consonants that had no source.

Run from the repository root:  python build/Japanese_test/make_glides.py

Kariyasu (2003), J. Kyushu Univ. of Health and Welfare 4:275-281: eleven
speakers, /awa/ and /aja/, formant movement rate per glide side.  /w/'s F2
moves SLOWER than its own F1 (3.47 against 4.49 kHz/s) where /j/'s moves
faster (8.61).  This had /w/ at 700 Hz, an English [w], which made its F2 rate
exceed its F1 rate -- the wrong way round.
"""
import os, sys, wave
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M
SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
NEW = dict(J.LOCUS['w'])
OLD = {'a': 700, 'i': 800, 'u': 700, 'e': 750, 'o': 700}

def variant(n):
    J.LOCUS['w'].clear(); J.LOCUS['w'].update(OLD if n == 'before' else NEW)

def clip(t, a=0):
    mo = M.to_morae(t); fr, e, q = S.build(mo)
    S.pitch(fr, e, a, q_ends=q, morae=mo)
    return S.render(fr)

def write(name, parts, note=""):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, GAP, p])
    with wave.open(os.path.join(HERE, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-28s %5.2f s  %s" % (name, len(x) / float(SR), note))

print("82  /w/, before and after")
parts = []
for t, a in (("awa", 0), ("wata", 0), ("kawa", 2), ("watashi", 0), ("kowai", 2)):
    variant('before'); parts.append(clip(t, a))
    variant('after');  parts.append(clip(t, a))
variant('after')
write("82-w-before-after.wav", parts, "pairs: English [w], then the Japanese labial-velar")

print("83  the two glides together")
variant('after')
write("83-glides.wav", [clip(t) for t in ("awa", "aya", "wa", "ya",
                                          "yama", "kawa", "oyu", "kuwa")])
