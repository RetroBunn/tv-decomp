/*
 * The SAPI 5 server, driven without SAPI.
 *
 * A registered voice can only be tested by registering it, which writes to
 * HKEY_LOCAL_MACHINE and changes what every other program on the machine sees.
 * Nothing here needs that: a COM in-process server is reachable through its own
 * DllGetClassObject, so this loads the DLL, asks it for the class factory and
 * drives ISpTTSEngine with a site and a token of its own.  No registry, no
 * CoCreateInstance, no administrator.
 *
 * That covers the part most likely to be wrong.  If Speak and GetOutputFormat
 * were transposed in the vtable -- the trap sapi_ddk.h is written around --
 * these calls would land on the wrong method and the first one would fail here
 * rather than inside somebody's word processor.
 *
 * What it cannot cover is SAPI's own half: that the tokens are where SAPI looks
 * and that it hands us fragments shaped the way we expect.  docs/SAPI5.md says
 * how to check that by hand once the DLL is registered.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>

#include "sapi_ddk.h"

DEFINE_GUID(CLSID_OpenTVEngine,
            0xc9b0b616, 0x4c0f, 0x45ac, 0xb4,0x35, 0xde,0x58,0x66,0xe7,0x5b,0x2e);

static int g_fail;

static void ok(int cond, const char *what)
{
    printf("%-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
        g_fail++;
}

/* ---- a token that answers one question ----------------------------------- */
/*
 * The engine asks a token for OpenTVVoice and nothing else, so only the three
 * IUnknown methods and GetStringValue are filled in.  The rest of the vtable
 * stays null: this is the only caller, and it does not touch them.
 */

typedef struct {
    ISpObjectToken iface;
    LONG refs;
    int voice;
} FakeToken;

