/*
 * tv - speak text with the decompiled engine.
 *
 * Nothing here loads CGRM_EN.DLL, talks to SAPI or reads the registry.  The
 * engine is the C in src/engine and the constant tables it reads were taken
 * out of the original image at build time (tools/gen_data.py), so the
 * program that comes out is self-contained.
 *
 * What this does is what the SAPI engine thread did for one
 * ITTSCentral::TextData call: construct the engine, feed the text into its
 * input ring, step it until it says it is idle, and collect the PCM it
 * hands over on the way.
 *
 * usage: tv [-v 0-9] [-8] [-p pitch] [-s wpm] [-V volume] [-P0] [-T0]
 *           [-z nuls] [-L word=phonemes] <text|@file> <out.wav>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "crt.h"

/* The original's operator new asked for this much; the struct is a little
 * smaller, and the spare tail keeps any stray write inside our allocation. */
#define ENGINE_ALLOC 0x9200
#define OUTBUF_SIZE  0x34bc

/* @0x100b5350 */ extern const uint32_t g_voice_pitch[10];
/* @0x100b53a0 */ extern const uint32_t g_voice_speed[10];

/* @0x10003d00 */
void TV_CDECL UserLex_Add(const char *word, const char *pron);

typedef struct {
    uint8_t *data;
    size_t len, cap;
} bytebuf;

