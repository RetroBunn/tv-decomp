"""Timing allocation regressions, independent of acoustic boundary detection.

python -B tests/japanese_timing_test.py

C checks use build/check/ja_timing.dll (or TVTTS_JA_TIMING_DLL), built from
ja_frame, ja_tables, ja_mora, ja_pitch, ja_voice, ja_devoice_tab and ja_ojt
with gcc -shared -O2 -Ilang/jpn/port. They compare unpitched control frames,
not perceived vowel duration. The frame oracle separately checks pitched C.
"""
import ctypes as C
import itertools
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'build/Japanese_test'))
import jp_mora as M
import jp_speak as S


class Mora(C.Structure):
    _fields_ = [('kind', C.c_ubyte), ('c', C.c_byte), ('v', C.c_byte)]


class Options(C.Structure):
    _fields_ = [('sr', C.c_int), ('engine_voice', C.c_int),
                ('rate_scale', C.c_double), ('fb', C.c_double),
                ('question', C.c_int), ('accent', C.c_int),
                ('accents', C.POINTER(C.c_int)), ('n_accents', C.c_int),
                ('devoiced', C.POINTER(C.c_int)), ('n_devoiced', C.c_int)]


class Utterance(C.Structure):
    _fields_ = [('frames', C.POINTER(C.c_ubyte)), ('n', C.c_int),
                ('ends', C.POINTER(C.c_double)), ('n_ends', C.c_int),
                ('q_ends', C.POINTER(C.c_double)), ('n_q', C.c_int)]


class CFront:
    def __init__(self, path):
        self.dll = d = C.CDLL(str(path))
        d.ja_to_morae.argtypes = [C.c_char_p, C.c_size_t,
                                 C.POINTER(Mora), C.c_int]
        d.ja_opts_default.argtypes = [C.POINTER(Options)]
        d.ja_build.argtypes = [C.POINTER(Mora), C.c_int,
                              C.POINTER(Options), C.POINTER(Utterance)]
        d.ja_utt_free.argtypes = [C.POINTER(Utterance)]
        d.ja_pitch.argtypes = [C.POINTER(Utterance), C.POINTER(Mora), C.c_int,
                              C.POINTER(Options), C.POINTER(C.c_double)]

    def build(self, text, scale=1., sr=16000, engine_voice=0, pitch=None):
        data = text.encode('utf-8')
        mo = (Mora * (len(data) + 1))()
        n = self.dll.ja_to_morae(data, len(data), mo, len(mo))
        o, u = Options(), Utterance()
        self.dll.ja_opts_default(C.byref(o))
        o.sr, o.rate_scale = sr, scale
        o.engine_voice = engine_voice
        if pitch is not None:
            o.fb = 72. * pitch / 85.
        if self.dll.ja_build(mo, n, C.byref(o), C.byref(u)) != 0:
            raise RuntimeError('ja_build failed')
        try:
            if pitch is not None:
                self.dll.ja_pitch(C.byref(u), mo, n, C.byref(o), None)
            raw = C.string_at(u.frames, u.n * 22)
            frames = [list(raw[i:i+22]) for i in range(0, len(raw), 22)]
            if engine_voice:
                for f in frames:
                    f[21] = (f[21] & 15) | (engine_voice << 4)
            return (frames,
                    list(u.ends[:u.n_ends]), list(u.q_ends[:u.n_q]))
        finally:
            self.dll.ja_utt_free(C.byref(u))


def shape(frames):
    """Remove repeated frames, retaining every distinct articulatory state."""
    return [key for key, _ in itertools.groupby(map(tuple, frames))]


