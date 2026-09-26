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
    /* Zero first.  Engine_ResetNodes relinks the node pool but does not clear
     * what the nodes hold, so without this a case that diverges leaves stale
     * node contents behind and every later case reports a difference it did
     * not cause.  Both engines get the same treatment, so the comparison is
     * unaffected -- only the isolation between cases improves. */
    memset(g, 0, sizeof *g);
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

/* Engine_InputStage reads from mid_ring, so a case is just a byte string put
 * there directly.  That reaches things no text can: the ten-character limit
 * exactly, a control record of every length including one the jump table
 * sends to the default arm, and the '[' and ']' forms under each combination
 * of the two flag words that gate them. */
struct in_case {
    const char *what;
    int32_t flags_a, flags_n;
    int len;
    unsigned char bytes[24];
};

static const struct in_case IN_CASES[] = {
    {"empty",            0x20, 0,  0,  {0}},
    {"one letter",       0x20, 0,  1,  {'a'}},
    {"a word",           0x20, 0,  4,  {'h', 'o', 'l', 'a'}},
    {"space first",      0x20, 0,  4,  {' ', 'h', 'o', 'l'}},
    {"space then word",  0x20, 0,  6,  {'h', 'o', ' ', 'l', 'a', 's'}},
    {"nine letters",     0x20, 0,  9,  {'a','b','c','d','e','f','g','h','i'}},
    {"ten letters",      0x20, 0, 10,  {'a','b','c','d','e','f','g','h','i','j'}},
    {"eleven letters",   0x20, 0, 11,  {'a','b','c','d','e','f','g','h','i','j','k'}},
    {"fifteen letters",  0x20, 0, 15,  {'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o'}},
    /* control records: ESC, letter, length, then that many bytes */
    {"ctl len 1",        0x20, 0,  4,  {0x1b, 'i', 1, 0x05}},
    {"ctl len 2 A",      0x20, 0,  5,  {0x1b, 'A', 2, 0x12, 0x34}},
    {"ctl len 2 N",      0x20, 0,  5,  {0x1b, 'N', 2, 0x56, 0x78}},
    {"ctl len 3",        0x20, 0,  6,  {0x1b, 'p', 3, 0x0c, 0x11, 0x22}},
    {"ctl len 3 bits",   0x20, 0,  6,  {0x1b, 'p', 3, 0x0f, 0x11, 0x22}},
    {"ctl len 4",        0x20, 0,  7,  {0x1b, 'i', 4, 1, 2, 3, 4}},
    {"ctl len 5",        0x20, 0,  8,  {0x1b, 'z', 5, 1, 2, 3, 4, 5}},
    {"ctl len 0",        0x20, 0,  3,  {0x1b, 'q', 0}},
    /* the bracket forms, under each gate */
    {"[ gated on",       0x20, 0,  1,  {'['}},
    {"[ gated on +40",   0x60, 0,  1,  {'['}},
    {"[ gate A off",     0x00, 0,  1,  {'['}},
    {"[ gate N set",     0x20, 1,  1,  {'['}},
    {"[ gate N set 2",   0x20, 2,  1,  {'['}},
    {"[ gate N set 4",   0x20, 4,  1,  {'['}},
    {"] gated on",       0x20, 0,  1,  {']'}},
    {"] gate A off",     0x00, 0,  1,  {']'}},
    {"mixed",            0x60, 0,  9,  {'a', '[', 'b', ']', ' ', 'c', 0x1b, 'i', 1}},
};

static int unit_input(void)
{
    typedef uint8_t(TV_THISCALL * fn)(Engine *);
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    int k, i, bad = 0, n = 0;

    for (k = 0; k < (int)(sizeof IN_CASES / sizeof IN_CASES[0]); k++) {
        const struct in_case *t = &IN_CASES[k];
        uint8_t ra, rb;
        int which;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            fresh(g, shared);
            for (i = 0; i < t->len; i++)
                g->mid_ring[i] = t->bytes[i];
            g->mid_rd = 0;
            g->mid_wr = t->len;
            g->e_9180 = -1;
            g->in_flags_A = t->flags_a;
            g->in_flags_N = t->flags_n;
            g->item_notify = 0xfeed;
        }
        ra = ORIG(fn, 0x1000e290)(g_a);
        rb = Engine_InputStage(g_b);
        normalize(g_a);
        normalize(g_b);
        n++;
        if (ra != rb || memcmp(g_a, g_b, sizeof *g_a) != 0) {
            bad++;
            fprintf(stderr, "  Engine_InputStage[%s]: returned %d/%d, "
                            "free_nodes=%d/%d mid_rd=%#x/%#x\n",
                    t->what, ra, rb, (int)g_a->free_nodes, (int)g_b->free_nodes,
                    (unsigned)g_a->mid_rd, (unsigned)g_b->mid_rd);
        }
    }
    fprintf(stderr, "%-18s %d/%d identical\n", "Engine_InputStage", n - bad, n);
    return bad != 0;
}

/* Engine_NodeAlloc's interesting behaviour is the tail of it: when the
 * running stage's window starts or ends at the node being inserted next to,
 * the window has to follow.  Engine_InputStage never reaches that, because
 * self->stage is null while it runs, so it needs its own cases.
 *
 * Each case builds identical state on both engines with the *original*
 * Engine_AppendNode, so the only thing under test is the one call that
 * follows. */
static int unit_nodealloc(void)
{
    typedef Node *(TV_THISCALL * alloc_t)(Engine *, Node *, int32_t, int32_t, uint8_t);
    typedef Node *(TV_THISCALL * append_t)(Engine *, int32_t, int32_t);
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    /* stage index (-1 for none), which window field to aim at ref, after, type */
    static const struct { int stage, aim, after, type; } CASES[] = {
        {-1, 0, 0, 1}, {-1, 0, 1, 1},
        { 0, 0, 0, 1}, { 0, 0, 1, 1},
        { 0, 1, 0, 1}, { 0, 1, 1, 1},   /* aim 1: st->first = ref */
        { 0, 2, 0, 1}, { 0, 2, 1, 1},   /* aim 2: st->last  = ref */
        { 1, 2, 1, 1}, { 2, 1, 0, 1}, { 3, 2, 1, 1}, { 4, 1, 0, 1},
        { 0, 2, 1, 0}, { 0, 2, 1, 2}, { 0, 2, 1, 3}, { 0, 2, 1, 4},
        { 0, 2, 1, 5}, { 0, 2, 1, 6}, { 0, 2, 1, 7},
    };
    int k, i, bad = 0, n = 0;

    for (k = 0; k < (int)(sizeof CASES / sizeof CASES[0]); k++) {
        Node *ref[2];
        int which;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            StageCtx *st;
            fresh(g, shared);
            /* The reference node has to have a real neighbour on both sides:
             * after == 1 inserts next to ref->next, and the tail sentinel's
             * next is null, so passing work_tail there walks off the end.
             * The middle of three appended nodes is safe either way. */
            for (i = 0; i < 3; i++) {
                Node *appended = ORIG(append_t, 0x10008b60)(g, 1, 'a' + i);
                if (i == 1)
                    ref[which] = appended;
            }
            /* Dirty the node that is about to be handed out.  A freshly
             * reset one has the per-stage flag bits clear already, so
             * without this the "flags &= ~0xf8" at the end of the function
             * has nothing to clear and any mistake in that mask is
             * invisible here -- as one was. */
            g->free_head->next->flags = 0xffffffffu;
            if (CASES[k].stage < 0) {
                g->stage = NULL;
            } else {
                st = &g->stage_ctx[CASES[k].stage];
                g->stage = st;
                if (CASES[k].aim == 1)
                    st->first = ref[which];
                else if (CASES[k].aim == 2)
                    st->last = ref[which];
            }
        }
        ORIG(alloc_t, 0x10008bd0)(g_a, ref[0], CASES[k].after, CASES[k].type, 'Z');
        Engine_NodeAlloc(g_b, ref[1], CASES[k].after, CASES[k].type, 'Z');
        normalize(g_a);
        normalize(g_b);
        n++;
        if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
            bad++;
            fprintf(stderr, "  Engine_NodeAlloc[stage=%d aim=%d after=%d type=%d]: "
                            "state differs (free_nodes=%d/%d)\n",
                    CASES[k].stage, CASES[k].aim, CASES[k].after, CASES[k].type,
                    (int)g_a->free_nodes, (int)g_b->free_nodes);
        }
    }
    fprintf(stderr, "%-18s %d/%d identical\n", "Engine_NodeAlloc", n - bad, n);
    return bad != 0;
}

/* Engine_Unlink and Engine_InsertBefore against a real five-node list, at
 * every position including next to each sentinel.  They are reached through
 * the allocator already; this pins the return value, which the allocator
 * only uses for one of the two. */
static int unit_list(void)
{
    typedef Node *(TV_THISCALL * unlink_t)(Engine *, Node *);
    typedef Node *(TV_THISCALL * insert_t)(Engine *, Node *, Node *);
    typedef Node *(TV_THISCALL * append_t)(Engine *, int32_t, int32_t);
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    int op, k, i, bad = 0, n = 0;

    for (op = 0; op < 2; op++)
        for (k = 0; k < 5; k++) {
            Node *chain[2][5], *spare[2], *ra = NULL, *rb = NULL;
            int which;
            for (which = 0; which < 2; which++) {
                Engine *g = which ? g_b : g_a;
                fresh(g, shared);
                for (i = 0; i < 5; i++)
                    chain[which][i] = ORIG(append_t, 0x10008b60)(g, 1, 'a' + i);
                /* for the insert cases, take one out first with the original
                 * on both sides so only the insert itself is under test */
                spare[which] = NULL;
                if (op == 1) {
                    spare[which] = ORIG(append_t, 0x10008b60)(g, 1, 'z');
                    ORIG(unlink_t, 0x10009110)(g, spare[which]);
                }
            }
            if (op == 0) {
                ra = ORIG(unlink_t, 0x10009110)(g_a, chain[0][k]);
                rb = Engine_Unlink(g_b, chain[1][k]);
            } else {
                ra = ORIG(insert_t, 0x10008ed0)(g_a, spare[0], chain[0][k]);
                rb = Engine_InsertBefore(g_b, spare[1], chain[1][k]);
            }
            /* the returned pointers are into different engines, so compare
             * them as offsets the way normalize() compares everything else */
            {
                uintptr_t oa = (uintptr_t)ra - (uintptr_t)g_a;
                uintptr_t ob = (uintptr_t)rb - (uintptr_t)g_b;
                normalize(g_a);
                normalize(g_b);
                n++;
                if (oa != ob || memcmp(g_a, g_b, sizeof *g_a) != 0) {
                    bad++;
                    fprintf(stderr, "  %s at %d: returned +%#x/+%#x%s\n",
                            op ? "Engine_InsertBefore" : "Engine_Unlink", k,
                            (unsigned)oa, (unsigned)ob,
                            oa == ob ? " (state differs)" : "");
                }
            }
        }
    fprintf(stderr, "%-18s %d/%d identical\n", "list surgery", n - bad, n);
    return bad != 0;
}

/* Preformat_Run, driven by putting a byte string straight into pre_ring and
 * draining it.  The parameter parser is where the boundaries are: sixteen
 * slots, an overflow flag at 255, and a digit handler that tests a slot for
 * -1 before it checks whether the slot exists, so past sixteen parameters it
 * writes into the engine field that follows the array. */
static const char *const ESC_CASES[] = {
    "hola",
    "\033[0i",
    "\033[1A", "\033[1;2;3A", "\033[8A", "\033[0A",
    "\033[1D", "\033[1;2;3;4;5;6;7D",
    "\033[1N", "\033[16N", "\033[17N", "\033[1;16F",
    "\033[C", "\033[H", "\033[S", "\033[w", "\033[x",
    "\033[0I", "\033[1I", "\033[2I", "\033[I",
    "\033[0P", "\033[1P", "\033[2P",
    "\033[0V", "\033[9V", "\033[10V", "\033[V",
    "\033[0a", "\033[16a", "\033[17a",
    "\033[0c", "\033[2c", "\033[3c",
    "\033[0f", "\033[9f", "\033[10f",
    "\033[1g", "\033[255g", "\033[256g", "\033[g",
    "\033[0i", "\033[1;2;3;4i", "\033[1;2;3;4;5i",
    "\033[0;1l", "\033[21;99l", "\033[22;1l", "\033[l",
    "\033[25p", "\033[200p", "\033[201p", "\033[24p", "\033[p",
    "\033[50r", "\033[49r", "\033[150r", "\033[0r", "\033[r",
    "\033[0v", "\033[25v", "\033[26v", "\033[v",
    "\033[0;1;2t", "\033[9;255;243t", "\033[10;0;0t", "\033[t",
    "\033[1s", "\033[255s",
    /* parameter parser boundaries */
    "\033[999r",
    "\033[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16r",
    "\033[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17r",
    "\033[;;;;;;;;;;;;;;;;;;;;;;;;5r",
    "\033[;;;;5r",
    /* malformed */
    "\033X", "\033\033[5V", "\033[12;34", "\033[5Z", "\033[",
};

static int unit_escape(void)
{
    typedef void(TV_THISCALL * fn)(Engine *);
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    int k, i, m, bad = 0, n = 0;

    for (m = 0; m < 2; m++)   /* once with index mode off, once with it on */
        for (k = 0; k < (int)(sizeof ESC_CASES / sizeof ESC_CASES[0]); k++) {
            const char *t = ESC_CASES[k];
            int len = (int)strlen(t);
            int which, guard;
            if (len > 0xff)
                continue;
            for (which = 0; which < 2; which++) {
                Engine *g = which ? g_b : g_a;
                fresh(g, shared);
                for (i = 0; i < len; i++)
                    g->pre_ring[i] = (uint8_t)t[i];
                g->pre_rd = 0;
                g->pre_wr = len;
                g->esc_state = 1;
                g->mode_I_on = m;
            }
            for (guard = 0; guard < 0x200 && g_a->pre_rd != g_a->pre_wr; guard++) {
                ORIG(fn, 0x10007810)(g_a);
                Preformat_Run(g_b);
            }
            normalize(g_a);
            normalize(g_b);
            n++;
            if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Preformat_Run[modeI=%d %s]: state differs "
                                    "(mid_wr=%#x/%#x esc_state=%d/%d)\n",
                            m, t, (unsigned)g_a->mid_wr, (unsigned)g_b->mid_wr,
                            (int)g_a->esc_state, (int)g_b->esc_state);
            }
        }
    fprintf(stderr, "%-18s %d/%d identical\n", "Preformat_Run", n - bad, n);
    return bad != 0;
}

/* The leaf utilities take no engine, so they can be swept outright.  The bit
 * sets are indexed from the far end -- bit 0 is in bits[2] -- so a bit past
 * 95 indexes before the array; the buffer is padded on both sides and the
 * pointer handed in points into the middle, which keeps a faithful test from
 * being an out-of-bounds one. */
static int unit_util(void)
{
    typedef int32_t(TV_CDECL * test_t)(int32_t, const uint32_t *);
    typedef uint32_t(TV_CDECL * set_t)(int32_t, uint32_t *);
    typedef int32_t(TV_CDECL * next_t)(int32_t, const uint32_t *);
    typedef uint8_t(TV_STDCALL * mask_t)(Node *, int32_t, int32_t);
    typedef int32_t(TV_STDCALL * mul_t)(int32_t, int32_t, int32_t *);
    static const int32_t BITS[] = {-33, -32, -1, 0, 1, 31, 32, 33, 63, 64, 65, 94, 95, 96, 127};
    static const uint32_t PAT[] = {0u, 0xffffffffu, 1u, 0x80000000u, 0xa5a5a5a5u};
    static const int32_t MASKS[] = {0, 1, 2, 0x7f, 0xff, 0x100, 0x1ff, 0x8001, -1, -0xff, -0x8001};
    static const int32_t MULS[] = {0, 1, -1, 2, 4095, 4096, -4096, 0x10000, -0x10000, 0x7fff};
    uint32_t a[16], b[16];
    int i, j, k, bad = 0, n = 0;

    for (i = 0; i < (int)(sizeof BITS / sizeof BITS[0]); i++)
        for (j = 0; j < (int)(sizeof PAT / sizeof PAT[0]); j++) {
            int32_t ra, rb;
            uint32_t sa, sb;
            for (k = 0; k < 16; k++)
                a[k] = b[k] = PAT[j] ^ (uint32_t)k;
            ra = ORIG(test_t, 0x1001da50)(BITS[i], a + 8);
            rb = Bits_Test(BITS[i], b + 8);
            n++;
            if (ra != rb || memcmp(a, b, sizeof a)) { bad++;
                fprintf(stderr, "  Bits_Test(%d, %#x): %d/%d\n", BITS[i], PAT[j], ra, rb); }

            ra = ORIG(next_t, 0x1001daf0)(BITS[i], a + 8);
            rb = Bits_Next(BITS[i], b + 8);
            n++;
            if (ra != rb) { bad++;
                fprintf(stderr, "  Bits_Next(%d, %#x): %d/%d\n", BITS[i], PAT[j], ra, rb); }

            sa = ORIG(set_t, 0x1001da90)(BITS[i], a + 8);
            sb = Bits_Set(BITS[i], b + 8);
            n++;
            if (sa != sb || memcmp(a, b, sizeof a)) { bad++;
                fprintf(stderr, "  Bits_Set(%d, %#x): %#x/%#x\n", BITS[i], PAT[j], sa, sb); }

            sa = ORIG(set_t, 0x1001dac0)(BITS[i], a + 8);
            sb = Bits_Clear(BITS[i], b + 8);
            n++;
            if (sa != sb || memcmp(a, b, sizeof a)) { bad++;
                fprintf(stderr, "  Bits_Clear(%d, %#x): %#x/%#x\n", BITS[i], PAT[j], sa, sb); }
        }

    for (i = 0; i < 256; i++)
        for (j = 0; j < (int)(sizeof MASKS / sizeof MASKS[0]); j++)
            for (k = -1; k <= 1; k++) {
                Node node;
                uint8_t ra, rb;
                memset(&node, 0, sizeof node);
                node.value = (uint8_t)i;
                ra = ORIG(mask_t, 0x1001a960)(&node, MASKS[j], k);
                rb = Phone_TestMask(&node, MASKS[j], k);
                n++;
                if (ra != rb) { if (bad++ < 8)
                    fprintf(stderr, "  Phone_TestMask(value=%#x, mask=%#x, neg=%d): %d/%d\n",
                            i, MASKS[j], k, ra, rb); }
            }
    /* and the null node, which is the first thing it checks */
    { uint8_t ra = ORIG(mask_t, 0x1001a960)(NULL, 1, 0), rb = Phone_TestMask(NULL, 1, 0);
      n++; if (ra != rb) { bad++; fprintf(stderr, "  Phone_TestMask(NULL): %d/%d\n", ra, rb); } }

    for (i = 0; i < (int)(sizeof MULS / sizeof MULS[0]); i++)
        for (j = 0; j < (int)(sizeof MULS / sizeof MULS[0]); j++) {
            int32_t ha = 0x5a5a5a5a, hb = 0x5a5a5a5a, ra, rb;
            ra = ORIG(mul_t, 0x1000aee0)(MULS[i], MULS[j], &ha);
            rb = Synth_MulShr12(MULS[i], MULS[j], &hb);
            n++;
            if (ra != rb || ha != hb) { bad++;
                fprintf(stderr, "  Synth_MulShr12(%d, %d): %d/%d hi %d/%d\n",
                        MULS[i], MULS[j], ra, rb, ha, hb); }
        }

    {
        typedef int32_t(TV_STDCALL * mul2_t)(int32_t, int32_t);
        for (i = 0; i < (int)(sizeof MULS / sizeof MULS[0]); i++)
            for (j = 0; j < (int)(sizeof MULS / sizeof MULS[0]); j++) {
                int32_t ra = ORIG(mul2_t, 0x1000aed0)(MULS[i], MULS[j]);
                int32_t rb = Synth_MulShr11(MULS[i], MULS[j]);
                n++;
                if (ra != rb) { if (bad++ < 8)
                    fprintf(stderr, "  Synth_MulShr11(%d, %d): %d/%d\n",
                            MULS[i], MULS[j], ra, rb); }
            }
    }

    /* Synth_Gate: every operation the callers could pass, against all four
     * states of the two flags it reads.  The whole engine is compared, not
     * just the answer, because three of the four operations write. */
    {
        typedef uint8_t(TV_THISCALL * gate_t)(Engine *, int32_t);
        int op, st;
        for (op = -2; op <= 10; op++)
            for (st = 0; st < 4; st++) {
                uint8_t ra, rb;
                memset(g_a, 0x5a, sizeof *g_a);
                memset(g_b, 0x5a, sizeof *g_b);
                g_a->synth_19ad = g_b->synth_19ad = (uint8_t)(st & 1);
                g_a->synth_19ae = g_b->synth_19ae = (uint8_t)((st >> 1) & 1);
                ra = ORIG(gate_t, 0x1000ea70)(g_a, op);
                rb = Synth_Gate(g_b, op);
                normalize(g_a);
                normalize(g_b);
                n++;
                if (ra != rb || memcmp(g_a, g_b, sizeof *g_a) != 0) {
                    if (bad++ < 8)
                        fprintf(stderr, "  Synth_Gate(op=%d, flags=%d): %d/%d\n",
                                op, st, ra, rb);
                }
            }
    }

    /* TextIn_Unmatched does nothing and says so; the check is that it really
     * does nothing, including to the token it is handed. */
    {
        typedef int32_t(TV_THISCALL * um_t)(TextIn *, Token *);
        uint8_t ta[sizeof(Token)], tb[sizeof(Token)];
        int32_t ra, rb;
        memset(g_a, 0x5a, sizeof *g_a);
        memset(g_b, 0x5a, sizeof *g_b);
        memset(ta, 0x33, sizeof ta);
        memset(tb, 0x33, sizeof tb);
        ra = ORIG(um_t, 0x1001fdd0)((TextIn *)g_a, (Token *)ta);
        rb = TextIn_Unmatched((TextIn *)g_b, (Token *)tb);
        n++;
        if (ra != rb || memcmp(ta, tb, sizeof ta) != 0 ||
            memcmp(g_a, g_b, sizeof *g_a) != 0) {
            if (bad++ < 8)
                fprintf(stderr, "  TextIn_Unmatched: %d/%d\n", ra, rb);
        }
    }

    /* Stage0_CharClass: every class number the interpreter could hand it,
     * including out-of-range ones, against every byte value. */
    {
        typedef uint8_t(TV_CDECL * cls_t)(int32_t, uint8_t);
        int cls;
        for (cls = -8; cls < 40; cls++)
            for (j = 0; j < 256; j++) {
                uint8_t ra = ORIG(cls_t, 0x100141d0)(cls, (uint8_t)j);
                uint8_t rb = Stage0_CharClass(cls, (uint8_t)j);
                n++;
                if (ra != rb) {
                    if (bad++ < 8)
                        fprintf(stderr, "  Stage0_CharClass(%d, %#04x): %d/%d\n",
                                cls, (unsigned)j, ra, rb);
                }
            }
    }

    /* The two set comparisons.  Every word position gets to be the one that
     * decides, with the other two filled either all-ones or all-zero so that
     * neither function can short-circuit before reaching it. */
    {
        typedef int32_t(TV_CDECL * bb_t)(const uint32_t *, const uint32_t *);
        static const uint32_t SETS[] = {
            0u, 1u, 3u, 0x80000000u, 0xffffffffu, 0xa5a5a5a5u,
        };
        static const uint32_t FILL[] = {0u, 0xffffffffu};
        int w, si, sj, fi;
        for (fi = 0; fi < 2; fi++)
            for (w = 0; w < 3; w++)
                for (si = 0; si < 6; si++)
                    for (sj = 0; sj < 6; sj++) {
                        uint32_t x[3], y[3];
                        int32_t ra, rb;
                        for (k = 0; k < 3; k++)
                            x[k] = y[k] = FILL[fi];
                        x[w] = SETS[si];
                        y[w] = SETS[sj];
                        ra = ORIG(bb_t, 0x1001db60)(x, y);
                        rb = Bits_AllIn(x, y);
                        n++;
                        if (ra != rb) { if (bad++ < 8)
                            fprintf(stderr, "  Bits_AllIn(w=%d %#x, %#x): %d/%d\n",
                                    w, SETS[si], SETS[sj], ra, rb); }
                        ra = ORIG(bb_t, 0x1001db90)(x, y);
                        rb = Bits_AnyIn(x, y);
                        n++;
                        if (ra != rb) { if (bad++ < 8)
                            fprintf(stderr, "  Bits_AnyIn(w=%d %#x, %#x): %d/%d\n",
                                    w, SETS[si], SETS[sj], ra, rb); }
                    }
    }

    /* Synth_ScaleParam: every raw byte against every index the jump table
     * covers and a margin either side of it. */
    {
        typedef int32_t(TV_STDCALL * sp_t)(int32_t, uint8_t);
        int idx;
        for (idx = -4; idx < 24; idx++)
            for (j = 0; j < 256; j++) {
                int32_t ra = ORIG(sp_t, 0x1001bfd0)(idx, (uint8_t)j);
                int32_t rb = Synth_ScaleParam(idx, (uint8_t)j);
                n++;
                if (ra != rb) { if (bad++ < 8)
                    fprintf(stderr, "  Synth_ScaleParam(%d, %d): %d/%d\n",
                            idx, j, ra, rb); }
            }
    }

    /* The two index loaders, both ways round: from cold each has to build
     * the same table the original builds and return what the original
     * returns, and on any later call it has to leave the table alone.  The
     * loaded flag is reset by hand because nothing else can -- neither
     * original ever clears it.  The table is put back afterwards so the
     * engine the rest of the suite uses is unharmed. */
    {
        typedef int32_t(TV_CDECL * init_t)(void);
        static const struct {
            const char *name;
            uint32_t va, table, flag;
            int entries;
        } LOADERS[] = {
            {"Abbrev_Init", 0x1001dd10, 0x10045898, 0x10068d3c, 507},
            {"Lexicon_Init", 0x10023120, 0x10046088, 0x1006b610, 78},
        };
        static int32_t save[507];
        int li;
        for (li = 0; li < 2; li++) {
            int32_t *idx = (int32_t *)(uintptr_t)LOADERS[li].table;
            int32_t *flag = (int32_t *)(uintptr_t)LOADERS[li].flag;
            size_t bytes = (size_t)LOADERS[li].entries * sizeof *idx;
            int32_t ra, rb;

            *flag = 0;
            memset(idx, 0x5a, bytes);
            ra = ((init_t)(uintptr_t)LOADERS[li].va)();
            memcpy(save, idx, bytes);

            *flag = 0;
            memset(idx, 0x5a, bytes);
            rb = li ? Lexicon_Init() : Abbrev_Init();
            n++;
            if (ra != rb || memcmp(save, idx, bytes) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  %s from cold: %d/%d, table %s\n",
                            LOADERS[li].name, ra, rb,
                            memcmp(save, idx, bytes) ? "differs" : "same");
            }

            memset(idx, 0x33, bytes);
            rb = li ? Lexicon_Init() : Abbrev_Init();
            n++;
            if (rb != 1 || *(unsigned char *)idx != 0x33) {
                if (bad++ < 8)
                    fprintf(stderr, "  %s rebuilt the table on a later call\n",
                            LOADERS[li].name);
            }
            memcpy(idx, save, bytes);
        }
    }

    /* The three vowel tests, over every byte.  Both copies of Vowel_Index
     * are checked at their own addresses: they are the same 35 bytes, so if
     * only one were replaced the test would still pass and the other would
     * quietly keep running the original. */
    {
        typedef int32_t(TV_STDCALL * vi_t)(uint8_t);
        typedef uint8_t(TV_STDCALL * iv_t)(uint8_t);
        for (j = 0; j < 256; j++) {
            uint8_t c = (uint8_t)j;
            int32_t ra = ORIG(vi_t, 0x100132b0)(c), rb = Vowel_Index(c);
            int32_t ra2 = ORIG(vi_t, 0x100121f0)(c), rb2 = Vowel_Index2(c);
            uint8_t va = ORIG(iv_t, 0x100132e0)(c), vb = Is_Vowel(c);
            n += 3;
            if (ra != rb) { if (bad++ < 8)
                fprintf(stderr, "  Vowel_Index(%#04x): %d/%d\n", (unsigned)j, ra, rb); }
            if (ra2 != rb2) { if (bad++ < 8)
                fprintf(stderr, "  Vowel_Index2(%#04x): %d/%d\n", (unsigned)j, ra2, rb2); }
            if (va != vb) { if (bad++ < 8)
                fprintf(stderr, "  Is_Vowel(%#04x): %d/%d\n", (unsigned)j, va, vb); }
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "leaf utilities", n - bad, n);
    return bad != 0;
}

