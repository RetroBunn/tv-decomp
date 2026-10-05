# -*- coding: utf-8 -*-
"""Find the next stray phoneme before anyone has to hear it.

Run from the repository root:  python build/Japanese_test/sweep.py

Every audible defect this project has fixed in Japanese has been the same
shape: a formant jumping somewhere a tract cannot move, because a number
measured in NOISE -- a fricative's frication F2, a nasal's murmur N2, a burst's
spectral peak, a flap's contact posture -- was used as a vocal-tract state and
so became the start or end of a VOICED transition.  Eight of them were found by
the user's ear, one at a time, after the fact.

The ear is still the judge, but the defect has a signature that can be swept
for: a step between two consecutive VOICED frames that is larger than a tract
can make in 10 ms.  Fant's figure for formant movement is of the order of
1-2 kHz per 100 ms at the fastest, so ~200 Hz per frame is the ceiling and
anything well above it is either a deliberate step or a bug.

Two kinds of step here are deliberate and are excluded by name:

  * the nasal release, where the murmur's nasal-cavity resonances and the
    vowel's oral ones swap rather than glide.  That step is real, it is placed
    at murmur amplitude on purpose, and it is sourced.
  * the flap's contact, which Arai measures as instantaneous -- his F2 track is
    a triangle with no steady top.

Everything else that shows up here is a candidate defect.
"""
import os, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

VOWELS = 'aiueo'
CONS = ['', 'k', 'g', 's', 'z', 't', 'd', 'n', 'h', 'b', 'p', 'm', 'y', 'r',
        'w', 'f', 'j', 'v', 'ky', 'gy', 'ny', 'hy', 'by', 'py', 'my', 'ry',
        'sh', 'ch', 'ts']
LIMIT = 200.0            # Hz per 10 ms frame; above this a tract cannot move
MURMUR = 52              # NASAL_A's track-0 level: the deliberate nasal step
BAR = 32                 # VOICE_BAR's: a shut tract, which radiates no F2
FLAP = ('r', 'ry')       # Arai measures the contact as instantaneous

# Sweeping the full cross product invents morae Japanese does not have -- /zi/,
# /yi/, /wi/, /ryi/, /ti/, /tu/ -- and they flagged, which sent me looking at
# defects in sounds that are never synthesised.  The real inventory is whatever
# the kana table can actually produce, so it is read back from there rather
# than written out by hand.
def _real():
    """Not M.to_morae: its romaji reader accepts any consonant plus any vowel,
    so asking it whether /zi/ is a mora gets a yes.  The kana table and the
    onset list are the two places that actually enumerate the inventory."""
    out = set(M.KANA.values()) | set(M._ONSETS)
    out |= set(['fa', 'fi', 'fe', 'fo'])     # the small-vowel digraphs /f/ takes
    return set(m for m in out if len(m) >= 2 and m[-1] in VOWELS)


def steps(morae):
    """-> list of (Hz, formant, frame) for voiced-to-voiced formant steps."""
    frames, ends, q = S.build(list(morae))
    S.pitch(frames, ends, 0, q_ends=q, morae=list(morae))
    out = []
    for i in range(1, len(frames)):
        a, b = frames[i - 1], frames[i]
        if not (a[0] > 0 and b[0] > 0):
            continue                      # at least one frame is unvoiced
        if MURMUR in (a[0], b[0]):
            continue                      # the nasal release, deliberate
        if BAR in (a[0], b[0]):
            # A prevoiced stop's closure.  The formants still move through it,
            # because the tongue really is travelling -- /igo/ runs F2 from the
            # velar locus for /i/ to the one for /o/, which is the velar pinch
            # and is the place cue Kashino measures.  None of it is radiated:
            # voice_bar() sets F1 to 200 and all three bandwidths to 250, and
            # the rendered closure measures 99.0% of its energy below 500 Hz
            # and 0.6% between 1 and 3 kHz, 9.3 dB down on the vowel.  So the
            # move is bookkeeping for where the articulator is, not a
            # transition anyone hears, and smoothing it would cost closure
            # duration -- which Homma measured -- to fix an inaudible number.
            continue
        for name, k, scale in (('F1', 9, 4.0), ('F2', 10, 8.0),
                               ('F3', 11, 16.0)):
            out.append((abs(b[k] - a[k]) * scale, name, i))
    return out


def main():
    real = _real()
    worst = collections.defaultdict(lambda: (0.0, '', '', 0))
    n_mora = 0
    for c in CONS:
        for v in VOWELS:
            if c + v not in real:
                continue                          # not a mora of Japanese
            n_mora += 1
            for pv in VOWELS:                     # VCV: the transition context
                mo = [pv, c + v]
                for d, f, i in steps(mo):
                    if f == 'F2' and c in FLAP:
                        continue                  # Arai's instantaneous contact
                    if d > worst[c or '(none)'][0]:
                        worst[c or '(none)'] = (d, f, pv + '-' + c + v, i)

    rows = sorted(worst.items(), key=lambda kv: -kv[1][0])
    flagged = [r for r in rows if r[1][0] > LIMIT]
    print("VCV sweep: %d real morae x 5 preceding vowels" % n_mora)
    print('flagging voiced-to-voiced formant steps above %d Hz per frame.' % LIMIT)
    print('the nasal release and the flap contact are excluded by name.\n')
    if not flagged:
        print('  nothing above the limit.')
    for c, (d, f, word, i) in flagged:
        print('  %-6s %6.0f Hz  %s  in %-8s at frame %d' % (c, d, f, word, i))

    # every row, not rows[:12]: thirteen categories tie at 200, so a slice
    # of twelve is all 200s and reads as though the whole inventory were at
    # the limit.  It is 13 of 27; /d/ runs at 96.
    print('\nworst remaining, consonant by consonant:')
    for c, (d, f, word, i) in rows:
        print('  %-6s %6.0f Hz  %s  %s' % (c, d, f, word))


if __name__ == '__main__':
    main()
