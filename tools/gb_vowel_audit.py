# -*- coding: utf-8 -*-
"""What quality do the General British vowels actually come out at?

    python -B tools/gb_vowel_audit.py

The targets in lang/eng/data/gb_vowels.h are Cruttenden's Table 6, measured
in CONNECTED SPEECH.  That is the right goal for the OUTPUT -- a reader reads
connected text -- but it is not obviously the right TARGET, because the engine
reduces and undershoots on its own account, and aiming an already-centralised
value at a system that centralises again centralises twice.  Table 4 gives the
same vowels in citation form, which is more peripheral; §8.6 says so outright.

This measures where the vowels actually land, over the sample sentences, and
compares that with Table 6.  It then reports what the error WOULD be with
Table 4 as the target, which is exact rather than a guess: for the pure vowels
the remap is affine in the target, since primary and glide move together, so
changing the table shifts every frame by exactly Table4 - Table6.

Frames are attributed to a vowel by gb.c's tv_gb_frame_trace hook, which
build/check/gb_formants.exe sets, so this is not guessed from the shape of the
numbers.  The nucleus is taken as the middle third of each span.
"""
import collections
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
from gb_samples import SAMPLES

EXE = ROOT / 'build' / 'check' / 'gb_formants.exe'

#: engine symbol -> (lexical set, Table 6 male F1/F2, Table 4 male F1/F2).
#: Table 6 has no /E:/ and Table 4 no separate NORTH, so those two rows carry
#: the same figures in both columns and cannot move.
VOWELS = [
    ('E', 'FLEECE',  (280, 2249), (275, 2221)),
    ('i', 'KIT',     (367, 1757), (382, 1958)),
    ('e', 'DRESS',   (494, 1650), (560, 1797)),
    ('a', 'TRAP',    (690, 1550), (732, 1527)),
    ('v', 'STRUT',   (644, 1259), (695, 1224)),
    ('o', 'LOT',     (558, 1047), (593,  866)),
    ('u', 'FOOT',    (379, 1173), (414, 1051)),
    ('b', 'GOOSE',   (316, 1191), (302, 1131)),
    ('w', 'THOUGHT', (415,  828), (453,  642)),
    ('g', 'NORTH',   (415,  828), (453,  642)),
    ('r', 'START',   (646, 1155), (687, 1077)),
    ('3', 'NURSE',   (478, 1436), (513, 1377)),
    ('@', 'comma',   (478, 1436), (513, 1377)),
    ('k', 'SQUARE',  (538, 1864), (538, 1864)),
]

#: THE TWO VOWELS NEITHER TABLE GOVERNS.  7.10.2(2) fronts /U, u:, U@/ to
#: [}, }:, }@] and calls it well established; footnote 3 to 8.6 (p.159) shows
#: Tables 4 and 6 are Deterding (1990, 1997), so they predate the 8th
#: edition's own 2014 prose and cannot show the change.  For these the goal is
#: not a figure but a band: in front of the measured back value, and no
#: further than the central landmark /3:/ F2 1436, which 8.6's note also makes
#: the value for /@/.  Their F2 is therefore left out of the table comparison
#: below.  Derivation in tools/gen_gb_vowels.py.
FRONTED = {'u': (1173, 1436), 'b': (1191, 1436)}


def collect():
    """symbol -> list of (F1, F2) at each occurrence's nucleus."""
    got = collections.defaultdict(list)
    for _name, text, _why in SAMPLES:
        out = subprocess.run([str(EXE), 'en-GB', text],
                             capture_output=True, text=True).stdout
        frames = []
        for line in out.splitlines():
            p = line.split()
            if len(p) == 5:
                frames.append((int(p[1]), int(p[2]), p[4]))
        # consecutive frames with the same owner are one occurrence
        run = []
        for f1, f2, sym in frames + [(0, 0, None)]:
            if run and sym != run[-1][2]:
                lo, hi = len(run) // 3, max(len(run) // 3 + 1, 2 * len(run) // 3)
                mid = run[lo:hi]
                if mid:
                    got[run[0][2]].append(
                        (statistics.median(x[0] for x in mid),
                         statistics.median(x[1] for x in mid)))
                run = []
            if sym is not None and sym != '?':
                run.append((f1, f2, sym))
    return got


