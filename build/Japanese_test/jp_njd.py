# -*- coding: utf-8 -*-
"""The NJD stages: morphology in, readings and accent and phrasing out.

Open JTalk exposes its front end as separate stages, and they are kept separate
here for the same reason -- each one is a rule set that can be read, checked and
argued with on its own:

    set_pronunciation   njd_set_pronunciation
    set_accent_phrase   njd_set_accent_phrase    eighteen rules over morphology
    set_accent_type     njd_set_accent_type      composing a compound's accent
    set_unvoiced_vowel  njd_set_unvoiced_vowel   six rules, and the lexicon's
                                                 own devoicing marks

Not implemented, and each for a stated reason:

    njd_set_digit       numbers; see jp_digit.py, which is its own piece
    njd_set_long_vowel  deprecated in Open JTalk itself -- its function body
                        is `return` behind an #if, so there is nothing to port
    njd2jpcommon        builds the full-context labels an HMM synthesiser
                        wants.  This engine is a formant synthesiser and takes
                        parameter frames, so the whole stage has no counterpart

The rules are read from Open JTalk's source as a specification and written out
again here; no code is copied.  Open JTalk is BSD 3-clause.  See NOTICE.
"""
from ojt_tables import (MORA as _MORA, MORA_MAX as _MORA_MAX,
                        MORA_LIST as _MORA_LIST,
                        ACCENT_CONST as _A, PRON_LIST as _PL,
                        UNVOICED as _UVT,
                        UNVOICED_CONST as _UV,
                        PRONUNCIATION_CONST as _PR)

# The surfaces of njd_set_pronunciation_list, for the exact-match test the
# kana-filler chaining does.
PRON_SURFACES = frozenset(r[0] for r in _PL)
from jp_dict import (unknown_pron, MEISHI, DOUSHI, KEIYOUSHI, FUKUSHI, SETSUZOKUSHI,
                     RENTAISHI, JODOUSHI, JOSHI, KANDOUSHI, KIGOU, SETTOUSHI,
                     FILLER, SETSUBI, HIJIRITSU, FUKUSHI_KANOU,
                     KEIYOUDOUSHI_GOKAN, SAHEN_SETSUZOKU, SETSUZOKUJOSHI,
                     RENYOU, SEI, MEI, TE, DE, _unknown_pron)

# The kana the rules name, EXTRACTED rather than typed out: every one is a
# #define in Open JTalk's own rule headers and tools/gen_ja_rules.py reads it,
# so typing them here as well would be the drift that generator exists to
# prevent.  SMALL, SOKUON and N_KANA were typed out and were not used by
# anything, so they are gone rather than extracted.
UNVOICED_MARK = _UV['QUOTATION']   # the dictionary's own devoicing mark
CHOUON = _UV['CHOUON']             # the length mark
PAUSE = _UV['TOUTEN']              # the weak break
QUESTION = _UV['QUESTION']
U_KANA = _PR['U']
SHI = _UV['SHI']
MA = _UV['MA']
DE_K = _UV['DE']
SU = _UV['SU']

# And the one that is OURS.  Open JTalk folds every punctuation mark into the
# single pause symbol above; this front end keeps the strong break, because
# its prosody grades the boundary and has nowhere later to recover the
# distinction.  See declared_pause in tools/ja_stage_parity.py.
PERIOD = u'\u3002'                 # 。

# A question mark is NOT in the strong-break set, and the order matters: the
# normaliser folds the fullwidth ？ to an ASCII ?, so if the full stops are
# tested first a question becomes a statement and the final rise is lost.
# Open JTalk keeps ？ as its own symbol all the way through for this reason --
# njd_set_unvoiced_vowel reads it, and so does the contour.
QUESTION_FORMS = frozenset(u'?' + QUESTION)
BREAK_STRONG = set(PERIOD + u'!\uff01.')
BREAK_WEAK = set(PAUSE + u',:;\u30fb\uff0c\uff1a\uff1b')


