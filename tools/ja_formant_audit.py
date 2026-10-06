"""Reconstruct Japanese formant targets from primary data and render comparisons.

python -B tools/ja_formant_audit.py

Mokhtari's source data and before-change snapshots live in
build/check/formant_audit/. Sources are identified by URL and SHA-256 below.
The output distinguishes corpus measurements, transferred contrasts, parameter
quantisation, and rendered PCM. Parameter values are not acoustic measurements.
"""
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
OUT = ROOT / 'build/check/formant_audit'
ETL = OUT / 'MokhtariTanaka2000_ETLformantdata.txt'
YAZAWA = ROOT / 'lang/jpn/research/acoustics/vowels/yazawa-kondo2019/dataset/JPLongShortVowels.csv'
ETL_URL = 'https://isd.pu-toyama.ac.jp/~parham/documents/formantsETL/'
sys.dont_write_bytecode = True
sys.path[:0] = [str(ROOT / 'build/Japanese_test'), str(ROOT / 'tests')]
import jp_voice as J
import jp_speak as S
from japanese_timing_test import CFront

# Appendix A, /u/ column, zero-based word indices. These four onsets are
# k/b/m/k respectively. Every speaker contributes five frames per word.
BACK_WORDS = {8: 'W0398', 9: 'W0927', 17: 'W0885', 18: 'W1341'}


def source_values():
    x = np.loadtxt(ETL).reshape(5, 5, 22, 5, 8)
    etl = {v: np.median(x[:, i].reshape(-1, 8), axis=0).tolist()
           for i, v in enumerate('ieaou')}
    u = x[:, 4]
    contexts = {}
    for name, indices in (('back', sorted(BACK_WORDS)),
                          ('front', [i for i in range(22) if i not in BACK_WORDS])):
        values = u[:, indices, :, 1].reshape(-1)
        contexts[name] = dict(frames=len(values), words=len(indices),
                              median_f2_hz=float(np.median(values)))
    groups = {}
    with YAZAWA.open(encoding='utf-8-sig') as f:
        for r in csv.DictReader(f):
            if r['Gender'] == 'M':
                groups.setdefault(r['V1'], []).append(r)
    means = {v: [statistics.mean(float(r[f'F{k}_Midpoint']) for r in rows)
                 for k in (1, 2, 3)] for v, rows in groups.items()}
    delta = {v: [round(b-a) for a, b in zip(means[v], means[v*2])]
             for v in 'aiueo'}
    return etl, contexts, means, delta


def quantized(posture):
    f = S.frame(posture)
    # Stock voice 0 has no percentage adjustments. Synth_Frame enforces F4
    # >= F3+320 even when the requested F4 is lower.
    return [f[9]*4, f[10]*8+500, f[11]*16,
            max(f[12], f[11]+20)*16, f[13]*2, f[14]*2, f[15]*2]


def write_wav(path, pcm, sr):
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sr)
        f.writeframes(np.asarray(pcm, dtype='<i2').tobytes())


def lpc_poles(pcm, sr):
    """Steady-window diagnostic, not independent evidence for vowel targets."""
    seg = np.asarray(pcm[round(.25*sr):round(.60*sr)], dtype=float)
    y = np.append(seg[0], seg[1:] - .97*seg[:-1]) * np.hamming(len(seg))
    order = 2 + int(sr/1000)
    r = np.correlate(y, y, 'full')[len(y)-1:len(y)+order]
    toeplitz = r[np.abs(np.arange(order)[:, None] - np.arange(order))]
    a = np.linalg.solve(toeplitz, -r[1:])
    poles = []
    for z in np.roots(np.r_[1., a]):
        if z.imag <= 0:
            continue
        hz = np.angle(z)*sr/(2*np.pi)
        bw = -np.log(abs(z))*sr/np.pi
        if 120 < hz < sr/2-200 and 0 < bw < 1000:
            poles.append([round(float(hz), 1), round(float(bw), 1)])
    return sorted(poles)