static void bb_append(bytebuf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 4096;
        b->data = (uint8_t *)realloc(b->data, b->cap);
        if (b->data == NULL) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static int write_wav(const char *path, const uint8_t *pcm, uint32_t n,
                     uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t s;

    if (f == NULL)
        return -1;
    fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f);            /* PCM */
    s = 1; fwrite(&s, 2, 1, f);            /* mono */
    fwrite(&rate, 4, 1, f);
    v = rate * 2; fwrite(&v, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    fwrite(pcm, 1, n, f);
    fclose(f);
    return 0;
}

static char *load_text(const char *arg, uint32_t *len)
{
    char *t;

    if (arg[0] == '@') {
        FILE *f = fopen(arg + 1, "rb");
        long n;

        if (f == NULL) {
            fprintf(stderr, "cannot open %s\n", arg + 1);
            exit(1);
        }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        t = (char *)malloc((size_t)n + 1);
        n = (long)fread(t, 1, (size_t)n, f);
        t[n] = '\0';
        fclose(f);
        *len = (uint32_t)n;
        return t;
    }
    *len = (uint32_t)strlen(arg);
    t = (char *)malloc(*len + 1);
    memcpy(t, arg, *len + 1);
    return t;
}

int main(int argc, char **argv)
{
    int voice = 0, phone = 0, i;
    int nuls = 2, opt_preformat = 1, opt_textin = 1;
    long opt_pitch = -1, opt_speed = -1, opt_volume = -1;
    const char *lex_add[16];
    int n_lex = 0;
    const char *textarg, *out;
    Engine *E;
    /* Where the host keeps the settings the engine reads back and writes to
     * when the text changes voice mid-sentence.  The engine calls it the
     * central object because that is what SAPI passed it; here it is just
     * this program's own block of settings. */
    SapiCentral host;
    char *text;
    uint32_t textlen, pos = 0, rate;
    int pending = 0, fed_all = 0, steps = 0, r;
    bytebuf pcm;

    memset(&pcm, 0, sizeof pcm);
    setvbuf(stderr, NULL, _IONBF, 0);
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) voice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-8")) phone = 1;
        else if (!strcmp(argv[i], "-z") && i + 1 < argc) nuls = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) opt_pitch = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) opt_speed = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-V") && i + 1 < argc) opt_volume = (long)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-P0")) opt_preformat = 0;
        else if (!strcmp(argv[i], "-T0")) opt_textin = 0;
        else if (!strcmp(argv[i], "-L") && i + 1 < argc && n_lex < 16)
            lex_add[n_lex++] = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (argc - i != 2 || voice < 0 || voice > 9) {
        fprintf(stderr, "usage: tv [-v 0-9] [-8] [-p pitch] [-s wpm]"
                        " [-V volume] [-P0] [-T0] [-z nuls]"
                        " [-L word=phonemes] <text|@file> <out.wav>\n");
        return 2;
    }
    textarg = argv[i];
    out = argv[i + 1];

    /* User lexicon entries, as the SAPI lexicon calls would add them. */
    for (i = 0; i < n_lex; i++) {
        char buf[256], *eq;

        strncpy(buf, lex_add[i], sizeof buf - 1);
        buf[sizeof buf - 1] = '\0';
        eq = strchr(buf, '=');
        if (eq == NULL) {
            fprintf(stderr, "bad -L %s\n", lex_add[i]);
            return 2;
        }
        *eq = '\0';
        UserLex_Add(buf, eq + 1);
    }

    rate = phone ? 8000u : 11025u;
    memset(&host, 0, sizeof host);
    host.voice = voice;
    host.pitch = (int16_t)(uint16_t)g_voice_pitch[voice];
    host.speed = (int32_t)g_voice_speed[voice];
    host.volume = 0xffff;
    host.ctx = 0;
    if (opt_pitch >= 0) host.pitch = (int16_t)opt_pitch;
    if (opt_speed >= 0) host.speed = (int32_t)opt_speed;
    if (opt_volume >= 0) host.volume = (int32_t)opt_volume;

    text = load_text(textarg, &textlen);
    /* ITTSCentral::TextData converted the caller's Unicode text and enqueued
     * it with one extra NUL of its own; SDK callers conventionally counted
     * their own terminator too, so the engine saw two.  The count matters:
     * the feed routine branches on the total length. */
    text = (char *)realloc(text, textlen + (uint32_t)nuls + 1);
    memset(text + textlen, 0, (size_t)nuls + 1);
    textlen += (uint32_t)nuls;

    E = (Engine *)calloc(1, ENGINE_ALLOC);
    if (E == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    Engine_Construct(E);
    E->w_212e = 1;
    E->w_212c = 0;
    E->w_2130 = 1;
    E->sapi = &host;
    E->sample_rate = (uint16_t)rate;
    E->fmt_2104 = 1;
    Engine_Init(E);
    E->preformat = (uint8_t)opt_preformat;
    E->textin_on = (uint8_t)opt_textin;
    E->out_buf = (uint8_t *)calloc(1, OUTBUF_SIZE);
    E->out_count = 0;
    if (E->textin_on)
        Engine_CreateTextIn(E);

    for (;;) {
        int flush = -1;

        if (E->st_idle && E->st_input_empty) {
            if (E->item_done) {
                if (fed_all) {
                    if (!pending)
                        break;   /* nothing queued and nothing running */
                    goto params;
                }
                pos = 0;
                E->item_done = 0;
                E->item_notify = 0;
                Engine_Feed(E, text, textlen, &pos);
                if (E->item_done)
                    fed_all = 1;
                flush = 1;
            }
        }
        if (flush < 0) {
            if (!E->item_done && Engine_InFree(E) > 0x800) {
                Engine_Feed(E, text, textlen, &pos);
                if (E->item_done)
                    fed_all = 1;
            } else if (E->st_idle) {
                flush = 0;
            }
        }
        if (flush >= 0) {
            Engine_Flush(E, flush);
            pending = 1;
        }
    params:
        if (E->cur_pitch != (uint32_t)(uint16_t)host.pitch) {
            E->cur_pitch = (uint32_t)(uint16_t)host.pitch;
            Engine_SetPitch(E, (int32_t)E->cur_pitch);
        }
        if (E->cur_speed != (uint32_t)host.speed) {
            E->cur_speed = (uint32_t)host.speed;
            Engine_SetSpeed(E, (int32_t)E->cur_speed);
        }
        if (E->cur_volume != (uint32_t)host.volume) {
            E->cur_volume = (uint32_t)host.volume;
            Engine_SetVolume(E, E->cur_volume);
        }
        if (E->cur_bac != (uint32_t)host.ctx)
            E->cur_bac = (uint32_t)host.ctx;
        if ((int32_t)E->cur_voice != host.voice) {
            E->cur_voice = (int16_t)host.voice;
            Engine_SetVoice(E, (uint32_t)(int32_t)E->cur_voice);
        }
        r = Engine_Step(E);
        steps++;
        if (r & 2) {
            bb_append(&pcm, E->out_buf, E->out_count);
            E->out_count = 0;
        }
        if (r & 1)
            pending = 0;
        if (steps > 10000000) {
            fprintf(stderr, "runaway loop\n");
            break;
        }
    }

    fprintf(stderr, "steps=%d pcm=%u bytes (%.2f s @ %u Hz)\n", steps,
            (unsigned)pcm.len, pcm.len / 2.0 / rate, (unsigned)rate);
    return write_wav(out, pcm.data, (uint32_t)pcm.len, rate) ? 1 : 0;
}