/* The engine's own lifecycle.  Engine_Step runs thousands of times per
 * utterance so the corpus covers it heavily, but the reset chain has state
 * no input reaches: Engine_ResetNodes hands eight of the nine preformat
 * fields to each stage context and leaves p_38 alone, which is invisible in
 * the audio because nothing in the corpus has an ESC[A or ESC[D in force
 * across a reset.  Setting the fields to distinctive values makes it
 * visible immediately. */
static int unit_lifecycle(void)
{
    typedef Engine *(TV_THISCALL * ctor2_t)(Engine *);
    typedef int32_t(TV_THISCALL * i_t)(Engine *);
    typedef void(TV_THISCALL * v_t)(Engine *);
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    int k, bad = 0, n = 0;
    int32_t ra, rb;

    /* Engine_Construct over a poisoned object */
    memset(g_a, 0x5a, sizeof *g_a);
    memset(g_b, 0x5a, sizeof *g_b);
    ORIG(ctor2_t, 0x1000ddc0)(g_a);
    Engine_Construct(g_b);
    normalize(g_a); normalize(g_b);
    n++;
    if (memcmp(g_a, g_b, sizeof *g_a)) { bad++;
        fprintf(stderr, "  Engine_Construct: state differs\n"); }

    /* Engine_Init, and Engine_Reset with and without a stop mark pending */
    for (k = 0; k < 3; k++) {
        int which;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            memset(g, 0x5a, sizeof *g);
            ORIG(ctor2_t, 0x1000ddc0)(g);
            g->out_buf = shared;
            g->out_count = 0;
            if (k != 0) {
                ORIG(i_t, 0x100086a0)(g);       /* a real engine to reset */
                g->stop_mark = (uint8_t)(k == 2);
                g->reset_value = 0x1234;
            }
        }
        if (k == 0) {
            ra = ORIG(i_t, 0x100086a0)(g_a);
            rb = Engine_Init(g_b);
        } else {
            ra = ORIG(i_t, 0x10008700)(g_a);
            rb = Engine_Reset(g_b);
        }
        normalize(g_a); normalize(g_b);
        n++;
        if (ra != rb || memcmp(g_a, g_b, sizeof *g_a)) { bad++;
            fprintf(stderr, "  %s[%d]: returned %d/%d%s\n",
                    k == 0 ? "Engine_Init" : "Engine_Reset", k, ra, rb,
                    ra == rb ? " (state differs)" : ""); }
    }

    /* Engine_ResetNodes and Engine_ResetRings, with every field they copy
     * from set to something distinctive first */
    for (k = 0; k < 2; k++) {
        int which;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            int j;
            fresh(g, shared);
            g->mode_I = 0x11; g->mode_P = 0x22; g->rate_index = 0x33;
            g->pitch = 0x44; g->volume_atten = 0x55; g->rate_class = 0x66;
            g->flags_N = 0x7777; g->flags_A = 0x8888; g->voice = 0x99;
            for (j = 0; j < 5; j++) {
                g->stage_ctx[j].p_34 = 0x0bad;
                g->stage_ctx[j].p_38 = 0x0bad;   /* must survive ResetNodes */
                g->stage_ctx[j].type_mask = 0x5eed;
            }
            g->in_rd = 0x111; g->in_wr = 0x222;
            g->pre_rd = 0x33; g->pre_wr = 0x44;
            g->mid_rd = 0x555; g->mid_wr = 0x666;
            g->e_9180 = 0x777;
            g->in_flags_N = 0x0bad; g->in_flags_A = 0x0bad;
        }
        if (k == 0) {
            ORIG(v_t, 0x10008ef0)(g_a);
            Engine_ResetNodes(g_b);
        } else {
            ORIG(v_t, 0x1000e240)(g_a);
            Engine_ResetRings(g_b);
        }
        normalize(g_a); normalize(g_b);
        n++;
        if (memcmp(g_a, g_b, sizeof *g_a)) { bad++;
            fprintf(stderr, "  %s: state differs (p_38 %#x/%#x free_nodes %d/%d)\n",
                    k == 0 ? "Engine_ResetNodes" : "Engine_ResetRings",
                    (unsigned)g_a->stage_ctx[0].p_38, (unsigned)g_b->stage_ctx[0].p_38,
                    (int)g_a->free_nodes, (int)g_b->free_nodes); }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "engine lifecycle", n - bad, n);
    return bad != 0;
}

/* Engine_RunControl, over every command letter at every stage.
 *
 * Two things here the corpus cannot reach.  The attenuation table is a step
 * function of 256 values and the corpus uses three of them, so all 256 are
 * swept against the engine itself -- which is better evidence than the
 * English decompilation had for the same table, since that one was checked
 * against the expression rather than against the code.  And most commands
 * act in exactly one of the five stages, so running each letter at all five
 * says the stage test is right and not just present.
 *
 * Each side gets its own SAPI object, compared separately: it holds no
 * pointers into itself, so a plain memcmp does for it. */
static int unit_control(void)
{
    typedef uint8_t(TV_THISCALL * run_t)(Engine *);
    typedef Node *(TV_THISCALL * append_t)(Engine *, int32_t, int32_t);
    static const char LETTERS[] = "ACINPVacfgilprstvxZ";
    static const int32_t ARGS[] = {0, 1, 2, 9, 15, 16, 21, 22, 100, 255};
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    uint8_t *sap[2];
    int li, si, ai, which, bad = 0, n = 0;

    for (which = 0; which < 2; which++)
        sap[which] = (uint8_t *)VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT,
                                             PAGE_READWRITE);

    for (li = 0; li < (int)(sizeof LETTERS) - 1; li++)
        for (si = 0; si < 5; si++)
            for (ai = 0; ai < (int)(sizeof ARGS / sizeof ARGS[0]); ai++) {
                uint8_t ra, rb;
                /* ESC[V is clamped to 0..9 by the escape parser, and the
                 * handler indexes a 2800-byte-per-voice table with it --
                 * feeding it 255 walks off the end of the image in both
                 * engines, which crashes rather than tests. */
                if (LETTERS[li] == 'V' && ARGS[ai] > 9)
                    continue;
                /* 'i' reaches the audio queue only when notify is set, and
                 * the queue is the host's; leave notify zero and it stays
                 * inside the engine. */
                for (which = 0; which < 2; which++) {
                    Engine *g = which ? g_b : g_a;
                    Node *node;
                    fresh(g, shared);
                    memset(sap[which], 0, 0x1000);
                    g->sapi = (SapiCentral *)sap[which];
                    g->stage = &g->stage_ctx[si];
                    node = ORIG(append_t, 0x10008b60)(g, 0, LETTERS[li]);
                    node->arg = (uint32_t)ARGS[ai];
                    node->b15 = (uint8_t)(ARGS[ai] ^ 0x5a);
                    node->notify = 0;
                    g->stage->ctl = node;
                }
                ra = ORIG(run_t, 0x1000ead0)(g_a);
                rb = Engine_RunControl(g_b);
                /* the two SAPI objects are at different addresses and
                 * normalize only rewrites pointers into the engine */
                g_a->sapi = g_b->sapi = NULL;
                normalize(g_a);
                normalize(g_b);
                n++;
                if (ra != rb || memcmp(g_a, g_b, sizeof *g_a) ||
                    memcmp(sap[0], sap[1], 0x1000)) {
                    if (bad++ < 10)
                        fprintf(stderr, "  RunControl['%c' stage=%d arg=%d]: "
                                        "returned %d/%d, vol %u/%u\n",
                                LETTERS[li], si, (int)ARGS[ai], ra, rb,
                                (unsigned)g_a->cur_volume, (unsigned)g_b->cur_volume);
                }
            }

    /* every step of the attenuation table */
    for (ai = 0; ai < 256; ai++) {
        uint8_t ra, rb;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            Node *node;
            fresh(g, shared);
            memset(sap[which], 0, 0x1000);
            g->sapi = (SapiCentral *)sap[which];
            g->stage = &g->stage_ctx[3];
            node = ORIG(append_t, 0x10008b60)(g, 0, 'a');
            node->arg = (uint32_t)ai;
            node->b15 = 0;
            g->stage->ctl = node;
        }
        ra = ORIG(run_t, 0x1000ead0)(g_a);
        rb = Engine_RunControl(g_b);
        g_a->sapi = g_b->sapi = NULL;
        normalize(g_a);
        normalize(g_b);
        n++;
        if (ra != rb || g_a->cur_volume != g_b->cur_volume ||
            memcmp(g_a, g_b, sizeof *g_a) || memcmp(sap[0], sap[1], 0x1000)) {
            if (bad++ < 10)
                fprintf(stderr, "  RunControl['a' atten=%d]: volume %u/%u mute %d/%d\n",
                        ai, (unsigned)g_a->cur_volume, (unsigned)g_b->cur_volume,
                        g_a->mute, g_b->mute);
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "Engine_RunControl", n - bad, n);
    return bad != 0;
}

/* Stage4_Run over a window built by hand.
 *
 * The loop condition is the point.  English stops before the last node of
 * the window; this engine processes it and leaves on the null that
 * Engine_StageNext returns afterwards.  Substituting English's condition
 * passes the whole corpus, so the only way to hold that line is to set the
 * window's last pointer directly and see what each version does with it.
 *
 * The parameter tracks come along for the ride: a 256-byte ring, a shape
 * curve and a fill, which are pure enough to compare buffer against buffer. */
static int unit_stage4(void)
{
    typedef int32_t(TV_THISCALL * run_t)(Engine *);
    typedef Node *(TV_THISCALL * append_t)(Engine *, int32_t, int32_t);
    typedef void(TV_STDCALL * fill_t)(uint8_t *, int32_t, int32_t, uint8_t);
    typedef void(TV_THISCALL * blend_t)(Engine *, uint8_t *, int32_t, int32_t,
                                        int32_t, uint8_t);
    typedef int32_t(TV_STDCALL * q15_t)(int32_t, int32_t);
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    /* how many nodes in the window, which one `last` points at, node type,
     * and how many phonemes the synthesiser has left to take */
    static const struct { int count, last_at, type, trk34; } CASES[] = {
        {1, 0, 4, 9}, {2, 1, 4, 9}, {4, 3, 4, 9}, {4, 2, 4, 9}, {4, 0, 4, 9},
        {4, 3, 4, 0}, {4, 3, 4, 2}, {3, 2, 0, 9}, {3, 1, 0, 9},
        {5, 4, 1, 9}, {5, 2, 1, 9},
    };
    int k, i, bad = 0, n = 0;

    for (k = 0; k < (int)(sizeof CASES / sizeof CASES[0]); k++) {
        int32_t ra, rb;
        int which;
        for (which = 0; which < 2; which++) {
            Engine *g = which ? g_b : g_a;
            Node *first = NULL, *node = NULL, *lastn = NULL;
            StageCtx *st;
            fresh(g, shared);
            for (i = 0; i < CASES[k].count; i++) {
                node = ORIG(append_t, 0x10008b60)(g, CASES[k].type, 'a' + i);
                if (i == 0)
                    first = node;
                if (i == CASES[k].last_at)
                    lastn = node;
            }
            st = &g->stage_ctx[4];
            st->first = first;
            st->cur = first;
            st->ctl = first;
            st->scan = first;
            st->last = lastn;
            st->type_mask = 0x3f;
            g->trk_34 = CASES[k].trk34;
            g->stage = NULL;
        }
        ra = ORIG(run_t, 0x10004820)(g_a);
        rb = Stage4_Run(g_b);
        normalize(g_a);
        normalize(g_b);
        n++;
        if (ra != rb || memcmp(g_a, g_b, sizeof *g_a)) {
            bad++;
            fprintf(stderr, "  Stage4_Run[%d nodes, last=%d, type=%d, trk34=%d]: "
                            "returned %d/%d trk34 %d/%d\n",
                    CASES[k].count, CASES[k].last_at, CASES[k].type, CASES[k].trk34,
                    ra, rb, (int)g_a->trk_34, (int)g_b->trk_34);
        }
    }

    /* the track writers, buffer against buffer */
    {
        uint8_t ba[256], bb[256];
        int pos, cnt, shape;
        for (i = 0; i < 256; i++)
            ba[i] = bb[i] = (uint8_t)(i * 7 + 3);
        for (pos = -3; pos < 300; pos += 37)
            for (cnt = 0; cnt < 40; cnt += 7) {
                ORIG(fill_t, 0x10017d90)(ba, pos, cnt, 0xa5);
                Track_Fill(bb, pos, cnt, 0xa5);
                n++;
                if (memcmp(ba, bb, 256)) { bad++;
                    fprintf(stderr, "  Track_Fill(pos=%d, n=%d)\n", pos, cnt); }
                for (shape = 0; shape < 8; shape++) {
                    ORIG(blend_t, 0x1000b060)(g_a, ba, pos, shape, cnt, 0x40);
                    Track_BlendFwd(g_b, bb, pos, shape, cnt, 0x40);
                    n++;
                    if (memcmp(ba, bb, 256)) { bad++;
                        fprintf(stderr, "  Track_BlendFwd(pos=%d, shape=%d, n=%d)\n",
                                pos, shape, cnt); }
                    ORIG(blend_t, 0x1000afe0)(g_a, ba, pos, shape, cnt, 0xc0);
                    Track_BlendBack(g_b, bb, pos, shape, cnt, 0xc0);
                    n++;
                    if (memcmp(ba, bb, 256)) { bad++;
                        fprintf(stderr, "  Track_BlendBack(pos=%d, shape=%d, n=%d)\n",
                                pos, shape, cnt); }
                }
            }
    }

    /* and the Q15 multiply both blends run through */
    {
        static const int32_t V[] = {-0x8000, -0x7fff, -256, -1, 0, 1, 255, 0x7ffe,
                                    0x7fff, 0x8000, 0x10000};
        int x, y;
        for (x = 0; x < (int)(sizeof V / sizeof V[0]); x++)
            for (y = 0; y < (int)(sizeof V / sizeof V[0]); y++) {
                int32_t ra = ORIG(q15_t, 0x1000af00)(V[x], V[y]);
                int32_t rb = Synth_MulQ15(V[x], V[y]);
                n++;
                if (ra != rb) { bad++;
                    fprintf(stderr, "  Synth_MulQ15(%d, %d): %d/%d\n",
                            V[x], V[y], ra, rb); }
            }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "stage 4 and tracks", n - bad, n);
    return bad != 0;
}

/* Stage2_Scan takes five parameters and the corpus can only graze them.
 * Build a window of nodes with assorted types and stress bits, point stage 2
 * at it, and sweep: both directions, several counts, positive and negative
 * masks, and all three modes. */
static int unit_scan(void)
{
    typedef uint8_t(TV_THISCALL * scan_t)(Engine *, int32_t, int32_t, int32_t,
                                          int32_t, int32_t);
    typedef Node *(TV_THISCALL * append_t)(Engine *, int32_t, int32_t);
    static const int32_t MASKS[] = {-2, -1, 0, 1, 2, 0x101, -0x101, 0x8001};
    static const int32_t COUNTS[] = {-1, 0, 1, 2, 5, 20};
    /* node type and the two stress bits at 0x08 and 0x10 */
    static const struct { int type; uint32_t stress; } SHAPE[] = {
        {1, 0}, {3, 0x18}, {1, 0x10}, {4, 8}, {3, 0}, {1, 0x18}, {2, 0x10}, {3, 0x08},
    };
    uint8_t *shared = (uint8_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE);
    int dir, ci, m1, m2, mode, i, bad = 0, n = 0;

    for (dir = 0; dir < 2; dir++)
        for (ci = 0; ci < (int)(sizeof COUNTS / sizeof COUNTS[0]); ci++)
            for (m1 = 0; m1 < (int)(sizeof MASKS / sizeof MASKS[0]); m1++)
                for (m2 = 0; m2 < (int)(sizeof MASKS / sizeof MASKS[0]); m2++)
                    for (mode = 0; mode < 3; mode++) {
                        uint8_t ra, rb;
                        int which;
                        for (which = 0; which < 2; which++) {
                            Engine *g = which ? g_b : g_a;
                            Node *first = NULL, *node = NULL, *mid = NULL;
                            StageCtx *st;
                            fresh(g, shared);
                            for (i = 0; i < 8; i++) {
                                node = ORIG(append_t, 0x10008b60)(
                                    g, SHAPE[i].type, 'a' + i);
                                node->flags |= SHAPE[i].stress;
                                if (i == 0)
                                    first = node;
                                if (i == 4)
                                    mid = node;
                            }
                            st = &g->stage_ctx[2];
                            st->first = first;
                            st->cur = first;
                            st->ctl = mid;      /* start in the middle */
                            st->scan = first;
                            st->last = node;
                            st->type_mask = 0x1c;
                            g->stage = st;
                        }
                        ra = ORIG(scan_t, 0x1001aa60)(g_a, dir, COUNTS[ci],
                                                      MASKS[m1], MASKS[m2], mode);
                        rb = Stage2_Scan(g_b, dir, COUNTS[ci], MASKS[m1],
                                         MASKS[m2], mode);
                        normalize(g_a);
                        normalize(g_b);
                        n++;
                        if (ra != rb || memcmp(g_a, g_b, sizeof *g_a)) {
                            if (bad++ < 8)
                                fprintf(stderr, "  Stage2_Scan(dir=%d n=%d m1=%d "
                                                "m2=%d mode=%d): %d/%d\n",
                                        dir, (int)COUNTS[ci], (int)MASKS[m1],
                                        (int)MASKS[m2], mode, ra, rb);
                        }
                    }

    fprintf(stderr, "%-18s %d/%d identical\n", "Stage2_Scan", n - bad, n);
    return bad != 0;
}

/* ---- the parameter setters ------------------------------------------------
 *
 * Each of these is a handful of stores, so what is worth testing is whether
 * they store into exactly the right fields.  Both engines are poisoned with
 * 0x5a first and the whole object is compared afterwards: a field written
 * that should not be shows up as a difference, and a field the original
 * writes that we miss shows up as our poison against its value.  No
 * constructed engine is needed because none of the four reads anything.
 */
typedef void(TV_THISCALL * set_fn)(Engine *, uint32_t);

static int cmp_set(const char *what, uint32_t va, set_fn ours,
                   const uint32_t *args, int nargs)
{
    set_fn orig = (set_fn)(uintptr_t)va;
    int i, bad = 0;

    for (i = 0; i < nargs; i++) {
        memset(g_a, 0x5a, sizeof *g_a);
        memset(g_b, 0x5a, sizeof *g_b);
        orig(g_a, args[i]);
        ours(g_b, args[i]);
        normalize(g_a);
        normalize(g_b);
        if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
            if (bad < 6)
                fprintf(stderr, "  %s(%#x): differs\n", what, (unsigned)args[i]);
            bad++;
        }
    }
    fprintf(stderr, "%-18s %d/%d identical\n", what, nargs - bad, nargs);
    return bad != 0;
}

/* The attenuation the original computes for one volume.  Only meaningful at
 * 0x50 and above; below that it mutes and leaves the field alone. */
static int32_t orig_atten(uint32_t vol)
{
    memset(g_a, 0, sizeof *g_a);
    ((set_fn)(uintptr_t)0x10008aa0)(g_a, vol);
    return g_a->volume_atten;
}

/* Engine_SetVolume is the one function in es/ whose value is not transcribed
 * from the original's instructions.  The original computes
 *
 *     log10(vol * (1/65535)) * -10.0
 *
 * in x87 and truncates; params.c reuses the table of step boundaries that
 * src/engine/volume.c built for the English engine, which computes the same
 * expression a different way.  So the boundaries are exactly what has to be
 * checked, and listing them here would only be asking our table whether it
 * agrees with itself.
 *
 * Instead the steps are found in the original by bisection -- the curve is
 * non-increasing, so for each attenuation there is a largest volume that
 * still reaches it -- and ours is then checked on both sides of every one.
 * That is 64 boundaries, and a step that moved by a single volume would land
 * on one of these probes.
 */
static int volume_args(uint32_t *out, int max)
{
    int32_t k;
    int n = 0, i;

    for (i = 0; i < 0x2000 && n < max; i++)          /* the whole audible low end */
        out[n++] = (uint32_t)i;
    for (k = 15; k >= -48 && n + 4 < max; k--) {
        uint32_t lo = 0x50, hi = 0xffffffffu, b;
        if (orig_atten(lo) < k)
            continue;                                 /* no volume reaches it */
        if (orig_atten(hi) >= k) {
            b = hi;
        } else {
            while (lo < hi) {                         /* largest v with atten >= k */
                uint32_t mid = lo + (hi - lo + 1) / 2;
                if (orig_atten(mid) >= k)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            b = lo;
        }
        out[n++] = b - 1;
        out[n++] = b;
        if (b != 0xffffffffu)
            out[n++] = b + 1;
    }
    return n;
}

static int unit_params(void)
{
    typedef void(TV_CDECL * trace_fn)(void *, const char *, ...);
    typedef void(TV_THISCALL * err_fn)(Engine *, int32_t);
    static const uint32_t PITCH[] = {
        0, 1, 2, 25, 42, 50, 84, 85, 100, 200, 400, 0x7fffffffu,
        0x80000000u, 0xffffffffu,
    };
    static const uint32_t VOICE[] = {
        0, 1, 5, 8, 9, 10, 11, 0x7fffffffu, 0x80000000u, 0xffffffffu,
    };
    /* every wpm the parameter accepts, and the two faults either side of it:
     * below 46 the shift wraps unsigned, above 254 the index runs off the
     * 26-row table.  The setter only stores, so both are safe to call. */
    uint32_t speed[416];
    uint32_t *vol;
    int i, n, bad = 0;

    if (alloc_engines())
        return 2;

    for (i = 0; i < 400; i++)
        speed[i] = (uint32_t)(i - 8);
    speed[400] = 0;
    speed[401] = 0x7fffffffu;
    speed[402] = 0x80000000u;
    speed[403] = 0xffffffffu;
    speed[404] = 46;
    speed[405] = 45;
    speed[406] = 254;
    speed[407] = 255;

    bad |= cmp_set("Engine_SetPitch", 0x10008a40, (set_fn)Engine_SetPitch,
                   PITCH, (int)(sizeof PITCH / sizeof PITCH[0]));
    bad |= cmp_set("Engine_SetSpeed", 0x10008a70, (set_fn)Engine_SetSpeed,
                   speed, 408);
    bad |= cmp_set("Engine_SetVoice", 0x10008b10, (set_fn)Engine_SetVoice,
                   VOICE, (int)(sizeof VOICE / sizeof VOICE[0]));

    vol = (uint32_t *)malloc(0x2200 * sizeof *vol);
    if (vol == NULL) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    n = volume_args(vol, 0x2200);
    bad |= cmp_set("Engine_SetVolume", 0x10008aa0, Engine_SetVolume, vol, n);
    free(vol);

    /* Both diagnostics are a bare return in the shipping build.  Calling
     * them has to leave the engine exactly as it was, which is the only
     * thing there is to check. */
    memset(g_a, 0x5a, sizeof *g_a);
    memset(g_b, 0x5a, sizeof *g_b);
    ORIG(trace_fn, 0x10008b40)(g_a, "ParL[P_F0]= %d", 1);
    Engine_Trace(g_b, "ParL[P_F0]= %d", 1);
    ORIG(err_fn, 0x10008b50)(g_a, 0x65);
    Engine_Error(g_b, 0x65);
    if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
        fprintf(stderr, "  the stubbed diagnostics are not inert\n");
        bad = 1;
    }
    fprintf(stderr, "%-18s %d/%d identical\n", "Engine_Trace", 2, 2);
    return bad != 0;
}

