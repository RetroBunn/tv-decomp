# -*- coding: utf-8 -*-
"""The ninth stray phoneme, and the one that was swept for rather than heard.

Run from the repository root:  python build/Japanese_test/make_voiced_sib.py

Every audible defect fixed in this Japanese work so far has been found the same
way: the user heard something wrong, said so, and it turned out that a number
measured in NOISE -- a fricative's frication F2, a nasal's murmur N2, a burst's
spectral peak -- had been used as a vocal-tract state and so had become the
start or the end of a VOICED transition.  Eight of those, one at a time.

sweep.py looks for the signature instead of waiting for the ear: a formant step
between two consecutive VOICED frames bigger than a tract can make in 10 ms.
Run over the 99 real morae in five preceding-vowel contexts it found two more.

/z/ and /j/, the only two voiced sibilants.  SIB_POST pushes F3 and F4 to the
engine's ceiling to place a sibilant peak the cascade cannot otherwise reach,
and noise_post's own docstring says those values "are a device for shaping
noise, not a vocal-tract state".  That was safe while every consumer was
voiceless -- with the voicing off, F3 at 4000 Hz shapes noise and nothing else.
/z/ and /j/ carry track 0 at 40 through their frication, so on those frames a
4000 Hz F3 is a resonance the VOICE is driving.  /uza/ ran F3 2224 -> 4000 ->
2272 across five voiced frames, a 1776 Hz excursion up and 1728 back, and
measured +20.8 dB of 4 kHz over 2 kHz against /s/'s +14.4 -- the voiced
fricative was the brighter of the pair, which is backwards.

The glides, the flap and the voiced fricatives all stepped F3 at the consonant's
first frame as well, for a different reason: LOCUS is an F2 table, so onset()
takes F3 and F4 from the FOLLOWING vowel and coda() from the PRECEDING one.
For a stop that is invisible behind a silent closure.  For a voiced continuant
it is a step on voiced frames -- /iwa/ 672 Hz, /uri/ and /iyu/ and /iryu/ 720.
Neither vowel is right, because the consonant has no F3 of its own; the
midpoint splits the move and the existing offglide and transition carry it.

   sweep.py, worst voiced-to-voiced step per consonant, Hz per 10 ms frame

                 before   after
      /z/         1776     368
      /j/          784       -
      /y/          720       -
      /r/          720     248
      /ry/         720       -
      /w/          672     304

What is left is 248-368 Hz at stop releases and glide onsets, where a tract
really does move fast, and it is left alone.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)


def mode(on):
    J.VOICED_SIB = on
    S.BRIDGE_F3 = on


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


print("89  /z/ and /j/, the voiced 4 kHz resonance taken out")
parts = []
for w in ("aza", "mizu", "kazoku", "zenbu", "aji", "jikan", "jouzu", "kanji"):
    mode(False); parts.append(clip(w))
    mode(True);  parts.append(clip(w))
mode(True)
write("89-voiced-sibilants.wav", parts, "pairs: before, then after")

print("90  against their voiceless partners, which never had the fault")
mode(True)
write("90-s-z-sh-j.wav", [clip(t) for t in ("asa", "aza", "asha", "aji",
                                            "suzu", "shiji", "kasa", "kaza")],
      "asa aza / asha aji / suzu shiji / kasa kaza")

print("91  the F3 bridge: glides and the flap between unlike vowels")
parts = []
for w in ("iwa", "uri", "iyu", "kiwa", "ariga", "oyu", "kuwa", "tori"):
    mode(False); parts.append(clip(w))
    mode(True);  parts.append(clip(w))
mode(True)
write("91-f3-bridge.wav", parts, "pairs: F3 stepping, then bridged")
