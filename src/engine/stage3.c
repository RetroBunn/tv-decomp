/*
 * Stage 3: phonetics.
 *
 * Stage 3 is where the phoneme string finally becomes sound: it walks the
 * nodes stage 2 sized and writes the 22 synthesis parameter tracks, frame by
 * frame, using the per-phoneme target tables and the transition rules that
 * say how each parameter travels from one phoneme to the next.
 */
#include "engine.h"

/* @0x100c8aa0 with the signed index the original uses; see stage0.c */
uint8_t Phone_Attr(int32_t idx);

/* The twenty-four vowels, in the order the stage-3 tables index them. */
/* @0x100bf7d8 */ extern const uint8_t g_vowel_chars[24];

/* Where in that order this phoneme sits, or -1 if it is not one of them. */
/* @0x100386d0 */
int32_t TV_STDCALL Vowel_Index(uint8_t c)
{
    int32_t i;

    for (i = 0; i < 0x18; i++)
        if (g_vowel_chars[i] == c)
            break;
    return (i == 0x18) ? -1 : i;
}

/* One of the four phonemes around stage 3's cursor, chosen by bits 1-2. */
/* @0x10051b70 */
uint8_t TV_THISCALL Stage3_Char(Engine *self, int32_t which)
{
    StageCtx *st = &self->stage_ctx[3];

    switch ((which & 6) >> 1) {
    case 0:  return st->ctl->value;
    case 1:  return st->scan->value;
    case 2:  return st->scan->next->value;
    default: return st->cur->prev->value;
    }
}

/* Flag bits per parameter index (shared with stage 0). */
/* @0x100f83a0 */ extern const uint32_t g_s0_flag_lo[16];

/* The sign-extended index Phone_Attr wants for a phoneme byte. */
static int32_t px3(uint8_t b)
{
    return (int32_t)(int8_t)b;
}

/* The rest of the stage, not yet decompiled. */
/* @0x1002cdf0 */
int32_t TV_THISCALL Stage3_Control(Engine *self);
/* @0x1002e060 */
Node *TV_THISCALL Stage3_Next(Engine *self);
/* @0x1002da60 */
void TV_THISCALL Stage3_Phone(Engine *self);
/* @0x1002dfa0 */
void TV_THISCALL Stage3_Emit(Engine *self);

/*
 * One step of stage 3.
 *
 * The stage keeps three cursors: cur is the phoneme it has finished, ctl the
 * one it is writing, and scan the one after.  Each step moves them on by one
 * phoneme and writes that phoneme's frames into the parameter tracks.
 * s3_1fe0 is how long a transition is allowed, which is longer when the
 * voice is speaking slowly; s3_1fb4 is a small state machine that says how
 * far into the utterance the stage is.
 *
 * Returns 0 idle, 1 when the track buffers are full, 2 when there is more.
 */
