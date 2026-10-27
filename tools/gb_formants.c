/*
 * What formants does the synthesiser actually receive, frame by frame?
 *
 *     gb_formants.exe <lang> <text>
 *     frame F1 F2 F3
 *
 * The accent is supposed to be a change to these numbers, so this is how to
 * tell whether it reached the audio and by how much, without guessing from a
 * spectrogram.  It wraps Synth_Frame and reads the tracks as they are handed
 * over, so it is the exact input the filter bank sees -- after stage 3's
 * rules, after the voice's adjustments, after everything.
 *
 * Tracks 9, 10 and 11 are F1, F2 and F3; the decode is stage 3's own, from
 * src/engine/stage3.c.  Nothing in the engine is changed by linking this.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"
#include "engine.h"

static int g_log;

/*
 * Which vowel owns each frame.
 *
 * Without this a dump is a wall of numbers and locating a vowel in it means
 * guessing from the shape, which is no way to compare one set of targets
 * against another.  The engine's own per-phoneme commit, Stage3_Emit, knows
 * the span exactly -- but it cannot be hooked from out here, because --wrap
 * only redirects references resolved between object files and stage3.c
 * calls its own Stage3_Emit directly.  So gb.c offers a hook instead, which
 * is NULL unless a tool sets it, and reports each General British vowel and
 * the frames it wrote.  Those are write-cursor positions: a running total,
 * never masked, so the spans do not wrap and a plain scan finds the owner.
 */
#define MAXSPAN 4096
static struct { int from, to; unsigned char ph; } g_span[MAXSPAN];
static int g_spans;

static void note_span(int32_t from, int32_t to, char sym)
{
    if (!g_log || g_spans >= MAXSPAN || to <= from)
        return;
    g_span[g_spans].from = from;
    g_span[g_spans].to = to;
    g_span[g_spans].ph = (unsigned char)sym;
    g_spans++;
}

static unsigned char owner(int pos)
{
    int i;

    for (i = 0; i < g_spans; i++)
        if (pos >= g_span[i].from && pos < g_span[i].to)
            return g_span[i].ph;
    return '?';
}

void __real_Synth_Frame(Engine *s);

void __wrap_Synth_Frame(Engine *s)
{
    int pos = s->trk_04, i;
    unsigned char p[22];

    for (i = 0; i < 22; i++)
        p[i] = s->trk_buf[i][pos & 255];
    __real_Synth_Frame(s);
    if (g_log && s->trk_04 != pos) {
        unsigned char ph = owner(pos);

        printf("%d %d %d %d %c\n", pos, p[9] * 4, p[10] * 8 + 500,
               p[11] * 16, (ph >= 0x20 && ph < 0x7f) ? ph : '?');
    }
}

static int TVTTS_CALL quiet(const tvtts_event *ev, void *user)
{
    (void)ev;
    (void)user;
    return 0;
}

/* Voices are numbered across every language at once, so a British synth has
 * to be given a British number or the API hands it an American speaker and
 * quietly switches language to suit. */
static int base_voice(const char *lang)
{
    int i, n = tvtts_voice_count();

    for (i = 0; i < n; i++) {
        const char *c = tvtts_voice_language(i);

        if (c != NULL && strcmp(c, lang) == 0)
            return i;
    }
    return 0;
}

int main(int argc, char **argv)
{
    tvtts_synth *s;

    if (argc < 3) {
        fprintf(stderr, "usage: gb_formants <lang> <text>\n");
        return 2;
    }
    s = tvtts_create_lang(11025, argv[1]);
    if (s == NULL) {
        fprintf(stderr, "no synth for %s\n", argv[1]);
        return 1;
    }
    tvtts_set_voice(s, base_voice(argv[1]));
    tv_gb_frame_trace = note_span;
    g_log = 1;
    tvtts_speak_bytes(s, argv[2], (uint32_t)strlen(argv[2]), quiet, NULL);
    g_log = 0;
    tv_gb_frame_trace = NULL;
    if (getenv("GBF_SPANS") != NULL) {
        int i;

        for (i = 0; i < g_spans; i++)
            fprintf(stderr, "span %3d..%3d  %c\n", g_span[i].from,
                    g_span[i].to,
                    (g_span[i].ph >= 0x20 && g_span[i].ph < 0x7f)
                        ? g_span[i].ph : '?');
    }
    tvtts_destroy(s);
    return 0;
}
