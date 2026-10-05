# -*- coding: utf-8 -*-
"""Render the shipped Japanese voices through the library, for listening.

    python tools/ja_samples.py [--sr 11025] [--out DIR]

Not the Python prototype -- the DLL, through tvtts_speak_bytes, the same path
NVDA and SAPI take.  So what comes out of this is what a person installing
OpenTV would actually hear, voice selection and punctuation and all.

One file per voice, then one file with all ten in order for comparison, then a
few files that each isolate something: the four /f/ realizations, the moraic
nasal, the devoiced vowels, the geminates, the long vowels, punctuation, and a
question.  The names are numbered so they sort into listening order.
"""
import argparse, ctypes, os, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

VOICES = ['Taro', 'Tsuyoshi', 'Kenta', 'Daichi', 'Takeshi',
          'Ojiisan', 'Osamu', 'Akira', 'Hanako', 'Keiko']

# Each line is here because it exercises something that cost work to get right.
PROBES = [
    ('f-four-ways',  u'ふね、ふく、ふとん、ふうふ'),
    ('final-n',      u'おばさん、おばあさん、おじさん、おじいさん'),
    ('devoiced',     u'くつ、です、すこし、した'),
    ('geminates',    u'がっこう、きって、ざっし'),
    ('long-vowels',  u'かあど、とおる、びいる、くうき'),
    ('sibilants',    u'しゃしん、つつじ、ちゃちゃちゃ'),
    ('flap',         u'さくら、ともだち、だいどころ'),
    ('moraic-n',     u'さんぽ、ぎんこう、しんぶん、れんあい'),
    ('punctuation',  u'これは ひとつめです。これは ふたつめです。'),
    ('question',     u'あしたの かいぎは なんじからですか？'),
    ('sentence',     u'わたしは にほんごを はなします。'),
]

# And the things only the DICTIONARY can say.  Everything above is kana, which
# the library could always read; these need data/ja/jadic.bin, the Viterbi and
# the five rule stages, and before the analyser was ported to C the library
# dropped every one of these kanji and said only the particles.
#
# They are here rather than in PROBES because they are the test of a different
# thing: not the phonetics, which the kana probes cover, but whether the
# shipped DLL -- not the Python prototype -- reads kanji, places the accent and
# devoices from the lexicon.
KANJI = [
    ('kanji-sentence', u'私は日本語を話します。'),
    ('kanji-accent',   u'箸が。橋が。端が。'),
    ('kanji-number',   u'値段は1,250円です。'),
    ('kanji-date',     u'会議は5月3日の午後2時30分からです。'),
    ('kanji-question', u'明日の会議は何時からですか？'),
    ('kanji-counter',  u'一粒、二口、一通り、一人、二十日。'),
    ('kanji-identifier', u'電話番号は03-1234-5678です。'),
    ('kanji-devoiced', u'複数の美しさ。学校に行きました。'),
    # The Latin-input routing, which is the part worth hearing as a pair:
    # the first line is read as Japanese because the romaji parser consumes
    # it whole, the second is spelled out because it does not, and Windows
    # finds a real dictionary reading.
    ('latin-romaji',  u'sakura konnichiwa watashiwa nihongo o hanashimasu'),
    ('latin-spelled', u'hello computer blorf NVDA'),
    ('latin-mixed',   u'Windowsです。helloです。'),
    # The lexicon step, which the routing order exists for: every one of
    # these has a dictionary reading, and trying romaji first got them
    # wrong -- DATE was /da.te/ instead of デイト, which is the same mistake
    # as reading a bare "take" as タケ, on a word we already have.
    ('latin-lexicon', u'Amazon Apple DATE AU ASEAN Windows'),
    # Per token, which the utterance-level decision got wrong: Windows
    # used to suppress konnichiwa's romaji reading and leave it spelled
    # out letter by letter.
    ('latin-mixed2',  u'Windows konnichiwa。これはsakuraです。'),
    # The Latin-word RULES, through the library: an unfamiliar word gets
    # a pronunciation instead of its letters.  These used to be spelled
    # out one letter at a time.
    ('latin-rules',  u'computer blorf zindle frobnic kludge'),
    ('latin-sentence', u'私はcomputerでblorfをつかいます。'),
]

GREETING = u'こんにちは。わたしの なまえは %sです。'


class EV(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int32), ("count", ctypes.c_uint32),
                ("samples", ctypes.POINTER(ctypes.c_int16)),
                ("mark", ctypes.c_uint32), ("sample_pos", ctypes.c_uint32)]


CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(EV), ctypes.c_void_p)