def split_morae(pron):
    """-> [(mora in katakana, already-devoiced flag)]

    Longest-prefix match against Open JTalk's own 159-mora inventory, which is
    what its get_mora_information walks.  That list is ordered so that each
    base's two-character combinations precede the bare one -- ヴョ ヴュ ヴャ
    ヴォ ヴェ ヴィ ヴァ ヴ -- so taking the first match, as upstream does, is
    taking the longest, and a longest-prefix match over the set is the same
    function.  Deriving the inventory instead means guessing which small kana
    attach to what, so it is extracted; see tools/gen_ja_rules.py.

    A quotation mark after a mora is the dictionary saying that mora's vowel
    is devoiced, so it is consumed here and reported rather than parsed as a
    character.

    ONE DELIBERATE DIFFERENCE.  A character the inventory does not contain --
    ヮ, the small WA of クヮルテット, is the only one that occurs in a reading
    -- makes upstream print "Wrong pron" and abandon the whole word, losing
    every mora after it as well.  Here it becomes a one-character mora and the
    kana reader decides what to do with it, which costs nothing and keeps the
    rest of the word.
    """
    out = []
    i = 0
    n = len(pron)
    while i < n:
        if pron[i] == UNVOICED_MARK:
            i += 1
            continue                    # a mark with no mora before it
        size = 0
        for k in range(min(_MORA_MAX, n - i), 0, -1):
            if pron[i:i + k] in _MORA:
                size = k
                break
        if size == 0:
            size = 1                    # not in the inventory; see above
        m = pron[i:i + size]
        i += size
        flag = 0
        if i < n and pron[i] == UNVOICED_MARK:
            flag = 1
            i += 1
        out.append((m, flag))
    return out


# ---- njd_set_pronunciation ------------------------------------------------

def set_pronunciation(words):
    """As far as this stage changes what is said.

    A word the dictionary has no reading for becomes a PAUSE if it is not
    kana, and a reading from its kana if it is.  Open JTalk's choice of a
    pause there is the right one and the opposite of what this front end did
    with kanji it could not read, which was to drop it silently.

    Then two fixes that are about pronunciation rather than unknown words: a
    verb or auxiliary followed by the auxiliary う lengthens instead -- はなそう
    is /hanasoo/ -- and です and ます before a question mark keep their vowel
    voiced, because the question rises on that mora.
    """
    for w in words:
        # A question mark is settled first and whatever the dictionary says
        # about it, because the analyser can hand it back as an ordinary
        # symbol with a reading and then nothing downstream would know a
        # question had been asked.
        #
        # Its part of speech stays whatever the dictionary gave it, which for
        # ？ is 記号.  1.09 set FILLER unconditionally inside its unknown-word
        # block, so ？ came out a filler there; 1.11 made that conditional on a
        # reading having actually been derived, and ？ yields no morae, so it
        # keeps 記号.  This follows 1.11.
        #
        # The difference is not cosmetic -- 記号 trips accent-phrase rule 14
        # and breaks the phrase where FILLER would instead bring rule 0 of the
        # devoicing stage into play -- which is why the boundary test in
        # jp_front keys on the PRONUNCIATION rather than on this field.
        if w.string in QUESTION_FORMS:
            w.pron = QUESTION
            continue
        if w.mora_size == 0 or not w.pron or w.pron == u'*':
            s = w.string
            # Only the FIRST part-of-speech field is rewritten, and only in
            # the two branches upstream rewrites it in: a derived reading
            # makes the word a filler and clears the three groups, and a word
            # that yields no reading at all becomes a 記号 with its groups
            # intact.  So 。 keeps 記号/句点 and ？ keeps 記号/一般, while an
            # unreadable 添励 goes from 名詞/一般 to 記号/一般.  Clobbering the
            # whole tuple with a bare 記号 disagreed with the reference on
            # every sentence that ended in a full stop; leaving it entirely
            # alone disagreed on every unreadable kanji pair.
            if s in BREAK_STRONG:
                w.pron = PERIOD
                w.pos = (KIGOU,) + tuple(w.pos[1:])
            elif s in BREAK_WEAK:
                w.pron = PAUSE
                w.pos = (KIGOU,) + tuple(w.pos[1:])
            else:
                r, nm = unknown_pron(s)
                if nm:
                    w.pron = r
                    w.mora_size = nm
                    # 1.11 overwrites the part of speech only when a reading
                    # was actually derived; 1.09 did it unconditionally.
                    w.pos = (FILLER, u'*', u'*', u'*', u'*', u'*')
                else:
                    w.pron = PAUSE
                    w.pos = (KIGOU,) + tuple(w.pos[1:])
    # 1.11 adds this: a RUN of unknown kana becomes one word rather than
    # several.  Without it a kana string the dictionary does not know is a
    # string of one-mora words, and the eighteen accent-phrase rules then see
    # a boundary between every character of it.
    #
    # The membership test is EXACT -- the word's whole string has to be one
    # row of the table -- and not the prefix walk that derived the reading a
    # few lines up.  So `ええ`, which the analyser hands over as one filler of
    # two kana, does not chain with the `と` after it, while two separate `え`
    # would.  Deriving a reading instead and treating any success as a match
    # joined ええ and と into one word where upstream keeps two.
    head = None
    for w in words:
        if w.pos[0] == FILLER and w.string in PRON_SURFACES:
            if head is None:
                head = w
            else:
                head.string += w.string
                head.pron += w.pron
                head.mora_size += w.mora_size
                w.pron = None
        else:
            head = None
    words = [w for w in words if w.pron]

    for i in range(len(words) - 1):
        a, b = words[i], words[i + 1]
        if (b.pron == U_KANA and b.pos[0] == JODOUSHI
                and a.pos[0] in (DOUSHI, JODOUSHI) and a.mora_size > 0):
            b.pron = CHOUON
    for i in range(len(words) - 1):
        a, b = words[i], words[i + 1]
        if a.pos[0] == JODOUSHI and b.string in (QUESTION, u'?'):
            if a.string == _PR['DESU_STR']:         # です
                a.pron = _PR['DESU_PRON']           # デス
            elif a.string == _PR['MASU_STR']:       # ます
                a.pron = _PR['MASU_PRON']           # マス
    return [w for w in words if w.pron]