def main():
    etl, contexts, means, delta = source_values()
    assert all([int(n) for n in etl[v][:7]] == list(J.V[v]) for v in J.V)
    assert delta == {v: list(d) for v, d in J.LONG_DELTA.items()}
    assert J.U_FRONT == round(contexts['front']['median_f2_hz'])
    assert J.U_BACK == round(contexts['back']['median_f2_hz'])
    spec = importlib.util.spec_from_file_location('old_voice', OUT/'baseline/jp_voice.py')
    old = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(old)
    report = dict(
        scope='Vowel anchors, length contrast and /u/ context; not a validation of every consonant locus or perceived voice quality',
        sources={
            'steady_reference': ETL_URL+'MokhtariTanaka2000_ETLformantdata.txt',
            'steady_documentation': ETL_URL+'MokhtariTanaka2000_ETLformantdoc.txt',
            'steady_paper': ETL_URL+'2000_ETLBulletin_MokhtariTanaka.pdf',
            'length_contrast': 'https://zenodo.org/records/15227304',
            'consonant_caveat': 'https://www2.ninjal.ac.jp/yutanaka/papers_cv/Tanaka2023_ICPhS2023.pdf'},
        source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in (ETL, YAZAWA, Path(J.__file__), ROOT/'lang/jpn/port/ja_tables.c',
                                 ROOT/'build/bin/tvtts64.dll', ROOT/'build/check/ja_timing.dll',
                                 OUT/'baseline/jp_voice.py',
                                 OUT/'baseline/ja_timing.dll', Path(__file__))},
        etl_selection='Five male speakers; 89 long and 21 doubled vowels; 5 frames per word/speaker. No short vowels in the selected set. 550 frames per vowel are not 550 independent speakers.',
        etl_pooled_medians_hz=etl,
        baseline_operation='Truncate the first seven pooled medians to integer Hz; reproduced all 35 existing constants exactly.',
        u_context=contexts,
        u_back_word_ids=list(BACK_WORDS.values()),
        u_policy='Round measured context medians to Hz. Extend to related labial/velar or palatal onsets as an explicit model approximation; retain pooled F2 for no onset or unrepresented places.',
        yazawa_selection='Male; both isolated and embedded positions; all five contexts; 160 tokens per vowel/length category',
        yazawa_midpoint_means_hz=means, long_minus_short_hz=delta,
        model='LONG = context-adjusted ETL steady reference; SHORT = LONG - Yazawa delta. Transferring the difference between corpora is a synthesis approximation; F4 and bandwidths have no matched length data.',
        consonant_limit='Tanaka measured F2 in the middle of release/frication NOISE, not at voiced-vowel onset. Existing transition rows inspired by that paper are proxies. No replacement voiced-onset table was established here.',
        targets=[], audio=[], acoustic_diagnostic=[],
        acoustic_method='Stock voice 0 at 100 Hz, 700 control ms; autocorrelation LPC of raw PCM from 250 to 600 ms, preemphasis .97, Hamming window, order 2+floor(sr/1000). Estimator-dependent poles, not grounds for retuning targets.',
        listening_status='Heard and judged better on 2026-10-05, on words-before-after.wav; a preference between two renderings, not a measurement of either')
    for v in 'aiueo':
        for ctx in ('', 'k', 's', 'py', 'h') if v == 'u' else ('',):
            for long in (False, True):
                post = J.vowel(v, ctx, long)
                report['targets'].append(dict(vowel=v, context=ctx, long=long,
                    before_hz=old.vowel(v, ctx, long), after_hz=post,
                    effective_voice0_hz=quantized(post)))

    baseline = CFront(OUT/'baseline/ja_timing.dll')
    current = CFront(ROOT/'build/check/ja_timing.dll')
    words = ('obaasan', 'ojiisan', 'biiru', 'kuuki', 'tooru', 'fune', 'ryouri')
    saved_sr = S.SR
    try:
        for sr in (11025, 16000):
            S.SR = sr
            dest = OUT / str(sr)
            dest.mkdir(exist_ok=True)
            gap = np.zeros(round(sr*.35), dtype=np.int16)
            held, running = [], []
            for v in 'aiueo':
                for long in (False, True):
                    post = J.vowel(v, long=long)
                    f = S.frame(post)
                    pcm = S.render([f.copy() for _ in range(70)], postprocess=False)
                    report['acoustic_diagnostic'].append(dict(sr=sr, vowel=v,
                        long=long, target_hz=post, effective_hz=quantized(post),
                        lpc_poles_hz_bw=lpc_poles(pcm, sr)))
                    for variant, module in (('before', old), ('after', J)):
                        f = S.frame(module.vowel(v, long=long))
                        pcm = S.render([f.copy() for _ in range(45)], postprocess=False)
                        held.extend([pcm, gap])
                        report['audio'].append(dict(kind='held', sr=sr, vowel=v,
                            long=long, variant=variant,
                            peak=int(np.max(np.abs(pcm.astype(np.int32)))),
                            clipped=int(np.count_nonzero((pcm == -32768) | (pcm == 32767)))))
            for word in words:
                for variant, front in (('before', baseline), ('after', current)):
                    frames, _, _ = front.build(word, sr=sr)
                    for f in frames: f[17] = 50
                    pcm = S.render(frames, postprocess=False)
                    running.extend([pcm, gap])
                    report['audio'].append(dict(kind='word', sr=sr, word=word,
                        variant=variant, frames=len(frames),
                        peak=int(np.max(np.abs(pcm.astype(np.int32)))),
                        clipped=int(np.count_nonzero((pcm == -32768) | (pcm == 32767)))))
            write_wav(dest/'vowels-before-after.wav', np.concatenate(held), sr)
            write_wav(dest/'words-before-after.wav', np.concatenate(running), sr)
    finally:
        S.SR = saved_sr
    report['audio_conditions'] = dict(voice='Stock 0, fixed 100-Hz pitch, raw PCM, no independent normalization',
        held_order='a i u e o; each has short before/after, then long before/after; each held for 450 control ms to isolate spectrum',
        word_order=list(words), words='Before then after. Changed formant travel can change transition duration.')
    assert not sum(r['clipped'] for r in report['audio'])
    (OUT/'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Verified all 35 baseline constants, all 15 length deltas, and both /u/ context medians.')
    print('Wrote', OUT/'results.json')


if __name__ == '__main__':
    main()
