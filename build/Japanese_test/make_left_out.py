# -*- coding: utf-8 -*-
"""The things that had been flagged as left out, now built.

Run from the repository root:  python build/Japanese_test/make_left_out.py

  56  focus, from Kawai's accent sandhi (section 7.1)
  57  questions, from Hirose's interrogative command
  58  prevoicing in /b d g/
  59  the moraic nasal's place assimilation
  60  the labials, whose locus pointed at the wrong place
  61  phrase-initial /r/ as a plosive, from Arai
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.50), dtype=np.int16)

# Tanaka's row, kept so the old labials can still be rendered for comparison.
OLD_LABIAL = {'a': 1600, 'i': 2250, 'u': 1980, 'e': 1930, 'o': 1780}
NEW_LABIAL = dict(J.LOCUS['p'])


def labials(which):
    tbl = OLD_LABIAL if which == 'old' else NEW_LABIAL
    for c in ('p', 'b'):
        J.LOCUS[c] = dict(tbl)
    J._LABIAL.clear()
    J._LABIAL.update(tbl)
    J.NASAL_ORAL['m'] = J._LABIAL
    J.NASAL_ORAL['N_m'] = J._LABIAL


def clip(text, accent=0, emph=(), question=None):
    if question is None:
        question = text.rstrip().endswith('?')
    morae = M.to_morae(text)
    frames, ends, q_ends = S.build(morae)
    S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae, emph=emph,
            question=question)
    return S.render(frames)


def write(name, parts, note=""):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, GAP, p])
    with wave.open(os.path.join(HERE, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-30s %5.2f s  %s" % (name, len(x) / float(SR), note))


print("56  focus: the same sentence with the emphasis moved")
SENT, ACC = "mainichi nihongoo benkyoushimasu", [1, 0, 0]
write("56-focus.wav", [clip(SENT, ACC, e) for e in
                       ((), (1, 0, 0), (0, 1, 0), (0, 0, 1))],
      "neutral, then +Emph on each phrase in turn")

print("57  questions against statements")
parts = []
for t, a in (("ikimasu", [0]), ("wakarimasu", [0]), ("hoteru desuka", [0, 0])):
    parts.append(clip(t, a, question=False))
    parts.append(clip(t, a, question=True))
write("57-questions.wav", parts, "pairs: statement, then question")

print("58  prevoicing: the voice bar in /b d g/")
parts = []
for w in ("aba", "ada", "aga"):
    S.PREVOICE = False; parts.append(clip(w))
    S.PREVOICE = True;  parts.append(clip(w))
S.PREVOICE = True
write("58-prevoicing.wav", parts, "pairs: without the voice bar, then with")

print("59  the moraic nasal, assimilating in place")
write("59-moraic-n.wav",
      [clip(w, a) for w, a in (("kanpai", 0), ("sanka", 0), ("sanda", 0),
                               ("nihon", 2), ("nippon", 0), ("ten'in", 0))],
      "bilabial, velar, alveolar, then three with no closure at all")

print("60  the labials, old locus then corrected")
parts = []
for w in ("mame", "papa", "bobo", "ohayou"):
    labials('old'); parts.append(clip(w))
    labials('new'); parts.append(clip(w))
labials('new')
write("60-labials.wav", parts, "pairs: Tanaka's row as a locus, then corrected")

print("61  phrase-initial /r/ as a plosive")
parts = []
for w, a in (("ringo", 0), ("ryokou", 0), ("rainen", 0)):
    S.PLOSIVE_R = False; parts.append(clip(w, a))
    S.PLOSIVE_R = True;  parts.append(clip(w, a))
S.PLOSIVE_R = True
write("61-initial-r.wav", parts, "pairs: flap, then plosive-like")
