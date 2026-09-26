/*
 * Helpers of the rule interpreter.
 *
 * `TextIn_Advance` looks a token up, gets back a list of rules, and hands
 * each one to `sub_1001e5d0`, which is a recursive interpreter over a
 * bytecode: a stream of int16 words, one opcode per word, dispatched through
 * an 88-entry jump table for opcodes 4..0x5b.  Operands that are single
 * bytes occupy the low half of the following word, which is why every
 * handler here advances `rule_ip` by two and reads one byte.  The
 * interpreter's state lives on the `TextIn` at 0x68 onward -- the 0x3c this
 * object has and the English one does not.
 *
 * The dispatcher itself is not written yet.  These are the pieces of it that
 * are separate functions, so they can be replaced and checked one at a time
 * while the arms that call them are still the original's.
 */
#include "es_engine.h"

/* Does the token carry these flags?  Mode 1 asks whether any of them are
 * set, anything else whether all of them are.  The bit set is Token.bits,
 * which is why the caller is handed the token and the offset is applied
 * here rather than by the interpreter. */
/* @0x10021630 */
int32_t TV_STDCALL Rule_TestBits(const uint32_t *want, Token *t, int32_t any)
{
    if (any == 1)
        return Bits_AnyIn(want, t->bits);
    return Bits_AllIn(want, t->bits);
}

/* Set the token's trailing character from the operand, and park a value in
 * the token's spare word. */
/* @0x10021600 */
int32_t TV_THISCALL Rule_SetTrail(TextIn *self, Token *t, uint32_t v)
{
    uint8_t c = *(const uint8_t *)self->rule_ip;

    self->rule_ip = (int16_t *)((uint8_t *)self->rule_ip + 2);
    t->trail = c;
    t->d1c = v;
    return 1;
}

/* Compare the token's trailing character with the operand, leaving the
 * character behind in rule_trail either way.  The store is sign-extending
 * and the comparison is not, so a trailing character above 0x7f is recorded
 * as a negative number and still matches. */
/* @0x100215d0 */
int32_t TV_THISCALL Rule_MatchTrail(TextIn *self, Token *t)
{
    uint8_t c = *(const uint8_t *)self->rule_ip;

    self->rule_ip = (int16_t *)((uint8_t *)self->rule_ip + 2);
    self->rule_trail = (int16_t)(int8_t)t->trail;
    return (uint8_t)(t->trail - c) == 0;
}

/* Unlink a token and keep it, where TextIn_RemoveToken unlinks it and frees
 * it.  The links are cleared and the token is parked in TextIn.detached.
 *
 * It refuses a token with nothing in front of it, the same refusal
 * TextIn_RemoveToken and TextIn_InsertAfter make, so the list it works on
 * always begins with the head node.  The original computes a return value
 * for that case first -- t->next -- and then takes the error path, which
 * returns zero regardless; the dead arm is kept below because it is what the
 * original does. */
