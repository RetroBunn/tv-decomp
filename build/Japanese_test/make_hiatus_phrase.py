# -*- coding: utf-8 -*-
"""Hiatus across a phrase boundary, where the two vowels are the same.

Run from the repository root:  python build/Japanese_test/make_hiatus_phrase.py

Kitazawa & Kiriyama (2004).  When an accentual phrase ends in a vowel and the
next begins with the SAME vowel -- ga-aru, wa-ame, to-omou -- there is no
formant movement to mark the boundary, so the glide added for `aoi` does
nothing and the two vowels run together as one long one.  What marks it
instead, in their 45 hiatus from the Japanese MULTEXT corpus:

  17 clear glottalization, 23 weak, 5 phrase-final nasalization    (89% glottal)
  F0 falls with the open quotient, 235 -> 142 -> 178 Hz
  the phrase-final vowel : phrase-initial vowel duration ratio is 1.7
  with the following phrase emphasised the ratio falls to about 0.76
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.50), dtype=np.int16)


def variant(name):
    S.GLOTTAL = (name != 'before')
    S.HIATUS_RATIO = 1.0 if name == 'before' else 1.7


def clip(text, accent=0, emph=()):
    morae = M.to_morae(text)
    frames, ends, q_ends = S.build(morae, emph=emph)
    S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae, emph=emph)
    return S.render(frames)


def write(name, parts, note=""):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, GAP, p])
    with wave.open(os.path.join(HERE, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-32s %5.2f s  %s" % (name, len(x) / float(SR), note))


# His own examples, by type.  Particle-vowel is the commonest.
PARTICLE = [("wa ame", [0, 0]), ("ga aru", [0, 0]), ("to omou", [0, 2]),
            ("te ekizo", [0, 0]), ("no otaku", [0, 0])]
ADJECTIVE = [("mada atarashii", [1, 4]), ("moshi ikite", [1, 0])]
WORDWORD = [("komugi iro", [0, 0]), ("takushii ichidai", [1, 0])]

print("71  his particle-vowel examples, before and after")
parts = []
for w, a in PARTICLE:
    variant('before'); parts.append(clip(w, a))
    variant('after');  parts.append(clip(w, a))
variant('after')
write("71-hiatus-phrase.wav", parts, "pairs: one long vowel, then a boundary")

print("72  adjective-vowel and word-word, which he says glottalize more")
variant('after')
write("72-hiatus-types.wav",
      [clip(w, a) for w, a in ADJECTIVE + WORDWORD])

print("73  the duration ratio, neutral then with the second phrase emphasised")
variant('after')
parts = []
for w, a in PARTICLE[:3]:
    parts.append(clip(w, a))
    parts.append(clip(w, a, emph=(0, 1)))
write("73-hiatus-emphasis.wav", parts, "pairs: ratio 1.7, then 0.76")

# Kitazawa 2006 groups his particles by which cue marks the boundary: ni|i and
# no|o by the +/-nasal contrast, wa|a and to|o by glottalization.  The split is
# just whether the particle's consonant is nasal.
print("74  his two cue groups, nasal contrast against glottalization")
variant('after')
write("74-hiatus-cues.wav",
      [clip(w, a) for w, a in (("ni itte", [0, 0]), ("no otaku", [0, 0]),
                               ("wa ame", [0, 0]), ("to omou", [0, 2]))],
      "ni|i no|o nasal, then wa|a to|o glottal")
