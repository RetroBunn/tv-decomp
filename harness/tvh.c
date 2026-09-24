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
 * usage: tvh [-v voice0-9] [-8] [-t] [-i] [-z nuls] [-e en|es]
 *             [-L word=phonemes] <dll> <text|@file> <out.wav>
 *
 * -i queues one TextData item per line instead of one for the whole text,
 * which is what a SAPI caller speaking a multi-line document does.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sandbox.h"
#include "coverage.h"
#ifdef TV_WITH_HOOKS
#include "hooks.h"
#ifndef TV_NO_UNIT
#include "unit.h"
#endif
#endif

#define THISCALL __attribute__((thiscall))

/* ---- per-engine addresses ---------------------------------------------- */
/* Two generations of the engine ship in these DLLs: American English is the
 * October 1997 build, every other language is November 1995.  They are
 * separate decompilations, but they are driven identically -- the same call
 * sequence, and the same SAPI central object layout -- so all the harness
 * needs is where each one keeps things.  How the Spanish addresses and
 * offsets were established is written up in docs/SPANISH.md, and the field
 * offsets are listed in es/engine.fields. */
typedef struct {
    const char *name;
    /* engine object methods */
    uint32_t ctor, init, textin, feed, flush, free_ring, step;
    uint32_t set_pitch, set_speed, set_volume, set_voice;
    /* module data */
    uint32_t pitch_table;  /* uint16 per voice, stride 4 */
    uint32_t speed_table;  /* uint32 per voice */
    uint32_t lexicon_cs;   /* CRITICAL_SECTION */
    uint32_t lex_add;      /* cdecl(word, pronunciation); 0 when not located */
    uint32_t object_count; /* live SAPI object count; 0 when not located */
    /* engine object fields */
    uint32_t o_preformat, o_item_done, o_textin_on, o_sapi;
    uint32_t o_cur_pitch, o_cur_speed, o_cur_volume, o_fmt, o_cur_bac;
    uint32_t o_sample_rate, o_cur_voice, o_st_input_empty, o_st_idle;
    uint32_t o_out_count, o_out_buf;
    uint32_t o_w_212c, o_w_212e, o_w_2130, o_item_notify;
} tv_abi;

/* CGRM_EN.DLL, the October 1997 American English engine. */
static const tv_abi abi_en = {
    "en",
    0x10030fc0, 0x1002c450, 0x10055ec0, 0x10055aa0, 0x10055f50, 0x100281f0, 0x1002c5a0,
    0x1002c810, 0x1002c840, 0x1002c870, 0x1002c8f0,
    0x100b5350, 0x100b53a0, 0x10148b38, 0x10003d00, 0x100bf5f8,
    0x20ec, 0x20ed, 0x20ee, 0x20f4,
    0x20f8, 0x20fc, 0x2100, 0x2104, 0x2108,
    0x210c, 0x210e, 0x2111, 0x2112,
    0x2124, 0x2128,
    0x212c, 0x212e, 0x2130, 0x2138,
};

/* CGRM_ES.DLL, the November 1995 Spanish engine.  The live object count has
 * not been located in it; it is skipped when zero, and the engine renders
 * without it. */
static const tv_abi abi_es = {
    "es",
    0x1000ddc0, 0x100086a0, 0x1001c6c0, 0x1001c310, 0x1001c710, 0x1000e5a0, 0x100087d0,
    0x10008a40, 0x10008a70, 0x10008aa0, 0x10008b10,
    0x1004c828, 0x1004c878, 0x10037c68, 0x10001370, 0,
    0x704, 0x705, 0x706, 0x70c,
    0x710, 0x714, 0x718, 0x1d8, 0x71c,
    0x720, 0x722, 0x725, 0x726,
    0x738, 0x73c,
    0x740, 0x742, 0x744, 0x748,
};

static const tv_abi *A = &abi_en;

/* Pick the engine from a voice name only that engine has.  Both tables are
 * plain ASCII in .data, so this reads as the DLL saying which one it is. */
