# -*- coding: utf-8 -*-
"""Check the C Japanese front end against the Python it was translated from.

    python tools/check_ja_front.py [--n 20000] [--exe build/check/ja_check.exe]

Two stages, because they fail for different reasons and a single pass/fail
would not say which:

  morae   build/check/ja_check.exe morae -- every word's mora list, compared
          token for token against jp_mora.to_morae.  Run over the dictionary
          in kana AND in romaji, plus the boundary marks and the cases that
          have each cost a bug: the moraic nasal before a vowel, katakana VU,
          the small kana, halfwidth katakana, long vowels written four ways.

  oracle  the frames, compared against oracle_frames.tsv.  That is the real
          test and it lives in the C binary; this script only runs it, so the
          two halves are reported together.

A mora list is a cheap thing to compare and a precise one to fail on, which is
why it is a stage of its own: an error here would otherwise surface as a frame
mismatch 2,000 lines further down, in a word whose spelling gives no hint that
its parse was wrong.
"""
import argparse, csv, io, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'build', 'Japanese_test'))
import jp_mora as M

DICT = os.path.join(ROOT, 'open_jtalk-1.11', 'mecab-naist-jdic',
                    'naist-jdic.csv')
ORACLE = os.path.join(ROOT, 'build', 'Japanese_test', 'oracle_frames.tsv')

# Each of these is here because something was once wrong with it.  The comment
# is what it catches, so a future reader can tell a regression from a typo.
FIXED = [
    u"konnichiwa",
    u"ren'ai",          # the moraic nasal before a vowel: Hepburn's apostrophe
    u"れんあい",          # the same word in kana, which is where it broke
    u"ヴァイオリン",       # katakana VU: the character used to be dropped
    u"ゔぁいおりん",
    u"ファイル", u"フィルム", u"ティー", u"ディスク", u"ツァー",   # the small vowels
    u"キャベツ", u"シャツ", u"チャンス", u"ジャム", u"ニュース",    # the small ya/yu/yo
    u"ｶﾞｯｺｳ", u"ｱｲｳｴｵ", u"ﾊﾟﾝ",                            # halfwidth katakana
    u"gakkou", u"がっこう", u"ガッコウ",
    u"tooru", u"touru", u"tōru", u"とおる", u"トール",
    u"obasan", u"obaasan", u"ojisan", u"ojiisan",
    u"kutsu", u"desu", u"sukoshi", u"shita",
    u"fune", u"fuku", u"futon", u"fuan", u"fuufu",
    u"sanpo", u"ginkou", u"shinbun", u"shashin",
    u"a|b", u"a||b", u"watashi wa", u"kore ga aru",
    u"",                # nothing at all
    u"123", u"!!!",     # the filter drops these; both sides must agree it does
    u"n", u"nn", u"q", u"-", u"'",
]


def corpus(n, seed=17):
    words = list(FIXED)
    if not os.path.exists(DICT):
        print('note: %s not found; the dictionary half is skipped' % DICT)
        return words
    rows = []
    with io.open(DICT, encoding='utf-8', errors='ignore') as fh:
        for r in csv.reader(fh):
            if len(r) > 12:
                p = r[12]
                if p and 2 <= len(p) <= 16 and all(
                        u'ァ' <= c <= u'ー' for c in p):
                    rows.append(p)
    random.Random(seed).shuffle(rows)
    rows = rows[:n]
    # Katakana as it comes, the hiragana the oracle uses, and the romaji the
    # Python's own reader produces -- three spellings of one word, which is
    # three different paths through the parser.
    def k2h(x):
        return u''.join(chr(ord(c) - 0x60) if u'ァ' <= c <= u'ヶ'
                        else c for c in x)
    for p in rows:
        words.append(p)
        words.append(k2h(p))
        words.append(M.kana_to_romaji(p))
    return words


def tokens(morae):
    return [m if m != ' ' else ' ' for m in morae]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--n', type=int, default=20000,
                    help='dictionary words, each in three spellings')
    ap.add_argument('--exe', default=os.path.join(ROOT, 'build', 'check',
                                                  'ja_check.exe'))
    ap.add_argument('--oracle', default=ORACLE)
    ap.add_argument('--skip-oracle', action='store_true')
    args = ap.parse_args(argv)

    if not os.path.exists(args.exe):
        print('no %s -- run harness/build.sh first' % args.exe)
        return 2

    words = corpus(args.n)
    # A word containing a newline would desynchronise the two sides silently,
    # so it is excluded rather than trusted; none exist, and the count says so.
    words = [w for w in words if '\n' not in w and '\r' not in w]
    tmp = os.path.join(ROOT, 'build', 'obj', 'ja_morae_in.txt')
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    with io.open(tmp, 'w', encoding='utf-8', newline='\n') as fh:
        for w in words:
            fh.write(w + u'\n')

    out = subprocess.run([args.exe, 'morae', tmp], capture_output=True)
    if out.returncode != 0:
        print('ja_check morae failed: %s' % out.stderr.decode('utf-8', 'replace'))
        return 1
    # stdout is a Windows text stream, so every line arrives with a CR on it.
    lines = [l.rstrip('\r') for l in out.stdout.decode('utf-8').split('\n')]
    if lines and lines[-1] == '':
        lines.pop()
    if len(lines) != len(words):
        print('%d words in, %d lines out' % (len(words), len(lines)))
        return 1

    bad = 0
    for w, got in zip(words, lines):
        want = tokens(M.to_morae(w))
        mine = got.split('\t') if got else []
        if mine == [''] and not want:
            mine = []
        if mine != want:
            bad += 1
            if bad <= 20:
                print('%-24s python %-40s c %s'
                      % (w, '/'.join(want), '/'.join(mine)))
    print('morae: %d words in three spellings, %d disagree' % (len(words), bad))
    rc = 1 if bad else 0

    if not args.skip_oracle:
        if not os.path.exists(args.oracle):
            print('no %s -- run build/Japanese_test/dump_oracle.py' % args.oracle)
            return rc or 2
        print()
        r = subprocess.run([args.exe, 'oracle', args.oracle])
        rc = rc or r.returncode
    return rc


if __name__ == '__main__':
    sys.exit(main())
