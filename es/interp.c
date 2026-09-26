/*
 * The rule interpreter.
 *
 * TextIn_Advance looks a token up, gets back a list of rules, and hands each
 * one to this function.  A rule is a stream of int16 words: one opcode per
 * word, operands in the words that follow, evaluated recursively.  The
 * opcode space runs 4..0x5b through an 88-entry jump table, with two values
 * handled outside it -- 3 is logical NOT and -31979 is a bare success.
 * Anything else is an error.
 *
 * Every call evaluates exactly one opcode and returns a flag.  The
 * combinators call back in for each of their operands, so a whole rule is
 * one call that unfolds into a tree.
 *
 *     3           not
 *     4..8        and, over two to six operands
 *     9..12       or, over two to five
 *     13, 14      true, false
 *     15..22      set, compare and range-test rule_trail
 *     23..28      move the cursor
 *     29          compare Token.w08 with an immediate
 *     33..38      test the token's flags
 *     39..59      scan for flags, backwards, forwards or both
 *     60..91      do something: say, spell, insert, remove, detach, move
 *
 * Three arguments come in besides the engine: `cursor`, which is where the
 * interpreter's idea of the current token lives and which most of the moving
 * opcodes write; `anchor`, which opcode 27 restores the cursor from and which
 * the removing opcodes keep out of the way of themselves; and `v`, a value
 * the handlers store into the token they act on.
 *
 * None of the combinators short-circuit.  Every operand of an `and` is
 * evaluated even after one has failed, which matters because operands have
 * side effects -- moving the cursor, setting rule_trail, rewriting a token.
 *
 * Two oddities are kept because they are the original's.  Opcode 28 walks
 * the list using the low half of the register holding `self`, which it gets
 * away with only because nothing reads `self` again before the return; here
 * that is just a loop counter.  And four of the scan opcodes ask Rule_Scan
 * for a `check` of 2, which makes it walk and always answer no.
 *
 * Eight of the handlers are still the original's, bound through the DLL:
 * nothing in the corpus reaches them, so there is nothing to check a
 * decompilation of them against yet.  They are named for the opcode that
 * calls them, which is the only thing established about them.
 */
#include "es_engine.h"

/* the next int16 of the rule stream, sign-extended */
static int32_t next_op(TextIn *self)
{
    int32_t v = *self->rule_ip;

    self->rule_ip++;
    return v;
}

static void take_bits(TextIn *self, uint32_t *set, int n)
{
    int i;

    for (i = 0; i < n; i++)
        Bits_Set(next_op(self), set);
}

/* Step over `n` tokens in the list, refusing to run off either end.  `sig`
 * asks for only the tokens that are not transparent, which is flag 0x51 --
 * the same flag Rule_Scan steps over. */
static int32_t walk(TextIn *self, Token **tp, int32_t n, int32_t back, int sig)
{
    Token *t = *tp;
    int16_t left = (int16_t)n;

    while (left != 0) {
        left--;
        for (;;) {
            Token *nx = back ? t->prev : t->next;
            if (nx == NULL)
                return 0;
            if (back && self->head == nx)
                return 0;
            t = nx;
            if (!sig || !Bits_Test(0x51, t->bits))
                break;
        }
    }
    *tp = t;
    return 1;
}