/* @0x1002c980 */
int32_t TV_THISCALL Stage3_Run(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *n, *p;
    int32_t freed = 0;
    int32_t v, i;
    uint32_t t;

    Engine_StageBegin(self, st);
    if (st->cur == NULL && st->ctl == NULL && st->scan == NULL)
        goto done;

    if (!(((uint32_t)st->p_34 >> 8) & 0x20) && st->rate_index < 0x13) {
        self->s3_1fe8 = 0x1e;
        self->s3_1fe4 = 0xf;
        self->s3_1fe0 = 0xc;
    } else {
        self->s3_1fe8 = 4;
        self->s3_1fe4 = 3;
        self->s3_1fe0 = 2;
    }

    if (self->trk_08 != -1 &&
        self->trk_0c - self->trk_04 - self->s3_1fe0 <= 8 &&
        self->s3_1fb4 == 0)
        self->s3_1fb4 = 1;

    n = st->ctl;
    v = self->s3_1fb0;
    if (v == 2 && NODE_TYPE(n) == 4 && n->value == ' ' &&
        (n->flags & 0x18u) == 0x18) {
        self->s3_1fbc = 1;
        goto done;
    }

    if (v != 3) {
        if (n == NULL)
            goto advance;
        if (NODE_TYPE(n) == 4)
            goto sized;
    }
    self->s3_1fb0 = 3;
    if (n == NULL)
        goto advance;
    if (NODE_TYPE(n) == 4 &&
        !(n->value == ' ' && (n->flags & 0x18u) == 0x18))
        goto sized;

    /* a control node, or the silence that ends the utterance */
    freed = Stage3_Control(self);
    Tracks_Op(self, 2, 0);
    self->s3_1fdd = 1;
    self->trk_38 = (freed < 1) ? freed : 1;
    if (freed == 0) {
        n = st->ctl;
        st->cur = n;
        n = Engine_StageNext(self, n);
        st->scan = n;
        st->ctl = n;
        self->s3_1fb0 = 0;
    }
    self->s3_1fb4 = 5;
    return Engine_StageEnd(self) ? 2 : 1;

sized:
    /* a sized phoneme: ask the tracks for room for its frames */
    self->trk_38 = (int32_t)n->arg;
    if (!Tracks_Op(self, 1, (int32_t)n->arg)) {
        self->trk_08 = -1;
        self->synth_busy = 0;
        if (self->s3_1fb4 == 4)
            self->s3_1fb4 = 5;
        return Engine_StageEnd(self) ? 2 : 1;
    }
    if (self->trk_0c - self->trk_04 - self->s3_1fe0 > 0x40) {
        self->trk_08 = -1;
        self->s2_1d54 = 0;
        self->synth_busy = 0;
        if (self->s3_1fb4 == 4)
            self->s3_1fb4 = 5;
    }
    if (st->p_20 == 0) {
        self->trk_08 = -1;
        self->synth_busy = 0;
        if (self->s3_1fb4 == 4)
            self->s3_1fb4 = 5;
    }

advance:
    n = Stage3_Next(self);
    if (n == NULL)
        goto done;
    st->scan = n;
    /* remember the phoneme after the one the scan is on */
    for (;;) {
        n = Engine_StageNext(self, n);
        if (n == NULL)
            break;
        t = NODE_TYPE(n);
        if (t == 4) {
            self->s3_1fec = n->value;
            break;
        }
        if (t != 3) {
            self->s3_1fec = 0x20;
            break;
        }
    }

    if (st->ctl->value != ' ') {
        v = self->s3_1fb4;
        if (v == 0)
            self->s3_1fb4 = 4;
        else if (v == 1)
            self->s3_1fb4 = 2;
    }

    Stage3_Phone(self);
    Stage3_Emit(self);

    n = st->ctl;
    if (!(Phone_Attr(px3(n->value)) & 4))
        self->s3_param_def[17] = self->s3_1fb8;
    if (n->b1a == 1) {
        self->s3_1fb8 = 0x64;
        self->s3_param_def[17] = 0x64;
    }

    p = st->cur;
    if (NODE_TYPE(p) == 4 && p->value == ' ' && (p->flags & 0x18u) == 8) {
        freed = 1;
        Engine_NodeFree(self, p, 1);
    }

    if (self->s3_1fb4 == 2 &&
        self->trk_0c - self->trk_04 - self->s3_1fe0 > 8) {
        self->trk_08 = -1;
        self->s3_1fb4 = 3;
        self->synth_busy = 0;
    }

    self->s3_1fed = st->cur->value;
    st->cur = st->ctl;
    st->ctl = Engine_StageNext(self, st->ctl);
    st->scan = Engine_StageNext(self, st->scan);

    n = st->ctl;
    if (NODE_TYPE(n) == 4 && n->value == ' ' &&
        (n->flags & 0x18u) == 0x18) {
        /* the closing silence: stop the synthesizer where it ends */
        self->synth_busy = 0;
        self->trk_08 = self->trk_0c - self->s3_1fe0;
        p = n;
        for (;;) {
            p = Engine_StageNext(self, p);
            if (p == NULL)
                break;
            t = NODE_TYPE(p);
            if (t == 4 || t == 5)
                break;
        }
        if (p == NULL) {
            Engine_NodeFree(self, st->ctl, 0);
            st->cur = NULL;
            st->ctl = NULL;
            st->scan = NULL;
            i = self->trk_0c - 1;
            self->trk_14[(uint32_t)(i & 0xf8) >> 3] &=
                (uint8_t)~(uint8_t)g_s0_flag_lo[i & 7];
            i = self->trk_0c - self->s3_1fe0 - 1;
            self->trk_14[(uint32_t)(i & 0xf8) >> 3] |=
                (uint8_t)g_s0_flag_lo[i & 7];
        }
    }
    Tracks_Op(self, 2, 0);

done:
    if (!Engine_StageEnd(self) && freed == 0)
        return 0;
    return 2;
}

/*
 * Commit the frames this step wrote.
 *
 * Every parameter advances by its own amount (s3_param[i].len), so after a
 * phoneme the 22 write positions are no longer level.  This finds the
 * earliest and latest of them, marks the earliest frame as ready in trk_14,
 * remembers each parameter's final value for the next phoneme to start from,
 * and lets the read positions catch up to within one transition.
 */
/* @0x1002dfa0 */
void TV_THISCALL Stage3_Emit(Engine *self)
{
    int32_t lo = 0x1000, hi = -1;
    int32_t i, pos, v;

    for (i = 0; i < 22; i++) {
        pos = self->trk_wr[i] + self->s3_param[i].len;
        self->trk_wr[i] = pos;
        self->s3_param_def[i] =
            Synth_ScaleParam(i, self->trk_buf[i][(pos - 1) & 0xff]);
        if (lo > pos)
            lo = pos;
        if (hi < pos)
            hi = pos;
        v = pos - self->s3_1fe0;
        if (self->trk_rd[i] < v)
            self->trk_rd[i] = v;
    }
    i = lo - 1;
    self->trk_14[(uint32_t)(i & 0xf8) >> 3] |= (uint8_t)g_s0_flag_lo[i & 7];
    self->trk_10 = hi;
    self->trk_0c = lo;
}
