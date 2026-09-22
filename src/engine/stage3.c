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

/* @0x1002cdf0 */
int32_t TV_THISCALL Stage3_Pause(Engine *self);
/* @0x10051bd0 */
void TV_THISCALL Stage3_Rules(Engine *self);

/* @0x1002e060 */
Node *TV_THISCALL Stage3_Next(Engine *self);

/* The rest of the stage, not yet decompiled. */
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
    freed = Stage3_Pause(self);
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

/* The 22 parameter values a held frame uses, one row per node class. */
/* @0x100f8450 */ extern const uint8_t g_hold_frame[];
/* The row the "s" hold uses. */
/* @0x100f8438 */ extern const uint8_t g_hold_silence[22];
/* How much its own pitch takes off a held frame's amplitude. */
/* @0x100f8548 */ extern const uint8_t g_hold_atten[16];

/*
 * Write the frames of a held sound.
 *
 * A pause, a breath or a held tone is one parameter frame repeated, so this
 * builds that frame once (s3_hold) and then copies it into all 22 tracks for
 * as many frames as there is room for -- at most 53 in one step, so the
 * engine keeps returning to its caller.  What is left to write comes back.
 */
/* @0x1002ced0 */
int32_t TV_THISCALL Stage3_Hold(Engine *self, int32_t ch, int32_t n,
                                int32_t room, int32_t restart)
{
    StageCtx *st = &self->stage_ctx[3];
    const uint8_t *row;
    Node *node;
    int32_t i, k, v, pos, budget;
    /* The original leaves this uninitialised; the row that reads it always
     * writes it first. */
    uint8_t strong = 0;

    if (restart != 0) {
        self->s3_hold_pitch = 0;
        if ((uint8_t)ch == 'g') {
            /* hold whatever the parameters were left at */
            for (i = 0; i < 22; i++)
                self->s3_hold[i] = self->s3_param_raw[i];
        } else if ((uint8_t)ch == 's') {
            for (i = 0; i < 22; i++)
                self->s3_hold[i] = g_hold_silence[i];
        } else if ((uint8_t)ch == 't') {
            node = st->ctl;
            k = (int32_t)((node->flags & 0x18u) >> 3);
            if (node->flags & 0x20u)
                k += 4;
            if (node->flags & 0x40u)
                k += 8;
            row = g_hold_frame + k * 22;
            if (row[9] == 0xff)
                self->s3_hold_pitch = (int32_t)node->b15;
            for (i = 0; i < 22; i++) {
                v = (int32_t)row[i];
                if (row[9] == 0xff && i >= 0xd && i <= 0xf && strong != 0)
                    v = 0x2d;
                if (v == 0xff) {
                    if (i >= 9 && i <= 0xb) {
                        v = self->s3_hold_pitch;
                        strong = (uint8_t)(v > 0xb4);
                    } else if (i == 0x11) {
                        v = (int32_t)st->ctl->b15;
                    } else if (i == 0 || i == 1) {
                        v = 0x3c - st->volume_atten;
                        if (self->s3_hold_pitch != 0)
                            v -= (int32_t)
                                g_hold_atten[self->s3_hold_pitch >> 4];
                        if (v <= 0)
                            v = 1;
                    }
                }
                self->s3_hold[i] = (uint8_t)v;
            }
        }
    }

    pos = self->trk_0c;
    budget = 0x35;
    while (n > 0 && room > 0 && budget != 0) {
        for (i = 0; i < 22; i++)
            self->trk_buf[i][pos & 0xff] = self->s3_hold[i];
        if (self->s3_hold_pitch != 0) {
            k = pos - 1;
            self->s3_1fbd[(uint32_t)(k & 0xf8) >> 3] |=
                (uint8_t)g_s0_flag_lo[k & 7];
        }
        n--;
        pos++;
        room--;
        budget--;
    }
    for (i = 0; i < 22; i++) {
        self->trk_rd[i] = pos - self->s3_1fe0;
        self->trk_wr[i] = pos;
    }
    self->trk_0c = pos;
    self->trk_10 = pos;
    return n;
}

/*
 * The stage-3 step for a held sound.
 *
 * Stage3_Run hands the held node over to this until it has written all of
 * its frames; the count left is the return value, which is what makes the
 * driver come back to the same node next step.
 */
/* @0x1002cdf0 */
int32_t TV_THISCALL Stage3_Pause(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *n;
    int32_t room;

    if (self->s3_1fae != 0) {
        /* a new one: take its length off the node and start the tracks over */
        n = st->ctl;
        if (NODE_TYPE(n) == 4)
            st->ctl = Engine_NodeFree(self, n, 1);
        n = st->ctl;
        self->s3_hold_ch = n->value;
        self->s3_hold_left = (int32_t)(n->arg & 0xffu);
        Stage3_ResetParams(self);
        self->s3_1fb0 = 3;
        self->s3_1fae = 0;
        self->s3_hold_start = 1;
    }

    room = self->trk_04 - self->s3_1fe4 - self->trk_10 + 0x100;
    if (self->s3_hold_left != 0 && room != 0) {
        self->s3_hold_left = Stage3_Hold(self, self->s3_hold_ch,
                                         self->s3_hold_left, room,
                                         self->s3_hold_start);
        if (self->s3_hold_start != 0) {
            self->trk_08 = -1;
            self->synth_busy = 0;
        }
        self->s3_hold_start = 0;
    }
    if (self->s3_hold_left == 0)
        self->s3_1fae = 1;
    return self->s3_hold_left;
}

/*
 * Put a silence in front of the cursor.
 *
 * Stage 3 always needs a phoneme to work on.  When what comes next is a
 * clause break, a stop mark, or simply too many control nodes in a row, it
 * makes one: a pair of silence nodes, the first the run into the silence and
 * the second the silence proper.
 */
/* @0x1002de10 */
Node *TV_THISCALL Stage3_Insert(Engine *self, Node *ref, int32_t mode)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *a, *b;
    uint32_t f;

    a = Engine_NodeAlloc(self, ref, 1, 4, ' ');
    if (mode == 1)
        a->arg = 4;
    else
        a->arg = (uint32_t)(self->s3_1fe0 + self->s3_1fe4 + 0xa);
    a->b15 = (st->ctl != NULL) ? st->ctl->b15 : 0x32;
    f = a->flags & ~0x20u;
    a->flags = f;
    f &= ~0x40u;
    a->flags = f;
    f = (f & ~8u) | 0x10u;
    a->flags = f;

    b = Engine_NodeAlloc(self, a, 1, 4, ' ');
    b->arg = 4;
    b->b15 = (st->ctl != NULL) ? st->ctl->b15 : 0x32;
    f = b->flags & ~0x20u;
    b->flags = f;
    f &= ~0x40u;
    b->flags = f;
    f |= 0x18u;
    b->flags = f;

    if (st->ctl == NULL) {
        st->ctl = a;
        a = Engine_StageNext(self, a);
    }
    self->synth_busy = 0;
    self->trk_08 = -1;
    return a;
}

/*
 * Find the phoneme stage 3 works on next.
 *
 * It looks forward past the control nodes, noting what it passes: a "C"
 * clause command, an "x" stop mark, an end-of-list marker or a run of ten
 * control nodes all mean the sound has to stop, and s3_1fb0 says which.  In
 * those cases it inserts a silence to stop on and returns that instead.
 */
/* @0x1002e060 */
Node *TV_THISCALL Stage3_Next(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *n, *ctl;
    uint32_t f, t;
    int32_t seen = 0, v;
    uint8_t c;

    ctl = st->ctl;
    if (ctl != NULL &&
        (st->cur == ctl || NODE_TYPE(st->cur) != 4)) {
        /* nothing finished yet: give the stage something to start from */
        n = Engine_NodeAlloc(self, ctl, 0, 4, ' ');
        st->cur = n;
        n->arg = 4;
        n->b15 = st->ctl->b15;
        f = n->flags & ~0x20u;
        n->flags = f;
        f &= ~0x40u;
        n->flags = f;
        f = (f & ~0x10u) | 8u;
        n->flags = f;
    }

    n = st->ctl;
    if (n != NULL) {
        for (;;) {
            n = Engine_StageNext(self, n);
            if (n == NULL)
                break;
            t = NODE_TYPE(n);
            if (t == 4) {
                if (self->s3_1fb0 == 3 && n->value == ' ' &&
                    (n->flags & 0x18u) == 0x18)
                    return n;
                self->s3_1fb0 = 0;
                break;
            }
            seen++;
            if (t == 0) {
                c = n->value;
                if (c == 'C') {
                    self->s3_1fb0 = 1;
                    break;
                }
                if (c == 'x' || (self->stop_mark != 0 && c == 'i')) {
                    n->flags |= 0x20u;
                    self->s3_1fb0 = 2;
                    break;
                }
            } else if (t == 5) {
                self->s3_1fb0 = 3;
                break;
            } else if (t == 3 &&
                       (Phone_Attr(px3(n->value) | 0x200) & 8) &&
                       n->value != ']') {
                /* a boundary the synthesizer has to stop on */
                self->trk_08 = -1;
                self->synth_busy = 0;
                if (self->s3_1fb4 == 4)
                    self->s3_1fb4 = 5;
            }
        }
    }

    if (seen >= 0xa)
        self->s3_1fb0 = 4;

    v = self->s3_1fb0;
    if (v != 0 && (n != NULL || v == 4)) {
        if (v == 1)
            return Stage3_Insert(self, n, 1);
        if (v == 4)
            return Stage3_Insert(self, st->last, 4);
        if (v == 2)
            return Stage3_Insert(self, n, 2);
        return n;
    }
    self->trk_08 = -1;
    return n;
}