# ---- a reading for a Latin token the dictionary does not know -------------

# The inverse of the mora inventory, DERIVED from it rather than written out.
# MORA_LIST is katakana and jp_mora.to_morae maps katakana to mora symbols, so
# running one over the other gives the map back.  A hand-written inverse would
# be 159 rows of katakana typed a second time, which is the transcription this
# project keeps getting bitten by; this one cannot disagree with the forward
# direction because it is built out of it.  Where two katakana give the same
# symbol -- ジ and ヂ do -- the first in upstream's order wins, which is the
# convention upstream uses everywhere else.
_INV = {}


def _inverse():
    import jp_mora as M
    if not _INV:
        for k in _MORA_LIST:
            got = M.to_morae(k)
            if len(got) != 1:
                continue
            old = _INV.get(got[0])
            # fewest characters, then lowest code point: the inventory makes
            # distinctions the symbols do not -- ヴ and ブ are both /bu/ --
            # and upstream's order is longest-first, so taking the first takes
            # the exotic spelling.  `bukku` came out ヴック.
            if old is None or (len(k), k) < (len(old), old):
                _INV[got[0]] = k
    return _INV


def romaji_to_kana(text):
    """-> (katakana, characters the parser could not place), or (None, n).

    The rest of the pipeline wants a katakana pronunciation, because that is
    what split_morae, the devoicing stage and the mora count all read.
    """
    import jp_mora as M
    drops = [0]
    got = M.to_morae(text, drops)
    if not got:
        return None, drops[0]
    inv = _inverse()
    out = []
    for g in got:
        k = inv.get(g)
        if k is None:
            return None, drops[0]
        out.append(k)
    return u''.join(out), drops[0]


