/*
 * Input stage: turns the preformatted character stream in mid_ring into work
 * list nodes, which is where the pipeline proper begins.
 *
 * It stops after ten characters, or at the first non-space after a space, so
 * the stages below it get a turn between words rather than after the whole
 * item.  e_9180 holds the one character of lookahead that survives across
 * calls; -1 means empty.
 *
 * The preformatter re-emits "ESC [ ... <letter>" commands in a binary form --
 * ESC, the letter, a length, then that many bytes -- and read_control turns
 * one of those back into a control node.  The length selects the shape of
 * what follows, which is the jump table at 0x1000e4d4.
 */
#include "es_engine.h"

/* Inlined into Engine_InputStage by the original's compiler; kept separate
 * here because it is a whole idea of its own.  gcc inlines it back. */
static void read_control(Engine *self)
{
    Node *n = Engine_AppendNode(self, NODE_CONTROL, 0x1b);
    int32_t v;

    n->value = (uint8_t)Engine_MidGet(self);
    switch (Engine_MidGet(self)) {
    case 1:
        n->arg = (uint32_t)Engine_MidGet(self);
        n->b15 = 0;
        if (n->value == 'i')
            n->notify = self->item_notify;
        break;
    case 2:
        n->arg = (uint32_t)Engine_MidGet(self);
        n->b15 = (uint8_t)Engine_MidGet(self);
        if (n->value == 'A')
            self->in_flags_A = ((int32_t)n->b15 << 8) | (uint8_t)n->arg;
        if (n->value == 'N')
            self->in_flags_N = ((int32_t)n->b15 << 8) | (uint8_t)n->arg;
        break;
    case 3:
        /* first byte carries flag bits for the node, then a 2-byte argument */
        v = Engine_MidGet(self);
        n->flags = (n->flags & ~0x18u) | (((uint32_t)v << 3) & 0x18u);
        if (v & 4)
            n->flags |= 0x20;
        if (v & 8)
            n->flags |= 0x40;
        n->arg = (uint32_t)Engine_MidGet(self);
        n->b15 = (uint8_t)Engine_MidGet(self);
        break;
    case 4:
        n->arg = (uint32_t)Engine_MidGet(self);
        n->arg |= (uint32_t)Engine_MidGet(self) << 8;
        n->arg |= (uint32_t)Engine_MidGet(self) << 16;
        n->arg |= (uint32_t)Engine_MidGet(self) << 24;
        if (n->value == 'i')
            n->notify = self->item_notify;
        break;
    default:
        n->arg = 0;
        n->b15 = 0;
        break;
    }
}

/* @0x1000e290 */
uint8_t TV_THISCALL Engine_InputStage(Engine *self)
{
    int32_t count = 0;
    uint8_t saw_space = 0;
    uint8_t appended = 0;
    int32_t c;
    Node *n;

    if (self->e_9180 == -1) {
        self->e_9180 = Engine_MidGet(self);
        if (self->e_9180 == -1)
            return 0;
    }
    for (;;) {
        c = self->e_9180;
        if (c == ' ')
            saw_space = 1;
        else if (saw_space)
            break;
        if (count++ >= 10)
            break;

        if (c == 0x1b) {
            read_control(self);
        } else if (c == '[' && (self->in_flags_A & 0x20) && !(self->in_flags_N & 3)) {
            n = Engine_AppendNode(self, NODE_CONTROL, 'I');
            n->arg = (self->in_flags_A & 0x40) ? 2 : 1;
        } else if (c == ']' && (self->in_flags_A & 0x20) && !(self->in_flags_N & 3)) {
            n = Engine_AppendNode(self, NODE_CONTROL, 'I');
            n->arg = 0;
        } else {
            Engine_AppendNode(self, 1, c);
        }

        self->e_9180 = -1;
        appended = 1;
        self->e_9180 = Engine_MidGet(self);
        if (self->e_9180 == -1)
            break;
    }
    return appended;
}