/* Where in the phoneme's own class table its transition rules live. */
/* @0x100eece0 */ extern const uint8_t *const g_phone_class;
/* @0x100ef338 */ extern const uint8_t g_class_kind[];
/* How fast the nasal parameters settle, and how far, per voice. */
/* @0x100b52e8 */ extern const int32_t g_voice_nasal_rate[];
/* @0x100b53f0 */ extern const int32_t g_voice_nasal_max[];

/*
 * The starting point for a phoneme's parameters.
 *
 * Most of a phoneme's targets carry over from the one before it; these are
 * the few that do not, which depend on whether the phoneme just finished was
 * voiced, nasal or a stop.
 */
/* @0x10048080 */
void TV_THISCALL Stage3_Defaults(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t ch = px3(st->cur->value);
    uint8_t a = Phone_Attr(ch);

    self->s3_param[17].mode = 6;
    self->s3_param_rate[17] = 0;
    if (!(a & 4)) {
        self->s3_param[0].mode = 6;
        self->trk_rd[17] = self->trk_wr[17];
    }
    if (Phone_Attr(ch | 0x180) & 2) {
        self->s3_param_rate[11] = 0x7ffe;
        self->s3_param_rate[10] = 0x7ffe;
        self->s3_param_rate[9] = 0x7ffe;
    }
    if (a & 0x20)
        self->trk_rd[1] = self->trk_wr[1];
}

/*
 * Lay one parameter's travel into its track.
 *
 * The phoneme rules leave three values behind for each parameter -- where it
 * should be in the frames before this phoneme (lead), where its own run
 * starts (start) and where it settles (target) -- along with the curves to
 * travel along.  "mode" says which of those apply, and this turns them into
 * calls on the track shapers.  Bit 0 asks for a blend backwards over the
 * frames already written, which is only possible if there are any.
 */
/* @0x100051b0 */
void TV_THISCALL Stage3_Write(Engine *self, int32_t param)
{
    S3Param *p = &self->s3_param[param];
    uint8_t *buf = self->trk_buf[param];
    int32_t pos = self->trk_wr[param];
    int32_t span = pos - self->trk_rd[param];
    int32_t mode = p->mode;
    int32_t shape_in = p->shape_in;
    int32_t shape_out = p->shape_out;
    int32_t lead = p->lead;
    int32_t start = p->start;
    int32_t target = p->target;

    if (lead < 0)
        lead = 0;
    if (start < 0)
        start = 0;

    /* the formants and the amplitudes are held wider than a byte */
    switch (param) {
    case 9:
        lead >>= 2;
        start >>= 2;
        target >>= 2;
        break;
    case 10:
        lead -= 0x1f4;
        start -= 0x1f4;
        target -= 0x1f4;
        lead >>= 3;
        start >>= 3;
        target >>= 3;
        break;
    case 11:
    case 12:
        lead >>= 4;
        start >>= 4;
        target >>= 4;
        break;
    case 13:
    case 14:
    case 15:
    case 17:
        lead >>= 1;
        start >>= 1;
        target >>= 1;
        break;
    case 16:
        lead -= 0xc0;
        start -= 0xc0;
        target -= 0xc0;
        lead >>= 2;
        start >>= 2;
        target >>= 2;
        break;
    default:
        break;
    }

    if ((mode & 1) && span <= 0)
        mode &= ~1;

    switch (mode) {
    case 1:
        Track_BlendBack(self, buf, pos, shape_in, span, (uint8_t)lead);
        break;
    case 2:
        Track_BlendFwd(self, buf, pos, shape_out, p->len, (uint8_t)start);
        break;
    case 3:
        Track_BlendBack(self, buf, pos, shape_in, span, (uint8_t)lead);
        Track_BlendFwd(self, buf, pos, shape_out, p->len, (uint8_t)start);
        break;
    case 4:
        Track_Decay(self, buf, pos, 0, p->len, 0, (uint8_t)target);
        break;
    case 5:
        Track_BlendBack(self, buf, pos, shape_in, span, (uint8_t)lead);
        Track_Decay(self, buf, pos, 0, p->len, 0, (uint8_t)target);
        break;
    case 6:
        Track_Decay(self, buf, pos, shape_out, p->len, (uint8_t)start,
                    (uint8_t)target);
        break;
    case 7:
        Track_BlendBack(self, buf, pos, shape_in, span, (uint8_t)lead);
        Track_Decay(self, buf, pos, shape_out, p->len, (uint8_t)start,
                    (uint8_t)target);
        break;
    case 8:
        Track_RampTo(buf, pos, shape_out, (uint8_t)start, (uint8_t)target);
        break;
    case 0x10:
        Track_Line(buf, pos, shape_in, shape_out);
        break;
    case 0x14:
        Track_Decay(self, buf, pos, shape_out, p->len, (uint8_t)start,
                    (uint8_t)target);
        Track_Line(buf, pos, shape_in, shape_out);
        break;
    default:
        break;
    }
}

/* The per-phoneme rule passes, not yet decompiled. */
/* @0x1002d0d0 */
void TV_THISCALL Stage3_Targets(Engine *self);
/* @0x10031050 */
void TV_THISCALL Sapi_PhoneNotify(Engine *self, int32_t ch);

/*
 * Set the parameter targets for one phoneme.
 *
 * This is the middle of stage 3.  It loads the phoneme's own targets, works
 * out which kind of sound it is (s3_flag), runs the context rules that bend
 * those targets toward the phonemes either side, takes the volume off the
 * amplitudes, adds the voice's nasal colouring, and finally asks
 * Stage3_Write to lay each of the 22 parameters into its track.
 */
/* @0x1002da60 */
void TV_THISCALL Stage3_Phone(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *n, *ctl;
    int32_t i, v, atten, voice;
    uint8_t c, d;

    c = st->ctl->value;
    Stage3_Targets(self);
    Stage3_Defaults(self);

    for (i = 0; i < 11; i++)
        self->s3_flag[i] = 0;
    n = st->scan;
    if (n->flags & 0x20u)
        self->s3_flag[10] = 1;
    if (Phone_Attr(px3(c)) & 4)
        self->s3_flag[0] = 1;

    v = self->s3_1e24;
    if (v == 0)
        self->s3_flag[1] = 1;
    else if (v == 3)
        self->s3_flag[2] = 1;
    else if (v == 1)
        self->s3_flag[3] = 1;
    else
        self->s3_flag[4] = 1;

    if ((self->s3_flag[2] != 0 &&
         (Phone_Attr(px3(st->cur->value)) & 0x10)) ||
        self->s3_flag[1] != 0)
        self->s3_flag[3] = 1;

    if (self->s3_1e28 != 0) {
        ctl = st->ctl;
        if (Phone_Attr(px3(ctl->value)) & 0x20) {
            d = n->value;
            if ((Phone_Attr(px3(d) | 0x100) & 1) || d == ' ' ||
                (ctl->flags & 0x40u))
                self->s3_flag[5] = 1;
        }
        v = (int32_t)g_class_kind[g_phone_class[px3(n->value)]];
        if (v == 0)
            self->s3_flag[6] = 1;
        else if (v == 1)
            self->s3_flag[7] = 1;
        else if (v == 3)
            self->s3_flag[8] = 1;
        else if (v == 2)
            self->s3_flag[9] = 1;
    }

    Stage3_Rules(self);

    ctl = st->ctl;
    if (!(Phone_Attr(px3(ctl->value)) & 4)) {
        self->s3_param[17].mode &= ~2;
        self->s3_1fb8 = self->s3_param[17].target;
        self->s3_param[17].target = 0;
    }
    if (ctl->b15 == 0) {
        /* no pitch: flatten the three pitch parameters */
        self->s3_param[0].start = 0;
        self->s3_param[0].lead = 0;
        self->s3_param[17].mode &= ~2;
        v = self->s3_param[0].target;
        self->s3_param[0].target = 0;
        self->s3_param[2].target = v + 5;
    }
    n = st->cur;
    if (!(Phone_Attr(px3(n->value)) & 4) || n->b15 == 0)
        self->s3_param[17].mode &= ~1;

    /* take the voice's attenuation off the amplitudes */
    atten = st->volume_atten;
    for (i = 0; i < 3; i++) {
        v = self->s3_param[i].target - atten;
        self->s3_param[i].target = (v < 0) ? 0 : v;
        v = self->s3_param[i].lead - atten;
        self->s3_param[i].lead = (v < 0) ? 0 : v;
        v = self->s3_param[i].start - atten;
        self->s3_param[i].start = (v < 0) ? 0 : v;
    }

    voice = st->voice;
    if (voice != 0) {
        /* the voice's own nasal colouring */
        v = self->s3_param[9].target;
        self->s3_param[9].target = Synth_MulQ15(v, g_voice_nasal_rate[voice]) + v;
        v = self->s3_param[10].target;
        self->s3_param[10].target =
            Synth_MulQ15(v, g_voice_nasal_rate[st->voice]) + v;
        v = self->s3_param[11].target;
        v = Synth_MulQ15(v, g_voice_nasal_rate[st->voice]) + v;
        self->s3_param[11].target = v;
        self->s3_param[12].target +=
            Synth_MulQ15(v, g_voice_nasal_rate[st->voice]);
        if (self->s3_param[12].target > g_voice_nasal_max[st->voice])
            self->s3_param[12].target = g_voice_nasal_max[st->voice];
    }

    self->s3_param[21].target |= st->voice << 4;
    self->s3_param[20].target |= st->volume_atten << 4;

    if (self->s3_1fdd != 0) {
        for (i = 0; i < 22; i++)
            self->trk_rd[i] = self->trk_wr[i];
        self->s3_1fdd = 0;
    }

    if (self->w_2130 != 0) {
        c = st->ctl->value;
        if (c != 'p')
            Sapi_PhoneNotify(self, (int32_t)c);
    }

    for (i = 0; i < 22; i++)
        Stage3_Write(self, i);
}

