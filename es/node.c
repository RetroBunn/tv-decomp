/*
 * Taking nodes off the free list and putting them on the work list.
 *
 * Every stage above this works on the same doubly linked list of 618 nodes,
 * so these two are underneath the whole pipeline.  Engine_ResetNodes threads
 * the pool onto the free list between two sentinels and Engine_NodeAlloc
 * takes them off the front one at a time; nothing ever allocates.
 */
#include "es_engine.h"

/* @0x10008bd0 */
Node *TV_THISCALL Engine_NodeAlloc(Engine *self, Node *ref, int32_t after, int32_t type,
                                   uint8_t value)
{
    Node *n;
    StageCtx *st;

    /* Two checks the 1997 engine does not have.  Engine_Error is a stub --
     * three bytes, "ret 4" -- so they report nothing and change nothing, but
     * they are in the instruction stream and so they are here. */
    if (self->free_nodes <= 0)
        Engine_Error(self, 0x1e);
    if (ref == NULL)
        Engine_Error(self, 0x1f);

    n = Engine_Unlink(self, self->free_head->next);
    self->free_nodes--;
    Engine_InsertBefore(self, n, after == 1 ? ref->next : ref);

    n->flags = (n->flags & ~NODE_TYPE_MASK) | ((uint32_t)type & NODE_TYPE_MASK);
    n->value = value;
    n->arg = 0;
    n->b15 = 0;
    n->flags &= ~0xf8u;

    st = self->stage;
    if (st != NULL) {
        if (st->last == ref && after == 1) {
            st->last = n;
            return n;
        }
        if (st->first == ref && after == 0)
            st->first = n;
    }
    return n;
}

/* Append at the end of the work list, and adopt the new node as stage 0's
 * window wherever that window is still empty. */
/* @0x10008b60 */
Node *TV_THISCALL Engine_AppendNode(Engine *self, int32_t type, int32_t value)
{
    StageCtx *s0 = &self->stage_ctx[0];
    Node *n = Engine_NodeAlloc(self, self->work_tail, 0, type, (uint8_t)value);
    s0->last = n;
    if (s0->scan == NULL)
        s0->scan = n;
    if (s0->ctl == NULL)
        s0->ctl = n;
    if (s0->cur == NULL)
        s0->cur = n;
    if (s0->first == NULL)
        s0->first = n;
    return n;
}
