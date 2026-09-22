/*
 * Stage 1: word pronunciation.
 *
 * Stage 0 hands stage 1 runs of letter nodes.  Stage 1 gathers each run into
 * a span, looks the word up in the lexicon (or falls back to the
 * letter-to-sound rules) and replaces the letters with phoneme nodes.
 *
 * The stage keeps two spans: the one it is working on (s1_word_start ..
 * s1_word_end) and the one it has just produced (s1_next_start ..
 * s1_next_end); Stage1_TakeSpan promotes the latter to the former once the
 * driver has handed the produced nodes to stage 2.
 */
#include "engine.h"

/* @0x100c8aa0, indexed as the original does (-128..255); see stage0.c */
uint8_t Phone_Attr(int32_t idx);

/* The table has two banks: the low half describes a character as it arrives
 * from stage 0, the high half the same character as a phoneme symbol. */
static uint8_t phone_attr_lo(uint8_t c)
{
    return Phone_Attr((int8_t)c);
}

static uint8_t phone_attr_hi(uint8_t c)
{
    return Phone_Attr((int16_t)((int16_t)(int8_t)c | 0x80));
}

/* The third bank, used by the prosody pass below. */
static uint8_t phone_attr_x(uint8_t c)
{
    return Phone_Attr((int16_t)((int16_t)(int8_t)c | 0x100));
}

/* Promote the span just produced and rewind the look-ahead cursor to its
 * end. */
/* @0x10062da0 */
void TV_THISCALL Stage1_TakeSpan(Engine *self)
{
    Node *n;

    self->s1_steps = 0;
    n = self->s1_next_start;
    self->s1_next_start = NULL;
    self->s1_word_start = n;
    n = self->s1_next_end;
    self->s1_cursor = n;
    self->s1_word_end = n;
    self->s1_next_end = NULL;
}

/* Walk the cursor forward to the next node stage 1 owns.  Gives up after 12
 * nodes, or at a 'C' (clear) command, so that a long run of nodes this stage
 * does not handle still gets passed on.  Returns 0 at the end of the window. */
/* @0x10062de0 */
uint8_t TV_THISCALL Stage1_ScanAhead(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *n;

    for (;;) {
        if (st->last == self->s1_cursor) {
            st->scan = self->s1_cursor;
            return 0;
        }
        /* not the last node of the window, so this never returns NULL */
        n = Engine_StageNext(self, self->s1_cursor);
        self->s1_cursor = n;
        if (n != NULL && (st->type_mask & g_node_type_bits[NODE_TYPE(n)])) {
            st->scan = self->s1_cursor;
            return 1;
        }
        self->s1_steps++;
        if (self->s1_steps >= 12 || (NODE_TYPE(n) == 0 && n->value == 'C')) {
            st->scan = self->s1_cursor;
            return 1;
        }
    }
}

/* @0x10062830 */
uint8_t TV_THISCALL Stage1_Run(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *n;

    Engine_StageBegin(self, st);
    if (st->cur == st->scan) {
        if (!Stage1_Gather(self))
            return Engine_StageEnd(self);
        Stage1_TakeSpan(self);
    }
    if (Stage1_ScanAhead(self)) {
        Stage1_Gather(self);
        n = Stage1_Pronounce(self);
        st->ctl = n;
        st->cur = n;
        st->scan = Engine_StageNext(self, self->s1_next_end);
        Stage1_TakeSpan(self);
        return Engine_StageEnd(self);
    }

    /* Nothing to pronounce: step over the phonemes already produced. */
    while (st->cur != NULL) {
        if (!(phone_attr_hi(st->cur->value) & 0x40))
            break;
        self->s1_1c38 = 0;
        st->cur = Engine_StageNext(self, st->cur);
    }
    return Engine_StageEnd(self);
}

/* Gather the run of nodes starting at the read cursor into the "next" span.
 * For a word (type 2) this also collects its letters into a '#'-framed buffer
 * and looks it up in the lexicon; for phonemes (type 3) it just extends the
 * span over the following phoneme symbols. */