/* The rules, reached through the classes of the phoneme being written and
 * the one before it. */
/* @0x10095040 */ extern const uint8_t *const g_rule_sel[];
/* @0x10094870 */ extern const S3Rule *const g_rule_set[];
/* @0x10094948 */ extern const uint8_t g_rule_count[];
/* Which attribute bit an "is it ..." condition asks about. */
/* @0x100c89e0 */ extern const uint32_t g_attr_mask[128];

/* @0x10051890 */
void TV_THISCALL Stage3_Apply(Engine *self, const S3Edit *const *edits,
                              int32_t mode);

/* The sixteen routines a rule can ask for. */
/* @0x10048100 */ void TV_THISCALL Stage3_Op1(Engine *self);
/* @0x100481b0 */ void TV_THISCALL Stage3_Op2(Engine *self);
/* @0x10048310 */ void TV_THISCALL Stage3_Op3(Engine *self);
/* @0x10048620 */ void TV_THISCALL Stage3_Op4(Engine *self);
/* @0x10049100 */ void TV_THISCALL Stage3_Op5(Engine *self);
/* @0x10049260 */ void TV_THISCALL Stage3_Op6(Engine *self);
/* @0x10049570 */ void TV_THISCALL Stage3_Op7(Engine *self);
/* @0x1004a0e0 */ void TV_THISCALL Stage3_Op8(Engine *self);
/* @0x1004a200 */ void TV_THISCALL Stage3_Op9(Engine *self);
/* @0x1004fe70 */ void TV_THISCALL Stage3_Op10(Engine *self);
/* @0x1004b5d0 */ void TV_THISCALL Stage3_Op11(Engine *self);
/* @0x1004be40 */ void TV_THISCALL Stage3_Op12(Engine *self);
/* @0x1004bfc0 */ void TV_THISCALL Stage3_Op13(Engine *self);
/* @0x1004e360 */ void TV_THISCALL Stage3_Op14(Engine *self);
/* @0x100055b0 */ void TV_THISCALL Stage3_Op15(Engine *self);
/* @0x10050c30 */ void TV_THISCALL Stage3_Op16(Engine *self);

/*
 * Run the rules for this phoneme against the one before it.
 *
 * Each rule is a little byte-code program: conditions of three kinds -- is
 * one of the s3_flag bits set, does a neighbouring phoneme have a given
 * attribute, is a neighbouring phoneme one of this list -- and then, if they
 * all hold, a block of parameter edits and a list of routines to run.  The
 * first rule that matches wins.
 */
/* @0x10051bd0 */
void TV_THISCALL Stage3_Rules(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    const S3Rule *r;
    const uint8_t *s;
    int32_t i, count, k, opc, want, v, idx;
    uint32_t mask;
    uint8_t b, c, d, found;

    k = (int32_t)g_rule_sel[g_phone_class[px3(st->ctl->value)]]
                           [g_phone_class[px3(st->cur->value)]];
    r = g_rule_set[k];
    count = (int32_t)g_rule_count[k];

    for (i = 0; i < count; i++, r++) {
        s = r->cond;
        if (s != NULL) {
            for (;;) {
                b = *s;
                want = b & 1;
                opc = b >> 3;
                if (opc == 0) {
                    s++;
                    if ((int32_t)self->s3_flag[*s] != want)
                        goto next;
                    s++;
                } else if (opc == 1) {
                    c = Stage3_Char(self, (int32_t)b);
                    s++;
                    mask = g_attr_mask[*s & 0x7f];
                    idx = (int32_t)((mask & 0xff00u) >> 1) | px3(c);
                    v = (int32_t)(Phone_Attr(idx) & mask) & 0xff;
                    if ((v == 0) == want)
                        goto next;
                    s++;
                } else if (opc == 2) {
                    c = Stage3_Char(self, (int32_t)b);
                    found = 0;
                    for (;;) {
                        d = s[1];
                        s++;
                        if (d <= 0x18)
                            break;
                        if (px3(c) == (int32_t)d) {
                            if (want == 0)
                                goto next;
                            found = 1;
                            do {
                                s++;
                            } while (*s > 0x18);
                            break;
                        }
                    }
                    if ((int32_t)found != want)
                        goto next;
                }
                if (*s == 0x18)
                    break;
            }
        }

        if (r->edits != NULL)
            Stage3_Apply(self, r->edits, 1);

        s = r->ops;
        if (s == NULL || *s == 0)
            return;
        do {
            switch (*s++) {
            case 1:  Stage3_Op1(self);  break;
            case 2:  Stage3_Op2(self);  break;
            case 3:  Stage3_Op3(self);  break;
            case 4:  Stage3_Op4(self);  break;
            case 5:  Stage3_Op5(self);  break;
            case 6:  Stage3_Op6(self);  break;
            case 7:  Stage3_Op7(self);  break;
            case 8:  Stage3_Op8(self);  break;
            case 9:  Stage3_Op9(self);  break;
            case 10: Stage3_Op10(self); break;
            case 11: Stage3_Op11(self); break;
            case 12: Stage3_Op12(self); break;
            case 13: Stage3_Op13(self); break;
            case 14: Stage3_Op14(self); break;
            case 15: Stage3_Op15(self); break;
            case 16: Stage3_Op16(self); break;
            default: break;
            }
        } while (*s != 0);
        return;
next:
        ;
    }
}

/*
 * Make the parameter edits a rule asks for.
 *
 * The edits come as NULL-terminated blocks of twelve-byte records, each one
 * naming a value (a field of one s3_param, a track position, one of the
 * stage's own variables) and what to do to it.  Records also carry a mask so
 * the same block can serve several passes; "mode" selects which apply.  An
 * accumulator carries a value from one record to the next.
 */
/* @0x10051890 */
void TV_THISCALL Stage3_Apply(Engine *self, const S3Edit *const *edits,
                              int32_t mode)
{
    const S3Edit *const *blk = edits + 1;
    const S3Edit *e = edits[0];
    /* The original seeds both of these from the block cursor; a record that
     * used either before setting it would be writing into the rule table. */
    int32_t *p = (int32_t *)(void *)blk;
    int32_t acc = (int32_t)(intptr_t)blk;
    int32_t i;

    while (e != NULL) {
        for (;;) {
            if (((int32_t)e->when & mode) != 0) {
                switch (e->field) {
                case 0:  p = &self->s3_param[e->index].target;    break;
                case 1:  p = &self->s3_param[e->index].mode;      break;
                case 2:  p = &self->s3_param[e->index].shape_out; break;
                case 3:  p = &self->s3_param[e->index].shape_in;  break;
                case 4:  p = &self->s3_param[e->index].start;     break;
                case 5:  p = &self->s3_param[e->index].lead;      break;
                case 6:  p = &self->s3_param[e->index].len;       break;
                case 7:  p = &self->trk_wr[e->index];             break;
                case 8:  p = &self->trk_rd[e->index];             break;
                case 9:  p = &self->s3_next[e->index];            break;
                case 10: p = &self->s3_param_def[e->index];       break;
                case 11: p = &self->s3_param_rate[e->index];      break;
                case 12: p = &self->s3_1e5c[e->index];            break;
                case 13: p = &self->s3_1e24;                      break;
                case 14: p = &self->s3_1e28;                      break;
                case 15: p = &self->s3_1e2c;                      break;
                case 16: p = &self->s3_1e30;                      break;
                case 17: p = &self->s3_1e34;                      break;
                case 18: p = &self->s3_1e38;                      break;
                case 19: p = &self->s3_1e3c;                      break;
                case 20: p = &self->s3_1e4c;                      break;
                case 21: p = &self->s3_1e50;                      break;
                case 22: p = &self->s3_1e54;                      break;
                case 23: p = &self->s3_1e58;                      break;
                case 24: p = &self->s3_1e48;                      break;
                case 25: p = &self->s3_1e44;                      break;
                default: break;   /* keeps the one before */
                }
                switch (e->op & 0x7f) {
                case 0: *p = e->arg;                        break;
                case 1: *p += e->arg;                       break;
                case 2: *p -= e->arg;                       break;
                case 3: *p = Synth_MulQ15(*p, e->arg);      break;
                case 4: *p += Synth_MulQ15(*p, e->arg);     break;
                case 5: acc = e->arg;                       break;
                case 6: acc = *p;                           break;
                case 7: *p = acc;                           break;
                case 8:
                    for (i = 0; e->arg >= i; i++)
                        p[i] = acc;
                    break;
                default: break;
                }
            }
            if (!(e->op & 0x80))
                break;
            e++;
        }
        e = *blk++;
    }
}

