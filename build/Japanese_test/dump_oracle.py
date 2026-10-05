# -*- coding: utf-8 -*-
"""Reference frames for the C port to be checked against, word by word.

Run from the repository root:  python build/Japanese_test/dump_oracle.py

WHY THIS COMES FIRST.  Shipping Japanese means `ja_speak_bytes` in the engine,
which means porting the front end -- mora parsing, frame building, pitch -- from
Python to C.  That is on the order of 2,500 lines, most of it tables.  The
engine half already works: `tvtts_speak_frames` renders frames to audio and has
done since the API was added, so nothing below the frame array needs porting.

A port that size, written and then listened to, is unverifiable.  Every error
this project has made in the last few sessions was a quantity that moved
without anyone checking it had moved, and a 2,500-line translation is that
failure mode with 2,500 opportunities.  So the oracle exists before the port:
the C side must reproduce these frames EXACTLY, word for word, byte for byte,
and any divergence names the word and the frame where it first appears.

WHAT IS AND IS NOT COVERED.  This fixes the FRONT END against the Python that
the ear has signed off on.  It says nothing about whether those frames are
right -- that is what the samples, the sweep and open_questions are for.  It is
a translation check, which is exactly what a translation needs.

The 22 tracks are the engine's own parameter frame, so a byte-identical array
is a byte-identical utterance by construction.

FORMAT.  One line per word:

    <kana>\\t<accent>\\t<nframes>\\t<hex, nframes * 22 bytes>

Tab-separated, UTF-8, hex lower-case.  Trivial to read from C with fscanf and
trivial to diff when it breaks.
"""
import io, csv, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_speak as S, jp_mora as M

OUT = os.path.join(HERE, 'oracle_frames.tsv')
# The release the rest of the pipeline is built from; see
# data/ja/jadic.provenance.txt.  Read here only as a corpus -- a varied
# word list -- but reading a different one than the front end uses would
# be a needless second source of difference.
DICT = os.path.join('open_jtalk-1.11', 'mecab-naist-jdic',
                    'naist-jdic.csv')
N = 500
SEED = 11

# Hand-picked words first, so the file leads with the cases that have cost the
# most to get right.  Each one is here because something was wrong with it.
FIXED = [
    'konnichiwa',      # the first sentence this ever said
    'obasan', 'obaasan', 'ojisan', 'ojiisan',   # final /N/, and vowel length
    'kutsu', 'desu', 'sukoshi', 'shita',        # devoiced vowels
    'fune', 'fuku', 'futon', 'fuan', 'fuufu',   # the four /f/ realizations
    'sakura', 'tomodachi', 'daidokoro',         # the flap
    'sanpo', 'ginkou', 'shinbun',               # moraic /N/ assimilation
    'kaado', 'tooru', 'biiru', 'kuuki',         # long vowels
    'gakkou', 'kitte', 'zasshi',                # geminates
    'shashin', 'tsutsuji', 'chachacha',         # sibilants and affricates
    'arigatou', 'sayounara', 'ohayou',
]


def corpus():
    words = list(FIXED)
    if os.path.exists(DICT):
        def k2h(x):
            return u''.join(chr(ord(c) - 0x60) if u'ァ' <= c <= u'ヶ'
                            else c for c in x)
        rows = []
        with io.open(DICT, encoding='utf-8', errors='ignore') as fh:
            for r in csv.reader(fh):
                if len(r) > 12:
                    p = r[12]
                    if p and 2 <= len(p) <= 12 and all(
                            u'ァ' <= c <= u'ー' for c in p):
                        rows.append(k2h(p))
        random.Random(SEED).shuffle(rows)
        words += rows[:N]
    else:
        # Loud, because the failure mode is an oracle that still passes: the
        # 33 hand-picked words would reproduce fine and the 500 dictionary
        # words would simply not be there, shrinking the reference by 94%
        # without anything reporting a problem.
        print('WARNING: %s not found.' % DICT)
        print('WARNING: writing the 33 hand-picked words ONLY -- this is a')
        print('WARNING: far weaker oracle than the committed one.  The')
        print('WARNING: dictionary is a reference checkout; see NOTICE.')
    return words


def main():
    n_words = n_frames = 0
    skipped = []
    with io.open(OUT, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('# reference frames from the Python front end.  The C port\n'
                 '# must reproduce every one of these exactly.\n'
                 '# <kana>\\t<accent>\\t<nframes>\\t<hex>\n')
        for w in corpus():
            try:
                morae = M.to_morae(w)
                if not morae:
                    skipped.append((w, 'no morae'))
                    continue
                frames, ends, q = S.build(list(morae))
                S.pitch(frames, ends, 0, q_ends=q, morae=list(morae))
            except Exception as e:
                skipped.append((w, type(e).__name__))
                continue
            flat = bytearray()
            for f in frames:
                flat.extend(bytes(bytearray(f)))
            fh.write('%s\t%d\t%d\t%s\n' % (w, 0, len(frames), flat.hex()))
            n_words += 1
            n_frames += len(frames)
    print('%s' % OUT)
    print('  %d words, %d frames, %d bytes of reference'
          % (n_words, n_frames, n_frames * 22))
    if skipped:
        print('  skipped %d: %s' % (len(skipped), skipped[:5]))
    print()
    print('  The C port passes when every line reproduces byte for byte.')
    print('  A divergence names the word and the first frame that differs.')


if __name__ == '__main__':
    main()
