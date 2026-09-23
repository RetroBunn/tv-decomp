/*
 * The flat C library: tvtts.h over the decompiled engine.
 *
 * This file is the layer the engine calls "the central object" -- what SAPI
 * used to be.  It owns the engine, holds the settings block the engine reads
 * back, and terminates the two paths the engine uses to talk upwards: the
 * bookmark queue (Sapi_QueuePush, below) and the window notifications (no-ops
 * in src/engine/sapi.c).
 *
 * The driving loop is the one src/port/main.c used to carry, which is the
 * sequence the SAPI engine thread ran for one ITTSCentral::TextData call.
 * It is byte-exact against the original across the whole corpus, so it is
 * copied here rather than rewritten.
 */
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "crt.h"
#include "tvtts.h"

/* The engine object is the original's size; it lives on a thread stack in
 * the original, so it has no allocator of its own. */
#define ENGINE_ALLOC 0x9200
#define OUTBUF_SIZE  0x34bc

/* @0x100b5350 */ extern const uint32_t g_voice_pitch[10];
/* @0x100b53a0 */ extern const uint32_t g_voice_speed[10];
/* The speaker names, each an ANSI string followed by the same name in
 * UTF-16, both padded to four bytes -- the table the SAPI mode-info block
 * was filled from.  See docs/VOICES.md. */
/* @0x100bf638 */ extern const char g_voice_names[];

extern uint8_t UserLex_Add(const char *word, const char *phonemes);

#define TV_VOICES 10

struct tvtts_synth {
    /* First, so Sapi_QueuePush can get back here from Engine.sapi. */
    SapiCentral host;
    Engine     *eng;
    uint8_t    *outbuf;
    uint32_t    rate;
    int         started;         /* an utterance has run on this engine */
    int         preformat, textin, nuls;
    /* valid only for the duration of one tvtts_speak call */
    tvtts_callback cb;
    void          *user;
    uint32_t       pos;          /* samples emitted so far this utterance */
    int            aborted;
};

/* Sapi_QueuePush recovers the synth by casting; make that true. */
typedef char tvtts_host_first[offsetof(struct tvtts_synth, host) == 0 ? 1 : -1];

/* ---- the engine's upward calls ------------------------------------------ */

static int emit(tvtts_synth *s, int32_t type, const int16_t *smp,
                uint32_t count, uint32_t mark, uint32_t pos)
{
    tvtts_event ev;

    if (s->cb == NULL || s->aborted)
        return s->aborted;
    ev.type = type;
    ev.count = count;
    ev.samples = smp;
    ev.mark = mark;
    ev.sample_pos = pos;
    if (s->cb(&ev, s->user) != 0)
        s->aborted = 1;
    return s->aborted;
}

/*
 * Where a mark's audio will land.
 *
 * A mark fires when stage 3 reaches it, and stage 3 runs ahead of the
 * synthesizer: it has already written this mark's frames into the parameter
 * tracks, but the synthesizer has not turned them into samples yet.  So the
 * event arrives before its own audio, by however much is queued -- twelve
 * frames in practice, about 120 ms.  Reporting the raw output position would
 * make a screen reader move its caret that far early.
 *
 * The earliest of the 22 track write positions is where this mark's frames
 * sit, and a frame is a hundredth of a second of output, so that position
 * scaled by the frame size is where the mark belongs in the stream.  Checked
 * against a mark placed before the first word: it lands within one frame of
 * where speech actually starts.
 */
static uint32_t mark_position(const tvtts_synth *s)
{
    const Engine *E = s->eng;
    int32_t lo = E->trk_wr[0];
    int i;

    for (i = 1; i < 22; i++)
        if (E->trk_wr[i] < lo)
            lo = E->trk_wr[i];
    if (lo < 0)
        lo = 0;
    return (uint32_t)lo * (s->rate / 100u);
}

/*
 * Where a bookmark surfaces.  Stage 3 reaches an index-mark node, builds a
 * three-word record and hands it to the layer above; the record's first word
 * says what it is (0 a bookmark, 1 a phoneme trace) and the queue then owns
 * it.  The original put it on the same queue as the audio so the SAPI layer
 * could raise it in step with playback; here it goes straight to the caller,
 * which has already been handed every sample before this point.
 */
