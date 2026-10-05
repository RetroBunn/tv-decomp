# -*- coding: utf-8 -*-
"""Reference output for the C port of the analyser, text by text.

Run from the repository root:
    python build/Japanese_test/dump_front_oracle.py

WHY THIS COMES FIRST, again.  oracle_frames.tsv fixed the FRAME BUILDER against
the Python, and it did its job: the C reproduces all 784,036 bytes of it and has
done through every change since.  What it cannot see is everything upstream of
a mora list -- the Viterbi over 486,757 dictionary entries, the normalisation,
the unknown-word grouping, five rule stages and the digit expansion.  That is
the half being ported now, and it is far larger than the half the frame oracle
covers.

So this is the same instrument one stage earlier: text in, and the four things
jp_speak.build and jp_speak.pitch are handed out.  The C must reproduce every
field for every line.

    <text>\\t<morae>\\t<accents>\\t<devoiced>\\t<question>

Tab-separated, UTF-8.  Morae space-joined, using the same symbols jp_mora
emits.  Accents comma-joined, one per accent phrase.  Devoiced is one
character per mora, `*` or `.`, so a misalignment is visible rather than
silent.  Question is 0 or 1.

THE CORPUS IS CHOSEN TO BREAK THINGS.  It leads with the cases that have each
cost a bug -- the compound entries, the adjacent devoicing, the normalisation
direction, the moraic nasal with no place to assimilate to -- then the numbers,
then real sentences, then dictionary readings at every length, then the
deliberate edge cases: nothing at all, punctuation alone, an unreadable
character, mixed scripts.
"""
import io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_dict as D, jp_front as F

OUT = os.path.join(HERE, 'front_oracle.tsv')
N_DICT = 1200
SEED = 23

# Each of these is here because something was once wrong with it.
FIXED = [
    # the four bugs the compiled-Open-JTalk comparison found
    u'複数', u'複製', u'複写', u'美しさ',
    u'ありがとうございます', u'せっぱ詰まる',
    u'1250円', u'ＡＢＣ', u'ガッツポーズ', u'ｶﾞｯﾂﾎﾟｰｽﾞ',
    # the moraic nasal with nothing to assimilate to
    u'三四', u'散歩', u'恋愛', u'電話', u'日本語', u'新聞', u'銀行',
    u'ヴァイオリン', u'れんあい',
    # numbers
    u'三百円', u'一本', u'三本', u'六本', u'千本', u'二十五', u'百二十三',
    u'一万二千三百四十五円', u'1.5', u'03-1234-5678', u'10000',
    u'会議は三時半からです。', u'値段は1250円です。',
    # the accent minimal pairs
    u'箸', u'橋', u'端', u'箸が', u'橋が', u'端が',
    u'雨', u'飴', u'牡蠣が', u'柿が', u'垣が',
    # real sentences, with punctuation and a question
    u'私は日本語を話します。', u'今日はいい天気ですね。',
    u'電車が駅に到着しました。', u'その本を読んでください。',
    u'明日の会議は何時からですか？', u'東京都の人口は増えています。',
    u'友達と一緒に図書館で勉強しました。',
    u'日本語学校の先生はとても親切です。',
    u'私は毎日学校へ行きます。友達と勉強します。',
    u'そうですか？', u'これは何ですか', u'お元気ですか？',
    # the phonetic cases the frame oracle leads with, as text this time
    u'こんにちは', u'おばさん', u'おばあさん', u'おじさん', u'おじいさん',
    u'くつ', u'です', u'すこし', u'した',
    u'ふね', u'ふく', u'ふとん', u'ふあん', u'ふうふ',
    u'さくら', u'ともだち', u'だいどころ',
    u'かあど', u'とおる', u'びいる', u'くうき',
    u'がっこう', u'きって', u'ざっし',
    u'しゃしん', u'つつじ', u'ちゃちゃちゃ',
    u'ありがとう', u'さようなら', u'おはよう',
    # and the edges
    u'', u' ', u'。', u'、', u'？', u'。。。', u'龘', u'あ龘い', u'龘あ',
    u'aiueo', u'ABC', u'あabcい', u'クヮルテット', u'ゴォォール',
    u'ちっちゃゅぅ',
]


def corpus():
    out = list(FIXED)
    d = D.load()
    import random
    rnd = random.Random(SEED)
    # dictionary surface forms at every length, so the Viterbi and the
    # unknown-word path both get exercised on real material
    pool = []
    step = max(1, d.n_entries // 60000)
    for i in range(0, d.n_entries, step):
        s = d.surface(d.entry(i)[0])
        if 1 <= len(s) <= 10:
            pool.append(s)
    rnd.shuffle(pool)
    out += pool[:N_DICT]
    # and pairs, which is where the eighteen phrase rules live
    for _ in range(N_DICT // 2):
        out.append(rnd.choice(pool) + rnd.choice(pool))
    return out


def main():
    n = 0
    with io.open(OUT, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(u'# reference output from the Python analyser.  The C port\n'
                 u'# must reproduce every field of every line.\n'
                 u'# <text>\\t<morae>\\t<accents>\\t<devoiced>\\t<question>\n')
        for text in corpus():
            if u'\n' in text or u'\t' in text:
                continue
            try:
                # ask=None: the Latin-word rules are OFF here, because
                # ja_check links no engine and so cannot ask.  See
                # jp_front.analyse.
                _w, morae, acc, dv, q, _st = F.analyse(text, None)
            except Exception as e:
                print('FAILED on %r: %s' % (text[:30], e))
                continue
            fh.write(u'%s\t%s\t%s\t%s\t%d\n'
                     % (text, u' '.join(morae),
                        u','.join(str(a) for a in acc),
                        u''.join(u'*' if x else u'.' for x in dv),
                        1 if q else 0))
            n += 1
    print('%s' % OUT)
    print('  %d lines, %d bytes' % (n, os.path.getsize(OUT)))
    print()
    print('  The C port passes when every field of every line matches.')


if __name__ == '__main__':
    main()
