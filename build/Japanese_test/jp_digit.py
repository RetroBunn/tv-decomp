# -*- coding: utf-8 -*-
"""njd_set_digit: digits into numbers, and counters into their real readings.

Without this stage `1250円` is five digit names in a row -- イチニゴゼロエン --
which is not how anyone says a price.  With it the digits become 千二百五十円,
センニヒャクゴジューエン, and `一本` stops being イチホン and becomes イッポン.

WHAT IS HERE, and these are three separate jobs:

  THE PLACE-VALUE EXPANSION.  A run of digits gets 十 百 千 inserted inside
  each group of four and 万 億 兆 between groups, with the digits that go
  unsaid silenced -- 一十 is 十, not イチジュー.

  IS IT A NUMBER AT ALL.  `sequence_score` decides.  A run with a counter
  after it scores up and is read as a quantity; a run in brackets, after a
  hyphen, or after 番号 scores down and is read digit by digit -- which is
  what a phone number or a part number wants, and it also lengthens 0, 2 and
  5 to ゼロ, ニー and ゴー, the way they are said over a line.

  THE COUNTER READINGS.  Japanese counters assimilate: 一本 is イッポン, 三本
  サンボン, 六本 ロッポン.  Eleven classes change the DIGIT's reading and five
  change the COUNTER's first mora, voicing or semi-voicing it.

  COMMA GROUPING, which is 1.11's addition and the reason this was rewritten.
  `1,250` is a number and `1,25,0` is not, and the test is whether every comma
  falls at a thousands position -- counting back from the last digit before
  any decimal point, a comma must sit at index 3, 7, 11 and nowhere else, and
  a thousands position must carry one.  Commas that pass are removed and the
  digits read as a quantity; commas that fail end the number at the first of
  them.  A leading zero makes it an identifier whatever the commas say.

  THE NAMED EXCEPTIONS, in `set_counters`: the native numerals (一粒 ヒトツブ),
  people (一人 ヒトリ), and days of the month (一日 ツイタチ after a month,
  二十日 ハツカ, 十四日 ジューヨッカ).  The native-numeral table keys on the
  counter's `read` field, which is why jadic.bin carries it; see
  tools/gen_ja_dict.py.

Read from Open JTalk 1.11's njd_set_digit.c as a specification and written out
again; no code is copied.  Open JTalk is BSD 3-clause.  See NOTICE.

The release matters for this stage and almost no other: 1.11 rewrote it,
by 211 lines, where every other stage differs from 1.09 only in its
copyright year.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ojt_tables import DIGIT as _T, DIGIT_CONST as _C
import jp_dict as D

KAZU = _C['KAZU']                       # 数
SUUSETSUZOKU = _C['SUUSETSUZOKU']       # 数接続
JOSUUSHI = _C['JOSUUSHI']               # 助数詞
FUKUSHIKANOU = _C['FUKUSHIKANOU']       # 副詞可能
TEN1, TEN2 = _C['TEN1'], _C['TEN2']     # the decimal point, ． and ・
BANGOU = _C['BANGOU']                   # 番号
KAKKO1, KAKKO2 = _C['KAKKO1'], _C['KAKKO2']
HAIHUN = tuple(_C['HAIHUN%d' % i] for i in range(1, 6))
ZERO = (_C['ZERO1'], _C['ZERO2'])       # 〇 ０
TWO, FIVE = _C['TWO'], _C['FIVE']       # 二 五
SIX = _C['SIX']                         # 六, new in 1.11
COMMA = _C['COMMA']                     # ，, new in 1.11
KIGOU = _C['KIGOU']                     # 記号
NIN = _C['NIN']                         # 人
GATSU = _C['GATSU']                     # 月
NICHI = _C['NICHI']                     # 日
NICHIKAN = _C['NICHIKAN']               # 日間
ONE = _C['ONE']                         # 一
FOUR = _C['FOUR']                       # 四
TEN = _C['TEN']                         # 十
# Upstream spells two of these DITIT rather than DIGIT.  Taking either
# name means a corrected typo upstream does not silently drop a rule.
_HATSUKA = _C['HATSUKA']
_YOKKA = _C['YOKKA']
MEISHI = _C['MEISHI']                   # 名詞
STAR = u'*'

# The eleven classes that change the digit's reading, and the five that change
# the counter's.  Paired as (class list, conversion table) in upstream's own
# order, because the first match wins.
DIGIT_CLASSES = [('numerative_class1b', 'conv_table1b'),
                 ('numerative_class1c1', 'conv_table1c1'),
                 ('numerative_class1c2', 'conv_table1c2'),
                 ('numerative_class1d', 'conv_table1d'),
                 ('numerative_class1e', 'conv_table1e'),
                 ('numerative_class1f', 'conv_table1f'),
                 ('numerative_class1g', 'conv_table1g'),
                 ('numerative_class1h', 'conv_table1h'),
                 ('numerative_class1i', 'conv_table1i'),
                 ('numerative_class1j', 'conv_table1j'),
                 ('numerative_class1k', 'conv_table1k')]
NUMERATIVE_CLASSES = [('numerative_class2b', 'conv_table2b'),
                      ('numerative_class2c', 'conv_table2c'),
                      ('numerative_class2d', 'conv_table2d'),
                      ('numerative_class2e', 'conv_table2e'),
                      ('numerative_class2f', 'conv_table2f')]


def word_from_feature(feature):
    """A Word from one of the tables' CSV feature strings.

    NJDNode_load's single-word path: the surface, six part-of-speech fields,
    the base form, the reading, the pronunciation, accent/mora, the chain rule
    and optionally the chain flag.
    """
    f = feature.split(u',')
    while len(f) < 12:
        f.append(STAR)
    acc_f = f[10]
    if STAR in acc_f or u'/' not in acc_f:
        acc = mora = 0
    else:
        a, m = (acc_f.split(u'/') + [u''])[:2]
        acc = int(a) if a.isdigit() else 0
        mora = int(m) if m.isdigit() else 0
    w = D.Word(f[0], tuple(f[1:7]), f[9], acc, mora, f[11], read=f[8])
    if len(f) > 12 and f[12] in (u'0', u'1'):
        w.chain_flag = int(f[12])
    return w


def is_period(st):
    return st is not None and st in (TEN1, TEN2)


def is_comma(st):
    return st is not None and COMMA is not None and st == COMMA


def get_digit(w, convert=False):
    """The value of a digit word, or -1.  With `convert`, also normalise its
    spelling to the kanji form the later tables are keyed by."""
    if w.string == STAR or w.pos[1] != KAZU:
        return -1
    lst = _T['numeral_list1']
    for i in range(0, len(lst) - 2, 3):
        if lst[i] == w.string:
            if convert:
                w.string = lst[i + 2]
            return int(lst[i + 1])
    return -1


def _at(words, i, f):
    return f(words[i]) if 0 <= i < len(words) else None


def sequence_score(words, s, e):
    """Positive: a quantity.  Negative: an identifier, read digit by digit."""
    score = 0
    pg1 = lambda w: w.pos[1]
    pg2 = lambda w: w.pos[2]
    st = lambda w: w.string
    if s - 1 >= 0:
        if _at(words, s - 1, pg1) == SUUSETSUZOKU:
            score += 2
        if (_at(words, s - 1, pg2) == JOSUUSHI
                or _at(words, s - 1, pg1) == FUKUSHIKANOU):
            score += 1
        v = _at(words, s - 1, st)
        if v is not None:
            if v in (TEN1, TEN2):
                if s - 2 >= 0 and _at(words, s - 2, pg1) == KAZU:
                    score -= 5
            elif v in HAIHUN or v == KAKKO2 or v == BANGOU:
                score -= 2
            elif v == KAKKO1:
                if s - 2 >= 0 and _at(words, s - 2, pg1) == KAZU:
                    score -= 2
        if s - 2 >= 0 and _at(words, s - 2, st) == BANGOU:
            score -= 2
    if e + 1 < len(words):
        if (_at(words, e + 1, pg2) == JOSUUSHI
                or _at(words, e + 1, pg1) == FUKUSHIKANOU):
            score += 2
        v = _at(words, e + 1, st)
        if v is not None:
            if v in HAIHUN or v == KAKKO1 or v == BANGOU:
                score -= 2
            elif v == KAKKO2:
                if e + 2 < len(words) and _at(words, e + 2, pg1) == KAZU:
                    score -= 2
            elif v in (TEN1, TEN2):
                score += 4
    return score


def _non_numerical(run):
    """Read each digit, as an identifier rather than a quantity.

    0, 2 and 5 lengthen to ゼロ, ニー and ゴー -- the forms used when reading a
    number out so it cannot be misheard -- and the digits pair into accent
    phrases two at a time.

    A single digit is left exactly as it is.  Both of upstream's conversion
    functions open with that guard, and dropping it from this one lengthened
    the digit after a decimal point: 1.5 came out イッテンゴー where the
    reference gives イッテンゴ.
    """
    if len(run) <= 1:
        return run
    for k, w in enumerate(run):
        if w.string in ZERO:
            w.pron, w.mora_size = _C['ZERO_AFTER_DP'], 2
        elif w.string == TWO:
            w.pron, w.mora_size = _C['TWO_AFTER_DP'], 2
        elif w.string == FIVE:
            w.pron, w.mora_size = _C['FIVE_AFTER_DP'], 2
        w.chain_rule = STAR
        if k % 2 == 0:
            w.chain_flag = 0
        else:
            w.chain_flag = 1
            run[k - 1].acc = 3
    return run


def _numerical(run):
    """Place value: 十 百 千 inside each group of four, 万 億 兆 between them."""
    size = len(run)
    if size <= 1:
        return run
    index = size % 4 or 4
    place = (size - index) // 4 if size > index else 0
    index -= 1
    if place > 17:
        return run
    out = []
    have = 0
    for w in run:
        digit = get_digit(w)
        out.append(w)
        if index == 0:
            if digit == 0:
                w.pron, w.acc, w.mora_size = None, 0, 0
            else:
                have = 1
            if have == 1:
                if place > 0:
                    out.append(word_from_feature(_T['numeral_list3'][place]))
                have = 0
            place -= 1
        else:
            if digit <= 0:
                w.pron, w.acc, w.mora_size = None, 0, 0
            elif digit == 1:
                # 1.11 REPLACES the one with its place name -- 一十 is 十 -- where
                # 1.09 silenced the digit and inserted a new node after it.
                # The surviving word sequence is the same either way.
                nw = word_from_feature(_T['numeral_list2'][index])
                out[-1] = nw
                have = 1
            else:
                out.append(word_from_feature(_T['numeral_list2'][index]))
                have = 1
        index -= 1
        if index < 0:
            index = 3
    return out


def convert_sequence(words, s, e):
    """-> the replacement for words[s:e+1].

    1.11's recursive driver.  A run may hold digits, commas and periods, and
    it is cut into pieces that are each read as a quantity or as an
    identifier:

      * leading commas and periods are skipped
      * the run is split at the first period, and the part before it decides
        for itself
      * the commas in that part are checked against the thousands positions.
        Counting back from its last digit, a comma must sit at index 3, 7, 11
        and a thousands position must carry one; anything else means this is
        not a grouped number
      * a leading zero overrides all of that: it is an identifier
      * with no commas at all there is no evidence either way, and the
        context score decides, which is the only path 1.09 had
    """
    if s > e or s >= len(words):
        return []
    # skip head marks
    if is_comma(words[s].string) or is_period(words[s].string):
        if s != e:
            return [words[s]] + convert_sequence(words, s + 1, e)
        return [words[s]]

    # the last digit before any period, ignoring a trailing comma
    fd = s
    while fd < e and not is_period(words[fd + 1].string):
        fd += 1
    while fd > s and is_comma(words[fd].string):
        fd -= 1

    numerical = 1               # 1 yes, 0 unknown, -1 no
    n_comma = 0
    first_comma = None
    rindex = 0
    k = fd
    while True:
        if is_comma(words[k].string):
            first_comma = k
            n_comma += 1
            if numerical == 1 and rindex % 4 != 3:
                numerical = 0
        elif numerical == 1 and rindex % 4 == 3:
            numerical = 0
        if k == s:
            break
        k -= 1
        rindex += 1

    if s != fd and get_digit(words[s]) == 0:
        numerical = -1          # a leading zero is an identifier
    if numerical == 1 and n_comma == 0:
        numerical = 0           # no commas, so no evidence either way

    if numerical == 1:
        run = [w for w in words[s:fd + 1] if not is_comma(w.string)]
        out = _numerical(run)
        if fd != e:
            out += convert_sequence(words, fd + 1, e)
        return out

    last = fd if first_comma is None else first_comma - 1
    if numerical == 0:
        numerical = 1 if sequence_score(words, s, last) >= 0 else -1
    run = words[s:last + 1]
    out = _numerical(run) if numerical == 1 else _non_numerical(run)
    if last != e:
        out += convert_sequence(words, last + 1, e)
    return out


def in_class(w, name):
    return w.string != STAR and w.string in _T[name]


def convert_digit_pron(table, w):
    """Change the DIGIT's reading before a counter -- 四 as ヨ, 七 as シチ.

    Each row is four strings: the digit, its new reading, its accent and its
    mora count.
    """
    if w.string == STAR:
        return
    t = _T[table]
    for i in range(0, len(t) - 3, 4):
        if t[i] == w.string:
            w.pron = t[i + 1]
            w.acc = int(t[i + 2]) if t[i + 2].lstrip(u'-').isdigit() else 0
            w.mora_size = int(t[i + 3]) if t[i + 3].isdigit() else 0
            return


def _replace_head_mora(pron, pairs):
    """Swap the first mora for its voiced or semi-voiced partner."""
    for i in range(0, len(pairs) - 1, 2):
        if pron.startswith(pairs[i]):
            return pairs[i + 1] + pron[len(pairs[i]):]
    return pron


def convert_numerative_pron(table, prev, w):
    """Voice or semi-voice the COUNTER after a digit: 一本 is イッポン.

    The table is pairs of (digit, type).  Type 1 voices the counter's first
    mora -- 三本 ホン -> ボン -- and type 2 semi-voices it: 一本 ホン -> ポン.
    """
    if prev.string == STAR:
        return
    t = _T[table]
    kind = 0
    for i in range(0, len(t) - 1, 2):
        if t[i] == prev.string:
            kind = int(t[i + 1]) if t[i + 1].isdigit() else 0
            break
    if kind == 1:
        w.pron = _replace_head_mora(w.pron, _T['voiced_sound_symbol_list'])
    elif kind == 2:
        w.pron = _replace_head_mora(w.pron,
                                    _T['semivoiced_sound_symbol_list'])


def set_digit(words):
    """The driver.  -> a new word list."""
    # Pass 1: expand every run of digits.
    found = False

    def in_run(w):
        # 1.11 takes a comma or a period INTO the run when the dictionary tags
        # it 数, which is what lets `1,250` and `1.5` be seen whole.
        return (get_digit(w, convert=True) >= 0
                or (w.pos[1] == KAZU
                    and (is_period(w.string) or is_comma(w.string))))

    out = []
    i = 0
    n = len(words)
    while i < n:
        if words[i].pos[1] == KAZU:
            found = True
        if in_run(words[i]):
            j = i
            while j + 1 < n and in_run(words[j + 1]):
                j += 1
            out.extend(convert_sequence(words, i, j))
            i = j + 1
            continue
        out.append(words[i])
        i += 1
    if not found:
        return words
    # the expansion silences the digits that go unsaid
    out = [w for w in out if w.pron]

    # Pass 2: a decimal point between two digits becomes テン, and the digit
    # before it lengthens if it is 0, 2 or 5.
    k = 1
    while k < len(out) - 1:
        w = out[k]
        if (w.string != STAR and out[k - 1].string != STAR
                and is_period(w.string)
                and out[k - 1].pos[1] == KAZU and out[k + 1].pos[1] == KAZU):
            nw = word_from_feature(_C['TEN_FEATURE'])
            nw.chain_flag = 1
            out[k] = nw
            p = out[k - 1]
            if p.string in ZERO:
                p.pron, p.mora_size = _C['ZERO_BEFORE_DP'], 2
            elif p.string == TWO:
                p.pron, p.mora_size = _C['TWO_BEFORE_DP'], 2
            elif p.string == FIVE:
                p.pron, p.mora_size = _C['FIVE_BEFORE_DP'], 2
            elif SIX is not None and p.string == SIX:
                p.acc = 1                      # new in 1.11
            # and skip past the digits after the point, so a second period in
            # the same number is not treated as another decimal point
            k += 1
            while k < len(out) and out[k].pos[0] == MEISHI:
                k += 1
            k += 1
        else:
            k += 1

    # Pass 3: the counter readings, and the accent phrase around them.
    for k in range(1, len(out)):
        w, p = out[k], out[k - 1]
        if p.pos[1] != KAZU:
            continue
        if w.pos[2] == JOSUUSHI or w.pos[1] == FUKUSHIKANOU:
            for cls, tbl in DIGIT_CLASSES:
                if in_class(w, cls):
                    convert_digit_pron(tbl, p)
                    break
            for cls, tbl in NUMERATIVE_CLASSES:
                if in_class(w, cls):
                    convert_numerative_pron(tbl, p, w)
                    break
            p.chain_flag = 0
            w.chain_flag = 1

    # Pass 4, and it has to be a SECOND pass rather than an else-branch of the
    # one above.  Upstream runs these as two loops, so the digit-adjacency
    # rule is applied after every counter has had its say and overrides it:
    # in 1250円 the counter pass sets 十 to 0 because 円 follows it, and then
    # this pass sets 十 back to 1 because 五 precedes it.  Folded into one
    # loop the order reverses, because the 五/十 pair is reached before the
    # 十/円 pair, and 十 ends up on the wrong side of a phrase boundary.
    for k in range(1, len(out)):
        w, p = out[k], out[k - 1]
        if p.pos[1] != KAZU:
            continue
        if w.pos[1] == KAZU and p.string != STAR and w.string != STAR:
            # two adjacent numbers: 十五 chains, 五十 does not
            l4, l5 = _T['numeral_list4'], _T['numeral_list5']
            if p.string in l4:
                if w.string in l5:
                    p.chain_flag = 0
                    w.chain_flag = 1
            elif p.string in l5:
                if w.string in l4:
                    w.chain_flag = 0
        if in_class(w, 'numeral_list8'):
            convert_digit_pron('numeral_list9', p)
        if in_class(w, 'numeral_list10'):
            convert_digit_pron('numeral_list11', p)
        if in_class(w, 'numeral_list6'):
            convert_numerative_pron('numeral_list7', p, w)
    return set_counters(out)


def _load_over(w, feature):
    """Replace a word in place from a feature string, as NJDNode_load does."""
    nw = word_from_feature(feature)
    w.string, w.pos, w.pron, w.read = nw.string, nw.pos, nw.pron, nw.read
    w.acc, w.mora_size, w.chain_rule = nw.acc, nw.mora_size, nw.chain_rule
    if nw.chain_flag != -1:
        w.chain_flag = nw.chain_flag


def _pair_table(table, digit, counter):
    """(digit, feature) pairs: replace the digit and silence the counter."""
    t = _T[table]
    for i in range(0, len(t) - 1, 2):
        if t[i] == digit.string:
            _load_over(digit, t[i + 1])
            counter.pron = None
            return True
    return False


def _class3(digit, counter):
    """The native numerals: 一粒 is ヒトツブ, 二口 フタクチ, 三切れ ミキレ.

    Sixty-two counters take 一 二 三 in their native readings rather than the
    Sino-Japanese ones, and the table that lists them keys on the counter's
    SURFACE AND ITS READING together -- 夜 appears twice, once as ヤ and once
    as ヨ, and 重ね twice, as カサネ and as its voiced ガサネ.  The reading has
    to be the dictionary's own `read` field and not the pronunciation: 通り
    reads トオリ and is pronounced トーリ, so keying on the pronunciation would
    miss 一通り, and the voicing passes that run before this one rewrite the
    pronunciation while leaving the reading alone.

    Carrying that field is what format version 4 of jadic.bin is for.
    """
    t = _T['numerative_class3']
    for i in range(0, len(t) - 1, 2):
        if counter.string == t[i] and counter.read == t[i + 1]:
            c = _T['conv_table3']
            for j in range(0, len(c) - 3, 4):
                if digit.string == c[j]:
                    digit.read = digit.pron = c[j + 1]
                    digit.acc = int(c[j + 2])
                    digit.mora_size = int(c[j + 3])
                    return True
            return False
    return False


def set_counters(words):
    """The named exceptions: native numerals, people, and days of the month.

    一粒 is ヒトツブ, 一人 is ヒトリ and not イチニン, 二日 is フツカ, 十日 is
    トーカ, and 一日 is ツイタチ after a month and イチニチ otherwise.  The
    date and person tables replace the digit and silence the counter, so two
    words become one -- which is why a missing one showed up as a word-count
    difference against the reference rather than as a wrong reading.  The
    native-numeral table only rewrites the digit.
    """
    n = len(words)
    for k in range(n):
        w = words[k]
        nx = words[k + 1] if k + 1 < n else None
        pv = words[k - 1] if k > 0 else None
        if nx is None or nx.string == STAR or w.pos[1] != KAZU:
            continue
        # 1.11 adds the 記号 arm: a digit after a symbol still counts as the
        # first of its run, so 2日 works at the start of a bracketed date.
        if not (pv is None or pv.pos[0] == KIGOU or pv.pos[1] != KAZU):
            continue
        if not (nx.pos[2] == JOSUUSHI or nx.pos[1] == FUKUSHIKANOU):
            continue
        _class3(w, nx)
        if nx.string == NIN:
            _pair_table('conv_table4', w, nx)
        elif nx.string == NICHI and w.string != STAR:
            if pv is not None and GATSU in pv.string and w.string == ONE:
                _load_over(w, _C['TSUITACHI'])
                nx.pron = None
            else:
                _pair_table('conv_table5', w, nx)
        elif nx.string == NICHIKAN:
            _pair_table('conv_table6', w, nx)

    # 十四日 and 二十日 span three or four words, so they are their own pass.
    # 二十日 is ハツカ, not ニジューニチ, and 二十四日 splits as 二十 plus
    # 四日 rather than collapsing, which is why the four-word arm exists.
    for k in range(len(words)):
        w = words[k]
        pv = words[k - 1] if k > 0 else None
        if pv is not None and pv.pos[1] == KAZU:
            continue
        if k + 2 >= len(words):
            continue
        a, b = words[k + 1], words[k + 2]
        c = words[k + 3] if k + 3 < len(words) else None
        if w.string == TEN and a.string == FOUR:
            if b.string == NICHI:
                _load_over(w, _C['JUYOKKA'])
                a.pron = b.pron = None
            elif b.string == NICHIKAN:
                _load_over(w, _C['JUYOKKAKAN'])
                a.pron = b.pron = None
        elif w.string == TWO and a.string == TEN:
            if b.string == NICHI:
                _load_over(w, _HATSUKA)
                a.pron = b.pron = None
            elif b.string == NICHIKAN:
                _load_over(w, _C['HATSUKAKAN'])
                a.pron = b.pron = None
            elif b.string == FOUR and c is not None:
                if c.string == NICHI:
                    _load_over(w, _C['NIJU'])
                    _load_over(a, _YOKKA)
                    b.pron = c.pron = None
                elif c.string == NICHIKAN:
                    _load_over(w, _C['NIJU'])
                    _load_over(a, _C['YOKKAKAN'])
                    b.pron = c.pron = None
    return [w for w in words if w.pron]
