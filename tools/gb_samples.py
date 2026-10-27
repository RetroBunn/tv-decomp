# -*- coding: utf-8 -*-
"""Render the British English samples, and check American is untouched.

    python -B tools/gb_samples.py

Writes build/en-gb_test at 11025 Hz -- the engine's native rate, and the rate
asked for, since there is no clipping here to go looking for.  Every sentence
is rendered twice, once in each accent, so a pair can be compared directly;
the GB file is the one to judge, the American one is what it used to sound
like.

It also answers the question a process-wide flag always raises: can American
come out British by accident?  Every American render is done once on its own
and once with a GB synth alive and speaking in between, and the two must be
byte for byte the same.
"""
import ctypes as C
import hashlib
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build' / 'en-gb_test'
RATE = 11025

d = C.CDLL(str(ROOT / 'build' / 'bin' / 'tvtts64.dll'))
d.tvtts_create_lang.argtypes = [C.c_uint32, C.c_char_p]
d.tvtts_create_lang.restype = C.c_void_p
d.tvtts_destroy.argtypes = [C.c_void_p]
d.tvtts_set_voice.argtypes = [C.c_void_p, C.c_int]
d.tvtts_voice_count.restype = C.c_int
d.tvtts_voice_language.argtypes = [C.c_int]
d.tvtts_voice_language.restype = C.c_char_p


class Event(C.Structure):
    _fields_ = [('type', C.c_int32), ('count', C.c_uint32),
                ('samples', C.POINTER(C.c_int16)), ('mark', C.c_uint32),
                ('sample_pos', C.c_uint32)]


CB = C.CFUNCTYPE(C.c_int, C.POINTER(Event), C.c_void_p)
d.tvtts_speak_bytes.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32, CB,
                                C.c_void_p]
d.tvtts_speak_bytes.restype = C.c_int


def base_voice(lang):
    """The first voice belonging to a language.

    tvtts_set_voice numbers voices across every language at once, so asking a
    British synth for voice 0 asks for Peter, who is English -- and the API
    then switches the synth to English to give him.  Working as designed, and
    the reason the accent looked like it did nothing for an hour.
    """
    for i in range(d.tvtts_voice_count()):
        if d.tvtts_voice_language(i).decode() == lang:
            return i
    raise SystemExit('no voice speaks %r' % lang)


def render(lang, text, voice=None):
    """One utterance, as raw PCM bytes."""
    syn = d.tvtts_create_lang(RATE, lang.encode())
    if not syn:
        raise SystemExit('no synth for %r' % lang)
    d.tvtts_set_voice(syn, base_voice(lang) if voice is None else voice)
    chunks = []

    @CB
    def cb(ev, user):
        e = ev[0]
        if e.type == 0 and e.count:
            chunks.append(bytes(C.cast(
                e.samples, C.POINTER(C.c_char))[:e.count * 2]))
        return 0

    b = text.encode('cp1252')
    d.tvtts_speak_bytes(syn, b, len(b), cb, None)
    d.tvtts_destroy(syn)
    return b''.join(chunks)


def write_wav(path, pcm):
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(pcm)


