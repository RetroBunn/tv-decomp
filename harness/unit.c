/*
 * Exhaustive unit comparisons between original DLL functions and their
 * decompiled replacements, for functions with small input domains that the
 * corpus does not cover well.  Run with hooks disabled (-H none) so the
 * original entry points are intact: `tvh_hook.exe -H none -U <name> <dll>`.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "unit.h"

#define ORIG(type, va) ((type)(uintptr_t)(va))

static Engine *new_engine(void)
{
    return (Engine *)VirtualAlloc(NULL, sizeof(Engine), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

static int unit_setvolume(void)
{
    typedef void(TV_THISCALL * fn)(Engine *, uint32_t);
    Engine *a = new_engine(), *b = new_engine();
    uint32_t v, bad = 0, n = 0;
    static const uint32_t extra[] = {0x10000, 0x12345, 0xfffff, 0x7fffffff, 0x80000000, 0xffffffff};
    for (v = 0; v < 0x20000 + TV_COUNTOF(extra); v++) {
        uint32_t vol = v < 0x20000 ? v : extra[v - 0x20000];
        memset(a, 0x5a, sizeof *a);
        memset(b, 0x5a, sizeof *b);
        ORIG(fn, 0x1002c870)(a, vol);
        Engine_SetVolume(b, vol);
        n++;
        if (memcmp(a, b, sizeof *a) != 0) {
            if (bad++ < 10)
                fprintf(stderr, "SetVolume(0x%x): orig atten=%d mute=%d, ours atten=%d mute=%d\n",
                        vol, a->volume_atten, a->mute, b->volume_atten, b->mute);
        }
    }
    fprintf(stderr, "SetVolume: %u/%u identical\n", n - bad, n);
    return bad != 0;
}

static int unit_scaleparam(void)
{
    typedef int32_t(TV_CDECL * fn)(int32_t, uint8_t);
    int32_t k, bad = 0;
    int r;
    for (k = -3; k < 30; k++)
        for (r = 0; r < 256; r++)
            if (ORIG(fn, 0x1002c920)(k, (uint8_t)r) != Synth_ScaleParam(k, (uint8_t)r))
                bad++;
    fprintf(stderr, "ScaleParam: %d mismatches\n", bad);
    return bad != 0;
}

int unit_run(const char *name)
{
    if (!strcmp(name, "setvolume")) return unit_setvolume();
    if (!strcmp(name, "scaleparam")) return unit_scaleparam();
    if (!strcmp(name, "all")) return unit_setvolume() | unit_scaleparam();
    fprintf(stderr, "unknown unit test %s\n", name);
    return 2;
}