def read_latin(words, ask=None):
    """A reading for a Latin token the dictionary does not know, PER TOKEN.

    THE DECISION CANNOT BE MADE PER UTTERANCE.  One known word would suppress
    the romaji reading of every other, so `Windows konnichiwa` found Windows,
    kept the spelled-out reading, and said the Japanese names of konnichiwa's
    ten letters.  A reading belongs to a word, so the decision goes where
    words get readings: after the pronunciation stage and before the digit,
    accent and devoicing stages, so a dictionary reading, a derived one and
    Japanese text can share a sentence.

    This applies only to a token the pronunciation stage has already given up
    on and SPELLED OUT, so anything readable is an improvement; a token the
    dictionary knew keeps its entry and is not touched.  A Latin-to-kana rule
    set would plug in exactly here, as the step between those two.
    """
    for w in words:
        if w.pos[0] != FILLER:
            continue
        st = w.string
        # fullwidth Latin only, which is what text2mecab turns ASCII into.
        # Anything with a kana in it is already Japanese and the derived
        # reading is the right one.
        if not st or not all(u'Ａ' <= c <= u'Ｚ' or u'ａ' <= c <= u'ｚ'
                             for c in st):
            continue
        plain = u''.join(chr(ord(c) - 0xfee0) for c in st)
        #
        # THE RULES FIRST, when there is an English front end to ask.
        #
        # Romaji-before-rules read `mouse` as モーセ, `fire` フィレ, `orange`
        # オランゲ and `button` ブットン: every one consumes whole as romaji,
        # so romaji answered before the rules were asked.  Complete
        # consumption proves a spelling CAN be read as romaji, never that it
        # was meant as one, and ordinary text wants the English default --
        # every Japanese system that was listened to reads a bare `take` as
        # テイク.
        #
        # Deliberate romaji is the explicit case, and its signal is that the
        # whole utterance is clean romaji with nothing in the dictionary,
        # which the library tests before it gets here.
        #
        acc = 0
        pron = None
        if ask is not None:
            import jp_g2p as GP
            ph = ask(plain)
            if ph:
                morae, g_acc, how = GP.read(plain, ph)
                if how == 'letters':
                    continue        # an initialism keeps its letter names
                if how == 'word' and morae:
                    inv = _inverse()
                    ks = [inv.get(m) for m in morae]
                    if all(k is not None for k in ks):
                        pron = u''.join(ks)
                        acc = g_acc
        if pron is None:
            pron, drops = romaji_to_kana(plain)
            # complete consumption, which is what tells deliberate romaji from
            # a Latin word that merely survived the parser's leniency
            if pron is None or drops != 0:
                continue
        w.pron = pron
        w.read = pron
        w.mora_size = len(split_morae(pron))
        w.acc = acc             # the rules' accent, or 0 from romaji
        # A NOUN now, not a filler.  Filler is upstream's marker for "a
        # reading was derived character by character" and it carries a rule of
        # its own -- the devoicing stage's rule 0 says a filler never devoices
        # -- so leaving it on a word that now has a real reading would read
        # スコシ with both its high vowels voiced.  The groups are left `*`
        # because that is what is actually known.
        w.pos = (MEISHI, u'*', u'*', u'*', u'*', u'*')
    return words


# ---- njd_set_accent_phrase ------------------------------------------------

def _renyou(cform):
    return cform is not None and cform.startswith(RENYOU)


