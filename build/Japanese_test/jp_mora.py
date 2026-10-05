# -*- coding: utf-8 -*-
"""Kana or romaji in, morae out, then morae to phonemes.

A mora is the unit Japanese times by, so the pipeline counts in morae rather
than syllables: kya is one mora, kyaa is two, and the three special morae -- N
(the moraic nasal), Q (the first half of a geminate) and the long-vowel mark --
each take a mora of their own.
"""
import re, unicodedata

KANA = {
 'あ':'a','い':'i','う':'u','え':'e','お':'o',
 'か':'ka','き':'ki','く':'ku','け':'ke','こ':'ko',
 'が':'ga','ぎ':'gi','ぐ':'gu','げ':'ge','ご':'go',
 'さ':'sa','し':'shi','す':'su','せ':'se','そ':'so',
 'ざ':'za','じ':'ji','ず':'zu','ぜ':'ze','ぞ':'zo',
 'た':'ta','ち':'chi','つ':'tsu','て':'te','と':'to',
 'だ':'da','ぢ':'ji','づ':'zu','で':'de','ど':'do',
 'な':'na','に':'ni','ぬ':'nu','ね':'ne','の':'no',
 'は':'ha','ひ':'hi','ふ':'fu','へ':'he','ほ':'ho',
 # Katakana VU, written as a voiced U.  It is a 20th-century invention for
 # transcribing foreign /v/ and most speakers render it as /b/, which is what
 # this does -- so does the engine, since /v/ has no MANNER row of its own.
 # It was absent from this table altogether, so the character was silently
 # DROPPED: violin came back as 'iorin'.
 'ゔ':'bu',
 'ば':'ba','び':'bi','ぶ':'bu','べ':'be','ぼ':'bo',
 'ぱ':'pa','ぴ':'pi','ぷ':'pu','ぺ':'pe','ぽ':'po',
 'ま':'ma','み':'mi','む':'mu','め':'me','も':'mo',
 'や':'ya','ゆ':'yu','よ':'yo',
 'ら':'ra','り':'ri','る':'ru','れ':'re','ろ':'ro',
 'わ':'wa','を':'o','ん':'n','っ':'q','ー':'-',
}
YOON = {'ゃ':'ya','ゅ':'yu','ょ':'yo'}          # small ya/yu/yo
SMALLV = {'ぁ':'a','ぃ':'i','ぅ':'u','ぇ':'e','ぉ':'o'}

def _kata_to_hira(s):
    return "".join(chr(ord(c) - 0x60) if 'ァ' <= c <= 'ヶ' else c for c in s)

def kana_to_romaji(s):
    """Kana string -> a romaji string this module's parser understands."""
    s = _kata_to_hira(unicodedata.normalize('NFKC', s))
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        nxt = s[i+1] if i+1 < len(s) else ''
        if c in KANA:
            r = KANA[c]
            if nxt in YOON and len(r) >= 2:
                # ki + ya -> kya, shi + ya -> sha, chi + ya -> cha, ji + ya -> ja
                base = r[:-1]
                v = YOON[nxt][-1]
                if base in ('sh', 'ch', 'j'):
                    r = base + v
                else:
                    r = base + 'y' + v
                i += 1
            elif nxt in SMALLV and r in ('fu', 'u', 'te', 'de', 'tsu', 'bu'):
                r = r[:-1] + SMALLV[nxt]       # fa, fi, ti, di, tsa ...
                i += 1
            elif r == 'n' and KANA.get(nxt, '')[:1] in tuple('aiueoy'):
                # The moraic nasal before a vowel or a y-mora.  This function
                # flattens kana to a romaji STRING and the parser then
                # re-tokenises it, so an unmarked `n` there is swallowed by the
                # vowel after it and the mora boundary is gone: /re N a i/ came
                # back as ['re', 'na', 'i'], and the word lost a mora as well as
                # its nasal.  Hepburn's apostrophe is exactly this problem and
                # the parser already understands it.
                r = "n'"
            out.append(r)
        elif c in ' 　':
            out.append(' ')
        i += 1
    return "".join(out)

