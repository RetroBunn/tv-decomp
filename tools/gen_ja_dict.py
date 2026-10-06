# -*- coding: utf-8 -*-
"""Compile the naist-jdic dictionary into one file both front ends can read.

    python tools/gen_ja_dict.py            # -> data/ja/jadic.bin

WHY A COMPILER AND NOT THE CSV.  naist-jdic ships as 53 MB of CSV plus a 26 MB
text connection matrix, and the Japanese front end has to look words up while
speaking.  This turns both into one little-endian file with the strings pooled,
the part-of-speech tuples interned, and the entries sorted so a surface form is
found by binary search.  The Python reference in build/Japanese_test/jp_dict.py
and the C in lang/jpn/port/ja_dict.c both read THIS file, which is the point: if they
read different data, a disagreement between them would mean nothing.

WHAT IT IS FOR.  Accurate pronunciation.  Every field carried here is one the
synthesiser already has a use for and has been guessing at:

    pron        the reading.  Without it the front end speaks the kana in the
                text and silently drops the kanji, which is most of Japanese.
    read        the OTHER reading -- the kana as written rather than as said,
                so 東京 reads トウキョウ and is pronounced トーキョー.  They
                differ for 18.85% of entries, and njd_set_digit's class3 table
                keys on read: 一粒 is ヒトツブ because 粒 has read ツブ, and
                一通り is ヒトトオリ because 通り has read トオリ -- whose pron
                is トーリ and would not match.  The earlier digit passes
                rewrite pron and leave read alone, which is the whole reason
                the test uses it.
    acc         the accent type -- which mora carries the nucleus.  ja_pitch
                takes exactly this integer and has always been handed 0.
    mora_size   how long the word is in morae, which is what the chain rules
                compose a compound's accent from.
    chain_rule  how this word's accent combines with the one before it.
    pos         six morphological fields.  njd_set_accent_phrase decides every
                accent-phrase boundary from these, and njd_set_unvoiced_vowel
                needs them for the /masu/, /desu/ and /shi/ rules.

LICENCE.  naist-jdic is BSD 3-clause, from the Nara Institute of Science and
Technology; Open JTalk, whose front end this follows, is BSD 3-clause from the
Nagoya Institute of Technology.  Both permit redistribution in source and
binary form with the notice retained, which is why the compiled dictionary can
live under data/ -- the same arrangement the Centigram tables already have.
See NOTICE.  No Open JTalk code is copied; the rule modules are read as a
specification and implemented separately.

FORMAT, all little-endian.  Offsets are from the start of the file.

    magic       8   "OPENTVJ1"
    version     u32
    n_entries   u32   dictionary entries
    n_pos       u32   distinct part-of-speech tuples
    n_chain     u32   distinct accent chain rules
    n_cat       u32   character categories from char.def
    n_unk       u32   unknown-word entries from unk.def
    dim         u32   connection matrix side
    off/len     u32 u32   x5: surface pool, pron pool, pos pool, chain pool,
                          and the two index arrays follow their pools
    off_entry   u32   n_entries * 20 bytes
    off_matrix  u32   dim*dim int16
    off_cat     u32   n_cat * 4 bytes: invoke, group, length, pad
    off_cmap    u32   0x10000 bytes: code point -> category id
    off_unk     u32   n_unk * 12 bytes

    off_comp    u32   n_comp * 12 bytes, the extra components of a compound

An entry is 28 bytes: surface offset u32, pron offset u32, read offset u32,
left id u16, right id u16, cost i16, pos id u16, accent u8, mora count u8,
chain id u8, component count u8, and the offset of its second component u32.
Entries are sorted by surface bytes and then by cost, so a binary search finds
the run for a surface form and the cheapest is first in it.

The read offset points into the SAME pool as the pron offset, so the 81.15% of
entries whose two readings agree cost nothing but the offset; only 49,852
distinct strings are added by carrying the field.

COMPOUND ENTRIES.  333 entries describe more than one word, with the parts
separated by colons -- `ありがとうございます` has pron `アリガトー:ゴザイマス’`
and accent `2/5:4/5`.  Upstream's NJDNode_load counts the slashes in the
accent field and, when there is more than one, splits every field on the colon
and emits one node per part, each after the first with its chain flag forced to
0 so it starts its own accent phrase.

This compiler used to parse `2/5:4/5` as a single pair, get a conversion error
and store 0/0 -- which made the entry look like a symbol with no reading, so
`ありがとうございます` fell back to spelling out its kana and `せっぱ詰まる`
produced no morae at all.  The parts are now kept: component 0 in the entry
itself and the rest in a side array, 16 bytes each -- pron offset u32, surface
offset u32, read offset u32, accent u8, mora count u8, pad u16.

An entry whose accent field holds a `*` or no `/` at all is a symbol, and gets
accent 0 and mora count 0 deliberately rather than by failing to parse: that
pair is the signal njd_set_pronunciation reads to replace it with a pause.
"""
import argparse, csv, hashlib, io, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# The official 1.11 release of December 2018, and the release matters here for
# one measured reason: the 1.09 dictionary stores a mora count that disagrees
# with its own reading for 1.96% of entries, and 1.11's for 0.00% of them.
# That count is what composes a compound's accent, so an older dictionary puts
# the fall in the wrong place.  Re-pointing this at one is a format bump, not
# a path change -- see VERSION below.
DIC = os.path.join(ROOT, 'open_jtalk-1.11', 'mecab-naist-jdic')
OUT = os.path.join(ROOT, 'data', 'ja', 'jadic.bin')