/* @0x10062e80 */
uint8_t TV_THISCALL Stage1_Gather(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *first, *n, *p, *end;
    char buf[100];
    int32_t i;

    first = st->scan;
    if (first == NULL)
        return 0;

    self->s1_next_start = first;
    self->s1_next_end = first;

    /* Carry the "start of clause" marker forward past the phonemes that may
     * not begin one; if the window ends first it goes back to the span. */
    n = Engine_StageNext(self, first);
    if (n != NULL && n->b14 == 1) {
        n->b14 = 0;
        for (;;) {
            n = Engine_StageNext(self, n);
            if (n == NULL) {
                self->s1_next_start->b14 = 1;
                break;
            }
            if (phone_attr_hi(n->value) & 2)
                continue;
            if (n->b14 == 1)
                continue;
            break;
        }
    }

    first = self->s1_next_start;
    if (self->s0_1c1c != 0) {
        self->s0_1c1c = 0;
        first->b14 = 2;
    } else {
        Node *nx = first->next;
        uint8_t nv = nx->value;
        uint8_t c;
        int32_t mark = 0;

        /* "S" + a capitalised word, or "S", "A", "Y" ... */
        if (nv >= 'A' && nv <= 'Z' && (phone_attr_lo(first->value) & 8) &&
            first->prev->value == 'S') {
            c = nx->next->value;
            if (c == ' ' || (phone_attr_hi(c) & 1))
                mark = 1;
        }
        if (!mark && (phone_attr_lo(first->value) & 8) &&
            first->prev->value == 'S' && nv == 'A' && nx->next->value == 'Y') {
            c = nx->next->next->value;
            if (c == ' ' || (phone_attr_hi(c) & 1))
                mark = 1;
        }
        if (mark) {
            self->s0_1c1c = 0;
            first->b14 = 2;
        }
    }

    first = self->s1_next_start;
    if (NODE_TYPE(first) == 3) {
        /* Already phonemes: extend the span over the symbols that go with
         * this one. */
        n = first;
        for (;;) {
            n = Engine_StageNext(self, n);
            if (n == NULL || NODE_TYPE(n) != 3)
                break;
            if (!(phone_attr_lo(n->value) & 0x80) && !(phone_attr_hi(n->value) & 2))
                break;
            self->s1_next_end = n;
        }
        Stage1_Emit(self);
        return 1;
    }
    if (NODE_TYPE(first) != 2)
        return 1;

    /* A word.  Strip the stress prefixes stage 0 may have put in front. */
    n = Engine_StageNext(self, first);
    if (n->value == '~') {
        self->s1_1c2a = 1;
        n = Engine_NodeFree(self, n, 1);
    } else {
        self->s1_1c2a = 0;
    }
    if (n->value == '`') {
        self->s1_1c2b = 1;
        n = Engine_NodeFree(self, n, 1);
    } else {
        self->s1_1c2b = 0;
    }
    switch (first->arg) {
    case 1:
    case 2:
    case 5:
        self->s1_1c2b = 1;
        break;
    case 9:
        self->s1_1c2a = 1;
        break;
    default:
        break;
    }
    first->flags = (first->flags & ~4u) | 3; /* type 2 -> 3 */
    self->s1_letters = n;

    /* The word runs to the next node that is not a letter, or to '&'. */
    for (;;) {
        self->s1_next_end = n;
        n = Engine_StageNext(self, n);
        if (n == NULL || NODE_TYPE(n) != 2 || n->value == '&')
            break;
    }

    self->s1_1c29 = 0;
    self->s1_1c2c = 0;
    self->s1_1c2d = 0;
    p = self->s1_letters;
    end = self->s1_next_end;
    i = 1;
    buf[0] = '#';
    for (;;) {
        if (end->next == p)
            break;
        if (i >= 0x62)
            break;
        buf[i] = (char)p->value;
        i++;
        p = p->next;
    }
    if (i < 0x62) {
        buf[i] = '#';
        buf[i + 1] = '\0';
        Word_Classify(buf, self->s1_next_start);
    }

    if (!Stage1_Lookup(self) || self->s1_1c29 != 0)
        Stage1_Rules(self);
    self->s1_next_start->b15 = self->s1_1c2c;
    Stage1_Emit(self);
    return 1;
}

/* Turn the span the gather pass marked out into its final phoneme nodes:
 * assign stress, number the syllables, and drop the markers.  Returns the
 * node just past the span. */
