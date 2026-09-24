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

/* Pointed at by the engine but not laid out yet; declaring it keeps the
 * field that holds it -- and so every offset after it -- honest. */
typedef struct SapiCentral SapiCentral;

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

/* ---- not written yet ------------------------------------------------------
 * Declared with an address and nothing else.  tools/gen_hookmap.py emits a
 * --defsym for every annotated symbol it cannot find a definition of, so a
 * call to one of these lands in the original inside the loaded DLL.  That is
 * what makes it possible to decompile one function at a time rather than a
 * whole subsystem at once. */

/* the whole ESC [ command set, 1968 bytes of it */
/* @0x10007810 */
void TV_THISCALL Preformat_Run(Engine *self);
/* @0x1001c850 */
void TV_THISCALL TextIn_Reset(TextIn *self);
/* @0x1001c8a0 */
void TV_THISCALL TextIn_Flush(TextIn *self, int32_t flag);

#endif /* TV_ES_ENGINE_H */
