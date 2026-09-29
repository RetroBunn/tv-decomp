/*
 * OpenTV as a SAPI 5 voice.
 *
 * A COM in-process server holding one class -- the TruVoice engine -- and one
 * token per voice, so every SAPI application sees twenty voices and picks one
 * the way it picks any other.  The engine itself is linked in statically: a
 * COM server is loaded by full path and its directory is not on the loader's
 * search path for dependents, so a tvsapi.dll that needed tvtts.dll beside it
 * would load in some hosts and not others.  This one needs nothing beside it.
 *
 * What SAPI asks of an engine is small -- implement ISpTTSEngine, be created
 * through ISpObjectWithToken -- but two of its details are silent traps, and
 * both are documented where they are relied on: Speak precedes GetOutputFormat
 * in the vtable (sapi_ddk.h), and a bookmark fragment's text is the bookmark's
 * name rather than words to say (speak_text below).
 *
 * Threading is "Both", and every engine object owns its own tvtts_synth, which
 * is what tvtts.h asks of a caller that uses one from more than one thread.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <olectl.h>   /* SELFREG_E_CLASS */
#include <stddef.h>

#include "sapi_ddk.h"
#include "tvtts.h"

/* {C9B0B616-4C0F-45AC-B435-DE5866E75B2E} -- OpenTV's own, generated for it. */
DEFINE_GUID(CLSID_OpenTVEngine,
            0xc9b0b616, 0x4c0f, 0x45ac, 0xb4,0x35, 0xde,0x58,0x66,0xe7,0x5b,0x2e);

/* Where SAPI keeps its voices.  mingw's sapi.h spells the same path as
 * SPCAT_VOICES, but with a "HKEY_LOCAL_MACHINE\\" on the front, which is the
 * form SAPI's own token API takes rather than the one RegCreateKeyEx wants. */
#define TV_TOKENS_KEY   L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens"

#define TV_TOKEN_VALUE  L"OpenTVVoice"   /* our own value in each token */
#define TV_VENDOR       L"OpenTV"
#define TV_SERVER_NAME  L"OpenTV speech engine"

static LONG      g_objects;
static HINSTANCE g_instance;

/* ---- small helpers, so the CRT is not needed ------------------------------ */
/*
 * The 32-bit build of this project is freestanding -- there is no 32-bit libc
 * in the toolchain -- so everything here comes from kernel32 or is written out.
 */

static int guid_eq(const GUID *a, const GUID *b)
{
    const BYTE *p = (const BYTE *)a, *q = (const BYTE *)b;
    int i;
    for (i = 0; i < (int)sizeof(GUID); i++)
        if (p[i] != q[i])
            return 0;
    return 1;
}

static void *tv_alloc(SIZE_T n)
{
    return HeapAlloc(GetProcessHeap(), 0, n);
}

static void *tv_realloc(void *p, SIZE_T n)
{
    return p ? HeapReAlloc(GetProcessHeap(), 0, p, n)
             : HeapAlloc(GetProcessHeap(), 0, n);
}

static void tv_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* A growable UTF-16 buffer.  The engine is fed one string per utterance. */
typedef struct {
    WCHAR *p;
    ULONG  len;
    ULONG  cap;
    int    bad;      /* an allocation failed; stop appending */
} WBuf;

