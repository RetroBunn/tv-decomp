"""Check Japanese voice selection against the explicit parameter-frame path.

Run after building: python tests/japanese_api_test.py
The Python frontend is the existing C frontend oracle. Rendering its frames
with the named stock voice checks the API's roster-to-engine mapping, including
switching voices on an existing synthesizer as NVDA does.
"""
import ctypes as C
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build' / 'Japanese_test'))
import jp_dict as D
import jp_speak as S


class Event(C.Structure):
    _fields_ = [('type', C.c_int32), ('count', C.c_uint32),
                ('samples', C.POINTER(C.c_int16)), ('mark', C.c_uint32),
                ('sample_pos', C.c_uint32)]


Callback = C.CFUNCTYPE(C.c_int, C.POINTER(Event), C.c_void_p)
# Japanese name -> stock engine voice and measured trims (11 kHz, 16 kHz).
# name: (engine voice, source trim at 11025, at 16000).  A copy of the table in
# lang/jpn/port/tvtts_ja.c, deliberately: what this checks is that the Japanese path
# differs from the explicit frame path by exactly the documented trim and by
# nothing else.
#
# Osamu's 16 kHz entry was 8 and is now 0.  All eight of those decibels were
# compensating for an out-of-bounds read: g_synhifi_8 reached 5592 Hz and
# Melvin, the voice Osamu uses, sets its ceiling at 5970.  With the table
# extended to the Nyquist endpoint the voice clips nothing at 16 kHz with no
# trim at all, measured over 35 texts.  The 11 kHz column is untouched because
# 11 kHz never had the overrun -- its Nyquist is below where the table ended.
VOICES = {'Taro': (0, 2, 0), 'Tsuyoshi': (1, 0, 0), 'Kenta': (2, 0, 0),
          'Daichi': (3, 0, 0), 'Takeshi': (4, 0, 0), 'Ojiisan': (5, 1, 0),
          'Osamu': (6, 4, 0), 'Akira': (7, 8, 0),
          'Hanako': (8, 0, 0), 'Keiko': (9, 0, 0)}


class JapaneseVoiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        d = C.CDLL(str(ROOT / 'build' / 'bin' / 'tvtts64.dll'))
        d.tvtts_create_lang.argtypes = [C.c_uint32, C.c_char_p]
        d.tvtts_create_lang.restype = C.c_void_p
        d.tvtts_destroy.argtypes = [C.c_void_p]
        for name in ('voice_name', 'voice_language'):
            f = getattr(d, 'tvtts_' + name)
            f.argtypes = [C.c_int]
            f.restype = C.c_char_p
        for name in ('set_voice', 'set_pitch', 'set_rate'):
            getattr(d, 'tvtts_' + name).argtypes = [C.c_void_p, C.c_int]
        d.tvtts_speak_bytes.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32,
                                        Callback, C.c_void_p]
        d.tvtts_speak_frames.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32,
                                         Callback, C.c_void_p]
        cls.d = d
        cls.voices = {d.tvtts_voice_name(v).decode(): v
                      for v in range(d.tvtts_voice_count())
                      if d.tvtts_voice_language(v) == b'ja'}

    def collect(self, fn, synth, data, count):
        out = bytearray()

        @Callback
        def callback(ev, _):
            if ev[0].type == 0 and ev[0].count:
                out.extend(C.string_at(ev[0].samples, ev[0].count * 2))
            return 0

        self.assertEqual(fn(synth, data, count, callback, None), 0)
        self.assertTrue(out)
        return bytes(out)

    def test_named_voices_and_switching(self):
        self.assertEqual(list(self.voices), list(VOICES))
        # These feed ROMAJI on purpose -- they are probing the frame builder
        # and the voice blocks, not the router -- so they ask for the romaji
        # reading.  Without the flag a romaji probe goes through the English
        # rules instead and `aoiueo` is read as five letter names.
        old_ext = self.d.tvtts_get_extensions()
        self.d.tvtts_set_extensions(0xffffffff)

        # Voiced vowels must retain the stock voice unchanged. In particular,
        # Kenta's noise correction must not become a whole-voice attenuation.
        text = 'aoiueo'
        old_sr = S.SR
        try:
            for sr in (11025, 16000):
                S.SR = sr
                morae = S.M.to_morae(text)
                frames, ends, q_ends = S.build(morae)
                S.pitch(frames, ends, 0, q_ends=q_ends, morae=morae)
                ja = self.d.tvtts_create_lang(sr, b'ja')
                self.assertTrue(ja)
                try:
                    self.d.tvtts_set_rate(ja, 150)
                    self.d.tvtts_set_pitch(ja, 85)
                    # Return to Taro after switching through the entire roster.
                    for name in list(VOICES) + ['Taro']:
                        with self.subTest(sr=sr, voice=name):
                            engine, trim11, trim16 = VOICES[name]
                            trim = trim16 if sr == 16000 else trim11
                            ref = [f[:] for f in frames]
                            for f in ref:
                                for k in range(3):
                                    f[k] = max(0, f[k] - trim)
                                f[21] = (f[21] & 15) | (engine << 4)
                            raw = bytes(v for f in ref for v in f)
                            buf = C.create_string_buffer(raw)
                            en = self.d.tvtts_create_lang(sr, b'en')
                            self.assertTrue(en)
                            try:
                                self.d.tvtts_set_voice(en, engine)
                                expected = self.collect(self.d.tvtts_speak_frames,
                                                        en, buf, len(ref))
                            finally:
                                self.d.tvtts_destroy(en)
                            self.d.tvtts_set_voice(ja, self.voices[name])
                            actual = self.collect(self.d.tvtts_speak_bytes, ja,
                                                  text.encode(), len(text))
                            self.assertEqual(actual == expected, True,
                                             'Japanese PCM differs from its named engine voice')
                finally:
                    self.d.tvtts_destroy(ja)
        finally:
            S.SR = old_sr
            self.d.tvtts_set_extensions(old_ext)

    def test_kanji_through_the_library(self):
        """The shipped DLL reads kanji, and reads it the way the Python does.

        This is the end-to-end test of the analyser port.  Before it, the
        library dropped every kanji and said only the particles, so
        `私は日本語を話します` reached the synthesiser as `はをします`; the
        analyser was Python only and nothing in it reached a user.

        The comparison is the same one the voice test makes: the Python front
        end's frames rendered through tvtts_speak_frames, against the same
        text spoken through tvtts_speak_bytes.  If the C analyser read the
        text differently -- a different reading, a different accent phrase, a
        different devoiced mora -- the PCM would differ, because all three
        change the frames.

        Skipped rather than failed when data/ja/jadic.bin is absent: without
        it the library is meant to fall back to the kana path, which is what
        the rest of this file tests.
        """
        import jp_front as F
        if not (ROOT / 'data' / 'ja' / 'jadic.bin').is_file():
            self.skipTest('data/ja/jadic.bin is not built')
        texts = ['私は日本語を話します。', '値段は1,250円です。',
                 '箸が', '橋が', '端が',
                 '明日の会議は何時からですか？', '一粒', '二十日']
        old_sr = S.SR
        try:
            for sr in (11025, 16000):
                S.SR = sr
                ja = self.d.tvtts_create_lang(sr, b'ja')
                self.assertTrue(ja)
                try:
                    self.d.tvtts_set_rate(ja, 150)
                    self.d.tvtts_set_pitch(ja, 85)
                    self.d.tvtts_set_voice(ja, self.voices['Taro'])
                    engine, trim11, _ = VOICES['Taro']
                    trim = 0 if sr == 16000 else trim11
                    for text in texts:
                        with self.subTest(sr=sr, text=text):
                            got = F.frames(text)
                            self.assertIsNotNone(
                                got, 'the Python analyser said nothing')
                            frames = got[4]
                            ref = [f[:] for f in frames]
                            for f in ref:
                                for k in range(3):
                                    f[k] = max(0, f[k] - trim)
                                f[21] = (f[21] & 15) | (engine << 4)
                            raw = bytes(v for f in ref for v in f)
                            buf = C.create_string_buffer(raw)
                            en = self.d.tvtts_create_lang(sr, b'en')
                            self.assertTrue(en)
                            try:
                                self.d.tvtts_set_voice(en, engine)
                                expected = self.collect(
                                    self.d.tvtts_speak_frames, en, buf,
                                    len(ref))
                            finally:
                                self.d.tvtts_destroy(en)
                            data = text.encode('utf-8')
                            actual = self.collect(self.d.tvtts_speak_bytes,
                                                  ja, data, len(data))
                            self.assertEqual(
                                actual, expected,
                                'the library read the kanji differently from '
                                'the Python analyser')
                finally:
                    self.d.tvtts_destroy(ja)
        finally:
            S.SR = old_sr

    def test_latin_input_routing_loses_no_letters(self):
        """A Latin string is read as romaji only if the parser consumes it all.

        "All-ASCII letters" is a test for the Latin alphabet, not for romaji,
        and the mora parser is lenient by design -- the kana path feeds it
        generated romaji and it must not reject a stray mark.  So it accepted
        every English word and quietly dropped the letters Japanese has no
        mora for: `hello` was /he.Q.o/, `computer` was /o.pu.te/ with the c, m
        and r gone, and `blorf` was the single mora /o/.

        The check is behavioural rather than a reading comparison: a word that
        consumes whole must produce exactly the romaji frames, and a word that
        does not must produce the analyser's reading, which says all of the
        input.  `Windows` is the case that proves the two agree now -- it used
        to be /wi.N.do/ alone and ウィンドーズ with any Japanese after it.
        """
        import jp_front as F
        if not (ROOT / 'data' / 'ja' / 'jadic.bin').is_file():
            self.skipTest('data/ja/jadic.bin is not built')
        # The three steps, in the order the router tries them.  The lexicon
        # group is the one the measurement forced: every Japanese system reads
        # a bare `take` as English テイク, and trying romaji first got 107 of
        # the 840 Latin words naist-jdic knows wrong.  `DATE` is `take`'s case
        # with a word we have, and `Apple` is one where romaji was also lossy.
        lexicon = ['DATE', 'AU', 'ASEAN', 'Apple', 'Windows', 'Amazon']
        romaji = ['sakura', 'konnichiwa', 'take', 'aoiueo',
                  'watashiwa nihongo o hanashimasu']
        # And the per-token cases, which the utterance-level decision
        # got wrong: one known word used to suppress the romaji reading
        # of every other, so this said the Japanese names of
        # konnichiwa's ten letters.
        mixed = ['Windows konnichiwa', 'konnichiwa Windows',
                 'sakura Amazon']
        spelled = ['hello', 'computer', 'blorf', 'NVDA', 'qzxv',
                   'xylophone', 'frobnic']
        old_sr = S.SR
        old_ext = self.d.tvtts_get_extensions()
        try:
            S.SR = 11025
            ja = self.d.tvtts_create_lang(11025, b'ja')
            self.assertTrue(ja)
            try:
                self.d.tvtts_set_rate(ja, 150)
                self.d.tvtts_set_pitch(ja, 85)
                self.d.tvtts_set_voice(ja, self.voices['Taro'])
                engine, trim11, _ = VOICES['Taro']
                for text, path in ([(t, 'lexicon') for t in lexicon]
                                   + [(t, 'romaji') for t in romaji]
                                   + [(t, 'spelled') for t in spelled]
                                   + [(t, 'mixed') for t in mixed]):
                    # The romaji reading is a CHOICE and lives behind
                    # TVTTS_EXT_JA_ROMAJI, off by default: a bare `take` is
                    # テイク unless a caller asks for タケ.  So the romaji
                    # group is checked with the flag on and the rest without
                    # it, which is also the test that the flag does anything.
                    self.d.tvtts_set_extensions(
                        0xffffffff if path == 'romaji' else old_ext)
                    with self.subTest(text=text, path=path):
                        if path == 'romaji':
                            morae = S.M.to_morae(text)
                            frames, ends, q_ends = S.build(morae)
                            S.pitch(frames, ends, 0, q_ends=q_ends,
                                    morae=morae)
                        else:
                            got = F.frames(text)
                            self.assertIsNotNone(got)
                            frames = got[4]
                            if path == 'mixed':
                                # every Latin token gets a reading of its own,
                                # which is what the per-utterance decision got
                                # wrong: one known word used to suppress the
                                # reading of every other
                                self.assertNotIn(
                                    'e nu', list(got[1]),
                                    '%r still spells a token out'
                                    % text)
                            elif path == 'lexicon':
                                # the dictionary really knew it, which is what
                                # the router tests before trying romaji
                                self.assertGreater(
                                    F.analyse(text)[0][0].mora_size, 0,
                                    '%r has no dictionary reading, so it '
                                    'cannot test the lexicon step' % text)
                            else:
                                # It must not be the romaji parser's reading.
                                # A mora count cannot tell a spell-out from a
                                # word, so the test is that the two readings
                                # differ at all -- which is what the defect
                                # made them not do.
                                lossy = S.M.to_morae(text)
                                self.assertNotEqual(
                                    list(got[1]), list(lossy),
                                    '%r was read with the romaji parser, '
                                    'which drops the letters it has no mora '
                                    'for' % text)
                        ref = [f[:] for f in frames]
                        for f in ref:
                            for k in range(3):
                                f[k] = max(0, f[k] - trim11)
                            f[21] = (f[21] & 15) | (engine << 4)
                        raw = bytes(v for f in ref for v in f)
                        buf = C.create_string_buffer(raw)
                        en = self.d.tvtts_create_lang(11025, b'en')
                        self.assertTrue(en)
                        try:
                            self.d.tvtts_set_voice(en, engine)
                            expected = self.collect(
                                self.d.tvtts_speak_frames, en, buf, len(ref))
                        finally:
                            self.d.tvtts_destroy(en)
                        data = text.encode('utf-8')
                        actual = self.collect(self.d.tvtts_speak_bytes, ja,
                                              data, len(data))
                        self.assertEqual(
                            actual, expected,
                            '%r should take the %s path' % (text, path))
            finally:
                self.d.tvtts_destroy(ja)
        finally:
            S.SR = old_sr
            self.d.tvtts_set_extensions(old_ext)

    def test_latin_word_rules_through_the_library(self):
        """An unfamiliar Latin word gets a pronunciation, not its letters.

        This is the end-to-end test of the Latin-word rules: the shipped DLL
        against jp_g2p's reading of the same word, rendered through
        tvtts_speak_frames.  If the C adaptation differed from the Python's
        anywhere -- a vowel, an inserted mora, a geminate, the accent -- the
        PCM would differ, because all of those change the frames.

        `computer` is the case worth naming: it used to come out as the
        Japanese names of its eight letters.
        """
        import jp_g2p as GP
        import jp_front as F
        import jp_njd as NJ
        if not (ROOT / 'data' / 'ja' / 'jadic.bin').is_file():
            self.skipTest('data/ja/jadic.bin is not built')
        words = ['computer', 'blorf', 'zindle', 'frobnic', 'kludge',
                 'mouse', 'fire', 'test', 'cat', 'music']
        try:
            phon = GP.phonemes_of(words)
        except Exception as e:                      # pragma: no cover
            self.skipTest('the English front end is unavailable: %s' % e)
        old_sr = S.SR
        try:
            S.SR = 11025
            ja = self.d.tvtts_create_lang(11025, b'ja')
            self.assertTrue(ja)
            try:
                self.d.tvtts_set_rate(ja, 150)
                self.d.tvtts_set_pitch(ja, 85)
                self.d.tvtts_set_voice(ja, self.voices['Taro'])
                engine, trim11, _ = VOICES['Taro']
                for w in words:
                    with self.subTest(word=w):
                        morae, acc, how = GP.read(w, phon[w])
                        self.assertEqual(how, 'word',
                                         '%r was not read as a word' % w)
                        self.assertTrue(morae)
                        # it must NOT be the spelled-out reading
                        spelled = NJ.unknown_pron(D.normalize(w))[0]
                        self.assertNotEqual(
                            u''.join(NJ._inverse().get(m, '?')
                                     for m in morae), spelled,
                            '%r was spelled out' % w)
                        # through the WHOLE pipeline, because the library runs
                        # the accent stages after the rules and so must this
                        got = F.frames(w)
                        self.assertIsNotNone(got)
                        frames = got[4]
                        ref = [f[:] for f in frames]
                        for f in ref:
                            for k in range(3):
                                f[k] = max(0, f[k] - trim11)
                            f[21] = (f[21] & 15) | (engine << 4)
                        raw = bytes(v for f in ref for v in f)
                        buf = C.create_string_buffer(raw)
                        en = self.d.tvtts_create_lang(11025, b'en')
                        self.assertTrue(en)
                        try:
                            self.d.tvtts_set_voice(en, engine)
                            expected = self.collect(
                                self.d.tvtts_speak_frames, en, buf, len(ref))
                        finally:
                            self.d.tvtts_destroy(en)
                        data = w.encode('utf-8')
                        actual = self.collect(self.d.tvtts_speak_bytes, ja,
                                              data, len(data))
                        self.assertEqual(
                            actual, expected,
                            'the library read %r differently from jp_g2p' % w)
            finally:
                self.d.tvtts_destroy(ja)
        finally:
            S.SR = old_sr

    def test_calibrated_voice_output_headroom(self):
        # Default-pitch speech through the same API used by NVDA. Include the
        # short-release contexts whose ringing clipped AFTER noise switched
        # off, so checking only scheduled noise frames would miss the defect.
        import numpy as np
        # These feed ROMAJI on purpose -- they are probing the frame builder
        # and the voice blocks, not the router -- so they ask for the romaji
        # reading.  Without the flag a romaji probe goes through the English
        # rules instead and `aoiueo` is read as five letter names.
        old_ext = self.d.tvtts_get_extensions()
        self.d.tvtts_set_extensions(0xffffffff)
        try:
            for sr, name in ((sr, name) for sr in (11025, 16000)
                             for name in ('Tsuyoshi', 'Kenta', 'Hanako',
                                          'Keiko')):
                ja = self.d.tvtts_create_lang(sr, b'ja')
                self.assertTrue(ja)
                try:
                    voice = self.voices[name]
                    self.d.tvtts_set_voice(ja, voice)
                    self.d.tvtts_set_pitch(ja,
                                           self.d.tvtts_voice_pitch(voice))
                    for rate in (90, 150, 300):
                        self.d.tvtts_set_rate(ja, rate)
                        for word in ('ki', 'ke', 'kya', 'kyu', 'kyo',
                                     'akiya', 'akyea', 'apyua', 'aQkyaa',
                                     'aQkyua', 'sashisuseso', 'tsui',
                                     'chichi', 'kyuu'):
                            with self.subTest(voice=name, sr=sr, rate=rate,
                                              word=word):
                                pcm = self.collect(self.d.tvtts_speak_bytes,
                                                   ja, word.encode(),
                                                   len(word))
                                x = np.frombuffer(
                                    pcm, dtype=np.int16).astype(np.int32)
                                self.assertLess(int(np.max(np.abs(x))), 32767)
                                self.assertGreater(int(np.max(np.abs(x))),
                                                   1000)
                finally:
                    self.d.tvtts_destroy(ja)
        finally:
            self.d.tvtts_set_extensions(old_ext)

    def test_an_inline_escape_is_consumed_and_applied(self):
        """NVDA announces a capital as ESC[<n>p then the letter.

        Nothing on this path consumed the engine's escapes, so the mora parser
        dropped the ESC and the '[' as characters it could not place and read
        what was left: ESC[50pA came out as "fifty P A".  The pitch is applied
        to this utterance only -- the driver drops the closing PitchCommand,
        having nothing left to apply it to, so a change that persisted would
        never be undone.
        """
        buf = C.create_string_buffer(32)
        n = self.d.tvtts_pitch_sequence(buf, 32, 300)
        self.assertGreater(n, 0)
        seq = buf.raw[:n]
        self.d.tvtts_get_pitch.argtypes = [C.c_void_p]
        for sr in (11025,):
            ja = self.d.tvtts_create_lang(sr, b'ja')
            self.assertTrue(ja)
            try:
                self.d.tvtts_set_voice(ja, self.voices['Taro'])
                before = self.d.tvtts_get_pitch(ja)
                plain = self.collect(self.d.tvtts_speak_bytes, ja,
                                     'あ'.encode(), len('あ'.encode()))
                raised = self.collect(self.d.tvtts_speak_bytes, ja,
                                      seq + 'あ'.encode(),
                                      len(seq + 'あ'.encode()))
                # consumed: the escape adds no speech of its own
                self.assertEqual(len(raised), len(plain))
                # applied: a different pitch is a different waveform
                self.assertNotEqual(raised, plain)
                # and it did not stick to the synthesiser
                self.assertEqual(self.d.tvtts_get_pitch(ja), before)
            finally:
                self.d.tvtts_destroy(ja)

    def test_an_utterance_ends_on_silence(self):
        """A word ending in a vowel used to stop mid-cycle and click."""
        import struct
        for word in ('あ', 'おはよう', 'こんにちは', 'さくら'):
            ja = self.d.tvtts_create_lang(11025, b'ja')
            self.assertTrue(ja)
            try:
                self.d.tvtts_set_voice(ja, self.voices['Taro'])
                b = word.encode()
                pcm = self.collect(self.d.tvtts_speak_bytes, ja, b, len(b))
            finally:
                self.d.tvtts_destroy(ja)
            x = struct.unpack('<%dh' % (len(pcm) // 2), pcm)
            self.assertEqual(x[-1], 0, word)


if __name__ == '__main__':
    unittest.main()
