/*
 * tvh - drive the original CGRM_EN.DLL engine object natively (32-bit).
 *
 * The DLL is mapped by our own loader with sandboxed imports (no registry,
 * no files, no windows).  We then replicate what the SAPI engine thread
 * (CGRM_EN sub_1002e240) does for a single ITTSCentral::TextData call:
 * construct the engine object, feed the text into its input ring, call the
 * step function until it reports idle, and collect every PCM buffer it
 * hands to the audio queue.
 *
 * usage: tvh [-v voice0-9] [-8] [-t] [-z nuls] [-L word=phonemes]
 *             <dll> <text|@file> <out.wav>
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sandbox.h"
#include "coverage.h"
#ifdef TV_WITH_HOOKS
#include "hooks.h"
#include "unit.h"
#endif

#define THISCALL __attribute__((thiscall))

/* ---- engine object methods (addresses in CGRM_EN.DLL) ------------------ */
#define VA_ENGINE_CTOR   0x10030fc0 /* this */
#define VA_ENGINE_INIT   0x1002c450 /* this */
#define VA_ENGINE_TEXTIN 0x10055ec0 /* this -> bool (TextIn helper object) */
#define VA_ENGINE_FEED   0x10055aa0 /* this, text, len, &pos */
#define VA_ENGINE_FLUSH  0x10055f50 /* this, flag */
#define VA_ENGINE_FREE   0x100281f0 /* this -> free bytes in input ring */
#define VA_ENGINE_STEP   0x1002c5a0 /* this -> flags: 1=idle, 2=pcm ready */
#define VA_SET_PITCH     0x1002c810
#define VA_SET_SPEED     0x1002c840
#define VA_SET_VOLUME    0x1002c870
#define VA_SET_VOICE     0x1002c8f0
#define VA_PITCH_TABLE   0x100b5350 /* uint16 per voice, stride 4 */
#define VA_SPEED_TABLE   0x100b53a0 /* uint32 per voice */
#define VA_LEXICON_CS    0x10148b38 /* CRITICAL_SECTION */
#define VA_LEX_ADD       0x10003d00 /* cdecl(word, pronunciation) */
#define VA_OBJECT_COUNT  0x100bf5f8 /* live SAPI object count */

#define ENGINE_SIZE 0x9200
#define SAPI_SIZE   0x1000
#define OUTBUF_SIZE 0x34bc

typedef void(THISCALL *m_v)(void *);
typedef int(THISCALL *m_i)(void *);
typedef void(THISCALL *m_vi)(void *, int);
typedef void(THISCALL *m_feed)(void *, const char *, uint32_t, uint32_t *);

#define U8(p, o)  (*(uint8_t *)((uint8_t *)(p) + (o)))
#define U16(p, o) (*(uint16_t *)((uint8_t *)(p) + (o)))
#define S16(p, o) (*(int16_t *)((uint8_t *)(p) + (o)))
#define U32(p, o) (*(uint32_t *)((uint8_t *)(p) + (o)))
#define PTR(p, o) (*(void **)((uint8_t *)(p) + (o)))

static pe_image g_img;
#define FN(type, va) ((type)pe_va(&g_img, (va)))

typedef struct {
    uint8_t *data;
    size_t len, cap;
} bytebuf;

