# -*- coding: utf-8 -*-
"""Build the blind boundary-annotation audit.  Protocol by Astra.

Run from the repository root:  python build/Japanese_test/boundary_audit.py

WHY.  The vowel-duration residual is strongly associated with how sensitive
each vowel is to the detector's growth cap, an engineering constant with no
phonetic content.  That makes boundary placement a confound: the current
measurements cannot separate synthesis error from detector bias.  It does not
show the residual IS detector bias.

WHAT THE THREE COMPARISONS SEPARATE.  The schedule records INTENDED CONTROL
TIMING and is not an independent measurement of the acoustic vowel, so it
locates a discrepancy once one is established and never decides whether there
is one.

  detector vs manual boundaries   detector bias, relative to this annotator
                                  and protocol
  manual duration vs the          a duration discrepancy from a SELECTED
    context-matched reference      REFERENCE CONDITION -- not automatically an
                                  error in Japanese synthesis, since speakers
                                  vary and the baseline is a design choice
  manual vs the schedule          how control timing becomes acoustic
                                  boundaries

PRE-REGISTERED, before any annotation exists.  Primary hypothesis: the detector
UNDERESTIMATES short /i/ across the three stop contexts.  Agreement in direction
across all three supports a systematic problem in the contexts tested; mixed
results support a context-dependent one; neither establishes population-wide
bias.  Decision rule, in boundary_analyse.py:

  * a detector boundary shows REPEATABLE DIRECTIONAL DISAGREEMENT when it falls
    outside both passes' plausible intervals, on the same side
  * falling inside either interval makes it UNRESOLVED BY THIS AUDIT, not
    "correct"
  * distance to the nearest edge of the combined interval is reported in ms
  * onset and offset are judged separately as well as through duration -- two
    misplaced boundaries can produce an apparently correct duration
  * conservative duration interval is [e_early - s_late, e_late - s_early]

An error-to-uncertainty RATIO was considered and rejected: it goes unstable
when an annotator gives a very narrow interval.

LIMITS, on the record.  One annotator with two passes can establish
repeatability and expose a large disagreement; it cannot estimate
inter-annotator agreement, and a person can repeat a systematic mistake.  The
plausible interval is the annotator's stated ambiguity on that occasion, not a
confidence interval.  The annotator here is already familiar with this
discussion and so cannot be fully blinded to the hypothesis; the file IDs,
shuffling and withheld rationale reduce the cue, they do not remove it.
Rodd et al. (2021) is explicit that annotation consistency is not the same
thing as the validity of a reference segmentation.

The 20 ms tail allowance in the renderer is calibrated against the very
detector under investigation, so it is provisional compensation rather than an
independently established amount of acoustic tail.
"""
import os, random, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, measure

OUT = os.path.join(HERE, 'boundary_audit')
BLIND = os.path.join(OUT, 'blind')
REVEALED = os.path.join(OUT, 'revealed')
CONTEXTS = [('b', 'p'), ('d', 't'), ('g', 'k')]
CAPS = [(0.25, 8), (0.5, 16), (1.0, 32)]
SEED = 20261004

# The annotator's instructions.  Deliberately contains no rationale, no vowel
# names, no sensitivities and no hypothesis -- the previous version wrote this
# module's docstring here, which told the annotator that /i/ and /a/ were the
# extremes at 13.7 ms against 2.2 and so handed over the expected pattern.
PROTOCOL = """BOUNDARY ANNOTATION

Eighteen short audio files, each a two-syllable nonsense word.  Mark the
boundaries of the FIRST vowel in each.

All times in SECONDS from the start of the file, to three decimal places.

For each file, in each pass, record six numbers:

    onset   earliest plausible / preferred / latest plausible
    offset  earliest plausible / preferred / latest plausible

HOW TO PLACE A BOUNDARY

1. Find the vowel's edge acoustically first.  Onset: the first glottal pulse
   carrying the vowel's formant structure.  Offset: the last glottal pulse
   before the amplitude falls away into the following consonant.  Use the
   waveform and the spectrogram together.
2. Then refine to the nearest POSITIVE-GOING ZERO CROSSING -- the sample where
   the waveform crosses zero while rising.  That is the convention the
   reference study used, but it is a refinement step: it does not by itself
   tell you where the edge is in an ambiguous signal.  Step 1 decides that.

WHERE IT IS AMBIGUOUS.  If voicing fades gradually and you cannot put one line
on it, that is expected and is the point of the three columns: earliest and
latest should bracket everywhere you would accept, and preferred is your best
single estimate.  If the edge is genuinely indeterminate, write `indet` in the
preferred column and still give earliest and latest if you can.

TWO PASSES.  Do pass 1 completely, then pass 2 -- on a different day if you
can.  The file order differs between passes.  Do not look at pass 1 while doing
pass 2, and do not open the `revealed` folder until both passes are finished.

Fill in sheet_pass1.tsv and sheet_pass2.tsv.  Leave a row blank if you skip it.
"""


def tokens():
    out = []
    for v in ('i', 'a'):
        for c1, c2 in CONTEXTS:
            for lng in (False, True):
                out.append((c1, v, c2, 'e', lng))
    for v in ('e', 'o', 'u'):
        for lng in (False, True):
            out.append(('b', v, 'p', 'e', lng))
    return out


def word(c1, v, c2, v2, lng):
    return '%s%s%s%s' % (c1, v * (2 if lng else 1), c2, v2)


def scheduled(c1, v, lng):
    man = J.MANNER[c1]
    start = man[2] + man[3]
    return start, start + S.vowel_frames(v, lng)