static const tv_abi *abi_detect(pe_image *img)
{
    static const struct { const char *mark; const tv_abi *abi; } known[] = {
        {"Peter", &abi_en}, {"Pedro", &abi_es},
    };
    size_t k;
    uint32_t i;
    for (k = 0; k < sizeof known / sizeof known[0]; k++) {
        size_t len = strlen(known[k].mark);
        for (i = 0; i + len < img->size; i++)
            if (memcmp(img->base + i, known[k].mark, len + 1) == 0)
                return known[k].abi;
    }
    return NULL;
}

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
    long opt_pitch = -1, opt_speed = -1, opt_volume = -1, opt_textin_mode = 0;
    int opt_preformat = 1, opt_textin = 1;
    const char *dll, *textarg, *out, *eng = NULL;
    uint8_t *E, *S;
    sapi_queue *aq;
    char *text;
    char *item[256];
    uint32_t itemlen[256];
    int n_items = 0, cur = 0, split = 0;
    uint32_t textlen, pos = 0, rate;
    int pending = 0, fed_all = 0, steps = 0, r;
    bytebuf pcm = {0};

    setvbuf(stderr, NULL, _IONBF, 0);
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) voice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-8")) phone = 1;
        else if (!strcmp(argv[i], "-i")) split = 1;
        else if (!strcmp(argv[i], "-M") && i + 1 < argc)
            opt_textin_mode = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-t")) trace = 1;
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) eng = argv[++i];
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
        fprintf(stderr, "usage: tvh [-v voice0-9] [-8] [-t] [-i] [-z nuls]"
                        " [-e en|es] [-M textin-mode] [-L word=phonemes]"
                        " <dll> <text|@file> <out.wav>\n");
        return 2;
    }
    dll = argv[i]; textarg = argv[i + 1]; out = argv[i + 2];

    fprintf(stderr, "loading %s\n", dll);
    if (sb_load(dll, &g_img) != 0)
        return 1;
    if (eng) {
        if (!strcmp(eng, "en")) A = &abi_en;
        else if (!strcmp(eng, "es")) A = &abi_es;
        else { fprintf(stderr, "unknown engine %s\n", eng); return 2; }
    } else if ((A = abi_detect(&g_img)) == NULL) {
        fprintf(stderr, "cannot tell which engine %s is; pass -e en|es\n", dll);
        return 1;
    }
    fprintf(stderr, "mapped at %p, %s engine, calling DllMain\n",
            (void *)g_img.base, A->name);
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
#ifndef TV_NO_UNIT
    if (unit) {
        /* unit comparisons call the originals directly: no hooks */
        InitializeCriticalSection((CRITICAL_SECTION *)pe_va(&g_img, A->lexicon_cs));
        return unit_run(unit);
    }
