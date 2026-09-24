/*
 * Unit comparisons between CGRM_ES.DLL functions and the decompiled
 * replacements in es/.  Run with hooks disabled so the original entry points
 * are intact:  tvh_hook_es.exe -H none -U all TruVoice/CGRM_ES.DLL
 *
 * The differential test cannot reach everything.  Engine_MidPut refuses to
 * write when fewer than one slot is free, but the preformatter only ever
 * moves 400 characters per flush and the input stage drains the ring every
 * step, so a full mid ring never happens however much text you feed in --
 * changing that guard from "< 1" to "< 2" passes the whole corpus.  The only
 * way to test a boundary the interface cannot reach is to set the ring
 * indices directly and call the function, which is what this does.
 *
 * Every case compares the entire engine object afterwards, so a write to the
 * wrong field is caught as well as a wrong return value.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es_engine.h"
#include "unit.h"

#define ORIG(type, va) ((type)(uintptr_t)(va))

/* Ring positions worth trying: both ends, the wrap, the ten-slot reserve
 * Engine_InPut keeps, and the middle. */
static const int32_t POS[] = {
    0, 1, 2, 3, 9, 10, 11, 12, 0x7ff, 0x800, 0x801,
    0xff4, 0xff5, 0xff6, 0xffd, 0xffe, 0xfff,
};
#define NPOS ((int)(sizeof POS / sizeof POS[0]))

static Engine *g_a, *g_b;

/* The engine holds pointers into itself -- the node pool, the stage cursor,
 * the per-track buffers -- so two engines at different addresses never
 * compare equal byte for byte however identically they were built.  Rewrite
 * every aligned word that points inside the object as its offset, and the
 * comparison becomes about content again.  A non-pointer that happens to
 * land in the object's address range is normalised on both sides, so it
 * cannot turn a difference into a match. */
static void normalize(Engine *e)
{
    uintptr_t base = (uintptr_t)e;
    uint32_t *w = (uint32_t *)e;
    size_t i;
    for (i = 0; i < sizeof *e / 4; i++)
        if ((uintptr_t)w[i] >= base && (uintptr_t)w[i] < base + sizeof *e)
            w[i] = (uint32_t)((uintptr_t)w[i] - base);
}

static void setup(int32_t rd, int32_t wr, int mid)
{
    memset(g_a, 0x5a, sizeof *g_a);
    memset(g_b, 0x5a, sizeof *g_b);
    if (mid) {
        g_a->mid_rd = g_b->mid_rd = rd;
        g_a->mid_wr = g_b->mid_wr = wr;
    } else {
        g_a->in_rd = g_b->in_rd = rd;
        g_a->in_wr = g_b->in_wr = wr;
    }
}

static int differ(const char *what, int32_t rd, int32_t wr, int64_t ra, int64_t rb)
{
    if (ra == rb && memcmp(g_a, g_b, sizeof *g_a) == 0)
        return 0;
    fprintf(stderr, "  %s(rd=%#x, wr=%#x): orig returned %lld, ours %lld%s\n",
            what, (unsigned)rd, (unsigned)wr, (long long)ra, (long long)rb,
            ra == rb ? " (state differs)" : "");
    return 1;
}

/* this -> int32 */
static int cmp_i(const char *what, uint32_t va, int32_t (TV_THISCALL *ours)(Engine *),
                 int mid)
{
    typedef int32_t(TV_THISCALL * fn)(Engine *);
    int i, j, bad = 0, n = 0;
    for (i = 0; i < NPOS; i++)
        for (j = 0; j < NPOS; j++) {
            int64_t ra, rb;
            setup(POS[i], POS[j], mid);
            ra = ORIG(fn, va)(g_a);
            rb = ours(g_b);
            n++;
            bad += differ(what, POS[i], POS[j], ra, rb);
        }
    fprintf(stderr, "%-18s %d/%d identical\n", what, n - bad, n);
    return bad != 0;
}