/* @0x10062900 */
Node *TV_THISCALL Stage1_Pronounce(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *start = self->s1_word_start;
    Node *n, *p, *nx, *stop, *accent = NULL;
    int32_t sep_count = 0, syl_count = 0, first_ch;
    uint8_t stress_n = 0, after_sep = 0;
    uint8_t v, a;

    if (NODE_TYPE(start) != 3 || (phone_attr_hi(start->value) & 0x40))
        return Engine_StageNext(self, start);

    /* Skim mode ('f'): keep only every p_30'th word and free the rest. */
    if (st->p_30 != 0) {
        uint8_t k = start->b15;
        int32_t keep = 0;

        if (k == 0 || k == 6 || k == 0xf || k == 8) {
            self->s1_1c20 = self->s1_1c20 + 1;
            self->s1_1c20 = self->s1_1c20 % st->p_30;
            keep = (self->s1_1c20 == 0);
        }
        if (!keep) {
            while (self->s1_word_end != self->s1_word_start)
                self->s1_word_start = Engine_NodeFree(self, self->s1_word_start, 1);
            return Engine_NodeFree(self, self->s1_word_start, 1);
        }
    }

    if (start->value == '%' && start->arg != 13)
        Stage1_Phrase(self);

    start = self->s1_word_start;
    first_ch = (int8_t)start->value;
    if (first_ch == '%' || start->b15 == 0xf)
        Stage1_SpreadStress(self);

    stop = Engine_StageNext(self, self->s1_word_end);

    for (n = self->s1_word_start; n != stop; n = Engine_StageNext(self, n)) {
        if (n == NULL)
            break;

        /* Every vowel gets its stress and duration worked out. */
        if (NODE_TYPE(n) == 3 && (phone_attr_lo(n->value) & 0x80) &&
            (((st->p_34 >> 8) & 1) || st->p_1c == 0) &&
            self->s1_word_start->arg != 13)
            n = Stage1_Vowel(self, n);

        if (self->s1_1c50 != 0 && (phone_attr_x(n->value) & 2) != 0) {
            v = n->value;
            if ((phone_attr_x(v) & 0x20) == 0 || v == 'a') {
                /* L062c4f */
                if (v != '@') {
                    self->s1_1c50 = 0;
                    accent = NULL;
                }
            } else if (accent == NULL) {
                /* Look back over the previous word for a better place to put
                 * the accent than the one just found. */
                if (self->s1_next_start != NULL &&
                    (phone_attr_hi(self->s1_next_start->value) & 0x40))
                    after_sep = 1;
                for (p = n->prev; p != NULL; ) {
                    a = phone_attr_hi(p->value);
                    if (a & 0x40)
                        break;
                    if (p->value == '&' || p->value == '%') {
                        sep_count++;
                        if (sep_count > 1)
                            after_sep = 0;
                    } else if (phone_attr_x(p->value) & 2) {
                        if (p->value != '@') {
                            self->s1_1c50 = 0;
                        } else if (after_sep != 0 && sep_count == 1) {
                            after_sep = 0;
                        } else {
                            Node *q = p->next->next;
                            int32_t demote = 1;

                            if (q->value == ' ' && q->next->value == '|')
                                demote = 0;
                            else if (syl_count >= 2)
                                demote = 0;
                            else if (p->next->value == '&' || p->next->value == '%')
                                demote = 0;
                            else if (!(phone_attr_x(q->value) & 2))
                                demote = 0;
                            else if (p->prev->value == '&' || p->prev->value == '%')
                                demote = 0;
                            if (demote)
                                p->value = '|';
                        }
                        syl_count = 0;
                    } else if (a & 0x20) {
                        syl_count++;
                    }
                    p = p->prev;
                    if (self->s1_1c50 == 0)
                        break;
                }
                self->s1_1c50 = 0;
            } else {
                /* L062bb5: an accent is already pending; keep it only if two
                 * syllables follow. */
                if (accent->value != '@' ||
                    accent->prev->value == '&' || accent->prev->value == '%') {
                    self->s1_1c50 = 0;
                } else {
                    nx = accent->next;
                    v = nx->value;
                    if (v == '[')
                        nx = nx->next;
                    if (!(phone_attr_hi(nx->value) & 0x20) ||
                        !(phone_attr_hi(nx->next->value) & 0x20))
                        accent->value = '|';
                    self->s1_1c50 = 0;
                }
            }
        }

        /* An '@' two phonemes into the word starts a pending accent. */
        if (self->s1_1c50 == 0 && n->value == '@') {
            p = n->prev;
            if (p->prev->prev != NULL &&
                !(phone_attr_hi(p->prev->value) & 0x40) &&
                !(phone_attr_hi(p->value) & 0x40)) {
                self->s1_1c50 = 1;
                accent = n;
            }
        }

        /* "|" before a syllabic l or j is a full accent after all. */
        if (n->value == '|' && (phone_attr_hi(n->next->value) & 0x20)) {
            v = n->next->next->value;
            if (v == 'l' || v == 'j')
                n->value = '@';
        }

        if (first_ch == '%' && (n->flags & 0x18))
            Stage1_Mark(self, n);
    }

    /* Number the syllables and drop the "[" markers. */
    for (n = self->s1_word_start; n != stop; n = nx) {
        if (n == NULL)
            break;
        if (phone_attr_x(n->value) & 2) {
            stress_n++;
            n->syllable = stress_n;
        }
        nx = Engine_StageNext(self, n);
        if (n->value == '[')
            Engine_NodeFree(self, n, 0);
    }

    start = self->s1_word_start;
    if (start->value == '%' && start->arg != 13)
        Stage1_PhraseEnd(self);

    self->s1_1c38 = (int8_t)stop->prev->value;
    return stop;
}

