/*
 * Emulation of raw offset accesses the original makes into the engine
 * object outside the member being indexed (only reachable with pathological
 * input).  In the hook build the object has the original layout, so a raw
 * access is exact; the portable build maps the original offset to members.
 */
#include "engine.h"

void Engine_ZeroDwordIfMinus1(Engine *self, uint32_t off32)
{
#if defined(TV_HOOK_BUILD)
    int32_t *p = (int32_t *)((uint8_t *)self + off32);
    if (off32 + 4 <= sizeof(Engine) && *p == -1)
        *p = 0;
#else
    /* TODO(portable): map off32 through the generated original-layout
     * accessor (tools/gen_struct.py) once the standalone build exists. */
    (void)self;
    (void)off32;
#endif
}
