# -*- coding: utf-8 -*-
"""Japanese text in, audio out, through the dictionary.

    python jp_front.py "私は日本語を話します" out.wav

The old entry point, jp_speak.say, takes kana or romaji and guesses: it reads
the kana in front of it, drops any kanji, gives every word a flat heiban
contour because nothing could tell it the accent, and decides devoicing from
the mora string alone.  This one asks the dictionary instead.

    text  ->  jp_dict.analyse       Viterbi over naist-jdic
          ->  jp_njd stages         readings, numbers, accent phrases,
                                    accent types, devoicing
          ->  to_front              morae, phrase marks, per-phrase accents
          ->  jp_speak.build/pitch  the part that was already built

Everything below to_front is unchanged and still covered by the oracle.  What
is new is what goes IN to it: the accent type per phrase, which ja_pitch has
always accepted and always been handed 0, and a devoicing flag per mora.
"""
import os, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_dict as D, jp_njd as N, jp_digit as G, jp_mora as M
import jp_speak as S

PAUSE, PERIOD, QUESTION = N.PAUSE, N.PERIOD, N.QUESTION


def _kata_to_hira(s):
    return u''.join(chr(ord(c) - 0x60) if u'ァ' <= c <= u'ヶ' else c
                    for c in s)


_mora_cache = {}

# A mora the kana reader has no symbol for, spelled out rather than dropped.
#
# These are morae Open JTalk's splitter produces and its own inventory does
# not contain, or that our romaji table declines to fuse.  Dropping one loses
# a mora from the word, so each is given the nearest thing the phoneme
# inventory can say:
#
#   a bare small vowel     its full-size vowel.  ゴォォール splits as ゴォ ォ
#                          ー ル, and the lone ォ is a mora of its own.
#   a bare small ya/yu/yo  the full-size glide mora.  ちっちゃゅぅ ends with
#                          a bare ュ.
#   ヮ                     /wa/.  クヮルテット is the one reading in the
#                          dictionary that uses it, and /kwa/ is not in this
#                          inventory at all -- there is no labialised velar --
#                          so it comes out as two morae, /ku.wa/ rather than
#                          /kwa/.  That is wrong about the consonant and right
#                          about the mora count, and it is audible, which
#                          dropping it was not.
_SPELL_OUT = {
    u'ァ': u'a', u'ィ': u'i', u'ゥ': u'u',
    u'ェ': u'e', u'ォ': u'o',                   # ァ ィ ゥ ェ ォ
    u'ャ': u'ya', u'ュ': u'yu', u'ョ': u'yo',   # ャ ュ ョ
    u'ヮ': u'wa',                                   # ヮ
}


def mora_of(kata):
    """One katakana mora -> the one mora symbol jp_speak understands.

    Routed through the existing kana reader rather than a second table, so the
    two inputs cannot drift: whatever `shi` or `kya` or `tsa` means on the
    kana path, it means the same here.
    """
    if kata in _mora_cache:
        return _mora_cache[kata]
    got = M.to_morae(_kata_to_hira(kata))
    out = got[0] if len(got) == 1 else None
    if out is None:
        out = _SPELL_OUT.get(kata)
    _mora_cache[kata] = out
    return out


