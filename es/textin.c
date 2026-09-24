/*
 * The TextIn tokenizer's edges: the two ways it reads a character from the
 * engine, the way it writes a string back, and the string allocator its
 * tokens use.
 *
 * TextIn sits between Engine_Feed and the preformatter when the SAPI
 * "TextIn" option is on, which it is by default.  It pulls characters out of
 * the input ring itself, splits them into tokens, rewrites some of them --
 * numbers, abbreviations -- and puts the result back through
 * Preformat_PutChar.  These four are the parts that touch the engine; the
 * tokenizing proper is still to do.
 */
#include "es_engine.h"

/* @0x1001dc10 */
int32_t TV_THISCALL TextIn_GetChar(TextIn *self)
{
    int32_t c;
    if (self->engine == NULL)
        return -1;
    c = Engine_InGet(self->engine);
    if (c == -1) {
        self->engine->st_input_empty = 1;
        self->input_done = 1;
        return -2;
    }
    return c;
}

/* @0x1001dc50 */
int32_t TV_THISCALL TextIn_Unget(TextIn *self)
{
    if (self->engine == NULL)
        return -1;
    return Engine_InUnget(self->engine);
}

/* Feed a string to the preformatter a character at a time, and wake the
 * engine if anything went in. */
/* @0x1001dc70 */
int32_t TV_THISCALL TextIn_PutString(TextIn *self, const char *s)
{
    int32_t n = (int32_t)strlen(s);
    int32_t i;

    for (i = 0; i < n; i++)
        Preformat_PutChar(self->engine, (uint8_t)s[i]);
    if (i > 0)
        self->engine->st_idle = 0;
    return 0;
}

/* Replace the string held in *p with room for n characters.  A length of
 * zero frees without allocating, and the caller is left with NULL. */
/* @0x1001dbc0 */
int32_t TV_CDECL AllocString(char **p, int32_t n)
{
    char *s;

    if (*p != NULL)
        tv_free(*p);
    *p = NULL;
    if (n == 0)
        return 1;
    s = (char *)tv_malloc((size_t)(n + 1));
    if (s == NULL)
        return -1;
    *p = s;
    return 1;
}

/* Take one token off the list and free everything hanging off it.  dir picks
 * which neighbour to hand back: -1 for the one before, anything else for the
 * one after. */
/* @0x1001d960 */
Token *TV_THISCALL TextIn_RemoveToken(TextIn *self, Token *t, int32_t dir)
{
    Token *ret;

    if (t == NULL)
        return NULL;
    ret = dir == -1 ? t->prev : t->next;
    /* English updates self->head when there is no previous token; this one
     * treats that as a caller error and gives up, so the list it works on
     * always has something in front. */
    if (t->prev == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev->next = t->next;
    if (t->next != NULL)
        t->next->prev = t->prev;
    if (t->text != NULL)
        tv_free(t->text);
    if (t->text2 != NULL)
        tv_free(t->text2);
    if (t->types != NULL)
        tv_free(t->types);
    tv_free(t);
    self->count--;
    return ret;
}

/* @0x1001d740 */
Token *TV_THISCALL TextIn_InsertAfter(TextIn *self, Token *ref)
{
    Token *t = (Token *)tv_malloc(sizeof(Token));

    t->prev = ref;
    if (ref == NULL) {
        /* the same refusal as TextIn_RemoveToken, and the token just
         * allocated is left where it is -- the original does not free it */
        TextIn_Error(self, 0);
        return NULL;
    }
    t->next = ref->next;
    ref->next = t;
    if (t->next != NULL)
        t->next->prev = t;
    if (self->tail == ref)
        self->tail = t;

    t->w08 = 0;
    t->w0a = 0;
    t->bits[0] = t->bits[1] = t->bits[2] = 0;
    t->d18 = NULL;
    t->d1c = 0;
    t->text = NULL;
    t->len = 0;
    t->text2 = NULL;
    t->types = NULL;
    t->trail = 0;
    t->is_number = 0;
    t->num = 0;
    t->w34 = 1;

    self->count++;
    return t;
}

/* Read tokens until the list is full or the input runs out.  In mode 4 a
 * token whose predecessor carries bit 0x53 gets special handling; English
 * tests bit 0x45 for the same thing.
 *
 * That 0x53 is read straight out of the disassembly ("push 0x53") and is not
 * covered by any test.  Bit 83 is set by TextIn_ReadToken and TextIn_Split,
 * neither of which is written yet, and nothing in tests/corpus_es produces a
 * token carrying it -- putting 0x45 here instead passes all 205
 * configurations even with -M 4.  It becomes testable once the tokenizer
 * proper is decompiled and it is possible to say which tokens get the bit. */
/* @0x1001c950 */
int32_t TV_THISCALL TextIn_Tokenize(TextIn *self)
{
    int32_t n = 0;
    int32_t room = 210 - self->count;
    Token *t;

    if (room < 1)
        return 1;
    while (room > n) {
        if (TextIn_ReadToken(self, &t) != 1)
            break;
        n++;
        TextIn_Split(self, &t);
        if (self->mode == 4 && t->prev != NULL && Bits_Test(0x53, t->prev->bits))
            TextIn_Mode4(self, t);
    }
    return n;
}

/* @0x1001c8a0 */
int32_t TV_THISCALL TextIn_Flush(TextIn *self, int32_t final)
{
    int i;

    if (final == 0) {
        TextIn_Tokenize(self);
        if (self->input_done && self->item_done) {
            while (TextIn_Advance(self))
                ;
            TextIn_Emit(self, 1);
            return 1;
        }
        for (i = 10; i != 0; i--)
            TextIn_Advance(self);
        TextIn_Emit(self, 0);
        return 10;
    }
    while (TextIn_Tokenize(self) > 0) {
        for (i = 10; i != 0; i--)
            TextIn_Advance(self);
        TextIn_Emit(self, 0);
    }
    while (TextIn_Advance(self))
        ;
    TextIn_Emit(self, 1);
    return 1;
}