/* Every volume the original accepts, 0x50..0xffffffff.  Only the fields the
 * setter writes are compared rather than the whole object -- unit_params is
 * what tests the field set, and at four billion cases the memcmp would be
 * the entire cost.  Kept out of "all" because it takes minutes; run it on
 * its own with -U volumefull.  docs/SPANISH.md records the result. */
static int unit_volume_full(void)
{
    set_fn orig = (set_fn)(uintptr_t)0x10008aa0;
    unsigned v, n = 0, bad = 0;

    if (alloc_engines())
        return 2;
    memset(g_a, 0, sizeof *g_a);
    memset(g_b, 0, sizeof *g_b);
    for (v = 0x50;; v++) {
        orig(g_a, v);
        Engine_SetVolume(g_b, v);
        n++;
        if (g_a->volume_atten != g_b->volume_atten || g_a->mute != g_b->mute) {
            if (bad < 8)
                fprintf(stderr, "  Engine_SetVolume(%#x): orig %d, ours %d\n",
                        v, (int)g_a->volume_atten, (int)g_b->volume_atten);
            bad++;
        }
        if (v == 0xffffffffu)
            break;
    }
    fprintf(stderr, "%-18s %u/%u identical\n", "SetVolume sweep", n - bad, n);
    return bad != 0;
}

/* ---- the reset chain ------------------------------------------------------
 *
 * These are almost all stores, so the same poison-and-compare that tests the
 * setters tests them, and it tests rather more: a reset names fields by
 * writing them, so a whole-object comparison after one is a direct check on
 * the part of es/engine.fields that reset touches.  Output_Reset in
 * particular is the witness that there is no o_20e8 in this engine -- if
 * there were, the original would clear it and we would not.
 */
typedef void(TV_THISCALL * reset_fn)(Engine *);

static int cmp_reset(const char *what, uint32_t va, reset_fn ours)
{
    reset_fn orig = (reset_fn)(uintptr_t)va;

    memset(g_a, 0x5a, sizeof *g_a);
    memset(g_b, 0x5a, sizeof *g_b);
    orig(g_a);
    ours(g_b);
    normalize(g_a);
    normalize(g_b);
    if (memcmp(g_a, g_b, sizeof *g_a) == 0) {
        fprintf(stderr, "%-18s 1/1 identical\n", what);
        return 0;
    }
    {
        const uint8_t *p = (const uint8_t *)g_a, *q = (const uint8_t *)g_b;
        size_t off, shown = 0;
        for (off = 0; off < sizeof *g_a && shown < 8; off++)
            if (p[off] != q[off]) {
                size_t start = off;
                while (off < sizeof *g_a && p[off] != q[off])
                    off++;
                fprintf(stderr, "  %s differs at 0x%04x..0x%04x\n",
                        what, (unsigned)start, (unsigned)off);
                shown++;
            }
    }
    fprintf(stderr, "%-18s 0/1 identical\n", what);
    return 1;
}

static int unit_reset(void)
{
    typedef void(TV_THISCALL * out_fn)(Engine *, int16_t);
    /* the rates the engine is ever given, the two that divide exactly, and
     * the signs either side of the 16-bit divide */
    static const int16_t RATE[] = {
        0, 1, 99, 100, 101, 8000, 11025, 22050, 32000, 32767, -1, -100,
        -32768,
    };
    out_fn orig = (out_fn)(uintptr_t)0x1000ad50;
    int i, bad = 0, n = 0;

    if (alloc_engines())
        return 2;

    bad |= cmp_reset("Preformat_Reset", 0x10007790, Preformat_Reset);
    bad |= cmp_reset("Prosody_Reset", 0x1000f070, Prosody_Reset);
    bad |= cmp_reset("Stage1_Reset", 0x1000fa10, Stage1_Reset);
    bad |= cmp_reset("Stage2_Reset", 0x1001a8b0, Stage2_Reset);
    bad |= cmp_reset("Synth_ResetTracks", 0x10017850, Synth_ResetTracks);
    bad |= cmp_reset("Synth_InitFilters", 0x1000e6f0, Synth_InitFilters);
    /* the poisoned object takes the not-8000 branch, so run it again
     * with the rate the phone-quality output uses -- that is the one
     * whose coefficients differ from English */
    memset(g_a, 0x5a, sizeof *g_a);
    memset(g_b, 0x5a, sizeof *g_b);
    g_a->sample_rate = g_b->sample_rate = 8000;
    ((reset_fn)(uintptr_t)0x1000e6f0)(g_a);
    Synth_InitFilters(g_b);
    normalize(g_a);
    normalize(g_b);
    if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
        fprintf(stderr, "  Synth_InitFilters at 8 kHz differs\n");
        bad = 1;
    }
    fprintf(stderr, "%-18s 1/1 identical\n", "InitFilters 8k");

    for (i = 0; i < (int)(sizeof RATE / sizeof RATE[0]); i++) {
        memset(g_a, 0x5a, sizeof *g_a);
        memset(g_b, 0x5a, sizeof *g_b);
        orig(g_a, RATE[i]);
        Output_Reset(g_b, RATE[i]);
        normalize(g_a);
        normalize(g_b);
        n++;
        if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
            if (bad < 6)
                fprintf(stderr, "  Output_Reset(%d): differs\n", (int)RATE[i]);
            bad++;
        }
    }
    fprintf(stderr, "%-18s %d/%d identical\n", "Output_Reset", n - bad, n);
    return bad != 0;
}

/* ---- the rule interpreter's helpers ----------------------------------------
 *
 * None of these needs an engine: they work on a TextIn and a Token, both of
 * which can be built by hand.  That is the point of writing them before the
 * dispatcher -- each is a function with a small closed input domain, so it
 * can be settled on its own while the arms that call it are still the
 * original's.
 *
 * Both sides get one contiguous block holding the TextIn, the tokens and the
 * bytecode, so that every pointer either side stores is inside its own block
 * and can be rewritten as an offset before the comparison.  TextIn_Error is
 * the one function here the corpus never reaches at all: no input the
 * differential test carries makes the tokenizer give up.
 */
typedef struct {
    TextIn ti;
    Token tok[5];
    int16_t code[16];
    char txt[64];              /* a writable Token.text, which the number
                                * formatter needs because it truncates it */
    uint8_t rec[64];           /* the record Rule_SayRecord reads through
                                * Token.d18, built by offset rather than
                                * through the struct so the test does not
                                * depend on the struct being right */
} rblock;

static void norm_block(void *p, size_t n)
{
    uintptr_t base = (uintptr_t)p;
    uint32_t *w = (uint32_t *)p;
    size_t i;
    for (i = 0; i < n / 4; i++)
        if ((uintptr_t)w[i] >= base && (uintptr_t)w[i] < base + n)
            w[i] = (uint32_t)((uintptr_t)w[i] - base);
}

/* head <-> tok[1] <-> tok[2] <-> tok[3], with tok[0] standing in for the
 * head node so that detaching it exercises the refusal. */
static void rb_list(rblock *b)
{
    int i;
    memset(b, 0x5a, sizeof *b);
    memset(&b->ti, 0, sizeof b->ti);
    for (i = 0; i < 5; i++)
        memset(&b->tok[i], 0, sizeof b->tok[i]);
    for (i = 0; i < 4; i++) {
        b->tok[i].prev = i ? &b->tok[i - 1] : NULL;
        b->tok[i].next = i < 3 ? &b->tok[i + 1] : NULL;
        b->tok[i].trail = (uint8_t)('a' + i);
    }
    b->ti.head = &b->tok[0];
    b->ti.tail = &b->tok[3];
    b->ti.count = 3;
    for (i = 0; i < 16; i++)
        b->code[i] = (int16_t)(0x4100 + i);
    b->ti.rule_ip = b->code;
}

/* A five-token list with the three flags Rule_Scan cares about placed by
 * mask: 0x51 makes a token transparent, 0x54 makes it a wall, and 0x4a is
 * the flag the caller is looking for.  tok[0] stands in for the head node,
 * so a backward walk runs into the boundary the function refuses to cross. */
static void rb_scan(rblock *b, unsigned m51, unsigned m54, unsigned m4a)
{
    int i;
    memset(b, 0x5a, sizeof *b);
    memset(&b->ti, 0, sizeof b->ti);
    for (i = 0; i < 5; i++) {
        memset(&b->tok[i], 0, sizeof b->tok[i]);
        b->tok[i].prev = i ? &b->tok[i - 1] : NULL;
        b->tok[i].next = i < 4 ? &b->tok[i + 1] : NULL;
        if (i && (m51 >> (i - 1)) & 1)
            Bits_Set(0x51, b->tok[i].bits);
        if (i && (m54 >> (i - 1)) & 1)
            Bits_Set(0x54, b->tok[i].bits);
        if (i && (m4a >> (i - 1)) & 1)
            Bits_Set(0x4a, b->tok[i].bits);
    }
    b->ti.head = &b->tok[0];
    b->ti.tail = &b->tok[4];
    b->ti.count = 4;
    for (i = 0; i < 16; i++)
        b->code[i] = (int16_t)(0x4100 + i);
    b->ti.rule_ip = b->code;
}

/* The inserts allocate from the DLL's heap, so the two sides end up holding
 * different addresses and the block comparison the other cases use says
 * nothing.  Compare contents instead: a token's body with the three string
 * pointers taken as strings, and the list as a walk from the head that
 * records, for each node, whether it is one of the block's and which. */
static int tok_same(const Token *a, const Token *b)
{
    const char *pa = (const char *)a, *pb = (const char *)b;

    if (memcmp(pa + 8, pb + 8, 0x18 - 8) != 0)          /* w08, w0a, bits */
        return 0;
    if (memcmp(pa + 0x1c, pb + 0x1c, 4) != 0)           /* d1c */
        return 0;
    /* d18 points at a record each side builds in its own block, so the
     * addresses differ by construction; the records themselves are compared
     * where they are built. */
    if ((a->d18 == NULL) != (b->d18 == NULL))
        return 0;
    if (memcmp(pa + 0x24, pb + 0x24, 4) != 0)           /* len */
        return 0;
    if (memcmp(pa + 0x30, pb + 0x30, 0x3c - 0x30) != 0) /* trail..num */
        return 0;
    if ((a->text == NULL) != (b->text == NULL))
        return 0;
    if (a->text != NULL && strcmp(a->text, b->text) != 0)
        return 0;
    if ((a->text2 == NULL) != (b->text2 == NULL))
        return 0;
    if (a->text2 != NULL && strcmp(a->text2, b->text2) != 0)
        return 0;
    return (a->types == NULL) == (b->types == NULL);
}

static int list_same(rblock *a, rblock *b, int max)
{
    Token *pa = a->ti.head, *pb = b->ti.head;
    int i;

    if (a->ti.count != b->ti.count)
        return 0;
    for (i = 0; i < max; i++) {
        int ina, inb;
        if ((pa == NULL) != (pb == NULL))
            return 0;
        if (pa == NULL)
            return 1;
        ina = (char *)pa >= (char *)a && (char *)pa < (char *)(a + 1);
        inb = (char *)pb >= (char *)b && (char *)pb < (char *)(b + 1);
        if (ina != inb)
            return 0;
        if (ina && (char *)pa - (char *)a != (char *)pb - (char *)b)
            return 0;
        if (!tok_same(pa, pb))
            return 0;
        pa = pa->next;
        pb = pb->next;
    }
    return 1;
}

/* Rule_SayGroupedNumber removes the tokens it has consumed, and
 * TextIn_RemoveToken frees what it takes out -- which rules out the block's
 * own tokens.  This builds a head sentinel from the block followed by heap
 * tokens, so the removal path can actually run.  The texts still come from
 * the block because the formatter truncates them. */
static Token *rb_group(rblock *b, const char *const *texts,
                       const uint8_t *trails, const uint8_t *flags, int n)
{
    Token *prev, *t = NULL;
    int i;

    memset(b, 0x5a, sizeof *b);
    memset(&b->ti, 0, sizeof b->ti);
    memset(&b->tok[0], 0, sizeof b->tok[0]);
    b->ti.head = &b->tok[0];
    prev = &b->tok[0];
    for (i = 0; i < n; i++) {
        t = (Token *)tv_malloc(sizeof(Token));
        memset(t, 0, sizeof *t);
        t->prev = prev;
        prev->next = t;
        /* the text has to come from the DLL's heap as well: the removal
         * path frees a token's strings along with the token */
        t->text = (char *)tv_malloc(strlen(texts[i]) + 1);
        strcpy(t->text, texts[i]);
        t->len = (int16_t)strlen(texts[i]);
        t->trail = trails[i];
        if (flags[i] & 1)
            Bits_Set(0x14, t->bits);
        if (flags[i] & 2)
            Bits_Set(0xf, t->bits);
        if (flags[i] & 4)
            Bits_Set(0x4a, t->bits);
        t->num = i + 1;
        prev = t;
    }
    b->ti.tail = prev;
    b->ti.count = n;
    return b->tok[0].next;
}

/* How far along the list a token sits, or -1.  The two sides hold different
 * addresses, so position is the only thing worth comparing. */
static int list_index(rblock *b, const Token *t)
{
    Token *p = b->ti.head;
    int i;

    for (i = 0; p != NULL && i < 16; i++, p = p->next)
        if (p == t)
            return i;
    return -1;
}