/* Turn the stress digits stage 0 left after each vowel into the two stress
 * bits of the vowel node, then drop them. */
/* @0x10063170 */
void TV_THISCALL Stage1_Emit(Engine *self)
{
    Node *cur, *nx;
    int32_t v, level;

    nx = self->s1_next_start;
    do {
        cur = nx;
        nx = Engine_StageNext(self, cur);
        if (self->s1_next_end != cur) {
            v = (int8_t)nx->value;
            if (v == '1' || v == '2' || v == '"') {
                if (phone_attr_lo(cur->value) & 1) {
                    level = (v == '1') ? 2 : (v == '2') ? 1 : 3;
                    cur->flags = (cur->flags & ~0x18u) |
                                 ((uint32_t)(level << 3) & 0x18u);
                }
                if (self->s1_next_end == nx)
                    self->s1_next_end = cur;
                nx = Engine_NodeFree(self, nx, 1);
            }
        }
        if (self->s1_next_start->value != '%' && (cur->flags & 0x18))
            Stage1_Mark(self, cur);
    } while (self->s1_next_end != cur);
    Stage1_Close(self);
}

/* A stressed syllable also marks the two syllables before it (and an "S"
 * before those), so the rest of stage 1 can see where the accent falls. */
/* @0x100637f0 */
void TV_THISCALL Stage1_Mark(Engine *self, Node *n)
{
    uint8_t v;

    n->flags |= 0x20;
    n = Node_PrevBoundary(self, n);
    if (n == NULL)
        return;
    v = n->value;
    if (!(phone_attr_hi(v) & 0x20) && v != 'Y')
        return;
    if (v == '~')
        return;

    n->flags |= 0x20;
    n = Node_PrevBoundary(self, n);
    if (n == NULL)
        return;
    v = n->value;
    if (!(phone_attr_hi(v) & 0x20))
        return;
    if (phone_attr_lo(v) & 2)
        return;
    if ((phone_attr_x(v) & 1) && (phone_attr_x(n->next->value) & 1))
        return;

    n->flags |= 0x20;
    n = Node_PrevBoundary(self, n);
    if (n == NULL)
        return;
    if (n->value != 'S')
        return;
    n->flags |= 0x20;
}

/* Copy the word's stress value onto each of its syllables. */
/* @0x10063e40 */
void TV_THISCALL Stage1_SpreadStress(Engine *self)
{
    Node *n = self->s1_word_start;

    if (Engine_StageNext(self, self->s1_word_end) == n)
        return;
    do {
        if (phone_attr_x(n->value) & 2)
            n->b19 = self->s1_word_start->b15;
        n = Engine_StageNext(self, n);
    } while (Engine_StageNext(self, self->s1_word_end) != n);
}

