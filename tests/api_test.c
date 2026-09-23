/*
 * Tests for the library surface the corpus cannot reach.
 *
 * tools/difftest.py already drives tv.exe -- and so the whole library --
 * over hundreds of configurations and demands byte-identical PCM, which
 * covers synthesis itself.  What it never exercises is a synth used more
 * than once, a synth interrupted part way, index marks, or the text
 * conversions, and those are exactly what a screen reader leans on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/* ---- a collector the tests can point a synth at ------------------------- */

#define MAX_MARKS 32

typedef struct {
    int16_t *pcm;
    size_t   n, cap;
    uint32_t mark[MAX_MARKS];
    uint32_t mark_pos[MAX_MARKS];
    int      marks;
    int      ended;
    /* abort after this many samples, 0 for never */
    size_t   stop_after;
} sink;

static int on_event(const tvtts_event *ev, void *user)
{
    sink *s = (sink *)user;

    if (ev->type == TVTTS_AUDIO) {
        if (s->n + ev->count > s->cap) {
            s->cap = (s->cap ? s->cap * 2 : 0x8000);
            while (s->cap < s->n + ev->count)
                s->cap *= 2;
            s->pcm = (int16_t *)realloc(s->pcm, s->cap * sizeof(int16_t));
        }
        memcpy(s->pcm + s->n, ev->samples, ev->count * sizeof(int16_t));
        s->n += ev->count;
        if (s->stop_after && s->n >= s->stop_after)
            return 1;
    } else if (ev->type == TVTTS_MARK) {
        if (s->marks < MAX_MARKS) {
            s->mark[s->marks] = ev->mark;
            s->mark_pos[s->marks] = ev->sample_pos;
        }
        s->marks++;
    } else if (ev->type == TVTTS_END) {
        s->ended = 1;
    }
    return 0;
}

static void sink_free(sink *s)
{
    free(s->pcm);
    memset(s, 0, sizeof *s);
}

static int same(const sink *a, const sink *b)
{
    return a->n == b->n &&
           memcmp(a->pcm, b->pcm, a->n * sizeof(int16_t)) == 0;
}

static void say(tvtts_synth *s, const char *text, sink *out)
{
    memset(out, 0, sizeof *out);
    tvtts_speak_bytes(s, text, (uint32_t)strlen(text), on_event, out);
}

/* ---- tests --------------------------------------------------------------- */

static const char TEXT[] = "The quick brown fox jumps over the lazy dog.";

/* A fresh synth speaking TEXT: what every other rendering must equal. */
static void render_fresh(sink *out)
{
    tvtts_synth *s = tvtts_create(11025);
    say(s, TEXT, out);
    tvtts_destroy(s);
}

static void test_reuse(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink a, b, ref;

    render_fresh(&ref);
    say(s, TEXT, &a);
    say(s, TEXT, &b);
    tvtts_destroy(s);

    check(a.n > 0 && a.ended, "first utterance produced audio and ended");
    check(same(&a, &ref), "first utterance matches a fresh synth");
    check(same(&b, &ref), "second utterance on the same synth matches");
    sink_free(&a); sink_free(&b); sink_free(&ref);
}

static void test_abort(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink cut, after, ref;
    int r;

    render_fresh(&ref);

    memset(&cut, 0, sizeof cut);
    cut.stop_after = 4000;
    r = tvtts_speak_bytes(s, TEXT, (uint32_t)strlen(TEXT), on_event, &cut);
    check(r == 1, "aborting mid-utterance reports it");
    check(cut.n < ref.n, "aborted utterance is shorter than a whole one");
    check(!cut.ended, "no end event after an abort");

    say(s, TEXT, &after);
    check(same(&after, &ref), "utterance after an abort is unaffected");
    tvtts_destroy(s);
    sink_free(&cut); sink_free(&after); sink_free(&ref);
}

static void test_marks(void)
{
    tvtts_synth *s = tvtts_create(11025);
    char buf[512], m[16];
    sink out;
    int i, ordered = 1;

    buf[0] = 0;
    tvtts_mark_sequence(m, sizeof m, 11); strcat(buf, m); strcat(buf, "Alpha ");
    tvtts_mark_sequence(m, sizeof m, 22); strcat(buf, m); strcat(buf, "beta ");
    tvtts_mark_sequence(m, sizeof m, 33); strcat(buf, m); strcat(buf, "gamma.");
    say(s, buf, &out);

    check(out.marks == 3, "three marks reported");
    check(out.marks == 3 && out.mark[0] == 11 && out.mark[1] == 22 &&
          out.mark[2] == 33, "marks arrive in order with their own values");
    for (i = 1; i < out.marks && i < MAX_MARKS; i++)
        if (out.mark_pos[i] < out.mark_pos[i - 1])
            ordered = 0;
    check(ordered, "mark positions do not go backwards");
    check(out.marks > 0 && out.mark_pos[out.marks - 1] < out.n,
          "the last mark lands inside the audio");
    tvtts_destroy(s);
    sink_free(&out);
}

