/*
 * Doubly linked list surgery, used by the node allocator and by every stage
 * that moves a node around.
 *
 * Both are member functions that never touch the engine: the original takes
 * `this` in ecx as thiscall requires and then ignores it, reading its real
 * arguments off the stack.  The self parameter is kept here so the calling
 * convention matches what the callers in the DLL expect.
 *
 * Neither checks anything.  The lists always have sentinels at both ends, so
 * next and prev are never null for a node that is actually on one -- which is
 * also why passing a sentinel to Engine_NodeAlloc with after == 1 walks off
 * the end, as the unit tests found out the hard way.
 */
#include "es_engine.h"

/* Take n out of whatever list it is on.  n's own next and prev are left
 * pointing at its old neighbours -- the allocator overwrites both before
 * anyone reads them, so clearing them here would sound identical.  The
 * unit case still rejects it, which is the point of comparing the whole
 * object: the job is to be the same function, not an equivalent one. */
/* @0x10009110 */
Node *TV_THISCALL Engine_Unlink(Engine *self, Node *n)
{
    (void)self;
    n->prev->next = n->next;
    n->next->prev = n->prev;
    return n;
}

/* @0x10008ed0 */
Node *TV_THISCALL Engine_InsertBefore(Engine *self, Node *n, Node *before)
{
    (void)self;
    n->prev = before->prev;
    n->prev->next = n;
    n->next = before;
    before->prev = n;
    return n;
}
