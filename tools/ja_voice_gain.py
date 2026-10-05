# -*- coding: utf-8 -*-
"""How hard each Japanese voice drives the filter bank, and what to back off.

    python tools/ja_voice_gain.py [--target 29000] [--words 120]

WHY THIS IS NEEDED.  A Japanese voice is one of Centigram's ten parameter
blocks applied to frames this project wrote: Synth_Frame reads the voice from
the high nibble of track 21 and scales F1-F4 and B1-B3 by that voice's seven
percentages (docs/VOICES.md).  Those percentages were chosen for the levels
STAGE 3 produces, and the Japanese front end is not stage 3 -- its track 0 sits
at 60 and its noise tracks at 70-80, fitted by ear on voice 0, whose seven
percentages are all zero.

Give those same levels to a voice that raises its formants and the filter bank
is driven past full scale.  Measured before any trim: Hanako clipped 231
samples in one sentence with a sample-to-sample step of 65535, which is a full-
scale discontinuity and is heard as a click, and Tsuyoshi clipped 162.  That is
not a voice that needs taste applied to it; it is a broken voice.

WHAT IS MEASURED.  The attenuation, in whole decibels through the engine's own
volume path -- which subtracts from tracks 0, 1 and 2, exactly as stage 3 does
-- that brings each voice's peak under `--target` across the corpus.  It is a
SEARCH and not a calculation, for the reason the noise-rate trim had to be one
too: a track unit is not a decibel, and the tracks saturate.

WHAT THE NUMBER MEANS, PRECISELY.  It speaks through tvtts_speak_bytes on the
Japanese language, so the library's OWN per-voice trim -- the table in
ja_port/tvtts_ja.c -- is already applied, and what is reported is the
attenuation needed ON TOP OF IT.  So a row of zeros is the answer "the shipped
table is still enough", which is the question worth asking after anything
changes the Japanese levels; it is not a re-derivation of the table from
nothing.  Deriving it from nothing is what the table's original measurement
did, with the trims at zero, and doing that again means editing them out first.

Re-run this after anything that changes the levels -- a new noise posture, a
different vowel amplitude, or the ANALYSER, which produces different frames
from the same sounds because a lexical accent moves the F0 contour and lexical
devoicing switches the source on different morae.  Measured over the corpus
below including its kanji half (2026-10-05): every voice clean at 0 dB extra
at both 11025 and 16000 Hz, the worst peak 22,784 of a 32,767 full scale and
no clipped sample anywhere.
"""
import argparse, ctypes, io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# src/engine/volume.c: the largest volume whose attenuation is still at least
# k, for k from -48.  So g_atten_bound[k + 48] is a volume that yields exactly
# attenuation k, which is how an attenuation is requested here.
ATTEN_BOUND = [
    0xffffffff, 0xf676c58d, 0xc3c5f567, 0x9b821c0e, 0x7b864b48, 0x621e7b24,
    0x4df05196, 0x3de8b0a9, 0x312d0fe0, 0x270fd8ef, 0x1f072940, 0x18a57a27,
    0x1393cbbd, 0x0f8d02ce, 0x0c5a3aba, 0x09cfd91d, 0x07cb3b5b, 0x0630de77,
    0x04eae7fc, 0x03e7fc17, 0x031a50ec, 0x0276f29d, 0x01f52df9, 0x018e19e1,
    0x013c3912, 0x00fb2f4f, 0x00c785ef, 0x009e7ca5, 0x007de3ff, 0x0063ff9b,
    0x004f6e7e, 0x003f1842, 0x00321e32, 0x0027cf63, 0x001f9f4e, 0x00191e54,
    0x0013f3cb, 0x000fd943, 0x000c96cc, 0x0009fff5, 0x0007f173, 0x00064f39,
    0x00050305, 0x0003fb23, 0x00032987, 0x00028308, 0x0001fec7, 0x000195b9,
    0x00014247, 0x0000cb58, 0x0000a185, 0x0000804d, 0x000065e9, 0x000050f3,
    0x0000404d, 0x00003313, 0x00002892, 0x0000203a, 0x00001999, 0x00001455,
    0x00001026, 0x00000cd4, 0x00000a30, 0x00000818,
]
ATTEN_MIN = -48

WORDS = [
    u'こんにちは', u'ありがとうございます', u'おはようございます',
    u'さようなら', u'がっこう', u'おばあさん', u'ふうふ', u'さくら',
    u'しんぶん', u'ぎんこう', u'とうきょう', u'でんわ', u'きっぷ',
    u'ざっし', u'しゃしん', u'つつじ', u'りょこう', u'しゅくだい',
    u'あおい', u'かえる', u'すうがく', u'とおる', u'びいる', u'くうき',
    u'ふとん', u'ふあん', u'ふく', u'ふね', u'とうふ', u'おじいさん',
    u'わたしは にほんごを はなします',
    u'きょうは いいてんきですね',
    u'でんしゃが えきに とうちゃくしました',
    u'そのほんを よんでください',
    u'あしたの かいぎは なんじからですか',
    # And the analyser's own path, which produces DIFFERENT FRAMES from the
    # same sounds: a lexical accent moves the F0 contour, and lexical
    # devoicing switches the source on morae the string-level rule left
    # voiced and leaves voiced ones it would have whispered.  The trims below
    # were measured before the analyser existed, on kana alone, so a corpus
    # without kanji would no longer be measuring what ships.
    u'私は日本語を話します。',
    u'値段は1,250円です。',
    u'会議は5月3日の午後2時30分からです。',
    u'明日の会議は何時からですか？',
    u'複数の美しさ。学校に行きました。',
    u'電話番号は03-1234-5678です。',
    u'一粒、二口、一通り、一人、二十日。',
    u'箸が。橋が。端が。',
    u'東京都の人口は増えています。',
    u'友達と一緒に図書館で勉強しました。',
    u'日本語学校の先生はとても親切です。',
    u'田中さんは大阪から来ました。',
]


