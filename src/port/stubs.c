/*
 * The one place the engine still calls out to the layer above it.
 *
 * Stage 2 can write the phonemes it decided on to a byte list the SAPI
 * layer allocated, for ITTSDialogs to show.  That list is COM-allocated and
 * the engine only builds it when Engine.w_212c is set, which nothing in the
 * standalone build does -- so there is no list here and nothing to append.
 */
#include "engine.h"

/* @0x100311d0 */
void TV_THISCALL ByteList_Append(void *list, int32_t b)
{
    (void)list;
    (void)b;
}
