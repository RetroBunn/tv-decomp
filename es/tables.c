/*
 * The two lookup tables the engine copies out of .data into .bss the first
 * time something needs them, and never again.
 *
 * This is the .bss question answered for 2 KB of the 103 KB: both tables can
 * be added to at runtime -- the image carries the strings "Save Lexicon
 * in: %s" and "Cannot save lexicon file: %s" -- so the build ships a
 * read-only copy and duplicates it on first use behind a flag nothing ever
 * clears.
 *
 * Each table is a list of cumulative byte offsets into a blob of records
 * that follows it, with entry 0 forced to zero, so record i runs from
 * index[i] to index[i+1].
 *
 * The abbreviation table expands text to text:
 *
 *     index 0x10061860, 506 entries      blob 0x10062068
 *     int32 a, int32 b, int16 reclen, int16 keylen,
 *     key[keylen] NUL, value[] NUL, int32, padding to reclen
 *
 *     "$"      -> "peso"
 *     "%"      -> "por ciento"
 *     "\x10:-)" -> "Sonrisa."
 *
 * The lexicon maps a written word to its phonemes, and is what stage 1
 * consults before its letter-to-sound rules:
 *
 *     index 0x1006ae78, 77 entries       blob 0x1006afb0
 *     int16 reclen, int16 keylen, int16, key[keylen] NUL, phonemes[] NUL
 *
 *     "ANO"    -> "Anj"       (with N-tilde, cp1252 0xd1)
 *     "BABY"   -> "BEbI"
 *     "BIRDIE" -> "B1IrRRrDI"
 *
 * The record layouts are read off the bytes and confirmed against the index
 * -- every reclen equals the gap between consecutive offsets -- but what the
 * two int32s at the head of an abbreviation record hold is not established.
 * The functions that walk these blobs are not decompiled yet; the two here
 * only build the index.
 *
 * The two loaders differ in one way worth keeping: the abbreviation one
 * returns 1 whether or not it did the work, and the lexicon one returns 1
 * only when the table was already there, 0 when it has just built it.
 */
#include "es_engine.h"

/* @0x10061860 */
extern const int32_t g_abbrev_index_data[506];
/* @0x10045898 */
extern int32_t g_abbrev_index[507];
/* @0x10068d3c */
extern int32_t g_abbrev_loaded;

/* @0x1006ae78 */
extern const int32_t g_lex_index_data[77];
/* @0x10046088 */
extern int32_t g_lex_index[78];
/* @0x1006b610 */
extern int32_t g_lex_loaded;

/* @0x1001dd10 */
int32_t TV_CDECL Abbrev_Init(void)
{
    if (g_abbrev_loaded != 0)
        return 1;
    g_abbrev_index[0] = 0;
    memcpy(&g_abbrev_index[1], g_abbrev_index_data, sizeof g_abbrev_index_data);
    g_abbrev_loaded = 1;
    return 1;
}

/* @0x10023120 */
int32_t TV_CDECL Lexicon_Init(void)
{
    if (g_lex_loaded != 0)
        return 1;
    g_lex_index[0] = 0;
    memcpy(&g_lex_index[1], g_lex_index_data, sizeof g_lex_index_data);
    g_lex_loaded = 1;
    return 0;
}