class EV(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int32), ("count", ctypes.c_uint32),
                ("samples", ctypes.POINTER(ctypes.c_int16)),
                ("mark", ctypes.c_uint32), ("sample_pos", ctypes.c_uint32)]


CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(EV), ctypes.c_void_p)


class Lib(object):
    def __init__(self, path):
        os.add_dll_directory(os.path.dirname(os.path.abspath(path)))
        self.d = ctypes.CDLL(os.path.abspath(path))
        self.d.tvtts_create_lang.restype = ctypes.c_void_p
        self.d.tvtts_voice_name.restype = ctypes.c_char_p
        self.d.tvtts_voice_language.restype = ctypes.c_char_p

    def ja_voices(self):
        out = []
        for v in range(self.d.tvtts_voice_count()):
            lang = self.d.tvtts_voice_language(v)
            if lang and lang.decode() == 'ja':
                out.append((v, self.d.tvtts_voice_name(v).decode()))
        return out

    def peak(self, text, voice, atten, sr):
        """-> (peak, clipped samples, largest sample-to-sample step)"""
        hi = [0]
        clip = [0]
        step = [0]
        last = [None]

        def cb(ev, u):
            e = ev[0]
            if e.type == 0 and e.count:
                for i in range(e.count):
                    s = e.samples[i]
                    a = -s if s < 0 else s
                    if a > hi[0]:
                        hi[0] = a
                    if a >= 32700:
                        clip[0] += 1
                    if last[0] is not None:
                        dd = s - last[0]
                        if dd < 0:
                            dd = -dd
                        if dd > step[0]:
                            step[0] = dd
                    last[0] = s
            return 0

        c = CB(cb)
        s = self.d.tvtts_create_lang(sr, b"ja")
        if not s:
            raise RuntimeError('tvtts_create_lang failed')
        self.d.tvtts_set_voice(ctypes.c_void_p(s), voice)
        self.d.tvtts_set_volume(ctypes.c_void_p(s),
                                ctypes.c_uint32(ATTEN_BOUND[atten - ATTEN_MIN]))
        b = text.encode('utf-8')
        self.d.tvtts_speak_bytes(ctypes.c_void_p(s), b, len(b), c, None)
        self.d.tvtts_destroy(ctypes.c_void_p(s))
        return hi[0], clip[0], step[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build', 'bin',
                                                  'tvtts64.dll'))
    ap.add_argument('--target', type=int, default=29000,
                    help='the peak each voice must come under (-1 dB of full)')
    ap.add_argument('--max-atten', type=int, default=15,
                    help='the engine caps its own volume attenuation here')
    ap.add_argument('--sr', type=int, default=11025,
                    help='11025 is what most people run TruVoice at')
    args = ap.parse_args(argv)

    lib = Lib(args.dll)
    voices = lib.ja_voices()
    if not voices:
        print('the library carries no Japanese voices')
        return 2

    print('%d Japanese voices, %d texts, at %d Hz'
          % (len(voices), len(WORDS), args.sr))
    print('target peak %d; the engine clamps its own attenuation at %d dB'
          % (args.target, args.max_atten))
    print()
    print('voice        atten  peak  clipped  max step   verdict')
    rows = []
    for v, name in voices:
        chosen = None
        for k in range(0, args.max_atten + 1):
            hi = clip = step = 0
            for t in WORDS:
                a, c, s = lib.peak(t, v, k, args.sr)
                hi = max(hi, a)
                clip += c
                step = max(step, s)
            if k == 0:
                first = (hi, clip, step)
            if hi <= args.target and clip == 0:
                chosen = (k, hi, clip, step)
                break
        if chosen is None:
            print('%-12s    --  %5d  %7d  %8d   NOT REACHED in %d dB'
                  % (name, first[0], first[1], first[2], args.max_atten))
            rows.append((name, None))
            continue
        k, hi, clip, step = chosen
        print('%-12s %5d  %5d  %7d  %8d   was peak %d, clipped %d'
              % (name, k, hi, clip, step, first[0], first[1]))
        rows.append((name, k))
    print()
    print('as a table for ja_port/tvtts_ja.c, in voice order:')
    print('    ' + ', '.join('%d' % (k if k is not None else -1)
                             for _, k in rows))
    return 0


if __name__ == '__main__':
    sys.exit(main())
