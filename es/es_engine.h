/*
 * The Spanish engine object.
 *
 * This is a second decompilation, not a translation of the English one: the
 * November 1995 engines and the October 1997 English engine are different
 * builds with different structure layouts, so nothing here may be assumed
 * from src/ without checking it against CGRM_ES.DLL.  docs/SPANISH.md says
 * how each fact was established and es/engine.fields carries the layout.
 *
 * Address annotations (/ * @0x1000e5a0 * /) name the function's address in
 * CGRM_ES.DLL, which is what tools/gen_hookmap.py binds for the hook build.
 * They are Spanish addresses; the identical-looking ones in src/ are not.
 */
#ifndef TV_ES_ENGINE_H
#define TV_ES_ENGINE_H

#include "tv_common.h"

#include "es_engine_struct.h" /* generated from es/engine.fields */

/* in_ring and mid_ring are 0x1000; pre_ring is 0x100 (Preformat_Run masks
 * its index with 0xff). */
#define TV_ES_RING_SIZE 0x1000

/* ---- the character rings (ring.c) ---------------------------------------- */

/* @0x1000e5a0 */
int32_t TV_THISCALL Engine_InFree(Engine *self);
/* @0x1000e5c0 */
int32_t TV_THISCALL Engine_InGet(Engine *self);
/* @0x1000e600 */
uint8_t TV_THISCALL Engine_InUnget(Engine *self);
/* @0x1000e630 */
uint8_t TV_THISCALL Engine_InPut(Engine *self, uint8_t c);
/* @0x1000e680 */
uint8_t TV_THISCALL Engine_InPutEnd(Engine *self);
/* @0x1000e4f0 */
int32_t TV_THISCALL Engine_MidFree(Engine *self);
/* @0x1000e510 */
int32_t TV_THISCALL Engine_MidGet(Engine *self);
/* @0x1000e550 */
uint8_t TV_THISCALL Engine_MidPut(Engine *self, uint8_t c);

/* ---- moving input onward (flush.c) --------------------------------------- */

/* @0x1001c710 */
void TV_THISCALL Engine_Flush(Engine *self, int32_t new_item);

/* ---- the preformatter (preformat.c) -------------------------------------- */

/* @0x10008060 */
void TV_THISCALL Preformat_PutChar(Engine *self, uint8_t c);

/* ---- the input stage (input.c) ------------------------------------------ */

/* Node types: the low three bits of Node.flags. */
#define NODE_TYPE_MASK 7u
#define NODE_CONTROL   0u
#define NODE_TYPE(n)   ((n)->flags & NODE_TYPE_MASK)
#define NODE_FREE      6u
#define NODE_SENTINEL  7u

/* Engine_ResetNodes links 0x26a of them, and 618 * 0x1c from 0x928 ends
 * exactly where in_ring begins. */
#define TV_ES_NODE_POOL 618

/* @0x1000e290 */
uint8_t TV_THISCALL Engine_InputStage(Engine *self);

/* ---- the node pool (node.c) ---------------------------------------------- */

/* @0x10008bd0 */
Node *TV_THISCALL Engine_NodeAlloc(Engine *self, Node *ref, int32_t after,
                                   int32_t type, uint8_t value);
/* @0x10008b60 */
Node *TV_THISCALL Engine_AppendNode(Engine *self, int32_t type, int32_t value);
/* @0x10008dc0 */
Node *TV_THISCALL Engine_NodeFree(Engine *self, Node *n, int32_t forward);

/* ---- list surgery (list.c) ----------------------------------------------- */

/* @0x10009110 */
Node *TV_THISCALL Engine_Unlink(Engine *self, Node *n);
/* @0x10008ed0 */
Node *TV_THISCALL Engine_InsertBefore(Engine *self, Node *n, Node *before);

/* ---- the escape parser (escape.c) ---------------------------------------- */

/* @0x10007810 */
void TV_THISCALL Preformat_Run(Engine *self);
/* @0x10014db0 */
uint8_t TV_CDECL FoldAccent(uint8_t *c);
void Engine_ZeroDwordIfMinus1(Engine *self, uint32_t off32);

/* ---- leaf utilities (util.c) --------------------------------------------- */