/* @0x1001e5d0 */
int32_t TV_THISCALL Rule_Eval(TextIn *self, Token **cursor, Token **anchor,
                              uint32_t v)
{
    uint32_t set[3];
    Token *t = *cursor;
    int32_t op = next_op(self);
    int16_t s0, s1, s2, s3, s4;
    int16_t saved;
    int32_t n, lo, hi;

    set[0] = set[1] = set[2] = 0;

    if (op <= 3) {
        if (op == 3)
            return Rule_Eval(self, cursor, anchor, v) == 0;
        if (op == -31979)
            return 1;
        return TextIn_Error(self, 0);
    }
    if ((uint32_t)(op - 4) > 0x57)
        return TextIn_Error(self, 0);

    switch (op) {
    /* ---- combinators.  Every operand runs; none of them short-circuit. */
    case 4:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0;
    case 5:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0;
    case 6:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0 && s3 != 0;
    case 7:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s4 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0 && s3 != 0 && s4 != 0;
    case 8: {
        int16_t s5;
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s4 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s5 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0 && s3 != 0 && s4 != 0 && s5 != 0;
    }
    case 9:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0;
    case 10:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0 || s2 != 0;
    case 11:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0 || s2 != 0 || s3 != 0;
    case 12:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s4 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0 || s2 != 0 || s3 != 0 || s4 != 0;

    case 13:
        return 1;
    case 14:
        return 0;

    /* ---- rule_trail, the interpreter's one register */
    case 15:
        self->rule_trail = (int16_t)next_op(self);
        return 1;
    case 16:
        return (int16_t)(self->rule_trail - (int16_t)next_op(self)) == 0;
    case 17:
        saved = self->rule_trail;
        Rule_Eval(self, cursor, anchor, v);
        return (int16_t)(self->rule_trail - saved) == 0;
    case 18:
        lo = next_op(self);
        hi = next_op(self);
        return (int16_t)lo <= self->rule_trail && (int16_t)hi >= self->rule_trail;
    case 19:
        return self->rule_trail < (int16_t)next_op(self);
    case 20:
        saved = self->rule_trail;
        Rule_Eval(self, cursor, anchor, v);
        return self->rule_trail > saved;
    case 21:
        return self->rule_trail > (int16_t)next_op(self);
    case 22:
        saved = self->rule_trail;
        Rule_Eval(self, cursor, anchor, v);
        return self->rule_trail < saved;

    /* ---- moving the cursor */
    case 23:
        if (!walk(self, &t, next_op(self), 1, 0))
            return 0;
        *cursor = t;
        return 1;
    case 24:
        if (!walk(self, &t, next_op(self), 1, 1))
            return 0;
        *cursor = t;
        return 1;
    case 25:
        if (!walk(self, &t, next_op(self), 0, 0))
            return 0;
        *cursor = t;
        return 1;
    case 26:
        if (!walk(self, &t, next_op(self), 0, 1))
            return 0;
        *cursor = t;
        return 1;
    case 27:
        *cursor = *anchor;
        return 1;
    case 28: {
        /* By rule_trail, either way, and without any of the checks the
         * opcodes above make: this one will walk off the end. */
        int16_t k = self->rule_trail;
        if (k < 0) {
            int16_t j = (int16_t)-k;
            while (j > 0) {
                t = t->prev;
                j--;
            }
        } else {
            while (k > 0) {
                t = t->next;
                k--;
            }
        }
        *cursor = t;
        return 1;
    }
    case 29:
        return (int16_t)(t->w08 - (int16_t)next_op(self)) == 0;

    /* ---- the token's flags */
    case 33:
        return Rule_MatchTrail(self, t);
    case 34:
        take_bits(self, set, 1);
        return Rule_TestBits(set, t, 2);
    case 35:
        take_bits(self, set, 2);
        return Rule_TestBits(set, t, 1);
    case 36:
        take_bits(self, set, 3);
        return Rule_TestBits(set, t, 1);
    case 37:
        take_bits(self, set, 2);
        return Rule_TestBits(set, t, 2);
    case 38:
        take_bits(self, set, 3);
        return Rule_TestBits(set, t, 2);

    /* ---- scanning for flags.  The count follows the flag numbers. */
    case 39: case 40: case 41: case 42: case 43:
        take_bits(self, set, op - 38);
        return Rule_Scan(self, set, t, -1, next_op(self), 1);
    case 44: case 45:
        take_bits(self, set, op - 42);
        return Rule_Scan(self, set, t, -1, next_op(self), 2);
    case 46: case 47: case 48: case 49: case 50:
        take_bits(self, set, op - 45);
        return Rule_Scan(self, set, t, 1, next_op(self), 1);
    case 51: case 52:
        take_bits(self, set, op - 49);
        return Rule_Scan(self, set, t, 1, next_op(self), 2);
    case 53: case 54: case 55: case 56: case 57:
        take_bits(self, set, op - 52);
        n = next_op(self);
        if (Rule_Scan(self, set, t, -1, n, 1))
            return 1;
        return Rule_Scan(self, set, t, 1, n, 1);
    case 58: case 59:
        take_bits(self, set, op - 56);
        n = next_op(self);
        if (Rule_Scan(self, set, t, -1, n, 2))
            return 1;
        /* Rewound so the second pass reads the same count again, which is
         * what Rule_Op60 below needs and what this one does anyway. */
        self->rule_ip--;
        return Rule_Scan(self, set, t, 1, n, 2);
    case 60: case 61:
        take_bits(self, set, op - 59);
        if (Rule_Op60(self, set, t, -1))
            return 1;
        self->rule_ip--;
        return Rule_Op60(self, set, t, 1);

    /* ---- the handlers */
    case 62:
        return Rule_SayRecord(self, t, v, 1);
    case 63:
        return Rule_SayRecord(self, t, v, 0);
    case 64:
        return Rule_Op64(self, t, v, next_op(self), 1);
    case 65:
        return Rule_Op64(self, t, v, next_op(self), 0);
    case 66:
        return Rule_SpellOut(self, t, v, 1);
    case 68:
        Bits_Set(next_op(self), t->bits);
        return 1;
    case 69:
        Bits_Clear(next_op(self), t->bits);
        return 1;
    case 70:
        return Rule_SetTrail(self, t, v);
    case 71:
        return Rule_InsertWord(self, t, v, 1);
    case 72:
        return Rule_InsertWord(self, t, v, -1);
    case 73:
        if (*anchor == t)
            *anchor = t->prev;
        *cursor = TextIn_RemoveToken(self, t, -1);
        return 1;
    case 74:
        if (*anchor == t)
            *anchor = t->prev;
        *cursor = TextIn_Detach(self, t);
        return 1;
    case 75:
        *cursor = TextIn_Reattach(self, t, -1);
        return 1;
    case 76:
        *cursor = TextIn_Reattach(self, t, 1);
        return 1;
    case 77:
        return Rule_Op77(self, t, v);
    case 78:
        if ((int16_t)Rule_Op78(self, &t, v) == 1)
            *cursor = t;
        return 1;
    case 79:
        return Rule_SayNumberText(self, t, v);
    case 80:
        return Rule_Op80(self, t, v);
    case 81:
        return Rule_SayNumberOrSpell(self, t, v);
    case 82:
        return Rule_Op82(self, t, v);
    case 83:
        n = (int16_t)Rule_SayGroupedNumber(self, &t, v, 0);
        *anchor = t;
        *cursor = t;
        return n;
    case 84:
        n = (int16_t)Rule_SayGroupedNumber(self, &t, v, 1);
        *anchor = t;
        *cursor = t;
        return n;
    case 85:
        return Rule_Op85(self, t, v);
    case 86:
        return Rule_SayNumber(self, t, v);
    case 87:
        return Rule_Acronym(self, t, v);

    /* ---- reading something about the token into rule_trail */
    case 88:
        if (t->text == NULL) {
            self->rule_trail = 0;
            return 1;
        }
        self->rule_trail = (int16_t)(int8_t)t->text[0];
        return 1;
    case 89: {
        int16_t len;
        if (t->text == NULL) {
            self->rule_trail = 0;
            return 1;
        }
        len = (int16_t)strlen(t->text);
        self->rule_trail = len;
        if (len == 0)
            return 1;
        self->rule_trail = (int16_t)(int8_t)t->text[len - 1];
        return 1;
    }
    case 90:
        self->rule_trail = t->len;
        return 1;
    case 91:
        self->rule_trail = (int16_t)t->num;
        return 1;

    default:                        /* 30, 31, 32 and 67 */
        return TextIn_Error(self, 0);
    }
}
