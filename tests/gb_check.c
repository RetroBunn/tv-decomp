/*
 * British English: does the accent reach the audio, and does American survive?
 *
 * Linked against the library directly rather than through the DLL, so a
 * failure here is in the engine and not in an export table.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"

void tv_gb_set(int on);
int tv_gb_get(void);

static unsigned long g_hash;
static unsigned long g_samples;
static int g_gb_seen;

static int TVTTS_CALL on_audio(const tvtts_event *ev, void *user)
{
    uint32_t i;

    (void)user;
    if (ev->type != TVTTS_AUDIO)
        return 0;
    /* Whether the engine thinks it is speaking GB while it makes samples is
     * the thing a DLL boundary could hide, so look from inside. */
    if (tv_gb_get())
        g_gb_seen = 1;
    for (i = 0; i < ev->count; i++)
        g_hash = g_hash * 1000003u + (unsigned long)(uint16_t)ev->samples[i];
    g_samples += ev->count;
    return 0;
}

/*
 * The first voice belonging to a language.
 *
 * tvtts_set_voice numbers voices in one run across every language, so asking
 * a British synth for voice 0 asks for Peter -- who is English -- and the API
 * then switches the synth to English to give him.  That is it working as
 * designed, and it is what made the GB accent look like it did nothing: what
 * was really happening is that nothing was speaking GB.
 */
static int base_voice(const char *lang)
{
    int i, n = tvtts_voice_count();

    for (i = 0; i < n; i++) {
        const char *c = tvtts_voice_language(i);

        if (c != NULL && strcmp(c, lang) == 0)
            return i;
    }
    return 0;
}

static unsigned long say(const char *lang, const char *text, int *gb_seen)
{
    tvtts_synth *s = tvtts_create_lang(11025, lang);

    if (s == NULL) {
        printf("  no synth for %s\n", lang);
        return 0;
    }
    g_hash = 1469598103u;
    g_samples = 0;
    g_gb_seen = 0;
    tvtts_set_voice(s, base_voice(lang));
    tvtts_speak_bytes(s, text, (uint32_t)strlen(text), on_audio, NULL);
    tvtts_destroy(s);
    if (gb_seen)
        *gb_seen = g_gb_seen;
    return g_hash;
}

/*
 * Non-rhoticity and the /r/ links, which are one rule: /r/ survives only
 * before a vowel.  This is a question about the phoneme string rather than
 * the audio, so it is checked there, against Cruttenden §12.4.7's own
 * examples and against the cases the same section says to refuse.
 *
 * The expected strings are exact on purpose.  An unrelated change to stress
 * or to a stop release will break this and want a human to look, which is
 * the point: the rule is cheap to get subtly wrong and expensive to notice
 * by ear.  build/check/gb_links.exe prints the pair for any phrase.
 */
