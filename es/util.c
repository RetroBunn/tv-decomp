/*
 * Leaf utilities: the 96-bit flag sets the stages carry around, the two node
 * walks that find word and phrase boundaries, the phoneme attribute test, and
 * one piece of fixed-point arithmetic.
 *
 * All seven are the English engine's, and five of them match it exactly by
 * shape.  Two do not match in convention: Phone_TestMask is stdcall here and
 * cdecl there, and it reads the attribute table directly rather than through
 * a Phone_Attr call.  The bit sets are indexed from the far end -- bit 0
 * lives in bits[2] -- which is why every one of them computes 2 - bit/32.
 */
#include "es_engine.h"

/* @0x10058618 */
extern const uint8_t g_phone_attr[];

/* @0x1001da50 */
int32_t TV_CDECL Bits_Test(int32_t bit, const uint32_t *bits)
{
    uint32_t mask = 1u << (bit & 31);
    if (bits == NULL)
        return 0;
    return (bits[2 - bit / 32] & mask) != 0;
}

/* @0x1001da90 */
uint32_t TV_CDECL Bits_Set(int32_t bit, uint32_t *bits)
{
    uint32_t mask = 1u << (bit & 31);
    bits[2 - bit / 32] |= mask;
    return mask;
}

/* @0x1001dac0 */
uint32_t TV_CDECL Bits_Clear(int32_t bit, uint32_t *bits)
{
    uint32_t mask = 1u << (bit & 31);
    bits[2 - bit / 32] &= ~mask;
    return ~mask;
}

/* The next set bit at or after bit + 1, or 0 when there is none -- so a
 * caller cannot tell "bit 0 is set" from "nothing is set", and the engine
 * relies on bit 0 never being used. */
/* @0x1001daf0 */
int32_t TV_CDECL Bits_Next(int32_t bit, const uint32_t *bits)
{
    int32_t k = bit + 1;
    int32_t w = 2 - k / 32;
    int32_t b = k & 31;
    uint32_t m = 1u << b;
    const uint32_t *p = &bits[w];

    while (w >= 0) {
        if (b < 32) {
            uint32_t v = *p;
            do {
                if (v & m)
                    return (2 - w) * 32 + b;
                m <<= 1;
                b++;
            } while (b < 32);
        }
        m = 1;
        b = 0;
        p--;
        w--;
    }
    return 0;
}

/* Back to the previous phrase boundary (type 3), stopping at a word
 * boundary (type 4) if one comes first. */
/* @0x1001a930 */
Node *TV_THISCALL Node_PrevBoundary(Engine *self, Node *n)
{
    uint32_t type;

    if (n == NULL)
        return NULL;
    do {
        n = Engine_StagePrev(self, n);
        if (n == NULL)
            return NULL;
        type = NODE_TYPE(n);
        if (type == 3)
            return n;
    } while (type != 4);
    return n;
}

/* @0x1001a9b0 */
Node *TV_THISCALL Node_NextWord(Engine *self, Node *n)
{
    if (n == NULL)
        return NULL;
    do {
        n = Engine_StageNext(self, n);
        if (n == NULL)
            return NULL;
    } while (NODE_TYPE(n) != 3);
    return n;
}

/* Test a phoneme's attribute bits.  A negative mask inverts the answer, and
 * a negative neg inverts it again, so both together cancel.  The high byte
 * of the mask selects which attribute row to read, shifted down one because
 * the row stride is 0x80 rather than 0x100. */
/* @0x1001a960 */
uint8_t TV_STDCALL Phone_TestMask(Node *n, int32_t mask, int32_t neg)
{
    uint8_t r;
    int32_t inv = 0;

    if (n == NULL)
        return 0;
    if (mask < 0) {
        mask = -mask;
        inv = 1;
    }
    r = (uint8_t)(g_phone_attr[(int32_t)(int8_t)n->value | ((mask & 0xff00) >> 1)] &
                  (uint8_t)mask);
    if (inv)
        r = (uint8_t)(r == 0);
    if (neg < 0)
        r = (uint8_t)(r == 0);
    return r;
}

/* @0x1000aee0 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi)
{
    int32_t v = b * a;

    *hi = v >> 13;
    return v >> 12;
}

/* @0x1000af00 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b)
{
    return (b * a) / 0x7fff;
}
