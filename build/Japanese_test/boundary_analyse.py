# -*- coding: utf-8 -*-
"""Apply the pre-registered decision rule to the completed annotation.

Run from the repository root:  python build/Japanese_test/boundary_analyse.py

Written BEFORE any annotation exists, so the rule cannot be chosen to suit the
result.  Rule by Astra:

  * a detector boundary shows REPEATABLE DIRECTIONAL DISAGREEMENT when it falls
    outside BOTH passes' plausible intervals, ON THE SAME SIDE
  * if it falls inside either interval it is UNRESOLVED BY THIS AUDIT -- not
    "correct"
  * the distance to the nearest edge of the combined interval is reported in ms
  * onset and offset are judged SEPARATELY as well as through duration, because
    two misplaced boundaries can produce an apparently correct duration
  * the conservative duration interval is [e_early - s_late, e_late - s_early]

An error-to-uncertainty ratio was considered and rejected: it goes unstable
when the annotator gives a very narrow interval.

PRIMARY HYPOTHESIS, pre-registered: the detector UNDERESTIMATES short /i/ in
the three stop contexts.  Agreement in direction across all three supports a
systematic problem in the contexts tested.  Mixed results support a
context-dependent one.  Neither establishes population-wide bias.

WHAT THIS CAN AND CANNOT SAY.  Every result is "detector bias relative to this
annotator and protocol".  Two passes by one person establish repeatability, not
inter-annotator agreement, and a person can repeat a systematic mistake.  The
earliest-latest interval is the annotator's stated ambiguity on that occasion,
not a confidence interval.  Comparison against the reference establishes a
duration discrepancy from a SELECTED REFERENCE CONDITION, not automatically an
error in Japanese synthesis: speakers vary and the baseline is a design choice.
The between-study annotation difference is unmeasured -- the reference was
annotated by other people, on natural speech, in another lab.

Results are reported by vowel, length and context and are NOT pooled into an
overall figure.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'boundary_audit')
BLIND = os.path.join(OUT, 'blind')
REVEALED = os.path.join(OUT, 'revealed')
CAP_REPORTED = 16          # the cap the synthesiser currently runs


def read_sheet(path):
    out = {}
    if not os.path.exists(path):
        return out
    for ln in open(path):
        if not ln.strip() or ln.startswith('#') or ln.startswith('file\t'):
            continue
        c = ln.rstrip('\n').split('\t')
        if len(c) < 7 or not any(x.strip() for x in c[1:7]):
            continue
        def f(i):
            v = c[i].strip()
            if not v or v.lower() == 'indet':
                return None
            try:
                return float(v)
            except ValueError:
                return None
        out[c[0]] = {'on': (f(1), f(2), f(3)), 'off': (f(4), f(5), f(6)),
                     'note': c[7].strip() if len(c) > 7 else ''}
    return out


def read_map():
    m = {}
    p = os.path.join(REVEALED, 'token_map.tsv')
    for ln in open(p):
        if ln.startswith('file\t'):
            continue
        c = ln.rstrip('\n').split('\t')
        m[c[0]] = {'word': c[1], 'vowel': c[2], 'length': c[3],
                   'context': c[4], 'intended': float(c[5])}
    return m


def read_labels(tid):
    """-> {cap_ms: (start_s, end_s)} and the schedule."""
    p = os.path.join(REVEALED, tid + '.labels.txt')
    det, sched = {}, None
    for ln in open(p):
        a, b, lab = ln.rstrip('\n').split('\t')
        if lab.startswith('schedule'):
            sched = (float(a), float(b))
        elif 'cap=' in lab:
            cap = int(lab.split('cap=')[1].split('ms')[0])
            det[cap] = (float(a), float(b))
    return det, sched


def verdict(x, iv1, iv2):
    """x against two passes' intervals -> (verdict, distance_ms, side)."""
    if x is None:
        return 'no detector value', None, ''
    ivs = [iv for iv in (iv1, iv2) if iv[0] is not None and iv[1] is not None]
    if len(ivs) < 2:
        return 'incomplete annotation', None, ''
    sides = []
    for lo, hi in ivs:
        if x < lo:
            sides.append('early')
        elif x > hi:
            sides.append('late')
        else:
            sides.append('in')
    if 'in' in sides:
        lo = min(i[0] for i in ivs); hi = max(i[1] for i in ivs)
        d = 0.0 if lo <= x <= hi else min(abs(x - lo), abs(x - hi)) * 1000.0
        return 'unresolved by this audit', d, ''
    if sides[0] != sides[1]:
        return 'disagrees, but not in a consistent direction', None, ''
    lo = min(i[0] for i in ivs); hi = max(i[1] for i in ivs)
    d = (lo - x if sides[0] == 'early' else x - hi) * 1000.0
    return 'REPEATABLE DIRECTIONAL DISAGREEMENT', d, sides[0]


