# -*- coding: utf-8 -*-
"""Samples for what the dictionary bought, laid out as A/B where there is a B.

    python build/Japanese_test/make_dict.py [--sr 11025]

Four things to listen for, and each is a file or a pair:

  01-03  PITCH ACCENT.  箸 /hashi/ 1, 橋 /hashi/ 2, 端 /hashi/ 0 are the same
         three morae and differ only in where the pitch falls.  Before the
         dictionary all three came out identical, because nothing could say
         which was which.  Same for 雨/飴 and 牡蠣/柿/垣.
  10s    KANJI.  The old front end read the kana in a sentence and dropped
         every kanji, so most of these were a few particles.  The B member of
         each pair is what it used to do.
  20s    DEVOICING.  Rule 3 blocks two adjacent devoiced morae and rule 4
         protects the mora carrying the accent nucleus.  一 /ichi/ is accent
         2/2, so its /chi/ must keep its voice; the old rule whispered it.
         A is the dictionary's answer, B the old string-level rule.
  30s    PHRASING AND DOWNSTEP.  Where the accent phrases fall now comes from
         njd_set_accent_phrase's eighteen rules rather than from a space the
         caller typed.

The B members are rendered through the same frames with one input changed, so
a difference you hear is that input and not a re-render.
"""
import argparse, io, os, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_dict as D, jp_njd as N, jp_front as F, jp_speak as S, jp_mora as M

# A: the accent-distinguished homophones.
#
# Each appears bare AND with the subject particle が, because a word accented
# on its FINAL mora and a word with no accent at all are indistinguishable in
# isolation -- both leave the fall after the last mora, which in isolation is
# the end of the utterance.  The contrast lives on the following particle:
# 橋が falls on the が, 端が stays high through it.  That is a fact about
# Japanese and not a limitation here; it is measured in these files, where
# 橋/端 bare are byte-identical and 橋が/端が are not.
ACCENT = [
    (u'箸', u'chopsticks, accent 1 -- the fall is after the first mora'),
    (u'橋', u'bridge, accent 2 -- on the last mora, so bare it cannot show'),
    (u'端', u'edge, accent 0 -- no fall; bare, identical to 橋 above'),
    (u'箸が', u'chopsticks + ga: falls after mora 1'),
    (u'橋が', u'bridge + ga: the fall lands ON the particle'),
    (u'端が', u'edge + ga: no fall, high through the particle'),
    (u'雨', u'rain, accent 1'),
    (u'飴', u'sweet, accent 0'),
    (u'牡蠣が', u'oyster + ga, accent 1'),
    (u'柿が', u'persimmon + ga, accent 0'),
    (u'垣が', u'fence + ga, accent 2'),
]

# B: sentences the old path could not read.  Each is (text, what the old path
# would have been given, which is the kana with the kanji removed).
KANJI = [
    u'私は日本語を話します。',
    u'今日はいい天気ですね。',
    u'電車が駅に到着しました。',
    u'その本を読んでください。',
    u'明日の会議は何時からですか？',
    u'東京都の人口は増えています。',
]

# C: devoicing, where the two rules disagree
DEVOICE = [
    (u'一', u'ichi: /chi/ is the accent nucleus, so it keeps its voice'),
    (u'十月', u'juugatsu: /tsu/ is the nucleus'),
    (u'ひく', u'hiku: the old rule devoiced both morae; only one may'),
    (u'いつつ', u'itsutsu: adjacent candidates, and the nucleus'),
    (u'明日の会議', u'ashita: /shi/ devoices, /ta/ does not'),
]

# D: phrasing
PHRASE = [
    u'私は毎日学校へ行きます。',
    u'友達と一緒に図書館で勉強しました。',
    u'日本語学校の先生はとても親切です。',
]

# E: the four bugs the comparison against a compiled Open JTalk turned up.
# Each is old-then-new, where "old" is this front end a day ago.
REPAIRS = [
    ('fukusuu', u'複数', u'adjacent devoicing: was フ-ク-スー with both devoiced, now '
              u'only the first, which is what upstream gives'),
    ('utsukushisa', u'美しさ', u'the same rule in a longer word'),
    ('arigatou', u'ありがとうございます', u'a compound entry: its two components and their '
                      u'accents 2 and 4 were being thrown away, so it '
                      u'spelled out its kana flat'),
    ('seppa', u'せっぱ詰まる', u'the same bug, and this one produced no morae at all'),
    ('1250yen', u'1250円', u'the normaliser ran the wrong way, so every digit missed the '
              u'dictionary and became a pause'),
    ('abc', u'ＡＢＣ', u'and so did Latin letters'),
    ('gattsupoozu', u'ガッツポーズ', u'halfwidth kana now compose before the lookup'),
]