static void bb_append(bytebuf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 4096;
        b->data = (uint8_t *)realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static int write_wav(const char *path, const uint8_t *pcm, uint32_t n, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t s;
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f);            /* PCM */
    s = 1; fwrite(&s, 2, 1, f);            /* mono */
    fwrite(&rate, 4, 1, f);
    v = rate * 2; fwrite(&v, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    fwrite(pcm, 1, n, f);
    fclose(f);
    return 0;
}

/* The SAPI audio queue object: { count, capacity(bytes), entries[] } where
 * each entry is { void *data, uint32 size }.  The engine pushes notification
 * records into it; we just drain them. */
typedef struct { uint32_t count, cap; uint32_t *entries; } sapi_queue;

static char *load_text(const char *arg, uint32_t *len)
{
    char *t;
    if (arg[0] == '@') {
        FILE *f = fopen(arg + 1, "rb");
        long n;
        if (!f) { fprintf(stderr, "cannot open %s\n", arg + 1); exit(1); }
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        t = (char *)malloc(n + 1);
        n = (long)fread(t, 1, n, f);
        t[n] = 0;
        fclose(f);
        *len = (uint32_t)n;
        return t;
    }
    *len = (uint32_t)strlen(arg);
    t = (char *)malloc(*len + 1);
    memcpy(t, arg, *len + 1);
    return t;
}

int main(int argc, char **argv)
{
    int voice = 0, phone = 0, i, trace = 0, nuls = 2;
    const char *cov_blocks = NULL, *cov_out = NULL, *hook_spec = "all", *unit = NULL;
    const char *lex_add[16];
    int n_lex = 0;
    long opt_pitch = -1, opt_speed = -1, opt_volume = -1;
    int opt_preformat = 1, opt_textin = 1;
    const char *dll, *textarg, *out;
    uint8_t *E, *S;
    sapi_queue *aq;
    char *text;
    uint32_t textlen, pos = 0, rate;
    int pending = 0, fed_all = 0, steps = 0, r;
    bytebuf pcm = {0};

    setvbuf(stderr, NULL, _IONBF, 0);
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) voice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-8")) phone = 1;
        else if (!strcmp(argv[i], "-t")) trace = 1;
        else if (!strcmp(argv[i], "-z") && i + 1 < argc) nuls = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) cov_blocks = argv[++i];
        else if (!strcmp(argv[i], "-C") && i + 1 < argc) cov_out = argv[++i];
        else if (!strcmp(argv[i], "-H") && i + 1 < argc) hook_spec = argv[++i];
        else if (!strcmp(argv[i], "-U") && i + 1 < argc) unit = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) opt_pitch = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) opt_speed = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-V") && i + 1 < argc) opt_volume = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-P0")) opt_preformat = 0;
        else if (!strcmp(argv[i], "-T0")) opt_textin = 0;
        else if (!strcmp(argv[i], "-L") && i + 1 < argc && n_lex < 16)
            lex_add[n_lex++] = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (argc - i != 3 || voice < 0 || voice > 9) {
        fprintf(stderr, "usage: tvh [-v voice0-9] [-8] [-t] [-z nuls]"
                        " [-L word=phonemes] <dll> <text|@file> <out.wav>\n");
        return 2;
    }
    dll = argv[i]; textarg = argv[i + 1]; out = argv[i + 2];

    fprintf(stderr, "loading %s\n", dll);
    if (sb_load(dll, &g_img) != 0)
        return 1;
    fprintf(stderr, "mapped at %p, calling DllMain\n", (void *)g_img.base);
    if (!pe_call_entry(&g_img, DLL_PROCESS_ATTACH)) {
        fprintf(stderr, "DllMain failed\n");
        return 1;
    }
    sb_set_trace(trace);
#ifdef TV_WITH_HOOKS
    if (g_img.base != (uint8_t *)(uintptr_t)g_img.pref_base) {
        fprintf(stderr, "hook build requires the DLL at its preferred base\n");
        return 1;
    }
    if (unit) {
        /* unit comparisons call the originals directly: no hooks */
        InitializeCriticalSection((CRITICAL_SECTION *)pe_va(&g_img, VA_LEXICON_CS));
        return unit_run(unit);
    }
    hooks_install(hook_spec, trace);
#else
    (void)hook_spec;