static void test_text(void)
{
    tvtts_synth *s = tvtts_create(11025);
    /* "caf" + e-acute + ".": cp1252 0xe9, U+00E9, UTF-8 c3 a9 */
    static const char raw[]  = { 'c', 'a', 'f', (char)0xe9, '.', 0 };
    static const char u8[]   = { 'c', 'a', 'f', (char)0xc3, (char)0xa9, '.', 0 };
    static const uint16_t u16[] = { 'c', 'a', 'f', 0x00e9, '.', 0 };
    /* a smart quote is cp1252 0x92 but U+2019, which plain Latin-1 loses */
    static const char rawq[] = { 'i', 't', (char)0x92, 's', 0 };
    static const char u8q[]  = { 'i', 't', (char)0xe2, (char)0x80,
                                 (char)0x99, 's', 0 };
    sink a, b, c;

    say(s, raw, &a);
    memset(&b, 0, sizeof b);
    tvtts_speak_utf8(s, u8, on_event, &b);
    memset(&c, 0, sizeof c);
    tvtts_speak_utf16(s, u16, on_event, &c);
    check(same(&a, &b), "utf-8 converts to the engine's encoding");
    check(same(&a, &c), "utf-16 converts to the engine's encoding");
    sink_free(&b); sink_free(&c);

    say(s, rawq, &b);
    memset(&c, 0, sizeof c);
    tvtts_speak_utf8(s, u8q, on_event, &c);
    check(same(&b, &c), "cp1252-only characters survive conversion");

    tvtts_destroy(s);
    sink_free(&a); sink_free(&b); sink_free(&c);
}

static void test_voices(void)
{
    int n = tvtts_voice_count(), i, distinct = 1, named = 1;
    sink a, b;
    tvtts_synth *s;

    check(n == 10, "ten voices");
    for (i = 0; i < n; i++) {
        const char *nm = tvtts_voice_name(i);
        int j;

        if (nm == NULL || nm[0] == 0)
            named = 0;
        for (j = 0; j < i; j++)
            if (nm && tvtts_voice_name(j) && !strcmp(nm, tvtts_voice_name(j)))
                distinct = 0;
    }
    check(named, "every voice has a name");
    check(distinct, "the names are distinct");
    check(tvtts_voice_name(0) != NULL && !strcmp(tvtts_voice_name(0), "Peter"),
          "voice 0 is Peter");
    check(tvtts_voice_name(-1) == NULL && tvtts_voice_name(n) == NULL,
          "out-of-range voices give no name");

    s = tvtts_create(11025);
    say(s, "Testing.", &a);
    tvtts_set_voice(s, 4);
    say(s, "Testing.", &b);
    check(!same(&a, &b), "changing the voice changes the audio");
    sink_free(&b);

    tvtts_set_voice(s, 0);
    tvtts_set_rate(s, 250);
    say(s, "Testing.", &b);
    check(b.n < a.n, "a higher rate makes it shorter");
    tvtts_destroy(s);
    sink_free(&a); sink_free(&b);
}

static void test_edges(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink out;
    char buf[8];

    check(tvtts_create(44100) == NULL, "an unsupported rate is refused");
    check(tvtts_speak_utf8(NULL, "x", on_event, NULL) < 0,
          "speaking through a null synth is refused");
    memset(&out, 0, sizeof out);
    tvtts_speak_bytes(s, "", 0, on_event, &out);
    check(out.ended, "empty text still ends cleanly");
    sink_free(&out);

    say(s, "Still here.", &out);
    check(out.n > 0, "the synth still works after empty text");
    sink_free(&out);

    check(tvtts_mark_sequence(buf, 2, 5) == 0, "a short mark buffer is refused");
    check(tvtts_mark_sequence(buf, sizeof buf, 5) == 4, "a mark is four bytes");
    tvtts_destroy(s);
    tvtts_destroy(NULL);
}

int main(void)
{
    test_reuse();
    test_abort();
    test_marks();
    test_text();
    test_voices();
    test_edges();
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