static int r_rules(void)
{
    static const struct {
        const char *text;
        const char *gb;
        const char *why;
    } cases[] = {
        /* (b) linking /r/, historical, before a vowel-initial word.  These
         * six are §12.4.7(1)'s own examples. */
        { "far off",         "&Fr1R%oF.",            "link" },
        { "four aces",       "&Fg1R&A1S|Z.",         "link" },
        { "answer it",       "&r1NS3R%iTp.",         "link" },
        { "wear out",        "&Wk1R%f1Tp.",          "link" },
        { "fur inside",      "&F31R%iNSI1Dp.",       "link" },
        { "near it",         "%N41R%iTp.",           "link" },
        /* (a) before a vowel-initial suffix, which the book calls obligatory
         * and American renders with no /r/ at all. */
        { "boring",          "&Bg1R|~p.",            "link, suffix" },
        /* ...and the same thing INSIDE a word, which is the repair the
         * non-rhoticity work needed: an r-coloured vowel in front of
         * another vowel is carrying a /r/ with no `R` node to keep, so
         * making the vowel plain dropped it and left *orange* as
         * /A:IndZ/.  Case (a), which the book calls obligatory. */
        { "orange",  "&o1R|NJz.",     "link, inside a word" },
        { "forest",  "&Fo1R|STp.",    "link, inside a word" },
        { "during",  "%Jzc1Ri~p.",    "link, inside a word" },
        /* ...and the same words with a consonant after them: no /r/, and the
         * vowel still folded to the right lexical set.  Deleting the /r/ at
         * the word boundary instead of letting vowel.c do it got *bar
         * closed* LOT for START and *fur coat* STRUT for NURSE. */
        { "far behind",      "&Fr1%BEdI1NDp.",       "no link" },
        { "four cats",       "&Fg1&Ka1TS.",          "no link" },
        { "bar closed",      "&Br1&KLO1ZDp.",        "no link" },
        { "fur coat",        "&F31&KO1Tp.",          "no link" },
        /* (c) the one intrusive /r/ the book reports as normal: after schwa.
         *
         * The `3` in `V|Ni1L3` is not ours and is worth understanding before
         * anyone "fixes" it.  Stage1_GbRhotic appends a CANDIDATE `R` after
         * every word-final linking vowel; where that vowel is schwa the
         * engine's own Stage1_Vowel then folds `@`+`R` into `3`, exactly as
         * it does for American *better* -> `&Be1t3`.  vowel.c drops the `R`
         * again unless a vowel follows, so the fold is left standing alone.
         * That is right rather than merely harmless: GB merges lettER and
         * commA on one /@/, `3` and `@` are handed the same GB target by
         * gen_gb_vowels.py, and the fold gives *vanilla* the same final
         * length as *better* -- 15 frames against 18, where an unfolded `@`
         * would be 6.  It does mean the audit's NURSE row counts word-final
         * schwas alongside real NURSE, which is part of why that row and
         * comma's disagree about F2 from an identical target. */
        { "vanilla essence", "&V|Ni1L3R&e1S|NS.",    "intrusive, schwa" },
        { "vanilla pudding", "&V|Ni1L3&Pu1D|~p.",    "no link" },
        /* ...and the fold stops at function words, whose schwa is a weak
         * form that must stay short: *a* keeps `@` and its 6 frames. */
        { "a book",          "%@&Bu1Kp.",            "weak form, no fold" },
        /* (d) intrusive /r/ after /A:/ and /O:/, which the book reports
         * speakers avoiding.  Refused because American spells these vowels
         * plain -- *law* ends on `w`, not the r-coloured `g` of *floor* --
         * and *spa* is still `o` when the question is asked, before the PALM
         * swap makes it `r`. */
        { "straw in the wind", "&STRw1%iN%x@&Wi1NDp.", "refuse (d)" },
        { "law and order",   "&Lw1%@N&g1D3.",        "refuse (d)" },
        { "spa is open",     "&SBr1%iZ&O1P|N.",      "refuse (d)" },
    };
    /* Words American has already made non-rhotic by folding the /r/ into the
     * vowel: GB must come out identical, since that fold lands on the vowel
     * GB wants anyway. */
    static const char *unchanged[] = {
        "card park bird north", "start large heart", "word work first"
    };
    tvtts_synth *en = tvtts_create_lang(11025, "en");
    tvtts_synth *gb = tvtts_create_lang(11025, "en-GB");
    char got[512], ref[512];
    int i, bad = 0, n = (int)(sizeof cases / sizeof cases[0]);
    int m = (int)(sizeof unchanged / sizeof unchanged[0]);

    if (en == NULL || gb == NULL) {
        printf("  no en/en-GB pair for the /r/ rules\n");
        return 1;
    }
    printf("%-20s %-24s %s\n", "text", "General British", "rule");
    for (i = 0; i < n; i++) {
        got[0] = '\0';
        tvtts_text_to_phonemes(gb, cases[i].text, got, sizeof got);
        printf("%-20s %-24s %-18s%s\n", cases[i].text, got, cases[i].why,
               strcmp(got, cases[i].gb) ? "  WRONG" : "");
        if (strcmp(got, cases[i].gb) != 0) {
            printf("%-20s %-24s  <- wanted\n", "", cases[i].gb);
            bad++;
        }
    }
    for (i = 0; i < m; i++) {
        got[0] = ref[0] = '\0';
        tvtts_text_to_phonemes(gb, unchanged[i], got, sizeof got);
        tvtts_text_to_phonemes(en, unchanged[i], ref, sizeof ref);
        printf("%-20s %-24s %-18s%s\n", unchanged[i], got,
               "already non-rhotic", strcmp(got, ref) ? "  WRONG" : "");
        if (strcmp(got, ref) != 0) {
            printf("%-20s %-24s  <- American, which GB should match\n",
                   "", ref);
            bad++;
        }
    }
    tvtts_destroy(gb);
    tvtts_destroy(en);
    printf("\n");
    return bad;
}

