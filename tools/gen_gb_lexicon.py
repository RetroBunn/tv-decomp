# -*- coding: utf-8 -*-
"""The General British lexicon: the words whose VOWEL CHOICE differs from
American, which no formant shift can reach.

    python -B tools/gen_gb_lexicon.py            show what each word becomes
    python -B tools/gen_gb_lexicon.py --c        emit the C table

src/engine/gb.c moves where each vowel is aimed.  That is the whole accent
for most words, but not for the ones where GB and American pick a DIFFERENT
vowel for the same word: *bath* is /a/ in America and /A:/ in Britain, and no
amount of re-aiming /a/ turns one into the other.  Those are listed here.

WHAT THE PATTERNS ARE WRITTEN AGAINST.  The phonemes as STAGE 1 HOLDS THEM,
which is not what tvtts_text_to_phonemes prints: stress has not been assigned
yet, stop releases have not been added, and /st/ is still spelt with a plain
T.  So *student* is STbD|NT in the engine and SDb1t|NTp on the page, and a
pattern written from the printed form puts the yod substitution on the second
/t/ -- which is what happened before build/check/gb_spans.exe existed to say
so.  Every pattern below is checked to occur EXACTLY ONCE in its word, so
none of this depends on picking the right occurrence.

WHY THESE ARE LISTS AND NOT RULES.  Measured, not assumed.  The environment
Cruttenden gives for BATH -- before a voiceless fricative, or a nasal plus a
consonant -- also catches *gas, mass, maths, ant, hand, random, ample* and
*anger*, which keep /a/ in GB; and *ant* and *aunt* come out of the American
engine as the same string.  See lang/eng/research/gimson8.md.
"""
import ctypes as C
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# --- the word lists ---------------------------------------------------------
#
# BATH, tier 1.  Cruttenden 8th ed. 8.9.7: the spellings table on p.124 (its
# <a>, <al> and <au> rows -- the <ar>/<ear>/<er> rows are not here because the
# American engine already returns its START vowel for those), the Sources
# table on p.125, and the words named in the prose on p.125.
BATH_BOOK = """
    bath path dance last pass past plaster raft calf half aunt laugh
    after staff ask cast rather advance grass chance answer example basket
    branch chant command
""".split()

# BATH, tier 2.  NOT from the book: ordinary BATH-set membership, added
# because leaving out *class* and *fast* would be audible in almost any
# sentence.  Less authoritative than tier 1, and the first place to look if a
# word sounds wrong.
BATH_COMMON = """
    class glass brass grasp clasp mask task flask blast vast fast master
    disaster plant grant slant demand craft draft shaft graph
    behalf nasty ghastly sample
""".split()

# Real inflections, written out rather than generated: suffixing by rule
# produced "aunter", "askes" and "advanceed", which are not words, and the
# engine will happily analyse any of them.
BATH_INFLECTED = """
    baths paths dances danced dancing dancer dancers lasts lasted lasting
    passes passed passing passer plasters plastered plastering rafts rafter
    rafters calves halves aunts laughs laughed laughing laughter asks asked
    asking casts casting advances advanced advancing grasses chances chanced
    answers answered answering examples baskets basketball branches branched
    branching chants chanted chanting commands commanded commanding classes
    glasses masks tasks blasted plants planted planting grants granted
    demands demanded drafts graphs afternoon afterwards bathroom
""".split()

BATH = BATH_BOOK + BATH_COMMON + BATH_INFLECTED

# PALM.  The same GB vowel as BATH, but American spells it with its LOT vowel
# rather than its TRAP one, so the substitution differs.  A list and not a
# rule because LOT words -- pot, hot, bother, wash, want, collar -- use that
# same American vowel and must not move.
PALM_FROM_LOT = """
    father calm palm balm psalm spa bra drama pasta lager sonata
""".split()

# ...and the "foreign a" words, which American gives its TRAP vowel but GB
# gives /A:/, outside Cruttenden's stated environment entirely.
PALM_FROM_TRAP = """
    llama lava plaza banana sultana pyjamas
""".split()

# CLOTH.  Cruttenden 8.9.8(3), p.127: "a smaller group of words with /Q/ in
# GB may have /O:/ in General American (including those words mentioned above
# which have a following voiceless fricative or a nasal), e.g. across, gone,
# dog, borrow"; and Note (1) to that section's Sources table, that ME [O]
# before [f,T,s] and nasals -- across, broth, lost, off, gone -- "has reverted
# to /Q/; pronunciations with /O:/ are nowadays considered CGB (or Cockney)".
# So GB wants the short vowel here and the long one is a marker to avoid.
# *laurel* was here and has been moved to the short-<r> list in
# src/engine/gb.c: the substitution could not survive, because `o` in front
# of an `R` folds straight back to START, so it needs the syllable marker
# that only the stage-1 rule can insert.
# The rest are from that section's own spellings table on p.126.
CLOTH = """
    dog cloth off often long song strong wrong boss cross loss lost soft
    coffee office across frost gone broth because sausage
    Austria cauliflower cough trough knowledge
""".split()