/* The vowel symbols of the phoneme alphabet ("@|ObfUAEIyaeivowu3rg5k4c"). */
/* @0x10048010 */
uint8_t TV_STDCALL Phone_IsVowel(uint8_t c)
{
    switch (c) {
    case 'A': case 'I': case 'y': case 'f': case 'e': case 'b':
    case 'E': case 'a': case 'O': case 'i': case 'v': case '@':
    case '3': case '|': case 'o': case 'w': case 'k': case 'r':
    case 'g': case '4': case 'c': case 'u': case 'U': case '5':
        return 1;
    default:
        return 0;
    }
}

/* Walk back to the node that starts the current syllable or word: type 3
 * (word boundary) or type 4 (syllable boundary). */
/* @0x1002b430 */
Node *TV_THISCALL Node_PrevBoundary(Engine *self, Node *n)
{
    uint32_t type;

    if (n == NULL)
        return NULL;
    do {
        n = Engine_StagePrev(self, n);
        if (n == NULL)
            return NULL;
        type = NODE_TYPE(n);
        if (type == 3)
            return n;
    } while (type != 4);
    return n;
}

/* Walk forward to the next word boundary (type 3). */
/* @0x1002b460 */
Node *TV_THISCALL Node_NextWord(Engine *self, Node *n)
{
    if (n == NULL)
        return NULL;
    do {
        n = Engine_StageNext(self, n);
        if (n == NULL)
            return NULL;
    } while (NODE_TYPE(n) != 3);
    return n;
}

/* Finish the span: the punctuation class stage 0 stored in arg decides what
 * the marker node becomes. */
/* @0x10064100 */
void TV_THISCALL Stage1_Close(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *n = self->s1_next_start;

    if (!(n->flags & 0x20))
        return;
    switch (n->arg) {
    case 1:
        if (st->cur != st->scan)
            n->value = '$';
        break;
    case 2:
        n->value = '%';
        self->s1_next_start->b15 = 1;
        break;
    case 3:
        n->value = '&';
        self->s1_next_start->b15 = 0;
        break;
    case 5:
        n->value = ' ';
        self->s1_next_start->flags = (self->s1_next_start->flags & ~4u) | 3;
        self->s1_next_start->arg = 4;
        self->s1_next_start->b15 = 0x3b;
        break;
    case 9:
        self->s1_1c2a = 1;
        break;
    case 11:
        n->next = Engine_NodeAlloc(self, n, 1, 3, ' ');
        self->s1_next_start->next->arg = 8;
        self->s1_next_start->b15 = 0x3b;
        break;
    default:
        break;
    }
}

/* Does the letter under the rule cursor have the features this rule wants?
 * Bit 0x8000 of the first mask means "none of these", of the second "any of
 * these"; otherwise all the bits must be present. */
/* @0x10060140 */
uint8_t TV_THISCALL Lts_TestFeatures(Engine *self, const LtsEntry *r)
{
    uint32_t lo = r->want[0];
    uint32_t hi = r->want[1];

    if (!(lo & 0x8000) && !(hi & 0x8000))
        return (uint8_t)((self->lts_feat_lo & lo) == lo &&
                         (self->lts_feat_hi & hi) == hi);
    if (lo & 0x8000)
        return (uint8_t)(!(self->lts_feat_lo & lo) && !(self->lts_feat_hi & hi));
    return (uint8_t)((self->lts_feat_lo & lo) != 0 || (self->lts_feat_hi & hi) != 0);
}

/* Match the rule's left context backwards against the letters before the
 * cursor, moving lts_back as it goes. */
/* @0x100601c0 */
uint8_t TV_THISCALL Lts_MatchLeft(Engine *self, const LtsEntry *r)
{
    const char *s = r->left;
    Node *n;

    if (*s == '\0')
        return 1;
    do {
        n = Engine_StagePrev(self, self->lts_back);
        self->lts_back = n;
        if (n == NULL)
            return 0;
        if (n->value != (uint8_t)*s)
            return 0;
        if (NODE_TYPE(n) != 2)
            return 0;
        s++;
    } while (*s != '\0');
    return 1;
}

/* The engine sign-extends the character it indexes these tables with, so a
 * byte >= 0x80 reads the 128 entries before the table. */
static const LtsRule *const *lts_list(const LtsRule *const *const *table, int32_t c)
{
    return table[c];
}

/* Does the context around n satisfy the rule's condition program?  The
 * program is a sequence of NUL-terminated conditions, each starting with a
 * flags byte: bits 5-7 are the kind, and for a letter list bit 4 marks it as
 * covering several letters (bits 0-1 give how many, plus two), bits 2-3 the
 * match mode and bit 3 also negates the whole condition. */