/* @0x1001da50 */
int32_t TV_CDECL Bits_Test(int32_t bit, const uint32_t *bits);
/* @0x1001da90 */
uint32_t TV_CDECL Bits_Set(int32_t bit, uint32_t *bits);
/* @0x1001dac0 */
uint32_t TV_CDECL Bits_Clear(int32_t bit, uint32_t *bits);
/* @0x1001daf0 */
int32_t TV_CDECL Bits_Next(int32_t bit, const uint32_t *bits);
/* @0x1001a930 */
Node *TV_THISCALL Node_PrevBoundary(Engine *self, Node *n);
/* @0x1001a9b0 */
Node *TV_THISCALL Node_NextWord(Engine *self, Node *n);
/* @0x1001a960 */
uint8_t TV_STDCALL Phone_TestMask(Node *n, int32_t mask, int32_t neg);
/* @0x1000aee0 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi);
/* @0x1000af00 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b);

/* ---- the TextIn tokenizer (textin.c) -------------------------------------- */

/* @0x1001dc10 */
int32_t TV_THISCALL TextIn_GetChar(TextIn *self);
/* @0x1001dc50 */
int32_t TV_THISCALL TextIn_Unget(TextIn *self);
/* @0x1001dc70 */
int32_t TV_THISCALL TextIn_PutString(TextIn *self, const char *s);
/* @0x1001dbc0 */
int32_t TV_CDECL AllocString(char **p, int32_t n);
/* @0x1001d960 */
Token *TV_THISCALL TextIn_RemoveToken(TextIn *self, Token *t, int32_t dir);
/* @0x1001d740 */
Token *TV_THISCALL TextIn_InsertAfter(TextIn *self, Token *ref);
/* @0x1001c950 */
int32_t TV_THISCALL TextIn_Tokenize(TextIn *self);
/* @0x1001c8a0 */
int32_t TV_THISCALL TextIn_Flush(TextIn *self, int32_t final);

/* ---- the C runtime the engine calls --------------------------------------
 * Statically linked into CGRM_ES.DLL, so the hook build binds these to the
 * DLL's own copies and memory from its heap is always freed by its heap.
 * The addresses are Spanish; src/crt.h has the English ones. */

/* @0x10023a06 */
void *TV_CDECL tv_malloc(size_t n);
/* @0x1002397d */
void TV_CDECL tv_free(void *p);
/* @0x100235d7 */
void *TV_CDECL tv_new(size_t n);

/* the tokenizer's inner parts, still to do */
/* @0x1001c9e0 */
int32_t TV_THISCALL TextIn_ReadToken(TextIn *self, Token **out);
/* @0x1001d260 */
void TV_THISCALL TextIn_Split(TextIn *self, Token **t);
/* @0x1001d470 */
void TV_THISCALL TextIn_Mode4(TextIn *self, Token *t);
/* @0x1001e0d0 */
int32_t TV_THISCALL TextIn_Advance(TextIn *self);
/* @0x1001f850 */
void TV_THISCALL TextIn_Emit(TextIn *self, int32_t final);
/* @0x1000e120 */
void TV_THISCALL Queue_Push(void *queue, void *data, int32_t len);
/* @0x1001dce0 */
void TV_THISCALL TextIn_Error(TextIn *self, int32_t code);

/* ---- the engine object's life (engine.c) ---------------------------------- */

/* @0x1000ddc0 */
Engine *TV_THISCALL Engine_Construct(Engine *self);
/* @0x100086a0 */
int32_t TV_THISCALL Engine_Init(Engine *self);
/* @0x10008700 */
int32_t TV_THISCALL Engine_Reset(Engine *self);
/* @0x1000e240 */
void TV_THISCALL Engine_ResetRings(Engine *self);
/* @0x10008ef0 */
void TV_THISCALL Engine_ResetNodes(Engine *self);
/* @0x100087d0 */
int32_t TV_THISCALL Engine_Step(Engine *self);