MAGIC = b'OPENTVJ1'
VERSION = 4   # 1 lost compound components; 2 was 1.09 data; 3 had no read
ENTRY = struct.Struct('<IIIHHhHBBBBI')
COMP = struct.Struct('<IIIBBH')
UNK = struct.Struct('<HHhHBBBB')


def pkg_version(dic):
    """The PACKAGE_VERSION of the release a dictionary folder belongs to."""
    for up in (os.path.dirname(dic), os.path.dirname(os.path.dirname(dic))):
        cfg = os.path.join(up, 'configure')
        if os.path.exists(cfg):
            try:
                txt = io.open(cfg, encoding='utf-8', errors='ignore').read()
            except OSError:
                continue
            m = re.search(r"PACKAGE_VERSION='([^']+)'", txt)
            if m:
                return m.group(1)
    return None


class Pool(object):
    """A NUL-separated string pool that hands back offsets and shares dupes."""

    def __init__(self):
        self.buf = bytearray()
        self.at = {}

    def add(self, s):
        b = s.encode('utf-8')
        if b in self.at:
            return self.at[b]
        off = len(self.buf)
        self.buf += b
        self.buf.append(0)
        self.at[b] = off
        return off


def read_char_def(path):
    """-> (categories in order, {name: id}, code point -> id)

    A code point may list several categories; MeCab takes the first as its
    own and the rest as compatible.  Only the first is kept here, which is
    what grouping and the unknown-word lengths use.  The others refine an
    edge case this does not implement, and saying so is better than implying
    the file was read in full.
    """
    cats, cid, rows = [], {}, []
    with io.open(path, encoding='utf-8', errors='ignore') as fh:
        for line in fh:
            line = line.split('#')[0].strip()
            if not line:
                continue
            f = line.split()
            if f[0].startswith('0x'):
                rows.append(f)
            else:
                cid[f[0]] = len(cats)
                cats.append((f[0], int(f[1]), int(f[2]), int(f[3])))
    cmap = bytearray([cid['DEFAULT']]) * 0x10000
    for f in rows:
        rng = f[0]
        if '..' in rng:
            lo, hi = (int(x, 16) for x in rng.split('..'))
        else:
            lo = hi = int(rng, 16)
        c = cid[f[1]]
        for u in range(lo, min(hi, 0xffff) + 1):
            cmap[u] = c
    return cats, cid, cmap


def read_unk_def(path, cid, pos_id, chain_id):
    out = []
    with io.open(path, encoding='utf-8', errors='ignore') as fh:
        for r in csv.reader(fh):
            if len(r) < 10:
                continue
            out.append((int(r[1]), int(r[2]), int(r[3]),
                        pos_id(tuple(r[4:10])), cid[r[0]]))
    return out