#: What each sentence is for.  The lexical sets a GB accent has to get right
#: are named so a listening pass can be systematic rather than general.
SAMPLES = [
    ('01-north-wind',
     'The North Wind and the Sun were disputing which was the stronger, '
     'when a traveller came along wrapped in a warm cloak.',
     "the IPA's standard passage, which Roach 2004 prints in GB"),
    ('02-nurse',
     'The nurse heard the early bird and turned her thirty words.',
     'NURSE: the biggest single change, F3 1456 to 2352'),
    ('03-start',
     'Father parked the car in the far dark yard after the carnival.',
     'START, and PALM in father'),
    ('04-north-force',
     'He saw four more horses for sure, and thought the door was short.',
     'NORTH and THOUGHT merged: saw, four, more, thought all one vowel'),
    ('05-square',
     'They were aware the air was fair, so the pair shared a chair.',
     'SQUARE, now a steady long vowel rather than a glide to schwa'),
    ('06-near-cure',
     'I hear the beer is near, and I am sure the cure is pure here.',
     'NEAR and CURE, the two centring diphthongs the 8th edition keeps'),
    ('07-lot-thought',
     'The dog got a lot of hot pots, but bought a short caught thought.',
     'LOT, 122 Hz closer in F1 than American'),
    ('08-goat-face',
     'The goat rowed the boat home, and they say the day was grey.',
     'GOAT and FACE, both from Table 5'),
    ('09-bath',
     'He had a bath after the dance class, then asked for a glass '
     'and half a laugh.',
     'BATH: a word list, not a rule -- bath and class move, cat does not'),
    ('10-goose-foot',
     'Who knew the goose would choose to look at a good book?',
     'GOOSE, 221 Hz back in F2, and FOOT, 153 Hz forward'),
    ('11-bath-vs-trap',
     'That cat sat on a flat mat, but the last glass of gas '
     'passed the ant and the aunt.',
     'the pairs the list has to keep apart: cat/last, gas/glass, ant/aunt'),
    ('12-yod',
     'On Tuesday the student bought a new tube, and during his duty '
     'he was too stupid to reduce it.',
     'yod coalescence: tube is chube, duty is jooty -- 7.10.1(5)'),
    ('13-cloth',
     'The dog ran across the office and knocked the coffee off, '
     'because the sausage was lost.',
     'CLOTH: short /Q/ where American has its THOUGHT vowel -- 8.9.8'),
    ('14-palm',
     'Father stayed calm in the drama, with a glass of lava-coloured '
     'pasta and a banana.',
     'PALM: American LOT and TRAP, both /A:/ in GB'),
    ('15-linking-r',
     'The car is far away, and the four aces were under the water. '
     'Answer it, wear out your fur inside, and secure everything.',
     'linking /r/: kept before a vowel, silent before a consonant -- 12.4.7'),
    ('16-intrusive-r',
     'The vanilla essence and the vodka and tonic sat in the area of '
     'the sofa, but the straw in the wind and the law and order did not.',
     'intrusive /r/ after schwa only; refused after /A:/ and /O:/'),
    ('17-rhotic-pairs',
     'Far off and far behind. Four aces and four cats. '
     'Bar is and bar closed. Fur inside and fur coat.',
     'the same word either side of the rule, back to back'),
    ('18-tap',
     'Put a little water in the metal kettle, then the writer and the rider '
     'had a better bottle of pretty bitter butter.',
     'the American tap is gone: water has a real t, writer is not rider'),
    ('19-cure',
     'The poor tourist was sure of a tour of the moor, but the pure cure '
     'was not.',
     'CURE to NORTH, a word list -- poor sure tour moor move, pure cure do not'),
    ('20-fire',
     'He will hire a tyre for the fire engine within the hour, '
     'and the higher wire caught fire.',
     'fire and hire: PRICE plus schwa, not the engine-wide /aIr/ vowel'),
    ('21-glottal',
     'It was not very good, so get back to that place quite quickly. '
     'But not a lot of the little bottle was left.',
     'a /t/ before a consonant is a glottal stop -- but not before a vowel, '
     'and not in little'),
    ('22-clear-l',
     'Follow the yellow lily, and the salad had the colour of a field '
     'of milk and a feeling of full value.',
     'clear /l/ before a vowel, dark elsewhere; American darkens it always'),
    ('23-orange',
     'The orange in the forest was sorry during the moral of the story.',
     'the /r/ an r-coloured vowel was carrying, put back inside the word'),
    ('24-mary-marry-merry',
     'Mary was very merry when she married and carried the berry, '
     'but the parent in the library had a rare and scary character.',
     'three vowels where American has one: Mary square, marry trap, '
     'merry dress -- and very is not vary'),
    ('25-orange-class',
     'I am sorry the orange in the forest had a moral, but tomorrow '
     'the quarrel over the porridge will borrow a horrid origin.',
     'GB short /Q/ where American has /O:/ or /A:/ before an r'),
    ('26-syllabic',
     'It was awful that my uncle was able to whistle a special tune '
     'while the apple and the barrel settled in the middle.',
     'GB keeps the syllabic l where American splits it into schwa plus dark l'),
    ('27-all-vowels',
     'Peter said a British voice should speak every vowel it owns: '
     'kit, dress, trap, lot, strut, foot, fleece, goose, thought, '
     'nurse, start, face, price, choice, goat, mouth, near, square, cure.',
     'the whole inventory in one breath, for comparing the two accents'),
]


def prune():
    """Drop renders whose sentence is no longer in the list.

    Renumbering a sample leaves the old file sitting in the directory, and
    nothing says so: inserting the /r/ and tap sentences moved all-vowels
    from 15 to 18 to 21 and left two stale pairs of it behind, so the
    directory offered three copies of the same sentence under three names.
    Anything not named below is not a sample.
    """
    keep = set()
    for name, _text, _why in SAMPLES:
        keep.add('%s.en.wav' % name)
        keep.add('%s.en-gb.wav' % name)
    for f in sorted(OUT.glob('*.wav')):
        if f.name not in keep:
            print('  dropping stale %s' % f.name)
            f.unlink()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    print('rendering %d sentences to %s at %d Hz'
          % (len(SAMPLES), OUT.relative_to(ROOT), RATE))
    print()
    prune()
    leaked = 0
    for name, text, why in SAMPLES:
        us = render('en', text)
        gb = render('en-GB', text)
        # American again, this time after GB has spoken, to catch the flag
        # leaking across synths.
        us2 = render('en', text)
        if us != us2:
            leaked += 1
            print('  !! %s: American differs after GB spoke' % name)
        write_wav(OUT / ('%s.en.wav' % name), us)
        write_wav(OUT / ('%s.en-gb.wav' % name), gb)
        same = 'IDENTICAL -- GB changed nothing!' if us == gb else ''
        print('  %-16s %6.2f s  %s %s'
              % (name, len(gb) / 2 / RATE, why, same))

    print()
    if leaked:
        print('FAIL: the GB flag leaked into %d American renders' % leaked)
        return 1
    print('American English is byte for byte unchanged by GB speaking '
          '(%d/%d)' % (len(SAMPLES), len(SAMPLES)))

    # And the stronger form of the same question: with GB never used at all,
    # is American what it was?  Hash it so a later change is caught.
    h = hashlib.sha256()
    for name, text, why in SAMPLES:
        h.update(render('en', text))
    print('sha256 of all American renders: %s' % h.hexdigest()[:32])
    print()
    print('listen to the .en-gb.wav files; the .en.wav beside each is the')
    print('same sentence as it was, for comparison.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