class TimingAllocation(unittest.TestCase):
    def test_pause_separates_timing_budgets(self):
        for first in ('a', 'papa', 'gaga', 'kiita', 'kitte'):
            for boundary in ('|', '||'):
                prefixes = []
                for last in ('kita', 'aoiueo', 'sashisuseso'):
                    mo = M.to_morae(first + boundary + last)
                    fr, ends, _ = S.build(mo)
                    at = mo.index(boundary)
                    prefixes.append((fr[:round(ends[at] * 100)], ends[:at+1]))
                self.assertEqual(prefixes[0], prefixes[1], (first, boundary))
                self.assertEqual(prefixes[0], prefixes[2], (first, boundary))

    def test_redistribution_preserves_transition_and_taper_shapes(self):
        for text in ('sashisuseso', 'shashin', 'bipe', 'kitte', 'gakkou',
                     'sakura', 'aoiueo', 'kiita', 'papa|shashin'):
            mo = M.to_morae(text)
            with patch.multiple(S, FINAL_FADE=0, FINAL_PAD=0):
                with patch.object(S, 'NO_COMPENSATE', True):
                    original = S.build(mo)[0]
                compensated = S.build(mo)[0]
            self.assertEqual(shape(original), shape(compensated), text)

    def test_long_increment_is_not_spent_or_inflated(self):
        for vowel in 'aiueo':
            for onset in ('', 'k', 'b', 's', 'm'):
                text = onset + vowel * 2 + 'ta'
                mo = M.to_morae(text)
                _, ends, _ = S.build(mo)
                at = mo.index(':')
                self.assertEqual(round((ends[at] - ends[at-1]) * 100),
                                 S.long_extra(vowel), text)

    def test_infeasible_budget_keeps_the_cues(self):
        S.build(M.to_morae('shashin'))
        self.assertGreater(sum(s['final_end'] - s['final_start']
                               for s in S.LAST_SPANS),
                           sum(s['nominal'] for s in S.LAST_SPANS))

    def test_expansion_rounding_never_shortens_a_vowel(self):
        # 51 extra frames shared by 100 equal vowels rounds to 100 first.
        # Removing the 49 excess additions must not consume original frames.
        frames, spans = [], []
        for i in range(100):
            f = [i] + [0] * 21
            spans.append(dict(start=len(frames), steady=len(frames), len=3,
                              head=0, nominal=4 if i < 51 else 3,
                              adjustable=True, budget_break=False,
                              steady_frame=f))
            frames.extend([f.copy() for _ in range(3)])
        after, ends, _ = S._compensate(frames, spans, [])
        self.assertEqual(len(after), 351)
        lengths = [round((end-start)*100)
                   for start, end in zip([0.] + ends, ends)]
        self.assertGreaterEqual(min(lengths), 3)
        self.assertEqual(shape(frames), shape(after))


class CTiming(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('TVTTS_JA_TIMING_DLL',
                                   ROOT / 'build/check/ja_timing.dll'))
        if not path.exists():
            raise unittest.SkipTest('Build ja_timing.dll for C timing checks')
        cls.front = CFront(path)

    def test_c_python_frames_and_mora_times(self):
        # Retain the existing oracle's inputs, including dictionary examples.
        words = []
        for line in (ROOT / 'build/Japanese_test/oracle_frames.tsv').read_text(
                encoding='utf-8').splitlines():
            if line and not line.startswith('#'):
                words.append(line.split('\t')[0])
        words += ['a|kita', 'a|aoiueo', 'gaga||shashin', 'papa||aoiueo',
                  'kiita', 'shiishi', 'gakkou|kitte', 'a||', '|a', 'a||a',
                  'muranaramabamagibigibinabanagigiramu']
        for sr in (11025, 16000):
            with patch.object(S, 'SR', sr):
                for word in words:
                    with self.subTest(sr=sr, word=word):
                        expected = S.build(M.to_morae(word))
                        self.assertEqual(self.front.build(word, sr=sr), expected)

    def test_length_contrasts_and_pauses_at_api_rates(self):
        for wpm in (90, 150, 225, 253):
            scale = 150. / wpm
            for sr in (11025, 16000):
                for vowel in 'aiueo':
                    _, ends, _ = self.front.build('k' + vowel*2 + 'ta', scale, sr)
                    target = max(1, max(2, round((S.V_DUR[vowel][1]*scale+35)/10))
                                 - max(2, round((S.V_DUR[vowel][0]*scale+35)/10)))
                    self.assertEqual(round((ends[1]-ends[0])*100), target)
                for first in ('papa', 'gaga', 'kiita', 'kitte'):
                    a = self.front.build(first+'|kita', scale, sr)
                    b = self.front.build(first+'|aoiueo', scale, sr)
                    at = len(M.to_morae(first))
                    self.assertEqual(a[1][:at+1], b[1][:at+1])
                    self.assertEqual(a[0][:round(a[1][at]*100)],
                                     b[0][:round(b[1][at]*100)])

    def test_speech_rate_still_changes_duration(self):
        for text in ('kakikukeko', 'sashisuseso', 'konnichiwa', 'kiita', 'kitte'):
            lengths = [len(self.front.build(text, 150./wpm)[0])
                       for wpm in (90, 150, 225, 253)]
            self.assertEqual(lengths, sorted(lengths, reverse=True), text)
            self.assertLess(lengths[-1], lengths[1], text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