# F: njd_set_digit.  Place value, the counter assimilations, the decimal
# point, and the identifier path that reads a phone number digit by digit
# with 0, 2 and 5 lengthened the way they are said over a line.
NUMBERS = [
    ('1250yen', u'1250円', u'sen nihyaku gojuu en -- place value'),
    ('nedan', u'値段は1250円です。',
     u'the same number in a sentence'),
    ('ippon', u'一本', u'ippon, not ichi-hon: the counter semi-voices'),
    ('sanbon', u'三本', u'sanbon: and here it voices instead'),
    ('roppon', u'六本', u'roppon'),
    ('sanbyaku', u'三百円', u'sanbyaku en: hyaku voices after san'),
    ('ichiman', u'一万二千三百四十五円',
     u'12,345 yen -- four place-value groups'),
    ('decimal', u'1.5', u'itten go -- the decimal point'),
    ('phone', u'03-1234-5678',
     u'an identifier, not a quantity: read digit by digit, with 0 2 5 '
     u'lengthened to zero nii goo'),
    ('sanji', u'会議は三時半からです。',
     u'a time'),
]

# G: the moraic nasal where there is nothing to assimilate to.  It used to
# fall back to the preceding vowel's own posture, so /sa N yo/ held F1 736 --
# /a/'s own first formant -- and was heard as "saa".  Every one of these holds
# a murmur now.  The second member of each pair is the context that always
# worked, for comparison.
NASAL = [
    # 三四 was here and was mislabelled: the digit stage expands it to
    # 三十四, サンジューヨン, so its /N/ sits before /j/ and takes the
    # alveolar place that always worked.  本屋 is /N/ before /y/ with
    # nothing in between, which is the context that was broken.
    ('honya', u'本屋', u'honya: /N/ before /y/, the broken context'),
    ('tenin', u'店員', u'tenin: /N/ before a vowel, then a final /N/'),
    ('sanpo', u'散歩', u'sanpo: /N/ before /p/, which always worked'),
    ('renai', u'恋愛', u"ren'ai: /N/ before a vowel"),
    ('denwa', u'電話', u'denwa: /N/ before /w/'),
    ('nihon', u'日本語', u'nihongo: /N/ before /g/, velar'),
    ('phone', u'03-1234-5678', u'the phone number the problem was heard in'),
]


# H: what the digit stage gained when it was re-ported against 1.11 and the
# accent-type stage's own digit branch was finished.  The first group is the
# native numerals, which need the dictionary's `read` field and so needed the
# dictionary rebuilt; the second is where the fall lands in a number, which no
# rule was setting before.
COUNTERS = [
    ('hitotsubu', u'一粒', u'hitotsubu, not ichiryuu: 粒 takes the native one'),
    ('futakuchi', u'二口', u'futakuchi: and the native two'),
    ('hitotaba', u'一束', u'hitotaba'),
    ('hitotoori', u'一通り',
     u'hitotoori -- 通り is read トオリ and pronounced トーリ, so this one '
     u'only works if the dictionary carries both'),
    ('santsubu', u'三粒', u'santsubu: 1.11 REMOVED the native three from the '
     u'same table, so this is NOT mitsubu -- and the row is still in the '
     u'header, commented out, which is how it got taken by mistake'),
    ('hitori', u'一人', u'hitori'),
    ('futsuka', u'二日', u'futsuka'),
    ('tsuitachi', u'五月一日', u'gogatsu tsuitachi: 一日 after a month'),
    ('hatsuka', u'二十日', u'hatsuka, not nijuu-nichi'),
    ('juuyokka', u'十四日', u'juuyokka'),
    ('gojuuichi', u'五十一',
     u'gojuuichi -- flat: the ten after a five, six or eight loses its fall '
     u'when another digit follows'),
    ('gojuu', u'五十', u'gojuu -- and keeps it when nothing does'),
    ('nanahyaku', u'七百', u'nanahyaku: seven before a hundred falls on the '
     u'second mora'),
    ('sanzen', u'三千', u'sanzen'),
    ('ichioku', u'一億', u'ichioku'),
    ('rokuchou', u'六兆', u'rokuchou'),
    ('juuni', u'十二', u'juuni -- a ten heading its own phrase is flat'),
]


