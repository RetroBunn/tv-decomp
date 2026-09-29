/*
 * The SAPI 5 engine-side declarations, which mingw-w64 does not ship.
 *
 * A TTS engine implements ISpTTSEngine and consumes ISpTTSEngineSite.  Both
 * live in Microsoft's sapiddk.h, the one SAPI header mingw-w64 has no
 * counterpart for -- it carries sapi.h, sapi51.h, sapi53.h and sapi54.h, which
 * between them give everything else this project needs: SPVSTATE, SPVACTIONS,
 * SPVPITCH, SPVCONTEXT, SPEVENT, ISpEventSink, ISpObjectToken and
 * ISpObjectWithToken, with their IIDs as DEFINE_GUID (so INITGUID instantiates
 * them and no GUID here is written by hand that the toolchain already knows).
 *
 * Mixing the two header sets is not an option: Microsoft's windows.h stops a
 * mingw compile at once with #error "No Target Architecture", so the four
 * missing declarations are restated here in the widl idiom the rest of
 * mingw's SAPI headers use, rather than the SDK's being included.
 *
 * Provenance, because a wrong vtable order here is a crash on the first
 * utterance rather than a compile error.  Every item was read out of
 * Windows Kits 10.0.26100.0:
 *
 *   Include/um/sapiddk.h:1738  SPVSKIPTYPE
 *   Include/um/sapiddk.h:1744  SPVESACTIONS
 *   Include/um/sapiddk.h:1922  SPVTEXTFRAG
 *   Include/um/sapiddk.h:1769  ISpTTSEngineSite, IID and method order
 *   Include/um/sapiddk.h:1947  ISpTTSEngine, IID and method order
 *
 * Two details are worth stating because they are easy to get wrong and both
 * are silent failures:
 *
 *   - Speak comes FIRST in ISpTTSEngine's vtable and GetOutputFormat second.
 *     Microsoft's own TTS Engine Vendor Porting Guide describes them in the
 *     opposite order, which is prose order, not vtable order.
 *   - ISpTTSEngineSite::GetActions returns DWORD, not HRESULT, so it is the
 *     one method here that is not an HRESULT.
 *
 * SPDFID_WaveFormatEx is declared by mingw's sapi51.h but no mingw library
 * defines it, so it is defined here.  Its value was taken from the byte image
 * of Lib/10.0.26100.0/um/x64/sapi.lib, where it occurs exactly once, in the
 * object built from MIDL's sapiint_i.c.
 *
 * tools/check_sapi_ddk.py re-verifies all of the above against the SDK when it
 * is installed, so this file does not have to be trusted on its say-so.
 */
#ifndef TV_SAPI_DDK_H
#define TV_SAPI_DDK_H

#include <sapi.h>
#include <sperror.h>   /* the SPERR_ codes; sapi.h does not pull this in */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- types ---------------------------------------------------------------- */

typedef enum SPVSKIPTYPE {
    SPVST_SENTENCE = (1L << 0)
} SPVSKIPTYPE;

typedef enum SPVESACTIONS {
    SPVES_CONTINUE = 0,
    SPVES_ABORT    = (1L << 0),
    SPVES_SKIP     = (1L << 1),
    SPVES_RATE     = (1L << 2),
    SPVES_VOLUME   = (1L << 3)
} SPVESACTIONS;

/*
 * One run of text with one XML state.  SAPI has already parsed its own markup
 * by the time the engine sees this, so a <pitch> or <bookmark> arrives as a
 * fragment with State.eAction and State.PitchAdj set rather than as characters
 * to be scanned for.  pTextStart is NOT terminated at ulTextLen -- it points
 * into the caller's whole string, so the length is the only bound.
 */
typedef struct SPVTEXTFRAG {
    struct SPVTEXTFRAG *pNext;
    SPVSTATE            State;
    LPCWSTR             pTextStart;
    ULONG               ulTextLen;
    ULONG               ulTextSrcOffset;
} SPVTEXTFRAG;

/* ---- ISpTTSEngineSite ----------------------------------------------------- */
/*
 * Implemented by SAPI, handed to the engine for the duration of one Speak.
 * It extends ISpEventSink, so AddEvents and GetEventInterest come before its
 * own six methods in the vtable; mingw's sapi51.h declares that base, and this
 * order matches it.
 */

typedef struct ISpTTSEngineSite ISpTTSEngineSite;

