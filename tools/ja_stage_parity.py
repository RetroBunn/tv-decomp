# -*- coding: utf-8 -*-
"""Compare every NJD stage against the compiled Open JTalk reference.

    python tools/ja_stage_parity.py [--n 20000] [--build]

WHY.  Reading Open JTalk's rules and writing them out again produced four bugs
that reading could not catch -- adjacent devoicing, compound entries, the
normalisation direction and the mora inventory -- and all four were found by
running the real thing instead.  Three of the four were in stages this checks;
the fourth was in the compiler.

`build/check/openjtalk_morph_audit.py` (Astra) established that the checkout's
stages can be driven from Python without Open JTalk's synthesiser, and spot-
checked devoicing on nine cases.  This runs all four stages over a corpus and
compares every word, because "I read eighteen rules correctly" is an
assumption, and the eighteen accent-phrase rules and the chain-rule grammar
had never been compared at all.

HOW THE COMPARISON IS CONTROLLED.  Both sides are given THE SAME input nodes,
produced by this project's own analyser.  So this does not test the Viterbi,
the dictionary interpretation or the normalisation -- only the stages, which
is the one thing it can isolate.  A mismatch is therefore unambiguous: the
rules disagree.

Reports the first few disagreements per stage with enough context to act on,
and the counts.  Exit status is non-zero if anything disagrees.
"""
import argparse, io, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'build', 'Japanese_test'))
sys.path.insert(0, os.path.join(ROOT, 'build', 'check'))

import jp_dict as D
import jp_njd as N
import jp_digit as G


PERIOD = u'。'      # 。
PAUSE = u'、'       # 、
QUESTION = u'？'    # ？
UNVOICED_MARK = u'’'   # the devoicing mark, which is not itself a mora


def declared_pause(mine, ref):
    """A difference this project makes on purpose, not a rule misread.

    Open JTalk collapses every unreadable word and every punctuation mark
    into one pause symbol, 、 -- both 。 and 、 have the accent field `*/*`,
    so both fall through njd_set_pronunciation's unknown branch and come out
    as 、.  It can afford to: the sentence-versus-clause distinction is made
    later, in the full-context labels its HMM synthesiser reads.

    This front end has nowhere later to make it.  Its prosody is Kawai's, and
    his phrase commands grade the boundary -- P2 for a clause, the weaker P3
    within one -- which this project already implements as `||` with a 0.3 s
    pause and `|` with 0.1 s.  Throwing the distinction away at this stage and
    then having no way to recover it would make every sentence boundary a
    comma.  So 。 is kept as 。, and that is a divergence with a reason rather
    than a disagreement.
    """
    return mine.pron == PERIOD and ref.pron == PAUSE


def declared_abort(words):
    """-> the first mora Open JTalk's devoicing stage cannot tokenise, or None.

    njd_set_unvoiced_vowel walks each pronunciation with a longest-prefix
    match against its 159-mora inventory, and when a character is not in it
    the stage prints

        WARNING: set_unvoiced_vowel() in njd_set_unvoiced_vowel.c: Wrong pron.

    and RETURNS.  Not skips the word -- returns, so every word from there to
    the end of the utterance keeps its pronunciation unexamined and nothing
    after the offending character is devoiced at all.  Its only exceptions are
    the two whole-word symbols 、 and ？.

    Two characters reach it here and neither is a rule this project misread:

      ヮ   small katakana wa.  クヮルテット (quartet) is a real naist-jdic
           entry whose own reading upstream cannot tokenise, so the warning
           fires on the dictionary's own data.  The stage then leaves the
           four words after it undevoiced, which is why `つかねっ` came out
           ツカネッ in the reference and ツ’カネッ on its own.

      。   kept rather than folded to 、; see declared_pause.  Upstream's
           special case tests for 、 exactly, so a 。 is looked up as a mora,
           found not to be one, and aborts the stage.

    Comparing past either point measures upstream's early return, not its
    rules, so an utterance containing one is declared rather than counted.
    This project does not copy the behaviour: an unknown character becomes a
    mora of its own and the walk continues, because the goal is accurate
    pronunciation of the rest of the sentence.
    """
    from ojt_tables import MORA, MORA_MAX
    for w in words:
        if w.pron in (PAUSE, QUESTION):
            continue                    # upstream's two whole-word symbols
        i, n = 0, len(w.pron)
        while i < n:
            if w.pron[i] == UNVOICED_MARK:
                i += 1
                continue
            for k in range(min(MORA_MAX, n - i), 0, -1):
                if w.pron[i:i + k] in MORA:
                    i += k
                    break
            else:
                return w.pron[i]
    return None


def clone(w):
    c = D.Word(w.string, w.pos, w.pron, w.acc, w.mora_size, w.chain_rule,
               read=w.read)
    c.chain_flag = w.chain_flag
    return c


