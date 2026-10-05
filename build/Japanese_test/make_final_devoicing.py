# -*- coding: utf-8 -*-
"""Fallback devoicing, before and after it was conditioned on upstream's rules.

The string-level fallback -- what romaji input and the oracle use, where the
analyser has not supplied an answer -- used to decide devoicing with one test:
a high vowel after a voiceless consonant devoices if the next onset is also
voiceless, or if nothing follows.  Upstream does neither.

  * FINALLY, njd_set_unvoiced_vowel's rule 5 opens with `if nxt is None:
    return 0`, so only rule 1 (/masu/, /desu/) and rule 2 (/shi/, which needs
    a part of speech) devoice at all.  Den & Koiso measure the same shape in
    spontaneous speech: masu 81.33%, desu 79.54% against /shi/ 1.81% and /ku/
    1.66%.
  * MEDIALLY, rule 5 runs three candidate classes, each with its own list of
    morae it devoices before, and the exclusions are the point: /su/ not
    before another s-row mora, /fu hi fi/ not before an h-row one, so two
    fricatives are never left with nothing between them.

Each pair below is the SAME word twice, old rule then new, with accent and
pitch held constant.  The first six lose a phrase-final devoicing; the next
three lose a medial one; the last two are the polite forms and MUST NOT
change -- they are the control.

    python build/Japanese_test/make_final_devoicing.py
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_mora as M                                        # noqa: E402
import jp_speak as S                                       # noqa: E402

SR = S.SR
GAP = np.zeros(int(SR * 0.30), dtype=np.int16)
LONG_GAP = np.zeros(int(SR * 0.65), dtype=np.int16)


def old_devoiced(morae):
    """The one test the fallback used to apply, as a per-mora answer.

    build() takes devoiced_in for exactly this -- a caller that has worked the
    answer out elsewhere -- so the old behaviour is reproduced by handing it
    in rather than by reaching into the module.
    """
    out = []
    for n, mo in enumerate(morae):
        nxt = morae[n + 1] if n + 1 < len(morae) else None
        c, v = M.split(mo)
        nc = M.split(nxt)[0] if nxt is not None else None
        final = nxt is None or nxt in (' ', '|', '||')
        out.append(bool(v in 'iu' and c in S.VOICELESS
                        and (nc in S.VOICELESS or final)))
    return out


def clip(text, accent=0, old=False):
    morae = M.to_morae(text)
    dv = old_devoiced(morae) if old else None
    frames, ends, q_ends = S.build(list(morae), devoiced_in=dv)
    S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae)
    return S.render(frames)


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
    print("  %-34s %5.2f s" % (name, len(x) / float(SR)))


# accent from naist-jdic, so the contour is not a variable here
FINAL = [("sushi", 2), ("fuku", 2), ("kutsu", 2), ("aki", 1),
         ("sukoshi", 2), ("tomodachi", 0)]
MEDIAL = [("supeesushatoru", 0), ("yamasuso", 0), ("fufuku", 0)]
CONTROL = [("desu", 1), ("ikimasu", 3)]


def main():
    print("fallback devoicing, old rule then new, one pair per word")
    parts = []
    for text, acc in FINAL + MEDIAL + CONTROL:
        parts.append(np.concatenate([clip(text, acc, old=True), GAP,
                                     clip(text, acc, old=False)]))
    write("90-fallback-devoicing-old-then-new.wav", parts, gap=LONG_GAP)
    print()
    print("  phrase-final changes: %s" % ', '.join(w for w, _ in FINAL))
    print("  medial changes:       %s" % ', '.join(w for w, _ in MEDIAL))
    print("  must not change:      %s" % ', '.join(w for w, _ in CONTROL))


if __name__ == '__main__':
    main()
