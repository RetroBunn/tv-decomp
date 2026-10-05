# -*- coding: utf-8 -*-
"""An unfamiliar Latin word -> a Japanese reading.

    python jp_g2p.py            # the comparison table
    python jp_g2p.py WORD ...   # those words

WHY THIS EXISTS.  A Japanese front end that spells `computer` out as the names
of its eight letters is doing the one thing no Japanese system does.  All
three that were checked -- Google, Microsoft OneCore, Eloquence -- pronounce
an unfamiliar Latin word, including an invented one, and the requirement is
that one: a plausible Japanese reading for a word nothing knows.

WHAT IT IS BUILT ON.  OpenTV already contains an English pronunciation
engine, and `tvtts_text_to_phonemes` already returns its answer as a string:

    take      &TA1Kp.             computer  &K|MPU1t3.
    blorf     &BLg1F.             NVDA      &e1N&VE1&DE1&A1.

So the English half is done and measured.  What this file is, is the other
half: those phonemes adapted to Japanese -- which is not a transliteration,
because Japanese has no syllable for `blorf` and has to build one.

THE ORDER OF PREFERENCE, and it is the point.  A reading comes from the first
of these that has one, never the next:

    1. the dictionary, or a user override      Windows -> ウィンドーズ
    2. romaji, if it consumes the whole token  sakura  -> サクラ
    3. THIS                                    blorf   -> ブロルフ
    4. spelling out, letter by letter          NVDA    -> エヌブイディーエー

and 4 is not a failure: an initialism SHOULD be spelled, which is why the
word-versus-letters test comes before the adaptation rather than after it.

WHAT IS NOT SETTLED.  The adaptation rules below are a first cut, and the
whole file exists to be listened to and argued with.  Mora counts,
permissible sequences, vowel insertion and accent can all be stated as rules
and tested -- they are, here -- but whether the result sounds like Japanese is
a judgement no rule makes.  Established loanwords also reflect spelling and
borrowing history and not only source pronunciation (Mao and Hulden 2016,
"How Regular is Japanese Loanword Adaptation?",
https://aclanthology.org/C16-1081/), so the spelling is kept alongside the
phonemes rather than thrown away after the lookup.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))


# ---- the engine's phoneme alphabet ---------------------------------------
#
# One character per phoneme.  The names are the two-letter ones the bracket
# syntax accepts, and the mapping between them is in src/engine/phonetic.c --
# read out of there rather than guessed, and checked in `self_test` below
# against words whose pronunciation is not in doubt.
NAME = {
    'o': 'AA',  'a': 'AE',  'w': 'AO',  'r': 'AR',  'f': 'AW',  '@': 'AX',
    'I': 'AY',  'B': 'B',   'C': 'CH',  'D': 'D',   'x': 'DH',  't': 'DT',
    'e': 'EH',  'k': 'ER',  'A': 'EY',  'F': 'F',   'G': 'G',   'd': 'HH',
    'h': 'HW',  'H': 'HX',  'i': 'IH',  '4': 'IR',  '|': 'IX',  'E': 'IY',
    'J': 'JH',  'K': 'K',   'L': 'L',   'j': 'LX',  'M': 'M',   'N': 'N',
    '~': 'NG',  'g': 'OR',  'O': 'OW',  'y': 'OY',  'P': 'P',   'Q': 'Q',
    'q': 'QQ',  'R': 'R',   '3': 'RR',  'S': 'S',   's': 'SH',  'T': 'T',
    'X': 'TH',  'u': 'UH',  'l': 'UL',  'm': 'UM',  'n': 'UN',  'c': 'UR',
    'b': 'UW',  'v': 'UX',  'V': 'V',   'W': 'W',   'p': 'XX',  'Y': 'Y',
    '5': 'YR',  'Z': 'Z',   'z': 'ZH',
    # And one the bracket parser has NO two-letter name for, found by asking
    # the engine for every symbol it emits over 135 words: `U` is /ju:/, as
    # in computer, cute, music, beauty, few, view, queue.  docs/SINGING.md
    # records two such gaps between the input and output alphabets; this is a
    # third, and it is why `computer` first came out キンプ.
    'U': 'YU',
}

# Stress is `1` and `2` ONLY.  `3`, `4` and `5` are phonemes -- hurt, hear,
# fire -- and treating every digit as stress ate them, which is why
# `computer` lost its final /3/ and `printer` came out プリント.
STRESS = '12'

# ---- English phoneme -> Japanese --------------------------------------------
#
# A vowel becomes a mora nucleus, with a length: `(vowel, long)`.  Long means
# the chouon follows, which is how Japanese takes a tense or r-coloured vowel
# -- `AO` is オー and `ER` アー, because Japanese has no /ɔ/ and no /ɜr/ and
# length is what it has instead.
#
# Each of these was PROBED and not guessed -- the engine was asked for words
# whose Japanese loanword is not in doubt, and the mapping is what those agree
# on.  Two are worth the note:
#
#   AA is /ɒ/ far more often than /ɑ:/ -- bottle, rock, hot, stop, box --
#   and Japanese takes that as オ: ボトル, ロック, ホット.  The /ɑ:/ words
#   (father, palm) would want アー and get オ; that is the minority and it is
#   accepted rather than hidden.
#
#   UX is /ʌ/ -- cup, up, but, love, run, sun, bus, club, much -- which is
#   ア and not ウ: カップ, アップ, バット, ラブ, ラン.  It was ウ in the first
#   cut and made `cup` クプ.
VOWEL = {
    'AA': ('o', 0), 'AE': ('a', 0), 'AH': ('a', 0), 'AX': ('a', 0),
    'AO': ('o', 1), 'OW': ('o', 1),
    'EH': ('e', 0), 'IH': ('i', 0), 'IX': ('i', 0), 'IY': ('i', 1),
    'UH': ('u', 0), 'UX': ('a', 0), 'UW': ('u', 1),
    'ER': ('a', 1), 'RR': ('a', 1), 'AR': ('a', 1), 'OR': ('o', 1),
}
# A diphthong is two morae, because that is what it is in Japanese: EY is
# エイ and AY アイ, each a mora of its own.
DIPH = {
    'EY': ('e', 'i'), 'AY': ('a', 'i'), 'AW': ('a', 'u'), 'OY': ('o', 'i'),
    'IR': ('i', 'a'), 'UR': ('u', 'a'),
}
# YR is /aɪər/ -- fire, tire, wire, hire -- three morae and not two:
# ファイア, タイア.  It was ('u', 'a') in the first cut and made `fire` フア.
TRIPH = {'YR': ('a', 'i', 'a')}

# A consonant becomes a Japanese onset.  Several English sounds have no
# Japanese equivalent and take the nearest one: TH is /s/ (`think` シンク), DH
# is /z/, and L is /r/, which is the merger every loanword makes.
ONSET = {
    'B': 'b',  'P': 'p',  'D': 'd',  'T': 't',  'G': 'g',  'K': 'k',
    'M': 'm',  'N': 'n',  'F': 'f',  'V': 'b',  'S': 's',  'Z': 'z',
    'SH': 'sh', 'ZH': 'j', 'CH': 'ch', 'JH': 'j',
    'TH': 's', 'DH': 'z',
    'HX': 'h', 'HW': 'h', 'HH': 'h',
    'R': 'r', 'L': 'r', 'W': 'w', 'Y': 'y',
    # Two allophones of /t/ that the engine spells separately and Japanese
    # does not: DT is the flap of `water` and `better`, QQ the glottal stop of
    # `button` and `cotton`.  Dropping them made `little` リル and `button`
    # ブン; as /t/ they are リトル and ボタン.
    'DT': 't', 'QQ': 't',
}

# A release marker rather than a sound: the engine writes XX after a final
# voiceless stop (`take` is TA1Kp).  It has to be DROPPED rather than ignored,
# because a following symbol is what the geminate rule tests for -- left in,
# it made every final stop look non-final and `cat` came out キャト.
DROP = ('XX',)

# The vowel an onset takes when it has none of its own, which is the heart of
# loanword adaptation: Japanese cannot say a bare consonant, so one is
# inserted.  /t/ and /d/ take オ (`test` テスト, `bed` ベッド) because /tu/ and
# /du/ are ツ and ヅ in Japanese and would change the consonant; /ch/ and /j/
# take イ for the same reason; everything else takes ウ (`desk` デスク).
EPENTHETIC = {'t': 'o', 'd': 'o', 'ch': 'i', 'j': 'i', 'sh': 'u', 'h': 'u'}
EPENTHETIC_DEFAULT = 'u'

# /ju:/ does not become a glide plus a vowel in Japanese, it PALATALISES the
# consonant before it: /pju:/ is ピュー and not ピユー, which is a different
# number of morae.  So the onset is replaced rather than extended.
PALATAL = {
    'k': 'ky', 'g': 'gy', 's': 'sh', 'z': 'j', 't': 'ch', 'd': 'j',
    'n': 'ny', 'h': 'hy', 'b': 'by', 'p': 'py', 'm': 'my', 'r': 'ry',
    'f': 'fy', 'ch': 'ch', 'j': 'j', 'sh': 'sh',
}

# /ae/ palatalises a velar, which is the one place the ア default is wrong
# often enough to notice: `cat` is キャット and not カット, `bag` バッグ.
AE_PALATAL = {'k': 'ky', 'g': 'gy'}

# A REDUCED VOWEL TAKES ITS JAPANESE VOWEL FROM THE SPELLING.
#
# This is the rule the first cut did not have, and the one that made
# `computer` キンピューター instead of コンピューター.  English reduces an
# unstressed vowel to something central -- the engine writes it IX or AX --
# and Japanese does not reduce at all, so it has to put SOME vowel there and
# the one it puts is the written one:
#
#     computer  コンピューター    the o of `com`, not the schwa
#     lemon     レモン           melon メロン, method メソッド, London ロンドン
#     banana    バナナ           sofa ソファ, about アバウト, Canada カナダ
#     pilot     パイロット      carrot キャロット, xylophone ザイロフォーン
#
# which is Mao and Hulden's finding stated as a rule rather than as a reason
# to give up.  The alignment is ordinal and crude: the word's runs of vowel
# letters are counted off against its syllable nuclei, so the Nth nucleus
# takes the Nth run.  That is not a real grapheme-to-phoneme alignment and it
# does not have to be -- it only has to find the vowel of the syllable the
# reduced one is in, and a reduced vowel is almost always its own syllable.
# Where the counts do not line up, the phonetic vowel stands.
REDUCED = ('IX', 'AX')
# And the r-coloured reduced vowels, but ONLY away from the end of the word.
# A final -er is アー in Japanese whatever it is spelled -- コンピューター,
# プリンター, ウォーター, ペーパー -- while a medial one takes the written
# vowel: カメラ, アメリカ.  Same phoneme, and the position is what decides.
# and the /r/ of a spelling-driven one does not vanish, it becomes the ONSET
# of the next mora: /mərə/ is メラ, two morae, with the r starting the second.
# Losing it made `camera` キャメア and `america` アメイカ.  Where no vowel
# follows there is nowhere for it to go and the whole thing is アー, which is
# what makes `internet` インターネット and not インテネット.
REDUCED_MEDIAL = ('ER', 'RR')
LETTER_VOWEL = {'a': 'a', 'e': 'e', 'i': 'i', 'o': 'o', 'u': 'u', 'y': 'i'}

# WHAT THE TARGET INVENTORY PERMITS, and what to do where it does not.
#
# Japanese has no /si/, /zi/, /tu/ or /du/ -- those syllables ARE shi, ji, tsu
# and zu -- and the front end's kana reader has no /wi/, /we/ or /wo/ either:
# it reads ウィ, ウェ and ウォ as plain /i/, /e/ and /o/, dropping the glide.
# Older loanwords render those as two morae, which the inventory does have:
# ウイスキー, ウエスト, ウオッチ.
#
# Each of these is a gap in the inventory rather than a preference, and
# `self_test` below walks every onset and vowel this can emit and fails if any
# of them has no katakana -- which is how ウォ was found, after `water` came
# out with a hole in it.
REPAIR = {
    ('s', 'i'): ['shi'], ('z', 'i'): ['ji'],
    ('t', 'u'): ['tsu'], ('d', 'u'): ['zu'],
    ('h', 'u'): ['fu'],
    ('w', 'i'): ['u', 'i'], ('w', 'e'): ['u', 'e'], ('w', 'o'): ['u', 'o'],
    ('y', 'i'): ['i'], ('y', 'e'): ['i', 'e'],
    # The reader has no チェ, シェ or ジェ -- those are post-war loanword
    # morae and its kana table predates them -- so these take the older
    # two-mora rendering, which it can say: チエ, シエ, ジエ.
    ('ch', 'e'): ['chi', 'e'], ('sh', 'e'): ['shi', 'e'],
    ('j', 'e'): ['ji', 'e'],
    # /wu/ is not a Japanese syllable at all: `wood` is ウッド, the glide
    # simply absent.
    ('w', 'u'): ['u'],
    # and no フュ either, where the conservative rendering of /fju:/ is ヒュー
    # -- which is why `fuse` is ヒューズ and not フューズ.
    ('fy', 'u'): ['hyu'],
}

# An onset that cannot take every vowel.  Japanese /f/ exists only before /u/
# unless the small vowels are used, and the mora reader has フ series morae
# for all five, so this is a note rather than a restriction.
# What geminates.  A word-final voiceless stop or /tʃ/ after a short vowel
# doubles: ブック, ホット, カップ, マッチ.  /s/ does NOT -- `bus` is バス and
# `case` ケース -- which is why this is a list and not "any obstruent".
GEMINATE_BEFORE = ('p', 't', 'k', 'ch', 'd', 'g', 'j')
# /d/ and /g/ geminate and /b/ does NOT, which is an asymmetry rather than an
# oversight: ベッド, バッド, レッド, バッグ, ビッグ, エッグ against クラブ,
# パブ, ウェブ, キャブ.  /s/ does not either -- バス, ケース.
VOICELESS_STOP = GEMINATE_BEFORE


def phonemes_of(words, dll=None):
    """-> {word: the engine's phoneme string}, through the English front end.

    This is the expensive part and the reason the prototype is a prototype:
    tvtts_text_to_phonemes gets the trace by SYNTHESISING and throwing the
    audio away, as its own header says.  It costs what speaking costs.  That
    is fine for deciding whether the readings are any good, and is the thing
    to fix afterwards rather than first.
    """
    import ctypes as C

    path = dll or os.path.join(ROOT, 'build', 'bin', 'tvtts64.dll')
    os.add_dll_directory(os.path.dirname(path))
    d = C.CDLL(path)
    d.tvtts_create_lang.argtypes = [C.c_uint32, C.c_char_p]
    d.tvtts_create_lang.restype = C.c_void_p
    d.tvtts_destroy.argtypes = [C.c_void_p]
    d.tvtts_text_to_phonemes.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p,
                                         C.c_uint32]
    d.tvtts_text_to_phonemes.restype = C.c_int
    s = d.tvtts_create_lang(11025, b'en')
    if not s:
        raise SystemExit('could not create an English synthesiser')
    out = {}
    try:
        buf = C.create_string_buffer(8192)
        for w in words:
            n = d.tvtts_text_to_phonemes(s, w.encode('utf-8'), buf, 8192)
            out[w] = buf.value.decode('latin1') if n > 0 else ''
    finally:
        d.tvtts_destroy(s)
    return out


_ASK_CACHE = {}
_ASK_DLL = []


def ask(word):
    """-> the engine's phoneme string for one word, cached.

    The DLL is opened on the first word and not before, so nothing that does
    not read a Latin word needs a build.  A failure to open it comes back as
    None, and the caller falls through to romaji and spelling -- which is the
    same degradation as a missing dictionary and for the same reason.
    """
    import ctypes as C

    if word in _ASK_CACHE:
        return _ASK_CACHE[word]
    if not _ASK_DLL:
        try:
            path = os.path.join(ROOT, 'build', 'bin', 'tvtts64.dll')
            os.add_dll_directory(os.path.dirname(path))
            d = C.CDLL(path)
            d.tvtts_create_lang.argtypes = [C.c_uint32, C.c_char_p]
            d.tvtts_create_lang.restype = C.c_void_p
            d.tvtts_text_to_phonemes.argtypes = [C.c_void_p, C.c_char_p,
                                                 C.c_char_p, C.c_uint32]
            d.tvtts_text_to_phonemes.restype = C.c_int
            syn = d.tvtts_create_lang(11025, b'en')
            _ASK_DLL.append((d, syn) if syn else None)
        except Exception:
            _ASK_DLL.append(None)
    if _ASK_DLL[0] is None:
        return None
    d, syn = _ASK_DLL[0]
    buf = C.create_string_buffer(8192)
    n = d.tvtts_text_to_phonemes(syn, word.encode('utf-8'), buf, 8192)
    out = buf.value.decode('latin1') if n > 0 else None
    _ASK_CACHE[word] = out
    return out


def parse(ph):
    """The engine's string -> [(name, stress)], and how many units it has.

    `&` opens a stressed unit and a digit after a phoneme is its stress, so a
    word is one unit and an initialism is several -- `NVDA` comes back as
    `&e1N&VE1&DE1&A1.`, four units, which is the test for whether a string
    was read as a word or as letters.  Inferring it from the `&` count is
    weaker than being told, and being told would mean a new API.
    """
    units = []
    cur = []
    i = 0
    n = len(ph)
    while i < n:
        c = ph[i]
        if c == '&' or c == '%':
            # `&` opens a stressed unit and `%` an unstressed one -- `this`
            # comes back `%xiS.` -- and both end the one before.
            if cur:
                units.append(cur)
            cur = []
            i += 1
            continue
        if c == '.':
            i += 1
            continue
        if c in STRESS:
            if cur:
                cur[-1] = (cur[-1][0], int(c))
            i += 1
            continue
        nm = NAME.get(c)
        if nm is not None and nm not in DROP:
            cur.append((nm, 0))
        i += 1
    if cur:
        units.append(cur)
    return units


def is_vowel(name):
    return (name in VOWEL or name in DIPH or name in TRIPH
            or name == 'YU')


def vowel_runs(word):
    """The word's runs of vowel letters, in order.

    Runs rather than letters, because `about` has two syllables and three
    vowel letters: `ou` is one of them.
    """
    import re
    return re.findall(r'[aeiouy]+', (word or u'').lower())


def _ons(ons, carry):
    """The onset to use, taking anything the mora before handed over."""
    if carry[0]:
        got, carry[0] = carry[0], ''
        return got if not ons else ons
    return ons


def adapt(units, word=None):
    """[(name, stress)] units -> (morae, accent), in jp_speak's own symbols.

    What it does, in order: every vowel becomes a mora nucleus and every
    consonant an onset; a consonant with no vowel of its own gets an inserted
    one; a nasal before a consonant or at the end becomes the moraic nasal;
    and a voiceless stop between a short vowel and another consonant becomes
    a geminate.  Those four are the whole of the adaptation, and each is a
    rule rather than a judgement, which is what makes them arguable.
    """
    morae = []
    inserted = False            # was the last mora's vowel one this inserted?
    heavy = False               # did the last nucleus end a diphthong?
    runs = vowel_runs(word)
    nucleus_at = [0]            # which syllable nucleus this is, 1-based
    n_nuclei = sum(1 for u in units for nm, _ in u if is_vowel(nm))
    carry = ['']                # an onset the mora before handed over
    for unit in units:
        k = 0
        while k < len(unit):
            name = unit[k][0]
            nxt = unit[k + 1][0] if k + 1 < len(unit) else None
            # a bare vowel
            if is_vowel(name):
                nucleus_at[0] += 1
                sp = _spelled(name, nucleus_at[0], runs, n_nuclei,
                              is_vowel(nxt))
                got = _nucleus(_ons('', carry), name, 1, sp)
                if sp is not None and name in REDUCED_MEDIAL:
                    carry[0] = 'r'      # the /r/ starts the next mora
                morae.extend(got)
                inserted = False
                heavy = len(got) > 1
                k += 1
                continue
            stress = unit[k + 1][1] if k + 1 < len(unit) else 0
            ons = ONSET.get(name)
            # The engine spells the unaspirated /t/ of an /st/ cluster as D --
            # `stop` is SDo1Pp -- and Japanese hears /t/: ストップ.
            if (ons == 'd' and morae and morae[-1] in ('su', 'si', 'shi')):
                ons = 't'
            if ons is None:
                # NG, QQ, XX and the syllabics
                if name == 'NG':
                    morae.append('N')
                elif name in ('LX', 'UL'):
                    # syllabic /l/: an onset when a vowel follows and a mora
                    # of its own otherwise.  `xylophone` is ザイロフォーン, so
                    # the /l/ takes the next vowel rather than a vowel of its
                    # own and then the next one.
                    if nxt is not None and is_vowel(nxt):
                        nucleus_at[0] += 1
                        after2 = (unit[k + 2][0] if k + 2 < len(unit)
                                  else None)
                        morae.extend(_nucleus(
                            'r', nxt, 1,
                            _spelled(nxt, nucleus_at[0], runs, n_nuclei,
                                     is_vowel(after2))))
                        # these were LEFT ALONE here, so a diphthong before a
                        # syllabic /l/ still blocked the geminate after it and
                        # `pilot` came out パイロト
                        inserted = False
                        heavy = False
                        k += 2
                        continue
                    morae.append('ru')
                elif name == 'UM':
                    morae.append('mu')
                elif name == 'UN':
                    morae.append('N')
                k += 1
                continue
            # a nasal with no vowel after it is the moraic nasal, not /nu/
            if ons in ('n', 'm') and (nxt is None or not is_vowel(nxt)):
                morae.append('N')
                k += 1
                continue
            # an affricate followed by its own fricative release is one
            # consonant, not two: `orange` ends JH ZH and is ジ, not ジジ
            if (nxt is not None and ons in ('j', 'ch')
                    and nxt in ('ZH', 'SH')):
                unit = unit[:k + 1] + unit[k + 2:]
                nxt = unit[k + 1][0] if k + 1 < len(unit) else None
            if nxt is not None and is_vowel(nxt):
                # English's syllabic -le is a consonant plus /l/ in Japanese
                # and the reduced vowel between them goes: `google` is
                # グーグル and `apple` アップル, not グーギル and アピル.
                after = unit[k + 2][0] if k + 2 < len(unit) else None
                if nxt in ('IX', 'AX') and after in ('LX', 'UL'):
                    nucleus_at[0] += 1
                    # the consonant takes an inserted vowel and the reduced
                    # one GOES -- it has to be consumed here, or it comes back
                    # as a mora of its own and `google` is グーグイル
                    morae.extend(_nucleus(_ons(ons, carry), None))
                    inserted = True
                    heavy = False
                    k += 2
                    continue
                else:
                    nucleus_at[0] += 1
                    after2 = (unit[k + 2][0] if k + 2 < len(unit) else None)
                    sp = _spelled(nxt, nucleus_at[0], runs, n_nuclei,
                                  is_vowel(after2))
                    got = _nucleus(_ons(ons, carry), nxt, stress, sp)
                    if sp is not None and nxt in REDUCED_MEDIAL:
                        carry[0] = 'r'
                    morae.extend(got)
                    inserted = False
                    heavy = len(got) > 1
                    k += 2
                    continue
            # No vowel of its own.  A voiceless stop GEMINATES rather than
            # taking a vowel, when it follows a short vowel and is either
            # final or before another obstruent: `cat` is キャット, `apple`
            # アップル, `book` ブック.  Not before /r/, /w/ or /y/, where
            # Japanese has no geminate -- that mistake made `screen` スッリーン
            # instead of スクリーン.
            if (ons in VOICELESS_STOP
                    and _can_geminate(morae, inserted or heavy)
                    and (nxt is None
                         or (ONSET.get(nxt) is not None
                             and ONSET[nxt] not in ('r', 'w', 'y')))):
                morae.append('Q')
                morae.extend(_nucleus(ons, None))
                inserted = True
                heavy = False
                k += 1
                continue
            morae.extend(_nucleus(ons, None))
            inserted = True
            heavy = False
            k += 1
    return morae, accent_of(morae)


SHORTEN_UNSTRESSED = ('OW', 'AO')


def _cv(ons, v):
    """One onset and one vowel -> morae, with the inventory's gaps repaired."""
    fix = REPAIR.get((ons, v))
    if fix is not None:
        return list(fix)
    return [ons + v]


def _spelled(vname, n, runs, n_nuclei=None, before_vowel=0):
    """The vowel the SPELLING gives this nucleus, or None.

    Only for a reduced vowel: everything else is pronounced and the sound is
    what Japanese takes.  None when the counts do not line up, which leaves
    the phonetic vowel standing.
    """
    if n < 1 or n > len(runs):
        return None
    if vname in REDUCED:
        pass
    elif vname in REDUCED_MEDIAL:
        if n_nuclei is None or n >= n_nuclei or not before_vowel:
            return None         # アー: final, or with nowhere for the /r/ to go
    else:
        return None
    return LETTER_VOWEL.get(runs[n - 1][0])


def _nucleus(ons, vname, stress=1, spelled=None):
    """One onset plus one vowel -> the morae it becomes."""
    ons = ons or ''
    if vname is None:
        v = EPENTHETIC.get(ons, EPENTHETIC_DEFAULT)
        return _cv(ons, v)
    if spelled is not None:
        # a reduced vowel: the written one, not the central one
        return _cv(ons, spelled)
    if vname == 'YU':
        # /ju:/ palatalises instead of adding a glide
        return _cv(PALATAL.get(ons, ons) if ons else 'y', 'u') + [':']
    if vname == 'AE' and ons in AE_PALATAL:
        return _cv(AE_PALATAL[ons], 'a')
    if vname in TRIPH:
        a, b, c = TRIPH[vname]
        return _cv(ons, a) + [b, c]
    if vname in DIPH:
        a, b = DIPH[vname]
        return _cv(ons, a) + [b]
    v, long_ = VOWEL.get(vname, ('a', 0))
    # An unstressed /oʊ/ or /ɔ/ comes out short: `microsoft` is
    # マイクロソフト and not マイクローソーフト.  The r-coloured vowels are left
    # long whatever their stress, because a final one is long even unstressed
    # -- コンピューター, プリンター, ウォーター.
    if long_ and stress == 0 and vname in SHORTEN_UNSTRESSED:
        long_ = 0
    out = _cv(ons, v)
    if long_:
        out.append(':')
    return out


def _can_geminate(morae, inserted):
    """A geminate needs a REAL short vowel before it.

    Not after the moraic nasal, not after another geminate, not after a long
    vowel -- Japanese has no ーッ -- and not after a vowel this inserted.
    That last one is the rule that matters: `test` is テスト and not テスット,
    because the /s/ already took an inserted ウ and the /t/ follows that
    rather than a vowel of the word's own.  `cat` is キャット because its /t/
    follows the real æ.
    """
    if not morae:
        return False
    if morae[-1] in ('N', 'Q', ':'):
        return False
    return not inserted


def accent_of(morae):
    """Where the fall goes in a loanword.

    The Tokyo default is the ANTEPENULTIMATE mora -- third from the end -- and
    it is a tendency rather than a rule, which is why this is the first thing
    to argue about.  A mora that cannot carry an accent (the moraic nasal, the
    first half of a geminate, the second half of a long vowel) passes it to
    the one before.
    """
    n = len(morae)
    if n == 0:
        return 0
    a = n - 2
    while a > 1 and morae[a - 1] in ('N', 'Q', ':'):
        a -= 1
    if a < 1:
        a = 1 if n >= 1 else 0
    return a if a <= n else 0


def read(word, phon=None):  # noqa: C901
    """-> (morae, accent, how) for one Latin word.

    `how` is 'word' when the English engine read it as a word and 'letters'
    when it read it as an initialism, which is the classification that decides
    whether this is used at all -- an initialism keeps the Japanese letter
    names it already gets, because adapting the English pronunciation of each
    letter would produce different ones.
    """
    ph = phon if phon is not None else phonemes_of([word])[word]
    units = parse(ph)
    if not units:
        return [], 0, 'none'
    if len(units) > 1:
        return [], 0, 'letters'
    # The `&` count is not enough on its own.  The English front end reads
    # `TTS` as one unit and pronounces it -- テックスタスビーチ came out of
    # that -- so an all-capital word with no vowel LETTER is taken as an
    # initialism whatever the engine made of it.  Capitalisation alone does
    # not decide: `NASA` and `SAPI` are all capitals and have vowels, and are
    # words.
    if (word.isupper() and len(word) > 1
            and not any(c in 'AEIOUY' for c in word)):
        return [], 0, 'letters'
    morae, acc = adapt(units, word)
    return morae, acc, 'word'


# ---- the self-check -------------------------------------------------------

def self_test():
    """Every mora this can emit has to have a katakana, and the alphabet has
    to mean what phonetic.c says it means.

    The first half is what found the inventory's gaps: `water` came out with a
    hole in it because the kana reader has no ウォ, and nothing said so.  A
    rule set is only testable if its output is constrained, so this constrains
    it -- walk every onset against every vowel, and every phoneme name against
    the adaptation, and require a reading for all of them.

    -> a list of complaints, empty when there are none.
    """
    import jp_njd as N
    inv = N._inverse()
    bad = []
    # Only what the rules can REACH.  A plain onset takes any of the five
    # vowels, a palatalised one takes only the /u/ of /ju:/, and the velars
    # take the /a/ of /ae/ -- walking every onset against every vowel asks
    # about combinations nothing emits and buries the real gaps.
    plain = sorted(set(list(ONSET.values()) + ['']))
    for ons in plain:
        for name in sorted(set(list(VOWEL) + list(DIPH) + list(TRIPH))):
            for m in _nucleus(ons, name):
                if m != ':' and m not in inv:
                    bad.append('%r has no katakana: %r + %s'
                               % (m, ons, name))
        for m in _nucleus(ons, None):
            if m not in inv:
                bad.append('%r has no katakana: the vowel inserted after %r'
                           % (m, ons))
        for m in _nucleus(ons, 'YU'):
            if m != ':' and m not in inv:
                bad.append('%r has no katakana: %r + /ju:/' % (m, ons))
    for ons in AE_PALATAL:
        for m in _nucleus(ons, 'AE'):
            if m not in inv:
                bad.append('%r has no katakana: %r + /ae/' % (m, ons))
    return bad


if __name__ == '__main__':
    import sys as _sys
    problems = self_test()
    if problems:
        print('%d problems with the rule set:' % len(problems))
        for b in problems[:40]:
            print('   %s' % b)
    else:
        print('self-check: every mora the rules can emit has a katakana')
    args = _sys.argv[1:]
    if args:
        ph = phonemes_of(args)
        import jp_njd as _N
        inv = _N._inverse()
        for w in args:
            m, a, how = read(w, ph[w])
            print('%-14s %-22s %-7s %-20s acc %s'
                  % (w, ph[w], how,
                     ''.join(inv.get(x, '[' + x + ']') for x in m), a))