# longest first, so "sh"/"ch"/"ts" and the y-series win over the bare consonant
_ONSETS = ['kya','kyu','kyo','gya','gyu','gyo','sha','shu','sho','shi',
           'ja','ju','jo','ji','cha','chu','cho','chi','tsu','nya','nyu','nyo',
           'hya','hyu','hyo','bya','byu','byo','pya','pyu','pyo',
           'mya','myu','myo','rya','ryu','ryo','fu']
_C = ['ky','gy','sh','ch','ts','ny','hy','by','py','my','ry',
      'k','g','s','z','t','d','n','h','b','p','m','y','r','w','f','j','v']

def romaji_to_morae(s, drops=None):
    """`drops`, when a one-element list is passed, receives the number of
    characters this could not place.

    The parser is LENIENT by design -- it is fed generated romaji from the
    kana path and must not reject a stray mark -- and that leniency is
    silent.  Fed an English word it drops the letters Japanese has no mora
    for, so `hello` came out /he.Q.o/ and `blorf` the single mora /o/.  That
    count is what tells deliberate romaji from a Latin word that merely
    survived the filter.
    """
    """-> list of morae, each 'CV' or 'V' or 'N' or 'Q' or ':' (long vowel)."""
    s = s.lower().replace('ō','oo').replace('ū','uu')
    s = re.sub(r"[^a-z\- '|]", '', s)
    out, i = [], 0
    while i < len(s):
        # Two levels of boundary above the mora.  A space is an ACCENT PHRASE
        # boundary: no pause, the articulation runs straight through, but the
        # pitch gets a fresh accent command and downstep chains across it.  A
        # bar is a MAJOR PHRASE boundary: a pause, a new Fujisaki phrase
        # command, and downstep resets.  Kawai's rules (1)-(6) decide where
        # these go from clause and ICRLB boundaries, which needs a parser we do
        # not have, so the symbols are exposed instead of being derived.
        if s[i] == '|':
            out.append('||' if s[i+1:i+2] == '|' else '|')
            i += 2 if s[i+1:i+2] == '|' else 1
            continue
        if s[i] == ' ':
            out.append(' '); i += 1; continue
        if s[i] == '-':                       # chouon
            out.append(':'); i += 1; continue
        if s[i] == "'":
            i += 1; continue                  # n'  -- just a reading aid
        if s[i] == 'q':                       # the kana path writes small tsu
            out.append('Q'); i += 1; continue
        # moraic nasal: n not followed by a vowel or y
        if s[i] == 'n' and (i+1 >= len(s) or s[i+1] not in 'aiueoy'):
            out.append('N'); i += 1; continue
        # geminate: a doubled consonant is Q + the consonant
        if (i+1 < len(s) and s[i] == s[i+1] and s[i] not in 'aiueon'):
            out.append('Q'); i += 1; continue
        hit = None
        for o in _ONSETS:
            if s.startswith(o, i): hit = o; break
        if hit:
            out.append(hit); i += len(hit); continue
        for c in _C:
            if s.startswith(c, i) and i+len(c) < len(s) and s[i+len(c)] in 'aiueo':
                out.append(c + s[i+len(c)]); i += len(c)+1; hit = c; break
        if hit: continue
        if s[i] in 'aiueo':
            prev = out[-1][-1] if (out and out[-1] and out[-1] not in ('N','Q',' ')) else ''
            # a repeated vowel is a long one; so, by convention, are "ou" and
            # "ei", which is how Japanese long o and long e are usually written
            if prev == s[i] or (prev == 'o' and s[i] == 'u')                             or (prev == 'e' and s[i] == 'i'):
                out.append(':')
            else:
                out.append(s[i])
            i += 1; continue
        i += 1
        if drops is not None:
            drops[0] += 1
    return out

def to_morae(text, drops=None):
    if any(ord(c) > 0x2000 for c in text):
        text = kana_to_romaji(text)
    return romaji_to_morae(text, drops)

def split(mora):
    """'kya' -> ('ky','a');  'a' -> ('','a');  'N'/'Q'/':'/' ' -> (mora, '')"""
    if mora in ('N', 'Q', ':', ' ', '|', '||'):
        return mora, ''
    if mora[-1] in 'aiueo':
        return mora[:-1], mora[-1]
    return mora, ''
