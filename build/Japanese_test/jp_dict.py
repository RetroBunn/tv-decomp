# -*- coding: utf-8 -*-
"""Japanese text -> words, readings, accent and phrase boundaries.

This is the stage the front end never had. Everything downstream of it --
morae, frames, the Fujisaki contour -- has been built and listened to; what it
was given was kana with no accent, because the thing that supplies kanji
readings and accent types is a morphological analyser over a dictionary, and
there wasn't one.

There is now. `lang/jpn/data/jadic.bin` (tools/gen_ja_dict.py) holds naist-jdic: for
every one of 486,757 entries a reading, an accent type, a mora count and an
accent chain rule, plus the 1377x1377 connection matrix that decides where one
word ends and the next begins.

The stages follow Open JTalk's, which exposes them individually and whose rule
modules are plain tests on morphological category -- `njd_set_accent_phrase` is
eighteen numbered rules over noun/verb/adjective/particle/suffix and nothing
else. They are read here as a specification and written out again; no code is
copied. Open JTalk and naist-jdic are both BSD 3-clause. See NOTICE.

    analyse(text)  -> [Word]            Viterbi over the dictionary
    njd(words)     -> [Word]            the NJD stages, in Open JTalk's order
    to_front(words)-> (morae, accents)  what jp_speak.build/pitch want

The accent type matters twice, which is the thing that makes the stages worth
having rather than just the readings: it is the nucleus the pitch contour falls
at, AND it blocks devoicing on the mora that carries it. Measured over all
486,646 pronounced naist-jdic entries, 11.2% of the 199,504 morae the old rule
devoiced were accent nuclei that should not have been.
"""
import io, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ojt_tables import (CONV as _CONV, MORA as _MORA,
                        MORA_MAX as _MORA_MAX,
                        PRON_LIST as _PRON_LIST,
                        ACCENT_PHRASE_CONST as _ACCENT_PHRASE_CONST,
                        UNVOICED_CONST as _UNVOICED_CONST,
                        PRONUNCIATION_CONST as _PRONUNCIATION_CONST)

ROOT = os.path.dirname(os.path.dirname(HERE))
DICT = os.path.join(ROOT, 'lang', 'jpn', 'data', 'jadic.bin')

MAGIC = b'OPENTVJ1'
ENTRY = struct.Struct('<IIIHHhHBBBBI')
COMP = struct.Struct('<IIIBBH')
UNK = struct.Struct('<HHhHBBBB')

# The part-of-speech strings and the kana the rules test against.
#
# EXTRACTED, not typed out.  Every one of these is a #define in Open JTalk's
# own rule headers, and tools/gen_ja_rules.py reads them into ojt_tables.py
# for the C front end -- so typing them here as well would be the exact drift
# that generator exists to prevent.  A mistyped character would be a rule
# that silently never fires.
#
# The three dictionaries overlap, because upstream defines 名詞 in one header
# and 動詞 in three; where they overlap they agree, and taking each name from
# the stage that uses it keeps the correspondence with upstream's source
# readable.
_AP = _ACCENT_PHRASE_CONST
_UV = _UNVOICED_CONST
_PR = _PRONUNCIATION_CONST

MEISHI = _AP['MEISHI']                        # noun
DOUSHI = _AP['DOUSHI']                        # verb
KEIYOUSHI = _AP['KEIYOUSHI']                  # adjective
FUKUSHI = _AP['FUKUSHI']                      # adverb
SETSUZOKUSHI = _AP['SETSUZOKUSHI']            # conjunction
RENTAISHI = _AP['RENTAISHI']                  # adnominal
JODOUSHI = _AP['JODOUSHI']                    # auxiliary verb
JOSHI = _AP['JOSHI']                          # particle
KANDOUSHI = _UV['KANDOUSHI']                  # interjection
KIGOU = _AP['KIGOU']                          # symbol
SETTOUSHI = _AP['SETTOUSHI']                  # prefix
FILLER = _PR['FILLER']
SETSUBI = _AP['SETSUBI']                      # suffix (pos_group1)
HIJIRITSU = _AP['HIJIRITSU']                  # dependent
FUKUSHI_KANOU = _AP['FUKUSHI_KANOU']
KEIYOUDOUSHI_GOKAN = _AP['KEIYOUDOUSHI_GOKAN']
SAHEN_SETSUZOKU = _AP['SAHEN_SETSUZOKU']
SETSUZOKUJOSHI = _AP['SETSUZOKUJOSHI']
RENYOU = _AP['RENYOU']
SEI = _AP['SEI']                              # surname (pos_group3)
MEI = _AP['MEI']                              # given name (pos_group3)
TE = _AP['TE']
DE = _AP['DE']
SHI = _UV['SHI']
MA = _UV['MA']
SU = _UV['SU']
CHOUON = _UV['CHOUON']
QUESTION = _UV['QUESTION']
TOUTEN = _UV['TOUTEN']