# CURE -> NORTH, Cruttenden 7.10.2(8), among the changes the book calls WELL
# ESTABLISHED: /O:/ may replace /U@/.  It says the change is lexically
# conditioned rather than general, rules it out in *dour, gourd, lure, Ruhr,
# Ure* and in the /u:/+/@/ words *doer, fewer, newer, viewer, sewer*, and sets
# *pure* against *poor* as its worked contrast.
#
# A word list and not a rule, and here the engine proves that as firmly as the
# book does: *cure* is KYbR at stage 1 and *pure* is PYbR, identical after the
# first consonant, so no pattern over the phonemes could ever separate them --
# the same wall *ant* and *aunt* put up in BATH.  *gourd* GbRD and *lure* LbR
# carry the same bR as *poor* and must not move either.
#
# *cure* is left OUT on purpose.  The section lists it among the words that
# may take /O:/, but its own contrast puts the yod words on the conservative
# side and *cure* is one of them, so /kjO:/ goes a step further than the book
# demonstrates.  One line to add if the ear wants it.
CURE_TO_NORTH = "poor sure tour poorly surely tourist".split()
#: *moor* already has the CURE vowel outright at stage 1 -- McR, not the bR
#: the others have -- so it needs its own substitution.
CURE_TO_NORTH_C = "moor".split()

# Yod coalescence, Cruttenden 7.10.1(5): /tj,dj/ "are regularly changed to
# /tS,dZ/", listed among the changes that are ALMOST COMPLETE in GB.  Only the
# words where American dropped the yod outright are here; where American has
# coalesced already -- costume, educate, education, graduate, individual,
# module, schedule -- there is nothing to do, which the probe confirmed.
YOD_T = """
    tube tuba tune tuner Tuesday tutor tulip tumour tuna tunic tumult
    attitude altitude gratitude institute constitute obtuse opportunity
    student studio stupid stew
""".split()
YOD_D = """
    duty duke dune dual duel dubious duration dutiful reduce produce endure
""".split()
# *during* has the CURE vowel already at stage 1; *endure* does not -- it is
# still DbR there -- so it goes with the plain /dj/ words above.
YOD_DC = "during".split()

# Yod dropping, Cruttenden 7.10.1(2): /j/ "is lost before /u:/ following
# /l,s,z/".  American keeps it in exactly one of the candidates probed.
YOD_DROP = "exhume".split()

# The substitutions, in stage 1's phonemes.  The vowel is kept as context in
# the yod rules so the edit lands on the consonant: overwriting the vowel node
# loses the stress the engine later wants to put on it, and *student* needs
# the vowel to tell its two /t/s apart.
#
#: (list, [(from, to), ...], why)
RULES = [
    (BATH,           [('a', 'r')], 'BATH 8.9.7'),
    (PALM_FROM_LOT,  [('o', 'r')], 'PALM 8.9.7'),
    (PALM_FROM_TRAP, [('a', 'r')], 'PALM 8.9.7'),
    (CLOTH,          [('w', 'o'), ('v', 'o')], 'CLOTH 8.9.8'),
    (CURE_TO_NORTH,   [('bR', 'wR')], 'CURE to NORTH 7.10.2(8)'),
    (CURE_TO_NORTH_C, [('cR', 'wR')], 'CURE to NORTH 7.10.2(8)'),
    (YOD_T,          [('Tb', 'Csb')], 'yod coalescence 7.10.1(5)'),
    (YOD_D,          [('Db', 'Jzb')], 'yod coalescence 7.10.1(5)'),
    (YOD_DC,         [('Dc', 'Jzc')], 'yod coalescence 7.10.1(5)'),
    (YOD_DROP,       [('Yb', 'b')],   'yod dropping 7.10.1(2)'),
]


def spans(words):
    """What stage 1 holds for each word, from build/check/gb_spans.exe."""
    exe = ROOT / 'build' / 'check' / 'gb_spans.exe'
    if not exe.exists():
        raise SystemExit('build first: %s is missing' % exe)
    out = subprocess.run([str(exe)] + list(words), capture_output=True,
                         text=True, check=True).stdout
    got = {}
    for line in out.splitlines():
        if '\t' in line:
            key, span = line.split('\t', 1)
            got.setdefault(key, span.lstrip('&%'))
    return got