/* this -> uint8 */
static int cmp_b(const char *what, uint32_t va, uint8_t (TV_THISCALL *ours)(Engine *),
                 int mid)
{
    typedef uint8_t(TV_THISCALL * fn)(Engine *);
    int i, j, bad = 0, n = 0;
    for (i = 0; i < NPOS; i++)
        for (j = 0; j < NPOS; j++) {
            int64_t ra, rb;
            setup(POS[i], POS[j], mid);
            ra = ORIG(fn, va)(g_a);
            rb = ours(g_b);
            n++;
            bad += differ(what, POS[i], POS[j], ra, rb);
        }
    fprintf(stderr, "%-18s %d/%d identical\n", what, n - bad, n);
    return bad != 0;
}

/* this, char -> uint8.  0x92 is in the character set on purpose: it is the
 * one value Engine_InPut rewrites. */
static int cmp_put(const char *what, uint32_t va,
                   uint8_t (TV_THISCALL *ours)(Engine *, uint8_t), int mid)
{
    typedef uint8_t(TV_THISCALL * fn)(Engine *, uint8_t);
    static const uint8_t chars[] = {0x00, 0x20, 0x41, 0x27, 0x92, 0x93, 0xf1, 0xff};
    int i, j, k, bad = 0, n = 0;
    for (i = 0; i < NPOS; i++)
        for (j = 0; j < NPOS; j++)
            for (k = 0; k < (int)(sizeof chars); k++) {
                int64_t ra, rb;
                setup(POS[i], POS[j], mid);
                ra = ORIG(fn, va)(g_a, chars[k]);
                rb = ours(g_b, chars[k]);
                n++;
                bad += differ(what, POS[i], POS[j], ra, rb);
            }
    fprintf(stderr, "%-18s %d/%d identical\n", what, n - bad, n);
    return bad != 0;
}

/* Engine_Flush moves at most 400 characters per call.  Changing that limit
 * to 401 passes the whole corpus, because it only changes how the work is
 * split across calls and not the sequence of characters that reaches the
 * preformatter -- the audio is identical either way.  Here it is visible:
 * one call, a known number of characters waiting, and the engine object
 * compared afterwards.
 *
 * Both sides need a real engine rather than a poisoned one, because the
 * function calls Preformat_PutChar, which walks live state.  With hooks
 * disabled that is the original in both cases, so the comparison is still
 * only about Engine_Flush. */
typedef void(TV_THISCALL * ctor_fn)(Engine *);
typedef int32_t(TV_THISCALL * init_fn)(Engine *);

/* A properly constructed engine.  Functions that reach into the pipeline --
 * anything that ends up in Preformat_Run -- walk live state, so a poisoned
 * object is not good enough the way it is for the ring accessors. */
static void fresh(Engine *g, uint8_t *shared)
{
    ORIG(ctor_fn, 0x1000ddc0)(g);
    ORIG(init_fn, 0x100086a0)(g);
    g->out_buf = shared;
    g->out_count = 0;
    g->textin_on = 0;
    g->textin = NULL;
}

static int unit_flush(void)
{
    typedef void(TV_THISCALL * fn)(Engine *, int32_t);
    static const int32_t FILL[] = {0, 1, 2, 399, 400, 401, 402, 800, 801, 4000};
    int k, i, bad = 0, n = 0;
    /* One buffer for both engines: two allocations would give two different
     * pointers and the whole-object comparison would never match. */
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);

    for (k = 0; k < (int)(sizeof FILL / sizeof FILL[0]); k++) {
        Engine *e[2];
        int which;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            e[which] = g;
            fresh(g, shared);
            for (i = 0; i < FILL[k]; i++)
                g->in_ring[i] = (uint8_t)("abcdefg hijklmn. "[i % 17]);
            g->in_rd = 0;
            g->in_wr = FILL[k];
        }
        ORIG(fn, 0x1001c710)(e[0], 0);
        Engine_Flush(e[1], 0);
        normalize(e[0]);
        normalize(e[1]);
        n++;
        if (memcmp(e[0], e[1], sizeof *e[0]) != 0) {
            const uint8_t *p = (const uint8_t *)e[0], *q = (const uint8_t *)e[1];
            size_t off, shown = 0;
            bad++;
            fprintf(stderr, "  Engine_Flush(%d waiting): in_rd=%#x/%#x mid_wr=%#x/%#x\n",
                    (int)FILL[k], (unsigned)e[0]->in_rd, (unsigned)e[1]->in_rd,
                    (unsigned)e[0]->mid_wr, (unsigned)e[1]->mid_wr);
            for (off = 0; off < sizeof *e[0] && shown < 6; off++)
                if (p[off] != q[off]) {
                    size_t start = off;
                    while (off < sizeof *e[0] && p[off] != q[off])
                        off++;
                    fprintf(stderr, "      differs at 0x%04x..0x%04x\n",
                            (unsigned)start, (unsigned)off);
                    shown++;
                }
        }
    }
    fprintf(stderr, "%-18s %d/%d identical\n", "Engine_Flush", n - bad, n);
    return bad != 0;
}