/* @0x1004f710 */
void TV_THISCALL Stage3_Coarticulate(Engine *self);

/*
 * Run all five parameter passes, then tidy up after them.
 *
 * The formants have to stay at least 200 Hz apart or the cascade rings, no
 * transition may be longer than the curves allow, and the first formant's
 * amplitude gets a little back.
 */
/* @0x1004fe70 */
void TV_THISCALL Stage3_Op10(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t i, v;

    Stage3_Op11(self);
    Stage3_Op12(self);
    Stage3_Op13(self);
    Stage3_Op14(self);
    Stage3_Coarticulate(self);

    if (st->ctl->value != 'T' || st->scan->value != '3') {
        for (i = 9; i < 12; i++) {
            v = self->s3_param[i].start + 0xc8;
            if (self->s3_param[i + 1].start < v)
                self->s3_param[i + 1].start = v;
            v = self->s3_param[i].lead + 0xc8;
            if (self->s3_param[i + 1].lead < v)
                self->s3_param[i + 1].lead = v;
        }
    }

    for (i = 0; i < 22; i++) {
        if (self->s3_param[i].shape_out > 0x1e)
            self->s3_param[i].shape_out = 0x1e;
        if (self->s3_param[i].shape_in > 0x14)
            self->s3_param[i].shape_in = 0x14;
    }

    if (self->s3_param[1].target > 0)
        self->s3_param[1].target += 7;
}

/*
 * The second parameter pass: a handful of cluster rules.
 *
 * Mostly the second and third formants, which move a long way across a
 * cluster and need nudging when the tables would leave them where the
 * previous phoneme put them.
 */
/* @0x1004be40 */
void TV_THISCALL Stage3_Op12(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *cur = st->cur;
    int32_t c_cur = px3(cur->value);
    int32_t c_ctl = px3(st->ctl->value);
    uint8_t a;

    if ((Phone_Attr(c_cur | 0x100) & 8) || (Phone_Attr(c_cur | 0x200) & 2)) {
        int32_t set = 0;

        if (c_ctl == 'K') {
            a = st->scan->value;
            if (!(Phone_Attr(px3(a) | 0x100) & 2) && a != 'p')
                set = 1;
        } else if (c_ctl == '~') {
            set = 1;
        }
        if (set) {
            self->s3_param[10].target = 0x6a4;
            self->s3_param[11].target = 0x76c;
        }
        if ((Phone_Attr(c_ctl | 0x100) & 2) &&
            !(Phone_Attr(c_cur | 0x100) & 2)) {
            self->s3_param[11].shape_out = 5;
            if (c_ctl == 'E' && c_cur == 'R')
                self->s3_param[0].shape_out = 6;
        }
    }

    if (c_ctl == 'R' && (Phone_Attr(c_cur | 0x100) & 0x10)) {
        self->s3_param_rate[10] = 0x19a0;
    } else if ((Phone_Attr(c_cur | 0x180) & 0x40) &&
               (c_ctl == 'Z' || c_ctl == 'n' || c_ctl == 'q' ||
                c_ctl == 't')) {
        self->s3_param[10].target = 0x76c;
    }

    if (c_ctl == 'd' && st->scan->value == 'b')
        self->s3_param[9].target = 0xb6;

    if (st->ctl->value == 'B' &&
        (Phone_Attr(px3(cur->value) | 0x100) & 2) &&
        st->ctl->next->value == 'p')
        self->s3_param[0].mode = 4;

    if (c_ctl == 'y' && cur->value == 'L')
        self->s3_param[9].shape_in = 5;

    if (c_ctl == 'J')
        self->s3_param[1].target = 0x40;
}

/*
 * Bring the aspiration forward over the frames already written.
 *
 * A voiced phoneme after an aspirated stop starts while the aspiration is
 * still dying away, so the stop's own run is shortened by s3_1e34 frames and
 * the aspiration amplitude is faded back into them.
 */
/* @0x100055b0 */
void TV_THISCALL Stage3_Op15(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t back = self->s3_1e34;
    int32_t pos = self->trk_wr[2];
    uint8_t *buf;

    if (back > pos)
        return;
    if (st->voice == 8 && self->sample_rate == 0x1f40)
        return;

    pos -= back;
    buf = self->trk_buf[2];
    self->trk_wr[2] = pos;
    Track_Nudge(self, buf, 2, pos, 3, pos - self->trk_rd[2],
                self->s3_1e58 - (int32_t)buf[pos & 0xff]);
    self->s3_param[2].len += self->s3_1e34;
    self->s3_param[2].shape_out = 3;
    self->s3_param_def[2] = self->s3_1e58;

    if (st->ctl->value == ' ') {
        pos = self->trk_wr[0];
        Track_Nudge(self, self->trk_buf[0], 0, pos, 0xa - self->s3_1e34,
                    pos - self->trk_rd[0], -4);
    }
    if (Phone_Attr(px3(st->ctl->value) | 0x100) & 1)
        self->s3_param[2].mode = 5;
}

/* A voiced phoneme after an aspirated stop: start it early. */
/* @0x10048100 */
void TV_THISCALL Stage3_Op1(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t c_ctl = px3(st->ctl->value);
    int32_t c_cur = px3(st->cur->value);
    int32_t v;

    if ((Phone_Attr(c_cur) & 2) && !(Phone_Attr(c_ctl | 0x100) & 1)) {
        self->s3_1e34 = 1;
        if (Phone_Attr(c_ctl) & 0x40) {
            v = self->trk_wr[1];
            if (v >= 2) {
                self->s3_param[1].len += 2;
                self->trk_wr[1] = v - 2;
            }
        }
        Stage3_Op15(self);
    }

    if (st->ctl->value != 'S' || st->scan->value != ' ' ||
        !(Phone_Attr(px3(st->cur->value) | 0x100) & 2))
        self->s3_param[17].mode = 4;
    if (st->ctl->value != 'S')
        self->s3_param[0].mode = 5;
}

/* A second phoneme grouping, used to pick how fast the formants move. */
/* @0x100ef6d8 */ extern const uint8_t *const g_phone_group;
/* @0x100ef278 */ extern const uint8_t g_group_shape[];

/*
 * How long the formants have to travel into this phoneme.
 *
 * The curve every formant follows comes from the group the phoneme before it
 * belongs to; the first formant gets half of it, since it moves least.
 */
/* @0x1004a0e0 */
void TV_THISCALL Stage3_Op8(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t c_cur = px3(st->cur->value);
    int32_t c_ctl = px3(st->ctl->value);
    int32_t i, v;
    uint8_t a;

    if (!(Phone_Attr(c_cur | 0x100) & 1)) {
        for (i = 0; i < 10; i++)
            self->s3_param[i].mode = 5;
        if (st->ctl->value == 'T')
            self->s3_param[9].mode = 7;
        if (st->ctl->value == 'M' && st->cur->value == 'z')
            self->s3_param[9].mode = 7;
    }

    a = Phone_Attr(c_ctl);
    if (a & 4) {
        if (a & 0x20)
            self->s3_param[0].mode = 6;
        else if (a & 0x10)
            self->s3_param[0].mode = 4;
    }

    if (Phone_Attr(c_cur | 0x100) & 0x40) {
        self->s3_param[11].mode = 5;
        self->s3_param[10].mode = 5;
    }

    if (Phone_Attr(c_cur | 0x200) & 1)
        v = 5;
    else
        v = (int32_t)g_group_shape[g_phone_group[c_ctl]];
    for (i = 9; i < 17; i++)
        self->s3_param[i].shape_out = v;
    self->s3_param[9].shape_out = self->s3_param[9].shape_out / 2 + 1;
}

/*
 * The same, but taking the curve from the phoneme just finished and choosing
 * how each parameter travels from what kind of sound it was.
 */