int32_t Sapi_QueuePush(SapiCentral *ctl, const void *data, uint32_t size)
{
    tvtts_synth *s = (tvtts_synth *)ctl;
    uint32_t *rec;

    if (size != sizeof(void *) || data == NULL)
        return 0;
    rec = *(uint32_t *const *)data;
    if (rec == NULL)
        return 0;
    /* rec[0] is 0 for a bookmark and 1 for a phoneme trace.  Mark 0 is the
     * engine's own end-of-item marker rather than one of the caller's, and
     * it also tells the node to stop reporting, so it is not passed on. */
    if (rec[0] == 0 && rec[2] != 0)
        emit(s, TVTTS_MARK, NULL, 0, rec[2], mark_position(s));
    tv_delete(rec);
    return 0;
}

/* ---- text ---------------------------------------------------------------- */

/* The 27 places cp1252 differs from Latin-1.  Anything with no cp1252 byte
 * becomes '?', which is what WideCharToMultiByte substituted. */
static int cp1252_from_unicode(uint32_t u)
{
    static const uint16_t high[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
    };
    int i;

    if (u < 0x80 || (u >= 0xa0 && u <= 0xff))
        return (int)u;
    for (i = 0; i < 32; i++)
        if (high[i] == u)
            return 0x80 + i;
    return '?';
}

static char *to_cp1252_utf16(const uint16_t *w, uint32_t *out_len)
{
    uint32_t len = 0, n = 0, i;
    char *b;

    while (w[len] != 0)
        len++;
    b = (char *)malloc(len + 1);
    if (b == NULL)
        return NULL;
    for (i = 0; i < len; i++) {
        uint32_t u = w[i];

        /* A surrogate pair is a character beyond U+FFFF, which cp1252 has
         * no byte for; consume both halves and substitute once. */
        if (u >= 0xd800 && u <= 0xdbff && i + 1 < len &&
            w[i + 1] >= 0xdc00 && w[i + 1] <= 0xdfff) {
            i++;
            b[n++] = '?';
            continue;
        }
        b[n++] = (char)cp1252_from_unicode(u);
    }
    b[n] = 0;
    *out_len = n;
    return b;
}

static char *to_cp1252_utf8(const char *u8, uint32_t *out_len)
{
    size_t cap = strlen(u8) + 1;
    char *b = (char *)malloc(cap);
    const unsigned char *p = (const unsigned char *)u8;
    uint32_t n = 0;

    if (b == NULL)
        return NULL;
    while (*p) {
        uint32_t u = *p;
        int extra = 0;

        if (u >= 0xf0)      { u &= 0x07; extra = 3; }
        else if (u >= 0xe0) { u &= 0x0f; extra = 2; }
        else if (u >= 0xc0) { u &= 0x1f; extra = 1; }
        else if (u >= 0x80) { u = '?';   extra = 0; }  /* stray continuation */
        p++;
        while (extra-- > 0 && (*p & 0xc0) == 0x80)
            u = (u << 6) | (*p++ & 0x3f);
        b[n++] = (char)cp1252_from_unicode(u);
    }
    b[n] = 0;
    *out_len = n;
    return b;
}

int TVTTS_CALL tvtts_mark_sequence(char *buf, size_t cap, uint32_t mark)
{
    char digits[12];
    int nd = 0, n = 0;

    if (buf == NULL || cap < 4)
        return 0;
    do {
        digits[nd++] = (char)('0' + mark % 10u);
        mark /= 10u;
    } while (mark != 0);
    if (cap < (size_t)nd + 4)
        return 0;
    buf[n++] = 0x1b;
    buf[n++] = '[';
    while (nd > 0)
        buf[n++] = digits[--nd];
    buf[n++] = 'i';
    buf[n] = 0;
    return n;
}

/* ---- lifetime ------------------------------------------------------------ */