/* @0x10060210 */
uint8_t TV_THISCALL Lts_TestContext(Engine *self, const uint8_t *cond, Node *n,
                                    int32_t dir)
{
    const uint8_t *p = cond;
    const uint8_t *start;
    uint8_t result, negate, on_match, all_of, flags;
    uint32_t kind, mode;
    int32_t wide, count, iter, v, c, syl;
    Node *cur, *q;

    if (*p == '\0')
        return 1;

    for (;;) {
        flags = *p;
        result = 0;
        negate = 0;
        kind = flags & 0xe0u;
        cur = n;

        if (kind == 0x40 || kind == 0x60) {
            /* a "strong" letter somewhere back before the syllable break */
            for (;;) {
                cur = Engine_StagePrev(self, cur);
                if (cur == NULL)
                    break;
                c = (int8_t)cur->value;
                if (Phone_Attr(c | 0x100) & 2)
                    break;
                if (Phone_Attr(c) & 8)
                    result = 1;
                if (c != 'Y')
                    continue;
                if (phone_attr_hi(cur->prev->value) & 0x20)
                    break;
            }
            if (kind == 0x60) {
                negate = 1;
                result = (uint8_t)(result == 0);
            }
        } else if (kind == 0xa0 || kind == 0xc0) {
            /* exactly one syllable back there */
            syl = 0;
            for (;;) {
                cur = Engine_StagePrev(self, cur);
                if (cur == NULL)
                    break;
                c = (int8_t)cur->value;
                if (Phone_Attr(c | 0x100) & 2)
                    syl++;
                if (Phone_Attr(c) & 8)
                    break;
                if (c != 'Y')
                    continue;
                if (phone_attr_hi(cur->prev->value) & 0x20)
                    syl++;
            }
            if (syl == 1)
                result = 1;
            if (kind == 0xc0) {
                negate = 1;
                result = (uint8_t)(result == 0);
            }
        } else if (kind == 0x20) {
            /* the word must have more than one syllable */
            if (self->lts_syllables <= 1)
                return 0;
            result = 1;
        } else if (kind == 0x80) {
            /* the next letter along is doubled */
            cur = dir == 1 ? Engine_StageNext(self, cur)
                           : Engine_StagePrev(self, cur);
            if (cur != NULL) {
                v = (int8_t)cur->value;
                if (Phone_Attr(v | 0x80) & 0x20) {
                    q = dir == 1 ? cur->next : cur->prev;
                    if ((int8_t)q->value == v)
                        result = 1;
                }
            }
        } else {
            /* A list of letters and letter classes.  A wide condition walks
             * on through the list letter by letter: only the first round
             * rewinds it and resets the running answer. */
            wide = flags & 0x10;
            mode = flags & 0xcu;
            start = p;
            negate = (uint8_t)(flags & 8);
            all_of = (uint8_t)((flags & 0x1c) == 0x1c);
            count = wide ? (int32_t)(flags & 3) + 2 : 1;
            on_match = 0;

            for (iter = 1; iter <= count; iter++) {
                cur = dir == 0 ? Engine_StagePrev(self, cur)
                               : Engine_StageNext(self, cur);
                if (cur == NULL) {
                    result = 0;
                    break;
                }
                v = (int8_t)cur->value;
                if (iter == 1 || wide == 0) {
                    p = start + 1;
                    if (mode & 4) {
                        result = 0;
                        on_match = 1;
                    } else {
                        result = 1;
                        on_match = 0;
                    }
                }
                /* modes 4 and 8 stop at the first item that matches, 0 and
                 * 0xc at the first that does not */
                for (;;) {
                    uint32_t cls;
                    int32_t hit;

                    c = (int8_t)*p;
                    p++;
                    if (c == 0)
                        break;
                    if (c & 0x80) {
                        cls = g_lts_class[c & 0x7f];
                        hit = (Phone_Attr((int32_t)((cls & 0xff00) >> 1) | v) &
                               cls & 0xff) != 0;
                    } else {
                        hit = (c == v);
                    }
                    if (mode == 0 || mode == 0xc)
                        hit = !hit;
                    if (hit) {
                        result = on_match;
                        break;
                    }
                    if (wide != 0)
                        break;
                }
                if (result == 0 && wide == 0)
                    break;
                if (all_of != 0 && result != 0)
                    break;
            }
        }

        /* One condition settles it unless it is the negated kind. */
        if ((result != 0) != (negate != 0))
            return result;
        for (;;) {
            const uint8_t *e = p;
            p++;
            if (*e == '\0')
                break;
        }
        if (*p == '\0')
            return result;
    }
}