/* @0x100481b0 */
void TV_THISCALL Stage3_Op2(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t c_cur = px3(st->cur->value);
    int32_t i, v;

    for (i = 9; i < 17; i++)
        self->s3_param[i].shape_out =
            (int32_t)g_group_shape[g_phone_group[px3(st->cur->value)]];
    self->s3_param[9].shape_out = self->s3_param[9].shape_out / 2 + 1;

    if (Phone_Attr(c_cur | 0x80) & 8) {
        self->s3_param[2].mode = 6;
        self->s3_param[1].mode = 6;
        self->s3_param[0].mode = 6;
        for (i = 3; i < 9; i++)
            self->s3_param[i].mode = 4;
    } else if (Phone_Attr(c_cur) & 0x20) {
        for (i = 1; i < 9; i++)
            self->s3_param[i].mode = 4;
        if (c_cur == 'T' && st->ctl->value == 'p') {
            self->s3_param[1].mode = 6;
            self->s3_param_rate[2] = 0;
            self->s3_param_rate[1] = 0;
            self->s3_param[2].mode = 6;
        }
    } else {
        for (i = 0; i < 9; i++)
            self->s3_param[i].mode = 6;
    }

    if ((Phone_Attr(c_cur) & 0x10) &&
        (Phone_Attr(px3(st->ctl->value)) & 4))
        self->s3_param[0].mode = 4;

    if (c_cur == 'J' || c_cur == 'B' || c_cur == 'D' || c_cur == 'G') {
        if (Phone_Attr(px3(st->cur->prev->value)) & 4) {
            self->s3_param[0].start = 0x34;
            self->s3_param[0].lead = 0x34;
        }
    }
}

/*
 * How far the formants have to travel out of this phoneme.
 *
 * A sound the tongue has to leave slowly -- a liquid, a nasal -- makes the
 * formants take longer, and the ones that hardly move at all get a limit on
 * how fast they may.
 */
/* @0x10049100 */
void TV_THISCALL Stage3_Op5(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t c_ctl = px3(st->ctl->value);
    int32_t c_cur = px3(st->cur->value);
    uint8_t a = Phone_Attr(c_ctl | 0x100);
    int32_t i, v;

    if ((a & 0x40) && (Phone_Attr(c_cur) & 2) &&
        !(Phone_Attr(c_cur) & 0x10)) {
        self->s3_param_rate[11] = 0x7350;
        self->s3_param_rate[10] = 0x7350;
        self->s3_param_rate[9] = 0x7350;
    }

    if (c_ctl != 'l' && st->scan->value != ' ') {
        v = 9;
        if (Phone_Attr(c_ctl | 0x180) & 1) {
            v = 7;
            if (Phone_Attr(c_cur | 0x180) & 1)
                v = 5;
        }
        for (i = 9; i < 17; i++)
            self->s3_param[i].shape_out = v;
    }

    if (a & 8) {
        for (i = 10; i < 13; i++)
            self->s3_param[i].shape_out = 6;
    }

    if (c_ctl == 'n') {
        if (st->scan->value == ' ' && st->scan->next->value == ' ') {
            for (i = 10; i < 13; i++)
                self->s3_param[i].shape_out = 4;
        }
        if (st->cur->value == 'D')
            self->s3_param[10].mode = 6;
    }

    if (c_ctl == 'H') {
        if (Phone_Attr(px3(st->scan->value) | 0x200) & 2)
            self->s3_param[2].target = 0x37;
        if (st->scan->value == 'g' && st->cur->value == 's' &&
            st->cur->prev->value == 'C')
            self->s3_param[2].target = 0x43;
    }
}

/*
 * The per-phoneme parameter tables, all indexed by the phoneme's class.
 * Eight of them give the formants and their bandwidths; the amplitudes come
 * through a second level of indirection because only the classes above 0x1e
 * -- the ones that make a noise of their own -- have them.
 */
/* @0x100eece8 */ extern const uint8_t g_pt_f1[64];
/* @0x100eed28 */ extern const uint8_t g_pt_f2[64];
/* @0x100eed68 */ extern const uint8_t g_pt_f3[64];
/* @0x100eeda8 */ extern const uint8_t g_pt_f4[64];
/* @0x100eede8 */ extern const uint8_t g_pt_b1[64];
/* @0x100eee28 */ extern const uint8_t g_pt_b2[64];
/* @0x100eee68 */ extern const uint8_t g_pt_b3[64];
/* @0x100eeea8 */ extern const uint8_t g_pt_av[64];
/* @0x100ef158 */ extern const uint8_t *const g_pt_a1;
/* @0x100ef180 */ extern const uint8_t *const g_pt_a2;
/* @0x100ef1a8 */ extern const uint8_t *const g_pt_a3;
/* @0x100ef1d0 */ extern const uint8_t *const g_pt_a4;
/* @0x100ef1f8 */ extern const uint8_t *const g_pt_a5;
/* @0x100ef220 */ extern const uint8_t *const g_pt_a6;
/* @0x100ef248 */ extern const uint8_t *const g_pt_a7;
/* @0x100ef270 */ extern const uint8_t *const g_pt_a8;
/* @0x100ef330 */ extern const uint8_t *const g_pt_a16;
/* The second set, for the classes below 0x13. */
/* @0x100eefd8 */ extern const uint8_t g_at_f1[];
/* @0x100eeff0 */ extern const uint8_t g_at_f2[];
/* @0x100ef008 */ extern const uint8_t g_at_f3[];
/* @0x100ef020 */ extern const uint8_t g_at_f4[];
/* @0x100ef038 */ extern const uint8_t g_at_b1[];
/* @0x100ef050 */ extern const uint8_t g_at_b2[];
/* @0x100ef068 */ extern const uint8_t g_at_b3[];
/* @0x100ef120 */ extern const uint8_t g_pt_open[];
/* The transition context each class leaves behind and expects. */
/* @0x100eeee8 */ extern const int32_t g_ctx_next[];
/* @0x100ef080 */ extern const int32_t g_ctx_prev[];
/* How fast a parameter may move, by the pair of contexts. */
/* @0x100ef290 */ extern const int32_t g_rate_by_ctx[];
/* The curve each parameter starts out travelling along. */
/* @0x100ef310 */ extern const uint8_t g_shape_init[22];
/* The burst a stop makes, by its place and whether a vowel follows. */
/* @0x100ef377 */ extern const uint8_t g_stop_burst[];
/* @0x1002d9cc */ extern const uint8_t g_stop_rel_kind[17];
/* Per-voice offsets. */
/* @0x100b53c8 */ extern const int32_t g_voice_f4[];
/* @0x100b5310 */ extern const uint8_t g_voice_p18[];
/* @0x100b5320 */ extern const uint8_t g_voice_p19[];
/* @0x100b5330 */ extern const uint8_t g_voice_p20[];
/* @0x100b5340 */ extern const uint8_t g_voice_p21[];

/*
 * Load this phoneme's parameter targets.
 *
 * Everything the synthesizer needs for one phoneme comes out of the tables
 * above, read with the phoneme's class: four formants and their bandwidths,
 * the amplitudes of the cascade and the parallel branch, the pitch.  What
 * follows is the part the tables cannot hold -- how a stop's burst and
 * release depend on what it sits between -- and then the defaults every
 * parameter starts from before the rules run.
 */
