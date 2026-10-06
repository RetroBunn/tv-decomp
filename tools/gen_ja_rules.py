# -*- coding: utf-8 -*-
"""Extract Open JTalk's rule tables verbatim into a Python module.

    python tools/gen_ja_rules.py      # -> build/Japanese_test/ojt_tables.py

Several of Open JTalk's rule files are data rather than logic, and every one
that was transcribed by hand came out with an error in it:

  text2mecab_conv_list        184 character substitutions applied BEFORE
                              analysis.  The direction matters and was got
                              backwards: Open JTalk maps ASCII *to* fullwidth,
                              because that is how naist-jdic stores digits and
                              Latin letters.  `１` is in the dictionary reading
                              イチ; `1` is not in it at all.  Converting the
                              other way is why `1250円` came out as a pause.

  njd_set_unvoiced_vowel_mora_list   the 159 morae the devoicing stage knows.
                              This is the authoritative inventory for splitting
                              a katakana reading into morae, and deriving one
                              instead means guessing which small kana attach.

  njd_set_digit_rule_*        fifty tables -- eleven numeral lists, then a
                              class list and a conversion table per counter
                              group -- plus 44 string constants.  Two of the
                              constants are spelled NJD_SET_DITIT_ upstream,
                              a typo, and a prefix match on DIGIT alone drops
                              the 二十日 and 四日 rules silently.

  njd_set_pronunciation_list  369 rows of (string, reading, morae): how a word
                              the dictionary has no reading for gets one.  It
                              covers far more than kana -- the fullwidth Latin
                              letters carry their Japanese names, so ＹＴＬ
                              reads ワイティーエル.

  njd_set_accent_type_*       the 19 place and digit names its digit branch
                              tests, which decide the accent of 三十, 四百,
                              何万.

  the remaining stages' own     every part-of-speech name the eighteen
  constants, and the devoicing  accent-phrase rules test, the kana of the
  stage's three candidate       masu/desu and shi look-aheads, and the three
  classes                       candidate classes with the following-mora
                                list each one devoices before.  These were
                                typed out on the Python side and the three
                                classes were re-derived as a row map; now
                                both front ends read the tables.

So they are extracted rather than copied out.  Re-running this is how the
result is checked, and a version bump upstream shows up here as a diff.

BOTH front ends read this.  build/Japanese_test/*.py imports it directly and
tools/gen_ja_ojt.py turns it into lang/jpn/port/ja_ojt.c, so the C and the Python
cannot be reading different tables -- and there is no longer a Japanese string
literal typed by hand in either of the five rule stages.

AND C COMMENTS ARE STRIPPED FIRST.  Upstream disables five of its own table
rows by commenting them out; a scan for quoted strings takes them all back,
and because each disabled row is a whole row the alignment still looks right.
See _strip_comments -- that is how 三粒 came out ミツブ.

These tables are Open JTalk's data, under its BSD 3-clause licence; see
NOTICE.  No Open JTalk code is reproduced.
"""
import argparse, io, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# The official 1.11 release; see the note in tools/gen_ja_dict.py for why the
# release matters.  Of the tables taken here, text2mecab's conversion list and
# the mora inventory are byte-identical between 1.09 and 1.11; the digit
# tables are not, and 1.11 disables five of their rows.
SRC = os.path.join(ROOT, 'open_jtalk-1.11')
OUT = os.path.join(ROOT, 'build', 'Japanese_test', 'ojt_tables.py')


def _strip_comments(s):
    """Remove C comments, leaving string literals alone.

    This is not cosmetic.  Open JTalk's digit header DISABLES five of its own
    table rows by commenting them out and marking them `/* modified */`:

        /* "三", "ミ", "1", "1", *//* modified */     conv_table3
        /* "四", "ヨッ", "1", "2", *//* modified */   conv_table2f
        /* "羽", "把", *//* modified */               twice
        "校", "港", /* "行", */ "項",                 numerative_class1c1

    A plain scan for quoted strings takes all five back, and because each
    disabled row is a whole row the table alignment still looks right -- so
    the result is a table with rows upstream removed on purpose, and no error
    anywhere.  That is how 三瓶 came out サンベ instead of ミ: conv_table3 has
    two rows in 1.11, not three.

    The headers also annotate live rows with readings -- `"年" /* ねん */` --
    and those comments have to go for the same reason.
    """
    out = []
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith('/*', i):
            end = s.find('*/', i + 2)
            i = n if end < 0 else end + 2
            out.append(' ')
        elif s.startswith('//', i):
            end = s.find('\n', i)
            i = n if end < 0 else end
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def _unescape(t):
    if '\\' in t:
        # the ascii-for-utf-8 variants spell bytes as \xNN
        return bytes(t, 'ascii').decode('unicode_escape') \
            .encode('latin1').decode('utf-8')
    return t