# I: the Latin-word rules, which are the new thing and the thing to judge.
#
# An unfamiliar Latin word should get a plausible Japanese reading, which is
# what every Japanese system does and what spelling it out letter by letter is
# not.  These go through jp_g2p: the English front end's own pronunciation,
# adapted to Japanese morae.  The invented words are the target -- nothing can
# look those up -- and the familiar ones are there to say whether the rules
# are sane, not because they would be used: a word the dictionary has comes
# from the dictionary.
LATIN = [
    ('invented', [u'blorf', u'zindle', u'frobnic', u'splunge', u'glorp',
                  u'thrimble', u'vorpal', u'brindle', u'snarf', u'kludge'],
     u'invented words, which is what the rules are for'),
    ('familiar', [u'test', u'desk', u'cat', u'cup', u'book', u'bed',
                  u'bottle', u'little', u'stop', u'rock', u'box', u'bus',
                  u'much', u'music', u'cute', u'fire', u'google'],
     u'familiar words the rules get right, for calibration'),
    ('spelling', [u'computer', u'america', u'internet', u'pilot',
                  u'lemon', u'method', u'banana', u'camera', u'about'],
     u'the reduced-vowel rule: the Japanese vowel comes from the '
     u'SPELLING, which is what made computer コンピューター instead of '
     u'キンピューター'),
    ('misses', [u'button', u'hello', u'orange', u'water',
                u'window', u'phone', u'table'],
     u'the ones that disagree with the established loanword -- spelling '
     u'history, or a gap in the kana the reader has'),
]


def write(path, pcm, sr):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def render_morae(morae, accent):
    """A mora list straight to audio, for a reading that came from a rule
    rather than from text.  jp_speak takes exactly this."""
    try:
        fr, ends, q = S.build(list(morae))
        S.pitch(fr, ends, [accent], q_ends=q, morae=list(morae))
        return S.render(fr)
    except Exception:
        return None


def render_new(text):
    """The dictionary path."""
    got = F.frames(text)
    if got is None:
        return None, None
    w, morae, accents, dv, fr, hz = got
    return S.render(fr), (morae, accents, dv, hz)


def render_flat(text):
    """The same morae, but heiban everywhere and the old devoicing rule --
    which is exactly what the front end did before the dictionary."""
    w, morae, accents, dv, q, _st = F.analyse(text)
    if not morae:
        return None, None
    fr, ends, qq = S.build(list(morae))          # no devoiced_in
    hz = S.pitch(fr, ends, 0, q_ends=qq, morae=list(morae), question=q)
    return S.render(fr), (morae, [0], None, hz)