class Word(object):
    __slots__ = ('string', 'pos', 'pron', 'read', 'acc', 'mora_size',
                 'chain_rule', 'chain_flag')

    def __init__(self, string, pos, pron, acc, mora_size, chain_rule,
                 read=None):
        self.string = string
        self.pos = pos                 # the six fields, as a tuple
        self.pron = pron
        # The kana as written rather than as said.  Only njd_set_digit's
        # class3 table tests it, but it has to be the dictionary's own field:
        # the digit passes that run before class3 rewrite pron and leave read
        # alone, and 通り reads トオリ while it is pronounced トーリ.
        self.read = pron if read is None else read
        self.acc = acc
        self.mora_size = mora_size
        self.chain_rule = chain_rule
        self.chain_flag = -1           # -1 unset, 0 starts a phrase, 1 chains

    def __repr__(self):
        return '%s/%s[%s %d/%d %s]' % (self.string, self.pron, self.pos[0],
                                       self.acc, self.mora_size,
                                       self.chain_rule)


class Dict(object):
    """The compiled dictionary, read once and shared."""

    def __init__(self, path=DICT):
        with open(path, 'rb') as fp:
            self.b = fp.read()
        if self.b[:8] != MAGIC:
            raise ValueError('%s is not a compiled Japanese dictionary' % path)
        f = struct.unpack_from('<25I', self.b, 8)
        (self.version, self.n_entries, self.n_pos, self.n_chain, self.n_cat,
         self.n_unk, self.dim, self.n_comp, self.off_surf, self.len_surf,
         self.off_pron, self.len_pron, self.off_posp, self.len_posp,
         self.off_chainp, self.len_chainp, self.off_posi, self.off_chaini,
         self.off_entry, self.off_matrix, self.off_cat, self.off_cmap,
         self.off_unk, self.off_comp, _) = f
        if self.version < 4:
            raise ValueError('%s is a version %d dictionary; version 1 lost '
                             'the components of compound entries, 2 was built '
                             'from 1.09 data, and 3 carried no read field.  '
                             'Re-run tools/gen_ja_dict.py.'
                             % (path, self.version))
        self._pos = {}
        self._chain = {}

    # ---- string access ---------------------------------------------------

    def _cstr(self, base, off):
        e = self.b.index(b'\0', base + off)
        return self.b[base + off:e].decode('utf-8')

    def surface(self, off):
        return self._cstr(self.off_surf, off)

    def surface_bytes(self, off):
        e = self.b.index(b'\0', self.off_surf + off)
        return self.b[self.off_surf + off:e]

    def pron(self, off):
        return self._cstr(self.off_pron, off)

    def pos(self, pid):
        if pid not in self._pos:
            off = struct.unpack_from('<I', self.b, self.off_posi + pid * 4)[0]
            at = self.off_posp + off
            out = []
            for _ in range(6):
                e = self.b.index(b'\0', at)
                out.append(self.b[at:e].decode('utf-8'))
                at = e + 1
            self._pos[pid] = tuple(out)
        return self._pos[pid]

    def chain(self, cid):
        if cid not in self._chain:
            off = struct.unpack_from('<I', self.b,
                                     self.off_chaini + cid * 4)[0]
            self._chain[cid] = self._cstr(self.off_chainp, off)
        return self._chain[cid]

    def entry(self, i):
        return ENTRY.unpack_from(self.b, self.off_entry + i * ENTRY.size)

    def component(self, off):
        """-> (pron offset, surface offset, read offset, accent, mora count)

        The second and later parts of a compound entry.  Upstream's
        NJDNode_load splits every field on the colon and emits one node per
        part; this is where the parts after the first are kept.
        """
        c = COMP.unpack_from(self.b, self.off_comp + off * COMP.size)
        return c[0], c[1], c[2], c[3], c[4]

    def connect(self, right_id, left_id):
        return struct.unpack_from(
            '<h', self.b,
            self.off_matrix + ((right_id * self.dim) + left_id) * 2)[0]

    def category(self, cp):
        if cp > 0xffff:
            cp = 0x4e00          # outside the map, treat as kanji
        return self.b[self.off_cmap + cp]

    def cat_flags(self, c):
        at = self.off_cat + c * 4
        return self.b[at], self.b[at + 1], self.b[at + 2]  # invoke, group, len

    def unk_entries(self, c):
        out = []
        for i in range(self.n_unk):
            u = UNK.unpack_from(self.b, self.off_unk + i * UNK.size)
            if u[4] == c:
                out.append(u)
        return out

    # ---- lookup ----------------------------------------------------------

    def find(self, key):
        """-> (first, last) entry indices whose surface equals `key`, or None.

        Entries are sorted by surface bytes then cost, so this is one binary
        search for the run and the cheapest member is first in it.
        """
        lo, hi = 0, self.n_entries
        while lo < hi:
            mid = (lo + hi) // 2
            s = self.surface_bytes(self.entry(mid)[0])
            if s < key:
                lo = mid + 1
            else:
                hi = mid
        if lo >= self.n_entries or self.surface_bytes(self.entry(lo)[0]) != key:
            return None
        first = lo
        hi = self.n_entries
        lo = first
        while lo < hi:
            mid = (lo + hi) // 2
            s = self.surface_bytes(self.entry(mid)[0])
            if s <= key:
                lo = mid + 1
            else:
                hi = mid
        return first, lo


