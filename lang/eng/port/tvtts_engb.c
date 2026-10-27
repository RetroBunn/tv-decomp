/*
 * General British: a language row over the American engine.
 *
 * Every other language here owns a front end -- Spanish a decompiled one,
 * Japanese one built from the literature.  This one owns none, and should
 * not: GB and GA are the same language, and the text analysis, the lexicon,
 * the letter-to-sound rules, the stress rules and the prosody are wanted
 * exactly as they are.  What differs is the vowels, and src/engine/gb.c moves
 * those by re-aiming stage 3's formant targets.
 *
 * So this file is thin on purpose.  Each entry point turns GB on, calls the
 * American one, and puts the flag back -- which keeps the two rows
 * independent even though they share an engine, and keeps the corpus
 * untouched, because with the flag off every accessor in gb.c is the array
 * subscript it replaced.
 *
 * Why an accent is a language row and not a voice: a voice in this engine is
 * fifteen numbers that scale the formants uniformly (docs/VOICES.md), which
 * is vocal-tract length.  An accent moves vowels relative to each other --
 * GB's LOT is 122 Hz closer in F1 than GA's while its STRUT is 48 Hz opener
 * -- and no single scale factor does that.  The ten voices are ten tracts
 * and are accent-independent, which is why the same ten speak GB here.
 */
#include <string.h>

#include "tvtts.h"
#include "tvtts_port.h"
#include "engine.h"

/*
 * The American engine with GB aiming.  TV_GB_CALL runs one of its entry
 * points with the flag set, and restores whatever was there before rather
 * than clearing it, so a nested call cannot turn GB off under its caller.
 */
#define TV_GB_CALL(expr)                                                      \
    do {                                                                      \
        int was_ = tv_gb_get();                                               \
        tv_gb_set(1);                                                         \
        (expr);                                                               \
        tv_gb_set(was_);                                                      \
    } while (0)

#define TV_GB_RET(type, expr)                                                 \
    do {                                                                      \
        int was_ = tv_gb_get();                                               \
        type r_;                                                              \
        tv_gb_set(1);                                                         \
        r_ = (expr);                                                          \
        tv_gb_set(was_);                                                      \
        return r_;                                                            \
    } while (0)

void *engb_create(uint32_t sample_rate)
{
    TV_GB_RET(void *, en_create(sample_rate));
}

void engb_destroy(void *s)
{
    TV_GB_CALL(en_destroy(s));
}

void engb_set_voice(void *s, int voice)
{
    TV_GB_CALL(en_set_voice(s, voice));
}

void engb_set_rate(void *s, int wpm)
{
    TV_GB_CALL(en_set_rate(s, wpm));
}

void engb_set_pitch(void *s, int pitch)
{
    TV_GB_CALL(en_set_pitch(s, pitch));
}

void engb_set_volume(void *s, uint32_t volume)
{
    TV_GB_CALL(en_set_volume(s, volume));
}

int engb_get_voice(const void *s)
{
    return en_get_voice(s);
}

int engb_get_rate(const void *s)
{
    return en_get_rate(s);
}

int engb_get_pitch(const void *s)
{
    return en_get_pitch(s);
}

uint32_t engb_get_rate_hz(const void *s)
{
    return en_get_rate_hz(s);
}

int engb_set_rate_hz(void *s, uint32_t hz)
{
    TV_GB_RET(int, en_set_rate_hz(s, hz));
}

void engb_set_compat(void *s, int preformat, int textin, int terminators)
{
    TV_GB_CALL(en_set_compat(s, preformat, textin, terminators));
}

void engb_set_textin_mode(void *s, int mode)
{
    TV_GB_CALL(en_set_textin_mode(s, mode));
}

void engb_set_extensions(uint32_t mask)
{
    en_set_extensions(mask);
}

int engb_voice_count(void)
{
    return en_voice_count();
}

/*
 * The speakers are the American ones -- a voice in this engine is a vocal
 * tract and a tract has no accent -- but they cannot keep their names.  The
 * API numbers voices in one run across every language and tests that the
 * names in that run are distinct, so a second Peter would collide with the
 * first and make "Peter" ambiguous to any caller that resolves by name.
 *
 * Approved, and one table to change.  Each sits in the same place as its
 * American counterpart -- the plain first voice, the eager one, the deep one,
 * the old one, the two women -- and is a name a British speaker would
 * actually be called.  "Gramps Amos" swaps the American "Grandpa" for the
 * British word and keeps Amos.
 */
static const char *const g_engb_names[] = {
    "Nigel",            /* 0, for Peter: the plain one the tables were written for */
    "Clive",            /* 1, for Sidney */
    "Eager Edwin",      /* 2, for Eager Eddie */
    "Deep Duncan",      /* 3, for Deep Douglas */
    "Rupert",           /* 4, for Biff */
    "Gramps Amos",      /* 5, for Grandpa Amos */
    "Malcolm",          /* 6, for Melvin */
    "Alistair",         /* 7, for Alex */
    "Fiona",            /* 8, for Wanda */
    "Imogen",           /* 9, for Julia */
    "Bernard",          /* 10, for Frank, the deep voice OpenTV added */
};

#define TV_ENGB_NAMES ((int)(sizeof g_engb_names / sizeof g_engb_names[0]))

const char *engb_voice_name(int voice)
{
    if (voice >= 0 && voice < TV_ENGB_NAMES)
        return g_engb_names[voice];
    /* A voice added to the English engine without a British name here still
     * has to answer with something unique, so fall back rather than return
     * NULL -- which the API treats as "no such voice". */
    return en_voice_name(voice);
}

int engb_voice_rate(int voice)
{
    return en_voice_rate(voice);
}

int engb_voice_pitch(int voice)
{
    return en_voice_pitch(voice);
}

int engb_speak_bytes(void *s, const void *text, uint32_t len,
                     tvtts_callback cb, void *user)
{
    TV_GB_RET(int, en_speak_bytes(s, text, len, cb, user));
}