/* @0x1001d9f0 */
Token *TV_THISCALL TextIn_Detach(TextIn *self, Token *t)
{
    Token *ret;

    if (t == NULL)
        return NULL;
    ret = t->prev != NULL ? t->prev : t->next;
    if (t->prev == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev->next = t->next;
    if (t->next != NULL)
        t->next->prev = t->prev;
    t->next = NULL;
    t->prev = NULL;
    self->detached = t;
    self->count--;
    return ret;
}

/* The error sink every part of the tokenizer reaches for.  It keeps the
 * first ten codes and counts them, and always answers -1 so that a caller
 * can return its result straight out.
 *
 * Nothing in the corpus reaches it -- no input the differential test carries
 * makes the tokenizer give up -- so it is one of the few functions here
 * whose evidence is entirely the unit case.  That is also why the ring is
 * worth having decompiled: the moment an input does trip it, the codes are
 * readable from the object rather than lost inside the DLL.
 */
/* @0x1001dce0 */
int32_t TV_THISCALL TextIn_Error(TextIn *self, int32_t code)
{
    if (self->err_count < 10) {
        self->errors[self->err_count] = code;
        self->err_count++;
    }
    return -1;
}

/*
 * Walk away from a token looking for one that carries the wanted flags, and
 * report how far it had to go.
 *
 * Each pass steps one token in the given direction and then keeps stepping
 * while the token it lands on has flag 0x51 set, which is what makes a token
 * transparent to this search; the pass stops on the first token that does
 * not.  Flag 0x54 is a wall: meeting one abandons the search outright.  Up
 * to `count` passes are made, so a count of 2 means "the second real token
 * along", not "within two tokens".
 *
 * The signed number of steps taken is left in TextIn.rule_trail on a hit and
 * is zero on every other path, including the ones that give up early.  It is
 * an int16 and it is not clamped, so a long enough walk wraps; nothing in
 * the corpus gets near that.
 *
 * Backwards, the head node ends the search, which is the same boundary
 * TextIn_Detach and TextIn_RemoveToken refuse to cross.
 *
 * The two flag tests at the end are asked in the order the original asks
 * them, and the second only adds the case the first cannot answer: an empty
 * want set, where nothing intersects but everything contains.  A `check`
 * other than 1 skips both, which makes the whole call a walk that always
 * answers no -- and four of the interpreter's opcodes do exactly that,
 * passing 2.  Whether that is deliberate or a slip is not established; what
 * is established is that the original behaves the same way, over every check
 * value the dispatcher is seen to pass.
 */
/* @0x10021080 */
int32_t TV_THISCALL Rule_Scan(TextIn *self, const uint32_t *want, Token *t,
                              int32_t dir, int32_t count, int32_t check)
{
    int16_t steps = 0;
    int32_t i;

    self->rule_trail = 0;
    if (t == NULL)
        return 0;
    for (i = 0; i < count; i++) {
        int transparent = 1;
        while (transparent) {
            if (dir == -1) {
                t = t->prev;
                if (t == NULL || self->head == t)
                    return 0;
                steps--;
            } else {
                t = t->next;
                if (t == NULL)
                    return 0;
                steps++;
            }
            if (!Bits_Test(0x51, t->bits))
                transparent = 0;
            if (Bits_Test(0x54, t->bits))
                return 0;
        }
        if (check == 1) {
            if (Bits_AnyIn(want, t->bits) || Bits_AllIn(want, t->bits)) {
                self->rule_trail = steps;
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Insert a spoken word next to a token.
 *
 * The operand names one of twenty-six fixed words, the new token is linked
 * in before or after the reference, and the word becomes its replacement
 * text.  This is how a rule turns a symbol into something sayable: "." ->
 * "punto", "$" -> "dolar", "%" -> "por".
 *
 * The table has not been fully translated.  Alongside the Spanish there is
 * "tiret", which is French, "minutes", which is French or English but not
 * Spanish, and "un mitad", which is not how a half is said.  They are left
 * exactly as the image has them: what the engine says is what this table
 * says, and correcting it would change the audio.
 *
 * Two details of the original are kept.  The room asked of AllocString is
 * the word's length plus five, not plus one, so every one of these tokens
 * carries four bytes of slack for whatever appends to it later.  And the
 * operand is read sign-extended, so a negative one indexes off the front of
 * the table; nothing in the corpus produces one.
 */
/* @0x10069b18 */
extern const char *const g_rule_words[26];

/* @0x10021670 */
int32_t TV_THISCALL Rule_InsertWord(TextIn *self, Token *ref, uint32_t v,
                                    int32_t dir)
{
    Token *t;
    const char *w;
    int32_t idx;
    size_t len;

    t = dir == -1 ? TextIn_InsertBefore(self, ref) : TextIn_InsertAfter(self, ref);
    idx = *self->rule_ip++;
    w = g_rule_words[idx];
    len = strlen(w);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, w, len + 1);
    t->d1c = v;
    t->w0a = (int16_t)idx;
    t->w08 = (int16_t)idx;
    t->trail = ' ';
    return 1;
}

/*
 * Spell a token's text out character by character into its replacement
 * text: each character's spoken name, separated by spaces.
 *
 * With `all` set every character above 0x1f is named; with it clear only
 * those whose class word has bit 2 or 3, which is letters and digits.  The
 * control characters at 0x1f and below are dropped either way, which is also
 * what keeps the one empty entry in the name table out of reach -- index
 * 0x1f is the only one of the 256 with no name.
 *
 * The buffer is the original's 132 bytes and the guard is its 100
 * characters, checked before each append rather than after.  That cannot
 * overflow: the longest name in the table is 22 characters, so the worst
 * append starts at 99 and ends at 123.
 *
 * The names are the shipped ones, typos and all -- "signo de pocentaje" is
 * missing its r, "Dollar" never made it out of English, and the exclamation
 * mark is named with a French apostrophe.  All three are audible, and all
 * three stay.
 */
/* @0x10069d08 */
extern const uint32_t g_char_flags[256];
/* @0x1006a2a0 */
extern const char *const g_char_names[256];
/* @0x10069c30 */
extern const char g_str_space[];

/* @0x10022810 */
int32_t TV_THISCALL Rule_SpellOut(TextIn *self, Token *t, uint32_t v,
                                  int32_t all)
{
    char buf[132];
    const char *s = t->text;
    size_t len;

    if (s == NULL)
        return 0;
    buf[0] = 0;
    for (; *s != 0; s++) {
        uint8_t c = (uint8_t)*s;
        if (strlen(buf) >= 0x64)
            break;
        if (c <= 0x1f)
            continue;
        if (all == 0 && (g_char_flags[c] & 0xc) == 0)
            continue;
        strcat(buf, g_char_names[c]);
        strcat(buf, g_str_space);
    }
    len = strlen(buf);
    if (AllocString(&t->text2, (int32_t)len + 1) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, buf, len + 1);
    t->d1c = v;
    return 1;
}

/*
 * Forward to the number-to-words formatter with its last two arguments
 * zeroed.  Eight of the interpreter's arms call the engine through this
 * thunk rather than the six-argument function behind it, so it is worth
 * having as itself: replacing it replaces all eight call sites at once.
 *
 * `digits` must be writable.  Above three digits the formatter writes a NUL
 * three characters from the end, recurses on the leading part, names the
 * group it cut off and then puts the three digits back, so a caller that
 * hands it a string literal gets a fault even though the string it gets back
 * is the one it passed in.  That restoration is asserted in unit_es over
 * every input it tries, because the callers rely on it -- Rule_SayNumberText
 * and Rule_SayNumberOrSpell hand over the token's own text.
 */
/* @0x10020920 */
int32_t TV_CDECL Number_Words(char *digits, char *out, int32_t style,
                              int32_t mode)
{
    return Number_WordsEx(digits, out, style, mode, 0, 0);
}

/*
 * Say a token's number.
 *
 * The number is turned into digits and then into words, and the words
 * become the token's replacement text.  Two of the token's flags steer it:
 * 0x4a picks the first of the formatter's two modes rather than the second,
 * and 0x31 picks style 1 rather than style 4.  A negative number is refused
 * outright.
 *
 * A token that had no trailing character gets a space and flag 0x50, which
 * is how a number that ran to the end of its text is given something to sit
 * against before the next token.
 *
 * The two buffers are the original's: 52 bytes for the digits and 256 for
 * the words, laid out exactly as its frame lays them out.
 */
/* @0x10021720 */
int32_t TV_THISCALL Rule_SayNumber(TextIn *self, Token *t, uint32_t v)
{
    char digits[52];
    char words[256];
    int32_t mode = 2, style;
    size_t len;

    if (t->num < 0)
        return 0;
    if (Bits_Test(0x4a, t->bits))
        mode = 1;
    tv_itoa(t->num, digits, 10);
    style = Bits_Test(0x31, t->bits) ? 1 : 4;
    Number_Words(digits, words, style, mode);
    len = strlen(words);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, words, len + 1);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    return 1;
}

/*
 * Say a number that is held as text rather than as a number.
 *
 * Rule_SayNumber works from Token.num, which is an int32 and so tops out at
 * ten digits.  This one works from Token.text and accepts up to seventeen,
 * which is what the length guard is for.
 *
 * The formatter's first mode is chosen when the token itself carries flag
 * 0x4a or when one of the next two real tokens does; otherwise the second.
 * That is the only place Rule_Scan is used to decide something rather than
 * to measure a distance.
 *
 * Two things it does that Rule_SayNumber does not.  It sets flag 0x31 on the
 * token rather than reading it.  And it overwrites the trailing character
 * with a space whatever it was, where Rule_SayNumber only fills one in when
 * there was none -- the 0x50 flag still marks only the tokens that had none.
 *
 * It hands the formatter the token's own text rather than a copy, so that
 * text has to be writable; the formatter puts it back before returning.
 */
/* @0x10021820 */
int32_t TV_THISCALL Rule_SayNumberText(TextIn *self, Token *t, uint32_t v)
{
    uint32_t want[3];
    char out[256];
    int32_t mode = 2;
    size_t len;

    if (t == NULL || t->text == NULL || strlen(t->text) > 0x11)
        return 0;
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        want[0] = want[1] = want[2] = 0;
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    Number_Words(t->text, out, 1, mode);
    Bits_Set(0x31, t->bits);
    len = strlen(out);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, out, len + 1);
    if (t->trail == 0)
        Bits_Set(0x50, t->bits);
    t->trail = ' ';
    t->d1c = v;
    return 1;
}

/*
 * Say a short number from a token's text, or spell it out if it is long.
 *
 * The third of the number handlers, and the one that has somewhere to go
 * when the text is too long: above seven characters it hands the token
 * straight to Rule_SpellOut with `all` clear, so a long run of digits is
 * read out digit by digit instead of being named.
 *
 * Against Rule_SayNumberText, which takes up to seventeen: this asks the
 * formatter for style 4 rather than style 1, it leaves flag 0x31 alone
 * rather than setting it, and it fills in a trailing space only when there
 * was none.  The way it picks the formatter's mode is the same -- flag 0x4a
 * on the token, or on one of the next two real tokens.
 *
 * Like the other one it hands the token's own text to the formatter, which
 * needs it writable.
 */
/* @0x10021aa0 */
int32_t TV_THISCALL Rule_SayNumberOrSpell(TextIn *self, Token *t, uint32_t v)
{
    uint32_t want[3];
    char out[256];
    int32_t mode = 2;
    size_t len;

    if (t == NULL || t->text == NULL)
        return 0;
    if (strlen(t->text) > 7)
        return Rule_SpellOut(self, t, v, 0);
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        want[0] = want[1] = want[2] = 0;
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    Number_Words(t->text, out, 4, mode);
    len = strlen(out);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, out, len + 1);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    return 1;
}

/*
 * Does this short token read as a word, or as initials?
 *
 * Answers 1 when it should be spelled out and 0 when it should be said as a
 * word, which is the way round the caller wants it.  Only three- and
 * four-letter tokens are really judged: anything shorter is spelled, anything
 * longer is said.
 *
 * The test builds a consonant/vowel pattern and then applies Spanish
 * phonotactics to it.  A token with no vowel after its first consonant is
 * initials -- CBS, IBM.  A doubled letter is a word.  An H at either end is
 * initials, H being silent.  Beyond that only three-letter tokens are
 * decided: two consonants in front are a word if the second is R or L
 * (BRA), initials if the first is S and the second is P, T or C (SPC), and a
 * word otherwise; and a vowel in each of the last two places is a word.
 * Everything else is initials.
 *
 * SOL and USA come out as words, IBM and CBS as initials, which is the
 * behaviour to keep in mind when reading it.
 */
/* @0x1006ae40 */
extern const char g_str_vowels[];      /* "AEIOUYaeiouy" */
/* @0x1006ae3c */
extern const char g_str_ptc[];         /* "PTC" */

/* @0x10020f00 */
int32_t TV_CDECL Word_IsAcronym(const char *s)
{
    char pat[5];
    char prev = 0;
    int32_t len, i, state = 0, repeats = 0;

    memset(pat, 0, sizeof pat);
    len = (int32_t)strlen(s);
    if (len < 3)
        return 1;
    if (len > 4)
        return 0;
    for (i = 0; s[i] != 0; i++) {
        char c = s[i];
        if (tv_strchr(g_str_vowels, c) != NULL) {
            pat[i] = 'V';
            if (state == 1)
                state = 2;
        } else {
            if (state == 0)
                state = 1;
            pat[i] = 'C';
        }
        if (prev == c)
            repeats++;
        prev = c;
    }
    if (repeats != 0)
        return 1;
    if (state != 2)
        return 1;
    if (s[0] == 'H' || s[i - 1] == 'H')
        return 1;
    if (len != 3)
        return 0;
    if (pat[0] == 'C' && pat[1] == 'C') {
        if (s[1] == 'R' || s[1] == 'L')
            return 0;
        if (s[0] == 'S' && tv_strchr(g_str_ptc, s[1]) != NULL)
            return 0;
        return 1;
    }
    return (pat[1] == 'V' && pat[2] == 'V') ? 1 : 0;
}

/*
 * Say a token as a word, or spell its letters out.
 *
 * A token of one character is left alone.  Flag 7 forces the spelling.
 * Otherwise Word_IsAcronym decides, with one softening: a token that looks
 * like initials is still said as a word if a neighbour carrying flag 0x19
 * reads as a word itself, which is how initials embedded in real text avoid
 * being spelled one letter at a time.  The token before is consulted first
 * and the token after second, and either one is enough.
 *
 * Said as a word, the text is lowercased into the replacement text.  Spelled
 * out, a trailing full stop is turned into a space first, so the letters do
 * not end on a sentence break that was really an abbreviation mark.
 *
 * The lowercasing goes through a 104-byte stack buffer with no length check,
 * which is the original's and is a real defect: a token of 104 characters or
 * more overruns it into the return address.  The tokenizer splits on spaces,
 * so it takes an unbroken run that long to reach -- a URL or a row of
 * symbols would do it.  Reproduced as-is; beyond that length neither the
 * original nor this has defined behaviour.
 */
/* @0x10020d70 */
int32_t TV_THISCALL Rule_Acronym(TextIn *self, Token *t, uint32_t v)
{
    char buf[104];
    int spell = 0;
    size_t len;

    if (t->text == NULL)
        return 0;
    if (t->len == 1)
        return 1;
    if (Bits_Test(7, t->bits)) {
        spell = 1;
    } else if (Word_IsAcronym(t->text) == 1) {
        spell = 1;
        /* Only a token that carries 0x19 itself gets the softening; without
         * it the spelling stands whatever the neighbours look like. */
        if (Bits_Test(0x19, t->bits)) {
            if (t->prev != NULL && Bits_Test(0x19, t->prev->bits) &&
                Word_IsAcronym(t->prev->text) == 0)
                spell = 0;
            else if (t->next != NULL && Bits_Test(0x19, t->next->bits) &&
                     Word_IsAcronym(t->next->text) == 0)
                spell = 0;
        }
    }
    if (spell) {
        if (t->trail == '.')
            t->trail = ' ';
        Rule_SpellOut(self, t, v, 0);
        return 1;
    }
    len = strlen(t->text);
    if (AllocString(&t->text2, (int32_t)len + 1) == -1)
        return TextIn_Error(self, 0);
    memcpy(buf, t->text, len + 1);
    tv_strlwr(buf);
    memcpy(t->text2, buf, strlen(buf) + 1);
    t->d1c = v;
    return 1;
}

/*
 * Say the text of the record the tokenizer attached to a token, and make it
 * plural if the rule asks.
 *
 * The record hangs off Token.d18 and carries a key of its own; the handler
 * does nothing unless that key matches the one the rule passes, which is how
 * one opcode serves several kinds of attachment.  Where the record came from
 * is not established -- what is known is the three fields this touches, so
 * that is all RuleRec names.
 *
 * Pluralising is Spanish's rule and the engine's own conditions.  It happens
 * only when the token carries flag 0x37, has something in front of it, and
 * the number on the token in front is more than one -- and for key 13 the
 * number on the token behind is added in first, which is how "2 metros 50"
 * counts as more than one.  A word ending in a vowel takes "s", anything
 * else takes "es".  The vowel set used here is "aoieuAOIEU", which is not
 * the "AEIOUYaeiouy" that Word_IsAcronym uses: no Y.
 *
 * Three smaller things it settles about spacing.  A trailing full stop
 * becomes a space unless flag 0x49 says to keep it, a token in front with no
 * trailing character gets a space, and a token with none of its own gets one
 * and flag 0x50.
 */
/* @0x1006ae58 */
extern const char g_str_vowels_plain[];   /* "aoieuAOIEU" */
/* @0x10069ba0 */
extern const char g_str_s[];
/* @0x10069b9c */
extern const char g_str_es[];

/* Only the fields Rule_SayRecord reads; the rest is unexamined. */
typedef struct {
    uint8_t  unknown00[0x20];
    uint32_t key;                /* 0x20, matched against the rule's */
    uint8_t  unknown24[6];
    int16_t  text_off;           /* 0x2a, from the start of text[] */
    uint8_t  unknown2c[1];
    char     text[1];            /* 0x2d */
} RuleRec;

/* @0x100212b0 */
int32_t TV_THISCALL Rule_SayRecord(TextIn *self, Token *t, uint32_t key,
                                   int32_t plural)
{
    const RuleRec *rec;
    const char *src;
    size_t len;
    int32_t count;

    if (t == NULL)
        return TextIn_Error(self, 0);
    rec = (const RuleRec *)t->d18;
    if (rec == NULL)
        return 0;
    if (rec->key != key)
        return 0;
    src = rec->text + rec->text_off;
    len = strlen(src);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, src, len + 1);
    t->d1c = key;
    if (t->trail == '.' && !Bits_Test(0x49, t->bits))
        t->trail = ' ';
    if (t->prev != NULL && t->prev->trail == 0)
        t->prev->trail = ' ';
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    if (plural == 0)
        return 1;

    len = strlen(t->text2);
    if ((int32_t)len < 1)
        return 0;
    if (!Bits_Test(0x37, t->bits) || t->prev == NULL)
        return 1;
    count = t->prev->num;
    if (key == 0xd && t->next != NULL)
        count += t->next->num;
    if (count <= 1)
        return 1;
    strcat(t->text2,
           tv_strchr(g_str_vowels_plain, t->text2[len - 1]) != NULL
               ? g_str_s : g_str_es);
    return 1;
}