/* Try each rule in `set` against the letters at the cursor.  On a match a
 * "[" boundary node is inserted and the matched rule returned. */
/* @0x10063480 */
const LtsRule *TV_THISCALL Lts_MatchAffix(Engine *self, Node *a, Node *b,
                                          const LtsRule *const *set, int32_t dir)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *cur, *stop, *end;
    const char *s;
    uint8_t lvl;

    if (dir == 0) {
        cur = b;
        stop = a;
    } else {
        cur = a;
        stop = b;
    }

    if (*set == NULL)
        return NULL;
    do {
        s = (*set)->text;
        if (s == NULL)
            return NULL;
        end = cur;
        if (end->value == (uint8_t)*s) {
            while (stop != end) {
                s++;
                if (*s == '\0')
                    break;
                end = dir == 0 ? end->prev : end->next;
                if (end->value != (uint8_t)*s)
                    break;
            }
        }
        if (*s == '\0' &&
            Lts_TestContext(self, (*set)->cond, end, dir)) {
            if (dir == 0) {
                st->d18 = end->prev;
                Engine_NodeAlloc(self, end, 0, 2, '[');
            } else {
                st->d14 = end->next;
                Engine_NodeAlloc(self, end, 1, 2, '[');
            }
            lvl = (*set)->b0a;
            if ((int8_t)lvl > 1) {
                self->s1_1c2d = lvl;
            } else {
                Node *ns = self->s1_next_start;
                ns->flags = (ns->flags & ~0x18u) |
                            ((uint32_t)((int32_t)(int8_t)lvl << 3) & 0x18u);
            }
            return *set;
        }
        set++;
    } while (*set != NULL);
    return NULL;
}

/* Try to pronounce the word: first straight from the lexicon, then by
 * stripping suffixes and prefixes with the affix rules and looking the stem
 * up again.  Returns 1 only when the lexicon had the whole word. */
/* @0x10063230 */
uint8_t TV_THISCALL Stage1_Lookup(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    const LtsRule *r;
    const LtsRule *const *set;
    Node *n;
    int32_t i, cons;
    uint8_t seen = 0;

    st->d18 = self->s1_next_end;
    st->d14 = self->s1_letters;
    if (UserLex_Try(self) == 1)
        return 1;
    if (Lexicon_Try(self) == 1)
        return 1;

    /* Leave at least a two-letter stem with a consonant in it. */
    n = self->s1_letters;
    i = 0;
    cons = 0;
    self->s1_1c28 = 0;
    self->s1_rule = NULL;
    for (;;) {
        if (i > 2 && cons != 0)
            break;
        if (self->s1_next_end == n)
            goto done;
        cons |= ((phone_attr_hi(n->value) & 0x20) == 0);
        if (cons != 0 && i > 1)
            break;
        n = n->next;
        i++;
    }
    if (self->s1_next_end == n)
        goto done;

    set = lts_list(g_lts_suffix, (int8_t)st->d18->value);
    while (set != NULL) {
        r = Lts_MatchAffix(self, n, st->d18, set, 0);
        self->s1_rule = r;
        if (r == NULL)
            break;
        if (seen == 0) {
            if (r->b09 == 6)
                self->s1_1c2c = 6;
            if (r->b09 == 0xf)
                self->s1_1c2c = 0xf;
            if (r->b09 == 8) {
                self->s1_1c2c = 8;
                self->s1_next_start->value = '%';
            }
        }
        if (r->b08 == 1) {
            if (UserLex_Try(self) == 1)
                return 0;
            if (Lexicon_Try(self) == 1)
                return 0;
            if (Lts_ApplyAffix(self, 1) == 1)
                return 0;
        }
        seen = 1;
        set = r->next;
    }

    /* Same again from the front of the word. */
    n = st->d18;
    i = 0;
    cons = 0;
    for (;;) {
        if (i >= 2 && cons != 0)
            break;
        if (self->s1_letters == n)
            goto done;
        cons |= ((phone_attr_hi(n->value) & 0x20) == 0);
        if (cons != 0 && i > 1)
            break;
        n = n->prev;
        i++;
    }
    if (self->s1_letters == n)
        goto done;
    if (self->s1_1c2d == 2)
        goto done;

    set = lts_list(g_lts_prefix, (int8_t)st->d14->value);
    while (set != NULL) {
        r = Lts_MatchAffix(self, st->d14, n, set, 1);
        if (r == NULL)
            break;
        if (r->b08 == 1) {
            if (Lexicon_Try(self) == 1)
                return 0;
            if (Lts_ApplyAffix(self, 0) == 1)
                return 0;
        }
        set = r->next;
    }

done:
    if (self->s1_1c28 == 1)
        st->d18 = Engine_NodeAlloc(self, st->d18, 1, 2, 'E');
    return 0;
}

