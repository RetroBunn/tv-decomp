# -*- coding: utf-8 -*-
"""A geminated fricative, as a closure and as what it actually is.

A geminate STOP is a silence -- that is the closure, and it is the length cue
Yanagisawa & Arai measured.  A geminate FRICATIVE has no closure phase at all:
/ʃː/ in スラッシュ is one long noise, roughly twice the singleton.  Rendering it
as a silence followed by a singleton's worth of frication builds the signature
of an affricate instead, which is what was heard -- スラッシュ as "surachu".

Each pair is the same word twice, old then new, through the analyser so the
accent and devoicing are the real ones.  The last two are STOP geminates and
must not change: they are the control.

    python build/Japanese_test/make_geminate_fric.py
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_front as F                                       # noqa: E402
import jp_speak as S                                       # noqa: E402

SR = S.SR
GAP = np.zeros(int(SR * 0.30), dtype=np.int16)
LONG_GAP = np.zeros(int(SR * 0.65), dtype=np.int16)

FRICATIVE = [
    ('スラッシュ', 'slash, the one that was reported'),
    ('雑誌',       'zasshi -- magazine'),
    ('一緒',       'issho -- together'),
    ('真っ直ぐ',   'massugu -- straight'),
    ('あっさり',   'assari'),
    ('決して',     'kesshite -- never'),
]
STOP = [
    ('学校', 'gakkou -- a stop geminate, must not change'),
    ('切手', 'kitte  -- likewise'),
]


def clip(text, old):
    """Through the analyser, so accent and devoicing are the real ones."""
    S.FRIC_GEMINATE = not old
    try:
        w, morae, acc, dv, q, st = F.analyse(text, None)
        fr, ends, q_ends = S.build(list(morae), devoiced_in=dv)
        S.pitch(fr, ends, acc[0] if acc else 0, q_ends=q_ends, morae=morae,
                final=not q)
        return S.render(fr)
    finally:
        S.FRIC_GEMINATE = True


def write(name, parts, gap=GAP):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, gap, p])
    path = os.path.join(HERE, name)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-36s %5.2f s" % (name, len(x) / float(SR)))


def main():
    print("geminate fricatives, closure then frication, one pair per word")
    parts = []
    for text, why in FRICATIVE + STOP:
        parts.append(np.concatenate([clip(text, True), GAP,
                                     clip(text, False)]))
        print("    %-10s %s" % (text, why))
    write("91-geminate-fricative-old-then-new.wav", parts, gap=LONG_GAP)


if __name__ == '__main__':
    main()