/* @0x1002d0d0 */
void TV_THISCALL Stage3_Targets(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *ctl = st->ctl;
    Node *cur = st->cur;
    Node *scan;
    int32_t c_ctl = px3(ctl->value);
    uint8_t c_cur = cur->value;
    int32_t cls_ctl = (int32_t)(int8_t)g_phone_class[c_ctl];
    int32_t cls_cur = (int32_t)g_phone_class[px3(c_cur)];
    int32_t voice = st->voice;
    int32_t i, v, kind, cls_scan, idx;
    uint8_t a, a_scan, c, d;

    if ((uint32_t)cls_cur < 0x13 && c_cur != 'u' && ctl->value != 'j')
        v = g_ctx_prev[cls_cur];
    else
        v = self->s3_1e30;
    self->s3_1e2c = v;
    self->s3_1e24 = self->s3_1e28;
    self->s3_1e30 = g_ctx_next[cls_ctl];
    self->s3_1e54 = 0x33;
    self->s3_1e5c[2] = 0;
    self->s3_1e5c[1] = 0;
    self->s3_1e58 = 0x33;
    self->s3_1e5c[0] = 0;

    a = Phone_Attr(c_ctl | 0x100);
    if (a & 2)
        self->s3_1e28 = 0;
    else if (a & 1)
        self->s3_1e28 = 3;
    else
        self->s3_1e28 = (Phone_Attr(c_ctl) & 2) ? 1 : 2;

    self->s3_param[9].target  = (int32_t)g_pt_f1[cls_ctl] << 2;
    self->s3_param[10].target = ((int32_t)g_pt_f2[cls_ctl] << 3) + 0x1f4;
    self->s3_param[11].target = (int32_t)g_pt_f3[cls_ctl] << 4;
    self->s3_param[12].target = ((int32_t)g_pt_f4[cls_ctl] << 4) +
                                g_voice_f4[voice];
    self->s3_param[13].target = (int32_t)g_pt_b1[cls_ctl] * 2;
    self->s3_param[14].target = (int32_t)g_pt_b2[cls_ctl] * 2;
    self->s3_param[15].target = (int32_t)g_pt_b3[cls_ctl] * 2;
    self->s3_param[0].target  = (int32_t)g_pt_av[cls_ctl];
    self->s3_param[17].target = (int32_t)ctl->b15 * 2;
    self->s3_param[18].target = (int32_t)g_voice_p18[voice];
    self->s3_param[19].target = (int32_t)g_voice_p19[voice];
    self->s3_param[20].target = (int32_t)g_voice_p20[voice];
    self->s3_param[21].target = (int32_t)g_voice_p21[voice];

    if (cls_ctl < 0x1e) {
        /* a class with no noise of its own */
        self->s3_param[1].target = 0;
        self->s3_param[2].target = 0;
        self->s3_param[3].target = 0x3c;
        self->s3_param[4].target = 0x3c;
        self->s3_param[5].target = 0x3c;
        self->s3_param[6].target = 0x3c;
        self->s3_param[7].target = 0x3c;
        self->s3_param[8].target = 0;
        if (cls_ctl < 0x13) {
            self->s3_1e44 = (int32_t)g_pt_open[cls_ctl];
            self->s3_alt[0] = (int32_t)g_at_f1[cls_ctl] << 2;
            self->s3_alt[1] = ((int32_t)g_at_f2[cls_ctl] << 3) + 0x1f4;
            self->s3_alt[2] = (int32_t)g_at_f3[cls_ctl] << 4;
            self->s3_alt[3] = (int32_t)g_at_f4[cls_ctl] << 4;
            self->s3_alt[4] = (int32_t)g_at_b1[cls_ctl] * 2;
            self->s3_alt[5] = (int32_t)g_at_b2[cls_ctl] * 2;
            self->s3_alt[6] = (int32_t)g_at_b3[cls_ctl] * 2;
        }
        if (c_ctl == 'u' && st->scan->value == 'j')
            self->s3_1e30 = 0;
        goto defaults;
    }

    self->s3_param[1].target = (int32_t)g_pt_a1[cls_ctl];
    self->s3_param[2].target = (int32_t)g_pt_a2[cls_ctl];
    self->s3_param[3].target = (int32_t)g_pt_a3[cls_ctl];
    self->s3_param[4].target = (int32_t)g_pt_a4[cls_ctl];
    self->s3_param[5].target = (int32_t)g_pt_a5[cls_ctl];
    self->s3_param[6].target = (int32_t)g_pt_a6[cls_ctl];
    self->s3_param[7].target = (int32_t)g_pt_a7[cls_ctl];
    self->s3_param[8].target = (int32_t)g_pt_a8[cls_ctl];

    if (cls_ctl < 0x22 || cls_ctl >= 0x2b)
        goto defaults;

    scan = st->scan;
    if (cls_ctl >= 0x26 && cls_ctl < 0x29) {
        /* an affricate: the burst depends on what follows */
        kind = (int32_t)g_class_kind[g_phone_class[px3(scan->value)]];
        if (kind == 8 || kind == 9)
            goto defaults;
        self->s3_1e5c[1] = 0x7f;
        if (kind == 6 && ctl->value != 't')
            self->s3_1e5c[1] = (ctl->value == 'J') ? 0x36 : 0x3c;
        self->s3_1e5c[2] = (ctl->value == 'J') ? 0 : 0x33;
        if (kind == 6 && ctl->value == 'C')
            self->s3_1e5c[2] = 0;
        self->s3_1e5c[1] = 0x7f;
        if (kind == 6 && ctl->value != 't')
            self->s3_1e5c[1] = (ctl->value == 'J') ? 0 : 0x46;
        self->s3_1e5c[0] = 0x7f;
        if (kind == 6 && ctl->value == 'J')
            self->s3_1e5c[0] = 0;
        self->s3_1e38 = 0x7f;
        self->s3_1e3c = (cls_ctl == 0x26) ? 0x7f : 1;
        goto defaults;
    }

    /* a stop: its burst comes from a small table of its own */
    idx = cls_ctl - 0x22;
    if (cls_ctl >= 0x29)
        idx -= 3;
    idx <<= 3;
    if (!(Phone_Attr(px3(scan->value) | 0x100) & 2))
        idx += 4;
    for (i = 1; i <= 3; i++)
        self->s3_1e5c[i - 1] = (int32_t)g_stop_burst[idx + i];
    self->s3_param[8].target = (int32_t)g_stop_burst[idx + 4];

    d = scan->value;
    kind = (int32_t)g_class_kind[g_phone_class[px3(d)]];
    if (kind == 8 || kind == 9) {
        c = ctl->value;
        if (c == 'P' || c == 'K' || c == 'B' || c == 'G' || c == 'T' ||
            c == 'D') {
            int32_t mute = 1;

            if (c == 'K' &&
                ((Phone_Attr(px3(d)) & 0x10) || d == 'Q' || d == 't'))
                mute = 0;
            if (mute) {
                self->s3_1e5c[0] = 0x7f;
                self->s3_1e5c[1] = 0x7f;
                self->s3_1e5c[2] = 0x7f;
                self->s3_param[8].target = 0;
            }
        }
    }

    if (Phone_Attr(px3(ctl->value)) & 4)
        self->s3_1e38 = 0x7f;
    else
        self->s3_1e38 = (int32_t)(ctl->d10 & 0xffu);

    self->s3_1e3c = 0;
    a_scan = Phone_Attr(px3(d) | 0x100);
    if ((a_scan & 1) && !(Phone_Attr(px3(d)) & 0x10) &&
        (!(Phone_Attr(px3(cur->value) | 0x100) & 1) ||
         (Phone_Attr(px3(cur->value)) & 0x10))) {
        /* the release of a stop into another stop */
        i = px3(ctl->value) - 0x44;
        if ((uint32_t)i > 0x10) {
            self->s3_1e3c = 0x7f;
            goto defaults;
        }
        switch (g_stop_rel_kind[i]) {
        case 0:
            if (cur->value == 'j' && (d == 'B' || d == 'G'))
                self->s3_1e3c = 1;
            break;
        case 1:
            if (d == 'P') {
                self->s3_1e3c = 1;
                self->s3_1e5c[1] = 0x3c;
                self->s3_1e5c[2] = 0x30;
            } else {
                self->s3_1e3c = 0x7f;
            }
            break;
        case 2:
            if (!(Phone_Attr(px3(d)) & 4)) {
                self->s3_1e3c = 1;
                self->s3_1e5c[1] = 0x3c;
                self->s3_1e5c[2] = 0x30;
            } else {
                self->s3_1e3c = 0x7f;
            }
            break;
        default:
            self->s3_1e3c = 0x7f;
            break;
        }
        goto defaults;
    }

    c = ctl->value;
    if (c == 'K' && (Phone_Attr(px3(d)) & 0x40)) {
        self->s3_1e3c = 1;
    } else if (c == 'G') {
        if (!(scan->flags & 0x20u) && (a_scan & 2))
            self->s3_1e3c = 1;
        else
            self->s3_1e3c = 2;
    } else if (c == 'T' && (Phone_Attr(px3(d)) & 0x40)) {
        self->s3_1e3c = 0x7f;
    } else if (c == 'D') {
        if ((Phone_Attr(px3(d)) & 2) && !(a_scan & 2))
            self->s3_1e3c = 0x7f;
        else if (cur->value == ' ')
            self->s3_1e3c = 2;
        else
            self->s3_1e3c = 1;
    } else if (!(Phone_Attr(px3(c) | 0x100) & 0x10) && d != 'p') {
        self->s3_1e3c = 2;
    } else {
        self->s3_1e3c = 1;
    }

defaults:
    if (cls_ctl >= 0x35)
        self->s3_param[16].target = (int32_t)g_pt_a16[cls_ctl] * 4 + 0xc0;
    else
        self->s3_param[16].target = 0xf8;

    v = g_rate_by_ctx[self->s3_1e24 * 4 + self->s3_1e28];
    for (i = 0; i < 22; i++) {
        self->s3_param[i].shape_out = (int32_t)g_shape_init[i];
        self->s3_param[i].mode = 7;
        self->s3_param[i].len = (int32_t)st->ctl->arg;
        self->s3_param_rate[i] = v;
    }
    self->s3_param[20].mode = 4;
    self->s3_param[21].mode = 4;

    /* and a look-ahead at where the next phoneme wants the formants */
    cls_scan = (int32_t)(int8_t)g_phone_class[px3(st->scan->value)];
    self->s3_next[9]  = (int32_t)g_pt_f1[cls_scan] << 2;
    self->s3_next[10] = ((int32_t)g_pt_f2[cls_scan] << 3) + 0x1f4;
    self->s3_next[11] = (int32_t)g_pt_f3[cls_scan] << 4;
    if (cls_scan >= 0x22) {
        self->s3_next[13] = (int32_t)g_pt_b1[cls_scan] * 2;
        self->s3_next[14] = (int32_t)g_pt_b2[cls_scan] * 2;
        self->s3_next[15] = (int32_t)g_pt_b3[cls_scan] * 2;
    }
}

