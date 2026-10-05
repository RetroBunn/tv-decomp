# -*- coding: utf-8 -*-
"""Vowel duration against Yazawa & Kondo (2019), per vowel, by their definition.

Run from the repository root:  python build/Japanese_test/check_vowel_length.py

This exists because vowel length drifted and was refitted FOUR times against a
number nobody had written down.  README.txt recorded the synthesiser at a
long/short ratio of 2.20 against "Yazawa's 2.24"; when that was finally checked
it came out 1.80, and then 1.50 after an unrelated smoothing change gave the
short vowel one more frame.  Worse, three plausible readings of "vowel
duration" gave 1.18, 1.50 and 2.50 on the same utterance -- so there was no
fact to drift FROM.  A measurement that is not written down cannot be regressed,
and gets refitted by whoever last looked at it.

So the definition is theirs, quoted in measure.vowel_duration, and the stimuli
are theirs too.

THE STIMULI.  Their 2.2: disyllabic nonsense words /C1V1C2V2/ read in Japanese
orthography.  V1 is the target -- short /i e a o u/ and long /ii ee aa oo uu/.
Five consonantal contexts: /bV1p/, /dV1t/, /gV1k/, /zV1s/, /hV1d/.  V2 is /e/
or /o/, "to minimize its influence on V1".  Accent on the first syllable.  Note
that every C1 but /h/ is voiced and every C2 is flanked so the target never
devoices -- that looks deliberate, and it means this harness needs no special
handling for devoicing.

Four of their fifty combinations are not morae Japanese writes -- /di/, /du/,
/zi/ (which is ji) and /hu/ (which is fu) -- so those are synthesised directly
from the consonant and vowel rather than through the kana reader, and are
marked in the output.  They are still the right acoustic context; they are just
not words.

THE TARGET.  Their appendix, male speakers, which is the column that matters
because this voice is male:

              /i/  /e/  /a/  /o/  /u/
    short      68   76   78   74   68   ms
    long      147  156  166  153  150   ms
    ratio    2.16 2.05 2.13 2.07 2.21

The female column runs 2.27-2.44.  The mean of all ten per-vowel ratios is
2.24, which is where README.txt's target came from -- it is the MALE AND FEMALE
mean, and fitting a male voice to it is fitting to the wrong column.  The male
mean is 2.12.

AND THE RATIO IS NOT THE TARGET.  Their own data show vowel identity matters:
/a/ is systematically the longest and /i/ and /u/ the shortest, short and long
alike, and they argue against reading long as simply twice short.  Three things
stay separate here:

    PHONOLOGY          short = 1 mora, long = 2 morae
    TIMING MODEL       mora duration, speaking rate, prosodic position,
                       vowel-dependent realization
    ACOUSTIC MEASURE   first positive zero crossing -> last positive zero
                       crossing

A regression on the ratio alone would quietly collapse the first two into the
third.  So this reports per-vowel durations, and the ratio only as a summary.
"""
import os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M, measure

# From their RELEASED DATASET (JPLongShortVowels.csv, 3,200 tokens), not the
# paper's rounded appendix: male speakers, EMBEDDED position, and -- the part
# that was wrong until Astra caught it -- averaged over THE SAME THREE
# CONSONANT CONTEXTS this harness measures.
#
# The reference has to match the measurement on every factor, and it did not.
# The harness averages our output over /bVp/, /dVt/ and /gVk/, because the
# other two of their five have an onset boundary it cannot place.  The targets
# were averaged over all five.  Comparing a three-context mean against a
# five-context mean is not a comparison, and it mattered: restricted to the
# same three, /i/ rises 63.2 -> 67.8 and /a/ falls 74.4 -> 76.3, which is
# enough to reverse the sign of the /a/ error and turn an apparently uniform
# residual into one that runs from +1% to -12%.
#
# Male because this voice is male; the two genders differ by about 10%.
# EMBEDDED because a reader produces connected text rather than citation forms.
# Their isolated tokens are longer, and by different amounts for short and long
# vowels -- +6.3 to +9.0 ms short, +11.1 to +16.0 ms long -- so position is not
# one constant either.
#
#                    all five contexts      the three measured here
#   /i/  /ii/         63.2  141.5             67.8  144.7
#   /e/  /ee/         72.2  148.4             74.0  148.0
#   /a/  /aa/         74.4  158.0             76.3  155.0
#   /o/  /oo/         70.6  147.5             69.1  144.0
#   /u/  /uu/         63.7  143.4             65.9  141.6
#
# NOTE the division of labour.  jp_speak.V_DUR, which is the TIMING MODEL's
# target, keeps the five-context means: it is meant to be the vowel's intrinsic
# duration, so averaging over more contexts is the better estimate.  TARGET
# here is a MEASUREMENT REFERENCE and must match what the harness can actually
# measure.  They are different numbers for different jobs and conflating them
# is what produced the error above.
TARGET = {'i': (67.8, 144.7), 'e': (74.0, 148.0), 'a': (76.3, 155.0),
          'o': (69.1, 144.0), 'u': (65.9, 141.6)}
CONTEXTS = [('b', 'p'), ('d', 't'), ('g', 'k'), ('z', 's'), ('h', 'd')]
# The onset boundary is placeable in the three STOP contexts and not in the two
# others, so the headline is taken from the stops and the rest are shown but
# not averaged in.  /zV1s/: a voiced fricative is genuinely part-periodic, so
# there is no instant where voicing begins -- it is already there.  /hV1d/: our
# [h] is a VOICELESS COPY OF THE FOLLOWING VOWEL, which is how onset() builds
# it and is sourced, so the two share formants, amplitude envelope, zero
# crossing rate and crest factor; only periodicity separates them and that
# needs a 32 ms window.  Both are limits of the MEASUREMENT, not evidence that
# the vowel is longer in those contexts.
PLACEABLE = [('b', 'p'), ('d', 't'), ('g', 'k')]
V2 = ('e', 'o')