def set_accent_phrase(words):
    """Where one accent phrase ends and the next begins.

    Eighteen numbered rules, each either chaining this word onto the phrase
    before it (1) or starting a new one (0).  A later rule overrides an
    earlier one, so the order IS the rule and they are kept in it.

    This is the stage that decides where our own phrase marks go.  The front
    end exposed them as input symbols and said so: "Kawai's rules decide where
    these go from clause and ICRLB boundaries, which needs a parser we do not
    have."  These eighteen tests are that parser's output.
    """
    if not words:
        return words
    # The first word is left UNSET, not set to 0.  Upstream's loop starts at
    # njd->head->next and never touches the head, so its chain flag stays -1,
    # and every consumer tests `!= 1` rather than `== 0`: an unset head and a
    # head that starts a phrase behave identically.  Writing 0 here therefore
    # changed nothing audible and showed up as a disagreement on the first
    # word of every single utterance, which is 313 of 1824 words in a parity
    # run -- enough noise to hide a real one.
    for i in range(1, len(words)):
        w, p = words[i], words[i - 1]
        if w.chain_flag >= 0:
            continue
        w.chain_flag = 1                                            # 01
        if p.pos[0] == MEISHI and w.pos[0] == MEISHI:               # 02
            w.chain_flag = 1
        if p.pos[0] == KEIYOUSHI and w.pos[0] == MEISHI:            # 03
            w.chain_flag = 0
        if (p.pos[0] == MEISHI and p.pos[1] == KEIYOUDOUSHI_GOKAN
                and w.pos[0] == MEISHI):                            # 04
            w.chain_flag = 0
        if p.pos[0] == DOUSHI and w.pos[0] in (KEIYOUSHI, MEISHI):  # 05
            w.chain_flag = 0
        if (w.pos[0] in (FUKUSHI, SETSUZOKUSHI, RENTAISHI)
                or p.pos[0] in (FUKUSHI, SETSUZOKUSHI, RENTAISHI)):  # 06
            w.chain_flag = 0
        if p.pos[0] == MEISHI and p.pos[1] == FUKUSHI_KANOU:        # 07
            w.chain_flag = 0
        if w.pos[0] == MEISHI and w.pos[1] == FUKUSHI_KANOU:
            w.chain_flag = 0
        if w.pos[0] in (JODOUSHI, JOSHI):                           # 08
            w.chain_flag = 1
        if (p.pos[0] in (JODOUSHI, JOSHI)
                and w.pos[0] not in (JODOUSHI, JOSHI)):             # 09
            w.chain_flag = 0
        if p.pos[1] == SETSUBI and w.pos[0] == MEISHI:              # 10
            w.chain_flag = 0
        if w.pos[0] == KEIYOUSHI and w.pos[1] == HIJIRITSU:         # 11
            if p.pos[0] in (DOUSHI, KEIYOUSHI):
                if _renyou(p.pos[5]):
                    w.chain_flag = 1
            elif p.pos[0] == JOSHI and p.pos[1] == SETSUZOKUJOSHI:
                if p.string in (TE, DE):
                    w.chain_flag = 1
        if w.pos[0] == DOUSHI and w.pos[1] == HIJIRITSU:            # 12
            if p.pos[0] == DOUSHI and _renyou(p.pos[5]):
                w.chain_flag = 1
            elif p.pos[0] == MEISHI and p.pos[1] == SAHEN_SETSUZOKU:
                w.chain_flag = 1
        if p.pos[0] == MEISHI:                                      # 13
            if (w.pos[0] in (DOUSHI, KEIYOUSHI)
                    or w.pos[1] == KEIYOUDOUSHI_GOKAN):
                w.chain_flag = 0
        if w.pos[0] == KIGOU or p.pos[0] == KIGOU:                  # 14
            w.chain_flag = 0
        if w.pos[0] == SETTOUSHI:                                   # 15
            w.chain_flag = 0
        if p.pos[3] == SEI and w.pos[0] == MEISHI:                  # 16
            w.chain_flag = 0
        if p.pos[0] == MEISHI and w.pos[3] == MEI:                  # 17
            w.chain_flag = 0
        if w.pos[1] == SETSUBI:                                     # 18
            w.chain_flag = 1
    return words


# ---- njd_set_accent_type --------------------------------------------------

