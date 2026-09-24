/*
 * Stage 0 helpers.
 *
 * Stage 0 is the letter-to-sound pass: a bytecode interpreter walking the
 * rule table, with a call stack of twenty frames, matching the characters in
 * its window against word lists and character classes.  The interpreter
 * itself is `Stage0_Run` and is not written yet; these are the four pieces
 * around it -- what resets it, how it classifies a character, how it matches
 * a word, and how it hands its work to stage 1.
 */
#include "es_engine.h"

/* The rule bytecode the interpreter walks. */
/* @0x1005fb88 */
extern const uint8_t g_stage0_rules[];

/* Word lists, one NULL-terminated array of strings per list number.  Each
 * string is the word, a NUL, and then the replacement text the rule uses. */
/* @0x1005fb18 */
extern const char *const *const g_stage0_words[];

/* @0x1001427c */
extern const uint8_t g_s0_class_index[16];

/* @0x10013310 */
void TV_THISCALL Stage0_Reset(Engine *self)
{
    self->s0_sp = self->s0_stack;
    self->s0_1b3d = 0;
    self->s0_1c1c = 0;
    self->stage_ctx[0].type_mask = 0x17; /* types 0, 1, 2 and 4 */
    self->s0_ip = g_stage0_rules;
    self->s0_need_test = 1;
    self->s0_pending_ptr = &self->s0_pending;
}

/* Is this character in that class?  The sixteen class numbers map onto six
 * tests through an index table, and the table is byte for byte the English
 * one.  What the tests do is not quite: the letter class here also accepts
 * '~' and '`' alongside the apostrophe, which is what FoldAccent leaves
 * behind when it splits an accented character into a base and a mark. */
/* @0x100141d0 */
uint8_t TV_CDECL Stage0_CharClass(int32_t cls, uint8_t c)
{
    int32_t i = cls - 4;
    if ((uint32_t)i > 15)
        return 0;
    /* The original compares signed throughout, which comes to the same thing
     * for every byte value: a byte of 0x80 or more fails the lower bound
     * signed and the upper bound unsigned.  The exception is the control
     * test, where the sign is the point and the cast is kept. */
    switch (g_s0_class_index[i]) {
    case 0:
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '\'' || c == '~' || c == '`';
    case 1:
        return c >= '0' && c <= '9';
    case 2:
        return c >= 'a' && c <= 'z';
    case 3:
        return c >= 'A' && c <= 'Z';
    case 4:
        return (int8_t)c < 0x20;
    case 5:
        return c == '.';
    default:
        return 0;
    }
}

/* Does the window start with any word from this list?  On a match,
 * s0_word_data is left pointing just past the word's terminator, which is
 * where the rule finds what to say instead. */
/* @0x10014130 */
uint8_t TV_THISCALL Stage0_MatchWord(Engine *self, uint8_t list, uint8_t fold_case)
{
    StageCtx *st = &self->stage_ctx[0];
    const char *const *words = g_stage0_words[list];
    const char *w;
    Node *n;

    for (;;) {
        w = *words;
        if (w == NULL)
            return 0;
        words++;
        n = st->d14;
        for (;;) {
            uint8_t c;
            st->d18 = n;
            n = st->d18;
            c = n->value;
            if (fold_case == 1 && c >= 'A' && c <= 'Z')
                c += 0x20;
            if (NODE_TYPE(n) == 1 && (uint8_t)*w == c) {
                w++;
                if (*w != 0 && st->scan != n) {
                    n = n->next;
                    continue;
                }
            }
            break;
        }
        if (*w == 0 && st->scan == st->d18) {
            self->s0_word_data = (const uint8_t *)(w + 1);
            /* English also raises s0_1c1c for lists 1, 0xd and 0xe here.
             * This engine does not. */
            return 1;
        }
        w++;
    }
}

/* Close the stage: decide how far it got and pass that to stage 1. */
/* @0x10014010 */
uint8_t TV_THISCALL Stage0_Finish(Engine *self, uint8_t done)
{
    StageCtx *st = &self->stage_ctx[0];

    if (done) {
        st->cur = st->ctl;
    } else {
        st->cur = st->first;
        if (st->ctl != st->first) {
            for (;;) {
                Node *n = st->cur;
                uint32_t t = NODE_TYPE(n);
                if (t != 0 && (t != 3 || n->value != ']'))
                    break;
                st->cur = Engine_StageNext(self, n);
                if (st->cur == st->ctl)
                    break;
            }
        }
    }
    if (Engine_StageEnd(self))
        done = 1;
    return done;
}
