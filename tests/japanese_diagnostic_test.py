"""Regression checks for assisted measurement and controlled Japanese stimuli.
Run from repo root: python tests/japanese_diagnostic_test.py
No audio, answer sheets, or calibration constants are written.
"""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build'/'Japanese_test'))
from detector_diag import Features, candidate, reference_level, sensitivity, steady_span
import jp_speak as S
import listening_test as L


def tone(sr=16000, duration=.12, gain=1000., hz=100.):
    n = round(sr*duration)
    return np.r_[np.zeros(round(sr*.08)),
                 gain*np.sin(2*np.pi*hz*np.arange(n)/sr),
                 np.zeros(round(sr*.15))]


def setup_signal(x, sr=16000):
    f = Features(x,sr)
    ref,n = reference_level(f,round(sr*.105),round(sr*.135))
    return f,ref,round(sr*.12)


class DetectorTests(unittest.TestCase):
    def test_complete_windows_and_empty_signal(self):
        for sr in (8000,11025,16000):
            f = Features(np.zeros(round(sr*.032)),sr)
            self.assertEqual(len(f.per),1)
            self.assertTrue(np.all(f.e_end <= len(f.x)))
            self.assertTrue(np.all(f.p_end <= len(f.x)))
        f = Features([])
        self.assertEqual(len(f.per),0)
        self.assertEqual(reference_level(f,0,1),(None,0))

    def test_valid_and_truncated_regions(self):
        f,ref,seed = setup_signal(tone())
        expected = candidate(f,seed,ref,20)
        self.assertEqual(expected.status,'ok')
        for hi in (3500,4000,len(f.x)):
            r = candidate(f,seed,ref,20,region=(0,hi))
            self.assertEqual((r.start,r.end),(expected.start,expected.end))
        cut = candidate(f,seed,ref,20,region=(0,2500))
        self.assertEqual(cut.status,'boundary_truncated')
        self.assertIsNone(cut.duration_ms)

    def test_exact_zero_padding_and_gain_invariance(self):
        x = tone()
        f,ref,seed = setup_signal(x)
        baseline = candidate(f,seed,ref,20)
        for other in (np.r_[x,np.zeros(1600)],x*.2,x*3):
            g,level,s = setup_signal(other)
            r = candidate(g,s,level,20)
            self.assertEqual((r.start,r.end),(baseline.start,baseline.end))

    def test_known_duration_increment(self):
        values = []
        for duration in (.12,.13,.16,.20):
            f,ref,seed = setup_signal(tone(duration=duration))
            r = candidate(f,seed,ref,20)
            self.assertEqual(r.status,'ok')
            values.append(r.duration_ms)
        np.testing.assert_allclose(np.array(values)-values[0],[0,10,40,80],atol=1.)

    def test_seed_stability(self):
        f,ref,_ = setup_signal(tone())
        results = [candidate(f,s,ref,20) for s in (1760,2080,2560)]
        self.assertTrue(all(r.status == 'ok' for r in results))
        self.assertEqual(len({(r.start,r.end) for r in results}),1)

    def test_reject_merged_object_and_suppress_range(self):
        f,ref,seed = setup_signal(tone(duration=.25))
        r = candidate(f,seed,ref,26,excluded_seeds=(3200,))
        self.assertEqual(r.status,'merged_candidate')
        self.assertIsNone(r.duration_ms)
        summary = sensitivity([candidate(f,seed,ref,14),r])
        self.assertEqual(summary['status'],'indeterminate')
        self.assertIsNone(summary['range_ms'])

    def test_periodicity_really_qualifies_candidate(self):
        f,ref,seed = setup_signal(tone())
        f.per[:] = 0
        r = candidate(f,seed,ref,20)
        self.assertEqual(r.status,'seed_not_periodic')
        self.assertIsNone(r.duration_ms)
        f,ref,seed = setup_signal(tone())
        f.p_rms[:] = 0
        self.assertEqual(candidate(f,seed,ref,20).status,'seed_not_periodic')

    def test_sample_rate_and_f0(self):
        for sr in (8000,11025,16000):
            for hz in (80,160,300):
                f,ref,seed = setup_signal(tone(sr=sr,hz=hz),sr)
                r = candidate(f,seed,ref,20)
                self.assertEqual(r.status,'ok')
                self.assertLess(abs(r.duration_ms-120),10)

    def test_insufficient_reference_and_invalid_input(self):
        f = Features(tone())
        self.assertEqual(reference_level(f,0,100)[0],None)
        self.assertEqual(candidate(f,1600,None,20).status,'insufficient_reference')
        with self.assertRaises(ValueError):
            Features([float('nan')])