def get_rule(chain_rule, prev_pos):
    """The chain rule's small grammar.

    A rule is either a bare code (C1), or alternatives chosen by the PREVIOUS
    word's part of speech, where '%' introduces a test, '@' a numeric offset
    and '/' separates alternatives:  動詞%F4@1/形容詞%F2@-1
    """
    if not chain_rule:
        return u'*', 0
    toks = []
    buf = u''
    for ch in chain_rule:
        if ch in u'%@/':
            toks.append((buf, ch))
            buf = u''
        else:
            buf += ch
    toks.append((buf, u'\0'))

    i = 0
    while i < len(toks):
        text, sep = toks[i]
        if sep == u'%':
            if prev_pos and text and text in prev_pos:
                rule = toks[i + 1][0] if i + 1 < len(toks) else u'*'
                add = 0
                if i + 1 < len(toks) and toks[i + 1][1] == u'@':
                    try:
                        add = int(toks[i + 2][0])
                    except (IndexError, ValueError):
                        add = 0
                return rule or u'*', add
            i += 1                      # skip this alternative
            while i < len(toks) and toks[i - 1][1] in (u'%', u'@'):
                i += 1
            continue
        add = 0
        if sep == u'@':
            try:
                add = int(toks[i + 1][0])
            except (IndexError, ValueError):
                add = 0
        return text or u'*', add
    return u'*', 0


# njd_set_accent_type's digit branch tests these by name.  SUU and KAZU are
# the same character; upstream has both names and so does this.
_KAZU = _A['KAZU']
_JYUU, _HYAKU, _SEN = _A['JYUU'], _A['HYAKU'], _A['SEN']
_MAN, _OKU, _CHOU = _A['MAN'], _A['OKU'], _A['CHOU']
_ICHI, _NI, _SAN = _A['ICHI'], _A['NI'], _A['SAN']
_YON, _GO, _ROKU = _A['YON'], _A['GO'], _A['ROKU']
_NANA, _HACHI, _KYUU = _A['NANA'], _A['HACHI'], _A['KYUU']
_NAN, _SUU, _IKU = _A['NAN'], _A['SUU'], _A['IKU']
_ONES = (_ICHI, _NI, _SAN, _YON, _GO, _ROKU, _NANA, _HACHI, _KYUU)


def _digit_accent(words, k):
    """Where the fall goes in a number: 三十 is サンジュー, 七百 ナナヒャク.

    A place name does not simply inherit what its chain rule would give it --
    each of 十 百 千 万 億 兆 has its own rule, and several of them depend on
    which digit precedes.  Upstream keeps this as a block of named tests after
    the chain-rule switch and before the mora tally, and so does this; it sets
    the accent of the PRECEDING word, not of the place name.

    Note that the accent it writes is a position in the number as a whole, so
    一千万 ends up with the fall its last place name put there.
    """
    w = words[k]
    p = words[k - 1] if k > 0 else None
    nx = words[k + 1] if k + 1 < len(words) else None
    if (p is not None and w.chain_flag == 1
            and p.pos[1] == _KAZU and w.pos[1] == _KAZU):
        if w.string == _JYUU:                               # 10^1
            # Upstream tests 三四九何数 here and then sets 1 in both arms, so
            # the test is vestigial; what matters is the override below it.
            p.acc = 1
            if p.string in (_GO, _ROKU, _HACHI) and nx is not None \
                    and nx.string in _ONES:
                # 五十一 is flat up to the one: ゴジューイチ, no fall on ゴ
                p.acc = 0
        elif w.string == _HYAKU:                            # 10^2
            if p.string == _NANA:
                p.acc = 2
            elif p.string in (_SAN, _YON, _KYUU, _NAN):
                p.acc = 1
            else:
                p.acc = p.mora_size + w.mora_size
        elif w.string == _SEN:                              # 10^3
            p.acc = p.mora_size + 1
        elif w.string == _MAN:                              # 10^4
            p.acc = p.mora_size + 1
        elif w.string == _OKU:                              # 10^8
            p.acc = 2 if p.string in (_ICHI, _ROKU, _NANA, _HACHI, _IKU) else 1
        elif w.string == _CHOU:                             # 10^12
            p.acc = 2 if p.string in (_ROKU, _NANA) else 1
    # Independent of all that: a 十 heading its own phrase with a number after
    # it is flat.  十二 is ジューニ, not ジューニ with a fall on ジュ.
    if (w.string == _JYUU and w.chain_flag != 1
            and nx is not None and nx.pos[1] == _KAZU):
        w.acc = 0