/*
 * The voiced stops and the fricatives before them.
 *
 * A voiced stop keeps a little voicing going through its closure, which
 * these rules size; the "s" of an affricate is shortened before a nasal;
 * and the parallel branch is muted where the closure is complete.
 */
/* @0x10049260 */
void TV_THISCALL Stage3_Op6(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *scan = st->scan;
    Node *cur = st->cur;
    int32_t c_ctl = px3(st->ctl->value);
    int32_t c_scan = px3(scan->value);
    uint8_t a, b;
    Node *n;
    int32_t v;

    if (Phone_Attr(c_ctl | 0x180) & 4) {
        int32_t shortened = 0;

        if (c_ctl == 's') {
            n = st->ctl->next;
            b = n->value;
            if ((b == '|' || b == '@') && n->next->value == 'N') {
                self->s3_param[1].shape_out -= 2;
                shortened = 1;
            } else if (!(Phone_Attr(c_scan | 0x100) & 0x20) &&
                       !(Phone_Attr(c_scan | 0x180) & 0x20)) {
                self->s3_param[1].shape_out -= 2;
                shortened = 1;
            }
        }
        if (!shortened)
            self->s3_param[1].shape_out = 6;

        if ((Phone_Attr(c_ctl | 0x80) & 8) &&
            !(Phone_Attr(px3(cur->value)) & 4))
            self->s3_param[0].target = 0;
    }

    if (Phone_Attr(c_scan) & 2) {
        self->s3_1e40 = 2;
        if (scan->arg <= 2u)
            self->s3_1e40 = (int32_t)scan->arg - 1;
        if (Phone_Attr(c_ctl) & 0x20)
            self->s3_1e40 = 0;
        if ((c_ctl == 'X' && c_scan == 'R') || c_ctl == 'S')
            self->s3_1e40 = 0;
        if (c_ctl != 's')
            self->s3_param[1].len += self->s3_1e40;
    }

    a = Phone_Attr(c_ctl);
    if (!(a & 4) || !(a & 0x20))
        goto tail;

    /* a voiced stop */
    if (!((c_ctl == 'D' &&
           ((Phone_Attr(c_scan | 0x100) & 2) || c_scan == 'n')) ||
          (c_ctl == 'J' && c_scan == 'z') ||
          (scan->flags & 0x20u))) {
        self->s3_param[5].target = 0;
        self->s3_param[4].target = 0;
    }

    if (!((Phone_Attr(c_scan | 0x100) & 2) ||
          (c_ctl == 'G' && c_scan == 'Y') || c_scan == 'Z' ||
          (c_ctl == 'D' && c_scan == 'n')))
        self->s3_param[0].target -= 0x14;

    b = Phone_Attr(px3(cur->value));
    if (((b & 4) || (c_ctl == 'D' && c_scan == '|')) && c_ctl != 'q') {
        if (!(b & 2))
            self->s3_param[0].target -= 0x1e;
    } else {
        self->s3_param[0].target = 0;
    }

    if (c_ctl != 'q' && (Phone_Attr(px3(cur->value)) & 4)) {
        self->s3_param[0].target = 0x32;
        self->s3_param[0].shape_out = (int32_t)st->ctl->arg;
        goto tail;
    }

    if ((c_ctl == 'B' || c_ctl == 'G') && cur->value == 'S') {
        self->s3_param[0].target = 0;
        goto tail;
    }
    if (!(Phone_Attr(c_scan | 0x100) & 2))
        goto tail;
    if (c_ctl == 'B') {
        self->s3_param[0].target = 0x32;
        goto tail;
    }
    if (c_ctl != 'D')
        goto tail;
    if (c_scan == '|') {
        self->s3_param[0].target = 0;
        goto tail;
    }
    b = cur->value;
    if (b != ' ' && b != 'S') {
        if (scan->flags & 0x20u)
            self->s3_param[0].target = 0x2b;
        goto tail;
    }
    if ((Phone_Attr(px3(b)) & 1) && scan->value != 'p') {
        self->s3_param[5].target = 0;
        self->s3_param[4].target = 0;
    }

tail:
    if (c_scan == 's' && scan->next->value == 'R' &&
        (st->ctl->flags & 0x20u)) {
        self->s3_1e5c[2] = 0;
        self->s3_param[7].target = 0x41;
        self->s3_param[4].target = 0x3c;
        self->s3_1e5c[1] = 0x3c;
    }
    if (c_ctl == 'F' && cur->value == ' ')
        self->s3_param[8].target = 0x3c;
    if (c_ctl == 'V')
        self->s3_param[8].target = 0x38;
}

/* @0x10050740 */
void TV_THISCALL Stage3_Voiced(Engine *self);

/*
 * A liquid or a glide next to a consonant.
 *
 * These are the sounds whose formants move furthest, so most of this decides
 * how long they have to get there, and when the amplitudes should go back to
 * the phoneme's own defaults rather than carrying over.
 */
/* @0x10048310 */
void TV_THISCALL Stage3_Op3(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *ctl = st->ctl;
    Node *cur = st->cur;
    Node *n;
    int32_t c_ctl = px3(ctl->value);
    int32_t c_cur = px3(cur->value);
    uint8_t a_cur = Phone_Attr(c_cur);
    uint8_t a_ctl = Phone_Attr(c_ctl | 0x100);
    uint8_t a_cur3, b;
    int32_t i, v, kind, restore = 0;

    if (((c_ctl == 'R' || c_ctl == 'L' || c_ctl == 'W') &&
         (a_cur & 0x40) && (a_cur & 4)) ||
        !((a_cur & 4) || c_cur == ' ' || c_cur == 'H' ||
          (cur->flags & 0x40u) ||
          ((ctl->flags & 0x20u) && (a_ctl & 2) && c_cur == 'D')))
        Stage3_Voiced(self);

    if (!(a_ctl & 2))
        self->s3_1e38 = 0x7f;

    if ((a_cur & 0x60) && c_cur != 's')
        self->s3_param[1].len -= self->s3_1e40;

    if (!Phone_IsVowel((uint8_t)c_ctl)) {
        a_cur3 = Phone_Attr(c_cur | 0x180);
        if (a_cur3 & 1) {
            v = 7;
            if ((Phone_Attr(c_ctl | 0x180) & 0x20) && c_cur == 'Y')
                v = 0xb;
            for (i = 9; i < 17; i++)
                self->s3_param[i].shape_out = v;
            if (Phone_Attr(c_cur | 0x100) & 8)
                self->s3_param[11].shape_out = 9;
        }
        if (!(a_ctl & 0x10) &&
            ((Phone_Attr(c_cur | 0x200) & 4) || (a_cur3 & 0x40))) {
            self->s3_param_rate[9] = 0x2cd8;
            self->s3_param_rate[10] = 0x2cd8;
            self->s3_param_rate[11] = 0x2cd8;
        }
        if ((Phone_Attr(c_ctl | 0x180) & 0x20) && (a_cur3 & 4))
            self->s3_param[10].shape_out += 5;
    }

    if (c_cur == 'T' && ((a_ctl & 2) || c_ctl == 'n')) {
        restore = 1;
    } else if (c_cur == 'K') {
        kind = (int32_t)g_class_kind[g_phone_class[c_ctl]];
        if (kind == 3 || kind == 2 || c_ctl == 'a' || c_ctl == 'l' ||
            c_ctl == 'W' || c_ctl == 'L' || c_ctl == 'Y' || c_ctl == 'p')
            restore = 1;
    }
    if (!restore && c_cur == 'P') {
        if (c_ctl == 'i') {
            restore = 1;
        } else if (c_ctl == 'R') {
            n = st->scan;
            if ((n->flags & 0x20u) &&
                (Phone_Attr(px3(n->value) | 0x100) & 2))
                restore = 1;
        }
    }
    if (!restore && c_cur == 'X' && (c_ctl == 'R' || (a_ctl & 2)))
        restore = 1;
    if (restore) {
        for (i = 3; i < 9; i++)
            self->s3_param[i].target = self->s3_param_def[i];
    }

    if ((c_cur == 'P' || c_cur == 'T' || c_cur == 'K') && c_ctl == 'p')
        self->s3_param[0].target = 0;

    if ((a_ctl & 2) && !(ctl->flags & 0x20u)) {
        b = ctl->next->value;
        if (b != '&' && b != '%') {
            n = st->scan;
            if ((Phone_Attr(px3(n->value) | 0x100) & 2) &&
                (n->flags & 0x20u))
                self->s3_param[0].target -= 2;
        }
    }
    if ((a_ctl & 2) && (ctl->flags & 0x20u)) {
        b = ctl->prev->value;
        if (b != '&' && b != '%') {
            n = st->cur;
            if ((Phone_Attr(px3(n->value) | 0x100) & 2) &&
                !(n->flags & 0x20u))
                self->s3_param[0].target += 2;
        }
    }

    if ((c_ctl == 'R' || c_ctl == 'L') && ctl->prev->value == 'P')
        self->s3_param[8].target = 0;
}