def load(path):
    os.add_dll_directory(os.path.dirname(os.path.abspath(path)))
    d = ctypes.CDLL(os.path.abspath(path))
    d.tvtts_create_lang.restype = ctypes.c_void_p
    d.tvtts_create_lang.argtypes = [ctypes.c_uint32, ctypes.c_char_p]
    d.tvtts_destroy.argtypes = [ctypes.c_void_p]
    d.tvtts_voice_name.restype = ctypes.c_char_p
    d.tvtts_voice_language.restype = ctypes.c_char_p
    return d


def ja_base(d):
    """The library numbers voices across every language, so Japanese does not
    start at 0 and where it starts moves when a voice is added elsewhere."""
    for v in range(d.tvtts_voice_count()):
        lang = d.tvtts_voice_language(v)
        if lang and lang.decode() == 'ja':
            return v
    raise SystemExit('the library carries no Japanese voices')


def say(d, text, voice, sr, defaults=True):
    """Speak one line.

    `defaults` applies the voice's own rate and pitch, which the library does
    NOT do on a voice change -- English does not either, and for the same
    reason: those are the caller's settings and a caller that has chosen them
    would not want a voice change to overwrite them.  src/port/main.c shows the
    pattern, asking tvtts_voice_rate and tvtts_voice_pitch and passing them in.
    Without this every voice here would speak at Taro's base pitch of 85, and
    the two female voices would be a short vocal tract at a male baseline --
    which is not what either of them is.
    """
    out = []

    def cb(ev, _u):
        e = ev[0]
        if e.type == 0 and e.count:
            out.extend(e.samples[0:e.count])
        return 0

    c = CB(cb)
    s = d.tvtts_create_lang(sr, b'ja')
    if not s:
        raise SystemExit('Japanese refuses %d Hz -- see ja_set_rate_hz' % sr)
    try:
        d.tvtts_set_voice(ctypes.c_void_p(s), voice)
        if defaults:
            d.tvtts_set_rate(ctypes.c_void_p(s), d.tvtts_voice_rate(voice))
            d.tvtts_set_pitch(ctypes.c_void_p(s), d.tvtts_voice_pitch(voice))
        b = text.encode('utf-8')
        d.tvtts_speak_bytes(ctypes.c_void_p(s), b, len(b), c, None)
    finally:
        d.tvtts_destroy(ctypes.c_void_p(s))
    return out


def write(path, pcm, sr):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(bytearray(
            b for s in pcm for b in (s & 0xff, (s >> 8) & 0xff))))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build', 'bin',
                                                  'tvtts64.dll'))
    ap.add_argument('--sr', type=int, default=11025)
    ap.add_argument('--out', default=None)
    args = ap.parse_args(argv)

    d = load(args.dll)
    base = ja_base(d)
    out = args.out or os.path.join(ROOT, 'build', 'Japanese_test',
                                   'voices %d Hz' % args.sr)
    os.makedirs(out, exist_ok=True)

    gap = [0] * int(args.sr * 0.4)
    everyone = []
    for i, name in enumerate(VOICES):
        v = base + i
        actual = d.tvtts_voice_name(v).decode()
        if actual != name:
            raise SystemExit('voice %d is %s, not %s' % (v, actual, name))
        pcm = say(d, GREETING % name, v, args.sr)
        write(os.path.join(out, '%02d-%s.wav' % (i, name.lower())), pcm,
              args.sr)
        everyone += pcm + gap
        print('  %02d %-9s  rate %3d  pitch %3d  %6d samples (%.2f s)'
              % (i, name, d.tvtts_voice_rate(v), d.tvtts_voice_pitch(v),
                 len(pcm), len(pcm) / float(args.sr)))
    write(os.path.join(out, '10-all-ten.wav'), everyone, args.sr)

    # The probes go through Taro, the identity voice, so what is being listened
    # to is the phonetics and not the voice block.
    for j, (tag, text) in enumerate(PROBES):
        pcm = say(d, text, base, args.sr, defaults=False)
        write(os.path.join(out, '%02d-%s.wav' % (11 + j, tag)), pcm, args.sr)
        print('  %02d %-14s %6d samples (%.2f s)'
              % (11 + j, tag, len(pcm), len(pcm) / float(args.sr)))
    for j, (tag, text) in enumerate(KANJI):
        pcm = say(d, text, base, args.sr, defaults=False)
        write(os.path.join(out, '%02d-%s.wav' % (11 + len(PROBES) + j, tag)),
              pcm, args.sr)
        print('  %02d %-18s %6d samples (%.2f s)'
              % (11 + len(PROBES) + j, tag, len(pcm),
                 len(pcm) / float(args.sr)))
        if len(pcm) == 0:
            print('     NOTHING SAYABLE -- the dictionary was not found?')
    print()
    print('%s' % out)
    print('  %d files at %d Hz, through the library rather than the prototype'
          % (len(VOICES) + 1 + len(PROBES) + len(KANJI), args.sr))
    return 0


if __name__ == '__main__':
    sys.exit(main())
