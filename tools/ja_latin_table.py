# -*- coding: utf-8 -*-
"""The Latin-word reading, as a table to read before anything is rendered.

    python tools/ja_latin_table.py

Astra's recommendation, and the point of doing it this way round: settle
whether the readings are any good BEFORE paying for the engine work that
would make them cheap.  So this prints kana and not audio, over the five
categories that pull in different directions --

    familiar words      the dictionary should win, and where it has no entry
                        this is what stands in
    invented words      the actual target: a plausible reading for something
                        nothing can know
    initialisms         must stay spelled out, in Japanese letter names
    ambiguous romaji    `take` is both タケ and テイク and nothing in the
                        spelling decides which
    mixed sentences     a dictionary reading, a guess and Japanese text in
                        one utterance

-- and marks which of the four steps produced each reading, because a reading
that is right for the wrong reason is not evidence.

The `want` column is the established loanword where there is one.  Disagreeing
with it is not automatically a defect: an established loanword reflects
spelling and borrowing history as much as source pronunciation -- ボタン from
`button`'s o, コンピューター from `computer`'s -- and those words should come
from the dictionary anyway.  What the rules have to be good at is the word the
dictionary does NOT have.
"""
import argparse
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'build', 'Japanese_test'))

import jp_dict as D
import jp_njd as N
import jp_g2p as G

# (word, the established loanword or None)
FAMILIAR = [
    ('computer', u'コンピューター'), ('printer', u'プリンター'),
    ('screen', u'スクリーン'), ('file', u'ファイル'),
    ('mouse', u'マウス'), ('test', u'テスト'),
    ('desk', u'デスク'), ('cat', u'キャット'),
    ('cup', u'カップ'), ('book', u'ブック'),
    ('bed', u'ベッド'), ('bottle', u'ボトル'),
    ('little', u'リトル'), ('button', u'ボタン'),
    ('water', u'ウォーター'), ('stop', u'ストップ'),
    ('rock', u'ロック'), ('box', u'ボックス'),
    ('bus', u'バス'), ('much', u'マッチ'),
    ('music', u'ミュージック'), ('cute', u'キュート'),
    ('fire', u'ファイア'), ('google', u'グーグル'),
    ('internet', u'インターネット'),
    ('microsoft', u'マイクロソフト'),
    ('hello', u'ハロー'), ('orange', u'オレンジ'),
    ('table', u'テーブル'), ('email', None),
    ('window', u'ウィンドー'), ('phone', u'フォン'),
]
INVENTED = ['blorf', 'zindle', 'frobnic', 'splunge', 'glorp', 'thrimble',
            'vorpal', 'quixe', 'brindle', 'snarf', 'plonk', 'kludge']
INITIALISMS = ['NVDA', 'NASA', 'TTS', 'HTML', 'USB', 'CPU', 'qzxv', 'SAPI']
ROMAJI = ['take', 'sakura', 'konnichiwa', 'kimono', 'sushi', 'karate',
          'tsunami', 'origami', 'date', 'same', 'name', 'kite']
MIXED = [u'Windows konnichiwa', u'私はWindowsをつかいます',
         u'これはsakuraです', u'blorfとzindle',
         u'NVDAでcomputerをつかう']


def kana(morae, inv):
    return u''.join(inv.get(m, u'[' + m + u']') for m in morae)


def route(word, ph, d, inv):
    """-> (reading, which step produced it).

    The same four steps the library uses, in the same order, so the table
    says which one answered rather than only what came out.
    """
    key = D.normalize(word)
    r = d.find(key.encode('utf-8'))
    if r is not None and d.entry(r[0])[8] != 0:
        e = d.entry(r[0])
        return d.pron(e[1]), 'dictionary', e[7]
    #
    # THE RULES BEFORE ROMAJI, which is the change this table forced.
    #
    # Romaji-first read `mouse` as モーセ, `fire` フィレ, `orange` オランゲ and
    # `button` ブットン -- every one of them consumes whole as romaji, so
    # romaji won before the rules were asked.  Complete consumption proves a
    # spelling CAN be read as romaji, never that it was meant as one, and
    # ordinary text wants the English default: all three systems that were
    # listened to read a bare `take` as テイク.
    #
    # Deliberate romaji is then the explicit case rather than the default,
    # and the signal for it today is that the whole utterance is clean romaji
    # with nothing in the dictionary -- which is what the library already
    # tests, and what keeps the romaji path bit-identical to what has been
    # signed off by ear.
    morae, acc, how = G.read(word, ph)
    if how == 'word' and morae:
        return kana(morae, inv), 'rules', acc
    if how == 'letters':
        spelled, _ = D.unknown_pron(key)
        return spelled, 'spelled', 0
    pron, drops = N.romaji_to_kana(word)
    if pron is not None and drops == 0:
        return pron, 'romaji', 0
    spelled, _ = D.unknown_pron(key)
    return spelled, 'spelled', 0


