/*
 * Stage 3 helpers.
 *
 * Stage 3 turns phonemes and their durations into the 22 parameter tracks
 * the synthesiser reads, so it is where a phoneme stops being a symbol and
 * becomes a set of formant targets moving over time.
 *
 * Stage3_Insert puts a pause into the stream: two silence nodes, the first
 * carrying the length and the second a fixed tail, with stress bits set so
 * that nothing downstream mistakes them for speech.
 */
#include "es_engine.h"

/* @0x1001b810 */
void TV_THISCALL Stage3_Reset(Engine *self)
{
    int i;

    /* English clears two more fields first, s3_2034 and s3_2035, which this
     * engine does not have. */
    self->s3_1fe8 = 4;
    self->s3_1fb0 = 0;
    self->s3_1fbc = 0;
    self->s3_1fdd = 0;
    self->s3_1fae = 1;
    self->s3_1fb4 = 1;
    self->s3_1fe4 = 3;
    self->s3_1fe0 = 2;
    self->stage_ctx[3].type_mask = 0x28; /* types 3 and 5 */
    self->s3_1fb8 = 0;
    for (i = 0; i < 22; i++)
        self->s3_param_raw[i] = self->cfg_bytes[i];
    Stage3_ResetParams(self);
}

/* @0x1001be30 */
Node *TV_THISCALL Stage3_Insert(Engine *self, Node *ref, int32_t mode)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *a, *b;
    uint32_t f;

    a = Engine_NodeAlloc(self, ref, 1, 4, ' ');
    /* English uses 4 here; this engine uses 15 */
    if (mode == 1)
        a->arg = 0xf;
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
    self->trk_08 = -1;
    self->synth_busy = 0;
    return a;
}