def set_accent_type(words):
    """The accent of a compound, composed from its parts.

    Each chained word applies its chain rule to the accent of the word that
    HEADS its phrase, offset by how many morae precede it.  C1 carries the
    second element's own accent across; C2 puts the fall on the first mora of
    the second element; C3 on the last of the first; C4 flattens the compound.
    """
    top = None
    mora = 0
    for i, w in enumerate(words):
        if i == 0 or w.chain_flag != 1:
            top, mora = w, 0
        else:
            rule, add = get_rule(w.chain_rule, words[i - 1].pos[0])
            if rule == u'F2':
                if top.acc == 0:
                    top.acc = mora + add
            elif rule == u'F3':
                if top.acc != 0:
                    top.acc = mora + add
            elif rule == u'F4':
                top.acc = mora + add
            elif rule == u'F5':
                top.acc = 0
            elif rule == u'C1':
                top.acc = mora + w.acc
            elif rule == u'C2':
                top.acc = mora + 1
            elif rule == u'C3':
                top.acc = mora
            elif rule == u'C4':
                top.acc = 0
            elif rule == u'P1':
                top.acc = 0 if w.acc == 0 else mora + w.acc
            elif rule == u'P2':
                top.acc = mora + 1 if w.acc == 0 else mora + w.acc
            elif rule == u'P6':
                top.acc = 0
            elif rule == u'P14':
                if w.acc != 0:
                    top.acc = mora + w.acc
            # '*', F1 and C5 leave the accent where it is
        _digit_accent(words, i)
        mora += w.mora_size
    return words


# ---- njd_set_unvoiced_vowel ----------------------------------------------

# The three candidate classes, each with the following-mora list it devoices
# before.  UPSTREAM'S OWN TABLES, extracted: these were re-derived here as a
# character-to-row map plus three sets of row letters, which is the same
# function over this data and one transcription of 110 kana away from not
# being.  The exclusions are the point: /su/ does not devoice before another
# s-row mora and /fu hi fi/ not before an h-row one, because that would leave
# two fricatives with nothing between them.
CAND = (_UVT['candidate_list1'], _UVT['candidate_list2'],
        _UVT['candidate_list3'])
NEXT = (_UVT['next_mora_list1'], _UVT['next_mora_list2'],
        _UVT['next_mora_list3'])


def apply_unvoice_rule(cur, nxt):
    """1 devoiced, 0 voiced, -1 the rule has nothing to say.

    The CANDIDATE is matched exactly -- ス devoices, スー does not -- and the
    FOLLOWING mora by prefix, which upstream does with strtopcmp and is not
    the same test.  キャ is not in next_mora_list3, but it starts with キ,
    which is, so ツ devoices before キャ.
    """
    if nxt is None:
        return 0
    for cand, allow in zip(CAND, NEXT):
        if cur in cand:
            return 1 if any(nxt.startswith(a) for a in allow) else 0
    return -1


