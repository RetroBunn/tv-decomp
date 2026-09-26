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

/* A stub that takes a token and returns success without touching it.  Both
 * call sites are in TextIn_Split, on the path where the rule lookup did not
 * match the token, so it is named for where it sits rather than for what it
 * did before the shipping build compiled it away to "mov eax,1; ret 4".
 * Engine_Trace and Engine_Error went the same way; this is the third. */
/* @0x1001fdd0 */
int32_t TV_THISCALL TextIn_Unmatched(TextIn *self, Token *t)
{
    (void)self;
    (void)t;
    return 1;
}

/*
 * Insert a fresh token in front of a reference token.  The mirror of
 * TextIn_InsertAfter, with the same fields initialised and the same
 * refusal -- there is nothing in front of the head node, so it gives up
 * rather than growing the list past it.
 *
 * It gives up too late, though.  By the time it decides the reference has
 * no predecessor it has already written "ref->prev = t", so the refused
 * token is left linked in front of ref with a NULL prev, where nothing owns
 * it and self->head does not know about it, and the token itself is never
 * freed.  TextIn_InsertAfter refuses before it links, which is why the note
 * there only has to mention the leak.  Nothing in the corpus reaches either
 * refusal; both are reachable only through a caller that has already lost
 * track of the head.
 */
/* @0x1001d7c0 */
Token *TV_THISCALL TextIn_InsertBefore(TextIn *self, Token *ref)
{
    Token *t = (Token *)tv_malloc(sizeof(Token));

    t->next = ref;
    t->prev = NULL;
    if (ref == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev = ref->prev;
    ref->prev = t;
    if (t->prev == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev->next = t;

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

/*
 * Put back the token TextIn_Detach lifted out, beside a reference token.
 *
 * This is the other half of TextIn.detached, and between them they are how a
 * rule moves a token: opcode 74 lifts it, opcodes 75 and 76 drop it in
 * before or after somewhere else.  The slot holds one token and is cleared
 * on the way out, so a second reattach without a detach in between does
 * nothing and answers NULL.
 *
 * Inserting before has the same late refusal as TextIn_InsertBefore -- it
 * writes ref->prev before it decides the reference had no predecessor --
 * except that here the token it leaves behind was already in the list once.
 *
 * Inserting after sets TextIn.cur when the token lands at the end, where
 * TextIn_InsertAfter sets TextIn.tail in the same situation.  The two are
 * different fields, four bytes apart, and the asymmetry is the original's.
 */
/* @0x1001d850 */
Token *TV_THISCALL TextIn_Reattach(TextIn *self, Token *ref, int32_t dir)
{
    Token *t = self->detached;

    if (t == NULL || ref == NULL)
        return NULL;
    self->detached = NULL;
    if (dir == -1) {
        t->next = ref;
        t->prev = NULL;
        t->prev = ref->prev;
        ref->prev = t;
        if (t->prev == NULL) {
            TextIn_Error(self, 0);
            return NULL;
        }
        t->prev->next = t;
        self->count++;
        return t;
    }
    t->prev = ref;
    t->next = NULL;
    t->next = ref->next;
    ref->next = t;
    if (t->next == NULL) {
        self->cur = t;
        self->count++;
        return t;
    }
    t->next->prev = t;
    self->count++;
    return t;
}

/*
 * The TextIn object's construction, reset and one-token advance.
 *
 * TextIn_Construct is where the abbreviation index gets built, the first
 * time an engine is asked for a tokenizer.  It sets the list up as a single
 * head node with everything in it cleared, points head, cur and tail at that
 * node, and takes the mode the SAPI object was carrying.
 *
 * TextIn_Reset does the same to an object that already exists, minus the
 * head node's own fields, and then calls sub_10022970 when the mode is 4 --
 * the one arm of it nothing in the corpus reaches.
 *
 * TextIn_Advance moves cur on by one token and decides what the new one
 * needs.  A token that already has a value in d1c is taken as finished; so
 * is one carrying flag 0x52 when ti_04 is 1.  Anything else goes to the rule
 * runner, which is what eventually reaches Rule_Eval.
 */
/* @0x1001c790 */
TextIn *TV_THISCALL TextIn_Construct(TextIn *self, int32_t mode)
{
    Token *h;
    int i;

    Abbrev_Init();
    self->head = &self->head_node;
    self->head->prev = NULL;
    h = self->head;
    h->next = h->prev;
    self->head->w0a = 0;
    h = self->head;
    h->w08 = h->w0a;
    for (i = 0; i < 3; i++)
        self->head->bits[i] = 0;
    self->head->d18 = NULL;
    self->head->d1c = 0;
    self->head->text2 = NULL;
    self->head->text = self->head->text2;
    self->head->len = 0;
    self->head->types = NULL;
    self->head->trail = 0;
    self->head->w32 = 0;
    self->head->w34 = 0;
    self->head->is_number = 0;
    self->head->num = 0;

    self->ti_6e = 0;
    self->detached = NULL;
    self->cur = self->head;
    self->tail = self->head;
    self->ti_74 = 0;
    self->count = 0;
    self->err_count = 0;
    self->mode = mode;
    self->ti_04 = 0;
    return self;
}

/* @0x1001c850 */
int32_t TV_THISCALL TextIn_Reset(TextIn *self)
{
    Token *h;

    self->detached = NULL;
    self->head = &self->head_node;
    self->head->prev = NULL;
    h = self->head;
    h->next = h->prev;
    self->ti_6e = 0;
    self->cur = self->head;
    self->tail = self->head;
    self->count = 0;
    self->ti_74 = 0;
    self->err_count = 0;
    if (self->mode == 4)
        TextIn_Mode4Reset(self);
    return 1;
}

/* @0x1001e0d0 */
int32_t TV_THISCALL TextIn_Advance(TextIn *self)
{
    Token *t;

    t = self->head == self->cur ? self->head->next : self->cur->next;
    if (t == NULL)
        return 0;
    if (self->ti_04 == 1 && !Bits_Test(0x52, t->bits)) {
        self->cur = t;
        return 1;
    }
    if (t->d1c != 0) {
        self->cur = t;
        return 1;
    }
    Rule_Run(self, &t);
    self->cur = t;
    return 1;
}

/*
 * Give the engine a tokenizer.
 *
 * The mode comes from the SAPI object when there is one and is zero when the
 * engine is standalone, which is the one place the tokenizer's behaviour
 * depends on the host.  A failed allocation is reported rather than
 * crashed on, and leaves the engine without a tokenizer -- Engine_Flush
 * tests for that before it uses one.
 */
/* @0x1001c6c0 */
uint8_t TV_THISCALL Engine_CreateTextIn(Engine *self)
{
    int32_t mode = 0;
    TextIn *t;

    if (self->sapi != NULL)
        mode = self->sapi->textin_mode;
    t = (TextIn *)tv_new(sizeof(TextIn));
    if (t != NULL)
        t = TextIn_Construct(t, mode);
    if (t == NULL)
        return 0;
    t->engine = self;
    self->textin = t;
    return 1;
}