/* the reset chain and the stages, still to do */
/* @0x10007790 */
void TV_THISCALL Preformat_Reset(Engine *self);
/* @0x10017850 */
void TV_THISCALL Synth_ResetTracks(Engine *self);
/* @0x1000e6f0 */
void TV_THISCALL Synth_InitFilters(Engine *self);
/* @0x1000ad50 */
void TV_THISCALL Output_Reset(Engine *self, int16_t rate);
/* @0x1000f070 */
void TV_THISCALL Prosody_Reset(Engine *self);
/* @0x1001bf10 */
void TV_THISCALL Stage3_ResetParams(Engine *self);
/* @0x1001a8b0 */
void TV_THISCALL Stage2_Reset(Engine *self);
/* @0x1000fa10 */
void TV_THISCALL Stage1_Reset(Engine *self);
/* @0x10013360 */
uint8_t TV_THISCALL Stage0_Run(Engine *self);
/* @0x1000fa30 */
uint8_t TV_THISCALL Stage1_Run(Engine *self);
/* @0x1001a070 */
uint8_t TV_THISCALL Stage2_Run(Engine *self);
/* @0x1001ad30 */
int32_t TV_THISCALL Stage3_Run(Engine *self);
/* @0x100077b0 */
uint8_t TV_THISCALL Synth_Step(Engine *self);
/* @0x10017900 */
uint8_t TV_THISCALL Tracks_Op(Engine *self, int32_t op, int32_t arg);

/* ---- stage windows (stage.c) --------------------------------------------- */

/* @0x10009020 */
uint8_t TV_THISCALL Engine_TypeSelected(Engine *self, Node *n);
/* @0x10008ca0 */
Node *TV_THISCALL Engine_StagePrev(Engine *self, Node *n);
/* @0x10008ea0 */
Node *TV_THISCALL Engine_StageNext(Engine *self, Node *n);
/* @0x10009050 */
uint8_t TV_THISCALL Engine_StageBegin(Engine *self, StageCtx *st);
/* @0x10008cd0 */
uint8_t TV_THISCALL Engine_StageEnd(Engine *self);

/* ---- control nodes (control.c) -------------------------------------------- */

/* @0x1000ead0 */
uint8_t TV_THISCALL Engine_RunControl(Engine *self);

/* ---- stage 0 helpers (stage0.c) ------------------------------------------ */

/* @0x10013310 */
void TV_THISCALL Stage0_Reset(Engine *self);
/* @0x100141d0 */
uint8_t TV_CDECL Stage0_CharClass(int32_t cls, uint8_t c);
/* @0x10014130 */
uint8_t TV_THISCALL Stage0_MatchWord(Engine *self, uint8_t list, uint8_t fold_case);
/* @0x10014010 */
uint8_t TV_THISCALL Stage0_Finish(Engine *self, uint8_t done);

/* ---- stage 4 and the parameter tracks (stage4.c) ------------------------- */

/* @0x10004810 */
void TV_THISCALL Stage4_Reset(Engine *self);
/* @0x10004820 */
int32_t TV_THISCALL Stage4_Run(Engine *self);
/* @0x10017d90 */
void TV_STDCALL Track_Fill(uint8_t *buf, int32_t pos, int32_t n, uint8_t value);
/* @0x1000b060 */
void TV_THISCALL Track_BlendFwd(Engine *self, uint8_t *buf, int32_t pos,
                                int32_t shape, int32_t n, uint8_t target);
/* @0x1000afe0 */
void TV_THISCALL Track_BlendBack(Engine *self, uint8_t *buf, int32_t pos,
                                 int32_t shape, int32_t n, uint8_t target);

/* ---- stage 2 and stage 3 helpers (stage2.c, stage3.c) -------------------- */

/* @0x1001aa60 */
uint8_t TV_THISCALL Stage2_Scan(Engine *self, int32_t dir, int32_t count,
                                int32_t mask1, int32_t mask2, int32_t mode);
/* @0x1001b810 */
void TV_THISCALL Stage3_Reset(Engine *self);
/* @0x1001be30 */
Node *TV_THISCALL Stage3_Insert(Engine *self, Node *ref, int32_t mode);

/* ---- not written yet ------------------------------------------------------
 * Declared with an address and nothing else.  tools/gen_hookmap.py emits a
 * --defsym for every annotated symbol it cannot find a definition of, so a
 * call to one of these lands in the original inside the loaded DLL.  That is
 * what makes it possible to decompile one function at a time rather than a
 * whole subsystem at once. */

/* @0x1001c850 */
void TV_THISCALL TextIn_Reset(TextIn *self);
/* @0x10008b50 */
void TV_THISCALL Engine_Error(Engine *self, int32_t code);

#endif /* TV_ES_ENGINE_H */
