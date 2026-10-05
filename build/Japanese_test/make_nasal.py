# -*- coding: utf-8 -*-
"""Samples for the nasal transitions.

Run from the repository root:  python build/Japanese_test/make_nasal.py

The murmur of a nasal and the transitions either side of it are two different
things.  Fujimura's N2 ~1000 Hz is a resonance of the NASAL CAVITY, measured
with the oral tract shut; it describes the murmur and nothing else.  The
formants that move into and out of the nasal belong to the ORAL tract, and they
are governed by where the oral closure is -- alveolar for [n], the same place
as /t/.  Using the murmur's N2 as the transition's locus made /ne/ sweep F2
from 1004 to 1876 in 50 ms, and that sweep is the extra segment heard between
the /n/ and the /e/.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
LONG = np.zeros(int(SR * 0.75), dtype=np.int16)

# The murmur postures, kept so the old behaviour can still be rendered.
_MURMUR = dict(J.NASAL_ORAL)


def variant(name):
    """'before' puts the transition back on the murmur's N2."""
    if name == 'before':
        J.NASAL_ORAL.clear()
    else:
        J.NASAL_ORAL.clear()
        J.NASAL_ORAL.update(_MURMUR)
    S.NASAL_RELEASE = (name != 'nostep')


def clip(text, accent=0):
    morae = M.to_morae(text)
    frames, ends, q_ends = S.build(morae)
    S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae)
    return S.render(frames)


def write(name, parts, gap=GAP):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, gap, p])
    with wave.open(os.path.join(HERE, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-30s %5.2f s" % (name, len(x) / float(SR)))


WORDS = [("neko", 1), ("onegai", 0), ("namae", 0), ("ane", 0),
         ("minna", 3), ("nihon", 2), ("yomimasu", 3), ("nyuusu", 1)]

print("50  /ne/ before and after, the one that was reported")
parts = []
for w in ("neko", "ane", "onegai"):
    variant('before'); parts.append(clip(w))
    variant('after');  parts.append(clip(w))
write("50-ne-before-after.wav", parts)

print("51  the n row, all five vowels, after")
variant('after')
write("51-n-row.wav", [clip(t) for t in ("nana", "nini", "nunu", "nene", "nono")])

print("52  the m row, which had the same error")
parts = []
for w in ("mame", "mimi", "momo"):
    variant('before'); parts.append(clip(w))
    variant('after');  parts.append(clip(w))
write("52-m-before-after.wav", parts)

print("53  the release step: with a frame of its own, then without")
parts = []
for w in ("neko", "namae"):
    variant('after');  parts.append(clip(w))
    variant('nostep'); parts.append(clip(w))
write("53-nasal-release.wav", parts)

print("54  words, after")
variant('after')
write("54-nasal-words.wav", [clip(w, a) for w, a in WORDS])

variant('after')
