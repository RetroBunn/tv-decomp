# -*- coding: utf-8 -*-
"""/f/ = [P], the last consonant that was resting on a pure estimate.

Run from the repository root:  python build/Japanese_test/make_phi.py

Scott Ruddell, "An acoustic study of the Japanese voiceless bilabial
fricative", San Francisco State University.  Ten native speakers read a word
list of loan, Sino-Japanese and native words beginning with /f/, in the carrier
"sore wa ___ desu"; the initial consonant was identified by ear and checked
against the spectrogram, with amplitude binned over 1-7, 7-13 and 13-20 kHz.

His conclusion is partly negative and worth quoting rather than hiding: "there
can be no concise label of the phonetic realization of /f/."  FOUR sounds came
back where he expected two -- [f], [P], [h], and a blend he had to give its own
category, [P/h].  So /f/ does not get a posture here; it gets four, and a rule
for choosing between them.

What the paper does NOT establish, and this does not pretend to:
  * anything about intervocalic /f/.  "Due to the limited scope of this paper,
    intervocalic /f/ sounds were not observed."  Every token is word-initial.
  * a band frequency for /fa fi fe fo/.  From his 4.2 on he looks only at /fu/,
    so the 1.7 kHz band is a /fu/ measurement and the other four vowels take a
    coarticulated posture instead.
  * absolute levels.  He could not hold mic distance constant and says so, so
    the levels here are relative, derived from his four-bin histograms.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
OLD_LOCUS = {'a': 1300, 'i': 1600, 'u': 1100, 'e': 1400, 'o': 1100}
NEW_LOCUS = dict(J.LOCUS['f'])
_mode = J.phi_mode


def forced(m):
    """Pin every /f/ to one realization, to hear the categories apart."""
    J.phi_mode = (lambda v, nxt, dv=False: m) if m else _mode


def before():
    """The single aspirated posture /f/ had before the paper."""
    S.PHI = False
    J.LOCUS['f'] = OLD_LOCUS


def after():
    S.PHI = True
    J.LOCUS['f'] = NEW_LOCUS
    forced(None)


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


print("84  the four sounds he found, each pinned onto the same word")
after()
parts = []
for m in ('f', 'P', 'P/h', 'h'):
    forced(m)
    parts.append(np.concatenate([clip('fuku'), GAP, clip('fune')]))
after()
write("84-phi-four-ways.wav", parts, "[f] / [P] / [P/h] / [h], fuku + fune")

print("85  initial lip closure -- his Fig. 10, and the whole point of the paper")
parts = []
for w in ("fuku", "futon", "fune", "fuyu"):
    forced('P/h'); parts.append(clip(w))     # lips already set: band from frame 1
    forced('P');   parts.append(clip(w))     # lips still moving: band fades in
after()
write("85-lip-closure.wav", parts,
      "pairs: steady lips [P/h], then the closing lips of [P]")

print("86  the rule picking for itself, on his own word list")
after()
write("86-phi-words.wav",
      [clip(w, a) for w, a in (("futon", 0), ("fushigi", 0), ("futarusan", 0),
                               ("fuku", 2), ("fune", 2), ("fuan", 2),
                               ("fuyu", 2), ("fuufu", 1))],
      "futon fushigi futarusan / fuku fune fuan / fuyu fuufu")

print("87  before and after, which is one posture against four")
parts = []
for w in ("fu", "fune", "fuku", "fuan", "fushigi", "fuufu"):
    before(); parts.append(clip(w))
    after();  parts.append(clip(w))
after()
write("87-phi-before-after.wav", parts, "pairs: single aspirated /f/, then Ruddell")

print("88  the loan sequences, where the locus moved from estimate to /p/'s row")
parts = []
for w in ("fan", "firumu", "feruto", "forio", "fa", "fi", "fe", "fo"):
    before(); parts.append(clip(w))
    after();  parts.append(clip(w))
after()
write("88-phi-loanwords.wav", parts, "pairs: estimated bilabial locus, then /p/'s")