/*
 * Read a number written in groups: thousands separated by spaces or full
 * stops, and a decimal part after a comma.
 *
 * It is the only handler that takes a Token ** rather than a Token *,
 * because it consumes several tokens and has to tell the interpreter which
 * one is left.  The digits of every group are pasted into one string, the
 * tokens that supplied them are removed, and the last token of the run
 * survives holding the whole thing spoken.
 *
 * A group joins if it carries flag 0x14 and not 0x0f, is exactly three
 * characters long, and follows the same separator as the first -- so
 * "1.234.567" joins and "1.234 567" does not.  At most fifteen groups.
 * After a comma, one more token is taken as the decimal part, and the words
 * become "<integer> coma <decimal>"; the comma is only looked for when the
 * rule's fourth argument is zero.
 *
 * Two things carry over from the other number handlers.  The formatter's
 * mode comes from flag 0x4a on the token or on one of the next two.  And the
 * text handed to it has to be writable -- here a local buffer for the joined
 * digits, and the decimal token's own text for the fraction.
 *
 * The value the formatter parsed is stored back into Token.num, which is the
 * only place that return value is used.
 */
/* @0x10021be0 */
int32_t TV_THISCALL Rule_SayGroupedNumber(TextIn *self, Token **first,
                                          uint32_t v, int32_t force_space)
{
    char digits[256];
    char words[256];
    char frac[256];
    uint32_t want[3];
    Token *t = *first;
    int32_t mode = 2, comma = 0, merged = 1;
    uint8_t trail;
    size_t len;

    want[0] = want[1] = want[2] = 0;
    if (t == NULL || t->text == NULL)
        return 0;
    digits[0] = 0;
    words[0] = 0;
    trail = t->trail;
    strcat(digits, t->text);
    if (trail == ' ' || trail == '.') {
        while (t->next != NULL) {
            if (t->trail != trail)
                break;
            if (!Bits_Test(0x14, t->next->bits))
                break;
            if (Bits_Test(0xf, t->next->bits))
                break;
            if (t->next->len != 3)
                break;
            if (merged >= 0xf)
                break;
            strcat(digits, t->next->text);
            t = t->next;
            merged++;
        }
        trail = t->trail;
    }
    if (force_space == 0 && trail == ',' && t->next != NULL &&
        Bits_Test(0x14, t->next->bits) && !Bits_Test(0xf, t->next->bits)) {
        t = t->next;
        comma = t->num;
    }
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    if (strlen(digits) > 0x11)
        return 0;
    t->num = Number_Words(digits, words, 4, mode);
    if (comma != 0) {
        strcat(words, g_str_space);
        strcat(words, g_rule_words[1]);        /* "coma" */
        strcat(words, g_str_space);
        Number_Words(t->text, frac, 4, mode);
        strcat(words, frac);
    }
    len = strlen(words);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, words, len + 1);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    if (force_space != 0)
        t->trail = ' ';
    if (t != *first) {
        Token *p = t->prev;
        while (*first != p)
            p = TextIn_RemoveToken(self, p, -1);
        *first = TextIn_RemoveToken(self, p, 1);
    }
    return 1;
}