def printed():
    """And what the phoneme printer shows, which is only for the report."""
    d = C.CDLL(str(ROOT / 'build' / 'bin' / 'tvtts64.dll'))
    d.tvtts_create_lang.argtypes = [C.c_uint32, C.c_char_p]
    d.tvtts_create_lang.restype = C.c_void_p
    d.tvtts_text_to_phonemes.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p,
                                         C.c_uint32]
    d.tvtts_text_to_phonemes.restype = C.c_int
    syn = d.tvtts_create_lang(11025, b'en')

    def one(word):
        buf = C.create_string_buffer(256)
        if d.tvtts_text_to_phonemes(syn, word.encode(), buf, 256) < 0:
            return '?'
        return buf.value.decode('latin-1').lstrip('&%').rstrip('.')

    return one


def build():
    """Every entry, plus the words a rule did not fire cleanly on."""
    allwords = sorted({w for words, _, _ in RULES for w in words})
    span = spans(allwords)
    show = printed()
    entries = {}
    misses = []
    for words, subs, why in RULES:
        for w in words:
            key = w.upper()
            sp = span.get(key)
            if sp is None:
                misses.append((w, why, 'stage 1 gave no span'))
                continue
            hit = None
            for f, t in subs:
                n = sp.count(f)
                if n == 1:
                    hit = (f, t)
                    break
                if n > 1:
                    # Never guess which one: an ambiguous pattern is a fault
                    # in the list, not something to settle by position.
                    misses.append((w, why, '%r appears %d times in %s'
                                   % (f, n, sp)))
                    hit = False
                    break
            if hit is None:
                misses.append((w, why, 'none of %s in %s'
                               % ('/'.join(f for f, _ in subs), sp)))
                continue
            if hit is False:
                continue
            f, t = hit
            if key in entries and entries[key][0] != (f, t):
                misses.append((w, why, 'clashes with an earlier rule'))
                continue
            entries[key] = ((f, t), sp, sp.replace(f, t, 1), show(w), why)
    return entries, misses


HEADER = [
    '/* Generated by tools/gen_gb_lexicon.py -- do not edit by hand.',
    ' *',
    ' * Words whose VOWEL CHOICE differs between American and General',
    ' * British, which no formant shift can reach.',
    ' *',
    ' * A substitution, not a pronunciation: stage 1 does its own work and',
    ' * this swaps the phonemes afterwards, so the word class, the stress',
    ' * and every context-sensitive allophone survive.  Replacing the whole',
    ' * word instead lost all three -- "after" stopped being unaccented,',
    ' * "because" lost its weak form, and "asked" froze its isolated /t/.',
    ' *',
    ' * The patterns are in the phonemes STAGE 1 HOLDS, which is not what',
    ' * the phoneme printer shows; see tools/gb_spans.c.  Each is checked to',
    ' * occur exactly once in its word, so nothing here depends on picking',
    ' * the right occurrence.',
    ' *',
    ' * Sorted, for bsearch; keys are upper case. */',
    'static const tv_gb_sub_entry g_gb_lex[] = {',
]


def main():
    entries, misses = build()
    if '--c' in sys.argv:
        for line in HEADER:
            print(line)
        for key in sorted(entries):
            (f, t), sp, gb, show, why = entries[key]
            pad = ' ' * max(1, 26 - len(key) - len(f) - len(t))
            print('    { "%s", "%s", "%s" },%s/* %-26s %s -> %s */'
                  % (key, f, t, pad, why, sp, gb))
        print('};')
        return 0

    print('General British lexicon: %d entries' % len(entries))
    print()
    by_rule = {}
    for key in sorted(entries):
        (f, t), sp, gb, show, why = entries[key]
        by_rule.setdefault(why, []).append((key, f, t, sp, gb, show))
    for why in sorted(by_rule):
        rows = by_rule[why]
        print('=== %s (%d) ===' % (why, len(rows)))
        for key, f, t, sp, gb, show in rows:
            print('  %-13s %-3s -> %-4s %-15s -> %-15s (printed %s)'
                  % (key.lower(), f, t, sp, gb, show))
        print()
    if misses:
        print('=== the rule did not fire cleanly (%d) ===' % len(misses))
        for w, why, reason in misses:
            print('  %-14s %-28s %s' % (w, why, reason))
    return 0


if __name__ == '__main__':
    sys.exit(main())
