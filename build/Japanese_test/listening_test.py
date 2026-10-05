# -*- coding: utf-8 -*-
"""Accessible, exploratory listening stimuli; no threshold estimation.

Task A asks which interval has the longer FIRST vowel. Identical pairs have
no correct answer and describe interval preference only. Task B asks short or
long on the first vowel. Results describe this listener and these conditions;
the project's listener has Japanese exposure but is not a native speaker.

Task A inserts steady parameter frames into an otherwise identical sequence,
uses constant F0, and verifies both the arrays and raw PCM sample counts.
Success at one frame means that change was detectable, not that smaller
changes were tested. No acoustic-boundary calibration is claimed.

Run from the repository root: python build/Japanese_test/listening_test.py
Existing output directories are never overwritten. Use --out for a new run.
"""
import argparse, os, random, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_speak as S, jp_mora as M

OUT = os.path.join(HERE, 'listening_test_verified')
BLIND = os.path.join(OUT, 'blind')
REVEALED = os.path.join(OUT, 'revealed')
SEED = 20261004
# Smallest tested change is one parameter frame. Not a psychophysical threshold.
DELTAS = [0, 1, 2, 4, 8]
GAP = 0.45


def say(morae, accent=1):
    """Ordinary speech, with its own prosody.  For task B."""
    frames, ends, q = S.build(list(morae))
    S.pitch(frames, ends, accent, q_ends=q, morae=list(morae))
    return S.render(frames)


def controlled_frames(morae, mora_index, extra_frames):
    """Insert steady frames; preserve every other parameter frame exactly."""
    from detector_diag import steady_span
    if not isinstance(extra_frames, int) or extra_frames < 0:
        raise ValueError('extra_frames must be a nonnegative integer')
    previous = S.NO_COMPENSATE, S.VOWEL_ALLOC
    try:
        S.NO_COMPENSATE, S.VOWEL_ALLOC = True, None
        base, _, _ = S.build(list(morae))
        span = steady_span(base, S.LAST_SPANS, mora_index)
    finally:
        S.NO_COMPENSATE, S.VOWEL_ALLOC = previous
    if span is None:
        raise ValueError('No suitable steady interior for controlled insertion')
    at = (span[0]+span[1])//2
    longer = base[:at]+[list(base[at]) for _ in range(extra_frames)]+base[at:]
    verify_insertion(base, longer, at, extra_frames)
    return base, longer, at


def verify_insertion(base, longer, at, extra_frames):
    """Check realized arrays, not a request or cached allocation counter."""
    a, b = np.asarray(base), np.asarray(longer)
    if (len(b)-len(a) != extra_frames or
            not np.array_equal(a[:at], b[:at]) or
            not np.array_equal(a[at:], b[at+extra_frames:]) or
            not np.array_equal(b[at:at+extra_frames],
                               np.repeat(a[at:at+1],extra_frames,axis=0))):
        raise AssertionError('Stimulus changed outside the steady-frame insertion')