/* Packed bit tables, one per kind of table the caller asks for. */
/* @0x10039cec */ extern const uint8_t g_bits_sel[24];
/* @0x100cef70 */ extern const uint8_t g_bits0[];
/* @0x1012c0be */ extern const uint8_t g_bits1[];
/* @0x1010ad82 */ extern const uint8_t g_bits2[];
/* @0x100ae526 */ extern const uint8_t g_bits3[];
/* @0x10101b1b */ extern const uint8_t g_bits4[];
/* @0x100c7d4f */ extern const uint8_t g_bits5[];
/* @0x100f72cb */ extern const uint8_t g_bits6[];

/* Count the bits set across the first n bytes of one of those tables. */
/* @0x10039bb0 */
int32_t TV_STDCALL Bits_Count(int32_t which, int32_t n)
{
    const uint8_t *tab;
    int32_t total = 0, i, v, b;
    /* The original leaves this from the previous row when "which" is out of
     * range, which the tables never are. */
    int32_t byte = 0;

    if (n > 0x91)
        n = 0x91;
    for (i = 0; i < n; i++) {
        v = 0x100;
        if ((uint32_t)which <= 0x17) {
            switch (g_bits_sel[which]) {
            case 0: tab = g_bits0; break;
            case 1: tab = g_bits1; break;
            case 2: tab = g_bits2; break;
            case 3: tab = g_bits3; break;
            case 4: tab = g_bits4; break;
            case 5: tab = g_bits5; break;
            default: tab = g_bits6; break;
            }
            byte = (int32_t)tab[which * 145 + i];
        }
        b = byte;
        while (b != 0) {
            v /= 2;
            if ((uint32_t)b >= (uint32_t)v) {
                b -= v;
                total++;
            }
        }
    }
    return total;
}

/*
 * Find which entry of each of four tables allows this pair of phonemes.
 *
 * Each table is a list of entries, and each entry names a phoneme and the
 * set of phonemes it may be followed by.  What comes back is the index of
 * the first entry that allows the pair, or the table's length when none do.
 */
/* @0x10050670 */
void TV_STDCALL Stage3_FindPair(int32_t *out, uint8_t c1, uint8_t c2,
                                const int32_t *counts,
                                const S3Pair *const *tab)
{
    const S3Pair *p;
    const uint8_t *s;
    int32_t slot, base = 0, i;
    uint8_t found;

    for (slot = 0; slot < 4; slot++) {
        found = 0;
        if (slot != 0)
            base += counts[slot - 1];
        for (i = 0; i < counts[slot] && !found; i++) {
            p = tab[base + i];
            for (; p->ch != 0 && !found; p++) {
                if (p->ch != c1)
                    continue;
                for (s = p->set; *s != 0 && !found; s++) {
                    if (*s == c2) {
                        found = 1;
                        out[slot] = i;
                    }
                }
            }
        }
        if (!found)
            out[slot] = counts[slot];
    }
}

/* A bit per position within a byte of the packed tables. */
/* @0x100bf7f0 */ extern const uint8_t g_bit_mask[8];

/*
 * Where in the packed table this bit sits.
 *
 * The tables store one bit per slot; this says how many set bits come before
 * the one asked for, which is its index among the entries that exist, or -1
 * when the slot is empty.
 */
/* @0x10038700 */
int32_t TV_STDCALL Bits_Rank(int32_t bit, int32_t row, int32_t which)
{
    const uint8_t *tab;
    int32_t idx = bit + 34 * row;
    int32_t byte_i = idx / 8;
    int32_t bit_i = idx - byte_i * 8;
    int32_t v = 0, n = 0, i;

    if ((uint32_t)which <= 0x17) {
        switch (g_bits_sel[which]) {
        case 0: tab = g_bits0; break;
        case 1: tab = g_bits1; break;
        case 2: tab = g_bits2; break;
        case 3: tab = g_bits3; break;
        case 4: tab = g_bits4; break;
        case 5: tab = g_bits5; break;
        default: tab = g_bits6; break;
        }
        v = (int32_t)tab[which * 145 + byte_i];
    }
    if (!(v & (int32_t)g_bit_mask[bit_i]))
        return -1;
    if (byte_i > 0)
        n = Bits_Count(which, byte_i);
    for (i = 0; i <= bit_i; i++)
        if (v & (int32_t)g_bit_mask[i])
            n++;
    return n - 1;
}

/* The rows of each table, indexed by which table and which row. */
/* @0x1003b20c */ extern const uint8_t g_row_sel[22];
/* @0x1012c4a0 */ extern const uint8_t *const g_row0[];
/* @0x1010b3a0 */ extern const uint8_t *const g_row1[];
/* @0x100aec90 */ extern const uint8_t *const g_row2[];
/* @0x10102514 */ extern const uint8_t *const g_row3[];
/* @0x100c898c */ extern const uint8_t *const g_row4[];
/* @0x100f8084 */ extern const uint8_t *const g_row5[];

/*
 * Look for a value in one row of a table.
 *
 * The row's first byte says which of two values to look for, and carries
 * half of the answer in its upper bits; finding the value plus 0x80 rather
 * than the value itself is what makes the search succeed.
 */
/* @0x1003b0d0 */
int32_t TV_THISCALL Stage3_FindRow(Engine *self, int32_t slot, int32_t which,
                                   int32_t lo, int32_t hi, int32_t len)
{
    const uint8_t *tab;
    int32_t off, want, i, b, half;
    int32_t result = -1;
    uint8_t stop = 0, hit = 0;

    i = which - 2;
    if ((uint32_t)i <= 0x15) {
        switch (g_row_sel[i]) {
        case 0: tab = g_row0[which]; break;
        case 1: tab = g_row1[which]; break;
        case 2: tab = g_row2[which]; break;
        case 3: tab = g_row3[which]; break;
        case 4: tab = g_row4[which]; break;
        default: tab = g_row5[which]; break;
        }
    } else {
        /* the original falls back on its own "this" here, which cannot be a
         * table; the callers never ask for a row it does not have */
        tab = (const uint8_t *)self;
    }

    if (slot == 0)
        return result;
    off = ((slot & 0xff) - 1) * (len + 1);
    b = (int32_t)tab[off];
    half = b >> 1;
    want = (b & 1) ? (int32_t)(int8_t)hi : (int32_t)(int8_t)lo;

    for (i = 1; len + 1 > i; i++) {
        if (stop || hit)
            continue;
        b = (int32_t)tab[off + i];
        if (b == want) {
            stop = 1;
        } else if (b == want + 0x80) {
            hit = 1;
            result = Bits_Count(which, 0x91) + half;
        }
    }
    return result;
}

/* Which of seven formant sets a phoneme's nasal pole uses. */
/* @0x100490b0 */ extern const uint8_t g_pole_sel[68];

/*
 * The formants of a nasal murmur.
 *
 * A nasal is voiced through a closed mouth, so its formants come from the
 * nose rather than from where the tongue is; there are seven sets of them,
 * chosen by which nasal it is and what it is next to.
 */
/* @0x10048ef0 */
void TV_THISCALL Stage3_NasalPole(Engine *self, int32_t *out)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t i = px3(st->ctl->value) - 0x34;

    switch (((uint32_t)i <= 0x43) ? g_pole_sel[i] : 6) {
    case 0:
        out[1] = 0x731; out[2] = 0x99e; out[3] = 0xd42;
        out[4] = 0x44;  out[5] = 0x198; out[6] = 0xbc;  out[0] = 0x108;
        break;
    case 1:
        out[1] = 0x646; out[2] = 0x9e6; out[3] = 0xd8c;
        out[4] = 0x4b;  out[5] = 0x198; out[6] = 0x119; out[0] = 0x128;
        break;
    case 2:
        out[1] = 0x66e; out[2] = 0x9a7; out[3] = 0xd6c;
        out[4] = 0x80;  out[5] = 0x4a;  out[6] = 0x37;  out[0] = 0x144;
        break;
    case 3:
        out[1] = 0x423; out[2] = 0xa95; out[3] = 0xcd9;
        out[4] = 0x8d;  out[5] = 0xc4;  out[6] = 0x5e;  out[0] = 0x162;
        break;
    case 4:
        out[1] = 0x5d1; out[2] = 0x96c; out[3] = 0xbe2;
        out[4] = 0x95;  out[5] = 0x4f;  out[6] = 0x3c;  out[0] = 0x17f;
        break;
    case 5:
        out[1] = 0x5b5; out[2] = 0x9fa; out[3] = 0xdc1;
        out[4] = 0xb2;  out[5] = 0x9c;  out[6] = 0x50;  out[0] = 0x183;
        break;
    default:
        out[1] = 0x5b3; out[2] = 0x9d5; out[3] = 0xd19;
        out[4] = 0x7d;  out[5] = 0xb9;  out[6] = 0x87;  out[0] = 0x154;
        break;
    }
}