def set_unvoiced_vowel(words):
    """-> a flat list of (mora, devoiced) over the whole utterance.

    Six rules, in Open JTalk's order and numbering:

      0  a filler never devoices
      1  /masu/ and /desu/ devoice their final mora, unless a question mark or
         a long vowel follows, where the question has to rise on it
      2  /shi/ as a verb, auxiliary or particle, by the same look-ahead
      3  NO TWO ADJACENT devoiced morae.  If the next one devoices, this one
         does not
      4  the mora carrying the ACCENT NUCLEUS does not devoice
      5  otherwise the three candidate classes decide

    Rules 3 and 4 are the two the old heuristic had no equivalent of, and they
    are not small: of the 199,504 morae it devoiced over all 486,646 pronounced
    naist-jdic entries, 11.5% were the first of an adjacent pair and 11.2% were
    accent nuclei -- 20.8% caught by one or the other.  (Quoted as 10.9% and
    14.2% until 2026-10-05; the second was wrong under every convention
    checked, and the two effects are the same size rather than the nucleus
    being the larger.)  Rule 4 is
    also why the accent type is worth having twice over -- it sets the pitch
    fall AND it protects a vowel from being whispered away.
    """
    # flatten to morae, carrying each one's word, index within its phrase,
    # and that phrase's accent type
    flat = []
    acc = 0
    midx = 0
    for w in words:
        if w.chain_flag != 1:
            midx = 0
            acc = w.acc
        for m, pre in split_morae(w.pron):
            flat.append({'mora': m, 'flag': 1 if pre else -1, 'word': w,
                         'midx': midx, 'acc': acc})
            midx += 1
    n = len(flat)

    for i in range(n):
        e = flat[i]
        w = e['word']
        nx = flat[i + 1] if i + 1 < n else None
        nx2 = flat[i + 2] if i + 2 < n else None

        # Rules 1 and 2 look AHEAD and settle the next mora, so they run even
        # when this one is already decided -- which it is whenever the
        # dictionary marked it.
        if e['flag'] == -1:
            # rule 1: the /masu/ and /desu/ look-ahead
            if (nx is not None and nx2 is not None and nx['word'] is w
                    and nx2['word'] is not w
                    and e['mora'] in (MA, DE_K) and nx['mora'] == SU
                    and nx['word'].pos[0] in (DOUSHI, JODOUSHI, KANDOUSHI)):
                nx['flag'] = 0 if nx2['word'].pron in (QUESTION, CHOUON) else 1
            # rule 2: the /shi/ look-ahead
            if (nx is not None and nx['flag'] == -1 and nx['mora'] == SHI
                    and nx['word'].pron == SHI
                    and nx['word'].pos[0] in (DOUSHI, JODOUSHI, JOSHI)):
                if nx['acc'] == nx['midx'] + 1:
                    nx['flag'] = 0                              # rule 4
                else:
                    nx['flag'] = apply_unvoice_rule(
                        nx['mora'], nx2['mora'] if nx2 else None)  # rule 5
                if nx['flag'] == 1:
                    e['flag'] = 0                               # rule 3
                    if nx2 is not None and nx2['flag'] == -1:
                        nx2['flag'] = 0

        # Then settle this mora, if nothing above or before it already did.
        if e['flag'] == -1:
            if w.pos[0] == FILLER:
                e['flag'] = 0                                   # rule 0
            elif nx is not None and nx['flag'] == 1:
                e['flag'] = 0                                   # rule 3
            elif e['acc'] == e['midx'] + 1:
                e['flag'] = 0                                   # rule 4
            else:
                e['flag'] = apply_unvoice_rule(
                    e['mora'], nx['mora'] if nx else None)      # rule 5
            if e['flag'] == -1:
                e['flag'] = 0

        # Rule 3, FORWARDS, and this is the half that does the work.  Upstream
        # ends every iteration of its own loop with
        #
        #     if (flag1 == 1 && flag2 == -1) flag2 = 0;
        #
        # A mora just marked devoiced forces the NEXT UNDECIDED one voiced.
        # The backward test above -- if the next one is already devoiced, this
        # one is not -- almost never fires, because the loop runs forwards and
        # the next mora is still undecided when this one is settled.  Without
        # this line adjacent devoicing is permitted, which is exactly what the
        # docstring claimed could not happen: 複数 came out
        # フ’ク’スー against upstream's
        # フ’クスー, and so did 複製, 複写
        # and 美しさ.
        #
        # It runs for EVERY mora, including one the dictionary already marked,
        # because upstream's is outside its own decision branch: ます
        # arrives with its ス pre-marked and still has to silence a
        # candidate after it.  Only an UNDECIDED neighbour is overridden, so a
        # run of three candidates alternates rather than all collapsing.
        if e['flag'] == 1 and nx is not None and nx['flag'] == -1:
            nx['flag'] = 0
    return [(e['mora'], e['flag'] == 1, e['word']) for e in flat]
