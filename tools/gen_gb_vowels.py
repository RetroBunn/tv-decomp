# -*- coding: utf-8 -*-
"""The General British vowel targets, and where every number comes from.

    python -B tools/gen_gb_vowels.py           compare, and show the deltas
    python -B tools/gen_gb_vowels.py --c       emit the C table

The American engine aims each vowel at a formant target held in g_pt_f1..f4
(and, for the classes below 0x13, a second target in g_at_f1..f4 that the
vowel glides to).  A General British accent is those targets moved, which is
why this file exists: it holds Cruttenden's measured values, reads the
engine's own, and prints one against the other so a change is never a guess.

PROVENANCE.  Cruttenden, *Gimson's Pronunciation of English*, 8th ed. (2014),
section 8.6, pages 104-105.  Three tables, each with its own speakers and
condition, and the male column is used throughout because the engine's voice 0
is a male tract and the stock targets were written for it:

  Table 6  pure vowels, CONNECTED SPEECH, 10 male + 10 female.  The default.
           Cruttenden: connected-speech values are "considerably less
           peripheral (more centralised)" than citation forms, and a reader
           produces connected text.  Same choice, for the same reason, as the
           Japanese work's preference for embedded over isolated tokens.
  Table 4  pure vowels, CITATION, 8 male + 8 female.  Used for /E:/ alone,
           which Table 6 does not carry.
  Table 5  diphthongs, CITATION, 3 male + 3 female, both components.  The only
           diphthong data the book gives; three speakers a sex is a small
           sample and that is a known weakness of these seven rows.

  /@/ has no figures at all.  Cruttenden: "average values may be taken to be
  equivalent to those for /3:/", and that is what is done here.

WHAT IS NOT CRUTTENDEN'S.  He publishes F1 and F2 only.  F3, F4 and the
bandwidths therefore keep the engine's own values -- with one exception that
is the whole point of the exercise, marked PRIMARY_F3_DONOR and
GLIDE_F3_DONOR below.

See lang/eng/research/gimson8.md.  Nothing here is copied from the book but
the measured figures, which are facts rather than expression.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
import tvdata

CLASS_REF = 0x100eece0
PT = dict(f1=0x100eece8, f2=0x100eed28, f3=0x100eed68, f4=0x100eeda8,
          b1=0x100eede8, b2=0x100eee28, b3=0x100eee68)
AT = dict(f1=0x100eefd8, f2=0x100eeff0, f3=0x100ef008, f4=0x100ef020)

# Gimson's male F1/F2, in Hz.  (engine symbol, lexical set, source, F1, F2,
# glide F1, glide F2).  A glide of None means a steady vowel.
T6, T4, T5 = 'Table 6 connected', 'Table 4 citation', 'Table 5 citation'
GB = [
    ('E', 'FLEECE',  T6, 280,  2249, None, None),
    ('i', 'KIT',     T6, 367,  1757, None, None),
    ('e', 'DRESS',   T6, 494,  1650, None, None),
    ('a', 'TRAP',    T6, 690,  1550, None, None),
    ('v', 'STRUT',   T6, 644,  1259, None, None),
    ('o', 'LOT',     T6, 558,  1047, None, None),
    ('u', 'FOOT',    T6, 379,  1173, None, None),
    ('b', 'GOOSE',   T6, 316,  1191, None, None),
    ('w', 'THOUGHT', T6, 415,   828, None, None),
    ('r', 'START',   T6, 646,  1155, None, None),
    ('3', 'NURSE',   T6, 478,  1436, None, None),
    ('@', 'comma',   T6, 478,  1436, None, None),   # = NURSE, on 8.6's note
    ('k', 'SQUARE',  T4, 538,  1864, None, None),   # 8.9.4: a pure long vowel
    ('g', 'NORTH',   T6, 415,   828, None, None),   # 7.10.1(1): merged
    ('A', 'FACE',    T5, 587,  1945,  413, 2130),
    ('I', 'PRICE',   T5, 734,  1117,  439, 2058),
    ('y', 'CHOICE',  T5, 477,   824,  443, 1924),
    ('O', 'GOAT',    T5, 537,  1266,  379, 1024),
    ('f', 'MOUTH',   T5, 780,  1368,  372, 1074),
    ('4', 'NEAR',    T5, 382,  2096,  578, 1643),
    ('c', 'CURE',    T5, 426,  1028,  587, 1250),
]

# WHERE THE AMERICAN r-COLOURING ACTUALLY LIVES.  Measured, not assumed, and
# it is not where it first looks.  A collapsed F3 is the signature -- every
# plain vowel sits at 2100-2900 Hz -- and printing both targets shows it is
# the GLIDE that collapses, not the vowel itself:
#
#        primary F3        glide F3
#   r  START    2512          1584
#   k  SQUARE   2080          1728
#   g  NORTH    2640          1504
#   4  NEAR     2640          1632
#   c  CURE     2096          1568
#   3  NURSE    1456          (none: class >= 0x13, a steady vowel)
#
# which is exactly how post-vocalic /r/ works in American: a normal vowel that
# glides into an r-coloured offset.  So the primaries need no F3 change at
# all, and five of these six lose their colouring for free -- GB makes them
# steady, and a steady vowel's glide target is the vowel itself.
#
# That leaves two cases needing a number Cruttenden does not publish, since he
# gives F1 and F2 only.  Both borrow the engine's OWN F3 from the nearest
# vowel of similar quality that was never r-coloured, so the voice keeps its
# own F3 conventions.  OUR CHOICE, not a measurement.
PRIMARY_F3_DONOR = {
    '3': ('@', 'NURSE is the one r-coloured in the primary; comma matches it'),
}
#: NEAR and CURE still glide in GB, to schwa -- so their glide takes schwa's.
GLIDE_F3_DONOR = {
    '4': ('@', 'glides to schwa'),
    'c': ('@', 'glides to schwa'),
}


# WHERE THE ENGINE WILL NOT GO, AND WHAT TO DO ABOUT IT.
#
# Cruttenden's Table 6 is the right goal for the OUTPUT -- a reader reads
# connected text -- but it is not automatically the right TARGET, because the
# engine reduces on its own account, and aiming an already-centralised value
# at a system that centralises again centralises twice.  §8.6 says outright
# that the connected-speech values are considerably less peripheral than the
# citation ones, so the risk is real.  (The point is Astra's.)
#
# Measured rather than argued, with tools/gb_vowel_audit.py over the 22 sample
# sentences, attributing frames to vowels through gb.c's frame-trace hook.
# Two things came out of it.
#
# FIRST, the risk is NOT systemic, and the obvious remedy is worse than the
# problem.  Eight of the fourteen pure vowels already land within a few Hz of
# Table 6 -- THOUGHT +1/+0, NORTH +1/+0, START +4/+1, FOOT +1/-1, KIT -5/-1,
# SQUARE +2/+4 -- so the engine is not quietly centralising everything.
# Rebuilding the whole table from Table 4 (citation) was tried and measured:
# summed error went from 666 Hz to 1152 Hz, because it moves the eight that
# are already right in order to help the three that are not.
#
# SECOND, three vowels do miss, all on F1, all in the same direction, and all
# of them SHORT: the long vowels have time to reach their target and these do
# not.  The misses are steady enough to correct for -- DRESS's quartiles span
# only 28 Hz -- and never change sign.  So the target is moved by the measured
# miss, which lands the OUTPUT on Cruttenden's figure; the target is the
# instrument and the output is the claim.
#
# AND WHY THERE IS NO F2_REACH TO GO WITH IT.  Three vowels miss on F2 as
# well -- FLEECE -149, STRUT -119, NURSE +100 -- and the same instrument was
# measured for them on 2026-10-11.  It does not apply.  The test the F1
# corrections pass is that the correction is at least as big as the quartile
# band, so that it dominates the variation with context rather than trading
# one error for another:
#
#   F1, corrected   DRESS  +74 against a band of  28    correction wins
#                   STRUT  +96              "     82    correction wins
#                   TRAP  +118              "    120    even
#   F2, measured    NURSE +100              "    104    even
#                   FLEECE -149             "    220    BAND WINS
#                   STRUT  -119             "    244    BAND WINS
#
# FLEECE's quartiles run -233..-13, so a flat +149 would put the -13
# occurrences 136 Hz too far FORWARD; the correction would create as much
# error as it removes.  STRUT is worse.
#
# NURSE looks borderline until you notice that '3' and '@' are handed the
# SAME target -- 8.6's note says schwa takes /3:/'s values -- and come out
# +100 and -48.  One target, two errors, opposite signs, differing only in
# whether the syllable carries the accent.  That is positional, and a
# per-vowel constant cannot express it.  So all three are left alone and
# tools/gb_vowel_audit.py prints the F2 quartiles beside the F1 ones, so the
# next person can see the reasoning rather than re-derive it.

#: engine symbol -> (F1 correction in Hz, median miss, quartile band)
F1_REACH = {
    'a': (+118, 'TRAP  measured 572 against 690, quartiles -182..-62'),
    'v': (+96,  'STRUT measured 548 against 644, quartiles -156..-74'),
    'e': (+74,  'DRESS measured 420 against 494, quartiles -90..-62'),
}

# 7.10.2(2): /U, u:, U@/ ARE FRONTED, AND THE TABLE CANNOT SHOW IT.
#
# This was the last open item, held back because the book looked as though it
# contradicted itself: 7.10.2(2) describes GB fronting /U, u:, U@/ to
# [}, }:, }@] -- "well established", the tier the notes commit to implementing
# -- while Table 6 gives /u:/ F2 1191, barely in front of /U/ at 1173, which
# is a back vowel.  Footnote 3 to 8.6 settles it (p.159, chapter 8's end
# notes, found this session):
#
#   Tables 4 and 6, and Figures 10 and 11, are Deterding (1990, 1997); the
#   /E:/ row of Table 4 is the first element of the diphthong [e@].  Table 5
#   was computed in the Phonetics Laboratory at Manchester.
#
# So the numbers are not Cruttenden's own and they are older than his prose.
# A change the 8th edition (2014) reports as already typical of the majority
# cannot appear in measurements it cites from an earlier study, and the
# tension was never the book disagreeing with itself -- it is older
# measurements against a later description of a change.  For these three
# vowels the prose is the later evidence and wins.  EVERY OTHER VOWEL KEEPS
# TABLE 6: this is not licence to prefer prose generally, it is one named
# change the measurements predate.
#
# Roach 2004 agrees in prose from the other side -- /u:/ is in his list of
# symbols whose RP position is "quite far removed from the Cardinal (or IPA)
# value" -- though his own Figure 1 still charts it back.  2004 again
# predates 2014.
#
# How far forward.  Cruttenden gives no figure for [}:], so the landmark is
# the central vowel of the same table, same speakers, same condition: /3:/
# F2 1436, which 8.6's note also makes the value for /@/.  Fronting must land
# between the measured back value and that, and the engine already does:
#
#   GOOSE  target 1191, measured 1364 -- 173 Hz in front, 72 short of central
#   FOOT   target 1173, measured 1172 -- not fronted at all
#
# GOOSE's +173 is an engine miss like the three F1 misses above, but it is a
# miss in the direction the book asks for and it stops short of central, so
# it is kept rather than corrected -- the output is the claim.  FOOT tracks
# its target exactly and so is still back.  In Table 6 the two are 18 Hz
# apart in F2; in our output they were 192 apart, which is the real defect.
# Moving FOOT by the same +173 restores the pairing at 1346, putting its
# output 18 Hz behind GOOSE's, as Deterding measured them.
#
# CURE's first element is /U/ and 7.10.2(2) names /U@/, so it takes the same
# +173.  Its base is Table 5 (Manchester, citation, three males), not
# Deterding, so this transfers an offset measured on one study to another;
# that is why 1028+173 is still behind Table 6's unfronted /U/.  Only the
# published change is applied, the vowel is not re-sourced.
#
# NOT applied: 7.10.2(2) adds that fronting "may also" reach the [U] of /@U/.
# "May" is not "does", GOAT is signed off by ear, and it is left alone.
#
#: engine symbol -> (F2 correction in Hz, why)
F2_FRONT = {
    'u': (+173, 'FOOT  1173->1346, the fronting GOOSE already shows'),
    'c': (+173, 'CURE  onset 1028->1201, same offset, Table 5 base'),
}


def load():
    d = tvdata.TvData(str(ROOT / 'lang/enu/data/engine.tvdata'))

    def at(va, n):
        return d.mem[va - d.base:va - d.base + n]

    cls_tab = struct.unpack_from('<I', d.mem, CLASS_REF - d.base)[0]
    pt = {k: at(v, 64) for k, v in PT.items()}
    a_t = {k: at(v, 24) for k, v in AT.items()}

    def klass(ch):
        return d.mem[cls_tab - d.base + (ord(ch) & 0xff)]

    return klass, pt, a_t


def table():
    """One row per vowel: what the engine aims at, and what GB wants."""
    klass, pt, a_t = load()
    rows = []
    for sym, name, src, f1, f2, gf1, gf2 in GB:
        c = klass(sym)
        us = (pt['f1'][c] << 2, (pt['f2'][c] << 3) + 500, pt['f3'][c] << 4)
        notes = []
        if sym in PRIMARY_F3_DONOR:
            donor, why = PRIMARY_F3_DONOR[sym]
            f3 = pt['f3'][klass(donor)] << 4
            notes.append('F3 %d->%d from %r: %s' % (us[2], f3, donor, why))
        else:
            f3 = us[2]
        if sym in F1_REACH:
            add, why = F1_REACH[sym]
            notes.append('F1 %d->%d, aimed past Cruttenden so the OUTPUT '
                         'reaches him: %s' % (f1, f1 + add, why))
            f1 = f1 + add
        if sym in F2_FRONT:
            add, why = F2_FRONT[sym]
            notes.append('F2 %d->%d, 7.10.2(2) fronting, which the tables '
                         'predate: %s' % (f2, f2 + add, why))
            f2 = f2 + add
        gb = (f1, f2, f3)
        old_glide = glide = None
        if c < 0x13:
            old_glide = (a_t['f1'][c] << 2, (a_t['f2'][c] << 3) + 500,
                         a_t['f3'][c] << 4)
            if gf1 is None:
                # A steady vowel: the glide target becomes the vowel itself.
                # That is how SQUARE stops being a diphthong, and how START,
                # NORTH and SQUARE shed their r-colouring without any choice
                # of ours -- the collapsed glide F3 simply stops being used.
                glide = gb
                if old_glide[2] < 2000:
                    notes.append('r-colour dropped: glide F3 %d unused now'
                                 % old_glide[2])
            else:
                g3 = old_glide[2]
                if sym in GLIDE_F3_DONOR:
                    gd, gwhy = GLIDE_F3_DONOR[sym]
                    g3 = pt['f3'][klass(gd)] << 4
                    notes.append('glide F3 %d->%d from %r: %s'
                                 % (old_glide[2], g3, gd, gwhy))
                glide = (gf1, gf2, g3)
        rows.append(dict(sym=sym, name=name, src=src, cls=c, us=us, gb=gb,
                         old_glide=old_glide, glide=glide, notes=notes))
    return rows


def q_f1(hz):
    return max(0, min(255, (hz + 2) >> 2))


def q_f2(hz):
    return max(0, min(255, (hz - 500 + 4) >> 3))


def q_f3(hz):
    return max(0, min(255, (hz + 8) >> 4))


def emit_c(rows):
    print('/* Generated by tools/gen_gb_vowels.py -- do not edit by hand.')
    print(' * General British vowel targets; provenance is in that file and')
    print(' * in lang/eng/research/gimson8.md. */')
    print('static const tv_gb_vowel g_gb_vowel[] = {')
    for r in rows:
        f = (q_f1(r['gb'][0]), q_f2(r['gb'][1]), q_f3(r['gb'][2]))
        g = r['glide'] or r['gb']
        gq = (q_f1(g[0]), q_f2(g[1]), q_f3(g[2]))
        print("    { '%s', 0x%02x, { %3d, %3d, %3d }, { %3d, %3d, %3d } },"
              '  /* %-7s %s */'
              % (r['sym'], r['cls'], f[0], f[1], f[2], gq[0], gq[1], gq[2],
                 r['name'], r['src']))
    print('};')


def main():
    rows = table()
    if '--c' in sys.argv:
        emit_c(rows)
        return
    print('American engine target -> General British, male values, Hz')
    print('%-4s %-8s %-17s    %-17s %s'
          % ('sym', 'set', 'engine F1 F2 F3', 'GB F1 F2 F3', 'delta'))
    print('-' * 78)
    for r in rows:
        us, gb = r['us'], r['gb']
        d = [b - a for a, b in zip(us, gb)]
        print('%-4s %-8s %5d %5d %5d -> %5d %5d %5d   %+5d %+5d %+5d'
              % (r['sym'], r['name'], us[0], us[1], us[2],
                 gb[0], gb[1], gb[2], d[0], d[1], d[2]))
    print()
    print('glide target -- the second half of a diphthong:')
    for r in rows:
        if r['old_glide'] is None:
            continue
        og, g = r['old_glide'], r['glide']
        tag = '   <- STEADY now' if g == r['gb'] else ''
        print('  %-4s %-8s %5d %5d %5d -> %5d %5d %5d%s'
              % (r['sym'], r['name'], og[0], og[1], og[2],
                 g[0], g[1], g[2], tag))
    print()
    print('F3: Cruttenden publishes F1 and F2 only, so every F3 below is the')
    print("engine's own except where a note says otherwise.")
    for r in rows:
        for note in r['notes']:
            print('  %-4s %-8s %s' % (r['sym'], r['name'], note))
    print()
    for src in sorted({r['src'] for r in rows}):
        n = sum(1 for r in rows if r['src'] == src)
        print('  %2d vowels from %s' % (n, src))


if __name__ == '__main__':
    main()
