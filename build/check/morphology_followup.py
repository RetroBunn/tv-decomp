"""Reproduce the second morphology review without editing production files.

python build/check/morphology_followup.py
Requires the existing build/check/njd-reference.dll. Like ja_stage_parity,
this supplies identical Python nodes to individual stages; it is not a
MeCab end-to-end comparison.

It reads the official 1.11 release, which is what the dictionary and the
extracted tables are built from, and that is the point of the mora-length
count below: the 1.09 dictionary stores a mora count that disagrees with its
own reading for 1.89% of entries and 1.11's for none of them.  The adapter no
longer substitutes pron for read -- doing so silently disabled
njd_set_digit's class3 table for every counter whose two readings differ.
"""
import collections
import copy
import csv
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SRC = 'open_jtalk-1.11'   # the official release, not the 1.09 line
sys.path[:0] = [str(ROOT / 'build/Japanese_test'), str(ROOT / 'build/check')]
import jp_dict as D
import jp_njd as N
import jp_digit as G
import openjtalk_morph_audit as A

def save(name, result):
    (ROOT / 'build/check' / name).write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')

def main():
    numbers = []
    for text in ['一粒', '二口', '一人', '二人', '一日', '1日', '一日中',
                 '五月一日', '5月1日', '十四日', '二十日', '三十', '四百',
                 '何万', '1,250円', '1250円', '3.14', '番号0123']:
        words = N.set_pronunciation(D.analyse(text))
        mine = G.set_digit(copy.deepcopy(words))
        reference = A.run(words, ['set_digit'])
        phrased = N.set_accent_phrase(copy.deepcopy(mine))
        accent_mine = N.set_accent_type(copy.deepcopy(phrased))
        accent_ref = A.run(phrased, ['set_accent_type'])
        numbers.append({'text': text, 'digit_python': A.summary(mine),
                        'digit_reference': A.summary(reference),
                        'accent_python': [w.acc for w in accent_mine],
                        'accent_reference': [w.acc for w in accent_ref]})
    save('morphology-followup-numbers.json', numbers)

    counts = collections.Counter()
    examples = []
    components = affected = entries = 0
    with (ROOT / SRC / 'mecab-naist-jdic/naist-jdic.csv').open(encoding='utf-8') as f:
        for row in csv.reader(f):
            if len(row) < 15:
                continue
            entries += 1
            bad = False
            for pron, accent in zip(row[12].split(':'), row[13].split(':')):
                if '/' not in accent or '*' in accent:
                    continue
                acc, stored = map(int, accent.split('/'))
                actual = len(N.split_morae(pron))
                components += 1
                if actual != stored:
                    bad = True
                    counts[stored, actual] += 1
                    if len(examples) < 16 or row[0] == 'あいせっする':
                        examples.append([row[0], pron, acc, stored, actual, row[4:10]])
            affected += int(bad)
    result = {'entries': entries, 'components': components,
              'mismatching_components': sum(counts.values()),
              'affected_entries': affected, 'largest_classes': counts.most_common(20),
              'examples': examples}
    save('morphology-followup-counts.json', result)

    files = [ROOT / 'build/check/njd-reference.dll', ROOT / 'data/ja/jadic.bin',
             ROOT / 'tools/gen_ja_dict.py', ROOT / 'tools/gen_ja_rules.py']
    for stage in ['njd', *A.STAGES]:
        files.extend(sorted((ROOT / SRC / stage).glob('*.c')))
        files.extend(sorted((ROOT / SRC / stage).glob('*.h')))
    files.extend(sorted((ROOT / SRC / 'mecab-naist-jdic').glob('*.def')))
    files.append(ROOT / SRC / 'mecab-naist-jdic/naist-jdic.csv')
    files.extend(ROOT / 'build/Japanese_test' / x for x in
                 ['jp_dict.py', 'jp_njd.py', 'jp_digit.py', 'ojt_tables.py'])
    save('morphology-followup-inputs.json', {
        'reference': 'Existing local njd-reference.dll, built from %s; not rebuilt by this script' % SRC,
        'scope': 'Stage comparisons on identical nodes, read and pron both carried',
        'identity': 'Hashes of files present at audit time, not proof of how the existing DLL was built',
        'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}})
    print('%d/%d entries have inconsistent stored and counted mora lengths.' % (affected, entries))
    for row in numbers:
        print(row['text'], 'Python:', ''.join(w[1] for w in row['digit_python']),
              'reference:', ''.join(w[1] for w in row['digit_reference']))

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
