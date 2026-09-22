/*
 * Engine object: the per-utterance-stream synthesizer state that the SAPI
 * engine thread creates, feeds with text, and steps to produce PCM.
 */
#ifndef TV_ENGINE_H
#define TV_ENGINE_H

#include "tv_common.h"
#include "engine_struct.h" /* generated from engine.fields */

#define TV_RING_SIZE 0x1000
#define TV_NODE_POOL 618

/* Node types (low three bits of Node.flags). */
#define NODE_TYPE_MASK 7u
#define NODE_CONTROL   0u
#define NODE_FREE      6u
#define NODE_SENTINEL  7u
#define NODE_TYPE(n)   ((n)->flags & NODE_TYPE_MASK)

/* Stage-mask bit for each node type (types 6 and 7 are never selected). */
/* @0x100ec108 */
extern const uint32_t g_node_type_bits[8];

/* ---- work list and stage windows (node.c) -------------------------------- */

/* @0x100295c0 */
Node *TV_CDECL List_InsertBefore(Node *n, Node *before);
/* @0x100295e0 */
Node *TV_CDECL List_Unlink(Node *n);
/* @0x10029900 */
void TV_THISCALL Engine_ResetNodes(Engine *self);
/* @0x10029600 */
Node *TV_THISCALL Engine_AppendNode(Engine *self, int32_t type, int32_t value);
/* @0x10029670 */
Node *TV_THISCALL Engine_NodeAlloc(Engine *self, Node *ref, int32_t after, int32_t type,
                                   uint8_t value);
/* @0x100298e0 */
Node *TV_THISCALL Engine_StageNext(Engine *self, Node *n);
/* @0x10029730 */
Node *TV_THISCALL Engine_StagePrev(Engine *self, Node *n);
/* @0x10029840 */
Node *TV_THISCALL Engine_NodeFree(Engine *self, Node *n, int32_t forward);
/* @0x10029a60 */
uint8_t TV_THISCALL Engine_StageBegin(Engine *self, StageCtx *st);
/* @0x10029750 */
uint8_t TV_THISCALL Engine_StageEnd(Engine *self);

/* ---- lifecycle and setters (engine.c) ----------------------------------- */

/* @0x10030fc0 */
Engine *TV_THISCALL Engine_Construct(Engine *self);
/* @0x1002c450 */
int32_t TV_THISCALL Engine_Init(Engine *self);
/* @0x1002c4d0 */
int32_t TV_THISCALL Engine_Reset(Engine *self);
/* @0x1002c810 */
void TV_THISCALL Engine_SetPitch(Engine *self, int32_t pitch);
/* @0x1002c840 */
void TV_THISCALL Engine_SetSpeed(Engine *self, int32_t wpm);
/* @0x1002c870 */
void TV_THISCALL Engine_SetVolume(Engine *self, uint32_t vol);
/* @0x1002c8f0 */
void TV_THISCALL Engine_SetVoice(Engine *self, uint32_t voice);
/* @0x10027ea0 */
void TV_THISCALL Engine_ResetRings(Engine *self);
/* @0x10002a20 */
void TV_THISCALL Prosody_Reset(Engine *self);
/* @0x1002c5a0 */
int32_t TV_THISCALL Engine_Step(Engine *self);

/* ---- synthesizer (synth.c) ---------------------------------------------- */

/* @0x10003840 */
void TV_THISCALL Synth_ResetTracks(Engine *self);
/* @0x100043c0 */
void TV_THISCALL Synth_InitFilters(Engine *self);
/* @0x1002c920 */
int32_t TV_CDECL Synth_ScaleParam(int32_t index, uint8_t raw);
/* @0x100047e0 */
uint8_t TV_THISCALL Synth_Step(Engine *self);
/* Generate the samples for one parameter frame (not yet decompiled). */
/* @0x10025cb0 */
void TV_THISCALL Synth_Generate(Engine *self, int16_t sample_rate, int16_t *coef);
/* Build one frame of filt_coef from the parameter tracks (not yet
 * decompiled). */
/* @0x10002a40 */
void TV_THISCALL Synth_Frame(Engine *self);
/* @0x10025150 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b);
/* @0x10025280 */
int32_t TV_STDCALL Synth_MulShr11(int32_t a, int32_t b);
/* @0x10025290 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi);
/* @0x10004750 */
uint8_t TV_THISCALL Synth_Gate(Engine *self, int32_t op);
/* @0x100038d0 */
uint8_t TV_THISCALL Tracks_Op(Engine *self, int32_t op, int32_t n);