/*
 * The intervocalic tap, which is American and not General British.
 *
 * *water* [ˈwɔːɾɚ] against [ˈwɔːtə] is about as loud as an accent marker
 * gets, and the engine taps in two different ways: by rule, for most of the
 * language, and spelled straight into a handful of dictionary entries.  Both
 * are checked, along with the pairs the tap collapses -- American makes
 * *writer* and *rider* homophones, and GB must not.
 */
static int tap_rules(void)
{
    static const struct {
        const char *text;
        const char *gb;
        const char *why;
    } cases[] = {
        /* by rule */
        { "city",     "&Si1TE.",     "rule" },
        { "pretty",   "&PRi1TE.",    "rule" },
        { "better",   "&Be1T3.",     "rule" },
        { "get up",   "&Ge1T%v1Pp.", "rule, across a word" },
        { "not a lot", "%No1T%@&Lo1Tp.", "rule, across a word" },
        /* spelled into the dictionary, where no rule can be suppressed and
         * the spelling has to say which stop the tap came from */
        { "water",    "&Ww1T3.",     "dictionary, <t>" },
        { "little",   "&Li1Tl.",     "dictionary, <t>, syllabic l kept" },
        { "metal",    "&Me1Tl.",     "dictionary, <t>, syllabic l kept" },
        { "leader",   "&LE1D3.",     "dictionary, <d>" },
        /* and the /l/ that de-flapping must not de-syllabify: §7.10.3 says
         * GB keeps the syllabic consonant */
        { "bottle",   "&Bo1Tl.",     "syllabic l kept" },
        { "title",    "&TI1Tl.",     "syllabic l kept" },
        /* §7.10.2(5): a /t/ before a CONSONANT is a glottal stop.  American
         * already does this before a sonorant, so *that man* is `xaq` in
         * both; GB drops that restriction. */
        { "not very",      "%No1q%Ve1RE.",   "glottal, 7.10.2(5)" },
        { "that place",    "%xaq&PLA1S.",    "glottal, 7.10.2(5)" },
        { "get back",      "&Ge1q&Ba1Kp.",   "glottal, 7.10.2(5)" },
        /* ...and the two that must NOT glottal: before a vowel it is a
         * plain [t], and word-internally before a syllabic /l/ the glottal
         * is the realisation the section calls stigmatised. */
        { "little bottle", "&Li1Tl&Bo1Tl.",  "no glottal before /l/" },
        /* §7.12.1: GA darkens /l/ in every position, GB keeps it clear
         * before a vowel and dark elsewhere.  The engine mostly had the
         * GB distribution already; what it lacked was a clear /l/ in
         * front of a BACK vowel or a schwa. */
        { "follow",   "&Fo1LO.",     "clear /l/ before a vowel" },
        { "colour",   "&Kv1L3.",     "clear /l/ before a vowel" },
        { "lily",     "&Li1LE.",     "clear /l/ before a vowel" },
        { "field",    "&FE1jDp.",    "dark /l/ before a consonant" },
        { "milk",     "&Mi1jKp.",    "dark /l/ before a consonant" },
        { "feel",     "&FE1j.",      "dark /l/ at the end" },
    };
    /* The pairs American's tap collapses into homophones. */
    static const char *const pairs[][2] = {
        { "writer", "rider" }, { "latter", "ladder" }
    };
    tvtts_synth *en = tvtts_create_lang(11025, "en");
    tvtts_synth *gb = tvtts_create_lang(11025, "en-GB");
    char got[512], b[512];
    int i, bad = 0, n = (int)(sizeof cases / sizeof cases[0]);
    int m = (int)(sizeof pairs / sizeof pairs[0]);

    if (en == NULL || gb == NULL) {
        printf("  no en/en-GB pair for the tap rules\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        got[0] = '\0';
        tvtts_text_to_phonemes(gb, cases[i].text, got, sizeof got);
        printf("%-20s %-24s %-18s%s\n", cases[i].text, got, cases[i].why,
               strcmp(got, cases[i].gb) ? "  WRONG" : "");
        if (strcmp(got, cases[i].gb) != 0) {
            printf("%-20s %-24s  <- wanted\n", "", cases[i].gb);
            bad++;
        }
    }
    for (i = 0; i < m; i++) {
        char label[64];

        got[0] = b[0] = '\0';
        tvtts_text_to_phonemes(gb, pairs[i][0], got, sizeof got);
        tvtts_text_to_phonemes(gb, pairs[i][1], b, sizeof b);
        sprintf(label, "%s/%s", pairs[i][0], pairs[i][1]);
        printf("%-20s %-11s %-12s %-18s%s\n", label, got, b,
               "must differ", (strcmp(got, b) == 0) ? "  WRONG" : "");
        if (strcmp(got, b) == 0) {
            printf("%-20s American makes these homophones; GB must not\n", "");
            bad++;
        }
    }
    tvtts_destroy(gb);
    tvtts_destroy(en);
    printf("\n");
    return bad;
}

/*
 * Two more of §7.10's graded changes, and one vowel the GB table was missing.
 *
 * 7.10.2(8) is lexically conditioned and the engine proves it has to be: at
 * stage 1 *cure* is `KYbR` and *pure* is `PYbR`, identical after the first
 * consonant, so no pattern over the phonemes can separate them.
 */
static int set_rules(void)
{
    static const struct {
        const char *text;
        const char *gb;
        const char *why;
    } cases[] = {
        /* 7.10.2(8): /O:/ for /U@/, the words the book names */
        { "poor",   "&Pg1.",       "CURE->NORTH" },
        { "sure",   "&sg1.",       "CURE->NORTH" },
        { "tour",   "&Tg1.",       "CURE->NORTH" },
        { "moor",   "&Mg1.",       "CURE->NORTH" },
        { "tourist", "&Tg1R|STp.", "CURE->NORTH, /r/ prevocalic" },
        /* ...and the ones it rules out, which must not move */
        { "pure",   "&PYc1.",      "stays CURE" },
        { "cure",   "&KYc1.",      "stays CURE, left out on purpose" },
        { "gourd",  "&Gc1Dp.",      "stays CURE" },
        { "lure",   "&Lc1.",       "stays CURE" },
        { "fewer",  "&FU13.",      "stays, /u:/+/@/" },
        /* Mary / marry / merry, which GA merges (§7.12.1) and GB keeps
         * apart.  The engine folded all three plus *very* into SQUARE.
         * A doubled <rr> never takes /E:/ -- §8.9.4's own spelling
         * table has twenty-nine examples and not one doubles it -- and
         * the vowel letter picks TRAP or DRESS.  A single <r> keeps
         * SQUARE unless the word is on the short list. */
        { "Mary",     "&Mk1RE.",     "single <r>: SQUARE" },
        { "marry",    "&Ma1RE.",     "<arr>: TRAP" },
        { "merry",    "&Me1RE.",     "<err>: DRESS" },
        { "berry",    "&Be1RE.",     "<err>, and the /r/ put back" },
        { "carry",    "&Ka1RE.",     "<arr>: TRAP" },
        { "very",     "%Ve1RE.",     "short list: DRESS" },
        { "vary",     "&Vk1RE.",     "not on it: stays SQUARE" },
        { "parent",   "&Pk1R|NTp.",   "single <r>: SQUARE" },
        { "librarian", "&LIBRk1REY@N.", "single <r>: SQUARE" },
        /* §7.12.1's *orange* class: GA has /O:/ or /A:/ where GB has
         * /Q/, so the engine gives START, NORTH or THOUGHT.  *sorry*
         * is in §8.9.8's own /Q/ table and *quarrel* too, its note
         * explaining the <a> as ME [a] rounded after /w/. */
        { "sorry",      "&So1RE.",        "<orr>: LOT" },
        { "borrow",     "&Bo1RO.",        "<orr>: LOT" },
        { "porridge",   "&Po1R|Jz.",      "<orr>: LOT" },
        { "moral",      "&Mo1R@j.",       "short list: LOT" },
        { "orange",     "&o1R|NJz.",      "short list: LOT" },
        { "quarrel",    "&KWo1R@j.",      "<a> after qu: LOT" },
        { "warrant",    "&Wo1R@NTp.",     "<a> after w: LOT" },
        { "boat",       "&BO1Tp.",        "GOAT untouched" },
        /* §7.10.3 lists schwa plus a non-syllabic consonant, where a
         * syllabic one has been the norm, among the trends GB has NOT
         * adopted, naming *garden bitten middle bottle*.  American
         * splits them; GB keeps the syllabic consonant. */
        { "awful",      "&w1Fl.",         "syllabic l kept" },
        { "uncle",      "&v1~Kl.",        "syllabic l kept" },
        { "whistle",    "&Wi1Sl.",        "syllabic l kept" },
        { "able",       "&A1Bl.",         "syllabic l kept" },
        { "apple",      "&a1Pl.",         "syllabic l kept" },
        /* ...but NOT after /r/, where English keeps the schwa anyway.
         * Caught by ear: *barrel* became `Ba1Rl` and the /r/ vanished,
         * having only a syllabic dark l to lean on. */
        { "barrel",     "&Ba1R@j.",       "schwa kept after /r/" },
        { "laurel",     "&Lo1R@j.",       "list: LOT, and the schwa after /r/" },
        { "squirrel",   "&SKWi1R@j.",     "<irr>: KIT" },
        { "stirrup",    "&SDi1R@Pp.",     "<irr>: KIT" },
        { "mirror",     "&Mi1R3.",        "<irr>: KIT" },
        { "irritate",   "&i1R|TATp.",     "<irr>, first vowel only" },
        { "hurry",      "&H31RE.",        "<urr> untouched" },
        { "worry",      "&Wv1RE.",        "list: <orr> after w is STRUT" },
        /* the `5` vowel the GB table has no row for: PRICE plus schwa
         * instead, which is what the dictionary holds before the merge */
        { "fire",   "&FI1Y3.",     "no `5`: PRICE + schwa" },
        { "hire",   "&HI1Y3.",     "no `5`: PRICE + schwa" },
        { "tyre is", "&TI1Y3R%i1Z.", "...and it can still link" },
    };
    tvtts_synth *gb = tvtts_create_lang(11025, "en-GB");
    char got[512];
    int i, bad = 0, n = (int)(sizeof cases / sizeof cases[0]);

    if (gb == NULL) {
        printf("  no en-GB synth for the lexical sets\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        got[0] = '\0';
        tvtts_text_to_phonemes(gb, cases[i].text, got, sizeof got);
        printf("%-20s %-24s %-18s%s\n", cases[i].text, got, cases[i].why,
               strcmp(got, cases[i].gb) ? "  WRONG" : "");
        if (strcmp(got, cases[i].gb) != 0) {
            printf("%-20s %-24s  <- wanted\n", "", cases[i].gb);
            bad++;
        }
    }
    tvtts_destroy(gb);
    printf("\n");
    return bad;
}

int main(void)
{
    static const char *texts[] = {
        "nurse bird heard",
        "start far car father",
        "four more thought",
        "square fair hair",
        "near here cure poor",
        "the north wind and the sun",
    };
    int i, bad = r_rules() + tap_rules() + set_rules();

    /* Can the callback see the flag at all, and does the engine respond to
     * it?  Set it by hand around an American render: if THAT changes the
     * audio, the engine is fine and only the language row is at fault. */
    {
        unsigned long plain, forced;
        int seen = 0;

        plain = say("en", "nurse bird heard", NULL);
        tv_gb_set(1);
        forced = say("en", "nurse bird heard", &seen);
        tv_gb_set(0);
        printf("flag forced by hand: seen=%d  audio %s\n\n", seen,
               (plain == forced) ? "UNCHANGED" : "changed");
    }

    printf("%-28s %-12s %-12s %s\n", "text", "en", "en-GB", "flag seen");
    for (i = 0; i < (int)(sizeof texts / sizeof texts[0]); i++) {
        int gb_us = 0, gb_gb = 0;
        unsigned long a = say("en", texts[i], &gb_us);
        unsigned long b = say("en-GB", texts[i], &gb_gb);
        unsigned long c = say("en", texts[i], NULL);

        printf("%-28s %08lx     %08lx     us=%d gb=%d%s%s\n",
               texts[i], a & 0xffffffffu, b & 0xffffffffu, gb_us, gb_gb,
               (a == b) ? "   NO CHANGE" : "",
               (a != c) ? "   AMERICAN DRIFTED" : "");
        if (a == b || a != c)
            bad++;
    }
    if (bad) {
        printf("\n%d check%s failed\n", bad, (bad == 1) ? "" : "s");
        return 1;
    }
    printf("\nGB changes every utterance; American is unchanged by it,\n"
           "and /r/ survives before a vowel and nowhere else.\n");
    return 0;
}
