/*
 * TruVoice as a library.
 *
 * A flat C API over the decompiled Centigram TruVoice engine, meant for
 * screen readers and anything else that wants the synthesizer without
 * SAPI: no COM, no registry, no window messages, no threads, and no audio
 * device.  The caller drives synthesis on its own thread and plays the
 * samples itself, which is what a screen reader wants -- it already owns
 * its output device, its ducking and its cancellation.
 *
 * The shape is deliberately close to eSpeak's: one callback receives audio
 * and index marks interleaved in stream order, and returning non-zero from
 * it aborts synthesis at once.
 *
 *   tvtts_synth *s = tvtts_create(11025);
 *   tvtts_set_voice(s, 0);
 *   tvtts_speak_utf8(s, "Hello world.", on_event, NULL);
 *   tvtts_destroy(s);
 *
 * Thread safety: a tvtts_synth is not thread safe; give each thread its
 * own.  tvtts_add_lexicon is process-global (the engine's user lexicon is
 * a single static table) and is not thread safe against anything.
 */
#ifndef TVTTS_H
#define TVTTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(TVTTS_SHARED)
#  ifdef TVTTS_BUILD
#    define TVTTS_API __declspec(dllexport)
#  else
#    define TVTTS_API __declspec(dllimport)
#  endif
#else
#  define TVTTS_API
#endif

/* Everything is cdecl, and the DLL exports undecorated names, so ctypes
 * and P/Invoke can bind it without a mangled-name dance. */
#define TVTTS_CALL

typedef struct tvtts_synth tvtts_synth;

enum {
    TVTTS_AUDIO = 0,   /* samples are ready */
    TVTTS_MARK  = 1,   /* synthesis reached an index mark */
    TVTTS_END   = 2    /* end of this utterance; no samples follow */
};

/*
 * One event.  Audio and marks arrive in stream order, so when a mark
 * arrives the caller has already been handed every sample that precedes
 * it: queue the audio, remember sample_pos, and raise the bookmark when
 * the play cursor passes it.
 *
 * Every field is 32 bits or smaller and the struct has no padding holes on
 * any normal ABI, so it binds from other languages without surprises.
 */
typedef struct {
    int32_t        type;        /* TVTTS_AUDIO / TVTTS_MARK / TVTTS_END */
    uint32_t       count;       /* TVTTS_AUDIO: number of samples */
    const int16_t *samples;     /* TVTTS_AUDIO: signed 16-bit mono, host order */
    uint32_t       mark;        /* TVTTS_MARK: the value from the escape */
    uint32_t       sample_pos;  /* samples emitted before this event */
} tvtts_event;

/* Return non-zero to abort synthesis immediately. */
typedef int (TVTTS_CALL *tvtts_callback)(const tvtts_event *ev, void *user);

/* rate is 11025 (the engine's native rate) or 8000.  NULL on failure. */
TVTTS_API tvtts_synth *TVTTS_CALL tvtts_create(uint32_t sample_rate);
TVTTS_API void TVTTS_CALL tvtts_destroy(tvtts_synth *s);

/*
 * Speak.  Blocks until the utterance finishes or the callback aborts it.
 * Returns 0 when it finished, 1 when the callback stopped it, negative on
 * error.  The synth is left ready for the next call either way.
 *
 * The engine reads cp1252, which is the encoding SAPI's WideCharToMultiByte
 * handed it; the utf8 and utf16 entry points convert the same way, so a
 * character the engine cannot represent becomes '?' exactly as it did then.
 * tvtts_speak_bytes takes cp1252 directly, for callers that already have it.
 */
TVTTS_API int TVTTS_CALL tvtts_speak_utf8(tvtts_synth *s, const char *text,
                                          tvtts_callback cb, void *user);
TVTTS_API int TVTTS_CALL tvtts_speak_utf16(tvtts_synth *s, const uint16_t *text,
                                           tvtts_callback cb, void *user);
TVTTS_API int TVTTS_CALL tvtts_speak_bytes(tvtts_synth *s, const void *text,
                                           uint32_t len, tvtts_callback cb,
                                           void *user);

/*
 * Index marks.  The engine takes them inline in the text, so a caller with
 * a sequence of (text, bookmark) pieces writes the escape between them.
 * Writes at most 16 bytes including the terminator and returns the length,
 * or 0 if the buffer is too small.
 */
TVTTS_API int TVTTS_CALL tvtts_mark_sequence(char *buf, size_t cap, uint32_t mark);

/* Settings.  These persist across utterances, as SAPI's did. */
TVTTS_API void TVTTS_CALL tvtts_set_voice(tvtts_synth *s, int voice);
TVTTS_API void TVTTS_CALL tvtts_set_rate(tvtts_synth *s, int wpm);
TVTTS_API void TVTTS_CALL tvtts_set_pitch(tvtts_synth *s, int pitch);
TVTTS_API void TVTTS_CALL tvtts_set_volume(tvtts_synth *s, uint32_t volume);

TVTTS_API int TVTTS_CALL tvtts_get_voice(const tvtts_synth *s);
TVTTS_API int TVTTS_CALL tvtts_get_rate(const tvtts_synth *s);
TVTTS_API int TVTTS_CALL tvtts_get_pitch(const tvtts_synth *s);

/* The voices, in the order the engine indexes them. */
TVTTS_API int TVTTS_CALL tvtts_voice_count(void);
TVTTS_API const char *TVTTS_CALL tvtts_voice_name(int voice);

/* The engine's default rate and pitch for a voice, as SAPI reported them. */
TVTTS_API int TVTTS_CALL tvtts_voice_rate(int voice);
TVTTS_API int TVTTS_CALL tvtts_voice_pitch(int voice);

/*
 * Add a word to the user lexicon, as ITTSDialogs' lexicon editor did.
 * Process-global: it affects every synth in the process, including ones
 * created later.  Returns 0 on success.
 */
TVTTS_API int TVTTS_CALL tvtts_add_lexicon(const char *word, const char *phonemes);

/*
 * Reproduction knobs, for callers that need to match the original exactly.
 *
 * preformat and textin are the two front-end passes the original let you
 * turn off through the registry.  terminators is how many NUL bytes SAPI
 * put after the text; the feed routine branches on the total length, so it
 * changes the output.  The defaults (1, 1, 2) are what SAPI produced.
 */
TVTTS_API void TVTTS_CALL tvtts_set_compat(tvtts_synth *s, int preformat,
                                           int textin, int terminators);

#ifdef __cplusplus
}
#endif

#endif