/* After stripping a suffix, the stem may still not be spelled the way the
 * lexicon has it: undo a doubled consonant, put back a dropped "E", turn a
 * final "I" back into "Y", and so on, trying the lexicon after each repair.
 * Returns 1 as soon as one of them is found. */
/* @0x100635a0 */
int32_t TV_THISCALL Lts_ApplyAffix(Engine *self, int32_t dir)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *n = st->d18;
    Node *p, *q;
    const void *r;
    uint8_t c, allow_y;

    /* "running" -> "runn" -> "run" */
    c = n->value;
    if ((phone_attr_hi(c) & 0x20) && n->prev->value == c) {
        p = n->prev;
        st->d18 = p;
        if (Lexicon_Try(self) == 1) {
            Engine_NodeFree(self, n, 0);
            return 1;
        }
        st->d18 = n;
        goto repair_y;
    }

    if (self->s1_1c28 == 1)
        goto add_e;

    /* "hoping" -> "hope": only these suffixes can have dropped an "E". */
    r = self->s1_rule;
    if (r == &g_lts_rule_ing || r == &g_lts_rule_est || r == &g_lts_rule_ily) {
        /* fall through */
    } else if (r == &g_lts_rule_able || r == &g_lts_rule_ably ||
               r == &g_lts_rule_or) {
        if (Lts_TestContext(self, g_lts_cond_stem_ok, n->next, 0))
            goto repair_y;
    } else {
        goto repair_y;
    }

    c = n->value;
    if (!(phone_attr_hi(c) & 0x20) && c != 'U')
        goto repair_y;
    if (!Lts_TestContext(self, g_lts_cond_want_e, n->next, 0)) {
        if (Lts_TestContext(self, g_lts_cond_no_e, n->next, 0))
            goto repair_y;
    }

add_e:
    n = Engine_NodeAlloc(self, n, 1, 2, 'E');
    st->d18 = n;
    if (Lexicon_Try(self) == 1)
        return 1;
    n = Engine_NodeFree(self, n, 0);
    st->d18 = n;
    self->s1_1c28 = 1;

repair_y:
    allow_y = (self->s1_rule == &g_lts_rule_s) ? 0 : (uint8_t)dir;

    /* "tried" -> "trie" -> "try" */
    if (n->value == 'E' && n->prev->value == 'I') {
        n = Engine_NodeFree(self, n, 0);
        st->d18 = n;
        n->value = 'Y';
        if (Lexicon_Try(self) == 1)
            return 1;
    }
    if (n->value == 'I' && allow_y != 0 && self->s1_1c2d != 2) {
        st->d18->value = 'Y';
        if (Lexicon_Try(self) == 1)
            return 1;
    }

    /* "-ction" stems want their "T" back. */
    if (self->s1_next_end != n &&
        Lts_TestContext(self, g_lts_cond_want_t, n->next, 0)) {
        q = Engine_StageNext(self, st->d18);
        if (q->value == '[') {
            q = Engine_StageNext(self, q);
            if (q->value == 'C' &&
                Engine_StageNext(self, q)->value != 'R' &&
                Engine_StageNext(self, q)->value != 'A') {
                st->d18 = Engine_NodeAlloc(self, n, 1, 2, 'T');
                if (Lexicon_Try(self) == 1)
                    return 1;
            }
        }
    }
    return 0;
}