def main():
    p1 = read_sheet(os.path.join(BLIND, 'sheet_pass1.tsv'))
    p2 = read_sheet(os.path.join(BLIND, 'sheet_pass2.tsv'))
    if not p1 or not p2:
        print('Need both passes filled in before this means anything.')
        print('  pass 1: %d rows   pass 2: %d rows' % (len(p1), len(p2)))
        print('  sheets live in %s' % BLIND)
        return
    tm = read_map()

    print(__doc__.strip().splitlines()[0])
    print('\ndetector cap = %d ms, the setting the synthesiser runs\n' % CAP_REPORTED)
    hdr = '%-5s %-8s %-6s %-6s  %-38s %9s'
    print(hdr % ('file', 'word', 'vowel', 'len', 'onset', 'dist ms'))
    print('-' * 80)
    rows = []
    for tid in sorted(set(p1) & set(p2)):
        info = tm.get(tid, {})
        det, sched = read_labels(tid)
        d_on, d_off = det.get(CAP_REPORTED, (None, None))
        a1, a2 = p1[tid], p2[tid]
        von = verdict(d_on, (a1['on'][0], a1['on'][2]), (a2['on'][0], a2['on'][2]))
        voff = verdict(d_off, (a1['off'][0], a1['off'][2]),
                       (a2['off'][0], a2['off'][2]))
        rows.append((tid, info, von, voff, a1, a2, d_on, d_off))
        print(hdr % (tid, info.get('word', '?'), info.get('vowel', '?'),
                     info.get('length', '?'), von[0],
                     '%.1f' % von[1] if von[1] is not None else '-'))
        print(hdr % ('', '', '', '', 'offset: ' + voff[0],
                     '%.1f' % voff[1] if voff[1] is not None else '-'))

    print('\nconservative duration interval vs the detector, per token')
    print('%-5s %-8s %18s %12s %12s' % ('file', 'word', 'annotator (ms)',
                                        'detector', 'reference'))
    for tid, info, von, voff, a1, a2, d_on, d_off in rows:
        lo = hi = None
        se = [a[ 'on'][0] for a in (a1, a2) if a['on'][0] is not None]
        sl = [a[ 'on'][2] for a in (a1, a2) if a['on'][2] is not None]
        ee = [a['off'][0] for a in (a1, a2) if a['off'][0] is not None]
        el = [a['off'][2] for a in (a1, a2) if a['off'][2] is not None]
        if se and sl and ee and el:
            lo = (min(ee) - max(sl)) * 1000.0
            hi = (max(el) - min(se)) * 1000.0
        dd = (d_off - d_on) * 1000.0 if (d_on is not None) else None
        print('%-5s %-8s %18s %12s %12s'
              % (tid, info.get('word', '?'),
                 '%.1f - %.1f' % (lo, hi) if lo is not None else 'incomplete',
                 '%.1f' % dd if dd is not None else '-',
                 '%.1f' % info['intended'] if 'intended' in info else '-'))

    short_i = [r for r in rows if r[1].get('vowel') == 'i'
               and r[1].get('length') == 'short']
    if short_i:
        print('\nPRE-REGISTERED PRIMARY TEST: detector underestimates short /i/')
        for tid, info, von, voff, a1, a2, d_on, d_off in short_i:
            print('   %-5s %-8s %-10s onset %-42s offset %s'
                  % (tid, info['word'], info['context'], von[0], voff[0]))
        print('   Agreement in direction across all three contexts supports a')
        print('   systematic problem IN THE CONTEXTS TESTED.  Mixed results')
        print('   support a context-dependent one.  Neither establishes')
        print('   population-wide bias, and all of it is relative to this')
        print('   annotator and protocol.')


if __name__ == '__main__':
    main()