_dict = None


def load(path=DICT):
    global _dict
    if _dict is None:
        _dict = Dict(path)
    return _dict


# ---- text2mecab: the normalisation Open JTalk does before analysis --------

# text2mecab's 184 substitutions, applied before the analyser sees the text.
#
# THE DIRECTION IS ASCII TO FULLWIDTH, and this had it backwards.  naist-jdic
# is keyed by the fullwidth forms: `１` is an entry reading イチ and `1` is not
# in the dictionary at all, `ＡＢＣ` reads エービーシー and `ABC` is absent.
# Folding the other way meant every digit and Latin letter missed the
# dictionary, fell through to the unknown-word path with no derivable reading,
# and came out as a pause -- which is why `1250円` said only the 円.  The same
# list also composes halfwidth kana, so ｶﾞ becomes ガ.
#
# The table is extracted rather than retyped; see tools/gen_ja_rules.py.
def normalize(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        for src, dst in _CONV:
            if text.startswith(src, i):
                out.append(dst)
                i += len(src)
                break
        else:
            out.append(text[i])
            i += 1
    return u''.join(out)


# ---- the lattice and Viterbi ---------------------------------------------

class _Node(object):
    __slots__ = ('begin', 'end', 'left', 'right', 'wcost', 'entry', 'unk_pos',
                 'cost', 'prev')

    def __init__(self, begin, end, left, right, wcost, entry=None,
                 unk_pos=None):
        self.begin, self.end = begin, end
        self.left, self.right, self.wcost = left, right, wcost
        self.entry = entry
        self.unk_pos = unk_pos
        self.cost = 0
        self.prev = None


def _char_starts(b):
    """Byte offsets where a UTF-8 character begins, plus the end."""
    out = [i for i in range(len(b)) if (b[i] & 0xc0) != 0x80]
    out.append(len(b))
    return out


def _cp_at(b, i):
    c = b[i]
    if c < 0x80:
        return c
    n = 3 if c >= 0xf0 else 2 if c >= 0xe0 else 1
    u = c & (0x07 if n == 3 else 0x0f if n == 2 else 0x1f)
    for k in range(1, n + 1):
        if i + k >= len(b):
            return 0xfffd
        u = (u << 6) | (b[i + k] & 0x3f)
    return u


def analyse(text, d=None):
    """Viterbi over the dictionary.  -> [Word] in order."""
    d = d or load()
    b = normalize(text).encode('utf-8')
    if not b:
        return []
    starts = _char_starts(b)
    is_start = set(starts)
    # ends[p] holds the nodes that finish at byte p, each with its best cost
    ends = {0: [_Node(0, 0, 0, 0, 0)]}
    nodes_at = {}
    for p in starts[:-1]:
        if p not in ends:
            continue                      # unreachable position
        cand = []
        # known words
        for q in starts:
            if q <= p:
                continue
            r = d.find(bytes(b[p:q]))
            if r is None:
                # a longer key cannot match if no entry has this prefix, but
                # proving that needs a trie; the dictionary's longest surface
                # is 78 bytes, so the scan is bounded instead
                if q - p > 78:
                    break
                continue
            for i in range(r[0], r[1]):
                e = d.entry(i)
                cand.append(_Node(p, q, e[3], e[4], e[5], entry=i))
        # unknown words
        cat = d.category(_cp_at(b, p))
        invoke, group, length = d.cat_flags(cat)
        if invoke or not cand:
            run = p
            k = starts.index(p)
            while k + 1 < len(starts):
                nxt = starts[k + 1]
                if nxt >= len(b) or d.category(_cp_at(b, nxt)) != cat:
                    break
                k += 1
                run = nxt
            run_end = starts[starts.index(run) + 1]
            lens = []
            if group:
                lens.append(run_end)
            kk = starts.index(p)
            for L in range(1, length + 1):
                if kk + L < len(starts) and starts[kk + L] <= run_end:
                    lens.append(starts[kk + L])
            for q in sorted(set(lens)):
                if q <= p:
                    continue
                for u in d.unk_entries(cat):
                    cand.append(_Node(p, q, u[0], u[1], u[2],
                                      unk_pos=(u[3], b[p:q])))
        if not cand:
            # nothing at all can start here: skip one character so the rest of
            # the utterance is still analysed rather than lost
            nxt = starts[starts.index(p) + 1]
            ends.setdefault(nxt, []).extend(ends[p])
            continue
        for nd in cand:
            best, bprev = None, None
            for pv in ends[p]:
                c = pv.cost + d.connect(pv.right, nd.left)
                if best is None or c < best:
                    best, bprev = c, pv
            nd.cost = best + nd.wcost
            nd.prev = bprev
            ends.setdefault(nd.end, []).append(nd)
        nodes_at[p] = cand
    # EOS
    last = len(b)
    if last not in ends:
        return []
    best, bprev = None, None
    for pv in ends[last]:
        c = pv.cost + d.connect(pv.right, 0)
        if best is None or c < best:
            best, bprev = c, pv
    chain = []
    nd = bprev
    while nd is not None and nd.end > 0:
        chain.append(nd)
        nd = nd.prev
    chain.reverse()
    out = []
    for nd in chain:
        if nd.entry is not None:
            e = d.entry(nd.entry)
            pos, rule = d.pos(e[6]), d.chain(e[9])
            out.append(Word(d.surface(e[0]), pos, d.pron(e[1]),
                            e[7], e[8], rule, read=d.pron(e[2])))
            # A compound entry is more than one word.  Upstream's
            # NJDNode_load emits one node per component, sharing the part of
            # speech and the chain rule, and forces the chain flag of every
            # component after the first to 0 so it starts its own accent
            # phrase: ありがとうございます is アリガトー then ゴザイマス, two
            # phrases with accents 2 and 4, not one word with no accent.
            for k in range(1, e[10]):
                c = d.component(e[11] + k - 1)
                w = Word(d.surface(c[1]), pos, d.pron(c[0]), c[3], c[4], rule,
                         read=d.pron(c[2]))
                w.chain_flag = 0
                out.append(w)
        else:
            pid, raw = nd.unk_pos
            s = raw.decode('utf-8', 'replace')
            out.append(Word(s, d.pos(pid), _unknown_pron(s), 0, 0, u'*'))
    return out


def unknown_pron(s):
    """-> (reading, mora count) for a word the dictionary has no reading for.

    Open JTalk's own table does this, and it holds far more than kana: the
    fullwidth Latin letters carry their Japanese names, so ＹＴＬ reads
    ワイティーエル.  This used to derive a reading instead -- katakana through
    unchanged, hiragana shifted up, anything else refused -- which turned every
    Latin string into a pause.

    First match in the table's order wins, which is how upstream walks it, and
    a character nothing matches is skipped rather than failing the whole word.
    A word with no matches at all comes back empty: kanji has no reading
    derivable from its shape, and guessing one would put a wrong word in the
    listener's ear, which is worse than a gap.
    """
    out = []
    morae = 0
    i = 0
    n = len(s)
    while i < n:
        for src, read, nm in _PRON_LIST:
            if s.startswith(src, i):
                out.append(read)
                morae += nm
                i += len(src)
                break
        else:
            i += 1
    return u''.join(out), morae


def _unknown_pron(s):
    """The reading alone, for callers that do not need the count."""
    return unknown_pron(s)[0]