/* ---- output (output.c) -------------------------------------------------- */

/* @0x100268d0 */
void TV_THISCALL Output_Reset(Engine *self, int16_t sample_rate);

/* ---- stage resets (stages.c) -------------------------------------------- */

/* @0x10031510 */
void TV_THISCALL Stage0_Reset(Engine *self);
/* @0x10060a20 */
void TV_THISCALL Stage1_Reset(Engine *self);
/* @0x1002b3b0 */
void TV_THISCALL Stage2_Reset(Engine *self);
/* @0x1002d9e0 */
void TV_THISCALL Stage3_Reset(Engine *self);
/* @0x1002dee0 */
void TV_THISCALL Stage3_ResetParams(Engine *self);
/* @0x1002bc00 */
void TV_THISCALL Stage4_Reset(Engine *self);
/* @0x1002bc10 */
int32_t TV_THISCALL Stage4_Run(Engine *self);

/* Stage drivers (not yet decompiled).  Stages 0-2 return whether they handed
 * nodes on; stage 3 returns 0 idle, 1 track buffer full, 2 more to do. */
/* @0x10031560 */
uint8_t TV_THISCALL Stage0_Run(Engine *self);
/* @0x100313d0 */
uint8_t TV_CDECL Stage0_CharClass(int32_t cls, uint8_t c);
/* @0x10031490 */
uint8_t TV_CDECL Stage0_IsPlain(char c);
/* @0x10031310 */
uint8_t TV_THISCALL Stage0_MatchWord(Engine *self, uint8_t list, uint8_t fold_case);
/* @0x10032170 */
void TV_THISCALL Stage0_Emit(Engine *self, int32_t type, uint8_t value);
/* @0x100320f0 */
uint8_t TV_THISCALL Stage0_Finish(Engine *self, uint8_t done);
/* Phonetic input mode: "[...]" spelled as phoneme names (phonetic.c). */
/* @0x10032230 */
void TV_THISCALL Stage0_Phonetic(Engine *self);
/* @0x10033290 */
void TV_THISCALL Stage0_PhoneticDigit(Engine *self);
/* @0x100332e0 */
void TV_THISCALL Stage0_PhoneticPair(Engine *self);

/* ---- stage 1: word pronunciation (stage1.c) ----------------------------- */

/* @0x10062830 */
uint8_t TV_THISCALL Stage1_Run(Engine *self);
/* @0x10062da0 */
void TV_THISCALL Stage1_TakeSpan(Engine *self);
/* @0x10062de0 */
uint8_t TV_THISCALL Stage1_ScanAhead(Engine *self);
/* @0x10062e80 */
uint8_t TV_THISCALL Stage1_Gather(Engine *self);
/* @0x10003480 */
void TV_CDECL Word_Classify(const char *word, Node *n);
/* Letter-to-sound rules, used when the lexicon has no entry (not yet
 * decompiled). */
/* @0x1005f7b0 */
void TV_THISCALL Stage1_Rules(Engine *self);
/* Settle the vowel of the syllable just finished. */
/* @0x100605c0 */
void TV_THISCALL Lts_Syllable(Engine *self, int32_t final);
/* Condition program: a strong letter before the syllable break. */
/* @0x100b0528 */
extern const uint8_t g_lts_cond_strong[];

/* @0x10062900 */
Node *TV_THISCALL Stage1_Pronounce(Engine *self);
/* @0x10060a60 */
Node *TV_THISCALL Stage1_Vowel(Engine *self, Node *n);
/* A helper of the per-vowel pass (unverified: never reached by the corpus). */
/* @0x10064200 */
uint8_t TV_THISCALL Stage1_VowelAux(Engine *self);
/* Phrase-level prosody, around the '%' marker (not yet decompiled). */
/* @0x100638a0 */
void TV_THISCALL Stage1_Phrase(Engine *self);
/* @0x10063e40 */
void TV_THISCALL Stage1_SpreadStress(Engine *self);
/* @0x10063ea0 */
void TV_THISCALL Stage1_PhraseEnd(Engine *self);
/* @0x100637f0 */
void TV_THISCALL Stage1_Mark(Engine *self, Node *n);
/* @0x10063170 */
void TV_THISCALL Stage1_Emit(Engine *self);
/* @0x10064100 */
void TV_THISCALL Stage1_Close(Engine *self);

