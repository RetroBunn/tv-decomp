"""Audit Japanese timing allocations and render a saved before/after comparison.

python -B tools/ja_timing_audit.py

The baseline is the source/DLL snapshot in build/check/timing_audit/baseline.
No acoustic threshold is fitted. Frame allocations are NOT acoustic boundaries.
The vowel reference is recomputed from the released Yazawa--Kondo dataset;
consonant references below retain the studies' context and measurement units.
"""
import collections
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import sys
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'build/Japanese_test'))
sys.path.insert(0, str(ROOT / 'tests'))
import jp_speak as S
import jp_mora as M
from japanese_timing_test import CFront

OUT = ROOT / 'build/check/timing_audit'
DATA = ROOT / 'lang/jpn/research/acoustics/vowels/yazawa-kondo2019/dataset/JPLongShortVowels.csv'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wav(path, samples, sr):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(np.asarray(samples, dtype='<i2').tobytes())


def main():
    oldspec = importlib.util.spec_from_file_location(
        'timing_baseline', OUT / 'baseline/jp_speak.py')
    old = importlib.util.module_from_spec(oldspec)
    oldspec.loader.exec_module(old)
    # The saved module normally imports its siblings from its own directory;
    # they are unchanged dependencies resolved from build/Japanese_test above.
    baseline = CFront(OUT / 'front_before.dll')
    current = CFront(ROOT / 'build/check/ja_timing.dll')
    grouped = collections.defaultdict(list)
    for r in csv.DictReader(DATA.open(encoding='utf-8-sig')):
        if r['Gender'] == 'M' and r['Position'] == 'embedded':
            grouped[r['V1']].append(float(r['Duration']))
    reference = {v: dict(n=len(xs), mean_ms=statistics.mean(xs),
                         sd_ms=statistics.stdev(xs)) for v, xs in grouped.items()}
    for v in 'aiueo':
        for i, key in enumerate((v, v*2)):
            assert round(reference[key]['mean_ms'], 1) == S.V_DUR[v][i]
    report = dict(
        kind='Control-allocation audit; not validated acoustic segmentation',
        listening_status='Heard and judged good on 2026-10-05, on timing-before-after.wav; a preference between two renderings, not a validated segmentation',
        source_sha256={str(p.relative_to(ROOT)): digest(p) for p in (
            DATA, Path(S.__file__), ROOT / 'lang/jpn/port/ja_frame.c',
            ROOT / 'build/bin/tvtts64.dll', Path(__file__))},
        vowel_reference=dict(selection='male, embedded, all five contexts',
                             values=reference,
                             dataset_url='https://zenodo.org/records/15227304',
                             dataset_version='v3; local MD5 matches the published 56ffdcaab756399e7d9f846ccb091860'),
        source_notes=dict(
            vowel_paper_url='https://www.internationalphoneticassociation.org/icphs-proceedings/ICPhS2019/papers/ICPhS_720.pdf',
            local_pdf_mismatch='The existing acoustics/vowels/yazawa-kondo2019/ICPhS_720.pdf is Amano, Hirata & Yamakawa, Logarithmic durations for classifying and predicting Japanese short and long vowels. The actual Yazawa--Kondo paper was saved separately as yazawa-kondo2019-correct.pdf; the original was not overwritten.',
            frication_paper_url='https://doi.org/10.1250/ast.42.134',
            gemination_paper_url='https://doi.org/10.1017/S0025100308003459',
            rate_paper_url='https://doi.org/10.1016/j.wocn.2026.101473',
            rate_caution='Katsuda & Kang 2026 find different rate sensitivity among segment classes. No new universal rate exponent was fitted.'),
        derivation=dict(vowel_allocation='round((V_DUR + 35 ms) / 10 ms); 35 ms remains unvalidated',
                        long_increment='long allocation minus short allocation; now protected from redistribution',
                        stop_closure='Homma 1981 Table II: medial voiceless 67 ms and voiced 44 ms, rounded to 7/4 frames',
                        medial_vot='Homma 1981 Table III: p 7 ms, t 16 ms, k 24 ms; 1/2/2 frames',
                        initial_vot='p 30 ms, t 28.5 ms, k 56.7 ms -> 3/3/6 frames; original comment credits user-reported 2006 paper, not independently authenticated here',
                        geminate='14 additional frames: engineering approximation; Idemaru & Guion 2008 pooled closure 69 -> 206 ms',
                        geminate_vowels='Idemaru & Guion 2008 pooled preceding 59 -> 75 ms (1.27), following 76 -> 63 ms (0.83)',
                        frication='Amano & Yamakawa 2021 Table 1, word-initial voiced-u context: mean rise + steady/decay over both sets is s 134.50 ms, ts 73.25 ms, ch 78.65 ms. Current 100/50/60 ms are scaled assumptions, not those absolute measurements',
                        uncalibrated='Nasal/glide holds, universal slew, final fade and rate law are not independently validated by this audit'),
        changes=[], corpus={}, rate_checks=[], audio=[])

    words = [line.split('\t')[0] for line in
             (ROOT / 'build/Japanese_test/oracle_frames.tsv').read_text(encoding='utf-8').splitlines()
             if line and not line.startswith('#')]
    deltas = []
    for word in words:
        mo = M.to_morae(word)
        before = old.build(mo)[0]
        after = S.build(mo)[0]
        deltas.append((len(after)-len(before))*10)
    report['corpus'] = dict(n=len(words), changed_duration=sum(d != 0 for d in deltas),
                            mean_change_ms=statistics.mean(deltas),
                            median_change_ms=statistics.median(deltas),
                            min_change_ms=min(deltas), max_change_ms=max(deltas))
    for word in ('kiita', 'biipe', 'shashin', 'sashisuseso',
                 'a|kita', 'a|aoiueo', 'papa|kita', 'papa|aoiueo'):
        mo = M.to_morae(word)
        a, _, _ = old.build(mo)
        sa = old.LAST_SPANS
        b, _, _ = S.build(mo)
        sb = S.LAST_SPANS
        report['changes'].append(dict(text=word, total_frames=[len(a),len(b)],
            mora_frames_before=[s['final_end']-s['final_start'] for s in sa],
            mora_frames_after=[s['final_end']-s['final_start'] for s in sb],
            long_increments_before=[s['final_end']-s['final_start'] for m,s in zip(mo,sa) if m==':'],
            long_increments_after=[s['final_end']-s['final_start'] for m,s in zip(mo,sb) if m==':']))

    # Actual C allocations across rates, not a resampled Python approximation.
    for word in ('kakikukeko','sashisuseso','kiita','kitte','konnichiwa'):
        lengths = [len(current.build(word,150./w)[0]) for w in (90,150,225,253)]
        assert lengths == sorted(lengths, reverse=True)
        report['rate_checks'].append(dict(text=word,wpm=[90,150,225,253],frames=lengths))

    examples = ('kiita', 'shashin', 'sashisuseso', 'papa|aoiueo')
    minimal = ('obasan','obaasan','ojisan','ojiisan','kita','kiita','kite','kitte',
               'saka','sakka','zashi','zasshi')
    saved_sr = S.SR
    try:
        for sr in (11025,16000):
            S.SR = sr
            dest = OUT / str(sr)
            dest.mkdir(exist_ok=True)
            gap = np.zeros(round(sr*.45),dtype=np.int16)
            parts = []
            for word in examples:
                pair=[]
                for front in (baseline,current):
                    frames, _, _ = front.build(word, sr=sr)
                    for f in frames: f[17]=50  # same 100-Hz F0 in both versions
                    pcm=S.render(frames,postprocess=False)
                    pair.extend([pcm,gap])
                    report['audio'].append(dict(text=word,sr=sr,
                        variant='before' if front is baseline else 'after',
                        samples=len(pcm),peak=int(np.max(np.abs(pcm.astype(np.int32)))),
                        full_scale_samples=int(np.count_nonzero((pcm==-32768)|(pcm==32767)))))
                joined=np.concatenate(pair)
                wav(dest/(word.replace('|','-')+'-before-after.wav'),joined,sr)
                parts.append(joined)
            wav(dest/'timing-before-after.wav',np.concatenate(parts),sr)
            parts=[]
            for word in minimal:
                mo=M.to_morae(word)
                frames,ends,q=S.build(mo)
                S.pitch(frames,ends,0,q_ends=q,morae=mo)
                parts.extend([S.render(frames,postprocess=False),gap])
            wav(dest/'length-contrasts.wav',np.concatenate(parts),sr)
    finally:
        S.SR=saved_sr
    report['audio_conditions']=dict(before_after='Fixed 100-Hz pitch, stock engine voice 0, raw PCM, before then after; no independent normalization',
                                    examples=list(examples),
                                    contrasts='Current timing with the same heiban contour for all words; checks length, not dictionary accent',
                                    contrast_order=list(minimal))
    assert not sum(x['full_scale_samples'] for x in report['audio'])
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report['corpus'],indent=2))
    print('Wrote',OUT/'results.json')


if __name__ == '__main__':
    main()
