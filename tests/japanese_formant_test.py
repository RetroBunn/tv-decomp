"""Source and implementation checks for Japanese vowel targets.

python -B tests/japanese_formant_test.py
Source checks use the downloaded primary data in build/check/formant_audit;
C checks use the same test-only DLL as japanese_timing_test.py.
"""
import ctypes as C
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT/'tools'))
from ja_formant_audit import ETL, YAZAWA, source_values, quantized, J
from japanese_timing_test import CFront

CONSONANTS = ('', 'k', 'ky', 'g', 'gy', 't', 'd', 'p', 'py', 'b', 'by', 'v',
              's', 'sh', 'z', 'j', 'ch', 'ts', 'h', 'hy', 'f', 'n', 'ny',
              'm', 'my', 'r', 'ry', 'w', 'y', 'N', 'N_n', 'N_m', 'N_k', 'N_q')


class FormantTargets(unittest.TestCase):
    def test_source_calculations(self):
        if not ETL.exists() or not YAZAWA.exists():
            self.skipTest('Primary formant data not downloaded')
        med, contexts, _, delta = source_values()
        for v in 'aiueo':
            self.assertEqual(tuple(int(x) for x in med[v][:7]), J.V[v])
            self.assertEqual(tuple(delta[v]), J.LONG_DELTA[v])
        self.assertEqual(contexts['front']['frames'], 450)
        self.assertEqual(contexts['back']['frames'], 100)
        self.assertEqual(round(contexts['front']['median_f2_hz']), J.U_FRONT)
        self.assertEqual(round(contexts['back']['median_f2_hz']), J.U_BACK)

    def test_sustained_reference_is_the_long_anchor(self):
        for v in 'aiueo':
            self.assertEqual(J.vowel(v, long=True), J.V[v])

    def test_short_targets_and_length_contrast(self):
        expected = {'a': (680, 1271, 2219), 'i': (293, 1928, 2806),
                    'u': (352, 1286, 2259), 'e': (464, 1778, 2334),
                    'o': (463, 992, 2259)}
        for v in 'aiueo':
            self.assertEqual(J.vowel(v)[:3], expected[v])
            self.assertEqual(J.vowel(v)[3:], J.vowel(v, long=True)[3:])

    def test_u_context_is_explicit_and_palatal_onsets_front(self):
        for c in ('', 'h', 'n', 'r', 'unknown'):
            self.assertEqual(J.vowel('u', c, True)[1], 1293, c)
        for c in ('k', 'b', 'm', 'g', 'p', 'f', 'v'):
            self.assertEqual(J.vowel('u', c, True)[1], 1087, c)
        for c in ('s', 'sh', 'y', 'ky', 'py', 'by', 'my', 'hy', 'ry'):
            self.assertEqual(J.vowel('u', c, True)[1], 1318, c)

    def test_default_voice_can_encode_all_targets_without_clamping(self):
        for v in 'aiueo':
            for c in CONSONANTS:
                for long in (False, True):
                    post = J.vowel(v, c, long)
                    for want, actual, halfstep in zip(post, quantized(post), (2, 4, 8, 8, 1, 1, 1)):
                        self.assertLessEqual(abs(want-actual), halfstep, (v, c, long))

    def test_c_python_postures_and_dependent_flap_loci(self):
        path = ROOT/'build/check/ja_timing.dll'
        if not path.exists():
            self.skipTest('Build ja_timing.dll first')
        d = CFront(path).dll
        d.ja_vowel_post.argtypes = [C.c_int, C.c_int, C.c_int, C.POINTER(C.c_double)]
        p = (C.c_double * 7)()
        for vi, v in enumerate('aiueo'):
            for ci, c in enumerate(CONSONANTS):
                for long in (False, True):
                    self.assertEqual(d.ja_vowel_post(vi, ci, int(long), p), 1)
                    self.assertEqual(tuple(p), J.vowel(v, c, long), (v, c, long))
        loci = ((C.c_short * 5) * len(CONSONANTS)).in_dll(d, 'ja_LOCUS')
        for c in ('r', 'ry'):
            self.assertEqual(list(loci[CONSONANTS.index(c)]), [J.LOCUS[c][v] for v in 'aiueo'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
