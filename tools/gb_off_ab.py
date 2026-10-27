# -*- coding: utf-8 -*-
"""Render the *off* question both ways, so it can be settled by ear.

    python -B tools/gb_off_ab.py before      (run before the change)
    python -B tools/gb_off_ab.py after       (run after it)

*off* is the one CLOTH word whose stress mark disappears when GB gives it the
shorter vowel.  American marks it `%w1F` where the word is prominent and
`%wF` where it is not; GB's swap to /6/ leaves `%oF` in both, because a `%`
word's mark depends on which vowel it carries while a `&` word's does not --
*cough*, the same CLOTH swap in a content word, keeps its mark as `&Ko1F`.

So the question is whether GB *off* should still be able to carry a mark.
This renders the same six sentences before and after the change, into
build/en-gb_test/off-question, and prints the phonemes beside each so the
audible difference and the written one can be checked against each other.

The sentences are chosen to cover both halves: three where American marks the
word and three where it does not, so a change that helps the first group can
be heard not to damage the second.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
import gb_samples as S

OUT = S.OUT / 'off-question'

#: (name, sentence, whether American marks the *off*)
CASES = [
    ('01-final',    'The light is off.',                        'marked'),
    ('02-final',    'Please turn the heating off.',             'marked'),
    ('03-final',    'I could not tell whether it was on or off.', 'marked'),
    ('04-particle', 'Take it off now.',                         'unmarked'),
    ('05-initial',  'Off we go to the fair.',                   'unmarked'),
    ('06-several',  'The lid came off, so I put it off until the power '
                    'was off.',                                 'both'),
    # §12.4.7(1)'s own linking-/r/ example, and the case that makes gb_check
    # fail while the change is in: its stored string records what GB used to
    # do here, which is not what American does.
    ('07-link',     'The sound was far off.',                   'marked'),
]


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ('before', 'after'):
        print(__doc__)
        return 2
    tag = sys.argv[1]
    OUT.mkdir(parents=True, exist_ok=True)
    print('rendering the %r side to %s at %d Hz'
          % (tag, OUT.relative_to(ROOT), S.RATE))
    print()
    print('%-12s %-9s %s' % ('name', 'American', 'phonemes'))
    for name, text, which in CASES:
        us = S.render('en', text)
        gb = S.render('en-GB', text)
        S.write_wav(OUT / ('%s.%s.en-gb.wav' % (name, tag)), gb)
        if tag == 'before':
            # The American reference is the same on both sides, so it is
            # written once and left alone.
            S.write_wav(OUT / ('%s.en.wav' % name), us)
        print('  %-12s %-9s %s' % (name, which, phonemes(text)))
    print()
    print('listen to *.%s.en-gb.wav against the other side, and against' % tag)
    print('the .en.wav, which is what American does with the same sentence.')
    return 0


def phonemes(text):
    """The GB string, so the audible change has a written counterpart.

    gb_samples.py never calls this entry point, so its signature is declared
    here; without argtypes ctypes passes the 64-bit handle as an int and
    truncates it.
    """
    import ctypes as C
    S.d.tvtts_text_to_phonemes.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p,
                                           C.c_uint32]
    syn = S.d.tvtts_create_lang(S.RATE, b'en-GB')
    buf = C.create_string_buffer(512)
    S.d.tvtts_text_to_phonemes(syn, text.encode(), buf, 512)
    S.d.tvtts_destroy(syn)
    return buf.value.decode('latin-1')


if __name__ == '__main__':
    sys.exit(main())