def tokens(path, name):
    """Every string literal in a named C array, in order."""
    s = _strip_comments(io.open(path, encoding='utf-8').read())
    m = re.search(re.escape(name) + r'\[\] = \{(.*?)\n\};', s, re.S)
    if m is None:
        raise SystemExit('%s not found in %s' % (name, path))
    return [_unescape(t)
            for t in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))]


def all_arrays(path, prefix):
    """-> {name without the prefix: [strings]} for every array in the file.

    The digit stage has fifty of these -- eleven numeral lists, then a
    class/conversion pair for each counter group -- and they are pure data.
    Retyping fifty tables of katakana is a transcription error waiting to
    happen, so they are read out of the header instead.
    """
    s = _strip_comments(io.open(path, encoding='utf-8').read())
    out = {}
    for m in re.finditer(r'\*' + re.escape(prefix) +
                         r'(\w+)\[\] = \{(.*?)\n\};', s, re.S):
        out[m.group(1)] = [_unescape(t) for t in
                           re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))]
    return out


def defines(path, prefix):
    """-> {name without the prefix: value} for the single-string #defines."""
    s = _strip_comments(io.open(path, encoding='utf-8').read())
    out = {}
    for m in re.finditer(r'#define\s+' + re.escape(prefix) +
                         r'(\w+)\s+"((?:[^"\\]|\\.)*)"', s):
        out[m.group(1)] = _unescape(m.group(2))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--src', default=SRC)
    ap.add_argument('--out', default=OUT)
    args = ap.parse_args(argv)

    conv_h = os.path.join(args.src, 'text2mecab', 'text2mecab_rule_utf_8.h')
    mora_h = os.path.join(args.src, 'njd_set_unvoiced_vowel',
                          'njd_set_unvoiced_vowel_rule_utf_8.h')
    for p in (conv_h, mora_h):
        if not os.path.exists(p):
            print('missing %s' % p, file=sys.stderr)
            print('open_jtalk-1.11 is a reference checkout; see NOTICE.',
                  file=sys.stderr)
            return 2

    digit_h = os.path.join(args.src, 'njd_set_digit',
                           'njd_set_digit_rule_utf_8.h')
    if not os.path.exists(digit_h):
        print('missing %s' % digit_h, file=sys.stderr)
        return 2
    conv = tokens(conv_h, 'text2mecab_conv_list')
    mora = tokens(mora_h, 'njd_set_unvoiced_vowel_mora_list')
    dig = all_arrays(digit_h, 'njd_set_digit_rule_')
    dfn = defines(digit_h, 'NJD_SET_DIGIT_')
    # Upstream spells two of these NJD_SET_DITIT_ -- a typo in the release --
    # and a prefix match on DIGIT alone silently drops the 二十日 and 四日
    # features, so the rules that need them never fire.  Both spellings are
    # taken, and if the typo is ever corrected this still finds them.
    dfn.update(defines(digit_h, 'NJD_SET_DITIT_'))
    # (string, reading, mora count) x 369.  This is how a word the dictionary
    # has no reading for gets one, and it covers far more than kana: the
    # fullwidth Latin letters carry their Japanese names, so ＹＴＬ reads
    # ワイティーエル.  Deriving a reading instead of reading this table is why
    # it came out as a pause.
    pron_h = os.path.join(args.src, 'njd_set_pronunciation',
                          'njd_set_pronunciation_rule_utf_8.h')
    if not os.path.exists(pron_h):
        print('missing %s' % pron_h, file=sys.stderr)
        return 2
    pl = tokens(pron_h, 'njd_set_pronunciation_list')
    # njd_set_accent_type's own constants: the place and digit names its digit
    # branch tests, which decides the accent of 三十, 四百, 何万.
    at_h = os.path.join(args.src, 'njd_set_accent_type',
                        'njd_set_accent_type_rule_utf_8.h')
    if not os.path.exists(at_h):
        print('missing %s' % at_h, file=sys.stderr)
        return 2
    atc = defines(at_h, 'NJD_SET_ACCENT_TYPE_')
    # The part-of-speech names and the kana the remaining two stages test, and
    # the devoicing stage's three candidate classes with the following-mora
    # rows each one devoices before.  These were hand-typed on the Python side;
    # the C side reads them from here, so a character cannot be mistyped into
    # one front end and not the other.
    ap_h = os.path.join(args.src, 'njd_set_accent_phrase',
                        'njd_set_accent_phrase_rule_utf_8.h')
    uv_h = os.path.join(args.src, 'njd_set_unvoiced_vowel',
                        'njd_set_unvoiced_vowel_rule_utf_8.h')
    for p in (ap_h, uv_h):
        if not os.path.exists(p):
            print('missing %s' % p, file=sys.stderr)
            return 2
    apc = defines(ap_h, 'NJD_SET_ACCENT_PHRASE_')
    uvc = defines(uv_h, 'NJD_SET_UNVOICED_VOWEL_')
    prc = defines(pron_h, 'NJD_SET_PRONUNCIATION_')
    uvt = all_arrays(uv_h, 'njd_set_unvoiced_vowel_')
    uvt = dict((k, v) for k, v in uvt.items() if k != 'mora_list')
    if len(conv) % 2:
        conv = conv[:-1]
    pairs = [(conv[i], conv[i + 1]) for i in range(0, len(conv), 2)]
    # longest first, so a halfwidth kana plus its voicing mark is matched as
    # one substitution rather than as the bare kana
    pairs.sort(key=lambda p: -len(p[0]))

    with io.open(args.out, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(u'# -*- coding: utf-8 -*-\n')
        fh.write(u'"""Generated by tools/gen_ja_rules.py -- do not edit.\n\n'
                 u'Two Open JTalk tables, extracted verbatim.  CONV is\n'
                 u'text2mecab\'s substitution list, longest source first, and\n'
                 u'it maps ASCII and halfwidth kana TO the fullwidth and\n'
                 u'composed forms naist-jdic is keyed by.  MORA is the mora\n'
                 u'inventory njd_set_unvoiced_vowel splits readings with.\n\n'
                 u'Open JTalk is BSD 3-clause; see NOTICE.\n"""\n\n')
        fh.write(u'CONV = [\n')
        for a, b in pairs:
            fh.write(u'    (%r, %r),\n' % (a, b))
        fh.write(u']\n\n')
        # The ORDER is data too.  Upstream walks the list and takes the FIRST
        # prefix match, and the list puts each base's two-character
        # combinations before the bare one, so first match is longest match.
        # The Python front end wants the set and the C wants the order, and
        # the order cannot be recovered from the set.
        fh.write(u'MORA_LIST = [\n')
        for m in mora:
            fh.write(u'    %r,\n' % m)
        fh.write(u']\n\nMORA = frozenset(MORA_LIST)\n')
        fh.write(u'\nMORA_MAX = %d\n' % max(len(m) for m in mora))
        fh.write(u"\n# njd_set_digit's fifty tables -- eleven numeral lists,"
                 u"\n# then a class list and a conversion table for each"
                 u"\n# counter group -- and its string constants.  All data.\n")
        fh.write(u'DIGIT = {\n')
        for k in sorted(dig):
            fh.write(u'    %r: %r,\n' % (k, dig[k]))
        fh.write(u'}\n\nDIGIT_CONST = {\n')
        for k in sorted(dfn):
            fh.write(u'    %r: %r,\n' % (k, dfn[k]))
        fh.write(u'}\n')
        fh.write(u'\n# njd_set_pronunciation_list: how a word the dictionary\n'
                 u'# has no reading for gets one.  (string, reading, morae),\n'
                 u'# and the FIRST match in this order wins, which is how\n'
                 u'# upstream walks it.\n'
                 u'PRON_LIST = [\n')
        for i in range(0, len(pl) - 2, 3):
            fh.write(u'    (%r, %r, %s),\n' % (pl[i], pl[i + 1], pl[i + 2]))
        fh.write(u']\n')
        fh.write(u'\n# njd_set_accent_type constants, for its digit branch --\n'
                 u'# the accent of 三十, 四百, 何万.\nACCENT_CONST = {\n')
        for k in sorted(atc):
            fh.write(u'    %r: %r,\n' % (k, atc[k]))
        fh.write(u'}\n')
        for name, dd, note in (
                ('ACCENT_PHRASE_CONST', apc,
                 u'the part-of-speech names the eighteen accent-phrase rules\n'
                 u'# test, plus the two particles rule 11 names'),
                ('UNVOICED_CONST', uvc,
                 u'what the devoicing stage tests: four parts of speech, the\n'
                 u'# two whole-word symbols, the devoicing mark itself, and\n'
                 u'# the kana of the masu/desu and shi look-aheads'),
                ('PRONUNCIATION_CONST', prc,
                 u'njd_set_pronunciation\'s own constants')):
            fh.write(u'\n# %s\n%s = {\n' % (note, name))
            for k in sorted(dd):
                fh.write(u'    %r: %r,\n' % (k, dd[k]))
            fh.write(u'}\n')
        fh.write(u'\n# The three devoicing candidate classes, each with the\n'
                 u'# following-mora rows it devoices before.  The exclusions\n'
                 u'# are the point: /su/ does not devoice before another\n'
                 u'# s-row mora and /fu hi fi/ not before an h-row one,\n'
                 u'# because that would leave two fricatives with nothing\n'
                 u'# between them.\nUNVOICED = {\n')
        for k in sorted(uvt):
            fh.write(u'    %r: %r,\n' % (k, uvt[k]))
        fh.write(u'}\n')
    print('%s' % os.path.relpath(args.out, ROOT))
    print('  %d pronunciation rows' % (len(pl) // 3))
    print('  %d substitutions, %d morae, longest mora %d characters'
          % (len(pairs), len(mora), max(len(m) for m in mora)))
    print('  %d digit tables, %d digit constants' % (len(dig), len(dfn)))
    print('  from %s' % os.path.relpath(args.src, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