typedef struct ISpTTSEngineSiteVtbl {
    BEGIN_INTERFACE

    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        ISpTTSEngineSite *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ISpTTSEngineSite *This);
    ULONG (STDMETHODCALLTYPE *Release)(ISpTTSEngineSite *This);

    /*** ISpEventSink methods ***/
    HRESULT (STDMETHODCALLTYPE *AddEvents)(
        ISpTTSEngineSite *This, const SPEVENT *pEventArray, ULONG ulCount);
    HRESULT (STDMETHODCALLTYPE *GetEventInterest)(
        ISpTTSEngineSite *This, ULONGLONG *pullEventInterest);

    /*** ISpTTSEngineSite methods ***/
    DWORD (STDMETHODCALLTYPE *GetActions)(ISpTTSEngineSite *This);
    HRESULT (STDMETHODCALLTYPE *Write)(
        ISpTTSEngineSite *This, const void *pBuff, ULONG cb, ULONG *pcbWritten);
    HRESULT (STDMETHODCALLTYPE *GetRate)(
        ISpTTSEngineSite *This, long *pRateAdjust);
    HRESULT (STDMETHODCALLTYPE *GetVolume)(
        ISpTTSEngineSite *This, USHORT *pusVolume);
    HRESULT (STDMETHODCALLTYPE *GetSkipInfo)(
        ISpTTSEngineSite *This, SPVSKIPTYPE *peType, long *plNumItems);
    HRESULT (STDMETHODCALLTYPE *CompleteSkip)(
        ISpTTSEngineSite *This, long ulNumSkipped);

    END_INTERFACE
} ISpTTSEngineSiteVtbl;

struct ISpTTSEngineSite {
    CONST_VTBL ISpTTSEngineSiteVtbl *lpVtbl;
};

#define ISpTTSEngineSite_AddEvents(This, a, b) \
    ((This)->lpVtbl->AddEvents(This, a, b))
#define ISpTTSEngineSite_GetEventInterest(This, a) \
    ((This)->lpVtbl->GetEventInterest(This, a))
#define ISpTTSEngineSite_GetActions(This) \
    ((This)->lpVtbl->GetActions(This))
#define ISpTTSEngineSite_Write(This, a, b, c) \
    ((This)->lpVtbl->Write(This, a, b, c))
#define ISpTTSEngineSite_GetRate(This, a) \
    ((This)->lpVtbl->GetRate(This, a))
#define ISpTTSEngineSite_GetVolume(This, a) \
    ((This)->lpVtbl->GetVolume(This, a))
#define ISpTTSEngineSite_GetSkipInfo(This, a, b) \
    ((This)->lpVtbl->GetSkipInfo(This, a, b))
#define ISpTTSEngineSite_CompleteSkip(This, a) \
    ((This)->lpVtbl->CompleteSkip(This, a))

/* ---- ISpTTSEngine -------------------------------------------------------- */
/* Implemented by us.  Speak first, GetOutputFormat second -- see above. */

typedef struct ISpTTSEngine ISpTTSEngine;

typedef struct ISpTTSEngineVtbl {
    BEGIN_INTERFACE

    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        ISpTTSEngine *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ISpTTSEngine *This);
    ULONG (STDMETHODCALLTYPE *Release)(ISpTTSEngine *This);

    /*** ISpTTSEngine methods ***/
    HRESULT (STDMETHODCALLTYPE *Speak)(
        ISpTTSEngine *This, DWORD dwSpeakFlags, REFGUID rguidFormatId,
        const WAVEFORMATEX *pWaveFormatEx, const SPVTEXTFRAG *pTextFragList,
        ISpTTSEngineSite *pOutputSite);
    HRESULT (STDMETHODCALLTYPE *GetOutputFormat)(
        ISpTTSEngine *This, const GUID *pTargetFmtId,
        const WAVEFORMATEX *pTargetWaveFormatEx, GUID *pOutputFormatId,
        WAVEFORMATEX **ppCoMemOutputWaveFormatEx);

    END_INTERFACE
} ISpTTSEngineVtbl;

struct ISpTTSEngine {
    CONST_VTBL ISpTTSEngineVtbl *lpVtbl;
};

/* ---- identifiers -------------------------------------------------------- */

DEFINE_GUID(IID_ISpTTSEngine,
            0xa74d7c8e, 0x4cc5, 0x4f2f, 0xa6,0xeb, 0x80,0x4d,0xee,0x18,0x50,0x0e);
DEFINE_GUID(IID_ISpTTSEngineSite,
            0x9880499b, 0xcce9, 0x11d2, 0xb5,0x03, 0x00,0xc0,0x4f,0x79,0x73,0x96);
DEFINE_GUID(SPDFID_WaveFormatEx,
            0xc31adbae, 0x527f, 0x4ff5, 0xa2,0x30, 0xf6,0x2b,0xb6,0x1f,0xf7,0x0c);

#ifdef __cplusplus
}
#endif

#endif
