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
/* @0x10002a40 */
void TV_THISCALL Synth_10002a40(Engine *self);
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
/* Letter-by-letter spelling (not yet decompiled). */
/* @0x10032230 */
void TV_THISCALL Stage0_Spell(Engine *self);
/* @0x10062830 */
uint8_t TV_THISCALL Stage1_Run(Engine *self);
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