/* ---- letter-to-sound rules (stage1.c) ----------------------------------- */

/* One rule of the letter-to-sound table.  The rules for a letter follow each
 * other in memory and are tried in order. */
typedef struct LtsEntry {
    const char *left;     /* 0x00 left-context letters, matched backwards */
    const uint8_t *out;   /* 0x04 control bytes, then the phonemes to emit */
    const uint8_t *cond;  /* 0x08 right-context condition program */
    const uint32_t *want; /* 0x0c the two feature masks the rule needs */
    const uint32_t *set;  /* 0x10 the two feature masks it leaves behind */
} LtsEntry;

/* The rules for each letter, indexed by the letter ('@'..'['). */
/* @0x100b4ee0 */
extern const LtsEntry *const g_lts_rules[];

/* One affix (prefix/suffix) rule.  The rules for a given letter form a
 * NULL-terminated array of pointers, and `next` chains to the array to try
 * after this one matched. */
typedef struct LtsRule {
    const char *text;     /* 0x00 the letters, in match order */
    const uint8_t *cond;  /* 0x04 context condition program */
    uint8_t b08;          /* 0x08 re-run the lexicon after stripping */
    uint8_t b09;          /* 0x09 word class */
    uint8_t b0a;          /* 0x0a stress level, or > 1: a s1_1c2d code */
    uint8_t b0b;
    const struct LtsRule *const *next; /* 0x0c */
} LtsRule;

/* Rule lists indexed by the last / first letter of the word. */
/* @0x100e4134 */
extern const LtsRule *const *const g_lts_suffix[256];
/* @0x100e4dd4 */
extern const LtsRule *const *const g_lts_prefix[256];
/* Letter-class descriptors: high byte selects a g_phone_attr bank, low byte
 * the bit to test. */
/* @0x100c89e0 */
extern const uint32_t g_lts_class[128];

/* @0x10060140 */
uint8_t TV_THISCALL Lts_TestFeatures(Engine *self, const LtsEntry *r);
/* @0x100601c0 */
uint8_t TV_THISCALL Lts_MatchLeft(Engine *self, const LtsEntry *r);
/* @0x10060210 */
uint8_t TV_THISCALL Lts_TestContext(Engine *self, const uint8_t *cond, Node *n,
                                    int32_t dir);
/* @0x10063480 */
const LtsRule *TV_THISCALL Lts_MatchAffix(Engine *self, Node *a, Node *b,
                                          const LtsRule *const *set, int32_t dir);
/* @0x10063230 */
uint8_t TV_THISCALL Stage1_Lookup(Engine *self);
/* @0x100635a0 */
int32_t TV_THISCALL Lts_ApplyAffix(Engine *self, int32_t dir);

/* The suffix rules whose stems need the spelling repaired ("-ING", "-EST",
 * "-ILY", "-ABLE", "-ABLY", "-OR", "-S").  Their text is the suffix reversed,
 * because a suffix is matched backwards from the end of the word. */
/* @0x100e3880 */ extern const LtsRule g_lts_rule_ing;
/* @0x100e3d50 */ extern const LtsRule g_lts_rule_est;
/* @0x100e3ed0 */ extern const LtsRule g_lts_rule_ily;
/* @0x100e3670 */ extern const LtsRule g_lts_rule_able;
/* @0x100e3eb0 */ extern const LtsRule g_lts_rule_ably;
/* @0x100e3bc0 */ extern const LtsRule g_lts_rule_or;
/* @0x100e3cd0 */ extern const LtsRule g_lts_rule_s;

/* Condition programs the stem repair uses. */
/* @0x100e2a80 */ extern const uint8_t g_lts_cond_stem_ok[];
/* @0x100e2a48 */ extern const uint8_t g_lts_cond_want_e[];
/* @0x100e2b18 */ extern const uint8_t g_lts_cond_no_e[];
/* @0x100e2a90 */ extern const uint8_t g_lts_cond_want_t[];
/* One user-lexicon entry: the spelling and its phoneme string.  The user
 * lexicon is the small sorted table the SAPI lexicon calls add to; the main
 * dictionary is a packed state machine inside the DLL (Lexicon_Try). */
typedef struct LexEntry {
    const char *word;
    const char *pron;
} LexEntry;