class EngineTests(unittest.TestCase):
    def test_metadata_names_current_and_original_timelines(self):
        for enabled in (False,True):
            with patch.object(S,'WORD_BUDGET',enabled):
                for morae in (['ba','pe'],['di','te'],['ga',':','ke'],['a','Q','ka']):
                    frames,ends,q = S.build(morae)
                    for s,end in zip(S.LAST_SPANS,ends):
                        self.assertEqual(s['start'],s['final_start'])
                        self.assertEqual(s['steady'],s['final_steady'])
                        self.assertEqual(s['len'],s['final_len'])
                        self.assertEqual(s['final_len'],s['original_len']+s['give'])
                        self.assertEqual(s['final_end'],round(end*100))
                        self.assertLessEqual(s['final_start'],s['final_end'])
                        self.assertLessEqual(s['final_end'],len(frames))

    def test_controlled_arrays_and_pcm_for_every_delta(self):
        for v in 'ieaou':
            morae = ['b'+v,'po' if v == 'e' else 'pe']
            for n in L.DELTAS:
                a,b,at = L.controlled_frames(morae,0,n)
                L.verify_insertion(a,b,at,n)
                x,y,na,nb = L.controlled_pair(morae,0,n)
                self.assertEqual(nb-na,n)
                self.assertEqual(len(y)-len(x),n*160)
                if n == 0:
                    np.testing.assert_array_equal(x,y)

    def test_stimulus_verifier_catches_unrelated_change(self):
        a,b,at = L.controlled_frames(['ba','pe'],0,2)
        b = [list(f) for f in b]
        b[-1][17] += 1
        with self.assertRaises(AssertionError):
            L.verify_insertion(a,b,at,2)

    def test_control_flags_restored_on_failure(self):
        previous = S.NO_COMPENSATE,S.VOWEL_ALLOC
        with patch.object(S,'build',side_effect=RuntimeError('intentional')):
            with self.assertRaises(RuntimeError):
                L.controlled_frames(['ba','pe'],0,1)
        self.assertEqual((S.NO_COMPENSATE,S.VOWEL_ALLOC),previous)

    def test_raw_audio_preserves_origin_default_preserves_processing(self):
        frames,_,_ = S.build(['ba','pe'])
        raw = S.render(frames,postprocess=False)
        self.assertTrue(np.all(raw[:S.raw_frame_sample(0)] == 0))
        expected = raw.astype(float)
        nz = np.nonzero(abs(expected)>20)[0]
        expected = expected[max(0,nz[0]-160):nz[-1]+320]
        if len(expected)>500:
            expected[:160] *= np.linspace(0,1,160)
            expected[-320:] *= np.linspace(1,0,320)
        np.testing.assert_array_equal(S.render(frames),np.clip(expected,-32768,32767).astype(np.int16))

    def test_reference_cannot_spill_into_following_vowel(self):
        frames,_,_ = S.build(['bi','pe'])
        st = steady_span(frames,S.LAST_SPANS,0)
        if st:
            self.assertLessEqual(st[1],S.LAST_SPANS[0]['final_end'])

    def test_real_pcm_selection_and_padding(self):
        frames,ends,q = S.build(['ba','pe'])
        span = steady_span(frames,S.LAST_SPANS)
        S.pitch(frames,ends,1,q_ends=q,morae=['ba','pe'])
        x = S.render(frames,postprocess=False)
        lo,hi = S.raw_frame_sample(span[0]+1),S.raw_frame_sample(span[1]-1)
        f = Features(x,S.SR)
        ref,_ = reference_level(f,lo,hi)
        seed = (lo+hi)//2
        baseline = candidate(f,seed,ref,20)
        self.assertEqual(baseline.status,'ok')
        for end in (baseline.end+160,baseline.end+320,len(x)):
            r = candidate(f,seed,ref,20,region=(0,end))
            self.assertEqual((r.start,r.end),(baseline.start,baseline.end))
        padded = Features(np.r_[x,np.zeros(1600)],S.SR)
        r = candidate(padded,seed,ref,20)
        self.assertEqual((r.start,r.end),(baseline.start,baseline.end))

    def test_existing_output_is_never_overwritten(self):
        with self.assertRaises(FileExistsError):
            L.main(['--out',str(ROOT/'tests')])


class GeminateFricative(unittest.TestCase):
    """A geminated fricative is long frication, not a closure.

    A geminate STOP is a silence -- that is the closure.  A geminate FRICATIVE
    has none: /ʃː/ is one long noise.  Rendering it as silence plus a
    singleton's worth of frication builds the signature of an affricate, which
    is how スラッシュ came to be heard as "surachu".
    """

    WORDS = ['su-ra-Q-shu', 'za-Q-shi', 'i-Q-sho', 'ma-Q-su-gu']
    SOUNDING = (0, 2, 8)

    def _render(self, spec, fric_geminate):
        morae = spec.split('-')
        old = S.FRIC_GEMINATE
        S.FRIC_GEMINATE = fric_geminate
        try:
            return S.build(list(morae)) + (morae,)
        finally:
            S.FRIC_GEMINATE = old

    def _silent_in_geminate(self, spec, fric_geminate):
        fr, ends, _q, morae = self._render(spec, fric_geminate)
        n = 0
        for i, mo in enumerate(morae[:-1]):
            if mo != 'Q':
                continue
            a = int(round(ends[i - 1] * 100)) if i else 0
            b = min(int(round(ends[i] * 100)), len(fr))
            n += sum(1 for k in range(a, b)
                     if all(fr[k][t] == 0 for t in self.SOUNDING))
        return n

    def test_no_silence_inside_a_geminated_fricative(self):
        for spec in self.WORDS:
            self.assertEqual(self._silent_in_geminate(spec, True), 0, spec)

    def test_the_check_catches_the_closure_it_replaced(self):
        for spec in self.WORDS:
            self.assertGreater(self._silent_in_geminate(spec, False), 0, spec)

    def test_a_geminated_stop_is_still_a_silent_closure(self):
        # the contrast this must not break: /kk/ keeps its closure
        self.assertGreater(self._silent_in_geminate('ga-Q-ko-u', True), 0)

    def test_total_duration_is_unchanged(self):
        for spec in self.WORDS:
            a = self._render(spec, False)
            b = self._render(spec, True)
            self.assertEqual(len(a[0]), len(b[0]), spec)
            self.assertEqual([round(x, 4) for x in a[1]],
                             [round(x, 4) for x in b[1]], spec)


if __name__ == '__main__':
    unittest.main(verbosity=2)
