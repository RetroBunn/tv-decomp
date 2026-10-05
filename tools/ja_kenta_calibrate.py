"""Reproduce Japanese stock-voice source calibration and running-speech audits.

python tools/ja_kenta_calibrate.py          # search the track offsets
python tools/ja_kenta_calibrate.py --audit  # requires harness/build.sh
python tools/ja_kenta_calibrate.py --voice 8 --audit  # Hanako/Wanda

Reference: untrimmed Peter Japanese at the same rate, not English acoustics.
Report AC RMS (subtract the integer filter's DC bias), on raw PCM after
startup. Search discrete offsets because amplitude lookups saturate. This
does not change any source files or assert perceptual equivalence.
"""
import argparse
import ctypes as C
import math
import os
from pathlib import Path
import subprocess
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build' / 'Japanese_test'))
import jp_speak as S
import jp_voice as J
from ja_voice_gain import WORDS


def render(frames, voice, sr):
    d = S._lib()
    raw = bytes(v for f in frames for v in f)
    buf = (C.c_ubyte * len(raw)).from_buffer_copy(raw)
    out = []

    @S._CB
    def cb(e, _):
        if e[0].type == 0:
            out.extend(e[0].samples[:e[0].count])
        return 0

    s = d.tvtts_create_lang(sr, b'en')
    if not s:
        raise RuntimeError('cannot create engine')
    try:
        d.tvtts_set_voice(C.c_void_p(s), voice)
        rc = d.tvtts_speak_frames(C.c_void_p(s), buf, len(frames), cb, None)
        if rc:
            raise RuntimeError('frame rendering failed')
    finally:
        d.tvtts_destroy(C.c_void_p(s))
    return np.asarray(out, dtype=float)


def held(posture, source, voice, sr):
    # Explicitly disable the Python frontend's scoped rate trims. Parallel
    # calibration below supplies its already rate-compensated track values.
    with S._noise_trim():
        f = S.frame(posture, source)
    f[21] = voice << 4
    x = render([f] * 140, voice, sr)[40 * (sr // 100):130 * (sr // 100)]
    return float(np.std(x)), float(np.max(np.abs(x)))


def search(posture, source, track, sr, max_trim, headroom=False, voice=2):
    target, _ = held(posture, source, 0, sr)
    candidates = []
    for trim in range(max_trim + 1):
        src = dict(source)
        src[track] = max(0, src[track] - trim)
        rms, peak = held(posture, src, voice, sr)
        if rms > 0 and (not headroom or peak < 29000):
            candidates.append((abs(math.log(rms / target)), trim, rms))
    if not candidates:
        raise RuntimeError('no valid calibration candidate')
    _, trim, rms = min(candidates)
    return trim, 20 * math.log10(rms / target)


def calibrate(voice=2):
    for sr in (11025, 16000):
        print('Rate', sr, '(vowels in a i u e o order)')
        for kind in ('vowel', 'k', 'ky', 'p', 'py'):
            rows = []
            for v in 'aiueo':
                p = J.V[v] if kind == 'vowel' else J.burst(kind, v)
                rows.append(search(p, {0: 0, 2: 65}, 2, sr, 40, voice=voice))
            print(kind, 'offsets', [r[0] for r in rows],
                  'residual dB', [round(r[1], 2) for r in rows])
        for c in ('s', 'sh', 'z', 'j', 'ch', 'ts', 't', 'd'):
            p = J.burst(c, 'a') if c in ('t', 'd') else J.SIB_POST[c]
            src = (J.BURST_SRC[c] if c in ('t', 'd') else
                   J.SIBIL_S if c == 's' else
                   J.SIBIL_Z if c in ('z', 'j') else J.SIBIL)
            rows = []
            for track in (5, 6):
                if track not in src:
                    rows.append((0, 0))
                    continue
                amp = src[track] - (S.NOISE_TRIM_UNITS.get(c, 0)
                                    if sr == 11025 else 0)
                rows.append(search(p, {0: 0, 1: 80, track: amp}, track,
                                   sr, amp - 1, headroom=True, voice=voice))
            print(c, 'tracks 5/6', [r[0] for r in rows],
                  'residual dB', [round(r[1], 2) for r in rows])


def audit_words():
    words = [''.join(S.M.to_morae(w)) for w in WORDS]
    words.append('sashisuseso')  # devoiced /u/ tail into the next /s/ posture
    # Initial, medial, geminate and palatal releases, voiced and devoiced
    # high vowels, plus held vowels and each noise-source family.
    for c in ('k', 'ky', 'p', 'py', 't', 's', 'sh', 'ts', 'ch', 'z', 'j', 'h', 'hy', 'f'):
        for v in 'aiueo':
            words.extend((c + v, 'a' + c + v + 'a'))
    for c in ('k', 'ky'):
        for v in 'aiueo':
            words.extend(('aQ' + c + v + 'a', c + v + v))
    return list(dict.fromkeys(words))


def audit(name='Kenta'):
    d = S._lib()
    d.tvtts_voice_name.restype = C.c_char_p
    voices = {d.tvtts_voice_name(v).decode(): v for v in range(d.tvtts_voice_count())}
    voice = voices[name]
    exe = ROOT / 'build/check/ja_source_diag.exe'
    failures = []
    words = audit_words()
    env = dict(os.environ)
    env.pop('TRACE', None)
    for sr in (11025, 16000):
        for wpm in (90, 150, 300):
            env['WPM'] = str(wpm)
            worst = (0, '')
            for w in words:
                out = subprocess.check_output([str(exe), 'ja', str(voice), str(sr), w],
                                              env=env, text=True)
                rows = dict((r.split()[0], r.split()[1:]) for r in out.splitlines())
                peak, rails = map(int, rows['PCM'][:2])
                overflow = sum(map(int, rows['STATE'])) + sum(map(int, rows['POLE']))
                table = list(map(int, rows['TABLE']))
                freq_bytes = 4 * (sr // 16 + 1) if sr == 16000 else 2800
                if rails or overflow or table[0] >= 720 or table[1] >= 720 or table[2] >= freq_bytes:
                    failures.append((sr, wpm, w, peak, rails, overflow, table))
                worst = max(worst, (peak, w))
            print(sr, wpm, len(words), 'tokens; maximum peak', worst)
    for row in failures[:20]:
        print('FAIL', row)
    print('No rail hits, state/pole wraps or out-of-range table reads.'
          if not failures else '%d failed probes' % len(failures))
    return int(bool(failures))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--audit', action='store_true')
    ap.add_argument('--voice', type=int, choices=(1, 2, 8, 9), default=2,
                    help='stock engine voice: Sidney, Eager Eddie, Wanda, Julia')
    args = ap.parse_args()
    if args.audit:
        sys.exit(audit({1: 'Tsuyoshi', 2: 'Kenta', 8: 'Hanako', 9: 'Keiko'}[args.voice]))
    calibrate(args.voice)