/* @0x101312c0 */ extern const LexEntry g_lexicon[];
/* @0x101312b8 */ extern const uint32_t g_lexicon_count;

/* @0x10003980 */
int32_t TV_CDECL UserLex_Compare(const void *a, const void *b);
/* @0x100039c0 */
uint8_t TV_THISCALL UserLex_Try(Engine *self);
void Lexicon_Lock(void);
void Lexicon_Unlock(void);
/* @0x10050d30 */
uint8_t TV_THISCALL Lexicon_Try(Engine *self);

/* ---- built-in dictionary (dict.c) --------------------------------------- */

/* The state machine that packs a word's letters into a lookup key. */
typedef struct DictKeyState {
    uint32_t mask;      /* 0x00 */
    uint32_t mask2;     /* 0x04 */
    uint8_t  next;      /* 0x08 */
    uint8_t  pad09[3];
    uint8_t  shift;     /* 0x0c */
    uint8_t  pad0d[3];
    uint8_t  pstate;    /* 0x10 the phoneme state to start unpacking in */
    uint8_t  pad11[3];
    uint8_t  last_mask; /* 0x14 */
    uint8_t  pad15[3];
} DictKeyState;

/* The state machine that unpacks an entry's phoneme stream. */
typedef struct DictPhState {
    uint32_t mask;      /* 0x00 */
    uint32_t mask2;     /* 0x04 */
    uint8_t  next;      /* 0x08 */
    uint8_t  pad09[3];
    uint8_t  shift;     /* 0x0c */
    uint8_t  pad0d[3];
} DictPhState;

/* @0x100f9a10 */ extern const DictKeyState g_dict_key[];
/* @0x100f9a58 */ extern const DictPhState g_dict_ph[];
/* Entry size per key state and suffix count: [state * 17 + n]. */
/* @0x100f9a98 */ extern const uint8_t g_dict_skip[];
/* The blob's index: one bucket pointer per letter, then a limit. */
/* @0x100f9acc */ extern const uint32_t *const g_dict_base;

/* @0x10050d20 */
uint8_t TV_CDECL Dict_Byte(const void *p);

/* @0x10048010 */
uint8_t TV_STDCALL Phone_IsVowel(uint8_t c);
/* @0x1002b430 */
Node *TV_THISCALL Node_PrevBoundary(Engine *self, Node *n);
/* @0x1002b460 */
Node *TV_THISCALL Node_NextWord(Engine *self, Node *n);
/* @0x1002b2b0 */
uint8_t TV_THISCALL Stage2_Run(Engine *self);
/* @0x1002c980 */
int32_t TV_THISCALL Stage3_Run(Engine *self);

/* ---- feeding (feed.c) --------------------------------------------------- */

/* @0x10055aa0 */
void TV_THISCALL Engine_Feed(Engine *self, const char *text, uint32_t len, uint32_t *ppos);
/* @0x10055f50 */
void TV_THISCALL Engine_Flush(Engine *self, int32_t new_item);

/* ---- preformatter (preformat.c) ----------------------------------------- */

/* @0x100047c0 */
void TV_THISCALL Preformat_Reset(Engine *self);
/* @0x1002c410 */
uint8_t TV_CDECL FoldAccent(uint8_t *c);
/* @0x100050f0 */
void TV_THISCALL Preformat_PutChar(Engine *self, uint8_t c);
/* @0x10004840 */
void TV_THISCALL Preformat_Run(Engine *self);

/* The dword at a given offset of the original 32-bit engine layout: set to 0
 * if it is -1 (see Preformat_Run: digits of a 17th+ CSI parameter). */
void Engine_ZeroDwordIfMinus1(Engine *self, uint32_t off32);

/* ---- TextIn front end (textin.c) ---------------------------------------- */

