/*
 * Small helpers of the synthesizer's frame pass.
 *
 * Synth_Frame (0x10002a40) itself is not decompiled yet; these are the
 * fixed-point helpers it calls, plus the little gate that keeps one frame's
 * work from running twice.
 */
#include "engine.h"

/* @0x10025150 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b)
{
    return (b * a) / 0x7fff;
}

/* @0x10025280 */
int32_t TV_STDCALL Synth_MulShr11(int32_t a, int32_t b)
{
    return (b * a) >> 11;
}

/* Returns the product at Q12 and leaves it at Q13 in *hi. */
/* @0x10025290 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi)
{
    int32_t v = b * a;

    *hi = v >> 13;
    return v >> 12;
}

/* @0x10004750 */
uint8_t TV_THISCALL Synth_Gate(Engine *self, int32_t op)
{
    if (op == 3) {
        if (self->synth_19ad != 0)
            return 0;
        if (self->synth_19ae != 0)
            return 0;
        self->synth_19ae = 1;
        return 1;
    }
    if (op == 4) {
        self->synth_19ad = 1;
        self->synth_19ae = 0;
        return 1;
    }
    if (op == 5)
        return self->synth_19ad;
    if (op == 6) {
        self->synth_19ad = 0;
        return 1;
    }
    return 0;
}
