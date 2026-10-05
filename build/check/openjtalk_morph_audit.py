"""Run the checked-out Open JTalk NJD stages independently of the Python port.

Build and run (requires the repository's gcc toolchain):
    python build/check/openjtalk_morph_audit.py --build
This audit does not edit the dictionary, frontend, or installed speech engine.
"""
import copy
import csv
import ctypes as C
import hashlib
import json
from pathlib import Path
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'build/Japanese_test'))
import jp_dict as D
import jp_njd as N
import jp_front as F

STAGES = ['njd_set_pronunciation', 'njd_set_digit', 'njd_set_accent_phrase',
          'njd_set_accent_type', 'njd_set_unvoiced_vowel', 'text2mecab']

# Which Open JTalk the reference is built from.  The generators and the port
# target the official 1.11 release of December 2018, so the reference has to
# as well: comparing 1.11-derived data against an older release's rules would
# mix two differences and attribute them to neither.
#
# Override with --src to build the reference from a different checkout.
SRC = 'open_jtalk-1.11'
for _i, _a in enumerate(sys.argv):
    if _a == '--src' and _i + 1 < len(sys.argv):
        SRC = sys.argv[_i + 1]
DLL = 'build/check/njd-reference.dll'
if '--build' in sys.argv:
    cmd = ['gcc', '-shared', '-O2', '-DCHARSET_UTF_8', '-I' + SRC + '/njd',
           '-o', DLL, SRC + '/njd/njd.c', SRC + '/njd/njd_node.c']
    for stage in STAGES:
        cmd += ['-I' + SRC + '/' + stage, SRC + '/' + stage + '/' + stage + '.c']
    subprocess.run(cmd, cwd=ROOT, check=True)
    print('built %s from %s' % (DLL, SRC))

class Node(C.Structure):
    pass
P = C.POINTER(Node)
STRINGS = ['string', 'pos', 'pos_group1', 'pos_group2', 'pos_group3',
           'ctype', 'cform', 'orig', 'read', 'pron']
Node._fields_ = [(k, C.c_char_p) for k in STRINGS] + [
    ('acc', C.c_int), ('mora_size', C.c_int), ('chain_rule', C.c_char_p),
    ('chain_flag', C.c_int), ('prev', P), ('next', P)]
class NJD(C.Structure):
    _fields_ = [('head', P), ('tail', P)]

lib = C.CDLL(str(ROOT / DLL))
for name in ['NJD_initialize', 'NJD_clear', 'njd_set_pronunciation',
             'njd_set_digit', 'njd_set_accent_phrase', 'njd_set_accent_type',
             'njd_set_unvoiced_vowel']:
    getattr(lib, name).argtypes = [C.POINTER(NJD)]
    getattr(lib, name).restype = None
lib.NJD_load.argtypes = [C.POINTER(NJD), C.c_char_p]
lib.NJD_load.restype = None
lib.NJDNode_load.argtypes = [P, C.c_char_p]
lib.NJDNode_load.restype = None
for name in ['NJDNode_clear', 'NJDNode_initialize']:
    getattr(lib, name).argtypes = [P]
    getattr(lib, name).restype = None
for field in STRINGS + ['chain_rule']:
    fn = getattr(lib, 'NJDNode_set_' + field)
    fn.argtypes = [P, C.c_char_p]
    fn.restype = None
lib.text2mecab.argtypes = [C.c_char_p, C.c_char_p]
lib.text2mecab.restype = None

def normalize(text):
    b = text.encode()
    out = C.create_string_buffer(len(b) * 8 + 64)
    lib.text2mecab(out, b)
    return out.value.decode()

def run(words, stages, feature=None):
    njd = NJD()
    lib.NJD_initialize(C.byref(njd))
    try:
        for word in words:
            # Allocate through the reference library, then use setters so a
            # comma or other punctuation cannot corrupt serialized fields.
            lib.NJD_load(C.byref(njd), b'x,*,*,*,*,*,*,x,x,x,0/1,*,-1')
            p = njd.tail
            # `read` is its own field and not a copy of `pron`.  They differ
            # for 18.85% of naist-jdic -- 通り reads トオリ and is pronounced
            # トーリ -- and njd_set_digit's class3 table keys on read, so
            # passing pron there silently disabled that table for every
            # counter whose two readings differ and made 一通り come back
            # イットオリ.
            fields = dict(zip(STRINGS, [word.string, *word.pos,
                                        word.string, word.read, word.pron]))
            fields['chain_rule'] = word.chain_rule
            for key, value in fields.items():
                getattr(lib, 'NJDNode_set_' + key)(p, value.encode())
            p.contents.acc = word.acc
            p.contents.mora_size = word.mora_size
            p.contents.chain_flag = word.chain_flag
        if feature is not None:
            lib.NJD_load(C.byref(njd), b'x,*,*,*,*,*,*,x,x,x,0/1,*,-1')
            p = njd.tail
            lib.NJDNode_clear(p)
            lib.NJDNode_initialize(p)
            lib.NJDNode_load(p, feature.encode())
            while p.contents.next:
                p = p.contents.next
            njd.tail = p
        for stage in stages:
            getattr(lib, 'njd_' + stage)(C.byref(njd))
        out = []
        p = njd.head
        while p:
            x = p.contents
            w = D.Word((x.string or b'*').decode(),
                       tuple((getattr(x, k) or b'*').decode() for k in STRINGS[1:7]),
                       (x.pron or b'*').decode(), x.acc, x.mora_size,
                       (x.chain_rule or b'*').decode(),
                       read=(x.read or b'*').decode())
            w.chain_flag = x.chain_flag
            out.append(w)
            p = x.next
        return out
    finally:
        lib.NJD_clear(C.byref(njd))