def read_matrix(path):
    with io.open(path, encoding='utf-8', errors='ignore') as fh:
        first = fh.readline().split()
        dim = int(first[0])
        assert int(first[1]) == dim, 'a non-square connection matrix'
        m = bytearray(dim * dim * 2)
        pack = struct.Struct('<h').pack_into
        for line in fh:
            f = line.split()
            if len(f) != 3:
                continue
            pack(m, ((int(f[0]) * dim) + int(f[1])) * 2, int(f[2]))
    return dim, m


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dic', default=DIC)
    ap.add_argument('--out', default=OUT)
    args = ap.parse_args(argv)

    for need in ('naist-jdic.csv', 'matrix.def', 'char.def', 'unk.def'):
        p = os.path.join(args.dic, need)
        if not os.path.exists(p):
            print('missing %s' % p, file=sys.stderr)
            print('naist-jdic is a reference checkout; see NOTICE.',
                  file=sys.stderr)
            return 2

    surf, pron, posp, chainp = Pool(), Pool(), Pool(), Pool()
    pos_ids, chain_ids = {}, {}
    pos_off, chain_off = [], []

    def pos_id(t):
        if t not in pos_ids:
            pos_ids[t] = len(pos_off)
            off = len(posp.buf)
            for s in t:
                posp.buf += s.encode('utf-8')
                posp.buf.append(0)
            pos_off.append(off)
        return pos_ids[t]

    def chain_id(s):
        if s not in chain_ids:
            chain_ids[s] = len(chain_off)
            chain_off.append(chainp.add(s))
        return chain_ids[s]

    chain_id('*')               # id 0, so an absent rule needs no special case

    def clamp(v):
        return 0 if v < 0 else (255 if v > 255 else v)

    def num(tok):
        try:
            return clamp(int(tok))
        except ValueError:
            return 0

    print('reading naist-jdic.csv ...')
    rows = []
    comps = []                  # the second and later components of compounds
    skipped = n_compound = 0
    with io.open(os.path.join(args.dic, 'naist-jdic.csv'),
                 encoding='utf-8', errors='ignore') as fh:
        for r in csv.reader(fh):
            if len(r) < 15:
                skipped += 1
                continue
            acc_f = r[13]
            # NJDNode_load's own test for a symbol, kept exactly: a star
            # anywhere, or no slash at all.  Accent 0 with mora count 0 is
            # then the signal that this entry has no usable reading.
            if '*' in acc_f or '/' not in acc_f:
                parts = [(r[12], r[11], 0, 0)]
            else:
                n_parts = acc_f.count('/')
                if n_parts == 1:
                    a, m = (acc_f.split('/') + [''])[:2]
                    parts = [(r[12], r[11], num(a), num(m))]
                else:
                    # one component per slash, every field split on the colon
                    prons = r[12].split(':')
                    reads = r[11].split(':')
                    accs = acc_f.split(':')
                    parts = []
                    for k in range(n_parts):
                        a, m = (accs[k].split('/') + [''])[:2] \
                            if k < len(accs) else ('', '')
                        parts.append((prons[k] if k < len(prons) else u'',
                                      reads[k] if k < len(reads) else u'',
                                      num(a), num(m)))
                    n_compound += 1
            surfs = r[0].split(':')
            comp_off = len(comps)
            for k, (pr, rd, a, m) in enumerate(parts[1:], 1):
                comps.append((pron.add(pr),
                              surf.add(surfs[k] if k < len(surfs) else r[0]),
                              pron.add(rd), a, m))
            rows.append((r[0].encode('utf-8'),
                         int(r[3]),
                         surf.add(r[0]), pron.add(parts[0][0]),
                         pron.add(parts[0][1]),
                         int(r[1]), int(r[2]),
                         pos_id(tuple(r[4:10])), parts[0][2], parts[0][3],
                         chain_id(r[14]), len(parts),
                         comp_off if len(parts) > 1 else 0))
    print('  %d entries, %d skipped, %d compound (%d extra components)'
          % (len(rows), skipped, n_compound, len(comps)))

    # by surface bytes, then by cost: the binary search finds the run and the
    # cheapest candidate is the first of it
    rows.sort(key=lambda e: (e[0], e[1]))

    cats, cid, cmap = read_char_def(os.path.join(args.dic, 'char.def'))
    unk = read_unk_def(os.path.join(args.dic, 'unk.def'), cid, pos_id, chain_id)
    print('  %d categories, %d unknown-word entries' % (len(cats), len(unk)))
    print('reading matrix.def ...')
    dim, matrix = read_matrix(os.path.join(args.dic, 'matrix.def'))
    print('  %d x %d' % (dim, dim))

    n_hdr_fields = 8 + 10 + 7
    head = 8 + 4 * n_hdr_fields
    blocks = []

    def place(b):
        off = head + sum(len(x) for x in blocks)
        blocks.append(b)
        return off, len(b)

    def pad(b, n=4):
        r = len(b) % n
        return b + bytes(n - r) if r else b

    off_surf, len_surf = place(pad(bytes(surf.buf)))
    off_pron, len_pron = place(pad(bytes(pron.buf)))
    off_posp, len_posp = place(pad(bytes(posp.buf)))
    off_chainp, len_chainp = place(pad(bytes(chainp.buf)))
    off_posi, _ = place(struct.pack('<%dI' % len(pos_off), *pos_off))
    off_chaini, _ = place(struct.pack('<%dI' % len(chain_off), *chain_off))

    ebuf = bytearray(len(rows) * ENTRY.size)
    for i, e in enumerate(rows):
        ENTRY.pack_into(ebuf, i * ENTRY.size, e[2], e[3], e[4], e[5], e[6],
                        e[1] if -32768 <= e[1] <= 32767 else 0,
                        e[7], e[8], e[9], e[10], e[11], e[12])
    off_entry, _ = place(bytes(ebuf))
    cbuf = bytearray(len(comps) * COMP.size)
    for i, c in enumerate(comps):
        COMP.pack_into(cbuf, i * COMP.size, c[0], c[1], c[2], c[3], c[4], 0)
    off_comp, _ = place(bytes(cbuf))
    off_matrix, _ = place(bytes(matrix))
    off_cat, _ = place(b''.join(bytes([c[1], c[2], min(c[3], 255), 0])
                                for c in cats))
    off_cmap, _ = place(bytes(cmap))
    ubuf = bytearray(len(unk) * UNK.size)
    for i, u in enumerate(unk):
        UNK.pack_into(ubuf, i * UNK.size, u[0], u[1],
                      u[2] if -32768 <= u[2] <= 32767 else 0,
                      u[3], u[4], 0, 0, 0)
    off_unk, _ = place(bytes(ubuf))

    hdr = MAGIC + struct.pack(
        '<%dI' % n_hdr_fields,
        VERSION, len(rows), len(pos_off), len(chain_off), len(cats),
        len(unk), dim, len(comps),
        off_surf, len_surf, off_pron, len_pron, off_posp, len_posp,
        off_chainp, len_chainp, off_posi, off_chaini,
        off_entry, off_matrix, off_cat, off_cmap, off_unk, off_comp, 0)
    assert len(hdr) == head, (len(hdr), head)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'wb') as fp:
        fp.write(hdr)
        for b in blocks:
            fp.write(b)
    # Provenance beside the binary.  "Which Open JTalk is this?" was an open
    # question for as long as the answer lived only in whoever ran the tool,
    # and the two checkouts in this repository differ in their dictionary as
    # well as their rules.  This records it where a future reader will look.
    prov = os.path.splitext(args.out)[0] + '.provenance.txt'
    with io.open(prov, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(u'Compiled by tools/gen_ja_dict.py, format version %d.\n\n'
                 % VERSION)
        fh.write(u'source   %s\n' % os.path.relpath(args.dic, ROOT))
        fh.write(u'release  %s\n' % (pkg_version(args.dic) or u'unrecorded'))
        fh.write(u'entries  %d\n\n' % len(rows))
        fh.write(u'sha256 of the inputs:\n')
        for need in ('naist-jdic.csv', 'matrix.def', 'char.def', 'unk.def'):
            fp2 = os.path.join(args.dic, need)
            h = hashlib.sha256(open(fp2, 'rb').read()).hexdigest()
            fh.write(u'  %-16s %s\n' % (need, h))
    print('  provenance -> %s' % os.path.relpath(prov, ROOT))
    n = os.path.getsize(args.out)
    print()
    print('%s' % os.path.relpath(args.out, ROOT))
    print('  %d bytes (%.1f MB)' % (n, n / 1048576.0))
    print('  surfaces %.1f MB, prons %.1f MB, entries %.1f MB, matrix %.1f MB'
          % (len_surf / 1048576.0, len_pron / 1048576.0,
             len(ebuf) / 1048576.0, len(matrix) / 1048576.0))
    print('  %d compound components, %d bytes' % (len(comps), len(cbuf)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