def token(c1, v1, c2, v2, long_v):
    """Their /C1V1C2V2/ as a mora list.  The length mark is its own mora."""
    first = c1 + v1
    return [first] + ([':'] if long_v else []) + [c2 + v2]


def v1_limit(frames, spans):
    """Where V1's acoustic realisation ends: the first frame of the FOLLOWING
    CLOSURE, not the mora boundary.

    Both of those were tried and both are wrong.  The mora boundary cuts the
    vowel's taper -- /bipe/ tapers at frames 11-13 and mora 1 begins at 12, so
    a third of the vowel's tail falls outside.  (An earlier version read a
    stale pre-compensation span index that happened to sit two frames late,
    which included the taper by accident and inflated every measurement.)
    The closure is the real end: the vowel is over when the tract shuts.
    """
    start = spans[0]['steady'] if spans[0]['steady'] is not None else 0
    for i in range(start + 1, len(frames)):
        if frames[i][0] == 0 or frames[i][0] == 32:    # silence, or a voice bar
            return i
    return spans[-1]['start'] if len(spans) > 1 else len(frames)


def measure_one(morae):
    """-> duration of V1 in ms.  V1 is the first vowel, so the search stops
    where the second mora begins."""
    frames, ends, q = S.build(list(morae))
    S.pitch(frames, ends, 1, q_ends=q, morae=list(morae))   # accent on syll. 1
    x = S.render(frames)
    spans = S.LAST_SPANS
    fl = S.SR // 100
    return measure.vowel_duration(
        x, S.SR, region=(0, v1_limit(frames, spans) * fl))[0]


def mean_over(v, ctxs, long_v):
    return float(np.mean([measure_one(token(c1, v, c2, v2, long_v))
                          for c1, c2 in ctxs for v2 in V2]))


def main():
    print('Vowel duration against Yazawa & Kondo (2019), by their definition:')
    print('first to last positive zero crossing, measured on rendered audio.')
    print('Their stimuli: /C1V1C2V2/, V2 in {e, o}, accent on syllable 1.')
    print()
    print('  %-6s %-22s %-22s %s' % ('vowel', 'short ms (theirs)',
                                     'long ms (theirs)', 'ratio (theirs)'))
    print('  ' + '-' * 70)
    rows = []
    for v in 'ieaou':
        s = mean_over(v, PLACEABLE, False)
        l = mean_over(v, PLACEABLE, True)
        ts, tl = TARGET[v]
        rows.append((v, s, l, ts, tl))
        print('  /%s/    %6.1f (%3d) %+5.0f%%      %6.1f (%3d) %+5.0f%%     '
              '%.2f (%.2f)' % (v, s, ts, 100.0 * (s - ts) / ts,
                               l, tl, 100.0 * (l - tl) / tl,
                               l / s, float(tl) / ts))
    ms = float(np.mean([r[1] for r in rows])); ts = float(np.mean([r[3] for r in rows]))
    ml = float(np.mean([r[2] for r in rows])); tl = float(np.mean([r[4] for r in rows]))
    print('  ' + '-' * 70)
    print('  mean   %6.1f (%3.0f) %+5.0f%%      %6.1f (%3.0f) %+5.0f%%     '
          '%.2f (%.2f)' % (ms, ts, 100.0 * (ms - ts) / ts,
                           ml, tl, 100.0 * (ml - tl) / tl, ml / ms, tl / ts))
    print()
    print('  Targets: male, embedded, averaged over THE SAME THREE consonant')
    print('  contexts measured here.  jp_speak.V_DUR keeps the five-context')
    print('  means -- that is the timing model, this is the reference.')
    print()

    print('  vowel-identity ordering, which a single ratio would hide')
    for lab, idx in (('measured short', 1), ('theirs, short', 3),
                     ('measured long', 2), ('theirs, long', 4)):
        print('    %-16s %s' % (lab + ':', ' > '.join(
            '/%s/' % r[0] for r in sorted(rows, key=lambda r: -r[idx]))))
    print()

    print('  what the SECOND MORA is worth acoustically.  A long vowel here is')
    print('  one continuous vowel held longer -- which is right, and is how they')
    print('  measure it too -- so the whole contrast lives in this increment.')
    print()
    print('    %-6s %10s %10s %12s' % ('vowel', 'ours +', 'theirs +', 'shortfall'))
    io_, it_ = 0.0, 0.0
    for v, s_, l_, ts_, tl_ in rows:
        o, t = l_ - s_, tl_ - ts_
        io_ += o; it_ += t
        print('    /%s/    %8.1f   %8.1f   %9.1f ms' % (v, o, t, t - o))
    print('    %-6s %8.1f   %8.1f   %9.1f ms' % ('mean', io_ / 5, it_ / 5,
                                                 (it_ - io_) / 5))
    print()
    print('    the length mark adds per-vowel frames, not a constant: ' +
          '  '.join('%s %d' % (v, S.long_extra(v)) for v in 'ieaou'))
    print()

    print('  per context, short /a/ then long /aa/ (theirs: 81.7, 173.7 ms)')
    for c1, c2 in CONTEXTS:
        tag = '' if (c1, c2) in PLACEABLE else '   boundary not placeable'
        print('    /%s V %s/   %6.1f   %6.1f%s'
              % (c1, c2, float(np.mean([measure_one(token(c1, 'a', c2, x, False))
                                        for x in V2])),
                 float(np.mean([measure_one(token(c1, 'a', c2, x, True))
                                for x in V2])), tag))


if __name__ == '__main__':
    main()