def controlled_pair(morae, mora_index, extra_frames):
    base, longer, at = controlled_frames(morae,mora_index,extra_frames)
    a = S.render(base,postprocess=False)
    b = S.render(longer,postprocess=False)
    expected = extra_frames*(S.SR//100)
    if len(b)-len(a) != expected:
        raise AssertionError('PCM delta %d samples, expected %d' % (len(b)-len(a),expected))
    return a,b,len(base),len(longer)


def pair(a, b):
    g = np.zeros(int(S.SR * GAP), dtype=np.int16)
    return np.concatenate([a, g, b])


def write(path, x):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(S.SR)
        w.writeframes(x.tobytes())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default=OUT)
    args = parser.parse_args(argv)
    blind = os.path.join(args.out,'blind')
    revealed = os.path.join(args.out,'revealed')
    # Freeze completed or partial runs; a new directory is required even if
    # answer cells are blank. Audio and key identity must remain reproducible.
    if os.path.exists(args.out):
        raise FileExistsError('Refusing to overwrite an existing run; choose --out')
    os.makedirs(blind)
    os.makedirs(revealed)

    rng = random.Random(SEED)

    # ---- task A: first or second -------------------------------------
    trials = []
    for v in 'ieaou':
        for d in DELTAS:
            for order in (0, 1):            # which member is the longer one
                trials.append(('A', v, d, order))
    rng.shuffle(trials)
    ids = ['a%02d' % (i + 1) for i in range(len(trials))]
    keyA = []
    for tid, (_, v, d, order) in zip(ids, trials):
        # Keep the carrier varied; manipulation is per segment, not identity.
        v2 = 'o' if v == 'e' else 'e'
        w = ['b' + v, 'p' + v2]
        a_, b_, fa, fb = controlled_pair(w, 0, d)
        x = pair(a_, b_) if order == 0 else pair(b_, a_)
        write(os.path.join(blind, tid + '.wav'), x)
        keyA.append((tid, v, d, 'second' if order == 0 else 'first',
                     fa, fb, (fb - fa) * 10))

    # ---- task B: short or long, forced choice ---------------------------
    # Every pair must contrast in the SAME syllable position, or the
    # instruction cannot name the target.  /obasan/ vs /obaasan/ and
    # /ojisan/ vs /ojiisan/ lengthen the SECOND vowel and were asked about as
    # if they were the first; they are replaced with first-vowel pairs.
    words = [('kado', 'kaado'), ('toru', 'tooru'),
             ('biru', 'biiru'), ('suji', 'suuji'),
             ('kuki', 'kuuki'), ('seki', 'seeki')]
    bt = []
    for s_, l_ in words:
        for which in ('short', 'long'):
            bt.append((s_, l_, which))
    rng.shuffle(bt)
    idsB = ['b%02d' % (i + 1) for i in range(len(bt))]
    keyB = []
    for tid, (s_, l_, which) in zip(idsB, bt):
        w = s_ if which == 'short' else l_
        write(os.path.join(blind, tid + '.wav'), say(M.to_morae(w), accent=1))
        keyB.append((tid, w, which, s_, l_))

    with open(os.path.join(blind, 'sheet_A.tsv'), 'w') as fh:
        fh.write('# Task A.  Each file is TWO words with a gap between them.\n'
                 '# WHICH ONE has the longer FIRST vowel?  Answer "first" or\n'
                 '# "second".  There is no "same" option: you must choose one\n'
                 '# even when they sound identical, and some pairs ARE\n'
                 '# identical.  Those measure which interval you tend to pick\n'
                 '# when you cannot tell -- they have no correct answer, so\n'
                 '# they are not scored as mistakes.  Confidence 1-5 records\n'
                 '# how sure you were.\n')
        fh.write('file\tanswer\tconfidence_1to5\tnotes\n')
        for tid in [k[0] for k in keyA]:
            fh.write('%s\t\t\t\n' % tid)

    with open(os.path.join(blind, 'sheet_B.tsv'), 'w') as fh:
        fh.write('# Task B.  Each file is ONE word.  Does the first vowel sound\n'
                 '# SHORT or LONG?  Answer "short" or "long".\n')
        fh.write('file\tanswer\tconfidence_1to5\tnotes\n')
        for tid, _, _, _, _ in keyB:
            fh.write('%s\t\t\t\n' % tid)

    with open(os.path.join(revealed, 'key_A.tsv'), 'w') as fh:
        fh.write('file\tvowel\tdelta_frames\ttotal_frames_a\ttotal_frames_b\t'
                 'verified_delta_ms\tlonger_member\n')
        for tid, v, d, which, fa, fb, dms in sorted(keyA):
            fh.write('%s\t%s\t%d\t%d\t%d\t%d\t%s\n'
                     % (tid, v, d, fa, fb, dms, which if d else 'neither'))
    with open(os.path.join(revealed, 'key_B.tsv'), 'w') as fh:
        fh.write('file\tword\tlength\tshort_form\tlong_form\n')
        for tid, w, which, s_, l_ in sorted(keyB):
            fh.write('%s\t%s\t%s\t%s\t%s\n' % (tid, w, which, s_, l_))

    print('task A: %d pairs, deltas %s FRAMES (0 = unscored interval-preference trial)'
          % (len(keyA), DELTAS))
    print('task B: %d single words, %d minimal pairs' % (len(keyB), len(words)))
    print('  %s   audio and the two sheets' % blind)
    print('  %s   answer keys -- closed until the sheets are filled' % revealed)


if __name__ == '__main__':
    main()