static HRESULT STDMETHODCALLTYPE ft_QI(ISpObjectToken *This, REFIID riid,
                                       void **ppv)
{
    (void)riid;
    *ppv = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ft_AddRef(ISpObjectToken *This)
{
    return (ULONG)InterlockedIncrement(&((FakeToken *)This)->refs);
}

static ULONG STDMETHODCALLTYPE ft_Release(ISpObjectToken *This)
{
    return (ULONG)InterlockedDecrement(&((FakeToken *)This)->refs);
}

/* widl retypes an inherited method's This to the derived interface, so this
 * takes ISpObjectToken even though GetStringValue comes from ISpDataKey. */
static HRESULT STDMETHODCALLTYPE ft_GetStringValue(ISpObjectToken *This,
                                                   LPCWSTR name,
                                                   LPWSTR *value)
{
    FakeToken *t = (FakeToken *)This;
    WCHAR *out;
    if (wcscmp(name, L"OpenTVVoice") != 0)
        return E_INVALIDARG;
    /* The engine frees this with CoTaskMemFree, so it must come from there. */
    out = (WCHAR *)CoTaskMemAlloc(16 * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    _snwprintf(out, 16, L"%d", t->voice);
    *value = out;
    return S_OK;
}

static ISpObjectTokenVtbl g_token_vtbl = {
    .QueryInterface = ft_QI,
    .AddRef = ft_AddRef,
    .Release = ft_Release,
    .GetStringValue = ft_GetStringValue
};

/* ---- a site that records what it is given -------------------------------- */

typedef struct {
    ISpTTSEngineSite iface;
    LONG   refs;
    ULONG  bytes;          /* audio written */
    int    events;         /* events queued */
    long   rate;
    USHORT volume;
    DWORD  actions;        /* what GetActions will answer */
    int    abort_after;    /* bytes after which to start answering ABORT */
    WCHAR  last_mark[64];  /* the most recent bookmark name */
    long   last_mark_num;
    ULONGLONG last_offset;
} FakeSite;

static HRESULT STDMETHODCALLTYPE fs_QI(ISpTTSEngineSite *This, REFIID riid,
                                       void **ppv)
{
    (void)riid;
    *ppv = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE fs_AddRef(ISpTTSEngineSite *This)
{
    return (ULONG)InterlockedIncrement(&((FakeSite *)This)->refs);
}

static ULONG STDMETHODCALLTYPE fs_Release(ISpTTSEngineSite *This)
{
    return (ULONG)InterlockedDecrement(&((FakeSite *)This)->refs);
}

static HRESULT STDMETHODCALLTYPE fs_AddEvents(ISpTTSEngineSite *This,
                                              const SPEVENT *ev, ULONG n)
{
    FakeSite *s = (FakeSite *)This;
    ULONG i;
    for (i = 0; i < n; i++) {
        s->events++;
        if (ev[i].eEventId == SPEI_TTS_BOOKMARK) {
            const WCHAR *name = (const WCHAR *)ev[i].lParam;
            s->last_mark_num = (long)(LONG_PTR)ev[i].wParam;
            s->last_offset = ev[i].ullAudioStreamOffset;
            if (name)
                _snwprintf(s->last_mark, 64, L"%s", name);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_GetEventInterest(ISpTTSEngineSite *This,
                                                     ULONGLONG *interest)
{
    (void)This;
    *interest = SPFEI(SPEI_TTS_BOOKMARK);
    return S_OK;
}

static DWORD STDMETHODCALLTYPE fs_GetActions(ISpTTSEngineSite *This)
{
    FakeSite *s = (FakeSite *)This;
    if (s->abort_after && s->bytes >= (ULONG)s->abort_after)
        return SPVES_ABORT;
    return s->actions;
}

static HRESULT STDMETHODCALLTYPE fs_Write(ISpTTSEngineSite *This,
                                          const void *buf, ULONG cb,
                                          ULONG *wrote)
{
    FakeSite *s = (FakeSite *)This;
    (void)buf;
    s->bytes += cb;
    if (wrote)
        *wrote = cb;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_GetRate(ISpTTSEngineSite *This, long *r)
{
    *r = ((FakeSite *)This)->rate;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_GetVolume(ISpTTSEngineSite *This,
                                              USHORT *v)
{
    *v = ((FakeSite *)This)->volume;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_GetSkipInfo(ISpTTSEngineSite *This,
                                                SPVSKIPTYPE *type, long *n)
{
    (void)This;
    *type = SPVST_SENTENCE;
    *n = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_CompleteSkip(ISpTTSEngineSite *This,
                                                 long n)
{
    (void)This;
    (void)n;
    return S_OK;
}

static ISpTTSEngineSiteVtbl g_site_vtbl = {
    fs_QI, fs_AddRef, fs_Release,
    fs_AddEvents, fs_GetEventInterest,
    fs_GetActions, fs_Write, fs_GetRate, fs_GetVolume,
    fs_GetSkipInfo, fs_CompleteSkip
};

static void site_init(FakeSite *s)
{
    memset(s, 0, sizeof *s);
    s->iface.lpVtbl = &g_site_vtbl;
    s->refs = 1;
    s->volume = 100;
    s->rate = 0;
    s->actions = SPVES_CONTINUE;
}

/* ---- fragments ------------------------------------------------------------ */

static void frag_init(SPVTEXTFRAG *f, const WCHAR *text, SPVACTIONS action)
{
    memset(f, 0, sizeof *f);
    f->State.eAction = action;
    f->State.Volume = 100;
    f->pTextStart = text;
    f->ulTextLen = text ? (ULONG)wcslen(text) : 0;
}

/* ---- reading back what was registered ------------------------------------ */

#define TEST_ROOT L"Software\\OpenTV\\RegistrationTest\\"
#define TOKENS    L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\"

/* The value at HKCU\<test root>\<subkey>, or NULL if it is not there. */
static const WCHAR *reg_str(const WCHAR *subkey, const WCHAR *value)
{
    static WCHAR buf[512];
    WCHAR path[512];
    HKEY k;
    DWORD n = sizeof buf, type = 0;
    LONG rc;

    path[0] = 0;
    wcsncat(path, TEST_ROOT, 500);
    wcsncat(path, subkey, 500);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k)
        != ERROR_SUCCESS)
        return NULL;
    rc = RegQueryValueExW(k, value, NULL, &type, (BYTE *)buf, &n);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS || type != REG_SZ)
        return NULL;
    buf[(n / sizeof(WCHAR)) < 511 ? n / sizeof(WCHAR) : 511] = 0;
    return buf;
}

static int reg_is(const WCHAR *subkey, const WCHAR *value, const WCHAR *want)
{
    const WCHAR *got = reg_str(subkey, value);
    return got && wcscmp(got, want) == 0;
}

/* ---- the test ------------------------------------------------------------- */

typedef HRESULT (__stdcall *GetClassObjectFn)(REFCLSID, REFIID, void **);

static ISpTTSEngine *make_engine(GetClassObjectFn dgco, FakeToken *token)
{
    IClassFactory *factory = NULL;
    ISpTTSEngine *engine = NULL;
    ISpObjectWithToken *with = NULL;
    HRESULT hr;

    hr = dgco(&CLSID_OpenTVEngine, &IID_IClassFactory, (void **)&factory);
    if (FAILED(hr) || !factory) {
        printf("DllGetClassObject failed: 0x%08lx\n", (unsigned long)hr);
        return NULL;
    }
    hr = factory->lpVtbl->CreateInstance(factory, NULL, &IID_ISpTTSEngine,
                                        (void **)&engine);
    factory->lpVtbl->Release(factory);
    if (FAILED(hr) || !engine) {
        printf("CreateInstance failed: 0x%08lx\n", (unsigned long)hr);
        return NULL;
    }

    hr = engine->lpVtbl->QueryInterface(engine, &IID_ISpObjectWithToken,
                                        (void **)&with);
    if (FAILED(hr) || !with) {
        printf("no ISpObjectWithToken: 0x%08lx\n", (unsigned long)hr);
        engine->lpVtbl->Release(engine);
        return NULL;
    }
    hr = with->lpVtbl->SetObjectToken(with, &token->iface);
    with->lpVtbl->Release(with);
    if (FAILED(hr)) {
        printf("SetObjectToken failed: 0x%08lx\n", (unsigned long)hr);
        engine->lpVtbl->Release(engine);
        return NULL;
    }
    return engine;
}

int main(int argc, char **argv)
{
    const char *dll = argc > 1 ? argv[1] : "build/bin/tvsapi64.dll";
    HMODULE mod;
    GetClassObjectFn dgco;
    HRESULT (__stdcall *can_unload)(void);
    FakeToken token;
    FakeSite site;
    ISpTTSEngine *engine;
    SPVTEXTFRAG f[3];
    GUID id;
    WAVEFORMATEX *wf = NULL;
    ULONG plain, with_mark;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    mod = LoadLibraryA(dll);
    if (!mod) {
        printf("cannot load %s (%lu)\n", dll, GetLastError());
        return 1;
    }
    dgco = (GetClassObjectFn)(void *)GetProcAddress(mod, "DllGetClassObject");
    can_unload = (HRESULT (__stdcall *)(void))(void *)
                 GetProcAddress(mod, "DllCanUnloadNow");
    ok(dgco != NULL, "DllGetClassObject is exported");
    ok(can_unload != NULL, "DllCanUnloadNow is exported");
    if (!dgco || !can_unload)
        return 1;

    ok(can_unload() == S_OK, "nothing outstanding, so the DLL can unload");

    /* --- the class, the token and the engine --- */
    memset(&token, 0, sizeof token);
    token.iface.lpVtbl = &g_token_vtbl;
    token.refs = 1;
    token.voice = 0;                     /* Peter */

    engine = make_engine(dgco, &token);
    ok(engine != NULL, "the engine is created and takes its token");
    if (!engine)
        return 1;
    ok(can_unload() == S_FALSE, "with an object alive it cannot unload");

    /* --- GetOutputFormat.  If the vtable were transposed this lands on
     *     Speak, with five arguments where four were passed. --- */
    hr = engine->lpVtbl->GetOutputFormat(engine, NULL, NULL, &id, &wf);
    ok(SUCCEEDED(hr) && wf != NULL, "GetOutputFormat answers");
    if (wf) {
        ok(memcmp(&id, &SPDFID_WaveFormatEx, sizeof id) == 0,
           "  the format id is SPDFID_WaveFormatEx");
        ok(wf->wFormatTag == WAVE_FORMAT_PCM, "  PCM");
        ok(wf->nChannels == 1, "  mono");
        ok(wf->wBitsPerSample == 16, "  16-bit");
        ok(wf->nSamplesPerSec == 11025, "  11025 Hz, the engine's own rate");
        ok(wf->nBlockAlign == 2 && wf->nAvgBytesPerSec == 11025 * 2,
           "  the derived fields agree");
        CoTaskMemFree(wf);
    }

    /* --- plain text --- */
    site_init(&site);
    frag_init(&f[0], L"One two three.", SPVA_Speak);
    hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL, &f[0],
                               &site.iface);
    ok(SUCCEEDED(hr), "Speak succeeds on plain text");
    ok(site.bytes > 4000, "  it wrote audio");
    plain = site.bytes;

    /* --- a bookmark, the way a screen reader sends one ---
     * word, bookmark, word: the bookmark's own text is its NAME and must not
     * be spoken, and the two words must not run together. */
    site_init(&site);
    frag_init(&f[0], L"One", SPVA_Speak);
    frag_init(&f[1], L"xyzzy", SPVA_Bookmark);
    frag_init(&f[2], L"two three.", SPVA_Speak);
    f[0].pNext = &f[1];
    f[1].pNext = &f[2];
    hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL, &f[0],
                               &site.iface);
    ok(SUCCEEDED(hr), "Speak succeeds with a bookmark between two words");
    ok(site.events == 1, "  exactly one event was queued");
    ok(wcscmp(site.last_mark, L"xyzzy") == 0,
       "  the event carries the bookmark's name");
    ok(site.last_mark_num == 0, "  a non-numeric name reports zero");
    ok(site.last_offset > 0 && site.last_offset < site.bytes,
       "  its offset falls inside the audio, not at either end");
    with_mark = site.bytes;

    /* "xyzzy" spoken aloud would add roughly its own length again; the two
     * renders should instead be within a fifth of each other. */
    ok(with_mark > plain / 2 && with_mark < plain * 3 / 2,
       "  the bookmark's name was not spoken");

    /* --- a numeric bookmark, which is what <bookmark mark='7'/> gives --- */
    site_init(&site);
    frag_init(&f[0], L"7", SPVA_Bookmark);
    frag_init(&f[1], L"Hello.", SPVA_Speak);
    f[0].pNext = &f[1];
    hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL, &f[0],
                               &site.iface);
    ok(SUCCEEDED(hr) && site.last_mark_num == 7,
       "a numeric bookmark reports its number");

    /* --- abort part way through --- */
    site_init(&site);
    site.abort_after = 2000;
    frag_init(&f[0], L"One two three four five six seven eight nine ten.",
              SPVA_Speak);
    hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL, &f[0],
                               &site.iface);
    ok(SUCCEEDED(hr), "an aborted Speak still returns success");
    ok(site.bytes < plain * 4, "  and it stopped early");

    /* --- silence --- */
    site_init(&site);
    frag_init(&f[0], L"", SPVA_Silence);
    f[0].State.SilenceMSecs = 500;
    frag_init(&f[1], L"After.", SPVA_Speak);
    f[0].pNext = &f[1];
    hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL, &f[0],
                               &site.iface);
    ok(SUCCEEDED(hr), "a silence fragment is accepted");
    ok(site.bytes > 11025 * 2 / 4, "  and lengthens the audio");

    /* --- an empty list, which SAPI does send --- */
    site_init(&site);
    frag_init(&f[0], L"", SPVA_Speak);
    hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL, &f[0],
                               &site.iface);
    ok(SUCCEEDED(hr) && site.bytes == 0, "an empty fragment says nothing");

    engine->lpVtbl->Release(engine);
    ok(can_unload() == S_OK, "after the last release it can unload again");
    ok(token.refs == 1, "the engine let go of the token");

    /* --- a second voice, in the other language --- */
    {
        FakeToken spanish;
        memset(&spanish, 0, sizeof spanish);
        spanish.iface.lpVtbl = &g_token_vtbl;
        spanish.refs = 1;
        spanish.voice = 10;             /* Pedro, if Spanish is built in */

        engine = make_engine(dgco, &spanish);
        if (engine) {
            site_init(&site);
            frag_init(&f[0], L"Hola, buenos dias.", SPVA_Speak);
            hr = engine->lpVtbl->Speak(engine, 0, &SPDFID_WaveFormatEx, NULL,
                                       &f[0], &site.iface);
            ok(SUCCEEDED(hr) && site.bytes > 4000,
               "voice 10 speaks Spanish through the same server");
            engine->lpVtbl->Release(engine);
        } else {
            printf("voice 10 is not in this build; skipping the Spanish case\n");
        }
    }

    /* --- a token that is not ours --- */
    {
        FakeToken bad;
        IClassFactory *factory = NULL;
        ISpObjectWithToken *with = NULL;
        memset(&bad, 0, sizeof bad);
        bad.iface.lpVtbl = &g_token_vtbl;
        bad.refs = 1;
        bad.voice = 9999;

        if (SUCCEEDED(dgco(&CLSID_OpenTVEngine, &IID_IClassFactory,
                           (void **)&factory)) && factory) {
            if (SUCCEEDED(factory->lpVtbl->CreateInstance(
                    factory, NULL, &IID_ISpObjectWithToken, (void **)&with))
                && with) {
                ok(FAILED(with->lpVtbl->SetObjectToken(with, &bad.iface)),
                   "a token naming no voice we have is refused");
                with->lpVtbl->Release(with);
            }
            factory->lpVtbl->Release(factory);
        }
    }

    /* --- an unknown class --- */
    {
        GUID other = CLSID_OpenTVEngine;
        void *p = NULL;
        other.Data1 ^= 1;
        ok(dgco(&other, &IID_IClassFactory, &p) == CLASS_E_CLASSNOTAVAILABLE,
           "another CLSID is turned away");
    }

    /*
     * What DllRegisterServer writes.  OPENTV_SAPI_TEST_HIVE sends it to
     * HKEY_CURRENT_USER under a key of ours, so this checks the real
     * registration path -- key names, labels, attributes -- without an
     * administrator and without disturbing the machine's actual voice list.
     */
    {
        HRESULT (__stdcall *reg)(void) = (HRESULT (__stdcall *)(void))(void *)
            GetProcAddress(mod, "DllRegisterServer");
        HRESULT (__stdcall *unreg)(void) = (HRESULT (__stdcall *)(void))(void *)
            GetProcAddress(mod, "DllUnregisterServer");

        SetEnvironmentVariableW(L"OPENTV_SAPI_TEST_HIVE", L"1");
        ok(reg && unreg && SUCCEEDED(reg()),
           "DllRegisterServer succeeds into the test hive");

        ok(reg_is(TOKENS L"OpenTV Peter 409", NULL,
                  L"OpenTV Peter (American English)"),
           "  voice 0's token is named OpenTV Peter (American English)");
        ok(reg_is(TOKENS L"OpenTV Peter 409", L"OpenTVVoice", L"0"),
           "  and carries the voice number the engine reads back");
        ok(reg_str(TOKENS L"OpenTV Peter 409", L"CLSID") != NULL,
           "  and the CLSID of this server");
        {
            /* reg_str hands back one static buffer, so the CLSID has to be
             * copied before it is used to look anything else up. */
            WCHAR clsid[64], path[256];
            const WCHAR *got = reg_str(TOKENS L"OpenTV Peter 409", L"CLSID");
            clsid[0] = 0;
            if (got)
                wcsncat(clsid, got, 60);
            path[0] = 0;
            wcsncat(path, L"SOFTWARE\\Classes\\CLSID\\", 200);
            wcsncat(path, clsid, 200);
            ok(reg_is(path, NULL, L"OpenTV speech engine"),
               "  the class registers as \"OpenTV speech engine\"");
            wcsncat(path, L"\\InprocServer32", 200);
            ok(reg_is(path, L"ThreadingModel", L"Both"),
               "  and its server is threading model Both");
        }
        ok(reg_is(TOKENS L"OpenTV Peter 409\\Attributes", L"Name",
                  L"OpenTV Peter (American English)"),
           "  its Name attribute matches");
        ok(reg_is(TOKENS L"OpenTV Peter 409\\Attributes", L"Language", L"409"),
           "  American English is 409");
        ok(reg_is(TOKENS L"OpenTV Peter 409\\Attributes", L"Gender", L"Male"),
           "  Peter is male");
        ok(reg_is(TOKENS L"OpenTV Peter 409\\Attributes", L"Vendor", L"OpenTV"),
           "  the vendor is OpenTV");

        ok(reg_is(TOKENS L"OpenTV Wanda 409\\Attributes", L"Gender", L"Female"),
           "the last two voices of a language are the female ones");
        ok(reg_is(TOKENS L"OpenTV Grandpa Amos 409\\Attributes", L"Age",
                  L"Senior"),
           "voice 5 is the elderly one");
        ok(reg_is(TOKENS L"OpenTV Josefa 40a", NULL,
                  L"OpenTV Josefa (Castilian Spanish)"),
           "Spanish voices are labelled with their own language");
        ok(reg_is(TOKENS L"OpenTV Josefa 40a\\Attributes", L"Language",
                  L"40a"),
           "  and Castilian Spanish is 40a");

        ok(SUCCEEDED(unreg()), "DllUnregisterServer succeeds");
        ok(reg_str(TOKENS L"OpenTV Peter 409", NULL) == NULL,
           "  and takes the tokens out again");
        SetEnvironmentVariableW(L"OPENTV_SAPI_TEST_HIVE", NULL);
    }

    CoUninitialize();
    printf("\n%s\n", g_fail ? "FAILED" : "all SAPI server checks passed");
    return g_fail ? 1 : 0;
}