#endif
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
    InitializeCriticalSection((CRITICAL_SECTION *)pe_va(&g_img, A->lexicon_cs));
    if (A->object_count)
        *(uint32_t *)pe_va(&g_img, A->object_count) = 1;

    /* User lexicon entries, as ITTSDialogs/the lexicon calls would add them
     * (sub_10003d00 copies and upper-cases the word itself). */
    if (n_lex && !A->lex_add) {
        fprintf(stderr, "-L is not supported for the %s engine yet\n", A->name);
        return 2;
    }
    for (i = 0; i < n_lex; i++) {
        void(__cdecl * add)(const char *, const char *) =
            (void(__cdecl *)(const char *, const char *))pe_va(&g_img, A->lex_add);
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
    U16(S, 0xb90) = U16(S, 0xb92) = *(uint16_t *)pe_va(&g_img, A->pitch_table + 4 * voice);
    U32(S, 0xb94) = U32(S, 0xb98) = *(uint32_t *)pe_va(&g_img, A->speed_table + 4 * voice);
    U32(S, 0xbd4) = (uint32_t)opt_preformat; /* registry "PreFormat" (default on) */
    U32(S, 0xbd8) = (uint32_t)opt_textin;    /* registry "TextIn" (default on) */
    /* The tokenizer mode TextIn_Construct is handed.  The 1997 engine passes
     * a literal 0 and ignores this; the 1995 engines read it, and mode 4 is
     * a branch of TextIn_Tokenize that no input can otherwise reach. */
    U32(S, 0xbb0) = (uint32_t)opt_textin_mode;
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
    if (!split) {
        text = (char *)realloc(text, textlen + nuls + 1);
        memset(text + textlen, 0, nuls + 1);
        item[0] = text;
        itemlen[0] = textlen + nuls;
        n_items = 1;
    } else {
        /* One item per line, cut at the end of each line's text rather than
         * after its terminator, so every item but the first begins with the
         * CR/LF that preceded it.  That is where a SAPI caller speaking a
         * document puts the break -- see docs/SPANISH.md, where it is what
         * reproduces spanish_test.wav exactly -- and it matters, because the
         * leading newline is worth 0.43 s of silence and shifts the pitch
         * contour of everything after it. */
        uint32_t a = 0, b, e = 0;
        while (e < textlen && n_items < 256) {
            for (b = e; b < textlen && text[b] != '\n'; b++)
                ;
            e = b;
            while (e > a && (text[e - 1] == '\r' || text[e - 1] == ' '))
                e--;
            if (e > a) {
                char *t = (char *)malloc(e - a + nuls + 1);
                memcpy(t, text + a, e - a);
                memset(t + (e - a), 0, nuls + 1);
                item[n_items] = t;
                itemlen[n_items] = (e - a) + nuls;
                n_items++;
                a = e;
            }
            e = b + 1;
        }
        /* Anything after the last line's text is terminator and trailing
         * blanks; a caller does not speak those on their own. */
        if (!n_items) {
            fprintf(stderr, "no text to speak\n");
            return 2;
        }
        fprintf(stderr, "%d items\n", n_items);
    }

    /* ---- engine thread prologue (sub_1002e240) ------------------------- */
    E = (uint8_t *)VirtualAlloc(NULL, ENGINE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    FN(m_v, A->ctor)(E);
    U16(E, A->o_w_212e) = 1;
    U16(E, A->o_w_212c) = 0;
    U16(E, A->o_w_2130) = 1;
    PTR(E, A->o_sapi) = S;
    U16(E, A->o_sample_rate) = (uint16_t)U32(S, 0xb58);
    if (U32(S, 0xba8) != 0 && U32(S, 0xba8) < 5)
        U16(E, A->o_fmt) = (uint16_t)U32(S, 0xba8);
    FN(m_v, A->init)(E);
    U8(E, A->o_preformat) = (uint8_t)U32(S, 0xbd4);
    U8(E, A->o_textin_on) = (uint8_t)U32(S, 0xbd8);
    PTR(E, A->o_out_buf) = GlobalAlloc(GPTR, OUTBUF_SIZE);
    U32(E, A->o_out_count) = 0;
    if (U8(E, A->o_textin_on))
        FN(m_i, A->textin)(E);

    /* ---- main loop ------------------------------------------------------ */
    for (;;) {
        int flush = -1;
        if (U8(E, A->o_st_idle) && U8(E, A->o_st_input_empty)) {
            if (U8(E, A->o_item_done)) {
                /* take the next queued TextData item */
                if (fed_all) {
                    if (!pending)
                        break; /* queue empty and engine idle: thread exits */
                    goto params;
                }
                pos = 0;
                U8(E, A->o_item_done) = 0;
                U32(E, A->o_item_notify) = 0; /* item flags (PostMessage 0x4ca payload) */
                FN(m_feed, A->feed)(E, item[cur], itemlen[cur], &pos);
                if (U8(E, A->o_item_done) && ++cur >= n_items)
                    fed_all = 1;
                flush = 1;
            }
        }
        if (flush < 0) {
            if (!U8(E, A->o_item_done) && FN(m_i, A->free_ring)(E) > 0x800) {
                FN(m_feed, A->feed)(E, item[cur], itemlen[cur], &pos);
                if (U8(E, A->o_item_done) && ++cur >= n_items)
                    fed_all = 1;
            } else if (U8(E, A->o_st_idle)) {
                flush = 0;
            }
        }
        if (flush >= 0) {
            FN(m_vi, A->flush)(E, flush);
            pending = 1;
        }
    params:
        if (U32(E, A->o_cur_pitch) != U16(S, 0xb90)) {
            U32(E, A->o_cur_pitch) = U16(S, 0xb90);
            FN(m_vi, A->set_pitch)(E, (int)U32(E, A->o_cur_pitch));
        }
        if (U32(E, A->o_cur_speed) != U32(S, 0xb94)) {
            U32(E, A->o_cur_speed) = U32(S, 0xb94);
            FN(m_vi, A->set_speed)(E, (int)U32(E, A->o_cur_speed));
        }
        if (U32(E, A->o_cur_volume) != U32(S, 0xb9c)) {
            U32(E, A->o_cur_volume) = U32(S, 0xb9c);
            FN(m_vi, A->set_volume)(E, (int)U32(E, A->o_cur_volume));
        }
        if (U32(E, A->o_cur_bac) != U32(S, 0xbac))
            U32(E, A->o_cur_bac) = U32(S, 0xbac);
        if ((int)S16(E, A->o_cur_voice) != (int)U32(S, 0x64)) {
            U16(E, A->o_cur_voice) = (uint16_t)U32(S, 0x64);
            FN(m_vi, A->set_voice)(E, (int)S16(E, A->o_cur_voice));
        }
        r = FN(m_i, A->step)(E);
        steps++;
        if (r & 2) {
            bb_append(&pcm, PTR(E, A->o_out_buf), U32(E, A->o_out_count));
            U32(E, A->o_out_count) = 0;
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
