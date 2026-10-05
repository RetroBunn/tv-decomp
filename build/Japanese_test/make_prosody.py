# -*- coding: utf-8 -*-
"""Samples for sentence-level prosody: phrasing, downstep, final lowering.

Run from the repository root:  python build/Japanese_test/make_prosody.py

Until now everything this synthesiser could say was one accent phrase with one
Fujisaki phrase command, so it could say words and not sentences.  Three things
changed, each from a paper in jp_res/prosody:

  Kawai, Hirose & Fujisaki (1994)   the phrase symbols P1/P2/P3, the L1 = 5
                                    mora threshold, and P0 = -0.50, the
                                    sentence-final lowering that was missing
  Kubozono (1987) ch. 5             downstep: accent-induced, chained within
                                    the major phrase, ratio 0.895

Input syntax: a space is an accent phrase boundary (no pause, downstep chains
across it), '|' is a major phrase boundary (pause, new phrase command, downstep
resets), '||' is the stronger clause-level one.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.55), dtype=np.int16)


def clip(text, accent, final=True, downstep=None, predicate=True):
    keep = S.DOWNSTEP
    S.FINAL_PREDICATE = predicate
    if downstep is not None:
        S.DOWNSTEP = downstep
    morae = M.to_morae(text)
    frames, ends, q_ends = S.build(morae)
    hz = S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae, final=final)
    S.DOWNSTEP = keep
    S.FINAL_PREDICATE = True
    return S.render(frames), morae, hz


def write(name, items, note=""):
    parts, x = [], None
    for it in items:
        a, morae, hz = clip(*it)
        print("    %-34s %-30s F0 %3.0f-%3.0f" %
              (it[0], " ".join(m for m in morae if m not in (' ',)), min(hz), max(hz)))
        parts.append(a)
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, GAP, p])
    path = os.path.join(HERE, name)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-30s %5.2f s  %s" % (name, len(x) / float(SR), note))


# Dictionary accents throughout (naist-jdic field 13, accent/mora_count).
print("47  sentences -- more than one accent phrase, which was not possible before")
write("47-sentences.wav", [
    ("kyouwa ii tenkidesune",            [1, 1, 0]),
    ("watashiwa gakuseidesu",            [0, 0]),
    ("mainichi nihongoo benkyoushimasu", [1, 0, 0]),
    ("kyouwa|tanoshii hanashio kikimashita", [1, 3, 3, 0]),
])

print("48  Kubozono (23), his own minimal pair for the downstep trigger")
write("48-downstep.wav", [
    ("umai nomimono", [2, 2]),      # uma'i  accented -> triggers downstep
    ("amai nomimono", [0, 2]),      # amai   unaccented -> does not
    ("umai nomimono", [2, 2], True, 1.0),   # the same, downstep disabled
], "a/b differ by the trigger, b/c by the model")

print("49  Kawai's P0, the sentence-final lowering that was never emitted")
write("49-final-lowering.wav", [
    ("konnichiwa",            [0], False),
    ("konnichiwa",            [0], True),
    ("watashiwa gakuseidesu", [0, 0], False),
    ("watashiwa gakuseidesu", [0, 0], True),
], "pairs: without P0, then with")

# Hirose et al.: the predicate phrase of a declarative takes a reduced accent
# command toward the end.  Kawai clamps the phrase component at zero once P0
# has driven it there, so this -- not an unbounded P0 -- is what ends a
# sentence.  Three per word: neither, P0 only, then P0 with the M grade.
print("55  how a declarative ends: P0 alone, then with the reduced predicate")
write("55-predicate-lowering.wav", [
    ("watashiwa gakuseidesu", [0, 0], False, None, False),
    ("watashiwa gakuseidesu", [0, 0], True,  None, False),
    ("watashiwa gakuseidesu", [0, 0], True,  None, True),
    ("kyouwa ii tenkidesune", [1, 1, 0], False, None, False),
    ("kyouwa ii tenkidesune", [1, 1, 0], True,  None, False),
    ("kyouwa ii tenkidesune", [1, 1, 0], True,  None, True),
], "triples: neither, P0 only, P0 + M grade")
