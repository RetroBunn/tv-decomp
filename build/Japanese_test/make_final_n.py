# -*- coding: utf-8 -*-
"""Word-final /N/, which was not a nasal at all.

Run from the repository root:  python build/Japanese_test/make_final_n.py

Reported by ear on 97-length-minimal-pairs: "is it trying to say Obasan and
Ojisan?  The N sounds like a softer A."  It was.  moraic_n() returned None for
three different situations and the /N/ branch treated all three the same way --
keep the preceding vowel's posture, widen the bandwidths, drop 8 dB:

    /obasan/   F1 736  F2 1228   held for the whole final mora
               which is the /a/ in front of it, quieter

Two of those three situations are right.  Before a vowel and before /h/ there
is no oral closure and the realisation IS a nasalised vowel.  Utterance-final
is not that case: the final allophone is uvular, there is a real constriction,
and the formants have somewhere to go.  Folding it in with the other two meant
final /N/ had nasal AMPLITUDE but no nasal RESONANCE, and the murmur is the
whole reason a nasal sounds nasal.

The murmur was already there and already sourced -- N1 250-300, N2 ~1000 from
the general literature -- and every /N/ before a consonant has always used it.
Only the final one bypassed it.

    /obasan/   F1 664 -> 300 over five frames, F2 held at 1228
               then the murmur: F1 272, F2 1004, at murmur amplitude

F1 does the closing, which is place-independent and real.  F2 stays where the
vowel left it, because F2 carries PLACE and there is no measured uvular locus.
Borrowing the velar row was tried and points the wrong way -- the velar locus
for /a/ is 1530, above /a/'s own 1225, so the velar pinch RAISES F2 going in
where a uvular has to lower it.  That is not an approximation, it is the
opposite sign.  Final /N/ has no place contrast to signal anyway, since it is
the only nasal that occurs there, so nothing is lost by declining to guess.
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


print("99  the words it was reported on")
parts = []
for w, a in (("obasan", 0), ("ojisan", 0), ("obaasan", 2), ("ojiisan", 2)):
    S.FINAL_N = False; parts.append(clip(w, a))
    S.FINAL_N = True;  parts.append(clip(w, a))
S.FINAL_N = True
write("99-final-n.wav", parts, "pairs: before, then after")

print("100  more words ending in /N/")
parts = []
for w, a in (("nihon", 0), ("mikan", 0), ("kaban", 0), ("shinbun", 0),
             ("sensei", 0), ("hon", 1), ("san", 0), ("gohan", 0)):
    S.FINAL_N = False; parts.append(clip(w, a))
    S.FINAL_N = True;  parts.append(clip(w, a))
S.FINAL_N = True
write("100-final-n-words.wav", parts, "pairs: before, then after")

print("101  the three cases, which must stay distinct")
S.FINAL_N = True
write("101-n-three-ways.wav",
      [clip(t, a) for t, a in (("renai", 0), ("hoan", 0),      # before a vowel
                               ("sanpo", 0), ("ginkou", 0),    # homorganic
                               ("nihon", 0), ("mikan", 0))],   # final
      "before a vowel / homorganic / final")