tvtts_synth *TVTTS_CALL tvtts_create(uint32_t sample_rate)
{
    tvtts_synth *s;

    if (sample_rate != 11025 && sample_rate != 8000)
        return NULL;
    s = (tvtts_synth *)calloc(1, sizeof *s);
    if (s == NULL)
        return NULL;
    s->eng = (Engine *)calloc(1, ENGINE_ALLOC);
    s->outbuf = (uint8_t *)calloc(1, OUTBUF_SIZE);
    if (s->eng == NULL || s->outbuf == NULL) {
        free(s->eng);
        free(s->outbuf);
        free(s);
        return NULL;
    }
    s->rate = sample_rate;
    s->preformat = 1;
    s->textin = 1;
    s->nuls = 2;

    s->host.voice = 0;
    s->host.pitch = (int16_t)(uint16_t)g_voice_pitch[0];
    s->host.speed = (int32_t)g_voice_speed[0];
    s->host.volume = 0xffff;
    s->host.ctx = 0;

    Engine_Construct(s->eng);
    s->eng->w_212e = 1;
    s->eng->w_212c = 0;          /* no phoneme trace: nothing collects it */
    s->eng->w_2130 = 1;
    s->eng->sapi = &s->host;
    s->eng->sample_rate = (uint16_t)sample_rate;
    s->eng->fmt_2104 = 1;
    Engine_Init(s->eng);
    s->eng->preformat = 1;
    s->eng->textin_on = 1;
    s->eng->out_buf = s->outbuf;
    s->eng->out_count = 0;
    Engine_CreateTextIn(s->eng);
    return s;
}

void TVTTS_CALL tvtts_destroy(tvtts_synth *s)
{
    if (s == NULL)
        return;
    free(s->eng);
    free(s->outbuf);
    free(s);
}

/* ---- settings ------------------------------------------------------------ */

void TVTTS_CALL tvtts_set_voice(tvtts_synth *s, int voice)
{
    if (s != NULL && voice >= 0 && voice < TV_VOICES)
        s->host.voice = voice;
}

void TVTTS_CALL tvtts_set_rate(tvtts_synth *s, int wpm)
{
    if (s != NULL)
        s->host.speed = wpm;
}

void TVTTS_CALL tvtts_set_pitch(tvtts_synth *s, int pitch)
{
    if (s != NULL)
        s->host.pitch = (int16_t)pitch;
}

void TVTTS_CALL tvtts_set_volume(tvtts_synth *s, uint32_t volume)
{
    if (s != NULL)
        s->host.volume = (int32_t)volume;
}

int TVTTS_CALL tvtts_get_voice(const tvtts_synth *s)
{
    return s != NULL ? s->host.voice : -1;
}

int TVTTS_CALL tvtts_get_rate(const tvtts_synth *s)
{
    return s != NULL ? (int)s->host.speed : -1;
}

int TVTTS_CALL tvtts_get_pitch(const tvtts_synth *s)
{
    return s != NULL ? (int)(uint16_t)s->host.pitch : -1;
}

void TVTTS_CALL tvtts_set_compat(tvtts_synth *s, int preformat, int textin,
                                 int terminators)
{
    if (s == NULL)
        return;
    s->preformat = preformat ? 1 : 0;
    s->textin = textin ? 1 : 0;
    s->nuls = terminators < 0 ? 0 : terminators;
}

int TVTTS_CALL tvtts_voice_count(void)
{
    return TV_VOICES;
}

/* Walk the paired ANSI/UTF-16 name table; see docs/VOICES.md. */
static const char *voice_entry(int voice)
{
    const char *p = g_voice_names;
    int i;

    if (voice < 0 || voice >= TV_VOICES)
        return NULL;
    for (i = 0; i < voice; i++) {
        size_t n = strlen(p) + 1;          /* the ANSI name */
        p += (n + 3) & ~(size_t)3;
        n = 0;
        while (p[n] != 0 || p[n + 1] != 0) /* the UTF-16 copy */
            n += 2;
        p += (n + 2 + 3) & ~(size_t)3;
    }
    return p;
}

const char *TVTTS_CALL tvtts_voice_name(int voice)
{
    return voice_entry(voice);
}

int TVTTS_CALL tvtts_voice_rate(int voice)
{
    return (voice >= 0 && voice < TV_VOICES) ? (int)g_voice_speed[voice] : -1;
}

int TVTTS_CALL tvtts_voice_pitch(int voice)
{
    return (voice >= 0 && voice < TV_VOICES) ? (int)g_voice_pitch[voice] : -1;
}

int TVTTS_CALL tvtts_add_lexicon(const char *word, const char *phonemes)
{
    if (word == NULL || phonemes == NULL)
        return -1;
    return UserLex_Add(word, phonemes) ? 0 : -1;
}

/* ---- synthesis ----------------------------------------------------------- */