def check_c(words, ph, inv, exe=None, tmp=None):
    """The C adaptation against the Python it was translated from.

    `ja_check g2p` takes the phoneme string as INPUT, so it needs no engine
    and can be the engine-free binary -- which is what makes this checkable
    at all.  It is the comparison that caught the C round-tripping its morae
    back through the romaji parser, where a repeated vowel became a long one
    and `take` came out テーク.

    -> a list of disagreements, empty when there are none, or None when the
    binary is not built.
    """
    import subprocess
    import tempfile

    exe = exe or os.path.join(ROOT, 'build', 'check', 'ja_check.exe')
    if not os.path.isfile(exe):
        return None
    fd, path = tempfile.mkstemp(suffix='.tsv', dir=tmp)
    os.close(fd)
    try:
        with io.open(path, 'w', encoding='utf-8', newline='\n') as fh:
            for w in words:
                if ph.get(w):
                    fh.write(u'%s\t%s\n' % (w, ph[w]))
        r = subprocess.run([exe, 'g2p', path], capture_output=True)
        out = r.stdout.decode('utf-8', 'replace').splitlines()
    finally:
        os.unlink(path)
    bad = []
    got = {}
    for line in out:
        f = line.split(u'\t')
        if len(f) == 4:
            got[f[0]] = (f[1], f[2], int(f[3]))
    for w in words:
        if not ph.get(w):
            continue
        morae, acc, how = G.read(w, ph[w])
        want = (how, u''.join(inv.get(m, u'?') for m in morae) if morae else u'',
                acc)
        if how != 'word':
            want = (how, u'', 0)
        if w not in got:
            bad.append('%s: the C said nothing' % w)
        elif got[w] != want:
            bad.append('%s: C %r, python %r' % (w, got[w], want))
    return bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', default=None)
    args = ap.parse_args(argv)

    problems = G.self_test()
    if problems:
        print('the rule set has %d gaps; fix those first' % len(problems))
        for b in problems[:10]:
            print('   %s' % b)
        return 1
    print('self-check: every mora the rules can emit has a katakana')
    print()

    d = D.load()
    inv = N._inverse()
    words = ([w for w, _ in FAMILIAR] + INVENTED + INITIALISMS + ROMAJI)
    ph = G.phonemes_of(words, args.dll)

    groups = [
        ('FAMILIAR WORDS -- the dictionary should win, and where it has no '
         'entry these rules stand in', FAMILIAR),
        ('INVENTED WORDS -- the target: a reading for what nothing can know',
         [(w, None) for w in INVENTED]),
        ('INITIALISMS -- must stay spelled, in Japanese letter names',
         [(w, None) for w in INITIALISMS]),
        ('AMBIGUOUS ROMAJI -- both readings are defensible and the spelling '
         'does not say which', [(w, None) for w in ROMAJI]),
    ]
    agree = compared = 0
    for title, items in groups:
        print('== %s' % title)
        for w, want in items:
            reading, how, acc = route(w, ph.get(w, ''), d, inv)
            mark = ' '
            if want is not None:
                compared += 1
                if reading == want:
                    agree += 1
                    mark = '='
                else:
                    mark = '~'
            print('  %s %-12s %-7s %-20s acc %-3s %s'
                  % (mark, w, how, reading, acc,
                     ('want ' + want) if want and reading != want else ''))
        print()

    print('== MIXED SENTENCES -- all of it in one utterance')
    import jp_front as F
    for t in MIXED:
        w, morae, accents, dv, q, st = F.analyse(D.normalize(t))
        print('    %-28s %s' % (t, u' '.join(morae)))
        print('    %-28s accents %s' % ('', accents))
    print()
    bad = check_c(words, ph, inv)
    if bad is None:
        print('build/check/ja_check.exe is not built, so the C adaptation')
        print('was not checked against the Python.')
    elif bad:
        print('THE C DISAGREES WITH THE PYTHON on %d of %d words:'
              % (len(bad), len(words)))
        for b in bad[:12]:
            print('   %s' % b)
        return 1
    else:
        print('the C adaptation agrees with the Python on all %d words'
              % len(words))
    print()
    print('%d of %d familiar words match the established loanword exactly'
          % (agree, compared))
    print('An established loanword reflects spelling and borrowing history as')
    print('well as sound, so disagreeing is not automatically a defect -- and')
    print('those words should come from the dictionary.  The invented ones are')
    print('what the rules are for.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