static void wbuf_free(WBuf *b)
{
    tv_free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

static int wbuf_room(WBuf *b, ULONG extra)
{
    ULONG want;
    WCHAR *q;
    if (b->bad)
        return 0;
    if (b->len + extra + 1 <= b->cap)
        return 1;
    want = b->cap ? b->cap * 2 : 256;
    while (want < b->len + extra + 1)
        want *= 2;
    q = (WCHAR *)tv_realloc(b->p, want * sizeof(WCHAR));
    if (!q) {
        b->bad = 1;
        return 0;
    }
    b->p = q;
    b->cap = want;
    return 1;
}

static void wbuf_add(WBuf *b, const WCHAR *s, ULONG n)
{
    ULONG i;
    if (!n || !wbuf_room(b, n))
        return;
    for (i = 0; i < n; i++)
        b->p[b->len + i] = s[i];
    b->len += n;
    b->p[b->len] = 0;
}

static void wbuf_add_ascii(WBuf *b, const char *s)
{
    ULONG n = 0;
    while (s[n])
        n++;
    if (!n || !wbuf_room(b, n))
        return;
    for (ULONG i = 0; i < n; i++)
        b->p[b->len + i] = (WCHAR)(unsigned char)s[i];
    b->len += n;
    b->p[b->len] = 0;
}

static void wbuf_add_ch(WBuf *b, WCHAR c)
{
    if (wbuf_room(b, 1)) {
        b->p[b->len++] = c;
        b->p[b->len] = 0;
    }
}

/* ---- the voice table ------------------------------------------------------ */
/*
 * The library numbers voices across every language it carries (English 0..9,
 * then Spanish), and SAPI wants one token per voice with a gender, an age and
 * a language on it.  Two of those the engine does not record, so they are
 * derived from the position a voice holds within its own language, which is
 * the same in all five of the original's DLLs: the last two voices are the
 * female ones and voice 5 is the elderly one -- Grandpa Amos in English, Opa
 * in German, Ezequiel in Spanish.  docs/VOICES.md establishes both.
 */

static int voice_index_in_language(int voice)
{
    const char *lang = tvtts_voice_language(voice);
    int i, first = 0;
    if (!lang)
        return voice;
    for (i = 0; i < voice; i++) {
        const char *other = tvtts_voice_language(i);
        if (!other || other[0] != lang[0] || other[1] != lang[1])
            first = i + 1;
    }
    return voice - first;
}

static const WCHAR *voice_gender(int voice)
{
    return voice_index_in_language(voice) >= 8 ? L"Female" : L"Male";
}

static const WCHAR *voice_age(int voice)
{
    return voice_index_in_language(voice) == 5 ? L"Senior" : L"Adult";
}

/*
 * The LANGID a token advertises, as a hexadecimal string, which is how SAPI
 * stores it.  The codes are the ones each original DLL states in its own
 * version resource -- see tvtts_language_name.
 */
static const WCHAR *voice_langid(int voice)
{
    const char *lang = tvtts_voice_language(voice);
    if (lang && lang[0] == 'e' && lang[1] == 's')
        return L"40a";
    return L"409";
}

/* ---- parameter mapping ---------------------------------------------------- */
/*
 * SAPI gives rate and pitch as -10..+10 around the voice's own default, and
 * volume as 0..100.  The NVDA driver already maps this engine onto a slider
 * (nvda-addon/synthDrivers/opentv.py) and the two should not disagree, so
 * the same two scales are used here: rate is linear to the ends of the
 * engine's range, pitch is logarithmic.
 *
 * Twenty logarithmic steps from 50 to 500 is a ratio of 10^(1/20) each, so
 * each SAPI step multiplies the pitch by about 1.122.  The table is that
 * ratio per step in thousandths, which keeps this integer -- there is no libm
 * in the freestanding 32-bit build.
 */
static const int k_pitch_step[21] = {
     316,  355,  398,  447,  501,  562,  631,
     708,  794,  891, 1000, 1122, 1259, 1413,
    1585, 1778, 1995, 2239, 2512, 2818, 3162
};

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int map_pitch(int base, long adj)
{
    int n = clampi((int)adj, -10, 10);
    int v = base * k_pitch_step[n + 10] / 1000;
    return clampi(v, TVTTS_PITCH_MIN, TVTTS_PITCH_MAX);
}

static int map_rate(int base, long adj)
{
    int n = clampi((int)adj, -10, 10);
    if (n == 0)
        return base;
    if (n < 0)
        return base + (base - TVTTS_RATE_MIN) * n / 10;
    return base + (TVTTS_RATE_MAX_EXT - base) * n / 10;
}

/* ---- the engine object ---------------------------------------------------- */

typedef struct Engine Engine;

struct Engine {
    ISpTTSEngine        engine;
    ISpObjectWithToken  with_token;
    LONG                refs;
    ISpObjectToken     *token;
    tvtts_synth        *synth;
    int                 voice;        /* index in the library's numbering */
    int                 base_rate;    /* the voice's own, for SAPI's zero */
    int                 base_pitch;
};

#define ENGINE_FROM_TTS(p)   ((Engine *)((char *)(p) - offsetof(Engine, engine)))
#define ENGINE_FROM_TOKEN(p) ((Engine *)((char *)(p) - offsetof(Engine, with_token)))

/*
 * One utterance in flight.  The callback the library drives is the only place
 * audio and marks appear, so everything Speak needs to answer SAPI with lives
 * here and is filled in as the engine runs.
 */
typedef struct {
    ISpTTSEngineSite *site;
    ULONGLONG         interest;    /* which events the host wants */
    ULONGLONG         bytes;       /* audio written so far, SAPI's offset */
    WCHAR           **marks;       /* bookmark names, by mark number */
    ULONG             mark_count;
    int               aborted;
    int               failed;
} Speaking;

/* wcstol is CRT; a bookmark name is a short decimal string, so this is all
 * that is wanted, and it answers zero for a name that is not a number. */
static long tv_wtol(const WCHAR *s)
{
    long v = 0;
    int neg = 0;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-' || *s == '+')
        neg = (*s++ == '-');
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/*
 * Audio and marks, in stream order.  Returning non-zero stops the engine,
 * which is how an abort from SAPI reaches it.
 *
 * Events must be queued before the audio they belong to, or SAPI cannot fire
 * them at the right moment; a mark arrives here before any of the samples that
 * follow it, so the offset to report is simply everything written so far.
 */
static int TVTTS_CALL on_event(const tvtts_event *ev, void *user)
{
    Speaking *sp = (Speaking *)user;
    ULONG wrote = 0;
    DWORD actions;

    if (ev->type == TVTTS_MARK) {
        ULONG i = ev->mark - 1;          /* mark_add made them one-based */
        if (ev->mark >= 1 && i < sp->mark_count && sp->marks[i] &&
            (sp->interest & SPFEI(SPEI_TTS_BOOKMARK))) {
            const WCHAR *name = sp->marks[i];
            SPEVENT e;
            ZeroMemory(&e, sizeof e);
            e.eEventId = SPEI_TTS_BOOKMARK;
            e.elParamType = SPET_LPARAM_IS_STRING;
            e.ullAudioStreamOffset = sp->bytes;
            /* SAPI wants the name both ways: as the string, and as the
             * number it spells when it is one, which is what a client that
             * passed <bookmark mark="1"/> gets back. */
            e.lParam = (LPARAM)name;
            e.wParam = (WPARAM)(LONG_PTR)tv_wtol(name);
            ISpTTSEngineSite_AddEvents(sp->site, &e, 1);
        }
        return 0;
    }

    if (ev->type != TVTTS_AUDIO || ev->count == 0)
        return 0;

    actions = ISpTTSEngineSite_GetActions(sp->site);
    if (actions & SPVES_ABORT) {
        sp->aborted = 1;
        return 1;
    }
    if (actions & SPVES_SKIP) {
        /* Only sentence skipping exists, and this engine renders an utterance
         * in one pass with no way back into it, so nothing can be skipped.
         * The documented answer is to report what was managed -- none -- and
         * abandon the call rather than carry on in the wrong place. */
        SPVSKIPTYPE type;
        long items = 0;
        if (SUCCEEDED(ISpTTSEngineSite_GetSkipInfo(sp->site, &type, &items))
            && items != 0) {
            ISpTTSEngineSite_CompleteSkip(sp->site, 0);
            sp->aborted = 1;
            return 1;
        }
    }

    if (FAILED(ISpTTSEngineSite_Write(sp->site, ev->samples,
                                      ev->count * (ULONG)sizeof(short),
                                      &wrote))) {
        /* SP_AUDIO_STOPPED lands here too: the device went away and the call
         * should end at once rather than keep rendering into nothing. */
        sp->failed = 1;
        return 1;
    }
    sp->bytes += ev->count * (ULONGLONG)sizeof(short);
    return 0;
}

/* ---- building the utterance ----------------------------------------------- */

/* Remember a bookmark's name and answer the number the engine will report. */
static ULONG mark_add(Speaking *sp, const WCHAR *text, ULONG len)
{
    WCHAR **grown = (WCHAR **)tv_realloc(sp->marks,
                                         (sp->mark_count + 1) * sizeof(WCHAR *));
    WCHAR *copy;
    ULONG i;
    if (!grown)
        return sp->mark_count;
    sp->marks = grown;
    copy = (WCHAR *)tv_alloc((len + 1) * sizeof(WCHAR));
    if (copy) {
        for (i = 0; i < len; i++)
            copy[i] = text[i];
        copy[len] = 0;
    }
    sp->marks[sp->mark_count] = copy;
    sp->mark_count++;
    /* The engine swallows mark 0: ESC[0i produces no event at all, where
     * ESC[1i does, which is of a piece with the rest of its escapes using
     * zero to mean off.  So the numbers handed to it start at one, and
     * on_event subtracts it again. */
    return sp->mark_count;
}

/*
 * Turn the fragment list into one string for the engine.
 *
 * SAPI has already parsed its own markup, so pitch, rate, silence and
 * bookmarks arrive as state on a fragment rather than as text; this engine
 * takes all four inline, and tvtts.h builds the escapes.  Only what changed is
 * emitted, so an utterance with no markup carries none.
 *
 * A bookmark fragment's text is the bookmark's NAME, not words to speak.  JAWS
 * sends every word as its own fragment with a bookmark between each pair, so
 * appending fragment text blindly reads the bookmark names aloud -- and
 * dropping them without putting anything back runs the neighbouring words
 * together.  The name is kept for the event and a space is restored at the seam
 * when neither side brought one.  (Both learned from Panthera Speech, MIT; see
 * NOTICE.)
 */
static void build_utterance(Engine *e, const SPVTEXTFRAG *frags, DWORD flags,
                            long site_rate, WBuf *out, Speaking *sp)
{
    const SPVTEXTFRAG *f;
    int gap = 0;
    /* What Speak already set on the synth, so a fragment that asks for the
     * same thing emits nothing.  Without this every utterance opened with a
     * pitch escape, which also gave an empty one something to say. */
    int cur_pitch = map_pitch(e->base_pitch, 0);
    int cur_rate = map_rate(e->base_rate, site_rate);
    char esc[32];

    if (flags & SPF_NLP_SPEAK_PUNC) {
        if (tvtts_punctuation_sequence(esc, sizeof esc, 1) > 0)
            wbuf_add_ascii(out, esc);
    }

    for (f = frags; f; f = f->pNext) {
        const WCHAR *t = f->pTextStart;
        ULONG n = f->ulTextLen, i;
        int pitch, rate;

        pitch = map_pitch(e->base_pitch, f->State.PitchAdj.MiddleAdj);
        rate = map_rate(e->base_rate, f->State.RateAdj + site_rate);
        if (pitch != cur_pitch) {
            if (tvtts_pitch_sequence(esc, sizeof esc, pitch) > 0)
                wbuf_add_ascii(out, esc);
            cur_pitch = pitch;
        }
        if (rate != cur_rate) {
            if (tvtts_rate_sequence(esc, sizeof esc, rate) > 0)
                wbuf_add_ascii(out, esc);
            cur_rate = rate;
        }

        switch (f->State.eAction) {
        case SPVA_Bookmark:
            if (tvtts_mark_sequence(esc, sizeof esc, mark_add(sp, t, n)) > 0)
                wbuf_add_ascii(out, esc);
            gap = 1;
            break;

        case SPVA_Silence:
            if (tvtts_break_sequence(esc, sizeof esc,
                                     f->State.SilenceMSecs) > 0)
                wbuf_add_ascii(out, esc);
            gap = 1;
            break;

        case SPVA_SpellOut:
            /* The engine names a letter standing on its own, so spacing the
             * characters out is all that spelling needs. */
            for (i = 0; i < n; i++) {
                wbuf_add_ch(out, t[i]);
                wbuf_add_ch(out, ' ');
            }
            gap = 0;
            break;

        case SPVA_Speak:
            if (gap && out->len && out->p[out->len - 1] != ' ' &&
                n && t[0] != ' ')
                wbuf_add_ch(out, ' ');
            wbuf_add(out, t, n);
            gap = 0;
            break;

        default:
            /* SPVA_Pronounce carries SAPI phone ids, whose alphabet is not
             * this engine's; SPVA_Section is unused, and an unknown tag is not
             * ours to interpret.  All three are passed over. */
            break;
        }
    }
}

/* ---- ISpTTSEngine --------------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE eng_QueryInterface(ISpTTSEngine *This,
                                                    REFIID riid, void **ppv)
{
    Engine *e = ENGINE_FROM_TTS(This);
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (guid_eq(riid, &IID_IUnknown) || guid_eq(riid, &IID_ISpTTSEngine))
        *ppv = &e->engine;
    else if (guid_eq(riid, &IID_ISpObjectWithToken))
        *ppv = &e->with_token;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&e->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE eng_AddRef(ISpTTSEngine *This)
{
    return (ULONG)InterlockedIncrement(&ENGINE_FROM_TTS(This)->refs);
}

static ULONG STDMETHODCALLTYPE eng_Release(ISpTTSEngine *This)
{
    Engine *e = ENGINE_FROM_TTS(This);
    LONG n = InterlockedDecrement(&e->refs);
    if (n == 0) {
        if (e->synth)
            tvtts_destroy(e->synth);
        if (e->token)
            e->token->lpVtbl->Release(e->token);
        tv_free(e);
        InterlockedDecrement(&g_objects);
    }
    return (ULONG)n;
}

/*
 * The format the engine renders in.  SAPI converts whatever it is handed, so
 * the honest answer is this engine's own rate rather than an attempt to match
 * what was asked for: 11025 Hz, mono, signed 16-bit, which is what TruVoice
 * shipped as its desktop rate and what the voices are known to sound like.
 */
static HRESULT STDMETHODCALLTYPE eng_GetOutputFormat(
    ISpTTSEngine *This, const GUID *target, const WAVEFORMATEX *target_wf,
    GUID *id, WAVEFORMATEX **wf)
{
    Engine *e = ENGINE_FROM_TTS(This);
    WAVEFORMATEX f;
    uint32_t hz = 11025;

    (void)target;
    (void)target_wf;
    if (!id || !wf)
        return E_POINTER;
    if (e->synth)
        hz = tvtts_sample_rate_hz(tvtts_get_sample_rate(e->synth));
    if (hz == 0)
        hz = 11025;

    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = hz;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = hz * 2;
    f.cbSize = 0;

    *wf = (WAVEFORMATEX *)CoTaskMemAlloc(sizeof f);
    if (!*wf)
        return E_OUTOFMEMORY;
    **wf = f;
    *id = SPDFID_WaveFormatEx;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE eng_Speak(
    ISpTTSEngine *This, DWORD flags, REFGUID format, const WAVEFORMATEX *wf,
    const SPVTEXTFRAG *frags, ISpTTSEngineSite *site)
{
    Engine *e = ENGINE_FROM_TTS(This);
    Speaking sp;
    WBuf text;
    long site_rate = 0;
    USHORT site_volume = 100;
    ULONG i;
    int rc = 0;

    (void)format;
    (void)wf;
    if (!site || !e->synth)
        return E_UNEXPECTED;

    ZeroMemory(&sp, sizeof sp);
    ZeroMemory(&text, sizeof text);
    sp.site = site;

    if (FAILED(ISpTTSEngineSite_GetEventInterest(site, &sp.interest)))
        sp.interest = 0;
    if (FAILED(ISpTTSEngineSite_GetRate(site, &site_rate)))
        site_rate = 0;
    if (FAILED(ISpTTSEngineSite_GetVolume(site, &site_volume)))
        site_volume = 100;
    if (site_volume > 100)
        site_volume = 100;

    tvtts_set_voice(e->synth, e->voice);
    tvtts_set_volume(e->synth, (uint32_t)(site_volume * 0xffffu / 100u));
    tvtts_set_rate(e->synth, map_rate(e->base_rate, site_rate));
    tvtts_set_pitch(e->synth, e->base_pitch);

    build_utterance(e, frags, flags, site_rate, &text, &sp);

    if (text.len && !text.bad)
        rc = tvtts_speak_utf16(e->synth, (const uint16_t *)text.p,
                               on_event, &sp);

    for (i = 0; i < sp.mark_count; i++)
        tv_free(sp.marks[i]);
    tv_free(sp.marks);

    if (sp.failed) {
        wbuf_free(&text);
        return E_FAIL;
    }
    if (text.bad || rc < 0) {
        wbuf_free(&text);
        return E_OUTOFMEMORY;
    }
    wbuf_free(&text);
    return S_OK;               /* an abort is a normal end of the call */
}

static const ISpTTSEngineVtbl g_engine_vtbl = {
    eng_QueryInterface,
    eng_AddRef,
    eng_Release,
    eng_Speak,
    eng_GetOutputFormat
};

/* ---- ISpObjectWithToken --------------------------------------------------- */
/* The same object, reached through its second vtable. */

static HRESULT STDMETHODCALLTYPE tok_QueryInterface(ISpObjectWithToken *This,
                                                    REFIID riid, void **ppv)
{
    return eng_QueryInterface(&ENGINE_FROM_TOKEN(This)->engine, riid, ppv);
}

static ULONG STDMETHODCALLTYPE tok_AddRef(ISpObjectWithToken *This)
{
    return eng_AddRef(&ENGINE_FROM_TOKEN(This)->engine);
}

static ULONG STDMETHODCALLTYPE tok_Release(ISpObjectWithToken *This)
{
    return eng_Release(&ENGINE_FROM_TOKEN(This)->engine);
}

/*
 * Which voice this engine is.  SAPI creates the object from a token, and the
 * token is the only thing that says which of the voices it stands for: the
 * value written at registration under OpenTVVoice.  A token without it, or
 * naming a voice this build does not carry, is refused here rather than left
 * to render silence later.
 */
static HRESULT STDMETHODCALLTYPE tok_SetObjectToken(ISpObjectWithToken *This,
                                                    ISpObjectToken *token)
{
    Engine *e = ENGINE_FROM_TOKEN(This);
    WCHAR *value = NULL;
    int voice;

    if (!token)
        return E_INVALIDARG;
    if (FAILED(token->lpVtbl->GetStringValue(token, TV_TOKEN_VALUE, &value))
        || !value)
        return SPERR_NOT_FOUND;

    voice = (int)tv_wtol(value);
    CoTaskMemFree(value);
    if (voice < 0 || voice >= tvtts_voice_count())
        return E_INVALIDARG;

    if (!e->synth) {
        e->synth = tvtts_create(11025);
        if (!e->synth)
            return E_OUTOFMEMORY;
    }
    e->voice = voice;
    e->base_rate = tvtts_voice_rate(voice);
    e->base_pitch = tvtts_voice_pitch(voice);
    tvtts_set_voice(e->synth, voice);

    token->lpVtbl->AddRef(token);
    if (e->token)
        e->token->lpVtbl->Release(e->token);
    e->token = token;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tok_GetObjectToken(ISpObjectWithToken *This,
                                                    ISpObjectToken **token)
{
    Engine *e = ENGINE_FROM_TOKEN(This);
    if (!token)
        return E_POINTER;
    *token = e->token;
    if (e->token) {
        e->token->lpVtbl->AddRef(e->token);
        return S_OK;
    }
    return S_FALSE;
}

static const ISpObjectWithTokenVtbl g_token_vtbl = {
    tok_QueryInterface,
    tok_AddRef,
    tok_Release,
    tok_SetObjectToken,
    tok_GetObjectToken
};

/* ---- the class factory --------------------------------------------------- */

static ULONG STDMETHODCALLTYPE cf_AddRef(IClassFactory *This);

static HRESULT STDMETHODCALLTYPE cf_QueryInterface(IClassFactory *This,
                                                   REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (guid_eq(riid, &IID_IUnknown) || guid_eq(riid, &IID_IClassFactory)) {
        *ppv = This;
        cf_AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

/* The factory is static, so its lifetime is the module's; the count it keeps
 * is only what stops DllCanUnloadNow saying yes while it is handed out. */
static ULONG STDMETHODCALLTYPE cf_AddRef(IClassFactory *This)
{
    (void)This;
    InterlockedIncrement(&g_objects);
    return 2;
}

static ULONG STDMETHODCALLTYPE cf_Release(IClassFactory *This)
{
    (void)This;
    InterlockedDecrement(&g_objects);
    return 1;
}

static HRESULT STDMETHODCALLTYPE cf_CreateInstance(IClassFactory *This,
                                                   IUnknown *outer,
                                                   REFIID riid, void **ppv)
{
    Engine *e;
    HRESULT hr;

    (void)This;
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    e = (Engine *)tv_alloc(sizeof *e);
    if (!e)
        return E_OUTOFMEMORY;
    ZeroMemory(e, sizeof *e);
    e->engine.lpVtbl = (ISpTTSEngineVtbl *)&g_engine_vtbl;
    e->with_token.lpVtbl = (ISpObjectWithTokenVtbl *)&g_token_vtbl;
    e->refs = 1;
    e->base_rate = 150;
    e->base_pitch = 100;
    InterlockedIncrement(&g_objects);

    hr = eng_QueryInterface(&e->engine, riid, ppv);
    eng_Release(&e->engine);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cf_LockServer(IClassFactory *This, BOOL lock)
{
    (void)This;
    InterlockedExchangeAdd(&g_objects, lock ? 1 : -1);
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    cf_QueryInterface,
    cf_AddRef,
    cf_Release,
    cf_CreateInstance,
    cf_LockServer
};

static IClassFactory g_factory = { (IClassFactoryVtbl *)&g_factory_vtbl };

/* ---- registration -------------------------------------------------------- */
/*
 * regsvr32 does the whole job: the class, and a token for every voice the
 * library carries.  A 32-bit tvsapi.dll registered by the 32-bit regsvr32
 * lands under Wow6432Node, which is what 32-bit SAPI applications read, and
 * the 64-bit one likewise -- so each bitness gets tokens naming a server of
 * its own word width, which is what lets both kinds of application use these
 * voices.
 */

static void wide_of(const char *s, WCHAR *out, int cap)
{
    int i = 0;
    if (cap <= 0)
        return;
    while (s && s[i] && i < cap - 1) {
        out[i] = (WCHAR)(unsigned char)s[i];
        i++;
    }
    out[i] = 0;
}

static void wide_num(WCHAR *out, int v)
{
    WCHAR tmp[16];
    int n = 0, i = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) {
        tmp[n++] = (WCHAR)('0' + v % 10);
        v /= 10;
    }
    while (n > 0)
        out[i++] = tmp[--n];
    out[i] = 0;
}

static void wide_cat(WCHAR *dst, int cap, const WCHAR *s)
{
    int i = 0, j = 0;
    while (dst[i])
        i++;
    while (s[j] && i < cap - 1)
        dst[i++] = s[j++];
    dst[i] = 0;
}

/*
 * Where the registration goes.  Normally HKEY_LOCAL_MACHINE, which is where
 * SAPI looks for voices and where writing needs an administrator.
 *
 * With OPENTV_SAPI_TEST_HIVE set in the environment it goes to
 * HKEY_CURRENT_USER instead, under a key of this project's own.  That is how
 * sapi5/sapi_test.c checks what DllRegisterServer actually writes -- the key
 * names, the labels, the attributes -- without administrator rights and
 * without changing what any other program on the machine sees.  SAPI does not
 * read that location: it is a test hook, not a per-user install.
 */
#define TV_TEST_HIVE_VAR L"OPENTV_SAPI_TEST_HIVE"
#define TV_TEST_ROOT     L"Software\\OpenTV\\RegistrationTest"

static int use_test_hive(void)
{
    WCHAR buf[8];
    return GetEnvironmentVariableW(TV_TEST_HIVE_VAR, buf, 8) > 0;
}

static HKEY reg_hive(void)
{
    return use_test_hive() ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

/* Starts `out` off: empty for a real registration, the test root otherwise. */
static void reg_prefix(WCHAR *out, int cap)
{
    out[0] = 0;
    if (use_test_hive()) {
        wide_cat(out, cap, TV_TEST_ROOT);
        wide_cat(out, cap, L"\\");
    }
}

static LONG set_sz(HKEY key, const WCHAR *name, const WCHAR *value)
{
    int n = 0;
    while (value[n])
        n++;
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                          (DWORD)((n + 1) * sizeof(WCHAR)));
}

/* The registry key for one voice's token, without the hive. */
static void token_key(WCHAR *key, int cap, int voice)
{
    WCHAR name[64];
    wide_of(tvtts_voice_name(voice), name, 64);
    reg_prefix(key, cap);
    wide_cat(key, cap, TV_TOKENS_KEY);
    wide_cat(key, cap, L"\\OpenTV ");
    wide_cat(key, cap, name);
    wide_cat(key, cap, L" ");
    wide_cat(key, cap, voice_langid(voice));
}

static HRESULT register_class(void)
{
    WCHAR clsid[64], key[192], path[MAX_PATH];
    HKEY h;

    if (!StringFromGUID2(&CLSID_OpenTVEngine, clsid, 64))
        return SELFREG_E_CLASS;
    if (!GetModuleFileNameW(g_instance, path, MAX_PATH))
        return SELFREG_E_CLASS;

    reg_prefix(key, 192);
    wide_cat(key, 192, L"SOFTWARE\\Classes\\CLSID\\");
    wide_cat(key, 192, clsid);
    if (RegCreateKeyExW(reg_hive(), key, 0, NULL, 0, KEY_WRITE, NULL,
                        &h, NULL) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    set_sz(h, NULL, TV_SERVER_NAME);
    RegCloseKey(h);

    wide_cat(key, 192, L"\\InprocServer32");
    if (RegCreateKeyExW(reg_hive(), key, 0, NULL, 0, KEY_WRITE, NULL,
                        &h, NULL) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    set_sz(h, NULL, path);
    set_sz(h, L"ThreadingModel", L"Both");
    RegCloseKey(h);
    return S_OK;
}

static HRESULT register_tokens(void)
{
    WCHAR clsid[64];
    int count = tvtts_voice_count(), i;

    if (!StringFromGUID2(&CLSID_OpenTVEngine, clsid, 64))
        return SELFREG_E_CLASS;

    for (i = 0; i < count; i++) {
        WCHAR key[256], label[192], name[64], lang[96], num[16];
        HKEY h, attrs;

        wide_of(tvtts_voice_name(i), name, 64);
        wide_of(tvtts_language_name(tvtts_voice_language(i)), lang, 96);
        wide_num(num, i);

        /* "OpenTV Peter (American English)".  SAPI puts every vendor's voices
         * in one list, so the name has to say whose it is -- which is why
         * Microsoft's own read "Microsoft David Desktop".  The speak window
         * and the NVDA driver leave the prefix off, because there a voice is
         * already listed under OpenTV. */
        label[0] = 0;
        wide_cat(label, 192, TV_VENDOR);
        wide_cat(label, 192, L" ");
        wide_cat(label, 192, name);
        wide_cat(label, 192, L" (");
        wide_cat(label, 192, lang);
        wide_cat(label, 192, L")");

        token_key(key, 256, i);
        if (RegCreateKeyExW(reg_hive(), key, 0, NULL, 0, KEY_WRITE,
                            NULL, &h, NULL) != ERROR_SUCCESS)
            return SELFREG_E_CLASS;
        set_sz(h, NULL, label);
        set_sz(h, SPTOKENVALUE_CLSID, clsid);
        set_sz(h, TV_TOKEN_VALUE, num);

        if (RegCreateKeyExW(h, SPTOKENKEY_ATTRIBUTES, 0, NULL, 0, KEY_WRITE,
                            NULL, &attrs, NULL) == ERROR_SUCCESS) {
            set_sz(attrs, L"Name", label);
            set_sz(attrs, L"Language", voice_langid(i));
            set_sz(attrs, L"Gender", voice_gender(i));
            set_sz(attrs, L"Age", voice_age(i));
            set_sz(attrs, L"Vendor", TV_VENDOR);
            RegCloseKey(attrs);
        }
        RegCloseKey(h);
    }
    return S_OK;
}

static void unregister_all(void)
{
    WCHAR clsid[64], key[256];
    int count = tvtts_voice_count(), i;

    for (i = 0; i < count; i++) {
        token_key(key, 256, i);
        RegDeleteTreeW(reg_hive(), key);
    }
    if (StringFromGUID2(&CLSID_OpenTVEngine, clsid, 64)) {
        reg_prefix(key, 256);
        wide_cat(key, 256, L"SOFTWARE\\Classes\\CLSID\\");
        wide_cat(key, 256, clsid);
        RegDeleteTreeW(reg_hive(), key);
    }
    /* The test root is ours entirely, so it goes as a whole. */
    if (use_test_hive())
        RegDeleteTreeW(HKEY_CURRENT_USER, TV_TEST_ROOT);
}

/* ---- exports ------------------------------------------------------------- */

HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!guid_eq(clsid, &CLSID_OpenTVEngine))
        return CLASS_E_CLASSNOTAVAILABLE;
    return cf_QueryInterface(&g_factory, riid, ppv);
}

HRESULT __stdcall DllCanUnloadNow(void)
{
    return g_objects ? S_FALSE : S_OK;
}

HRESULT __stdcall DllRegisterServer(void)
{
    HRESULT hr = register_class();
    if (SUCCEEDED(hr))
        hr = register_tokens();
    return hr;
}

HRESULT __stdcall DllUnregisterServer(void)
{
    unregister_all();
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