static int unit_rule(void)
{
    typedef int32_t(TV_STDCALL * bits_t)(const uint32_t *, Token *, int32_t);
    typedef int32_t(TV_THISCALL * set_t)(TextIn *, Token *, uint32_t);
    typedef int32_t(TV_THISCALL * match_t)(TextIn *, Token *);
    typedef Token *(TV_THISCALL * det_t)(TextIn *, Token *);
    typedef int32_t(TV_THISCALL * err_t)(TextIn *, int32_t);
    static const uint32_t PAT3[] = {
        0u, 1u, 3u, 0x80000000u, 0xffffffffu, 0xa5a5a5a5u,
    };
    static rblock A, B;
    int i, j, k, bad = 0, n = 0;

    /* TextIn_Error: every count either side of the ring's ten slots. */
    for (i = 0; i <= 12; i++) {
        int32_t ra, rb;
        memset(&A, 0x5a, sizeof A);
        memset(&B, 0x5a, sizeof B);
        A.ti.err_count = B.ti.err_count = i;
        ra = ORIG(err_t, 0x1001dce0)(&A.ti, 0x1234 + i);
        rb = TextIn_Error(&B.ti, 0x1234 + i);
        n++;
        if (ra != rb || memcmp(&A, &B, sizeof A) != 0) {
            if (bad++ < 8)
                fprintf(stderr, "  TextIn_Error(count=%d): %d/%d\n", i, ra, rb);
        }
    }

    /* Rule_TestBits: every word gets to be the one that decides, against
     * both modes. */
    for (k = 0; k < 2; k++)
        for (i = 0; i < 6; i++)
            for (j = 0; j < 6; j++) {
                uint32_t want[3];
                Token t;
                int32_t ra, rb, w;
                for (w = 0; w < 3; w++) {
                    want[w] = PAT3[i];
                    t.bits[w] = PAT3[j];
                }
                ra = ORIG(bits_t, 0x10021630)(want, &t, k);
                rb = Rule_TestBits(want, &t, k);
                n++;
                if (ra != rb) {
                    if (bad++ < 8)
                        fprintf(stderr, "  Rule_TestBits(%#x, %#x, %d): %d/%d\n",
                                PAT3[i], PAT3[j], k, ra, rb);
                }
            }

    /* Rule_SetTrail and Rule_MatchTrail: the operand byte against every
     * trailing character, including the ones above 0x7f that the one
     * sign-extends and the other does not. */
    for (i = 0; i < 256; i += 5)
        for (j = 0; j < 256; j += 7) {
            int32_t ra, rb;
            rb_list(&A);
            rb_list(&B);
            A.code[0] = B.code[0] = (int16_t)i;
            A.tok[1].trail = B.tok[1].trail = (uint8_t)j;
            ra = ORIG(set_t, 0x10021600)(&A.ti, &A.tok[1], 0xdeadbeefu);
            rb = Rule_SetTrail(&B.ti, &B.tok[1], 0xdeadbeefu);
            norm_block(&A, sizeof A);
            norm_block(&B, sizeof B);
            n++;
            if (ra != rb || memcmp(&A, &B, sizeof A) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_SetTrail(op=%d, trail=%d): %d/%d\n",
                            i, j, ra, rb);
            }

            rb_list(&A);
            rb_list(&B);
            A.code[0] = B.code[0] = (int16_t)i;
            A.tok[1].trail = B.tok[1].trail = (uint8_t)j;
            ra = ORIG(match_t, 0x100215d0)(&A.ti, &A.tok[1]);
            rb = Rule_MatchTrail(&B.ti, &B.tok[1]);
            norm_block(&A, sizeof A);
            norm_block(&B, sizeof B);
            n++;
            if (ra != rb || memcmp(&A, &B, sizeof A) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_MatchTrail(op=%d, trail=%d): %d/%d\n",
                            i, j, ra, rb);
            }
        }

    /* TextIn_Detach at every position, plus the head it refuses and the
     * null it tolerates. */
    for (i = -1; i < 4; i++) {
        Token *ra, *rb;
        rb_list(&A);
        rb_list(&B);
        ra = ORIG(det_t, 0x1001d9f0)(&A.ti, i < 0 ? NULL : &A.tok[i]);
        rb = TextIn_Detach(&B.ti, i < 0 ? NULL : &B.tok[i]);
        n++;
        {
            uintptr_t oa = ra ? (uintptr_t)ra - (uintptr_t)&A : 0;
            uintptr_t ob = rb ? (uintptr_t)rb - (uintptr_t)&B : 0;
            norm_block(&A, sizeof A);
            norm_block(&B, sizeof B);
            if (oa != ob || (ra == NULL) != (rb == NULL) ||
                memcmp(&A, &B, sizeof A) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  TextIn_Detach(tok[%d]): %p/%p\n",
                            i, (void *)ra, (void *)rb);
            }
        }
    }

    /* Rule_Scan over every arrangement of its three flags across a
     * five-token list, both directions, one and two passes, and a want set
     * that is either a single flag or empty -- the empty one being the only
     * way the second of its two tests can answer where the first cannot. */
    {
        typedef int32_t(TV_THISCALL * scan_t)(TextIn *, const uint32_t *,
                                              Token *, int32_t, int32_t, int32_t);
        unsigned m51, m54, m4a;
        int dir, cnt, w;
        uint32_t want[2][3];
        memset(want, 0, sizeof want);
        Bits_Set(0x4a, want[0]);
        for (m51 = 0; m51 < 16; m51++)
            for (m54 = 0; m54 < 16; m54++)
                for (m4a = 0; m4a < 16; m4a++)
                    for (w = 0; w < 2; w++)
                        for (dir = 0; dir < 2; dir++)
                            for (cnt = 1; cnt <= 2; cnt++) {
                                int32_t ra, rb, d = dir ? 1 : -1;
                                rb_scan(&A, m51, m54, m4a);
                                rb_scan(&B, m51, m54, m4a);
                                ra = ORIG(scan_t, 0x10021080)(
                                    &A.ti, want[w], &A.tok[2], d, cnt, 1);
                                rb = Rule_Scan(&B.ti, want[w], &B.tok[2],
                                               d, cnt, 1);
                                norm_block(&A, sizeof A);
                                norm_block(&B, sizeof B);
                                n++;
                                if (ra != rb || memcmp(&A, &B, sizeof A) != 0) {
                                    if (bad++ < 8)
                                        fprintf(stderr, "  Rule_Scan(m51=%u m54=%u "
                                                "m4a=%u want=%d dir=%d n=%d): %d/%d\n",
                                                m51, m54, m4a, w, d, cnt, ra, rb);
                                }
                            }
        /* every check value the dispatcher is seen to pass, not just 1:
         * opcodes 44, 45, 51 and 52 pass 2, which makes the call a walk
         * with no result. */
        for (cnt = 0; cnt < 4; cnt++) {
            int32_t ra, rb;
            rb_scan(&A, 5, 0, 0xf);
            rb_scan(&B, 5, 0, 0xf);
            ra = ORIG(scan_t, 0x10021080)(&A.ti, want[0], &A.tok[2], 1, 2, cnt);
            rb = Rule_Scan(&B.ti, want[0], &B.tok[2], 1, 2, cnt);
            norm_block(&A, sizeof A);
            norm_block(&B, sizeof B);
            n++;
            if (ra != rb || memcmp(&A, &B, sizeof A) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_Scan(check=%d): %d/%d\n", cnt, ra, rb);
            }
        }

        /* the arguments the walk rejects outright */
        for (cnt = -1; cnt <= 1; cnt++) {
            int32_t ra, rb;
            rb_scan(&A, 0, 0, 0xf);
            rb_scan(&B, 0, 0, 0xf);
            ra = ORIG(scan_t, 0x10021080)(&A.ti, want[0], NULL, 1, cnt, 1);
            rb = Rule_Scan(&B.ti, want[0], NULL, 1, cnt, 1);
            n++;
            if (ra != rb) { if (bad++ < 8)
                fprintf(stderr, "  Rule_Scan(NULL, n=%d): %d/%d\n", cnt, ra, rb); }

            rb_scan(&A, 0, 0, 0xf);
            rb_scan(&B, 0, 0, 0xf);
            ra = ORIG(scan_t, 0x10021080)(&A.ti, want[0], &A.tok[2], 1, cnt, 0);
            rb = Rule_Scan(&B.ti, want[0], &B.tok[2], 1, cnt, 0);
            norm_block(&A, sizeof A);
            norm_block(&B, sizeof B);
            n++;
            if (ra != rb || memcmp(&A, &B, sizeof A) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_Scan(check=0, n=%d): %d/%d\n",
                            cnt, ra, rb);
            }
        }
    }

    /* TextIn_InsertBefore at every position, including the head it refuses
     * after it has already linked the token in, and the null it refuses
     * before. */
    {
        typedef Token *(TV_THISCALL * ins_t)(TextIn *, Token *);
        for (i = -1; i < 5; i++) {
            Token *ra, *rb;
            rb_scan(&A, 0, 0, 0);
            rb_scan(&B, 0, 0, 0);
            ra = ORIG(ins_t, 0x1001d7c0)(&A.ti, i < 0 ? NULL : &A.tok[i]);
            rb = TextIn_InsertBefore(&B.ti, i < 0 ? NULL : &B.tok[i]);
            n++;
            if ((ra == NULL) != (rb == NULL) ||
                (ra != NULL && !tok_same(ra, rb)) ||
                !list_same(&A, &B, 12)) {
                if (bad++ < 8)
                    fprintf(stderr, "  TextIn_InsertBefore(tok[%d]) differs\n", i);
                continue;
            }
            /* the refused case leaves the token linked in front of ref, so
             * that is where it has to be compared */
            if (ra == NULL && i >= 0 &&
                ((A.tok[i].prev == NULL) != (B.tok[i].prev == NULL) ||
                 (A.tok[i].prev != NULL &&
                  !tok_same(A.tok[i].prev, B.tok[i].prev)))) {
                if (bad++ < 8)
                    fprintf(stderr, "  TextIn_InsertBefore(tok[%d]) left "
                            "different wreckage\n", i);
            }
        }
    }

    /* Rule_InsertWord for every word in the table, both directions. */
    {
        typedef int32_t(TV_THISCALL * ins_t)(TextIn *, Token *, uint32_t, int32_t);
        int idx, dir;
        for (idx = 0; idx < 26; idx++)
            for (dir = 0; dir < 2; dir++) {
                int32_t ra, rb, d = dir ? 1 : -1;
                rb_scan(&A, 0, 0, 0);
                rb_scan(&B, 0, 0, 0);
                A.code[0] = B.code[0] = (int16_t)idx;
                ra = ORIG(ins_t, 0x10021670)(&A.ti, &A.tok[2], 0x1234u, d);
                rb = Rule_InsertWord(&B.ti, &B.tok[2], 0x1234u, d);
                n++;
                if (ra != rb || !list_same(&A, &B, 12) ||
                    A.ti.rule_ip - A.code != B.ti.rule_ip - B.code) {
                    if (bad++ < 8)
                        fprintf(stderr, "  Rule_InsertWord(%d, dir=%d): %d/%d\n",
                                idx, d, ra, rb);
                }
            }
    }

    /* Rule_SpellOut: both modes over text that exercises each arm -- letters
     * and digits, symbols that only the open mode names, the control
     * characters it drops, the high half of cp1252, and a string long enough
     * to run into the hundred-character guard. */
    {
        typedef int32_t(TV_THISCALL * spell_t)(TextIn *, Token *, uint32_t, int32_t);
        static char longstr[200];
        static const char *const TEXTS[] = {
            NULL, "", "A", "AB", "a1!", "Hola, mundo.", "$100.50 %",
            "\x01\x02\x1f\x20", "ma\xf1""ana \xe1\xe9\xed", "  ", "9876543210",
            longstr,
        };
        int ti, all;
        memset(longstr, 'A', sizeof longstr - 1);
        longstr[sizeof longstr - 1] = 0;
        for (ti = 0; ti < (int)(sizeof TEXTS / sizeof TEXTS[0]); ti++)
            for (all = 0; all < 2; all++) {
                int32_t ra, rb;
                rb_scan(&A, 0, 0, 0);
                rb_scan(&B, 0, 0, 0);
                A.tok[2].text = (char *)TEXTS[ti];
                B.tok[2].text = (char *)TEXTS[ti];
                ra = ORIG(spell_t, 0x10022810)(&A.ti, &A.tok[2], 0x777u, all);
                rb = Rule_SpellOut(&B.ti, &B.tok[2], 0x777u, all);
                n++;
                if (ra != rb || !tok_same(&A.tok[2], &B.tok[2]) ||
                    !list_same(&A, &B, 12)) {
                    if (bad++ < 8)
                        fprintf(stderr, "  Rule_SpellOut(%s, all=%d): %d/%d "
                                "-> %s / %s\n",
                                TEXTS[ti] ? TEXTS[ti] : "(null)", all, ra, rb,
                                A.tok[2].text2 ? A.tok[2].text2 : "(null)",
                                B.tok[2].text2 ? B.tok[2].text2 : "(null)");
                }
            }
    }

    /* Number_Words is a pure forward, so the check is that both routes
     * produce the same words for the same request -- and leave the same
     * wreckage in the digits buffer, which above three digits the formatter
     * truncates in place as it recurses.  Each side gets its own writable
     * copy for exactly that reason: a string literal here faults. */
    {
        typedef void(TV_CDECL * nw_t)(char *, char *, int32_t, int32_t);
        static const char *const DIGITS[] = {
            "0", "1", "7", "10", "16", "21", "100", "101", "999", "1000",
            "1000000", "2147483647", "", "007", "1-23", "12-3", "1 23",
            "12a4", "99999999999999999",
        };
        int di, st, md;
        for (di = 0; di < (int)(sizeof DIGITS / sizeof DIGITS[0]); di++)
            for (st = 0; st < 2; st++)
                for (md = 1; md <= 2; md++) {
                    char oa[256], ob[256], da[64], db[64];
                    memset(oa, 0x5a, sizeof oa);
                    memset(ob, 0x5a, sizeof ob);
                    memset(da, 0x5a, sizeof da);
                    memset(db, 0x5a, sizeof db);
                    strcpy(da, DIGITS[di]);
                    strcpy(db, DIGITS[di]);
                    ORIG(nw_t, 0x10020920)(da, oa, st ? 1 : 4, md);
                    Number_Words(db, ob, st ? 1 : 4, md);
                    n++;
                    /* It writes into the digits buffer as it recurses -- a
                     * read-only string faults -- but it puts the buffer back
                     * before it returns.  Callers rely on that, so it is
                     * asserted rather than assumed. */
                    if (memcmp(oa, ob, sizeof oa) != 0 ||
                        memcmp(da, db, sizeof da) != 0 ||
                        strcmp(da, DIGITS[di]) != 0) {
                        if (bad++ < 8)
                            fprintf(stderr, "  Number_Words(%s, %d, %d): %s / %s,"
                                    " digits %s / %s\n",
                                    DIGITS[di], st ? 1 : 4, md, oa, ob, da, db);
                    }
                }
    }

    /* Rule_SayNumber over both steering flags, a trailing character or
     * none, and the numbers either side of every place the formatter
     * changes its mind -- including the negative it refuses. */
    {
        typedef int32_t(TV_THISCALL * say_t)(TextIn *, Token *, uint32_t);
        static const int32_t NUMS[] = {
            -2147483647 - 1, -1, 0, 1, 2, 9, 10, 11, 15, 16, 20, 21, 30, 99,
            100, 101, 199, 200, 999, 1000, 1001, 100000, 1000000, 2147483647,
        };
        int ni, f4a, f31, tr;
        for (ni = 0; ni < (int)(sizeof NUMS / sizeof NUMS[0]); ni++)
            for (f4a = 0; f4a < 2; f4a++)
                for (f31 = 0; f31 < 2; f31++)
                    for (tr = 0; tr < 2; tr++) {
                        int32_t ra, rb;
                        rb_scan(&A, 0, 0, 0);
                        rb_scan(&B, 0, 0, 0);
                        A.tok[2].num = B.tok[2].num = NUMS[ni];
                        A.tok[2].trail = B.tok[2].trail = (uint8_t)(tr ? '.' : 0);
                        if (f4a) {
                            Bits_Set(0x4a, A.tok[2].bits);
                            Bits_Set(0x4a, B.tok[2].bits);
                        }
                        if (f31) {
                            Bits_Set(0x31, A.tok[2].bits);
                            Bits_Set(0x31, B.tok[2].bits);
                        }
                        ra = ORIG(say_t, 0x10021720)(&A.ti, &A.tok[2], 0x99u);
                        rb = Rule_SayNumber(&B.ti, &B.tok[2], 0x99u);
                        n++;
                        if (ra != rb || !tok_same(&A.tok[2], &B.tok[2]) ||
                            !list_same(&A, &B, 12)) {
                            if (bad++ < 8)
                                fprintf(stderr, "  Rule_SayNumber(%d, %d%d%d): "
                                        "%d/%d -> %s / %s\n",
                                        (int)NUMS[ni], f4a, f31, tr, ra, rb,
                                        A.tok[2].text2 ? A.tok[2].text2 : "(null)",
                                        B.tok[2].text2 ? B.tok[2].text2 : "(null)");
                        }
                    }
    }

    /* Rule_SayNumberText: the length guard either side of seventeen, both
     * ways of reaching the formatter's first mode -- the flag on the token
     * and the flag on one of the next two -- and a trailing character that
     * it overwrites where Rule_SayNumber would have left it.  The text goes
     * in the block because the formatter truncates it in place, and
     * tok_same compares it afterwards. */
    {
        typedef int32_t(TV_THISCALL * sayt_t)(TextIn *, Token *, uint32_t);
        static const char *const TEXTS[] = {
            "", "0", "7", "21", "100", "1000", "12345678901234567",
            "123456789012345678", "00000000000000001", "abc",
        };
        int ti, f4a, nb, tr;
        for (ti = 0; ti < (int)(sizeof TEXTS / sizeof TEXTS[0]); ti++)
            for (f4a = 0; f4a < 2; f4a++)
                for (nb = 0; nb < 3; nb++)
                    for (tr = 0; tr < 2; tr++) {
                        int32_t ra, rb;
                        rb_scan(&A, 0, 0, 0);
                        rb_scan(&B, 0, 0, 0);
                        strcpy(A.txt, TEXTS[ti]);
                        strcpy(B.txt, TEXTS[ti]);
                        A.tok[2].text = A.txt;
                        B.tok[2].text = B.txt;
                        A.tok[2].trail = B.tok[2].trail = (uint8_t)(tr ? '.' : 0);
                        if (f4a) {
                            Bits_Set(0x4a, A.tok[2].bits);
                            Bits_Set(0x4a, B.tok[2].bits);
                        }
                        if (nb) {              /* the flag one or two along */
                            Bits_Set(0x4a, A.tok[2 + nb].bits);
                            Bits_Set(0x4a, B.tok[2 + nb].bits);
                        }
                        ra = ORIG(sayt_t, 0x10021820)(&A.ti, &A.tok[2], 0x55u);
                        rb = Rule_SayNumberText(&B.ti, &B.tok[2], 0x55u);
                        n++;
                        if (ra != rb || !tok_same(&A.tok[2], &B.tok[2]) ||
                            strcmp(A.txt, B.txt) != 0 || !list_same(&A, &B, 12)) {
                            if (bad++ < 8)
                                fprintf(stderr, "  Rule_SayNumberText(%s, %d%d%d): "
                                        "%d/%d -> %s / %s\n",
                                        TEXTS[ti], f4a, nb, tr, ra, rb,
                                        A.tok[2].text2 ? A.tok[2].text2 : "(null)",
                                        B.tok[2].text2 ? B.tok[2].text2 : "(null)");
                        }
                    }
        /* and the two it refuses before it looks at anything */
        {
            int32_t ra, rb;
            rb_scan(&A, 0, 0, 0);
            rb_scan(&B, 0, 0, 0);
            A.tok[2].text = B.tok[2].text = NULL;
            ra = ORIG(sayt_t, 0x10021820)(&A.ti, &A.tok[2], 0x55u);
            rb = Rule_SayNumberText(&B.ti, &B.tok[2], 0x55u);
            n++;
            if (ra != rb || !list_same(&A, &B, 12)) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_SayNumberText(no text): %d/%d\n", ra, rb);
            }
            rb_scan(&A, 0, 0, 0);
            rb_scan(&B, 0, 0, 0);
            ra = ORIG(sayt_t, 0x10021820)(&A.ti, NULL, 0x55u);
            rb = Rule_SayNumberText(&B.ti, NULL, 0x55u);
            n++;
            if (ra != rb) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_SayNumberText(NULL): %d/%d\n", ra, rb);
            }
        }
    }

    /* Rule_SayNumberOrSpell: the seven-character fork either side, both
     * routes to the formatter's first mode, and a trailing character it
     * leaves alone where Rule_SayNumberText overwrites one. */
    {
        typedef int32_t(TV_THISCALL * says_t)(TextIn *, Token *, uint32_t);
        static const char *const TEXTS[] = {
            "", "0", "7", "21", "1000", "1234567", "12345678", "123456789",
            "ab cd", "%$&",
        };
        int ti, f4a, nb, tr;
        for (ti = 0; ti < (int)(sizeof TEXTS / sizeof TEXTS[0]); ti++)
            for (f4a = 0; f4a < 2; f4a++)
                for (nb = 0; nb < 3; nb++)
                    for (tr = 0; tr < 2; tr++) {
                        int32_t ra, rb;
                        rb_scan(&A, 0, 0, 0);
                        rb_scan(&B, 0, 0, 0);
                        strcpy(A.txt, TEXTS[ti]);
                        strcpy(B.txt, TEXTS[ti]);
                        A.tok[2].text = A.txt;
                        B.tok[2].text = B.txt;
                        A.tok[2].trail = B.tok[2].trail = (uint8_t)(tr ? '.' : 0);
                        if (f4a) {
                            Bits_Set(0x4a, A.tok[2].bits);
                            Bits_Set(0x4a, B.tok[2].bits);
                        }
                        if (nb) {
                            Bits_Set(0x4a, A.tok[2 + nb].bits);
                            Bits_Set(0x4a, B.tok[2 + nb].bits);
                        }
                        ra = ORIG(says_t, 0x10021aa0)(&A.ti, &A.tok[2], 0x66u);
                        rb = Rule_SayNumberOrSpell(&B.ti, &B.tok[2], 0x66u);
                        n++;
                        if (ra != rb || !tok_same(&A.tok[2], &B.tok[2]) ||
                            strcmp(A.txt, B.txt) != 0 || !list_same(&A, &B, 12)) {
                            if (bad++ < 8)
                                fprintf(stderr, "  Rule_SayNumberOrSpell(%s, %d%d%d):"
                                        " %d/%d -> %s / %s\n",
                                        TEXTS[ti], f4a, nb, tr, ra, rb,
                                        A.tok[2].text2 ? A.tok[2].text2 : "(null)",
                                        B.tok[2].text2 ? B.tok[2].text2 : "(null)");
                        }
                    }
        {
            int32_t ra, rb;
            rb_scan(&A, 0, 0, 0);
            rb_scan(&B, 0, 0, 0);
            A.tok[2].text = B.tok[2].text = NULL;
            ra = ORIG(says_t, 0x10021aa0)(&A.ti, &A.tok[2], 0x66u);
            rb = Rule_SayNumberOrSpell(&B.ti, &B.tok[2], 0x66u);
            n++;
            if (ra != rb || !list_same(&A, &B, 12)) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_SayNumberOrSpell(no text): %d/%d\n",
                            ra, rb);
            }
        }
    }

    /* Word_IsAcronym over every three-letter combination of A..Z, which is
     * the length its rules actually decide, plus shorter and longer ones and
     * the letters its special cases name. */
    {
        typedef int32_t(TV_CDECL * acr_t)(const char *);
        static const char ALPHA[] = "AEHLRSPTCBIOUY";
        char w[8];
        int a, b, c, d;
        for (a = 'A'; a <= 'Z'; a++)
            for (b = 'A'; b <= 'Z'; b++)
                for (c = 'A'; c <= 'Z'; c++) {
                    int32_t ra, rb;
                    w[0] = (char)a; w[1] = (char)b; w[2] = (char)c; w[3] = 0;
                    ra = ORIG(acr_t, 0x10020f00)(w);
                    rb = Word_IsAcronym(w);
                    n++;
                    if (ra != rb) { if (bad++ < 8)
                        fprintf(stderr, "  Word_IsAcronym(%s): %d/%d\n", w, ra, rb); }
                }
        for (a = 0; ALPHA[a]; a++)
            for (b = 0; ALPHA[b]; b++)
                for (c = 0; ALPHA[c]; c++)
                    for (d = 0; ALPHA[d]; d++) {
                        int32_t ra, rb;
                        w[0] = ALPHA[a]; w[1] = ALPHA[b];
                        w[2] = ALPHA[c]; w[3] = ALPHA[d]; w[4] = 0;
                        ra = ORIG(acr_t, 0x10020f00)(w);
                        rb = Word_IsAcronym(w);
                        n++;
                        if (ra != rb) { if (bad++ < 8)
                            fprintf(stderr, "  Word_IsAcronym(%s): %d/%d\n",
                                    w, ra, rb); }
                    }
        {
            static const char *const ODD[] = {
                "", "A", "AB", "sol", "USA", "IBM", "CBS", "BRA", "SPC",
                "ma\xf1""a", "AAAA", "HOLA", "OLAH", "12345", "A1",
            };
            int k;
            for (k = 0; k < (int)(sizeof ODD / sizeof ODD[0]); k++) {
                int32_t ra = ORIG(acr_t, 0x10020f00)(ODD[k]);
                int32_t rb = Word_IsAcronym(ODD[k]);
                n++;
                if (ra != rb) { if (bad++ < 8)
                    fprintf(stderr, "  Word_IsAcronym(%s): %d/%d\n",
                            ODD[k], ra, rb); }
            }
        }
    }

    /* Rule_Acronym: the forcing flag, the softening flag on the token and on
     * each neighbour, the single-character token it leaves alone, and the
     * trailing full stop it turns into a space.  Texts stay well under the
     * 104 bytes the original's lowercase buffer holds -- past that neither
     * side has defined behaviour, so there is nothing to compare. */
    {
        typedef int32_t(TV_THISCALL * acr_t)(TextIn *, Token *, uint32_t);
        static const char *const TEXTS[] = {"IBM", "SOL", "BRA", "AB", "HOLA"};
        static char nprev[] = "SOL", nnext[] = "CBS";
        int ti, f7, f19, p19, x19, tr, one;
        for (ti = 0; ti < (int)(sizeof TEXTS / sizeof TEXTS[0]); ti++)
            for (f7 = 0; f7 < 2; f7++)
                for (f19 = 0; f19 < 2; f19++)
                    for (p19 = 0; p19 < 2; p19++)
                        for (x19 = 0; x19 < 2; x19++)
                            for (tr = 0; tr < 2; tr++)
                                for (one = 0; one < 2; one++) {
                                    int32_t ra, rb;
                                    rb_scan(&A, 0, 0, 0);
                                    rb_scan(&B, 0, 0, 0);
                                    strcpy(A.txt, TEXTS[ti]);
                                    strcpy(B.txt, TEXTS[ti]);
                                    A.tok[2].text = A.txt;
                                    B.tok[2].text = B.txt;
                                    A.tok[1].text = B.tok[1].text = nprev;
                                    A.tok[3].text = B.tok[3].text = nnext;
                                    A.tok[2].len = B.tok[2].len =
                                        (int16_t)(one ? 1 : 3);
                                    A.tok[2].trail = B.tok[2].trail =
                                        (uint8_t)(tr ? '.' : ' ');
                                    if (f7) { Bits_Set(7, A.tok[2].bits);
                                              Bits_Set(7, B.tok[2].bits); }
                                    if (f19) { Bits_Set(0x19, A.tok[2].bits);
                                               Bits_Set(0x19, B.tok[2].bits); }
                                    if (p19) { Bits_Set(0x19, A.tok[1].bits);
                                               Bits_Set(0x19, B.tok[1].bits); }
                                    if (x19) { Bits_Set(0x19, A.tok[3].bits);
                                               Bits_Set(0x19, B.tok[3].bits); }
                                    ra = ORIG(acr_t, 0x10020d70)(&A.ti, &A.tok[2], 0x44u);
                                    rb = Rule_Acronym(&B.ti, &B.tok[2], 0x44u);
                                    n++;
                                    if (ra != rb || !list_same(&A, &B, 12) ||
                                        strcmp(A.txt, B.txt) != 0) {
                                        if (bad++ < 8)
                                            fprintf(stderr, "  Rule_Acronym(%s, "
                                                    "%d%d%d%d%d%d): %d/%d -> %s / %s\n",
                                                    TEXTS[ti], f7, f19, p19, x19,
                                                    tr, one, ra, rb,
                                                    A.tok[2].text2 ? A.tok[2].text2 : "-",
                                                    B.tok[2].text2 ? B.tok[2].text2 : "-");
                                    }
                                }
    }

    /* Rule_SayRecord: the key it must match, the offset into the record's
     * text, both plural conditions, the key-13 special case that adds the
     * number behind, and the three spacing fixups. */
    {
        typedef int32_t(TV_THISCALL * rec_t)(TextIn *, Token *, uint32_t, int32_t);
        static const char *const WORDS[] = {"metro", "mes", "kilo", "a"};
        int wi, off, keyok, pl, f37, f49, key13, pn, nn, tr, ptr2;
        for (wi = 0; wi < 4; wi++)
          for (off = 0; off < 3; off += 2)
            for (keyok = 0; keyok < 2; keyok++)
              for (pl = 0; pl < 2; pl++)
                for (f37 = 0; f37 < 2; f37++)
                  for (f49 = 0; f49 < 2; f49++)
                    for (key13 = 0; key13 < 2; key13++)
                      for (pn = 0; pn < 3; pn++)
                        for (nn = 0; nn < 2; nn++)
                          for (tr = 0; tr < 3; tr++)
                            for (ptr2 = 0; ptr2 < 2; ptr2++) {
                                uint32_t key = key13 ? 0xd : 0x21;
                                int32_t ra, rb;
                                int w;
                                rb_scan(&A, 0, 0, 0);
                                rb_scan(&B, 0, 0, 0);
                                for (w = 0; w < 2; w++) {
                                    rblock *g = w ? &B : &A;
                                    memset(g->rec, 0, sizeof g->rec);
                                    *(uint32_t *)(g->rec + 0x20) =
                                        keyok ? key : key + 1;
                                    *(int16_t *)(g->rec + 0x2a) = (int16_t)off;
                                    strcpy((char *)g->rec + 0x2d + off, WORDS[wi]);
                                    g->tok[2].d18 = g->rec;
                                    g->tok[2].trail = (uint8_t)(
                                        tr == 0 ? 0 : tr == 1 ? '.' : ',');
                                    g->tok[1].trail = (uint8_t)(ptr2 ? 'x' : 0);
                                    g->tok[1].num = pn;
                                    g->tok[3].num = nn;
                                    if (f37) Bits_Set(0x37, g->tok[2].bits);
                                    if (f49) Bits_Set(0x49, g->tok[2].bits);
                                }
                                ra = ORIG(rec_t, 0x100212b0)(&A.ti, &A.tok[2], key, pl);
                                rb = Rule_SayRecord(&B.ti, &B.tok[2], key, pl);
                                n++;
                                if (ra != rb || !list_same(&A, &B, 12) ||
                                    memcmp(A.rec, B.rec, sizeof A.rec) != 0) {
                                    if (bad++ < 8)
                                        fprintf(stderr, "  Rule_SayRecord(%s off=%d "
                                                "key=%d pl=%d 37=%d 49=%d k13=%d "
                                                "pn=%d nn=%d tr=%d): %d/%d -> %s / %s\n",
                                                WORDS[wi], off, keyok, pl, f37, f49,
                                                key13, pn, nn, tr, ra, rb,
                                                A.tok[2].text2 ? A.tok[2].text2 : "-",
                                                B.tok[2].text2 ? B.tok[2].text2 : "-");
                                }
                            }
        /* the null token, which it reports rather than ignores */
        {
            int32_t ra, rb;
            rb_scan(&A, 0, 0, 0);
            rb_scan(&B, 0, 0, 0);
            ra = ORIG(rec_t, 0x100212b0)(&A.ti, NULL, 1, 0);
            rb = Rule_SayRecord(&B.ti, NULL, 1, 0);
            n++;
            /* the TextIn holds pointers into its own block, so what is
             * compared is the error ring it is supposed to have written */
            if (ra != rb || A.ti.err_count != B.ti.err_count ||
                memcmp(A.ti.errors, B.ti.errors, sizeof A.ti.errors) != 0) {
                if (bad++ < 8)
                    fprintf(stderr, "  Rule_SayRecord(NULL): %d/%d, errors %d/%d\n",
                            ra, rb, (int)A.ti.err_count, (int)B.ti.err_count);
            }
        }
    }

    /* Rule_SayGroupedNumber: groups that join and groups that do not, both
     * separators, the decimal comma, the fifteen-group ceiling approached
     * from below, and the token the interpreter is handed back. */
    {
        typedef int32_t(TV_THISCALL * grp_t)(TextIn *, Token **, uint32_t, int32_t);
        static const struct {
            const char *text[4];
            uint8_t trail[4];
            uint8_t flag[4];
            int n;
        } CASES[] = {
            {{"1"},                  {' '},              {1},          1},
            {{"1", "234"},           {'.', ' '},         {1, 1},       2},
            {{"1", "234", "567"},    {'.', '.', ' '},    {1, 1, 1},    3},
            {{"1", "234", "567"},    {' ', ' ', ' '},    {1, 1, 1},    3},
            {{"1", "234", "567"},    {'.', ' ', ' '},    {1, 1, 1},    3},
            {{"12", "34"},           {'.', ' '},         {1, 1},       2},
            {{"1", "234"},           {'.', ' '},         {1, 3},       2},
            {{"1", "234"},           {'.', ' '},         {1, 0},       2},
            {{"1", "50"},            {',', ' '},         {1, 1},       2},
            {{"1", "234", "50"},     {'.', ',', ' '},    {1, 1, 1},    3},
            {{"1", "50"},            {',', ' '},         {1, 3},       2},
            {{"1", "234"},           {'.', ' '},         {5, 1},       2},
            {{"1", "234"},           {'.', ' '},         {1, 5},       2},
            {{"123456789012345678"}, {' '},              {1},          1},
            {{"1", "234", "567", "890"}, {'.', '.', '.', 0}, {1,1,1,1}, 4},
        };
        int ci, fs;
        for (ci = 0; ci < (int)(sizeof CASES / sizeof CASES[0]); ci++)
            for (fs = 0; fs < 2; fs++) {
                Token *fa, *fb;
                int32_t ra, rb;
                fa = rb_group(&A, CASES[ci].text, CASES[ci].trail,
                              CASES[ci].flag, CASES[ci].n);
                fb = rb_group(&B, CASES[ci].text, CASES[ci].trail,
                              CASES[ci].flag, CASES[ci].n);
                ra = ORIG(grp_t, 0x10021be0)(&A.ti, &fa, 0x88u, fs);
                rb = Rule_SayGroupedNumber(&B.ti, &fb, 0x88u, fs);
                n++;
                if (ra != rb || A.ti.count != B.ti.count ||
                    list_index(&A, fa) != list_index(&B, fb) ||
                    !list_same(&A, &B, 12)) {
                    if (bad++ < 8) {
                        Token *sa = A.ti.head->next, *sb = B.ti.head->next;
                        fprintf(stderr, "  Rule_SayGroupedNumber(case %d, fs=%d): "
                                "%d/%d count %d/%d at %d/%d -> %s / %s\n",
                                ci, fs, ra, rb, (int)A.ti.count, (int)B.ti.count,
                                list_index(&A, fa), list_index(&B, fb),
                                sa && sa->text2 ? sa->text2 : "-",
                                sb && sb->text2 ? sb->text2 : "-");
                    }
                }
            }
    }

    /* Rule_Eval driven by hand-written bytecode.  One short program per
     * opcode, run against the same five-token list on both sides, with the
     * whole block compared afterwards -- so a program that moves the cursor,
     * sets a flag or rewrites rule_trail is checked on its effect as well as
     * its answer.  The handler opcodes are left to the corpus: they need a
     * token with text and a lexicon behind it, and the corpus has both.
     *
     * Opcode 28 is included only with rule_trail at zero, because with
     * anything else it walks off the end of the list by design. */
    {
        typedef int32_t(TV_THISCALL * ev_t)(TextIn *, Token **, Token **, uint32_t);
        static const struct { const char *name; int16_t code[8]; int16_t trail; }
        PROG[] = {
            {"true",      {13},                 0},
            {"false",     {14},                 0},
            {"not true",  {3, 13},              0},
            {"not false", {3, 14},              0},
            {"and t t",   {4, 13, 13},          0},
            {"and t f",   {4, 13, 14},          0},
            {"and f t",   {4, 14, 13},          0},
            {"and x3",    {5, 13, 13, 14},      0},
            {"and x4",    {6, 13, 13, 13, 13},  0},
            {"and x5",    {7, 13, 13, 13, 13, 14}, 0},
            {"and x6",    {8, 13, 13, 13, 13, 13, 13}, 0},
            {"or t f",    {9, 13, 14},          0},
            {"or f f",    {9, 14, 14},          0},
            {"or x3",     {10, 14, 14, 13},     0},
            {"or x4",     {11, 14, 14, 14, 14}, 0},
            {"or x5",     {12, 14, 14, 14, 14, 13}, 0},
            {"set trail", {15, 42},             0},
            {"eq imm",    {16, 42},             42},
            {"ne imm",    {16, 41},             42},
            {"eq eval",   {17, 13},             7},
            {"eq eval2",  {17, 15, 9},          7},
            {"range in",  {18, 10, 50},         42},
            {"range lo",  {18, 50, 60},         42},
            {"range hi",  {18, 10, 20},         42},
            {"lt imm",    {19, 50},             42},
            {"ge imm",    {19, 10},             42},
            {"gt eval",   {20, 15, 9},          42},
            {"gt imm",    {21, 10},             42},
            {"lt eval",   {22, 15, 99},         42},
            {"back 1",    {23, 1},              0},
            {"back 9",    {23, 9},              0},
            {"back sig",  {24, 1},              0},
            {"fwd 1",     {25, 1},              0},
            {"fwd 9",     {25, 9},              0},
            {"fwd sig",   {26, 1},              0},
            {"anchor",    {27},                 0},
            {"by trail",  {28},                 0},
            {"w08 eq",    {29, 0},              0},
            {"w08 ne",    {29, 1},              0},
            {"match trl", {33, 'x'},            0},
            {"bits 1/all",{34, 0x4a},           0},
            {"bits 2/any",{35, 0x4a, 0x51},     0},
            {"bits 3/any",{36, 0x4a, 0x51, 7},  0},
            {"bits 2/all",{37, 0x4a, 0x51},     0},
            {"bits 3/all",{38, 0x4a, 0x51, 7},  0},
            {"scan b1",   {39, 0x4a, 2},        0},
            {"scan b2",   {40, 0x4a, 0x51, 2},  0},
            {"scan b3",   {41, 0x4a, 0x51, 7, 2}, 0},
            {"scan b c2", {44, 0x4a, 0x51, 2},  0},
            {"scan f1",   {46, 0x4a, 2},        0},
            {"scan f2",   {47, 0x4a, 0x51, 2},  0},
            {"scan f c2", {51, 0x4a, 0x51, 2},  0},
            {"scan both", {53, 0x4a, 2},        0},
            {"scan b2th", {54, 0x4a, 0x51, 2},  0},
            {"set flag",  {68, 0x30},           0},
            {"clr flag",  {69, 0x4a},           0},
            {"first ch",  {88},                 0},
            {"last ch",   {89},                 0},
            {"len",       {90},                 0},
            {"num",       {91},                 0},
            {"bad op",    {30},                 0},
            {"bad op2",   {67},                 0},
            {"bad op3",   {1},                  0},
            {"bare ok",   {-31979},             0},
        };
        int pi, m4a;
        for (pi = 0; pi < (int)(sizeof PROG / sizeof PROG[0]); pi++)
            for (m4a = 0; m4a < 4; m4a++) {
                Token *ca, *cb, *aa, *ab;
                int32_t ra, rb;
                int w;
                rb_scan(&A, 0xa, 0, (unsigned)m4a);
                rb_scan(&B, 0xa, 0, (unsigned)m4a);
                for (w = 0; w < 2; w++) {
                    rblock *g = w ? &B : &A;
                    memcpy(g->code, PROG[pi].code, sizeof PROG[pi].code);
                    g->ti.rule_ip = g->code;
                    g->ti.rule_trail = PROG[pi].trail;
                    g->tok[2].text = g->txt;
                    g->tok[2].len = 3;
                    g->tok[2].num = 7;
                    g->tok[2].trail = 'x';
                }
                strcpy(A.txt, "abc");
                strcpy(B.txt, "abc");
                ca = &A.tok[2]; aa = &A.tok[1];
                cb = &B.tok[2]; ab = &B.tok[1];
                ra = ORIG(ev_t, 0x1001e5d0)(&A.ti, &ca, &aa, 0x11u);
                rb = Rule_Eval(&B.ti, &cb, &ab, 0x11u);
                n++;
                if (ra != rb ||
                    list_index(&A, ca) != list_index(&B, cb) ||
                    list_index(&A, aa) != list_index(&B, ab) ||
                    A.ti.rule_ip - A.code != B.ti.rule_ip - B.code ||
                    A.ti.rule_trail != B.ti.rule_trail ||
                    !list_same(&A, &B, 12)) {
                    if (bad++ < 10)
                        fprintf(stderr, "  Rule_Eval[%s, m4a=%d]: %d/%d ip %d/%d "
                                "trail %d/%d cur %d/%d\n",
                                PROG[pi].name, m4a, ra, rb,
                                (int)(A.ti.rule_ip - A.code),
                                (int)(B.ti.rule_ip - B.code),
                                (int)A.ti.rule_trail, (int)B.ti.rule_trail,
                                list_index(&A, ca), list_index(&B, cb));
                }
            }
    }

    /* TextIn_Reattach, which neither the corpus nor anything else reaches:
     * covrun --lang es --decompiled reports 0 of its 11 blocks executed, so
     * this is its only evidence.  Every reference position, both directions,
     * the head it refuses to go in front of, the null reference and the
     * empty slot. */
    {
        typedef Token *(TV_THISCALL * re_t)(TextIn *, Token *, int32_t);
        int ri, dir, empty;
        for (ri = -1; ri < 4; ri++)
            for (dir = 0; dir < 2; dir++)
                for (empty = 0; empty < 2; empty++) {
                    Token *ra, *rb;
                    int d = dir ? 1 : -1;
                    int w;
                    rb_scan(&A, 0, 0, 0);
                    rb_scan(&B, 0, 0, 0);
                    for (w = 0; w < 2; w++) {
                        rblock *g = w ? &B : &A;
                        /* lift tok[4] out by hand and park it */
                        g->tok[3].next = NULL;
                        g->tok[4].prev = NULL;
                        g->tok[4].next = NULL;
                        g->ti.tail = &g->tok[3];
                        g->ti.count = 3;
                        g->ti.detached = empty ? NULL : &g->tok[4];
                    }
                    ra = ORIG(re_t, 0x1001d850)(&A.ti, ri < 0 ? NULL : &A.tok[ri], d);
                    rb = TextIn_Reattach(&B.ti, ri < 0 ? NULL : &B.tok[ri], d);
                    n++;
                    if ((ra == NULL) != (rb == NULL) ||
                        list_index(&A, ra) != list_index(&B, rb) ||
                        A.ti.count != B.ti.count ||
                        (A.ti.detached == NULL) != (B.ti.detached == NULL)) {
                        if (bad++ < 8)
                            fprintf(stderr, "  TextIn_Reattach(tok[%d], %d, "
                                    "empty=%d): %d/%d\n", ri, d, empty,
                                    list_index(&A, ra), list_index(&B, rb));
                        continue;
                    }
                    if (!list_same(&A, &B, 12) ||
                        (A.ti.cur == NULL) != (B.ti.cur == NULL) ||
                        list_index(&A, A.ti.cur) != list_index(&B, B.ti.cur)) {
                        if (bad++ < 8)
                            fprintf(stderr, "  TextIn_Reattach(tok[%d], %d, "
                                    "empty=%d): list differs\n", ri, d, empty);
                    }
                }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "rule helpers", n - bad, n);
    return bad != 0;
}