static int alloc_engines(void)
{
    if (g_a && g_b)
        return 0;
    g_a = (Engine *)VirtualAlloc(NULL, sizeof(Engine), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g_b = (Engine *)VirtualAlloc(NULL, sizeof(Engine), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_a || !g_b) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    return 0;
}

/* Preformat_PutChar has a small enough input domain to cover completely:
 * every one of the 256 character values, and free_nodes either side of the
 * 0x260 it compares against.  The corpus reaches most of the character
 * branches now that it has control characters in it, but "most" is not the
 * same as all, and this is cheap. */
static int unit_putchar(void)
{
    typedef void(TV_THISCALL * fn)(Engine *, uint8_t);
    static const int32_t NODES[] = {0, 0x25f, 0x260, 0x261, 0x400};
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    int c, k, bad = 0, n = 0;

    for (k = 0; k < (int)(sizeof NODES / sizeof NODES[0]); k++)
        for (c = 0; c < 256; c++) {
            fresh(g_a, shared);
            fresh(g_b, shared);
            g_a->free_nodes = g_b->free_nodes = NODES[k];
            ORIG(fn, 0x10008060)(g_a, (uint8_t)c);
            Preformat_PutChar(g_b, (uint8_t)c);
            normalize(g_a);
            normalize(g_b);
            n++;
            if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Preformat_PutChar(%#04x, free_nodes=%#x): "
                                    "pre_wr=%#x/%#x s2_1d54=%d/%d\n",
                            (unsigned)c, (unsigned)NODES[k],
                            (unsigned)g_a->pre_wr, (unsigned)g_b->pre_wr,
                            g_a->s2_1d54, g_b->s2_1d54);
            }
        }
    fprintf(stderr, "%-18s %d/%d identical\n", "Preformat_PutChar", n - bad, n);
    return bad != 0;
}

static int unit_rings(void)
{
    int bad = 0;
    if (alloc_engines())
        return 2;
    bad |= cmp_i("Engine_InFree", 0x1000e5a0, Engine_InFree, 0);
    bad |= cmp_i("Engine_InGet", 0x1000e5c0, Engine_InGet, 0);
    bad |= cmp_b("Engine_InUnget", 0x1000e600, Engine_InUnget, 0);
    bad |= cmp_put("Engine_InPut", 0x1000e630, Engine_InPut, 0);
    bad |= cmp_b("Engine_InPutEnd", 0x1000e680, Engine_InPutEnd, 0);
    bad |= cmp_i("Engine_MidFree", 0x1000e4f0, Engine_MidFree, 1);
    bad |= cmp_i("Engine_MidGet", 0x1000e510, Engine_MidGet, 1);
    bad |= cmp_put("Engine_MidPut", 0x1000e550, Engine_MidPut, 1);
    return bad != 0;
}

int unit_run(const char *name)
{
    if (alloc_engines())
        return 2;
    if (!strcmp(name, "rings")) return unit_rings();
    if (!strcmp(name, "flush")) return unit_flush();
    if (!strcmp(name, "putchar")) return unit_putchar();
    if (!strcmp(name, "all")) return unit_rings() | unit_flush() | unit_putchar();
    fprintf(stderr, "unknown unit test %s\n", name);
    return 2;
}