def to_front(words):
    """-> (morae, accents, devoiced, question, stats)

    `morae` is jp_speak's own list, with ' ' between accent phrases, '|' for a
    comma and '||' for a full stop.  `accents` is one accent type per accent
    phrase, in order, which is what jp_speak.pitch takes as a list.
    `devoiced` is one flag per entry in `morae`, aligned by index.

    `stats` counts what did not survive, because otherwise it is invisible:

        words        how many words the analyser produced
        unreadable   how many of them had no reading and became a pause
        dropped      morae the kana reader had no symbol for
        empty        True when nothing sayable came out at all

    A pause between two spoken morae is kept.  One at the very start or end is
    not: it has nothing to separate and would only be latency.  So an
    unreadable word at the edge of an utterance leaves no trace in the audio,
    and these counts are the only way to know it was there -- which is the
    distinction worth drawing between an unknown token, a known token with no
    reading, and an utterance that comes out empty.
    """
    flat = N.set_unvoiced_vowel(words)
    morae, devoiced, accents = [], [], []
    question = False
    open_phrase = False
    stats = {'words': len(words), 'unreadable': 0, 'dropped': 0,
             'empty': False}

    i = 0
    for w in words:
        n_mora = len(N.split_morae(w.pron))
        chunk = flat[i:i + n_mora]
        i += n_mora
        # Keyed on the PRONUNCIATION, not the part of speech.  A question mark
        # comes out of njd_set_pronunciation as a FILLER rather than a 記号 --
        # see the note there -- so testing the part of speech lost every
        # question.  The pronunciation is what the stage actually decides.
        if w.pron in (PAUSE, PERIOD, QUESTION) and w.mora_size == 0:
            if w.pron == QUESTION:
                question = True
                continue           # the rise is prosody, not a pause
            mark = u'||' if w.pron == PERIOD else u'|'
            if w.string not in N.BREAK_STRONG and w.string not in N.BREAK_WEAK:
                stats['unreadable'] += 1   # punctuation is not a failure
            if open_phrase:
                morae.append(mark)
                devoiced.append(False)
                open_phrase = False
            continue
        if not chunk:
            continue
        if w.chain_flag != 1:
            if open_phrase:
                morae.append(u' ')
                devoiced.append(False)
            accents.append(w.acc)
            open_phrase = True
        for kata, dv, _w in chunk:
            m = mora_of(kata)
            if m is None:
                stats['dropped'] += 1
                continue           # a mora the kana reader has no symbol for
            morae.append(m)
            devoiced.append(bool(dv))
    # A trailing boundary is a pause with nothing after it to separate.
    while morae and morae[-1] in (u' ', u'|', u'||'):
        morae.pop()
        devoiced.pop()
    stats['empty'] = not morae
    return morae, accents, devoiced, question, stats


_NO_ASK = object()


def analyse(text, ask=_NO_ASK):
    """text -> (words, morae, accents, devoiced, question, stats)

    `ask` is how a Latin word nothing knows gets a pronunciation, and it
    defaults to the English front end.  Passing None turns the Latin-word
    rules OFF, which `dump_front_oracle` does for one reason: the oracle is
    checked by `ja_check`, which links the Japanese front end alone with no
    engine and no data, and that is worth keeping.  So the oracle covers the
    analyser's own stages and the rules are verified where the engine is
    available -- end to end through the DLL, in tests/japanese_api_test.py.
    """
    if ask is _NO_ASK:
        import jp_g2p as GP
        ask = GP.ask
    w = D.analyse(text)
    w = N.set_pronunciation(w)
    # Not one of Open JTalk's stages: a reading for a Latin token nothing
    # knew, before the stages that need one.
    w = N.read_latin(w, ask)
    w = G.set_digit(w)              # Open JTalk's own order: digits here
    w = N.set_accent_phrase(w)
    w = N.set_accent_type(w)
    morae, accents, devoiced, question, stats = to_front(w)
    return w, morae, accents, devoiced, question, stats


def frames(text, ask=_NO_ASK):
    w, morae, accents, devoiced, question, stats = analyse(text, ask)
    if not morae:
        return None
    fr, ends, q = S.build(list(morae), devoiced_in=devoiced)
    hz = S.pitch(fr, ends, accents or [0], q_ends=q, morae=list(morae),
                 question=question)
    return w, morae, accents, devoiced, fr, hz


def say(text, path):
    got = frames(text)
    if got is None:
        return None
    w, morae, accents, devoiced, fr, hz = got
    x = S.render(fr)
    with wave.open(path, 'wb') as fp:
        fp.setnchannels(1)
        fp.setsampwidth(2)
        fp.setframerate(S.SR)
        fp.writeframes(x.tobytes())
    return morae, accents, len(fr), (min(hz), max(hz)), len(x) / float(S.SR)


if __name__ == '__main__':
    t = sys.argv[1] if len(sys.argv) > 1 else u'私は日本語を話します'
    p = sys.argv[2] if len(sys.argv) > 2 else 'out.wav'
    r = say(t, p)
    if r is None:
        print('nothing sayable in %r' % t)
    else:
        morae, accents, nf, rng, dur = r
        print('%s' % t)
        print('  %s' % ' '.join(morae))
        print('  accents %s   %d frames   F0 %.0f-%.0f   %.2f s -> %s'
              % (accents, nf, rng[0], rng[1], dur, os.path.basename(p)))