def main():
    got = collect()
    print('Where the GB vowels actually land, over the %d sample sentences.'
          % len(SAMPLES))
    print('Target now = Cruttenden Table 6 (connected speech, 10 male),')
    print('except FOOT and GOOSE, which 7.10.2(2) fronts past it.')
    print()
    print('%-8s %4s  %-11s %-11s %-13s %s'
          % ('set', 'n', 'wanted T6', 'measured', 'error now',
             'error if T4'))
    print('-' * 72)
    tot_now = tot_alt = 0.0
    spread = []
    spread2 = []
    rows = 0
    for sym, name, t6, t4 in VOWELS:
        obs = got.get(sym, [])
        if not obs:
            print('%-8s    0  (not seen)' % name)
            continue
        m1 = statistics.median(o[0] for o in obs)
        m2 = statistics.median(o[1] for o in obs)
        e1, e2 = m1 - t6[0], m2 - t6[1]
        # switching the table shifts every frame by exactly T4 - T6
        a1, a2 = e1 + (t4[0] - t6[0]), e2 + (t4[1] - t6[1])
        band = FRONTED.get(sym)
        if band:
            flat = ('  (7.10.2(2): want F2 %d..%d, %s)'
                    % (band[0], band[1],
                       'in' if band[0] < m2 < band[1] else 'OUT'))
        else:
            flat = '  (same in both tables)' if t4 == t6 else ''
        print('%-8s %4d  %4d/%-6d %4d/%-6d %+5d/%+-6d %+5d/%+-6d%s'
              % (name, len(obs), t6[0], t6[1], m1, m2, e1, e2, a1, a2, flat))
        q = lambda v, p: v[min(len(v) - 1, int(len(v) * p))]
        f1s = sorted(o[0] for o in obs)
        f2s = sorted(o[1] for o in obs)
        spread.append((abs(e1), name, e1, q(f1s, .25) - t6[0],
                       q(f1s, .75) - t6[0]))
        spread2.append((abs(e2), name, e2, q(f2s, .25) - t6[1],
                        q(f2s, .75) - t6[1]))
        if t4 != t6 and not band:
            tot_now += abs(e1) + abs(e2) / 2.0
            tot_alt += abs(a1) + abs(a2) / 2.0
            rows += 1
    print()
    print('Summed error over the %d vowels the two tables differ on,' % rows)
    print('F1 plus half F2 (F2 steps are twice as coarse):')
    print('   aiming at Table 6 (connected, what we do now): %6.0f Hz' % tot_now)
    print('   aiming at Table 4 (citation)                 : %6.0f Hz' % tot_alt)
    print()
    print('Is the F1 miss steady enough to compensate for?  Quartiles of the')
    print('per-occurrence error, so a wide band means it varies with context:')
    print('%-8s %-9s %s' % ('set', 'median', 'quartiles of the same error'))
    for _a, name, e1, lo, hi in sorted(spread, reverse=True)[:6]:
        print('  %-8s %+5d Hz   %+5d .. %+-5d   (band %d Hz)'
              % (name, e1, lo, hi, hi - lo))
    print()
    print('The same question for F2.  A miss as steady as the F1 ones could')
    print('be compensated the same way; a wide band means it cannot.')
    print('%-8s %-9s %s' % ('set', 'median', 'quartiles of the same error'))
    for _a, name, e2, lo, hi in sorted(spread2, reverse=True)[:6]:
        print('  %-8s %+5d Hz   %+5d .. %+-5d   (band %d Hz)'
              % (name, e2, lo, hi, hi - lo))


main()