/* @0x1001aca0 */
int32_t TV_CDECL Bits_Test(int32_t bit, const uint32_t *bits);
/* @0x1001ace0 */
uint32_t TV_CDECL Bits_Set(int32_t bit, uint32_t *bits);
/* @0x1001ad10 */
int32_t TV_CDECL Bits_Next(int32_t bit, const uint32_t *bits);
/* @0x1001ad80 */
int32_t TV_CDECL AllocString(char **p, int32_t n);
/* @0x1001aa80 */
Token *TV_THISCALL TextIn_InsertAfter(TextIn *self, Token *ref);
/* @0x1001ab00 */
Token *TV_THISCALL TextIn_InsertBefore(TextIn *self, Token *ref);
/* @0x1001ab80 */
int32_t TV_THISCALL TextIn_InsertText(TextIn *self, Token *ref, const char *s, int32_t dir);
/* @0x1001ac10 */
Token *TV_THISCALL TextIn_RemoveToken(TextIn *self, Token *t, int32_t dir);
/* @0x1001add0 */
int32_t TV_THISCALL TextIn_GetChar(TextIn *self);
/* @0x1001ae10 */
int32_t TV_THISCALL TextIn_Unget(TextIn *self);
/* @0x1001ae30 */
int32_t TV_THISCALL TextIn_PutString(TextIn *self, const char *s);
/* @0x1001ae80 */
int32_t TV_THISCALL TextIn_Error(TextIn *self, int32_t code);
/* @0x10029b10 */
TextIn *TV_THISCALL TextIn_Construct(TextIn *self, int32_t mode);
/* @0x10029b40 */
int32_t TV_THISCALL TextIn_Reset(TextIn *self);
/* @0x10055ec0 */
uint8_t TV_THISCALL Engine_CreateTextIn(Engine *self);
/* @0x1002a0b0 */
int32_t TV_THISCALL TextIn_ReadEscape(TextIn *self);
/* @0x10029cb0 */
int32_t TV_THISCALL TextIn_ReadToken(TextIn *self, Token **out);
/* @0x1002a260 */
int32_t TV_THISCALL TextIn_Split(TextIn *self, Token **pt);
/* @0x1002b9e0 */
int32_t TV_THISCALL TextIn_Stub(TextIn *self, Token *t);
/* @0x1002b710 */
int32_t TV_THISCALL TextIn_Classify(TextIn *self, Token *t);
/* @0x1002b9f0 */
int32_t TV_THISCALL TextIn_ClassifyNumber(TextIn *self, Token *t);
/* @0x10029c20 */
int32_t TV_THISCALL TextIn_Tokenize(TextIn *self);
/* @0x100253b0 */
int32_t TV_THISCALL TextIn_Advance(TextIn *self);
/* @0x10025b80 */
int32_t TV_THISCALL TextIn_ExpandToken(TextIn *self, Token **pt);
/* @0x10025bb0 */
int32_t TV_THISCALL TextIn_Emit(TextIn *self, int32_t all);
/* @0x10029b70 */
int32_t TV_THISCALL TextIn_Flush(TextIn *self, int32_t final);
/* @0x1002a450 */
int32_t TV_THISCALL TextIn_Mode4(TextIn *self, Token *t);
/* @0x10025450 */
int32_t TV_THISCALL TextIn_Expand(TextIn *self, Token *t, char *buf1, char *buf2);

/* ---- input stage (input.c) ---------------------------------------------- */

/* @0x10027ef0 */
uint8_t TV_THISCALL Engine_InputStage(Engine *self);

/* ---- control commands (control.c) --------------------------------------- */

/* Execute the control node at the running stage's ctl pointer; returns 0 if
 * the stage must stop there. */
/* @0x10028340 */
uint8_t TV_THISCALL Engine_RunControl(Engine *self);

/* Calls back into the owning SAPI layer (sapi.c; no-ops when standalone). */
void Sapi_Lock(SapiCentral *s);
void Sapi_Unlock(SapiCentral *s);
void Sapi_Post(SapiCentral *s, uint32_t msg, uint32_t wp, uint32_t lp);
int32_t Sapi_QueuePush(SapiCentral *s, const void *data, uint32_t size);

/* ---- input rings (ring.c) ------------------------------------------------ */

/* @0x100281f0 */
int32_t TV_THISCALL Engine_InFree(Engine *self);
/* @0x10028210 */
int32_t TV_THISCALL Engine_InGet(Engine *self);
/* @0x10028250 */
uint8_t TV_THISCALL Engine_InUnget(Engine *self);
/* @0x10028280 */
uint8_t TV_THISCALL Engine_InPut(Engine *self, uint8_t c);
/* @0x100282d0 */
uint8_t TV_THISCALL Engine_InPutEnd(Engine *self);
/* @0x10028140 */
int32_t TV_THISCALL Engine_MidFree(Engine *self);
/* @0x10028160 */
int32_t TV_THISCALL Engine_MidGet(Engine *self);
/* @0x100281a0 */
uint8_t TV_THISCALL Engine_MidPut(Engine *self, uint8_t c);

#endif
