# -*- coding: utf-8 -*-
"""Is the loanword accent rule any good?  Ask naist-jdic.

    python tools/ja_accent_check.py

The Latin-word rules put the fall on the ANTEPENULTIMATE mora, which is the
textbook default for a Japanese loanword.  It was the least tested thing in
jp_g2p -- a tendency quoted from memory, applied to every invented word, and
never checked against anything.

It can be checked without an accent dictionary, because naist-jdic IS one for
the words it has: tens of thousands of its entries are katakana loanwords with
an accent type attached.  So this takes every katakana entry, works out what
the rule would say from its morae alone, and compares.

What it CANNOT tell us is whether the rule is right for a word nobody has
borrowed yet -- an invented word has no established accent to be right about.
What it can tell us is whether the rule agrees with Japanese on the words
Japanese has already decided, which is the nearest thing available.

WHAT IT FOUND, over 19,907 katakana loanwords of three or more morae:

  * the rule agrees with 55.1% of them, and with **73.0% of the accented
    ones**.  So the PLACEMENT is good: of the three positions it could pick,
    the antepenultimate gets 73.0% where one-from-the-end gets 5.6% and
    three-from-the-end 47.6%.
  * the shortfall is almost all one thing: **24.6% of loanwords are heiban**,
    unaccented, and the rule never says heiban.
  * which concentrates at FOUR morae, where heiban is 43.2% and the
    antepenultimate only 34.6%.  Everywhere else the rule wins clearly --
    61.5% at three morae, 65.3% at five, 70.7% at six, 74.4% at seven.

CALLING EVERY FOUR-MORA WORD HEIBAN would take the total from 55.1% to 57.8%,
and it is NOT obviously worth it: on any individual word it is close to a coin
flip.  naist-jdic gives ストップ accent 2 and ボックス accent 1, which the rule
gets right and heiban would get wrong, against グーグル and アメリカ at 0,
which heiban would get right.  +2.7 points for trading one kind of error for
another is not a case this measurement makes, so the rule is left as it is --
and the honest summary is that no simple rule does much better.
"""
import argparse
import collections
import csv
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'build', 'Japanese_test'))

DIC = os.path.join(ROOT, 'open_jtalk-1.11', 'mecab-naist-jdic',
                   'naist-jdic.csv')

import jp_g2p as G
import jp_mora as M
import jp_njd as N

KATA_LO, KATA_HI = 0x30a1, 0x30fc


def is_katakana(s):
    return bool(s) and all(KATA_LO <= ord(c) <= KATA_HI for c in s)


SPECIAL = ('N', 'Q', ':')      # in the symbol alphabet, as accent_of reads


def _nth_from_end(morae, k):
    """Where the fall would go if it were k morae from the end."""
    a = len(morae) - k
    while a > 1 and morae[a - 1] in SPECIAL:
        a -= 1
    return max(1, a)


def _four_heiban(morae):
    """The one variant the measurement makes a case for, so it can be seen."""
    return 0 if len(morae) == 4 else G.accent_of(morae)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dic', default=DIC)
    ap.add_argument('--min-morae', type=int, default=3,
                    help='a one- or two-mora word has no antepenultimate')
    args = ap.parse_args(argv)

    if not os.path.exists(args.dic):
        print('%s is not here; naist-jdic is a reference checkout, see NOTICE'
              % os.path.relpath(args.dic, ROOT), file=sys.stderr)
        return 2

    agree = total = 0
    heiban = 0
    by_len = collections.Counter()
    agree_by_len = collections.Counter()
    heiban_by_len = collections.Counter()
    off = collections.Counter()
    pairs = []
    seen = set()
    with io.open(args.dic, encoding='utf-8', errors='ignore') as fh:
        for r in csv.reader(fh):
            if len(r) < 15:
                continue
            surface, acc_f, pron = r[0], r[13], r[12]
            if not is_katakana(surface) or '/' not in acc_f or '*' in acc_f:
                continue
            if ':' in acc_f:            # a compound: several accents
                continue
            try:
                acc, stored = (int(x) for x in acc_f.split('/')[:2])
            except ValueError:
                continue
            if surface in seen:
                continue
            seen.add(surface)
            kata = [m for m, _ in N.split_morae(pron)]
            if len(kata) != stored or len(kata) < args.min_morae:
                continue
            #
            # Into the alphabet accent_of actually reads.  It tests the morae
            # against 'N', 'Q' and ':' -- the symbols the rules work in -- and
            # these came out of split_morae as KATAKANA, so comparing them
            # directly never shifts the accent off a special mora and
            # understates the rule by ten points.  Converting here tests the
            # real function on its real input instead of a lookalike.
            #
            morae = []
            for k in kata:
                got = M.to_morae(k)
                morae.append(got[0] if len(got) == 1 else k)
            pairs.append((morae, acc))
            want = G.accent_of(morae)
            total += 1
            by_len[len(morae)] += 1
            if acc == 0:
                heiban += 1
                heiban_by_len[len(morae)] += 1
            if want == acc:
                agree += 1
                agree_by_len[len(morae)] += 1
            else:
                off[want - acc] += 1

    if total == 0:
        print('no katakana entries with a usable accent were found')
        return 1
    print('%d katakana loanwords of %d+ morae, with naist-jdic\'s own accent'
          % (total, args.min_morae))
    print()
    print('the antepenultimate rule agrees with %d of them -- %.1f%%'
          % (agree, 100.0 * agree / total))
    print('%d of them are HEIBAN, unaccented -- %.1f%%'
          % (heiban, 100.0 * heiban / total))
    print()
    print('by length:')
    print('  morae  words   rule agrees      heiban')
    for n in sorted(by_len):
        if by_len[n] < 20:
            continue
        print('  %5d %6d   %5d %5.1f%%   %5d %5.1f%%'
              % (n, by_len[n], agree_by_len[n],
                 100.0 * agree_by_len[n] / by_len[n], heiban_by_len[n],
                 100.0 * heiban_by_len[n] / by_len[n]))
    print()
    print('where it is wrong, by how far (rule minus dictionary):')
    for d, k in off.most_common(8):
        print('  %+3d  %6d  %5.1f%%' % (d, k, 100.0 * k / total))
    print()
    # the two numbers that actually matter: can it PLACE an accent, and is
    # there a better simple rule
    acc_only = [(m, a) for m, a in pairs if a != 0]
    if acc_only:
        ok = sum(1 for m, a in acc_only if G.accent_of(m) == a)
        print('among the %d ACCENTED ones it places the fall right %.1f%%'
              % (len(acc_only), 100.0 * ok / len(acc_only)))
        for k in (1, 3):
            alt = sum(1 for m, a in acc_only if _nth_from_end(m, k) == a)
            print('   %d from the end would be %.1f%%'
                  % (k, 100.0 * alt / len(acc_only)))
    print()
    print('variants, against all %d:' % total)
    for name, f in (('as it is: antepenultimate always', G.accent_of),
                    ('4 morae -> heiban', _four_heiban)):
        ok = sum(1 for m, a in pairs if f(m) == a)
        print('   %-34s %5d  %5.1f%%' % (name, ok, 100.0 * ok / total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
