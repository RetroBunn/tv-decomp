/*
 * Stage 2 helpers.
 *
 * Stage 2 is where prosody is decided -- stress, phrase shape, the pitch
 * contour -- and most of its rules are questions about what lies on either
 * side of the phoneme in hand.  Stage2_Scan is how it asks them: walk a
 * number of words forwards or boundaries backwards, and say whether the
 * first mask matched before the second one stopped the search.
 *
 * The two masks are signed, and the sign is not part of the value: a
 * negative mask means the same test inverted.  Modes 1 and 2 replace the
 * phoneme-attribute test with a look at the node's own stress bits, which
 * is why the same function answers both "is there a vowel before the next
 * boundary" and "is the next word stressed".
 */
#include "es_engine.h"

/* @0x1001aa60 */
uint8_t TV_THISCALL Stage2_Scan(Engine *self, int32_t dir, int32_t count,
                                int32_t mask1, int32_t mask2, int32_t mode)
{
    Node *n = self->stage_ctx[2].ctl;
    uint8_t result = 0;
    uint32_t st;
    int32_t kind, v;

    if (count <= 0)
        return 0;
    for (;;) {
        if ((uint8_t)dir == 1) {
            n = Node_NextWord(self, n);
            if (n == NULL)
                break;
        } else {
            n = Node_PrevBoundary(self, n);
            if (n == NULL)
                break;
        }

        if ((uint8_t)mode != 1) {
            if (Phone_TestMask(n, mask1, 0)) {
                result = 1;
                break;
            }
        } else {
            kind = 0;
            st = n->flags & 0x18u;
            if (st == 0x18) {
                kind = 1;
            } else {
                v = mask1 < 0 ? -mask1 : mask1;
                if (v == 2 && st == 0x10)
                    kind = 2;
            }
            if ((kind != 0 && mask1 > 0) || (kind == 0 && mask1 < 0)) {
                result = 1;
                break;
            }
        }

        if ((uint8_t)mode != 2) {
            if (Phone_TestMask(n, mask2, -1))
                break;
        } else {
            kind = 0;
            st = n->flags & 0x18u;
            if (st == 0x18) {
                kind = 1;
            } else {
                v = mask2 < 0 ? -mask2 : mask2;
                if (v == 2 && st == 0x10)
                    kind = 2;
            }
            if ((kind != 0 && mask2 < 0) || (kind == 0 && mask2 > 0)) {
                result = 0;
                break;
            }
        }

        count--;
        if (count <= 0)
            break;
    }
    return result;
}