def corpus(n, seed=19):
    """Dictionary surface forms strung together, plus real sentences.

    Random concatenation is crude as Japanese but it is the point: it puts
    part-of-speech pairs next to each other that a natural corpus would take
    a very long time to cover, and the eighteen rules are all about which
    pair is adjacent.
    """
    import random
    d = D.load()
    rnd = random.Random(seed)
    pool = []
    step = max(1, d.n_entries // 40000)
    for i in range(0, d.n_entries, step):
        s = d.surface(d.entry(i)[0])
        if 1 <= len(s) <= 6:
            pool.append(s)
    out = [u''.join(rnd.choice(pool) for _ in range(rnd.randint(2, 9)))
           for _ in range(n)]
    out += [
        u'私は毎日学校へ行きます。', u'今日はいい天気ですね。',
        u'電車が駅に到着しました。', u'その本を読んでください。',
        u'明日の会議は何時からですか？', u'東京都の人口は増えています。',
        u'友達と一緒に図書館で勉強しました。',
        u'日本語学校の先生はとても親切です。',
        u'ありがとうございます。', u'せっぱ詰まる', u'複数の美しさ',
        u'田中さんは大阪から来ました。', u'値段は1250円です。',
    ]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--n', type=int, default=20000)
    ap.add_argument('--build', action='store_true',
                    help='rebuild the reference DLL first')
    ap.add_argument('--show', type=int, default=4)
    args = ap.parse_args(argv)

    if args.build:
        sys.argv = [sys.argv[0], '--build']
    try:
        import openjtalk_morph_audit as A
    except Exception as e:
        print('cannot load the reference harness: %s' % e)
        print('build it with: python build/check/openjtalk_morph_audit.py --build')
        return 2

    # Every stage in order, and the fields each one is allowed to change.
    PLAN = [
        ('set_pronunciation', ('pron', 'pos', 'mora_size'),
         lambda w: N.set_pronunciation(w)),
        ('set_digit', ('pron', 'acc', 'mora_size', 'chain_flag'),
         lambda w: G.set_digit(w)),
        ('set_accent_phrase', ('chain_flag',),
         lambda w: N.set_accent_phrase(w)),
        ('set_accent_type', ('acc',),
         lambda w: N.set_accent_type(w)),
    ]

    texts = corpus(args.n)
    print('%d inputs, reference = %s' % (len(texts), A.lib._name))
    print('both sides are given the same input nodes, so a disagreement is')
    print('the rules disagreeing and nothing else.\n')

    bad = {name: 0 for name, _, _ in PLAN}
    bad['set_unvoiced_vowel'] = 0
    declared = 0
    aborted = 0
    abort_cause = {}
    utterances = 0
    shown = {k: 0 for k in bad}
    seen = {k: 0 for k in bad}
    length = 0

    for text in texts:
        words = D.analyse(text)
        if not words:
            continue
        mine = [clone(w) for w in words]
        for name, fields, fn in PLAN:
            ref = A.run([clone(w) for w in mine], [name])
            mine = fn(mine)
            seen[name] += len(mine)
            if len(ref) != len(mine):
                length += 1
                if shown[name] < args.show:
                    print('  %-18s %s' % (name, text[:30]))
                    print('      %d words out, reference gave %d'
                          % (len(mine), len(ref)))
                    shown[name] += 1
                bad[name] += 1
                mine = ref           # resynchronise so later stages still run
                continue
            for a, b in zip(mine, ref):
                diff = [f for f in fields
                        if getattr(a, f) != getattr(b, f)]
                if diff == ['pron'] and declared_pause(a, b):
                    declared += 1
                    continue
                if diff:
                    bad[name] += 1
                    if shown[name] < args.show:
                        print('  %-18s %s' % (name, text[:30]))
                        print('      word %-10s %s'
                              % (a.string, ', '.join(
                                  '%s mine=%r ref=%r'
                                  % (f, getattr(a, f), getattr(b, f))
                                  for f in diff)))
                        print('      pos %s  rule %s'
                              % ('/'.join(x for x in a.pos if x != u'*'),
                                 a.chain_rule))
                        shown[name] += 1
        # devoicing last, over the whole utterance, compared as the
        # pronunciation string the stage writes back
        ref = A.run([clone(w) for w in mine], ['set_unvoiced_vowel'])
        got = N.set_unvoiced_vowel([clone(w) for w in mine])
        seen['set_unvoiced_vowel'] += len(got)
        utterances += 1
        mine_s = u''.join(m + (u'’' if dv else u'') for m, dv, _ in got)
        ref_s = u''.join(w.pron for w in ref)
        stop = declared_abort(mine)
        if mine_s != ref_s and stop is not None:
            aborted += 1
            abort_cause[stop] = abort_cause.get(stop, 0) + 1
        elif mine_s != ref_s:
            bad['set_unvoiced_vowel'] += 1
            if shown['set_unvoiced_vowel'] < args.show:
                print('  %-18s %s' % ('set_unvoiced_vowel', text[:30]))
                print('      mine %s' % mine_s)
                print('      ref  %s' % ref_s)
                shown['set_unvoiced_vowel'] += 1

    print()
    print('%-20s %10s %10s' % ('stage', 'compared', 'disagree'))
    total = 0
    for k in ('set_pronunciation', 'set_digit', 'set_accent_phrase',
              'set_accent_type', 'set_unvoiced_vowel'):
        # The devoicing stage is compared as one string per utterance, so its
        # unit is the utterance; `compared` is still morae, which is the
        # number of decisions those utterances contain.
        unit = 'utterances, %d morae' % seen[k] \
            if k == 'set_unvoiced_vowel' else 'words'
        print('%-20s %10d %10d   (%s)'
              % (k, utterances if k == 'set_unvoiced_vowel' else seen[k],
                 bad[k], unit))
        total += bad[k]
    if length:
        print('%d of those were a word-count difference' % length)
    if declared:
        print('%d declared divergences, not counted: see declared_pause'
              % declared)
    if aborted:
        print('%d utterances where the reference aborted devoicing, not '
              'counted: see declared_abort' % aborted)
        for ch in sorted(abort_cause, key=lambda c: -abort_cause[c]):
            print('    %s  %d' % (ch, abort_cause[ch]))
    print()
    print('%s' % ('every stage agrees with the reference' if total == 0
                  else '%d disagreements' % total))
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
