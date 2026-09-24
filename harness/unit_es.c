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
    if (!strcmp(name, "all"))
        return unit_rings() | unit_flush() | unit_putchar() | unit_input()
             | unit_nodealloc() | unit_list() | unit_escape() | unit_util()
             | unit_lifecycle() | unit_control() | unit_stage4() | unit_scan();
    fprintf(stderr, "unknown unit test %s\n", name);
    return 2;
}
