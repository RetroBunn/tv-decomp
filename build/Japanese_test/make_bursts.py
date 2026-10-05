# -*- coding: utf-8 -*-
"""Stop bursts: the gross spectral shape that carries place.

Run from the repository root:  python build/Japanese_test/make_bursts.py

Kitazawa & Doshita (1984), "Discrimination of Japanese voiceless stop
consonants by burst spectrum", J. Acoust. Soc. Japan.  28 male speakers, /p t
k/ in CV, averaged critical-band spectra up to 9.25 kHz, and vowel- and
speaker-independent discriminant functions that classify place at over 80%,
about 90% with the vowel known.  Their Fig. 2(a):

    [p]  flat, with little low-frequency energy
    [t]  high-rising, peak at 5.3 kHz
    [k]  compact: 1.2 kHz before back vowels, 4.7 kHz before front

This had /t/ FLAT, which is one arm of an either/or in Morikawa that this
paper settles the other way.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
FLAT_T = (900, 1800, 2800, 3800, 420, 520, 620)


def t_variant(name):
    """'before' restores the flat /t/ burst."""
    if name == 'before':
        J.BURST_SRC.clear()
    else:
        J.BURST_SRC.clear()
        J.BURST_SRC.update({'t': {1: 80, 8: 40, 6: 66, 0: 0},
                            'd': {1: 80, 8: 34, 6: 60, 0: 0}})
    S._T_FLAT = (name == 'before')


# burst() has no flat-/t/ switch, so swap the function for the before case.
_orig_burst = J.burst
def _burst(c, v):
    if getattr(S, '_T_FLAT', False) and c in ('t', 'd'):
        return FLAT_T
    return _orig_burst(c, v)
J.burst = _burst


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


print("75  /t/ burst, flat then rising")
parts = []
for w in ("ta", "te", "to", "tabemasu", "tokei"):
    t_variant('before'); parts.append(clip(w))
    t_variant('after');  parts.append(clip(w))
t_variant('after')
write("75-t-burst.wav", parts, "pairs: flat, then rising to 5.3 kHz")

print("76  the three places together, which is what the paper discriminates")
t_variant('after')
write("76-ptk-bursts.wav", [clip(t) for t in ("pa", "ta", "ka",
                                              "pi", "ti", "ki",
                                              "po", "to", "ko")],
      "pa ta ka / pi ti ki / po to ko")

print("77  /p/, where Morikawa and Kitazawa disagree")
parts = []
for w in ("pa", "pi", "po", "pan"):
    J.P_FLAT = False; parts.append(clip(w))
    J.P_FLAT = True;  parts.append(clip(w))
J.P_FLAT = False
write("77-p-burst.wav", parts, "pairs: Morikawa falling, then Kitazawa flat")

print("78  /k/ before front vowels, 3.1 kHz against his 4.7")
parts = []
for w in ("ki", "ke", "kirei", "kesa"):
    J.K_FRONT = 0;    parts.append(clip(w))
    J.K_FRONT = 4700; parts.append(clip(w))
J.K_FRONT = 0
write("78-k-front.wav", parts, "pairs: per-vowel table, then 4.7 kHz")