int TVTTS_CALL tvtts_speak_bytes(tvtts_synth *s, const void *text, uint32_t len,
                                 tvtts_callback cb, void *user)
{
    Engine *E;
    char *buf;
    uint32_t textlen, pos = 0;
    int pending = 0, fed_all = 0, r;
    long steps = 0;

    if (s == NULL || (text == NULL && len != 0))
        return -1;
    E = s->eng;

    /* SAPI enqueued the caller's text with a NUL of its own, and SDK callers
     * conventionally counted their own too.  The count matters: the feed
     * routine branches on the total length. */
    buf = (char *)malloc(len + (uint32_t)s->nuls + 1);
    if (buf == NULL)
        return -1;
    if (len != 0)
        memcpy(buf, text, len);
    memset(buf + len, 0, (size_t)s->nuls + 1);
    textlen = len + (uint32_t)s->nuls;

    if (s->started)
        Engine_Reset(E);
    s->started = 1;
    E->preformat = (uint8_t)s->preformat;
    E->textin_on = (uint8_t)s->textin;
    E->out_count = 0;
    s->cb = cb;
    s->user = user;
    s->pos = 0;
    s->aborted = 0;

    for (;;) {
        int flush = -1;

        if (E->st_idle && E->st_input_empty) {
            if (E->item_done) {
                if (fed_all) {
                    if (!pending)
                        break;
                    goto params;
                }
                pos = 0;
                E->item_done = 0;
                /* SAPI put the caller's TextData context here, and an index
                 * mark only reports itself when it is non-zero; the library
                 * has no such context, so a constant stands in for it. */
                E->item_notify = 1;
                Engine_Feed(E, buf, textlen, &pos);
                if (E->item_done)
                    fed_all = 1;
                flush = 1;
            }
        }
        if (flush < 0) {
            if (!E->item_done && Engine_InFree(E) > 0x800) {
                Engine_Feed(E, buf, textlen, &pos);
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
        if (E->cur_pitch != (uint32_t)(uint16_t)s->host.pitch) {
            E->cur_pitch = (uint32_t)(uint16_t)s->host.pitch;
            Engine_SetPitch(E, (int32_t)E->cur_pitch);
        }
        if (E->cur_speed != (uint32_t)s->host.speed) {
            E->cur_speed = (uint32_t)s->host.speed;
            Engine_SetSpeed(E, (int32_t)E->cur_speed);
        }
        if (E->cur_volume != (uint32_t)s->host.volume) {
            E->cur_volume = (uint32_t)s->host.volume;
            Engine_SetVolume(E, E->cur_volume);
        }
        if (E->cur_bac != (uint32_t)s->host.ctx)
            E->cur_bac = (uint32_t)s->host.ctx;
        if ((int32_t)E->cur_voice != s->host.voice) {
            E->cur_voice = (int16_t)s->host.voice;
            Engine_SetVoice(E, (uint32_t)(int32_t)E->cur_voice);
        }

        r = Engine_Step(E);
        if (r & 2) {
            uint32_t n = E->out_count / 2;
            E->out_count = 0;
            if (n != 0) {
                emit(s, TVTTS_AUDIO, (const int16_t *)s->outbuf, n, 0, s->pos);
                s->pos += n;
            }
        }
        if (r & 1)
            pending = 0;
        if (s->aborted)
            break;
        if (++steps > 10000000L)     /* the engine has stopped making progress */
            break;
    }

    if (!s->aborted)
        emit(s, TVTTS_END, NULL, 0, 0, s->pos);
    r = s->aborted;
    s->cb = NULL;
    s->user = NULL;
    free(buf);
    return r;
}

int TVTTS_CALL tvtts_speak_utf8(tvtts_synth *s, const char *text,
                                tvtts_callback cb, void *user)
{
    uint32_t len;
    char *b;
    int r;

    if (s == NULL || text == NULL)
        return -1;
    b = to_cp1252_utf8(text, &len);
    if (b == NULL)
        return -1;
    r = tvtts_speak_bytes(s, b, len, cb, user);
    free(b);
    return r;
}

int TVTTS_CALL tvtts_speak_utf16(tvtts_synth *s, const uint16_t *text,
                                 tvtts_callback cb, void *user)
{
    uint32_t len;
    char *b;
    int r;

    if (s == NULL || text == NULL)
        return -1;
    b = to_cp1252_utf16(text, &len);
    if (b == NULL)
        return -1;
    r = tvtts_speak_bytes(s, b, len, cb, user);
    free(b);
    return r;
}