def summary(words):
    return [[w.string, w.pron, w.acc, w.mora_size, w.chain_flag] for w in words]

def main():
    report = {'reference_version': 'Built from %s' % SRC,
              'scope': 'NJD stages on identical input nodes; not a MeCab/Viterbi parity test',
              'sha256': {}}
    for rel in ['data/ja/jadic.bin', SRC + '/njd/njd_node.c'] + [
            SRC + '/' + s + '/' + s + '.c' for s in STAGES]:
        report['sha256'][rel] = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
    report['normalization'] = [[t, D.normalize(t), normalize(t)]
        for t in ['1250円', 'ＡＢＣ', 'ｶﾞｯﾂﾎﾟｰｽﾞ', 'です?', 'A B']]
    report['devoicing'] = []
    for s in ['キクカ', 'クツシタ', 'キツツキ', 'シキチ', 'スキカ']:
        w = D.Word(s, ('名詞', '一般', '*', '*', '*', '*'), s, 0,
                   len(N.split_morae(s)), '*')
        py = ''.join(m + ('’' if flag else '') for m, flag, _ in N.set_unvoiced_vowel([w]))
        ref = run([w], ['set_unvoiced_vowel'])[0].pron
        report['devoicing'].append([s, py, ref])
    report['real_word_devoicing'] = []
    for text in ['複数', '複製', '複写', '美しさ']:
        words = N.set_accent_type(N.set_accent_phrase(N.set_pronunciation(D.analyse(text))))
        py = ''.join(m + ('’' if flag else '') for m, flag, _ in N.set_unvoiced_vowel(words))
        ref = ''.join(w.pron for w in run(words, ['set_unvoiced_vowel']))
        report['real_word_devoicing'].append([text, py, ref])
    d = D.load()
    multi = []
    with (ROOT / SRC / 'mecab-naist-jdic/naist-jdic.csv').open(encoding='utf-8') as f:
        for row in csv.reader(f):
            if len(row) >= 15 and ':' in row[13]:
                multi.append(row)
    report['compound_entries'] = {'count': len(multi),
        'examples': [[r[0], r[12], r[13]] for r in multi[:8]]}
    report['compiled_compounds'] = []
    for row in [r for r in multi if r[0] in ['ありがとうございます', 'せっぱ詰まる']]:
        surface, pron, acc = row[0], row[12], row[13]
        hit = d.find(surface.encode())
        entries = [d.entry(i) for i in range(*hit)]
        report['compiled_compounds'].append({'surface': surface, 'raw_pron': pron,
            'raw_accent': acc, 'compiled_accent_mora': [[e[6], e[7]] for e in entries],
            'python_frontend': F.analyse(surface)[1:],
            'reference_node_loading': summary(run([], [], ','.join([row[0]] + row[4:])))})
    report['digit_nodes'] = []
    for text in ['1250円', '１２５０円', '一二五〇円', '三百円']:
        words = D.analyse(text)
        report['digit_nodes'].append([text, summary(words),
            summary(N.set_pronunciation(copy.deepcopy(words))), F.analyse(text)[1:]])
    report['rare_morae'] = [[s, summary(D.analyse(s)),
                           [N.split_morae(w.pron) for w in D.analyse(s)], F.analyse(s)[1:]]
                           for s in ['ちっちゃゅぅ', 'クヮルテット', 'ゴォォール']]
    report['upstream_normalization_experiment'] = []
    old_normalize = D.normalize
    try:
        D.normalize = normalize
        for text in ['1250円', 'ＡＢＣ', 'ｶﾞｯﾂﾎﾟｰｽﾞ']:
            words = D.analyse(text)
            report['upstream_normalization_experiment'].append([text, summary(words),
                summary(run(words, ['set_pronunciation', 'set_digit', 'set_accent_phrase',
                                    'set_accent_type', 'set_unvoiced_vowel']))])
    finally:
        D.normalize = old_normalize
    report['unknown_boundaries'] = [[s, F.analyse(s)[1:]]
        for s in ['あ龘い', '龘あ', 'あ龘', '龘', 'あ']]
    target = ROOT / 'build/check/openjtalk-morph-audit.json'
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