/* ---- the number formatter --------------------------------------------------
 *
 * Number_WordsEx is a pure function of its six arguments, so it can be swept
 * properly: every value from zero to 9999 against all four styles, both
 * genders, every group level the scale tables index, and both values of the
 * flag word -- 640,000 comparisons, and they cost under a second.  Longer numbers come from a
 * curated list, because the recursion is what they are there to exercise and
 * one per group depth is enough.
 *
 * The digits buffer goes in and comes back out: the formatter truncates it
 * as it recurses and restores it before returning, so each side gets its own
 * writable copy and both copies are compared afterwards.
 */
static int unit_number(void)
{
    typedef int32_t(TV_CDECL * nwx_t)(char *, char *, int32_t, int32_t,
                                      int32_t, int32_t);
    static const char *const LONG[] = {
        "10000", "12345", "99999", "100000", "1000000", "1000001",
        "999999999", "1000000000", "1234567890", "100000000000",
        "999999999999", "1000000000000", "12345678901234567",
        "00000000000000001", "000", "0000",
    };
    nwx_t orig = (nwx_t)(uintptr_t)0x100200d0;
    char da[64], db[64], oa[512], ob[512];
    int v, sty, md, lv, fl, k, bad = 0, n = 0;

    for (v = 0; v <= 9999; v++)
        for (sty = 1; sty <= 4; sty++)
            for (md = 0; md < 2; md++)
                for (lv = 0; lv < 4; lv++)
                    for (fl = 0; fl < 2; fl++) {
                        int32_t ra, rb;
                        sprintf(da, "%d", v);
                        strcpy(db, da);
                        memset(oa, 0x5a, sizeof oa);
                        memset(ob, 0x5a, sizeof ob);
                        oa[0] = ob[0] = 0;
                        ra = orig(da, oa, sty, md, lv, fl);
                        rb = Number_WordsEx(db, ob, sty, md, lv, fl);
                        n++;
                        if (ra != rb || strcmp(oa, ob) != 0 ||
                            strcmp(da, db) != 0) {
                            if (bad++ < 8)
                                fprintf(stderr, "  Number_WordsEx(%d, s=%d m=%d "
                                        "l=%d f=%d): %d/%d -> %s / %s\n",
                                        v, sty, md, lv, fl, ra, rb, oa, ob);
                        }
                    }

    for (k = 0; k < (int)(sizeof LONG / sizeof LONG[0]); k++)
        for (sty = 1; sty <= 4; sty++)
            for (md = 0; md < 2; md++)
                for (lv = 0; lv < 4; lv++) {
                    int32_t ra, rb;
                    strcpy(da, LONG[k]);
                    strcpy(db, LONG[k]);
                    memset(oa, 0x5a, sizeof oa);
                    memset(ob, 0x5a, sizeof ob);
                    oa[0] = ob[0] = 0;
                    ra = orig(da, oa, sty, md, lv, 0);
                    rb = Number_WordsEx(db, ob, sty, md, lv, 0);
                    n++;
                    if (ra != rb || strcmp(oa, ob) != 0 ||
                        strcmp(da, db) != 0) {
                        if (bad++ < 8)
                            fprintf(stderr, "  Number_WordsEx(%s, s=%d m=%d "
                                    "l=%d): %d/%d -> %s / %s\n",
                                    LONG[k], sty, md, lv, ra, rb, oa, ob);
                    }
                }

    fprintf(stderr, "%-18s %d/%d identical\n", "Number_WordsEx", n - bad, n);
    return bad != 0;
}

/* ---- the frame generator ---------------------------------------------------
 *
 * Synth_Generate is a leaf with a clean interface: it is handed the engine,
 * a sample rate and the forty filter coefficients, and everything it does is
 * visible afterwards in the output block and in the samples it appends to
 * out_buf.  That makes it the one big function that can be driven directly
 * rather than through the corpus, which matters because 4 KB of fixed point
 * needs a test that says *where* it went wrong, not just that it did.
 *
 * So this reports the first sample that differs and every byte range of the
 * object that differs, named where the field is known.  Both engines get
 * their own output buffer; everything else about them is identical.
 *
 * The coefficients start from the 11 kHz filter table, which is real data,
 * with the handful of entries that are not filter taps set to sane values:
 * coef[19] is the number of samples to generate, coef[30] and coef[31] are
 * the two pitch periods, and coef[0] is a control word whose low five bits
 * index a table.  Feeding those junk makes the original loop for a very long
 * time or index off the end, which tests nothing.
 */
typedef void(TV_THISCALL * gen_t)(Engine *, uint16_t, const int16_t *);

static const struct { const char *name; size_t off, len; } SYNTH_FIELDS[] = {
    {"synth_hold", 0x00, 1}, {"synth_19ad", 0x01, 1}, {"synth_19ae", 0x02, 1},
    {"filt_coef", 0x04, 80}, {"o_2076", 0x692, 2}, {"o_2078", 0x694, 2},
    {"o_207a", 0x696, 2}, {"o_207c", 0x698, 2}, {"o_207e", 0x69a, 2},
    {"o_2080", 0x69c, 2}, {"o_2082", 0x69e, 2}, {"o_2086", 0x6a2, 2},
    {"o_2088", 0x6a4, 2}, {"o_208a", 0x6a6, 2}, {"o_208c", 0x6a8, 2},
    {"o_208e", 0x6aa, 2}, {"o_rate_div100", 0x6ac, 2},
    {"o_2092[]", 0x6ae, 22}, {"o_20a8", 0x6c4, 2}, {"o_20aa", 0x6c6, 2},
    {"o_20ac", 0x6c8, 2}, {"o_20ae[]", 0x6ca, 44}, {"o_20dc", 0x6f8, 4},
    {"o_20e0", 0x6fc, 4}, {"o_20e4", 0x700, 4},
    {"out_count", 0x738, 4},
};

static const char *synth_field(size_t off)
{
    size_t i;
    for (i = 0; i < sizeof SYNTH_FIELDS / sizeof SYNTH_FIELDS[0]; i++)
        if (off >= SYNTH_FIELDS[i].off &&
            off < SYNTH_FIELDS[i].off + SYNTH_FIELDS[i].len)
            return SYNTH_FIELDS[i].name;
    return "?";
}

/* A plausible coefficient set: the 11 kHz filter table, with the control
 * words set to something the generator can actually run on. */
static void synth_seed(Engine *e, int16_t *coef, int variant)
{
    static const int16_t BASE[40] = {
        0, 0, -24759, 20770, -24550, 28898, -19553, 27618, 0, 0,
        0, 0, 31521, 30905, 0, 0, 0, 17368, 0, 8192,
        0, 27691, 0, 25148, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 30905, 0, 0, 0, 11354, 0, 0,
    };
    int i;

    /* The real table is one shape; a spread of them is what actually
     * exercises the saturation, so every other variant gets pseudo-random
     * taps instead.  Only the control words are held sane. */
    memcpy(coef, BASE, sizeof BASE);
    if (variant & 0x80) {
        uint32_t rng = (uint32_t)variant * 1103515245u + 12345u;
        for (i = 2; i < 40; i++) {
            rng = rng * 1103515245u + 12345u;
            coef[i] = (int16_t)(rng >> 16);
        }
    }
    coef[0] = (int16_t)((variant & 0x1f) | ((variant & 0x20) ? 0x80 : 0));
    coef[1] = (int16_t)(0x2000 + variant * 91);
    coef[16] = (int16_t)(0x1000 + variant * 37);
    coef[17] = (int16_t)(0x4000 - variant * 53);
    coef[19] = 8192;                    /* a filter tap, not a count */
    coef[30] = (int16_t)(60 + (variant % 9) * 3);
    coef[31] = (int16_t)(90 + variant * 5);
    coef[36] = (int16_t)(0x0800 + variant * 17);
    for (i = 0; i < 40; i++)
        e->filt_coef[i] = coef[i];
    /* a deterministic starting state for the delay lines */
    for (i = 0; i < 22; i++)
        e->o_20ae[i] = (int16_t)(variant * 131 + i * 17);
    for (i = 0; i < 11; i++)
        e->o_2092[i] = (int16_t)(variant * 71 + i * 13);
    /* These five decide which arm of the excitation runs, so they are
     * spread combinatorially rather than linearly: the state machine in
     * o_207a, whether a pulse starts this sample, whether the phase
     * accumulator wraps, and which of the two pulse shapes is used. */
    e->o_2076 = (int16_t)((variant >> 3) & 3);
    e->o_2078 = (int16_t)(variant % 5);
    e->o_207a = (int16_t)(variant & 3);
    if ((variant >> 2) & 1)
        coef[30] = 0;
    if ((variant >> 5) & 1)
        e->o_2080 = 0x4000;
    e->o_207c = (int16_t)(variant * 29);
    e->o_207e = (int16_t)(0x0400 + variant * 11);
    e->o_2080 = (int16_t)(variant * 3);
    e->o_2082 = (int16_t)(((variant >> 6) % 3) - 1);
    e->o_2086 = (int16_t)(variant * 5);
    e->o_2088 = (uint16_t)(0x5a5a + variant * 37);  /* the noise carried in */
    e->o_208a = (int16_t)(0x1234 + variant);
    e->o_208c = (int16_t)(variant * 7);
    e->o_20a8 = (int16_t)(variant * 11);
    e->o_20aa = (int16_t)(variant * 13);
    e->o_20ac = (int16_t)(variant * 19);
    e->o_20dc = 0x1000 + variant;
    e->o_20e0 = ((variant >> 4) & 1) ? 0xfff00 : variant * 2048;
    e->o_20e4 = 0x2000 + variant;
}

static int synth_compare(const char *what, Engine *A, Engine *B,
                         const int16_t *ba, const int16_t *bb, int shown)
{
    size_t off;
    int diffs = 0, samples = (int)(A->out_count / 2);
    int i;

    if (A->out_count != B->out_count) {
        fprintf(stderr, "  %s: out_count %u/%u\n", what,
                (unsigned)A->out_count, (unsigned)B->out_count);
        diffs++;
    }
    for (i = 0; i < samples && i < 0x800; i++)
        if (ba[i] != bb[i]) {
            fprintf(stderr, "  %s: sample %d is %d, ours %d\n",
                    what, i, ba[i], bb[i]);
            diffs++;
            break;
        }
    {
        void *pa = A->out_buf, *pb = B->out_buf;
        A->out_buf = NULL;
        B->out_buf = NULL;
        for (off = 0; off < sizeof *A && diffs < shown; off++)
            if (((const uint8_t *)A)[off] != ((const uint8_t *)B)[off]) {
                size_t start = off;
                while (off < sizeof *A &&
                       ((const uint8_t *)A)[off] != ((const uint8_t *)B)[off])
                    off++;
                fprintf(stderr, "  %s: 0x%04x..0x%04x differs (%s)",
                        what, (unsigned)start, (unsigned)off,
                        synth_field(start));
                if ((start & 1) == 0)
                    fprintf(stderr, "  %d vs ours %d",
                            *(const int16_t *)((const uint8_t *)A + start),
                            *(const int16_t *)((const uint8_t *)B + start));
                fprintf(stderr, "\n");
                diffs++;
            }
        A->out_buf = (uint8_t *)pa;
        B->out_buf = (uint8_t *)pb;
    }
    return diffs;
}

