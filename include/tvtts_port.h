/*
 * The inside of the library: what each language's port layer provides, and
 * what src/port/api.c calls.
 *
 * tvtts.h is the interface callers see, and it has one set of functions
 * whatever language a synth speaks.  Underneath, each language has its own
 * engine -- its own object layout, its own constant tables, its own
 * decompilation -- and so its own copy of the layer that owns one.  The two
 * are not variants: CGRM_EN is the 1997 engine and CGRM_ES the 1995 one, and
 * they share no code.
 *
 * So each language exports the same dozen entry points under its own prefix,
 * `en_` and `es_`, and api.c holds the public names and picks by language.  A
 * synth is an opaque `void *` across this boundary because the two structures
 * have nothing in common but their purpose; the language that made one is the
 * only thing that may look inside it.
 *
 * Adding a language is adding a file like es_port/tvtts_es.c, a row in
 * api.c's table, and nothing else.
 */
#ifndef TVTTS_PORT_H
#define TVTTS_PORT_H

#include <stdint.h>

#include "tvtts.h"

/*
 * OpenTV: `[:phone TruVoice on|off]` rewritten in place into the engine's
 * phoneme-mode escape, `ESC[1I` / `ESC[0I`.  Returns the new length; the
 * text only ever shrinks, so the caller's buffer is always big enough.
 * English spells the command, so English calls this.  See src/port/sing.c.
 */
size_t tv_phone_commands(char *text, size_t len);

/* One language's engine, as the dispatcher sees it.  Every function takes the
 * synth its own create returned. */
typedef struct {
    const char *code;                   /* "en", "es" */
    const char *name;                   /* "American English" */

    void       *(*create)(uint32_t sample_rate);
    void        (*destroy)(void *s);

    void        (*set_voice)(void *s, int voice);
    void        (*set_rate)(void *s, int wpm);
    void        (*set_pitch)(void *s, int pitch);
    void        (*set_volume)(void *s, uint32_t volume);
    int         (*get_voice)(const void *s);
    int         (*get_rate)(const void *s);
    int         (*get_pitch)(const void *s);

    /* The sample rate in hertz rather than as an index, because which indices
     * exist is the dispatcher's business and which rates an engine can be built
     * for is the engine's.  All of them have all three, as it happens, but that
     * is the engine's answer to give and not the dispatcher's to assume. */
    uint32_t    (*get_rate_hz)(const void *s);
    int         (*set_rate_hz)(void *s, uint32_t hz);

    void        (*set_compat)(void *s, int preformat, int textin,
                              int terminators);
    /* Before the first utterance only: the engine makes its tokenizer when the
     * synth is made, so this rebuilds it. */
    void        (*set_textin_mode)(void *s, int mode);

    int         (*voice_count)(void);
    const char *(*voice_name)(int voice);
    int         (*voice_rate)(int voice);
    int         (*voice_pitch)(int voice);

    int         (*speak_bytes)(void *s, const void *text, uint32_t len,
                               tvtts_callback cb, void *user);

    /* Which OpenTV extensions are on.  Process-wide rather than per-synth,
     * because the engines keep their stage 2 working state in globals, so one
     * synth was never independent of another here.  Each language takes the
     * whole mask and picks out what means anything to it. */
    void        (*set_extensions)(uint32_t mask);
} tvtts_lang;

/* English, src/port/tvtts.c. */
void       *en_create(uint32_t sample_rate);
void        en_destroy(void *s);
void        en_set_voice(void *s, int voice);
void        en_set_rate(void *s, int wpm);
void        en_set_pitch(void *s, int pitch);
void        en_set_volume(void *s, uint32_t volume);
int         en_get_voice(const void *s);
int         en_get_rate(const void *s);
int         en_get_pitch(const void *s);
uint32_t    en_get_rate_hz(const void *s);
int         en_set_rate_hz(void *s, uint32_t hz);
void        en_set_compat(void *s, int preformat, int textin, int terminators);
void        en_set_textin_mode(void *s, int mode);
void        en_set_extensions(uint32_t mask);
int         en_voice_count(void);
const char *en_voice_name(int voice);
int         en_voice_rate(int voice);
int         en_voice_pitch(int voice);
int         en_speak_bytes(void *s, const void *text, uint32_t len,
                           tvtts_callback cb, void *user);

/* Spanish, es_port/tvtts_es.c. */
void       *es_create(uint32_t sample_rate);
void        es_destroy(void *s);
void        es_set_voice(void *s, int voice);
void        es_set_rate(void *s, int wpm);
void        es_set_pitch(void *s, int pitch);
void        es_set_volume(void *s, uint32_t volume);
int         es_get_voice(const void *s);
int         es_get_rate(const void *s);
int         es_get_pitch(const void *s);
uint32_t    es_get_rate_hz(const void *s);
int         es_set_rate_hz(void *s, uint32_t hz);
void        es_set_compat(void *s, int preformat, int textin, int terminators);
void        es_set_textin_mode(void *s, int mode);
void        es_set_extensions(uint32_t mask);
int         es_voice_count(void);
const char *es_voice_name(int voice);
int         es_voice_rate(int voice);
int         es_voice_pitch(int voice);
int         es_speak_bytes(void *s, const void *text, uint32_t len,
                           tvtts_callback cb, void *user);

#endif