#endif
    if (cov_blocks) {
        if (cov_load(cov_blocks) != 0)
            return 1;
        cov_arm();
    }

    /* One-time globals the first SAPI object construction sets up
     * (sub_10052c40): the shared lexicon critical section and the live
     * object count.  sub_10052f90 then tries to load the user dictionary
     * english.dic; with no such file that is a no-op, so we skip it. */
    InitializeCriticalSection((CRITICAL_SECTION *)pe_va(&g_img, VA_LEXICON_CS));
    *(uint32_t *)pe_va(&g_img, VA_OBJECT_COUNT) = 1;

    /* User lexicon entries, as ITTSDialogs/the lexicon calls would add them
     * (sub_10003d00 copies and upper-cases the word itself). */
    for (i = 0; i < n_lex; i++) {
        void(__cdecl * add)(const char *, const char *) =
            (void(__cdecl *)(const char *, const char *))pe_va(&g_img, VA_LEX_ADD);
        char buf[256], *eq;
        strncpy(buf, lex_add[i], sizeof buf - 1);
        buf[sizeof buf - 1] = 0;
        eq = strchr(buf, '=');
        if (!eq) { fprintf(stderr, "bad -L %s\n", lex_add[i]); return 2; }
        *eq = 0;
        add(buf, eq + 1);
    }

    /* ---- fake SAPI central object, as ITTSEnum::Select leaves it -------- */
    S = (uint8_t *)calloc(1, SAPI_SIZE);
    aq = (sapi_queue *)calloc(1, sizeof *aq);
    InitializeCriticalSection((CRITICAL_SECTION *)(S + 0x3c));
    InitializeCriticalSection((CRITICAL_SECTION *)(S + 0xb6c));
    PTR(S, 0xb60) = aq;
    U32(S, 0x64) = (uint32_t)voice;
    rate = phone ? 8000 : 11025;
    U32(S, 0xb58) = rate;
    U32(S, 0xba8) = 1;
    U32(S, 0xb9c) = 0xffff;
    U32(S, 0xba0) = 0xffff;
    U16(S, 0xb90) = U16(S, 0xb92) = *(uint16_t *)pe_va(&g_img, VA_PITCH_TABLE + 4 * voice);
    U32(S, 0xb94) = U32(S, 0xb98) = *(uint32_t *)pe_va(&g_img, VA_SPEED_TABLE + 4 * voice);
    U32(S, 0xbd4) = (uint32_t)opt_preformat; /* registry "PreFormat" (default on) */
    U32(S, 0xbd8) = (uint32_t)opt_textin;    /* registry "TextIn" (default on) */
    /* as if the application had called ITTSAttributes::Pitch/Speed/VolumeSet */
    if (opt_pitch >= 0) U16(S, 0xb90) = (uint16_t)opt_pitch;
    if (opt_speed >= 0) U32(S, 0xb94) = (uint32_t)opt_speed;
    if (opt_volume >= 0) U32(S, 0xb9c) = (uint32_t)opt_volume;

    text = load_text(textarg, &textlen);
    /* ITTSCentral::TextData (sub_10054000) converts the caller's Unicode text
     * with WideCharToMultiByte and enqueues it together with one extra NUL of
     * its own.  SAPI SDK callers conventionally include their terminating
     * L'\0' in SDATA.dwSize, so the engine sees two trailing NULs.  The
     * count matters: the feed routine branches on total length (0x28, 0x82). */
    text = (char *)realloc(text, textlen + nuls + 1);
    memset(text + textlen, 0, nuls + 1);
    textlen += nuls;

    /* ---- engine thread prologue (sub_1002e240) ------------------------- */
    E = (uint8_t *)VirtualAlloc(NULL, ENGINE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    FN(m_v, VA_ENGINE_CTOR)(E);
    U16(E, 0x212e) = 1;
    U16(E, 0x212c) = 0;
    U16(E, 0x2130) = 1;
    PTR(E, 0x20f4) = S;
    U16(E, 0x210c) = (uint16_t)U32(S, 0xb58);
    if (U32(S, 0xba8) != 0 && U32(S, 0xba8) < 5)
        U16(E, 0x2104) = (uint16_t)U32(S, 0xba8);
    FN(m_v, VA_ENGINE_INIT)(E);
    U8(E, 0x20ec) = (uint8_t)U32(S, 0xbd4);
    U8(E, 0x20ee) = (uint8_t)U32(S, 0xbd8);
    PTR(E, 0x2128) = GlobalAlloc(GPTR, OUTBUF_SIZE);
    U32(E, 0x2124) = 0;
    if (U8(E, 0x20ee))
        FN(m_i, VA_ENGINE_TEXTIN)(E);

    /* ---- main loop ------------------------------------------------------ */
    for (;;) {
        int flush = -1;
        if (U8(E, 0x2112) && U8(E, 0x2111)) {
            if (U8(E, 0x20ed)) {
                /* take the next queued TextData item */
                if (fed_all) {
                    if (!pending)
                        break; /* queue empty and engine idle: thread exits */
                    goto params;
                }
                pos = 0;
                U8(E, 0x20ed) = 0;
                U32(E, 0x2138) = 0; /* item flags (PostMessage 0x4ca payload) */
                FN(m_feed, VA_ENGINE_FEED)(E, text, textlen, &pos);
                if (U8(E, 0x20ed))
                    fed_all = 1;
                flush = 1;
            }
        }
        if (flush < 0) {
            if (!U8(E, 0x20ed) && FN(m_i, VA_ENGINE_FREE)(E) > 0x800) {
                FN(m_feed, VA_ENGINE_FEED)(E, text, textlen, &pos);
                if (U8(E, 0x20ed))
                    fed_all = 1;
            } else if (U8(E, 0x2112)) {
                flush = 0;
            }
        }
        if (flush >= 0) {
            FN(m_vi, VA_ENGINE_FLUSH)(E, flush);
            pending = 1;
        }
    params:
        if (U32(E, 0x20f8) != U16(S, 0xb90)) {
            U32(E, 0x20f8) = U16(S, 0xb90);
            FN(m_vi, VA_SET_PITCH)(E, (int)U32(E, 0x20f8));
        }
        if (U32(E, 0x20fc) != U32(S, 0xb94)) {
            U32(E, 0x20fc) = U32(S, 0xb94);
            FN(m_vi, VA_SET_SPEED)(E, (int)U32(E, 0x20fc));
        }
        if (U32(E, 0x2100) != U32(S, 0xb9c)) {
            U32(E, 0x2100) = U32(S, 0xb9c);
            FN(m_vi, VA_SET_VOLUME)(E, (int)U32(E, 0x2100));
        }
        if (U32(E, 0x2108) != U32(S, 0xbac))
            U32(E, 0x2108) = U32(S, 0xbac);
        if ((int)S16(E, 0x210e) != (int)U32(S, 0x64)) {
            U16(E, 0x210e) = (uint16_t)U32(S, 0x64);
            FN(m_vi, VA_SET_VOICE)(E, (int)S16(E, 0x210e));
        }
        r = FN(m_i, VA_ENGINE_STEP)(E);
        steps++;
        if (r & 2) {
            bb_append(&pcm, PTR(E, 0x2128), U32(E, 0x2124));
            U32(E, 0x2124) = 0;
        }
        if (r & 1)
            pending = 0;
        /* drop notification records the engine queued for the audio thread */
        EnterCriticalSection((CRITICAL_SECTION *)(S + 0xb6c));
        aq->count = 0;
        LeaveCriticalSection((CRITICAL_SECTION *)(S + 0xb6c));
        if (steps > 10000000) {
            fprintf(stderr, "runaway loop\n");
            break;
        }
    }

    fprintf(stderr, "steps=%d pcm=%u bytes (%.2f s @ %u Hz)\n", steps, (unsigned)pcm.len,
            pcm.len / 2.0 / rate, rate);
    if (trace)
        sb_report();
    if (cov_blocks && cov_out)
        cov_write(cov_out);
    return write_wav(out, pcm.data, (uint32_t)pcm.len, rate) ? 1 : 0;
}