static int unit_synth(void)
{
    gen_t orig = (gen_t)(uintptr_t)0x10009bf0;
    static const uint16_t RATES[] = {8000, 11025};
    int16_t *ba, *bb, ca[40], cb[40];
    int v, r, bad = 0, n = 0;
    char label[64];

    if (alloc_engines())
        return 2;
    ba = (int16_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                 PAGE_READWRITE);
    bb = (int16_t *)VirtualAlloc(NULL, 0x4000, MEM_RESERVE | MEM_COMMIT,
                                 PAGE_READWRITE);
    if (ba == NULL || bb == NULL)
        return 2;

    for (v = 0; v < 256; v++)
        for (r = 0; r < 2; r++) {
            memset(g_a, 0, sizeof *g_a);
            memset(g_b, 0, sizeof *g_b);
            synth_seed(g_a, ca, v);
            synth_seed(g_b, cb, v);
            memset(ba, 0x5a, 0x4000);
            memset(bb, 0x5a, 0x4000);
            g_a->out_buf = (uint8_t *)ba;
            g_b->out_buf = (uint8_t *)bb;
            g_a->out_count = g_b->out_count = 0;
            g_a->sample_rate = g_b->sample_rate = RATES[r];
            /* the sample count for one frame: Output_Reset's rate/100 */
            g_a->o_rate_div100 = g_b->o_rate_div100 = (int16_t)(RATES[r] / 100);
            orig(g_a, RATES[r], g_a->filt_coef);
            Synth_Generate(g_b, RATES[r], g_b->filt_coef);
            n++;
            {
                void *pa = g_a->out_buf, *pb = g_b->out_buf;
                int differ;
                g_a->out_buf = NULL;
                g_b->out_buf = NULL;
                differ = memcmp(ba, bb, 0x4000) != 0 ||
                         memcmp(g_a, g_b, sizeof *g_a) != 0;
                g_a->out_buf = (uint8_t *)pa;
                g_b->out_buf = (uint8_t *)pb;
                if (differ) {
                    if (bad < 3) {
                        sprintf(label, "Synth_Generate(v=%d, %u Hz)", v,
                                (unsigned)RATES[r]);
                        synth_compare(label, g_a, g_b, ba, bb, 6);
                    }
                    bad++;
                }
            }
        }

    /* A run of frames rather than one.
     *
     * Everything above seeds the state fresh and calls once, which cannot
     * see a field that is carried from frame to frame wrongly -- the pitch
     * counters o_2076/o_2078, the phase accumulator o_20e0, the noise in
     * o_2088, the o_207a state machine.  A real utterance is thousands of
     * consecutive calls, so this walks both engines through a long run with
     * the coefficients changing under them and stops at the first frame
     * whose state or output diverges.
     */
    for (v = 0; v < 24; v++) {
        int frame;
        memset(g_a, 0, sizeof *g_a);
        memset(g_b, 0, sizeof *g_b);
        synth_seed(g_a, ca, v * 11);
        synth_seed(g_b, cb, v * 11);
        g_a->sample_rate = g_b->sample_rate = (v & 1) ? 11025 : 8000;
        g_a->o_rate_div100 = g_b->o_rate_div100 =
            (int16_t)(g_a->sample_rate / 100);
        n++;
        for (frame = 0; frame < 400; frame++) {
            int differ;
            void *pa, *pb;
            /* new coefficients each frame, as Prosody_Build would give */
            {
                uint32_t rng = (uint32_t)(v * 7919 + frame * 104729 + 1);
                int i;
                for (i = 2; i < 40; i++) {
                    rng = rng * 1103515245u + 12345u;
                    ca[i] = cb[i] = (int16_t)(rng >> 16);
                }
                /* the control word and the period are held plausible so the
                 * pitch machine runs rather than sitting in one arm */
                ca[0] = cb[0] = (int16_t)(frame % 0x1f);
                ca[1] = cb[1] = (int16_t)(0x2000 + frame * 13);
                ca[19] = cb[19] = 8192;
                ca[30] = cb[30] = (int16_t)(40 + (frame % 17) * 7);
                ca[31] = cb[31] = (int16_t)(50 + (frame % 23) * 5);
                memcpy(g_a->filt_coef, ca, sizeof ca);
                memcpy(g_b->filt_coef, cb, sizeof cb);
            }
            memset(ba, 0x5a, 0x4000);
            memset(bb, 0x5a, 0x4000);
            g_a->out_buf = (uint8_t *)ba;
            g_b->out_buf = (uint8_t *)bb;
            g_a->out_count = g_b->out_count = 0;
            orig(g_a, g_a->sample_rate, g_a->filt_coef);
            Synth_Generate(g_b, g_b->sample_rate, g_b->filt_coef);
            pa = g_a->out_buf;
            pb = g_b->out_buf;
            g_a->out_buf = NULL;
            g_b->out_buf = NULL;
            differ = memcmp(ba, bb, 0x4000) != 0 ||
                     memcmp(g_a, g_b, sizeof *g_a) != 0;
            g_a->out_buf = (uint8_t *)pa;
            g_b->out_buf = (uint8_t *)pb;
            if (differ) {
                if (bad < 3) {
                    sprintf(label, "Synth_Generate(chain v=%d, frame %d)",
                            v, frame);
                    synth_compare(label, g_a, g_b, ba, bb, 8);
                }
                bad++;
                break;
            }
        }
    }

    /* The harness's own control: the same call with one coefficient moved
     * has to be reported, or the comparison above is proving nothing. */
    {
        int caught;
        memset(g_a, 0, sizeof *g_a);
        memset(g_b, 0, sizeof *g_b);
        synth_seed(g_a, ca, 3);
        synth_seed(g_b, cb, 3);
        /* a whole-bit change, not a least-significant one: a +1 on a Q15
         * coefficient multiplied by a small excitation rounds away, which
         * the first version of this control did not survive. */
        {
            int q;
            for (q = 0; q < 22; q++)
                g_b->o_20ae[q] = (int16_t)(g_b->o_20ae[q] ^ 0x2000);
            for (q = 0; q < 11; q++)
                g_b->o_2092[q] = (int16_t)(g_b->o_2092[q] ^ 0x2000);
            g_b->filt_coef[17] = (int16_t)(g_b->filt_coef[17] ^ 0x1000);
        }
        memset(ba, 0x5a, 0x4000);
        memset(bb, 0x5a, 0x4000);
        g_a->out_buf = (uint8_t *)ba;
        g_b->out_buf = (uint8_t *)bb;
        g_a->out_count = g_b->out_count = 0;
        g_a->sample_rate = g_b->sample_rate = 11025;
        g_a->o_rate_div100 = g_b->o_rate_div100 = 110;
        orig(g_a, 11025, g_a->filt_coef);
        orig(g_b, 11025, g_b->filt_coef);
        caught = memcmp(ba, bb, 0x4000) != 0;
        n++;
        if (!caught) {
            fprintf(stderr, "  the synth harness does not see a changed "
                            "coefficient -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "Synth_Generate", n - bad, n);
    return bad != 0;
}

/*
 * BitTable_Count and BitTable_Rank are pure functions of their arguments
 * and two static tables, so the whole reachable domain can be swept: five
 * table kinds, every byte count a row has, and every (a, b) the 23-stride
 * can address.  `kind` above 4 is left out on purpose -- the original reads
 * an uninitialised stack slot there and no caller does it.
 */
typedef int32_t(__stdcall *btcount_t)(int32_t, int32_t);
typedef int32_t(__stdcall *btrank_t)(int32_t, int32_t, int32_t);

static int unit_bittab(void)
{
    btcount_t o_count = (btcount_t)(uintptr_t)0x100122f0;
    btrank_t o_rank = (btrank_t)(uintptr_t)0x10012220;
    int kind, i, a, b, bad = 0, n = 0, shown = 0;

    for (kind = 0; kind <= 4; kind++)
        for (i = 0; i <= 0x44; i++) {
            int32_t x = o_count(kind, i), y = BitTable_Count(kind, i);
            n++;
            if (x != y) {
                if (shown++ < 5)
                    fprintf(stderr, "  BitTable_Count(%d,%d) = %d, ours %d\n",
                            kind, i, (int)x, (int)y);
                bad++;
            }
        }

    /* every bit a row can hold, reached the way the callers index it */
    for (kind = 0; kind <= 4; kind++)
        for (b = 0; b < 24; b++)
            for (a = 0; a < 24; a++) {
                int32_t x, y;
                if (a + b * 23 >= 0x43 * 8)
                    continue;
                x = o_rank(a, b, kind);
                y = BitTable_Rank(a, b, kind);
                n++;
                if (x != y) {
                    if (shown++ < 5)
                        fprintf(stderr,
                                "  BitTable_Rank(%d,%d,%d) = %d, ours %d\n",
                                a, b, kind, (int)x, (int)y);
                    bad++;
                }
            }

    /* the control: a table this does not read must change nothing, and one
     * it does read must be seen, or the sweep is proving nothing */
    {
        uint8_t *row = (uint8_t *)(uintptr_t)0x100572d0;
        uint8_t save = row[7];
        int32_t before = o_count(0, 0x43);
        int caught;
        row[7] = (uint8_t)(save ^ 0xff);
        caught = o_count(0, 0x43) != before;
        row[7] = save;
        n++;
        if (!caught) {
            fprintf(stderr, "  the bittab harness does not see a changed "
                            "table byte -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "bit table", n - bad, n);
    return bad != 0;
}

/*
 * Variant_Find is a pure function of its five arguments and one static
 * table per vowel, so the sweep is bounded only by the table: each is 312
 * bytes, so for a stride of nctx+1 the highest record that stays inside it
 * is (312 - nctx) / (nctx + 1).  Going past that reads off the end in the
 * original as well as here, and the engine never does.
 */
typedef int32_t(__thiscall *variant_t)(void *, int32_t, int32_t, int32_t,
                                       int32_t, int32_t);

static int unit_variant(void)
{
    variant_t orig = (variant_t)(uintptr_t)0x10012df0;
    int vowel, nctx, rec, k, bad = 0, n = 0, shown = 0;

    for (vowel = 0; vowel <= 4; vowel++)
        for (nctx = 1; nctx <= 3; nctx++) {
            int last = (312 - nctx) / (nctx + 1);
            for (rec = 0; rec <= last; rec++)
                for (k = 0; k < 256; k += 17) {
                    int32_t c0 = k, c1 = k ^ 0x2a, x, y;
                    x = orig(g_a, rec, vowel, c0, c1, nctx);
                    y = Variant_Find(g_a, rec, vowel, c0, c1, nctx);
                    n++;
                    if (x != y) {
                        if (shown++ < 5)
                            fprintf(stderr,
                                    "  Variant_Find(%d,%d,%d,%d,%d) = %d, "
                                    "ours %d\n",
                                    rec, vowel, c0, c1, nctx, (int)x, (int)y);
                        bad++;
                    }
                }
        }

    /* the control: the flag bit decides which character is matched, so
     * flipping it in the table has to change an answer somewhere */
    {
        uint8_t *tab = *(uint8_t **)(uintptr_t)0x100577e8;
        int caught = 0;
        for (rec = 1; rec <= 156 && !caught; rec++) {
            int32_t before = orig(g_a, rec, 0, 'K', 'S', 1);
            tab[(rec - 1) * 2] ^= 1;
            caught = orig(g_a, rec, 0, 'K', 'S', 1) != before;
            tab[(rec - 1) * 2] ^= 1;
        }
        n++;
        if (!caught) {
            fprintf(stderr, "  the variant harness does not see a flipped "
                            "flag bit -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "variant table", n - bad, n);
    return bad != 0;
}

/*
 * Cluster_SetTracks reads three stage-3 node pointers and a short chain
 * hanging off `scan`, and writes four cells of trk_param.  Both engines get
 * the same chain, so the sweep is over the phoneme letters rather than over
 * the list shape: every consonant on the left, the substitutions 'N'+'C'
 * and 'Z' on the right, and a letter that maps to -1 so the early returns
 * are covered too.
 *
 * `vowel` has to agree with the nucleus letter -- the argument indexes the
 * per-vowel tables while the letter picks which of the two sets they come
 * from, and the original indexes a three-entry table with it -- so the
 * sweep pairs them the way the caller does.  '@' and '|' are left out: the
 * original reads uninitialised locals for them unless the rank is -1, so
 * there is nothing well-defined to compare against.
 */
typedef uint8_t(__thiscall *cluster_t)(Engine *, int32_t);

static Node g_chain[8];
static Node g_cur_node, g_ctl_node;

static void cluster_chain(Engine *e, int nuc, int a, int b, int cur, int ctl)
{
    int i;

    for (i = 0; i < 8; i++) {
        g_chain[i].next = &g_chain[i + 1 < 8 ? i + 1 : 7];
        g_chain[i].prev = NULL;
        g_chain[i].value = (uint8_t)('B' + i);
    }
    g_chain[0].value = (uint8_t)nuc;
    g_chain[1].value = (uint8_t)a;
    g_chain[2].value = (uint8_t)b;
    g_cur_node.value = (uint8_t)cur;
    g_ctl_node.value = (uint8_t)ctl;
    e->stage_ctx[3].scan = &g_chain[0];
    e->stage_ctx[3].cur = &g_cur_node;
    e->stage_ctx[3].ctl = &g_ctl_node;
}

static int unit_cluster(void)
{
    cluster_t orig = (cluster_t)(uintptr_t)0x10012f30;
    static const char VOW[] = "AEIOU";
    static const char CTL[] = "BCDFGKLMNPRSTXYbdgnry~ ZQ";
    static const char SETA[] = "BCKLMNPRSTnrZQ";
    static const char SETB[] = "CBKSn~";
    static const char CUR[] = "BKS ";
    int v, i, j, k, m, lead, bad = 0, n = 0, shown = 0, ones = 0;

    if (alloc_engines())
        return 2;

    /* lead 0: the nucleus is on scan.  lead 1 and 2: a '&' or a '%' sits in
     * front of it and has to be stepped over. */
    for (lead = 0; lead < 3; lead++)
        for (v = 0; v < 5; v++)
            for (i = 0; CTL[i]; i++)
                for (j = 0; SETA[j]; j++)
                    for (k = 0; SETB[k]; k++)
                        for (m = 0; CUR[m]; m++) {
                            uint8_t x, y;
                            int differ;

                            memset(g_a, 0, sizeof *g_a);
                            memset(g_b, 0, sizeof *g_b);
                            if (lead == 0) {
                                cluster_chain(g_a, VOW[v], SETA[j], SETB[k],
                                              CUR[m], CTL[i]);
                                x = orig(g_a, v);
                                cluster_chain(g_b, VOW[v], SETA[j], SETB[k],
                                              CUR[m], CTL[i]);
                                y = Cluster_SetTracks(g_b, v);
                            } else {
                                /* scan is the marker; the nucleus is two
                                 * nodes along, where the original looks */
                                cluster_chain(g_a, lead == 1 ? '&' : '%',
                                              SETA[j], VOW[v], CUR[m], CTL[i]);
                                g_chain[3].value = (uint8_t)SETA[j];
                                g_chain[4].value = (uint8_t)SETB[k];
                                x = orig(g_a, v);
                                cluster_chain(g_b, lead == 1 ? '&' : '%',
                                              SETA[j], VOW[v], CUR[m], CTL[i]);
                                g_chain[3].value = (uint8_t)SETA[j];
                                g_chain[4].value = (uint8_t)SETB[k];
                                y = Cluster_SetTracks(g_b, v);
                            }
                            n++;
                            if (x)
                                ones++;
                            g_a->stage_ctx[3].scan = NULL;
                            g_b->stage_ctx[3].scan = NULL;
                            g_a->stage_ctx[3].cur = NULL;
                            g_b->stage_ctx[3].cur = NULL;
                            g_a->stage_ctx[3].ctl = NULL;
                            g_b->stage_ctx[3].ctl = NULL;
                            differ = x != y ||
                                     memcmp(g_a, g_b, sizeof *g_a) != 0;
                            if (differ) {
                                if (shown++ < 5)
                                    fprintf(stderr,
                                            "  Cluster_SetTracks(lead %d, "
                                            "%c %c %c cur %c ctl %c, v=%d) "
                                            "= %d, ours %d\n",
                                            lead, VOW[v], SETA[j], SETB[k],
                                            CUR[m], CTL[i], v, x, y);
                                bad++;
                            }
                        }

    /* The control.  A single byte is not enough: most environments have no
     * record at all and return 0 without reading the table, so the control
     * first finds an environment that does return 1, then moves the whole
     * record block under it rather than one byte of it. */
    {
        uint8_t *rec = *(uint8_t **)(uintptr_t)0x100573a0;
        int caught = 0, q;
        int32_t before[4];

        for (i = 0; CTL[i] && !caught; i++)
            for (j = 0; SETA[j] && !caught; j++) {
                memset(g_a, 0, sizeof *g_a);
                cluster_chain(g_a, 'A', SETA[j], 'C', 'B', CTL[i]);
                if (!orig(g_a, 0))
                    continue;
                for (q = 0; q < 4; q++)
                    before[q] = g_a->trk_param[9 + q][6];
                for (q = 0; q < 0x1000; q++)
                    rec[q] ^= 0xff;
                memset(g_a, 0, sizeof *g_a);
                cluster_chain(g_a, 'A', SETA[j], 'C', 'B', CTL[i]);
                orig(g_a, 0);
                for (q = 0; q < 4; q++)
                    if (g_a->trk_param[9 + q][6] != before[q])
                        caught = 1;
                for (q = 0; q < 0x1000; q++)
                    rec[q] ^= 0xff;
            }
        n++;
        if (!caught) {
            fprintf(stderr, "  the cluster harness does not see a moved "
                            "record table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "  (%d of %d environments set the parameters)\n", ones, n - 1);
    fprintf(stderr, "%-18s %d/%d identical\n", "cluster tracks", n - bad, n);
    return bad != 0;
}

/*
 * Ramp_Fill writes only into the buffer it is handed, so the sweep is the
 * whole interesting domain: every run length the callers use, every start
 * offset that can wrap the 256-byte track, and endpoints chosen to cover
 * rising, falling, equal and the extremes where the eight-bit sum wraps.
 */
typedef void(__stdcall *ramp_t)(uint8_t *, int32_t, int32_t, int32_t,
                               int32_t);

static int unit_ramp(void)
{
    ramp_t orig = (ramp_t)(uintptr_t)0x10012ed0;
    static const int ENDS[] = {0, 1, 2, 17, 64, 127, 128, 200, 254, 255};
    uint8_t ba[256], bb[256];
    int n, st, i, j, bad = 0, cnt = 0, shown = 0;

    for (n = 0; n <= 300; n += (n < 20 ? 1 : 7))
        for (st = 0; st < 256; st += 37)
            for (i = 0; i < 10; i++)
                for (j = 0; j < 10; j++) {
                    memset(ba, 0x5a, sizeof ba);
                    memset(bb, 0x5a, sizeof bb);
                    orig(ba, st, n, ENDS[i], ENDS[j]);
                    Ramp_Fill(bb, st, n, ENDS[i], ENDS[j]);
                    cnt++;
                    if (memcmp(ba, bb, sizeof ba) != 0) {
                        if (shown++ < 5)
                            fprintf(stderr,
                                    "  Ramp_Fill(start %d, n %d, %d -> %d) "
                                    "differs\n", st, n, ENDS[i], ENDS[j]);
                        bad++;
                    }
                }

    /* the control: a run that really is written, so a changed endpoint has
     * to show up somewhere in the buffer */
    {
        int caught;

        memset(ba, 0x5a, sizeof ba);
        memset(bb, 0x5a, sizeof bb);
        orig(ba, 3, 40, 10, 200);
        orig(bb, 3, 40, 10, 201);
        caught = memcmp(ba, bb, sizeof ba) != 0;
        cnt++;
        if (!caught) {
            fprintf(stderr, "  the ramp harness does not see a changed "
                            "endpoint -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "ramp fill", cnt - bad, cnt);
    return bad != 0;
}

/*
 * Track_Set reads one node -- stage 3's cur -- and writes two rows of
 * trk_param plus two cells of trk_490.  The sweep is every track the one
 * caller passes, every phoneme letter that the flag in column 0 turns on
 * or off, and byte values spread over the range the scalings act on.
 * Tracks outside 9..12 are left out: the original writes an uninitialised
 * local for them and there is nothing to compare against.
 */
typedef void(__thiscall *trackset_t)(Engine *, int32_t, int32_t, int32_t,
                                    int32_t, int32_t, int32_t);

static int unit_trackset(void)
{
    trackset_t orig = (trackset_t)(uintptr_t)0x10012cb0;
    static const char PH[] = "MNn~RrBKSAZ ";
    static const int V[] = {0, 1, 2, 63, 100, 128, 200, 255};
    int tr, ph, i, j, k, bad = 0, n = 0, shown = 0;

    if (alloc_engines())
        return 2;

    for (tr = 9; tr <= 12; tr++)
        for (ph = 0; PH[ph]; ph++)
            for (i = 0; i < 8; i++)
                for (j = 0; j < 8; j++)
                    for (k = 0; k < 8; k++) {
                        int differ;

                        memset(g_a, 0, sizeof *g_a);
                        memset(g_b, 0, sizeof *g_b);
                        g_cur_node.value = (uint8_t)PH[ph];
                        g_a->stage_ctx[3].cur = &g_cur_node;
                        g_b->stage_ctx[3].cur = &g_cur_node;
                        orig(g_a, V[i], V[j], V[k], 0x1234, tr, 0);
                        Track_Set(g_b, V[i], V[j], V[k], 0x1234, tr, 0);
                        g_a->stage_ctx[3].cur = NULL;
                        g_b->stage_ctx[3].cur = NULL;
                        n++;
                        differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
                        if (differ) {
                            if (shown++ < 5)
                                fprintf(stderr,
                                        "  Track_Set(track %d, '%c', "
                                        "%d %d %d) differs\n",
                                        tr, PH[ph], V[i], V[j], V[k]);
                            bad++;
                        }
                    }

    /* the control: column 0 is the flag, so a phoneme that turns it off has
     * to give a different row from one that leaves it on */
    {
        int32_t on, off;

        memset(g_a, 0, sizeof *g_a);
        g_cur_node.value = 'B';
        g_a->stage_ctx[3].cur = &g_cur_node;
        orig(g_a, 40, 50, 60, 0, 10, 0);
        on = g_a->trk_param[10][0];
        memset(g_a, 0, sizeof *g_a);
        g_cur_node.value = 'N';
        g_a->stage_ctx[3].cur = &g_cur_node;
        orig(g_a, 40, 50, 60, 0, 10, 0);
        off = g_a->trk_param[10][0];
        g_a->stage_ctx[3].cur = NULL;
        n++;
        if (on == off) {
            fprintf(stderr, "  the track harness does not see the column 0 "
                            "flag change -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "track set", n - bad, n);
    return bad != 0;
}

/*
 * Extend writes only into the buffer it is handed, so the sweep is every
 * TCon the table holds plus the clamped ones above it, run lengths on both
 * sides of the contour length, start offsets that wrap the 256-byte track,
 * and endpoints covering rising, falling and equal.
 */
typedef void(__thiscall *extend_t)(Engine *, uint8_t *, int32_t, int32_t,
                                  int32_t, int32_t, int32_t);

static int unit_extend(void)
{
    extend_t orig = (extend_t)(uintptr_t)0x1000af20;
    static const int ENDS[] = {0, 1, 30, 128, 200, 255};
    uint8_t ba[256], bb[256];
    int tc, nn, st, i, j, bad = 0, cnt = 0, shown = 0;

    if (alloc_engines())
        return 2;

    for (tc = 0; tc <= 24; tc++)
        for (nn = 0; nn <= 40; nn += (nn < 12 ? 1 : 9))
            for (st = 0; st < 256; st += 61)
                for (i = 0; i < 6; i++)
                    for (j = 0; j < 6; j++) {
                        memset(ba, 0x5a, sizeof ba);
                        memset(bb, 0x5a, sizeof bb);
                        orig(g_a, ba, st, tc, nn, ENDS[i], ENDS[j]);
                        Extend(g_b, bb, st, tc, nn, ENDS[i], ENDS[j]);
                        cnt++;
                        if (memcmp(ba, bb, sizeof ba) != 0) {
                            if (shown++ < 5)
                                fprintf(stderr,
                                        "  Extend(start %d, tcon %d, n %d, "
                                        "%d -> %d) differs\n",
                                        st, tc, nn, ENDS[i], ENDS[j]);
                            bad++;
                        }
                    }

    /* the control: the contour is what shapes the glide, so moving a byte
     * of it has to change a run long enough to reach that byte */
    {
        uint8_t *shape = (uint8_t *)((const uint8_t *const *)
                                     (uintptr_t)0x10058368)[10];
        int caught;

        memset(ba, 0x5a, sizeof ba);
        orig(g_a, ba, 0, 10, 20, 255, 0);
        shape[4] = (uint8_t)(shape[4] ^ 0x3c);
        memset(bb, 0x5a, sizeof bb);
        orig(g_a, bb, 0, 10, 20, 255, 0);
        caught = memcmp(ba, bb, sizeof ba) != 0;
        shape[4] = (uint8_t)(shape[4] ^ 0x3c);
        cnt++;
        if (!caught) {
            fprintf(stderr, "  the extend harness does not see a changed "
                            "contour byte -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "extend", cnt - bad, cnt);
    return bad != 0;
}

/*
 * Track_Contour writes into the track buffers and moves three engine
 * fields, so both engines are compared whole -- with trk_buf nulled, since
 * each points into its own trk_data and the pointers necessarily differ.
 *
 * The structural parameters are swept exhaustively, because they are what
 * choose the branches: the track, the segment length across the 4, 7 and 8
 * boundaries the code tests, the transition length across 4, 20 and the
 * 0x7f that means "none", and the phonemes that the two special cases key
 * on -- P, T and K on track 9, and Y after a flagged phoneme.  The six
 * levels, four breakpoints and three partner levels are pseudo-random on
 * top of that, kept small enough that no run is longer than the 256-byte
 * track it wraps in.
 */
typedef void(__thiscall *contour_t)(Engine *, int32_t, int32_t, int32_t,
                                    int32_t, int32_t, int32_t, int32_t,
                                    int32_t, int32_t, int32_t, int32_t,
                                    int32_t, int32_t, int32_t, int32_t);

static Node g_ct_cur, g_ct_prev;

static void contour_seed(Engine *e, int track, int seg, int trans, int ph,
                         int pph)
{
    int i;

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        memset(e->trk_data[i], 0x5a, 256);
        e->trk_wr[i] = (int32_t)(7 + i * 3);
        e->trk_490[i] = (int32_t)(i * 37 + 11);
    }
    g_ct_cur.value = (uint8_t)ph;
    g_ct_cur.prev = &g_ct_prev;
    g_ct_prev.value = (uint8_t)pph;
    e->stage_ctx[3].cur = &g_ct_cur;
    e->seg_len = seg;
    e->trans_len = trans;
    (void)track;
}

static int unit_contour(void)
{
    contour_t orig = (contour_t)(uintptr_t)0x10012390;
    static const int SEG[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 20, 40};
    static const int TRANS[] = {-1, 0, 1, 2, 3, 4, 5, 20, 21, 0x7f};
    static const char PH[] = "PTKYBM A";
    static const char PPH[] = "AB";
    int track, si, ti, pi, qi, v, bad = 0, n = 0, shown = 0;

    if (alloc_engines())
        return 2;

    for (track = 9; track <= 12; track++)
        for (si = 0; si < 12; si++)
            for (ti = 0; ti < 10; ti++)
                for (pi = 0; PH[pi]; pi++)
                    for (qi = 0; PPH[qi]; qi++)
                        for (v = 0; v < 3; v++) {
                            uint32_t r = (uint32_t)(track * 7919 + si * 104729 +
                                                    ti * 1299709 + pi * 15485863 +
                                                    qi * 179 + v * 65537 + 1);
                            int32_t a[15];
                            int differ, k;
                            void *pa[22], *pb[22];

                            for (k = 0; k < 15; k++) {
                                r = r * 1103515245u + 12345u;
                                a[k] = (int32_t)((r >> 16) & 0xff);
                            }
                            /* the four breakpoints stay inside the track */
                            for (k = 4; k < 8; k++)
                                a[k] = a[k] % 50;
                            a[13] = track;
                            a[14] = 0;

                            contour_seed(g_a, track, SEG[si], TRANS[ti],
                                         PH[pi], PPH[qi]);
                            contour_seed(g_b, track, SEG[si], TRANS[ti],
                                         PH[pi], PPH[qi]);
                            orig(g_a, a[0], a[1], a[2], a[3], a[4], a[5],
                                 a[6], a[7], a[8], a[9], a[10], a[11],
                                 a[12], a[13], a[14]);
                            Track_Contour(g_b, a[0], a[1], a[2], a[3], a[4],
                                          a[5], a[6], a[7], a[8], a[9],
                                          a[10], a[11], a[12], a[13], a[14]);
                            n++;
                            for (k = 0; k < 22; k++) {
                                pa[k] = g_a->trk_buf[k];
                                pb[k] = g_b->trk_buf[k];
                                g_a->trk_buf[k] = NULL;
                                g_b->trk_buf[k] = NULL;
                            }
                            g_a->stage_ctx[3].cur = NULL;
                            g_b->stage_ctx[3].cur = NULL;
                            differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
                            for (k = 0; k < 22; k++) {
                                g_a->trk_buf[k] = (uint8_t *)pa[k];
                                g_b->trk_buf[k] = (uint8_t *)pb[k];
                            }
                            if (differ) {
                                if (shown++ < 5)
                                    fprintf(stderr,
                                            "  Track_Contour(track %d, seg %d, "
                                            "trans %d, '%c' after '%c', set %d)"
                                            " differs\n",
                                            track, SEG[si], TRANS[ti], PH[pi],
                                            PPH[qi], v);
                                bad++;
                            }
                        }

    /* the control: the segment length decides every run length, so moving
     * it has to change the buffer */
    {
        int caught;

        contour_seed(g_a, 9, 12, 3, 'B', 'A');
        orig(g_a, 10, 20, 30, 40, 2, 4, 6, 8, 50, 60, 70, 80, 90, 9, 0);
        contour_seed(g_b, 9, 13, 3, 'B', 'A');
        orig(g_b, 10, 20, 30, 40, 2, 4, 6, 8, 50, 60, 70, 80, 90, 9, 0);
        caught = memcmp(g_a->trk_data[9], g_b->trk_data[9], 256) != 0;
        g_a->stage_ctx[3].cur = NULL;
        g_b->stage_ctx[3].cur = NULL;
        n++;
        if (!caught) {
            fprintf(stderr, "  the contour harness does not see a changed "
                            "segment length -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "track contour", n - bad, n);
    return bad != 0;
}

/*
 * Segment_Apply reads five nodes and writes four tracks, their partners and
 * three engine fields, so the engines are compared whole with trk_buf
 * nulled.
 *
 * The sweep pairs the nucleus letter with the vowel number, as the caller
 * does -- they index the same tables from two directions -- and covers the
 * consonants on both sides including the ones the substitutions rewrite
 * ('N' before 'C', 'Z', and 'r' before 'R'), the segment lengths the
 * contour code branches on, and the transition lengths including the 0x7f
 * that means "none".
 *
 * trk_param[t][3] is seeded above zero: it is the count the trailing
 * Ramp_Fill runs on, and at zero or below the original spins for four
 * billion iterations rather than doing nothing.  The engine never reaches
 * it that way.
 */
typedef uint8_t(__thiscall *segment_t)(Engine *, int32_t);

static Node g_sg_cur, g_sg_prev, g_sg_ctl, g_sg_scan, g_sg_next;

static void segment_seed(Engine *e, int nuc, int ccur, int cprev, int cscan,
                         int cnext, int arg, int trans, int p3rd)
{
    int i;

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        memset(e->trk_data[i], 0x5a, 256);
        e->trk_wr[i] = (int32_t)(5 + i * 3);
        e->trk_490[i] = (int32_t)(i * 37 + 11);
        e->trk_param[i][3] = p3rd;
    }
    g_sg_cur.value = (uint8_t)ccur;
    g_sg_cur.prev = &g_sg_prev;
    g_sg_prev.value = (uint8_t)cprev;
    g_sg_ctl.value = (uint8_t)nuc;
    g_sg_ctl.arg = (uint32_t)arg;
    g_sg_scan.value = (uint8_t)cscan;
    g_sg_scan.next = &g_sg_next;
    g_sg_next.value = (uint8_t)cnext;
    e->stage_ctx[3].cur = &g_sg_cur;
    e->stage_ctx[3].ctl = &g_sg_ctl;
    e->stage_ctx[3].scan = &g_sg_scan;
    e->trans_len = trans;
}

static int unit_segment(void)
{
    segment_t orig = (segment_t)(uintptr_t)0x100117e0;
    static const char VOW[] = "AEIOU";
    static const char CUR[] = "BCDKLMNPRSTZbdgnry~ ";
    static const char SCAN[] = "BCKMNRSTZrn~";
    static const char NEXT[] = "CR ";
    static const int ARG[] = {8, 12, 20, 40};
    static const int TRANS[] = {0, 1, 3, 0x7f};
    /* above 1: Track_Contour takes one off when trans_len is 1, and at
     * zero the trailing Ramp_Fill gets a count of -1, which spins in the
     * original as well as here.  The engine never reaches it that way. */
    static const int P3[] = {2, 3, 6, 8};
    int v, ci, si, ni, ai, ti, pi, bad = 0, n = 0, shown = 0, ones = 0;

    if (alloc_engines())
        return 2;

    for (v = 0; v < 5; v++)
        for (ci = 0; CUR[ci]; ci++)
            for (si = 0; SCAN[si]; si++)
                for (ni = 0; NEXT[ni]; ni++)
                    for (ai = 0; ai < 4; ai++)
                        for (ti = 0; ti < 4; ti++)
                            for (pi = 0; pi < 4; pi++) {
                                uint8_t x, y;
                                int differ, k;
                                void *pa[22], *pb[22];

                                segment_seed(g_a, VOW[v], CUR[ci], 'A',
                                             SCAN[si], NEXT[ni], ARG[ai],
                                             TRANS[ti], P3[pi]);
                                x = orig(g_a, v);
                                segment_seed(g_b, VOW[v], CUR[ci], 'A',
                                             SCAN[si], NEXT[ni], ARG[ai],
                                             TRANS[ti], P3[pi]);
                                y = Segment_Apply(g_b, v);
                                n++;
                                if (x)
                                    ones++;
                                for (k = 0; k < 22; k++) {
                                    pa[k] = g_a->trk_buf[k];
                                    pb[k] = g_b->trk_buf[k];
                                    g_a->trk_buf[k] = NULL;
                                    g_b->trk_buf[k] = NULL;
                                }
                                g_a->stage_ctx[3].cur = NULL;
                                g_b->stage_ctx[3].cur = NULL;
                                g_a->stage_ctx[3].ctl = NULL;
                                g_b->stage_ctx[3].ctl = NULL;
                                g_a->stage_ctx[3].scan = NULL;
                                g_b->stage_ctx[3].scan = NULL;
                                differ = x != y ||
                                         memcmp(g_a, g_b, sizeof *g_a) != 0;
                                for (k = 0; k < 22; k++) {
                                    g_a->trk_buf[k] = (uint8_t *)pa[k];
                                    g_b->trk_buf[k] = (uint8_t *)pb[k];
                                }
                                if (differ) {
                                    if (shown++ < 5)
                                        fprintf(stderr,
                                                "  Segment_Apply('%c' %c/%c "
                                                "next %c arg %d trans %d p3 "
                                                "%d) = %d, ours %d\n",
                                                VOW[v], CUR[ci], SCAN[si],
                                                NEXT[ni], ARG[ai], TRANS[ti],
                                                P3[pi], x, y);
                                    bad++;
                                }
                            }

    /* the control: the contour tables are six bytes an entry and every
     * breakpoint comes out of them, so moving one has to be seen */
    {
        uint8_t *tab = (uint8_t *)((const uint8_t *const *)
                                   (uintptr_t)0x10057400)[0];
        int caught, k;

        segment_seed(g_a, 'A', 'K', 'A', 'S', 'C', 20, 3, 6);
        orig(g_a, 0);
        /* the whole table, not a window of it: the entry is at
         * wide[0] * 6 and wide[0] is eight bits wide, so a short window
         * misses the entry the record actually selects.  1480 bytes is the
         * span to the next contour table. */
        for (k = 0; k < 1480; k++)
            tab[k] ^= 0xff;
        segment_seed(g_b, 'A', 'K', 'A', 'S', 'C', 20, 3, 6);
        orig(g_b, 0);
        for (k = 0; k < 1480; k++)
            tab[k] ^= 0xff;
        caught = memcmp(g_a->trk_data[9], g_b->trk_data[9], 256) != 0;
        g_a->stage_ctx[3].cur = NULL;
        g_b->stage_ctx[3].cur = NULL;
        n++;
        if (!caught) {
            fprintf(stderr, "  the segment harness does not see a moved "
                            "contour table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "  (%d of %d environments found a record)\n", ones, n - 1);
    fprintf(stderr, "%-18s %d/%d identical\n", "segment apply", n - bad, n);
    return bad != 0;
}

/*
 * Track_Adjust is a chain of tests on stage 3's three phonemes, so the sweep
 * is over those three plus the neighbours the deeper tests walk to: scan's
 * next, ctl's next twice over, and ctl's four predecessors.  Everything it
 * reads and writes is seeded pseudo-randomly so a missed write shows up.
 */
typedef void(__thiscall *adjust_t)(Engine *);

static Node g_aj_cur, g_aj_ctl, g_aj_scan, g_aj_sn, g_aj_n1, g_aj_n2;
static Node g_aj_sn2, g_aj_sn3, g_aj_sn4;
static Node g_aj_p1, g_aj_p2, g_aj_p3, g_aj_p4;

static void adjust_seed(Engine *e, int ccur, int cctl, int cscan, int csn,
                        int cn2, const char *prevs, int s650, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2654435761u + 1u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        memset(e->trk_data[i], 0x5a, 256);
        e->trk_wr[i] = (int32_t)(20 + i * 3);
        r = r * 1103515245u + 12345u;
        e->trk_490[i] = (int32_t)((r >> 16) & 0x7ff);
        r = r * 1103515245u + 12345u;
        e->trk_4e8[i] = (int32_t)((r >> 16) & 0x7fff);
        r = r * 1103515245u + 12345u;
        e->trk_55c[i] = (int32_t)((r >> 16) & 0x3ff);
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0xfff);
        }
    }
    e->s3_1fe8 = 4 + (seed % 9);
    e->s3_650 = (uint8_t)s650;

    g_aj_cur.value = (uint8_t)ccur;
    g_aj_ctl.value = (uint8_t)cctl;
    g_aj_scan.value = (uint8_t)cscan;
    g_aj_scan.next = &g_aj_sn;
    g_aj_sn.value = (uint8_t)csn;
    /* Cluster_SetTracks walks three more nexts off scan, so the chain
     * has to be long enough or the original reads through a null. */
    g_aj_sn.next = &g_aj_sn2;
    g_aj_sn2.next = &g_aj_sn3;
    g_aj_sn3.next = &g_aj_sn4;
    g_aj_sn4.next = &g_aj_sn4;
    g_aj_sn2.value = 'K';
    g_aj_sn3.value = 'A';
    g_aj_sn4.value = 'S';
    g_aj_ctl.next = &g_aj_n1;
    g_aj_n1.next = &g_aj_n2;
    g_aj_n1.value = 'A';
    g_aj_n2.value = (uint8_t)cn2;
    g_aj_ctl.prev = &g_aj_p1;
    g_aj_p1.prev = &g_aj_p2;
    g_aj_p2.prev = &g_aj_p3;
    g_aj_p3.prev = &g_aj_p4;
    g_aj_p1.value = (uint8_t)prevs[0];
    g_aj_p2.value = (uint8_t)prevs[1];
    g_aj_p3.value = (uint8_t)prevs[2];
    g_aj_p4.value = (uint8_t)prevs[3];
    e->stage_ctx[3].cur = &g_aj_cur;
    e->stage_ctx[3].ctl = &g_aj_ctl;
    e->stage_ctx[3].scan = &g_aj_scan;
}

static int unit_adjust(void)
{
    adjust_t orig = (adjust_t)(uintptr_t)0x10010ba0;
    static const char SET[] = "AEIOULMNnRrbdgSXYPTKCBGI~aehijkvwqou";
    static const char *PREVS[] = {"rRrP", "rRrb", "rRrB", "rRrK", "ABCD"};
    int ci, li, si, ni, pi, s6, sd, bad = 0, n = 0, shown = 0;

    if (alloc_engines())
        return 2;

    for (ci = 0; SET[ci]; ci++)
        for (li = 0; SET[li]; li++)
            for (si = 0; SET[si]; si += 7)
                for (ni = 0; ni < 2; ni++)
                    for (pi = 0; pi < 5; pi++)
                        for (s6 = 0; s6 < 2; s6++)
                            for (sd = 0; sd < 1; sd++) {
                                int differ, k;
                                void *pa[22], *pb[22];
                                int csn = ni ? 'O' : 'C';
                                int cn2 = ni ? 'A' : 'K';

                                adjust_seed(g_a, SET[ci], SET[li], SET[si],
                                            csn, cn2, PREVS[pi], s6, sd);
                                orig(g_a);
                                adjust_seed(g_b, SET[ci], SET[li], SET[si],
                                            csn, cn2, PREVS[pi], s6, sd);
                                Track_Adjust(g_b);
                                n++;
                                for (k = 0; k < 22; k++) {
                                    pa[k] = g_a->trk_buf[k];
                                    pb[k] = g_b->trk_buf[k];
                                    g_a->trk_buf[k] = NULL;
                                    g_b->trk_buf[k] = NULL;
                                }
                                g_a->stage_ctx[3].cur = NULL;
                                g_b->stage_ctx[3].cur = NULL;
                                g_a->stage_ctx[3].ctl = NULL;
                                g_b->stage_ctx[3].ctl = NULL;
                                g_a->stage_ctx[3].scan = NULL;
                                g_b->stage_ctx[3].scan = NULL;
                                differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
                                for (k = 0; k < 22; k++) {
                                    g_a->trk_buf[k] = (uint8_t *)pa[k];
                                    g_b->trk_buf[k] = (uint8_t *)pb[k];
                                }
                                if (differ) {
                                    if (shown++ < 8) {
                                        size_t off;
                                        fprintf(stderr,
                                                "  Track_Adjust(cur %c ctl %c "
                                                "scan %c sn %c n2 %c prev %s "
                                                "s650 %d seed %d):",
                                                SET[ci], SET[li], SET[si],
                                                csn, cn2, PREVS[pi], s6, sd);
                                        /* the whole object, skipping only
                                         * trk_buf, whose pointers always
                                         * differ between two engines */
                                        for (off = 0; off < sizeof *g_a; off++)
                                            if (off >= 0x7130 && off < 0x7188)
                                                continue;
                                            else
                                            if (((uint8_t *)g_a)[off] !=
                                                ((uint8_t *)g_b)[off]) {
                                                size_t d = off & ~3u;
                                                fprintf(stderr,
                                                        " 0x%04x(%d/%d)",
                                                        (unsigned)d,
                                                        *(int32_t *)((uint8_t *)g_a + d),
                                                        *(int32_t *)((uint8_t *)g_b + d));
                                                off = (d | 3);
                                            }
                                        fprintf(stderr, "\n");
                                    }
                                    bad++;
                                }
                            }

    /* the control: almost every test here keys on a bit of g_10058618, so
     * moving that block has to change the answer somewhere */
    {
        uint8_t *fl = (uint8_t *)(uintptr_t)0x10058618;
        int caught, k;

        adjust_seed(g_a, 'A', 'L', 'A', 'C', 'K', "rRrP", 1, 3);
        orig(g_a);
        for (k = 0x100; k < 0x180; k++)
            fl[k] ^= 0xff;
        adjust_seed(g_b, 'A', 'L', 'A', 'C', 'K', "rRrP", 1, 3);
        orig(g_b);
        for (k = 0x100; k < 0x180; k++)
            fl[k] ^= 0xff;
        g_a->stage_ctx[3].cur = NULL;
        g_b->stage_ctx[3].cur = NULL;
        caught = memcmp(g_a->trk_param, g_b->trk_param,
                        sizeof g_a->trk_param) != 0;
        n++;
        if (!caught) {
            fprintf(stderr, "  the adjust harness does not see a moved flag "
                            "table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "track adjust", n - bad, n);
    return bad != 0;
}

/*
 * Track_Emit writes into one track buffer and reads seven trk_param cells,
 * so the sweep is over the mode that picks the writer, the track that picks
 * the scaling, and the values those writers take.  trk_rd is placed on both
 * sides of trk_wr so the "nothing buffered" case that drops bit 0 is
 * covered.
 *
 * The shape indices stay within the 21-entry contour table: past it both
 * sides read the same neighbouring data and would agree, but there is no
 * reason to walk off the end.
 */
typedef void(__thiscall *emit_t)(Engine *, int32_t);

static void emit_seed(Engine *e, int track, int mode, int count, int p2,
                      int p3, int rdoff, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2246822519u + 7u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        memset(e->trk_data[i], (uint8_t)(0x40 + i), 256);
        e->trk_wr[i] = 60 + i;
        e->trk_rd[i] = 60 + i + rdoff;
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0x1fff);
        }
    }
    e->trk_param[track][0] = mode;
    e->trk_param[track][1] = count;
    e->trk_param[track][2] = p2;
    e->trk_param[track][3] = p3;
}

static int unit_emit(void)
{
    emit_t orig = (emit_t)(uintptr_t)0x10017aa0;
    static const int COUNT[] = {0, 1, 5, 20};
    static const int P2[] = {0, 3, 20};
    static const int P3[] = {0, 1, 8, 30};
    static const int RD[] = {-5, 0, 3};
    int track, mode, ci, pi, qi, ri, sd, bad = 0, n = 0, shown = 0;

    if (alloc_engines())
        return 2;

    for (track = 8; track <= 18; track++)
        for (mode = 0; mode <= 8; mode++)
            for (ci = 0; ci < 4; ci++)
                for (pi = 0; pi < 3; pi++)
                    for (qi = 0; qi < 4; qi++)
                        for (ri = 0; ri < 3; ri++)
                            for (sd = 0; sd < 2; sd++) {
                                int differ, k;
                                void *pa[22], *pb[22];

                                emit_seed(g_a, track, mode, COUNT[ci], P2[pi],
                                          P3[qi], RD[ri], sd);
                                orig(g_a, track);
                                emit_seed(g_b, track, mode, COUNT[ci], P2[pi],
                                          P3[qi], RD[ri], sd);
                                Track_Emit(g_b, track);
                                n++;
                                for (k = 0; k < 22; k++) {
                                    pa[k] = g_a->trk_buf[k];
                                    pb[k] = g_b->trk_buf[k];
                                    g_a->trk_buf[k] = NULL;
                                    g_b->trk_buf[k] = NULL;
                                }
                                differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
                                for (k = 0; k < 22; k++) {
                                    g_a->trk_buf[k] = (uint8_t *)pa[k];
                                    g_b->trk_buf[k] = (uint8_t *)pb[k];
                                }
                                if (differ) {
                                    if (shown++ < 6)
                                        fprintf(stderr,
                                                "  Track_Emit(track %d mode %d "
                                                "count %d p2 %d p3 %d rd %+d "
                                                "seed %d) differs\n",
                                                track, mode, COUNT[ci], P2[pi],
                                                P3[qi], RD[ri], sd);
                                    bad++;
                                }
                            }

    /* the control: the mode is what picks the writer, so two different modes
     * on the same state have to leave different buffers */
    {
        int caught;

        emit_seed(g_a, 10, 3, 5, 3, 8, -5, 1);
        orig(g_a, 10);
        emit_seed(g_b, 10, 7, 5, 3, 8, -5, 1);
        orig(g_b, 10);
        caught = memcmp(g_a->trk_data[10], g_b->trk_data[10], 256) != 0;
        n++;
        if (!caught) {
            fprintf(stderr, "  the emit harness does not see the mode change "
                            "-- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "track emit", n - bad, n);
    return bad != 0;
}

/*
 * Stage3_Segment has two halves and the sweep has to reach both: the five
 * vowels take the Segment_Apply path, and the fourteen lowercase letters
 * whose class flag has bit 4 set take the three-track pass.  Everything
 * else falls out of the middle, which the rest of the control set covers.
 *
 * The seeds are kept in ranges the engine itself stays in: trk_param[t][3]
 * small enough that the Track_Fill run is a few buffer-lengths rather than
 * thousands, above zero so the trailing Ramp_Fill inside Segment_Apply gets
 * a non-negative count, and s3_1fe8 low enough that the blend shape index
 * stays inside the 21-entry contour table.
 */
typedef void(__thiscall *seg3_t)(Engine *);

static Node g_s3_cur, g_s3_prev, g_s3_ctl, g_s3_scan;
static Node g_s3_n[6], g_s3_p[4];

static void seg3_seed(Engine *e, int cctl, int ccur, int cscan, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2654435761u + 11u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        memset(e->trk_data[i], (uint8_t)(0x30 + i), 256);
        e->trk_wr[i] = 40 + i;
        e->trk_rd[i] = 30 + i;
        r = r * 1103515245u + 12345u;
        e->trk_490[i] = (int32_t)((r >> 16) & 0x7ff);
        r = r * 1103515245u + 12345u;
        e->trk_4e8[i] = (int32_t)((r >> 16) & 0x7fff);
        r = r * 1103515245u + 12345u;
        e->trk_55c[i] = (int32_t)((r >> 16) & 0x3ff);
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0x3ff);
        }
        e->trk_param[i][3] = 3 + (int32_t)((r >> 8) % 40u);
        e->trk_param[i][0] = (int32_t)((r >> 4) & 7u);
    }
    for (i = 0; i < 3; i++) {
        r = r * 1103515245u + 12345u;
        e->trk_540[i] = (int32_t)((r >> 16) & 0x3ff);
    }
    e->s3_468 = 4 + (seed * 7) % 30;
    e->s3_46c = 5;
    e->s3_1fe8 = 4 + (seed % 9);
    e->trans_len = 3;

    for (i = 0; i < 6; i++) {
        g_s3_n[i].next = &g_s3_n[i + 1 < 6 ? i + 1 : 5];
        g_s3_n[i].value = (uint8_t)("KASCLn"[i]);
    }
    for (i = 0; i < 4; i++) {
        g_s3_p[i].prev = &g_s3_p[i + 1 < 4 ? i + 1 : 3];
        g_s3_p[i].value = (uint8_t)("rRrP"[i]);
    }
    g_s3_cur.value = (uint8_t)ccur;
    g_s3_cur.prev = &g_s3_prev;
    g_s3_prev.value = 'A';
    g_s3_ctl.value = (uint8_t)cctl;
    g_s3_ctl.arg = 20;
    g_s3_ctl.next = &g_s3_n[0];
    g_s3_ctl.prev = &g_s3_p[0];
    g_s3_scan.value = (uint8_t)cscan;
    g_s3_scan.next = &g_s3_n[0];
    e->stage_ctx[3].cur = &g_s3_cur;
    e->stage_ctx[3].ctl = &g_s3_ctl;
    e->stage_ctx[3].scan = &g_s3_scan;
}

static int unit_seg3(void)
{
    seg3_t orig = (seg3_t)(uintptr_t)0x10015720;
    static const char CTL[] = "AEIOUaehijkmopqtuvwLMNRrbdS";
    static const char CUR[] = "ALKNMbgIU ";
    static const char SCAN[] = "LKACOn";
    int li, ci, si, sd, bad = 0, n = 0, shown = 0, loops = 0;

    if (alloc_engines())
        return 2;

    for (li = 0; CTL[li]; li++)
        for (ci = 0; CUR[ci]; ci++)
            for (si = 0; SCAN[si]; si++)
                for (sd = 0; sd < 4; sd++) {
                    int differ, k;
                    void *pa[22], *pb[22];

                    seg3_seed(g_a, CTL[li], CUR[ci], SCAN[si], sd);
                    orig(g_a);
                    seg3_seed(g_b, CTL[li], CUR[ci], SCAN[si], sd);
                    Stage3_Segment(g_b);
                    n++;
                    if (g_a->trk_wr[9] != 40 + 9 || g_a->trk_param[9][0] == 3)
                        loops++;
                    for (k = 0; k < 22; k++) {
                        pa[k] = g_a->trk_buf[k];
                        pb[k] = g_b->trk_buf[k];
                        g_a->trk_buf[k] = NULL;
                        g_b->trk_buf[k] = NULL;
                    }
                    g_a->stage_ctx[3].cur = NULL;
                    g_b->stage_ctx[3].cur = NULL;
                    g_a->stage_ctx[3].ctl = NULL;
                    g_b->stage_ctx[3].ctl = NULL;
                    g_a->stage_ctx[3].scan = NULL;
                    g_b->stage_ctx[3].scan = NULL;
                    differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
                    for (k = 0; k < 22; k++) {
                        g_a->trk_buf[k] = (uint8_t *)pa[k];
                        g_b->trk_buf[k] = (uint8_t *)pb[k];
                    }
                    if (differ) {
                        if (shown++ < 6) {
                            size_t off;
                            fprintf(stderr,
                                    "  Stage3_Segment(ctl %c cur %c scan %c "
                                    "seed %d):", CTL[li], CUR[ci], SCAN[si], sd);
                            for (off = 0; off < sizeof *g_a; off++)
                                if (off >= 0x7130 && off < 0x7188)
                                    continue;
                                else if (((uint8_t *)g_a)[off] !=
                                         ((uint8_t *)g_b)[off]) {
                                    size_t d = off & ~3u;
                                    fprintf(stderr, " 0x%04x(%d/%d)",
                                            (unsigned)d,
                                            *(int32_t *)((uint8_t *)g_a + d),
                                            *(int32_t *)((uint8_t *)g_b + d));
                                    off = (d | 3);
                                }
                            fprintf(stderr, "\n");
                        }
                        bad++;
                    }
                }

    /* the control: the divisor table drives both derived durations, so
     * moving it has to change the answer */
    {
        uint8_t *div = *(uint8_t **)(uintptr_t)0x10057d50;
        int caught, k;

        seg3_seed(g_a, 'k', 'L', 'L', 1);
        orig(g_a);
        for (k = 0x20; k < 0x7f; k++)
            div[k] = (uint8_t)(div[k] ? div[k] + 1 : 0);
        seg3_seed(g_b, 'k', 'L', 'L', 1);
        orig(g_b);
        for (k = 0x20; k < 0x7f; k++)
            div[k] = (uint8_t)(div[k] ? div[k] - 1 : 0);
        caught = memcmp(g_a->trk_data[9], g_b->trk_data[9], 256) != 0 ||
                 g_a->s3_468 != g_b->s3_468;
        g_a->stage_ctx[3].cur = NULL;
        g_b->stage_ctx[3].cur = NULL;
        n++;
        if (!caught) {
            fprintf(stderr, "  the seg3 harness does not see a moved divisor "
                            "table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "  (%d of %d reached the track pass)\n", loops, n - 1);
    fprintf(stderr, "%-18s %d/%d identical\n", "stage3 segment", n - bad, n);
    return bad != 0;
}

/*
 * The three extra correction passes together.  All three take only the
 * engine, read stage 3's three phonemes and write trk_param, trk_4e8 and
 * trk_rd, so one sweep over the phonemes with everything they touch seeded
 * covers all of them; each is compared on its own so a failure names which.
 */
typedef void(__thiscall *adj3_t)(Engine *);

static Node g_a3_cur, g_a3_ctl, g_a3_scan;

static void adj3_seed(Engine *e, int ccur, int cctl, int cscan, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2654435761u + 17u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_wr[i] = 40 + i * 3;
        e->trk_rd[i] = 25 + i * 2;
        r = r * 1103515245u + 12345u;
        e->trk_4e8[i] = (int32_t)((r >> 16) & 0x7fff);
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0x7ff);
        }
    }
    g_a3_cur.value = (uint8_t)ccur;
    g_a3_ctl.value = (uint8_t)cctl;
    g_a3_scan.value = (uint8_t)cscan;
    e->stage_ctx[3].cur = &g_a3_cur;
    e->stage_ctx[3].ctl = &g_a3_ctl;
    e->stage_ctx[3].scan = &g_a3_scan;
}

static int unit_adj3(void)
{
    static const struct {
        const char *name;
        uintptr_t addr;
        void (TV_THISCALL *ours)(Engine *);
    } WHICH[3] = {
        {"AdjustWeights", 0x10014f20, Track_AdjustWeights},
        {"AdjustBeforeR", 0x10016220, Track_AdjustBeforeR},
        {"AdjustGap", 0x100162b0, Track_AdjustGap},
    };
    static const char SET[] = "AEIOULMNnRrZCKBDbdg ~XYSTP";
    int w, ci, li, si, sd, bad = 0, n = 0, shown = 0;

    if (alloc_engines())
        return 2;

    for (w = 0; w < 3; w++) {
        adj3_t orig = (adj3_t)WHICH[w].addr;

        for (ci = 0; SET[ci]; ci++)
            for (li = 0; SET[li]; li++)
                for (si = 0; SET[si]; si += 2)
                    for (sd = 0; sd < 2; sd++) {
                        adj3_seed(g_a, SET[ci], SET[li], SET[si], sd);
                        orig(g_a);
                        adj3_seed(g_b, SET[ci], SET[li], SET[si], sd);
                        WHICH[w].ours(g_b);
                        n++;
                        g_a->stage_ctx[3].cur = NULL;
                        g_b->stage_ctx[3].cur = NULL;
                        g_a->stage_ctx[3].ctl = NULL;
                        g_b->stage_ctx[3].ctl = NULL;
                        g_a->stage_ctx[3].scan = NULL;
                        g_b->stage_ctx[3].scan = NULL;
                        if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
                            if (shown++ < 6)
                                fprintf(stderr,
                                        "  %s(cur %c ctl %c scan %c seed %d)"
                                        " differs\n", WHICH[w].name, SET[ci],
                                        SET[li], SET[si], sd);
                            bad++;
                        }
                    }
    }

    /* the control: all three key on bits of g_10058618, so moving the two
     * blocks they index has to change something */
    {
        uint8_t *fl = (uint8_t *)(uintptr_t)0x10058618;
        int caught = 0, k, i;

        for (i = 0; i < 3 && !caught; i++) {
            adj3_t orig = (adj3_t)WHICH[i].addr;

            adj3_seed(g_a, 'L', 'C', 'A', 1);
            orig(g_a);
            for (k = 0; k < 0x80; k++) {
                fl[k] ^= 0xff;
                fl[0x100 + k] ^= 0xff;
                fl[0x180 + k] ^= 0xff;
            }
            adj3_seed(g_b, 'L', 'C', 'A', 1);
            orig(g_b);
            for (k = 0; k < 0x80; k++) {
                fl[k] ^= 0xff;
                fl[0x100 + k] ^= 0xff;
                fl[0x180 + k] ^= 0xff;
            }
            g_a->stage_ctx[3].cur = NULL;
            g_b->stage_ctx[3].cur = NULL;
            caught = memcmp(g_a, g_b, sizeof *g_a) != 0;
        }
        n++;
        if (!caught) {
            fprintf(stderr, "  the adj3 harness does not see a moved flag "
                            "table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "adjust passes", n - bad, n);
    return bad != 0;
}

/*
 * Track_Couple and Track_SetModes.
 *
 * Couple writes only into tracks 10 and 11, so its sweep is over the start
 * position (including values that wrap the 256-byte buffer), the run length
 * either side of the count-of-one that does nothing, and gains spread over
 * the Q15 range.  SetModes reads the three phonemes and writes column 0 of
 * every track and column 2 of nine, so its sweep is over the phonemes.
 */
typedef void(__thiscall *couple_t)(Engine *, int32_t, int32_t, int32_t);
typedef void(__thiscall *setmodes_t)(Engine *);

static Node g_cm_cur, g_cm_ctl, g_cm_scan;

static void cm_seed(Engine *e, int ccur, int cctl, int cscan, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2654435761u + 23u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        for (j = 0; j < 256; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_data[i][j] = (uint8_t)(r >> 16);
        }
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0x7ff);
        }
    }
    g_cm_cur.value = (uint8_t)ccur;
    g_cm_ctl.value = (uint8_t)cctl;
    g_cm_scan.value = (uint8_t)cscan;
    e->stage_ctx[3].cur = &g_cm_cur;
    e->stage_ctx[3].ctl = &g_cm_ctl;
    e->stage_ctx[3].scan = &g_cm_scan;
}

static int cm_compare(const char *what, const char *detail, int *shown)
{
    int k, differ;
    void *pa[22], *pb[22];

    for (k = 0; k < 22; k++) {
        pa[k] = g_a->trk_buf[k];
        pb[k] = g_b->trk_buf[k];
        g_a->trk_buf[k] = NULL;
        g_b->trk_buf[k] = NULL;
    }
    g_a->stage_ctx[3].cur = NULL;
    g_b->stage_ctx[3].cur = NULL;
    g_a->stage_ctx[3].ctl = NULL;
    g_b->stage_ctx[3].ctl = NULL;
    g_a->stage_ctx[3].scan = NULL;
    g_b->stage_ctx[3].scan = NULL;
    differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
    for (k = 0; k < 22; k++) {
        g_a->trk_buf[k] = (uint8_t *)pa[k];
        g_b->trk_buf[k] = (uint8_t *)pb[k];
    }
    if (differ && (*shown)++ < 6)
        fprintf(stderr, "  %s(%s) differs\n", what, detail);
    return differ;
}

static int unit_couple(void)
{
    couple_t o_cpl = (couple_t)(uintptr_t)0x100179d0;
    setmodes_t o_sm = (setmodes_t)(uintptr_t)0x10016350;
    static const int POS[] = {0, 1, 200, 250, 255, 300};
    static const int CNT[] = {0, 1, 2, 9, 40, 300};
    static const int GAIN[] = {0, 1, 100, 0x2000, 0x7ffe, -500};
    static const char SET[] = "AEIOULMNnRrZCKBDbdg ~XYST";
    int pi, ci, gi, sd, li, ui, bad = 0, n = 0, shown = 0;
    char d[64];

    if (alloc_engines())
        return 2;

    for (pi = 0; pi < 6; pi++)
        for (ci = 0; ci < 6; ci++)
            for (gi = 0; gi < 6; gi++)
                for (sd = 0; sd < 3; sd++) {
                    cm_seed(g_a, 'A', 'K', 'S', sd);
                    o_cpl(g_a, POS[pi], CNT[ci], GAIN[gi]);
                    cm_seed(g_b, 'A', 'K', 'S', sd);
                    Track_Couple(g_b, POS[pi], CNT[ci], GAIN[gi]);
                    n++;
                    sprintf(d, "pos %d cnt %d gain %d seed %d", POS[pi],
                            CNT[ci], GAIN[gi], sd);
                    bad += cm_compare("Track_Couple", d, &shown);
                }

    for (li = 0; SET[li]; li++)
        for (ui = 0; SET[ui]; ui++)
            for (ci = 0; SET[ci]; ci += 3)
                for (sd = 0; sd < 2; sd++) {
                    cm_seed(g_a, SET[ui], SET[li], SET[ci], sd);
                    o_sm(g_a);
                    cm_seed(g_b, SET[ui], SET[li], SET[ci], sd);
                    Track_SetModes(g_b);
                    n++;
                    sprintf(d, "cur %c ctl %c scan %c seed %d", SET[ui],
                            SET[li], SET[ci], sd);
                    bad += cm_compare("Track_SetModes", d, &shown);
                }

    /* the control: Couple's per-step gains come out of g_10048940, and
     * SetModes' shape out of the two-level table, so moving either has to
     * change the answer */
    {
        int32_t *g = (int32_t *)(uintptr_t)0x10048940;
        uint8_t *t = (uint8_t *)(uintptr_t)0x100581e0;
        int caught, k;

        cm_seed(g_a, 'A', 'K', 'S', 1);
        o_cpl(g_a, 3, 40, 0x4000);
        for (k = 0; k < 64; k++)
            g[k] ^= 0x1111;
        cm_seed(g_b, 'A', 'K', 'S', 1);
        o_cpl(g_b, 3, 40, 0x4000);
        for (k = 0; k < 64; k++)
            g[k] ^= 0x1111;
        caught = memcmp(g_a->trk_data[10], g_b->trk_data[10], 256) != 0;
        n++;
        if (!caught) {
            fprintf(stderr, "  the couple harness does not see moved step "
                            "gains -- it is proving nothing\n");
            bad++;
        }
        cm_seed(g_a, 'A', 'K', 'S', 1);
        o_sm(g_a);
        for (k = 0; k < 256; k++)
            t[k] = (uint8_t)(t[k] + 1);
        cm_seed(g_b, 'A', 'K', 'S', 1);
        o_sm(g_b);
        for (k = 0; k < 256; k++)
            t[k] = (uint8_t)(t[k] - 1);
        caught = g_a->trk_param[10][2] != g_b->trk_param[10][2];
        g_a->stage_ctx[3].cur = NULL;
        g_b->stage_ctx[3].cur = NULL;
        n++;
        if (!caught) {
            fprintf(stderr, "  the setmodes harness does not see a moved "
                            "shape table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "couple+setmodes", n - bad, n);
    return bad != 0;
}

/*
 * Track_AdjustTrill and Track_SetShapes.
 *
 * Trill reaches its neighbours through Engine_StageNext and
 * Engine_StagePrev, which return NULL at the edges of the stage window --
 * and the caller dereferences the result unchecked, in the original as much
 * as here.  So the window's `last` is set past the nodes being swept and the
 * chain is closed on itself, which is the shape stage 3 actually has when
 * this runs.
 */
typedef void(__thiscall *ts_t)(Engine *);

static Node g_ts_cur, g_ts_ctl, g_ts_scan, g_ts_nx, g_ts_pv, g_ts_last;

static void ts_seed(Engine *e, int ccur, int cctl, int cscan, int cnx,
                    int cpv, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2654435761u + 29u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++)
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0x7ff);
        }
    g_ts_cur.value = (uint8_t)ccur;
    g_ts_ctl.value = (uint8_t)cctl;
    g_ts_scan.value = (uint8_t)cscan;
    g_ts_nx.value = (uint8_t)cnx;
    g_ts_pv.value = (uint8_t)cpv;
    g_ts_last.value = 'Z';
    g_ts_ctl.next = &g_ts_nx;
    g_ts_ctl.prev = &g_ts_pv;
    g_ts_nx.next = &g_ts_last;
    g_ts_pv.prev = &g_ts_last;
    e->stage_ctx[3].cur = &g_ts_cur;
    e->stage_ctx[3].ctl = &g_ts_ctl;
    e->stage_ctx[3].scan = &g_ts_scan;
    e->stage_ctx[3].last = &g_ts_last;
    e->stage = &e->stage_ctx[3];
}

static int unit_trillshapes(void)
{
    ts_t o_tr = (ts_t)(uintptr_t)0x10014fb0;
    ts_t o_sh = (ts_t)(uintptr_t)0x10015410;
    static const char SET[] = "RrAEIOULMNnZCKBDbdg ~XYST";
    int li, ui, ci, xi, sd, bad = 0, n = 0, shown = 0;

    if (alloc_engines())
        return 2;

    /* Trill: the control phoneme and both neighbours are what it keys on */
    for (li = 0; SET[li]; li++)
        for (xi = 0; SET[xi]; xi++)
            for (ci = 0; SET[ci]; ci += 4)
                for (sd = 0; sd < 2; sd++) {
                    ts_seed(g_a, 'A', SET[li], 'S', SET[xi], SET[ci], sd);
                    o_tr(g_a);
                    ts_seed(g_b, 'A', SET[li], 'S', SET[xi], SET[ci], sd);
                    Track_AdjustTrill(g_b);
                    n++;
                    g_a->stage = NULL;
                    g_b->stage = NULL;
                    if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
                        if (shown++ < 6)
                            fprintf(stderr,
                                    "  Track_AdjustTrill(ctl %c next %c prev %c"
                                    " seed %d) differs\n", SET[li], SET[xi],
                                    SET[ci], sd);
                        bad++;
                    }
                }

    /* Shapes: the three phonemes */
    for (li = 0; SET[li]; li++)
        for (ui = 0; SET[ui]; ui++)
            for (ci = 0; SET[ci]; ci += 3)
                for (sd = 0; sd < 2; sd++) {
                    ts_seed(g_a, SET[ui], SET[li], SET[ci], 'A', 'A', sd);
                    o_sh(g_a);
                    ts_seed(g_b, SET[ui], SET[li], SET[ci], 'A', 'A', sd);
                    Track_SetShapes(g_b);
                    n++;
                    g_a->stage = NULL;
                    g_b->stage = NULL;
                    if (memcmp(g_a, g_b, sizeof *g_a) != 0) {
                        if (shown++ < 6)
                            fprintf(stderr,
                                    "  Track_SetShapes(cur %c ctl %c scan %c"
                                    " seed %d) differs\n", SET[ui], SET[li],
                                    SET[ci], sd);
                        bad++;
                    }
                }

    /* the control: Shapes takes its shape from the same two-level table
     * Track_SetModes uses, so moving it has to change the answer */
    {
        uint8_t *t = (uint8_t *)(uintptr_t)0x100581e0;
        int caught, k;

        ts_seed(g_a, 'A', 'K', 'S', 'A', 'A', 1);
        o_sh(g_a);
        for (k = 0; k < 256; k++)
            t[k] = (uint8_t)(t[k] + 3);
        ts_seed(g_b, 'A', 'K', 'S', 'A', 'A', 1);
        o_sh(g_b);
        for (k = 0; k < 256; k++)
            t[k] = (uint8_t)(t[k] - 3);
        caught = g_a->trk_param[10][2] != g_b->trk_param[10][2];
        g_a->stage = NULL;
        g_b->stage = NULL;
        n++;
        if (!caught) {
            fprintf(stderr, "  the shapes harness does not see a moved shape "
                            "table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "trill+shapes", n - bad, n);
    return bad != 0;
}

/*
 * Track_BlendDelta and the three functions above it.
 *
 * BlendDelta does not clamp its contour index the way Extend does, so the
 * sweep keeps it inside the 21-entry table; past that the original reads off
 * the end too.  Shorten and AdjustPause move cursors and durations, so their
 * comparison is the whole object.
 */
typedef void(__thiscall *bd_t)(Engine *, uint8_t *, int32_t, int32_t, int32_t,
                               int32_t, int32_t);
typedef void(__thiscall *v_t)(Engine *);

static Node g_bd_cur, g_bd_ctl, g_bd_scan;

static void bd_seed(Engine *e, int ccur, int cctl, int cscan, int seed)
{
    int i, j;
    uint32_t r = (uint32_t)(seed * 2654435761u + 31u);

    memset(e, 0, sizeof *e);
    for (i = 0; i < 22; i++) {
        e->trk_buf[i] = e->trk_data[i];
        for (j = 0; j < 256; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_data[i][j] = (uint8_t)(r >> 16);
        }
        e->trk_wr[i] = 90 + i;
        e->trk_rd[i] = 40 + i;
        r = r * 1103515245u + 12345u;
        e->trk_490[i] = (int32_t)((r >> 16) & 0x7ff);
        r = r * 1103515245u + 12345u;
        e->trk_4e8[i] = (int32_t)((r >> 16) & 0x7fff);
        for (j = 0; j < 7; j++) {
            r = r * 1103515245u + 12345u;
            e->trk_param[i][j] = (int32_t)((r >> 16) & 0x7ff);
        }
    }
    e->s3_458 = 1 + (seed % 5);
    e->s3_478 = 40 + seed * 7;
    e->s3_47c = 0;
    g_bd_cur.value = (uint8_t)ccur;
    g_bd_ctl.value = (uint8_t)cctl;
    g_bd_scan.value = (uint8_t)cscan;
    e->stage_ctx[3].cur = &g_bd_cur;
    e->stage_ctx[3].ctl = &g_bd_ctl;
    e->stage_ctx[3].scan = &g_bd_scan;
}

static int bd_cmp(const char *what, const char *d, int *shown)
{
    int k, differ;
    void *pa[22], *pb[22];

    for (k = 0; k < 22; k++) {
        pa[k] = g_a->trk_buf[k];
        pb[k] = g_b->trk_buf[k];
        g_a->trk_buf[k] = NULL;
        g_b->trk_buf[k] = NULL;
    }
    g_a->stage_ctx[3].cur = NULL;
    g_b->stage_ctx[3].cur = NULL;
    g_a->stage_ctx[3].ctl = NULL;
    g_b->stage_ctx[3].ctl = NULL;
    g_a->stage_ctx[3].scan = NULL;
    g_b->stage_ctx[3].scan = NULL;
    differ = memcmp(g_a, g_b, sizeof *g_a) != 0;
    for (k = 0; k < 22; k++) {
        g_a->trk_buf[k] = (uint8_t *)pa[k];
        g_b->trk_buf[k] = (uint8_t *)pb[k];
    }
    if (differ && (*shown)++ < 8)
        fprintf(stderr, "  %s(%s) differs\n", what, d);
    return differ;
}

static int unit_blenddelta(void)
{
    bd_t o_bd = (bd_t)(uintptr_t)0x10017dc0;
    v_t o_sh = (v_t)(uintptr_t)0x10017e60;
    v_t o_pa = (v_t)(uintptr_t)0x10015090;
    v_t o_ve = (v_t)(uintptr_t)0x10010a30;
    static const int MODE[] = {0, 2, 3, 0xd};
    static const int POS[] = {0, 1, 100, 255, 300};
    static const int TCON[] = {0, 1, 3, 6, 10, 20};
    static const int N[] = {-2, 0, 1, 5, 40};
    static const int DELTA[] = {-300, -4, 0, 0x32, 5000};
    static const char SET[] = "AEIOULMNnRrZCKG~ pmtUbdgSTX";
    int mi, pi, ti, ni, di, li, ui, ci, sd, bad = 0, n = 0, shown = 0;
    char d[80];

    if (alloc_engines())
        return 2;

    for (mi = 0; mi < 4; mi++)
        for (pi = 0; pi < 5; pi++)
            for (ti = 0; ti < 6; ti++)
                for (ni = 0; ni < 5; ni++)
                    for (di = 0; di < 5; di++) {
                        bd_seed(g_a, 'A', 'K', 'S', 1);
                        o_bd(g_a, g_a->trk_data[2], MODE[mi], POS[pi],
                             TCON[ti], N[ni], DELTA[di]);
                        bd_seed(g_b, 'A', 'K', 'S', 1);
                        Track_BlendDelta(g_b, g_b->trk_data[2], MODE[mi],
                                         POS[pi], TCON[ti], N[ni], DELTA[di]);
                        n++;
                        sprintf(d, "mode %d pos %d tcon %d n %d delta %d",
                                MODE[mi], POS[pi], TCON[ti], N[ni], DELTA[di]);
                        bad += bd_cmp("Track_BlendDelta", d, &shown);
                    }

    for (li = 0; SET[li]; li++)
        for (ui = 0; SET[ui]; ui += 2)
            for (ci = 0; SET[ci]; ci += 5)
                for (sd = 0; sd < 3; sd++) {
                    int w;

                    for (w = 0; w < 3; w++) {
                        v_t o = w == 0 ? o_sh : (w == 1 ? o_pa : o_ve);
                        void (TV_THISCALL *mine)(Engine *) =
                            w == 0 ? Track_Shorten
                                   : (w == 1 ? Track_AdjustPause
                                             : Track_AdjustVelar);

                        bd_seed(g_a, SET[ui], SET[li], SET[ci], sd);
                        o(g_a);
                        bd_seed(g_b, SET[ui], SET[li], SET[ci], sd);
                        mine(g_b);
                        n++;
                        sprintf(d, "cur %c ctl %c scan %c seed %d", SET[ui],
                                SET[li], SET[ci], sd);
                        bad += bd_cmp(w == 0 ? "Track_Shorten"
                                             : (w == 1 ? "Track_AdjustPause"
                                                       : "Track_AdjustVelar"),
                                      d, &shown);
                    }
                }

    /* the control: the contour shapes the whole run, so moving one has to be
     * seen in a run long enough to reach it */
    {
        uint8_t *shape = (uint8_t *)((const uint8_t *const *)
                                     (uintptr_t)0x10058368)[10];
        int caught, k;

        bd_seed(g_a, 'A', 'K', 'S', 1);
        o_bd(g_a, g_a->trk_data[2], 2, 100, 10, 40, 0x40);
        for (k = 0; k < 10; k++)
            shape[k] = (uint8_t)(shape[k] ^ 0x3c);
        bd_seed(g_b, 'A', 'K', 'S', 1);
        o_bd(g_b, g_b->trk_data[2], 2, 100, 10, 40, 0x40);
        for (k = 0; k < 10; k++)
            shape[k] = (uint8_t)(shape[k] ^ 0x3c);
        caught = memcmp(g_a->trk_data[2], g_b->trk_data[2], 256) != 0;
        n++;
        if (!caught) {
            fprintf(stderr, "  the blenddelta harness does not see a changed "
                            "contour -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "blend delta", n - bad, n);
    return bad != 0;
}

/*: Track_Average reads the three phonemes and rewrites two columns of
 * eighteen tracks, so the sweep is over the phonemes with trk_490 and
 * trk_param seeded.  It reuses the blend-delta seeder. */
static int unit_average(void)
{
    v_t orig = (v_t)(uintptr_t)0x10011660;
    static const char SET[] = "AEIOULMNnRrZCKGDS~ pmtUbdgTX";
    int li, ui, ci, sd, bad = 0, n = 0, shown = 0;
    char d[80];

    if (alloc_engines())
        return 2;

    for (li = 0; SET[li]; li++)
        for (ui = 0; SET[ui]; ui++)
            for (ci = 0; SET[ci]; ci += 5)
                for (sd = 0; sd < 2; sd++) {
                    bd_seed(g_a, SET[ui], SET[li], SET[ci], sd);
                    orig(g_a);
                    bd_seed(g_b, SET[ui], SET[li], SET[ci], sd);
                    Track_Average(g_b);
                    n++;
                    sprintf(d, "cur %c ctl %c scan %c seed %d", SET[ui],
                            SET[li], SET[ci], sd);
                    bad += bd_cmp("Track_Average", d, &shown);
                }

    /* the control: the limit chain keys on a flag bit, so moving that block
     * has to change the answer */
    {
        uint8_t *fl = (uint8_t *)(uintptr_t)0x10058618;
        int caught, k;

        bd_seed(g_a, 'A', 'K', 'S', 1);
        orig(g_a);
        for (k = 0; k < 0x80; k++)
            fl[k] ^= 0xff;
        bd_seed(g_b, 'A', 'K', 'S', 1);
        orig(g_b);
        for (k = 0; k < 0x80; k++)
            fl[k] ^= 0xff;
        g_a->stage_ctx[3].cur = NULL;
        g_b->stage_ctx[3].cur = NULL;
        caught = memcmp(g_a->trk_param, g_b->trk_param,
                        sizeof g_a->trk_param) != 0;
        n++;
        if (!caught) {
            fprintf(stderr, "  the average harness does not see a moved flag "
                            "table -- it is proving nothing\n");
            bad++;
        }
    }

    fprintf(stderr, "%-18s %d/%d identical\n", "track average", n - bad, n);
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
    if (!strcmp(name, "input")) return unit_input();
    if (!strcmp(name, "nodealloc")) return unit_nodealloc();
    if (!strcmp(name, "list")) return unit_list();
    if (!strcmp(name, "escape")) return unit_escape();
    if (!strcmp(name, "util")) return unit_util();
    if (!strcmp(name, "lifecycle")) return unit_lifecycle();
    if (!strcmp(name, "control")) return unit_control();
    if (!strcmp(name, "stage4")) return unit_stage4();
    if (!strcmp(name, "scan")) return unit_scan();
    if (!strcmp(name, "params")) return unit_params();
    if (!strcmp(name, "reset")) return unit_reset();
    if (!strcmp(name, "rule")) return unit_rule();
    if (!strcmp(name, "number")) return unit_number();
    if (!strcmp(name, "synth")) return unit_synth();
    if (!strcmp(name, "bittab")) return unit_bittab();
    if (!strcmp(name, "variant")) return unit_variant();
    if (!strcmp(name, "cluster")) return unit_cluster();
    if (!strcmp(name, "ramp")) return unit_ramp();
    if (!strcmp(name, "trackset")) return unit_trackset();
    if (!strcmp(name, "extend")) return unit_extend();
    if (!strcmp(name, "contour")) return unit_contour();
    if (!strcmp(name, "segment")) return unit_segment();
    if (!strcmp(name, "adjust")) return unit_adjust();
    if (!strcmp(name, "emit")) return unit_emit();
    if (!strcmp(name, "seg3")) return unit_seg3();
    if (!strcmp(name, "adj3")) return unit_adj3();
    if (!strcmp(name, "couple")) return unit_couple();
    if (!strcmp(name, "trill")) return unit_trillshapes();
    if (!strcmp(name, "blenddelta")) return unit_blenddelta();
    if (!strcmp(name, "average")) return unit_average();
    if (!strcmp(name, "volumefull")) return unit_volume_full();
    if (!strcmp(name, "all"))
        return unit_rings() | unit_flush() | unit_putchar() | unit_input()
             | unit_nodealloc() | unit_list() | unit_escape() | unit_util()
             | unit_lifecycle() | unit_control() | unit_stage4() | unit_scan()
             | unit_params() | unit_reset() | unit_rule() | unit_number()
             | unit_synth()
             | unit_bittab()
             | unit_variant()
             | unit_cluster()
             | unit_ramp()
             | unit_trackset()
             | unit_extend()
             | unit_contour()
             | unit_segment()
             | unit_adjust()
             | unit_emit()
             | unit_seg3()
             | unit_adj3()
             | unit_couple()
             | unit_trillshapes()
             | unit_blenddelta()
             | unit_average();
    fprintf(stderr, "unknown unit test %s\n", name);
    return 2;
}