def render_old_kana(text):
    """What the old path actually received: the text with everything it could
    not read removed, which for most Japanese is the particles."""
    keep = u''.join(c for c in text
                    if u'ぁ' <= c <= u'ゖ' or u'ァ' <= c <= u'ー'
                    or c in u' ')
    if not keep.strip():
        return None, keep
    morae = M.to_morae(keep)
    if not morae:
        return None, keep
    fr, ends, q = S.build(list(morae))
    S.pitch(fr, ends, 0, q_ends=q, morae=list(morae))
    return S.render(fr), keep


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sr', type=int, default=11025)
    ap.add_argument('--out', default=None)
    args = ap.parse_args(argv)

    S.SR = args.sr
    out = args.out or os.path.join(HERE, 'dictionary %d Hz' % args.sr)
    os.makedirs(out, exist_ok=True)
    notes = []

    def say(tag, pcm, note):
        p = os.path.join(out, tag + '.wav')
        write(p, pcm, args.sr)
        notes.append(u'%-34s %s' % (tag + '.wav', note))
        print('  %-32s %s' % (tag, note))

    print('A. pitch accent -- same morae, different fall')
    allp = []
    for i, (word, gloss) in enumerate(ACCENT):
        pcm, info = render_new(word)
        if pcm is None:
            continue
        # the reading and the accent go in the NAME: these files are listened
        # to from a directory listing, and "01-accent-1" says nothing about
        # which of three identical-sounding words it is
        romaji = ''.join(m for m in info[0] if m not in (' ', '|', '||'))
        say('a%02d-%s-acc%d' % (i + 1, romaji, info[1][0]), pcm,
            u'%s  %s' % (word, gloss))
        allp.append(pcm)
    if allp:
        import numpy as np
        sil = np.zeros(int(args.sr * 0.45), dtype=allp[0].dtype)
        say('a99-all', np.concatenate(
            [x for p in allp for x in (p, sil)]),
            u'all of them in order, for comparison')

    print('B. kanji -- what the old path was given, and what it is given now')
    import numpy as np
    sil = np.zeros(int(args.sr * 0.5), dtype=np.int16)
    for i, text in enumerate(KANJI):
        new, info = render_new(text)
        old, kept = render_old_kana(text)
        if new is None:
            continue
        say('b%02d-kanji-new' % i, new, u'%s' % text)
        if old is not None:
            say('b%02d-kanji-old' % i, old,
                u'the same sentence as the old path read it: "%s"' % kept)
            say('b%02d-kanji-pair' % i, np.concatenate([old, sil, new]),
                u'old then new')
        else:
            notes.append(u'%-34s %s' % ('b%02d-kanji-old' % i,
                u'the old path had nothing sayable left: "%s"' % kept))
            print('  %-32s nothing sayable in the old path' % ('b%02d-kanji-old' % i))

    print('C. devoicing -- the dictionary against the string-level rule')
    for i, (text, gloss) in enumerate(DEVOICE):
        new, info = render_new(text)
        old, _ = render_flat(text)
        if new is None or old is None:
            continue
        say('c%02d-devoice-new' % i, new, u'%s  %s' % (text, gloss))
        say('c%02d-devoice-old' % i, old, u'%s  the old rule' % text)
        say('c%02d-devoice-pair' % i, np.concatenate([old, sil, new]),
            u'%s  old then new' % text)

    print('D. phrasing and downstep')
    for i, text in enumerate(PHRASE):
        new, info = render_new(text)
        flat, _ = render_flat(text)
        if new is None:
            continue
        say('d%02d-phrase-new' % i, new,
            u'%s   accents %s' % (text, info[1]))
        if flat is not None:
            say('d%02d-phrase-flat' % i, flat,
                u'%s   the same phrasing, every accent heiban' % text)

    print('E. the four bugs the compiled-Open-JTalk comparison turned up')
    for i, (slug, text, gloss) in enumerate(REPAIRS):
        new, info = render_new(text)
        tag = 'e%02d-%s' % (i, slug)
        if new is None:
            notes.append(u'%-34s %s' % (tag, u'%s  produced nothing' % text))
            print('  %-32s produced nothing' % tag)
            continue
        say(tag, new, u'%s   %s' % (text, gloss))

    print('F. numbers, which used to be dropped and then were read digit by digit')
    for i, (slug, text, gloss) in enumerate(NUMBERS):
        new, info = render_new(text)
        tag = 'f%02d-%s' % (i, slug)
        if new is None:
            notes.append(u'%-34s %s' % (tag, u'%s  produced nothing' % text))
            print('  %-32s produced nothing' % tag)
            continue
        say(tag, new, u'%s   %s' % (text, gloss))

    print('G. the moraic nasal with no place to assimilate to')
    for i, (slug, text, gloss) in enumerate(NASAL):
        new, info = render_new(text)
        tag = 'g%02d-%s' % (i, slug)
        if new is None:
            notes.append(u'%-34s %s' % (tag, u'%s  produced nothing' % text))
            continue
        say(tag, new, u'%s   %s' % (text, gloss))

    print('H. native numerals, and where the fall lands in a number')
    for i, (slug, text, gloss) in enumerate(COUNTERS):
        new, info = render_new(text)
        tag = 'h%02d-%s' % (i, slug)
        if new is None:
            notes.append(u'%-34s %s' % (tag, u'%s  produced nothing' % text))
            print('  %-32s produced nothing' % tag)
            continue
        say(tag, new, u'%s   %s   accents %s' % (text, gloss, info[1]))

    print('I. the Latin-word rules -- an unfamiliar word, pronounced')
    import jp_g2p as GP
    import jp_njd as NJ
    allw = [w for _, ws, _ in LATIN for w in ws]
    try:
        phon = GP.phonemes_of(allw)
    except Exception as e:
        phon = None
        print('  the English front end is not available: %s' % e)
    if phon is not None:
        inv = NJ._inverse()
        for gi, (slug, words, gloss) in enumerate(LATIN):
            parts = []
            for w in words:
                morae, acc, how = GP.read(w, phon[w])
                if how != 'word' or not morae:
                    continue
                pcm = render_morae(morae, acc)
                if pcm is None:
                    continue
                kana = u''.join(inv.get(m, u'?') for m in morae)
                tag = 'i%02d-%s-%s' % (gi, slug, w)
                say(tag, pcm, u'%s -> %s   accent %d' % (w, kana, acc))
                parts += list(pcm) + list(sil)
            if parts:
                import numpy as np
                say('i%02d-%s-all' % (gi, slug),
                    np.array(parts, dtype=np.int16), gloss)

    with io.open(os.path.join(out, 'notes.txt'), 'w',
                 encoding='utf-8', newline='\n') as fh:
        fh.write(u'Samples for the dictionary front end, %d Hz.\n\n' % args.sr)
        fh.write(u'A pair file is old-then-new with half a second between.\n\n')
        for line in notes:
            fh.write(line + u'\n')
    print()
    print('%s' % out)
    print('  %d files, and notes.txt describing each' % len(notes))
    return 0


if __name__ == '__main__':
    sys.exit(main())