def detector(x, sr, frac, region):
    import types, io as _io
    src = _io.open(os.path.join(HERE, 'measure.py'), encoding='utf-8').read()
    src = src.replace('cap = int((WIN / 2.0) / h)',
                      'cap = int((WIN * %.4f) / h)' % frac)
    m = types.ModuleType('m')
    m.__dict__['__name__'] = 'm'
    exec(compile(src, 'measure.py', 'exec'), m.__dict__)
    return m.vowel_duration(x, sr, region=region)


def main():
    for d in (BLIND, REVEALED):
        if not os.path.isdir(d):
            os.makedirs(d)

    # Never destroy annotation.  The previous version opened the sheet with
    # mode 'w' on every run, so regenerating after someone had filled it in
    # would have silently erased the work this whole exercise exists to get.
    existing = []
    for p in (1, 2):
        f = os.path.join(BLIND, 'sheet_pass%d.tsv' % p)
        if os.path.exists(f):
            with open(f) as fh:
                body = [ln for ln in fh.read().splitlines()
                        if ln.strip() and not ln.startswith('#')
                        and not ln.startswith('file\t')
                        and any(c.strip() for c in ln.split('\t')[1:])]
            if body:
                existing.append((os.path.basename(f), len(body)))
    if existing:
        print('REFUSING to regenerate: these sheets already have data in them.')
        for n, k in existing:
            print('   %-18s %d filled rows' % (n, k))
        print('Move or rename %s first if you really want a fresh audit.' % BLIND)
        return

    rng = random.Random(SEED)
    toks = tokens()
    ids = ['t%02d' % (i + 1) for i in range(len(toks))]
    rng.shuffle(ids)                      # the ID does not track the token

    rows = []
    for tid, (c1, v, c2, v2, lng) in zip(ids, toks):
        morae = [c1 + v] + ([':'] if lng else []) + [c2 + v2]
        frames, ends, q = S.build(list(morae))
        S.pitch(frames, ends, 1, q_ends=q, morae=list(morae))
        x = S.render(frames)

        with wave.open(os.path.join(BLIND, tid + '.wav'), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(S.SR)
            w.writeframes(x.tobytes())

        fl = S.SR // 100
        spans = S.LAST_SPANS
        last = spans[-1]['start'] if len(spans) > 1 else len(frames)
        region = (0, last * fl)
        a, b = scheduled(c1, v, lng)
        tgt = S.V_DUR.get(v, S.V_DUR['a'])[1 if lng else 0]
        tail = S.VOWEL_TAIL.get(v, S.VOWEL_TAIL_MS)
        lines = ['%.6f\t%.6f\tschedule (control timing, + %.0f ms tail)'
                 % (a * fl / float(S.SR), b * fl / float(S.SR), tail)]
        meas = []
        for frac, ms in CAPS:
            d_ms, s0, s1 = detector(x, S.SR, frac, region)
            lines.append('%.6f\t%.6f\tdetector cap=%dms  %.1f ms'
                         % (s0 / float(S.SR), s1 / float(S.SR), ms, d_ms))
            meas.append((ms, d_ms, s0 / float(S.SR), s1 / float(S.SR)))
        with open(os.path.join(REVEALED, tid + '.labels.txt'), 'w') as fh:
            fh.write('\n'.join(lines) + '\n')
        rows.append((tid, word(c1, v, c2, v2, lng), v, lng, '%s_%s' % (c1, c2),
                     tgt, (b - a) * 10.0, meas))

    hdr = ('file\tonset_earliest\tonset_preferred\tonset_latest\t'
           'offset_earliest\toffset_preferred\toffset_latest\tnotes\n')
    for p in (1, 2):
        order = [r[0] for r in rows]
        random.Random(SEED + p).shuffle(order)      # independent per pass
        with open(os.path.join(BLIND, 'sheet_pass%d.tsv' % p), 'w') as fh:
            fh.write('# pass %d.  All times in SECONDS from file start.\n' % p)
            fh.write(hdr)
            for tid in order:
                fh.write('%s\t\t\t\t\t\t\t\n' % tid)

    with open(os.path.join(BLIND, 'PROTOCOL.txt'), 'w') as fh:
        fh.write(PROTOCOL)

    with open(os.path.join(REVEALED, 'token_map.tsv'), 'w') as fh:
        fh.write('file\tword\tvowel\tlength\tcontext\tintended_ms\tschedule_ms\n')
        for tid, w_, v, lng, ctx, tgt, sch, _ in sorted(rows):
            fh.write('%s\t%s\t%s\t%s\t%s\t%.1f\t%.0f\n'
                     % (tid, w_, v, 'long' if lng else 'short', ctx, tgt, sch))

    with open(os.path.join(REVEALED, 'results.txt'), 'w') as fh:
        fh.write(__doc__.strip() + '\n\n')
        fh.write('%-5s %-8s %9s %9s %9s %9s %9s\n'
                 % ('file', 'word', 'intended', 'schedule', 'cap 8ms',
                    'cap 16ms', 'cap 32ms'))
        for tid, w_, v, lng, ctx, tgt, sch, meas in sorted(rows):
            fh.write('%-5s %-8s %9.1f %9.0f %9.1f %9.1f %9.1f\n'
                     % (tid, w_, tgt, sch, meas[0][1], meas[1][1], meas[2][1]))
        fh.write('\nOne token each, V2 = /e/ throughout.  These are NOT the\n'
                 'six-token means check_vowel_length.py reports, and must be\n'
                 'compared against the reference for THEIR exact context and\n'
                 'V2, not against those averages.\n')

    print('%d tokens, neutral IDs, order shuffled independently per pass' % len(rows))
    print('  %s' % os.path.join(BLIND, 'PROTOCOL.txt'))
    print('  %s   audio + the two sheets' % BLIND)
    print('  %s   do not open until both passes are done' % REVEALED)


if __name__ == '__main__':
    main()
