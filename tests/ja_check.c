/*
 * The Japanese front end's own test program.  Two jobs, one binary:
 *
 *   ja_check morae  <file>        one line of text in, its morae out
 *   ja_check oracle <tsv>         the full pipeline against oracle_frames.tsv
 *   ja_check front  <tsv>         the ANALYSER against front_oracle.tsv
 *   ja_check say    <file>        each line through the analyser, by hand
 *   ja_check romaji <file>        each line through the ROMAJI parser, with
 *                                 the count of characters it could not place
 *   ja_check g2p    <file>        word TAB phonemes -> the Japanese reading,
 *                                 which needs no engine because the phonemes
 *                                 are the input
 *
 * `morae` exists so the parser can be checked BEFORE the frame builder is
 * written, against jp_mora.to_morae over as much of the dictionary as anyone
 * cares to feed it -- see tools/check_ja_front.py.  It prints the same tokens
 * the Python prints, so the comparison is a diff and not an interpretation.
 *
 * `oracle` is the real test.  Every line of oracle_frames.tsv is a word, its
 * frame count and its frames as hex, written by the Python front end whose
 * output has been listened to; this reproduces them from the same text and
 * requires every byte to match.  When one does not it says which word, which
 * frame and which track, because "the Japanese sounds wrong" is not a bug
 * report anybody can act on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ja.h"
#include "ja_njd.h"

static const char *const c_name[JA_NC] = {
    "", "k", "ky", "g", "gy", "t", "d", "p", "py", "b", "by", "v",
    "s", "sh", "z", "j", "ch", "ts", "h", "hy", "f",
    "n", "ny", "m", "my", "r", "ry", "w", "y",
    "N", "N_n", "N_m", "N_k", "N_q"
};

static void print_mora(const ja_mora *m)
{
    static const char v[] = "aiueo";

    switch (m->kind) {
    case JA_M_CV:
        printf("%s%c", c_name[m->c], v[m->v]);
        break;
    case JA_M_N:      printf("N");  break;
    case JA_M_Q:      printf("Q");  break;
    case JA_M_LONG:   printf(":");  break;
    case JA_M_SP:     printf(" ");  break;
    case JA_M_BAR:    printf("|");  break;
    case JA_M_BARBAR: printf("||"); break;
    default:          printf("?");  break;
    }
}

static int do_morae(const char *path)
{
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    char line[4096];

    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        ja_mora m[1024];
        size_t len = strlen(line);
        int n, i;

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        n = ja_to_morae(line, len, m, (int)(sizeof m / sizeof m[0]));
        for (i = 0; i < n; i++) {
            if (i)
                printf("\t");
            print_mora(&m[i]);
        }
        printf("\n");
    }
    if (f != stdin)
        fclose(f);
    return 0;
}

/* ---- the oracle ---------------------------------------------------------- */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int do_oracle(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *line;
    size_t cap = 1 << 20;
    int words = 0, bad = 0, frames = 0, shown = 0;

    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    line = (char *)malloc(cap);
    if (line == NULL) {
        fclose(f);
        return 1;
    }
    while (fgets(line, (int)cap, f) != NULL) {
        char *tab1, *tab2, *tab3, *hex;
        ja_mora m[1024];
        ja_opts o;
        ja_utt u;
        int n_morae, want_n, i, k, first = -1;
        size_t hl;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        tab1 = strchr(line, '\t');
        if (tab1 == NULL) continue;
        tab2 = strchr(tab1 + 1, '\t');
        if (tab2 == NULL) continue;
        tab3 = strchr(tab2 + 1, '\t');
        if (tab3 == NULL) continue;
        *tab1 = *tab2 = *tab3 = '\0';
        hex = tab3 + 1;
        hl = strlen(hex);
        while (hl > 0 && (hex[hl - 1] == '\n' || hex[hl - 1] == '\r'))
            hex[--hl] = '\0';
        want_n = atoi(tab2 + 1);

        n_morae = ja_to_morae(line, strlen(line), m,
                              (int)(sizeof m / sizeof m[0]));
        ja_opts_default(&o);
        o.accent = atoi(tab1 + 1);
        if (n_morae <= 0 || ja_build(m, n_morae, &o, &u) != 0) {
            printf("%-20s BUILD FAILED\n", line);
            bad++;
            continue;
        }
        ja_pitch(&u, m, n_morae, &o, NULL);
        words++;
        frames += u.n;
        if (u.n != want_n) {
            printf("%-20s frame count %d, oracle has %d\n", line, u.n, want_n);
            bad++;
            ja_utt_free(&u);
            continue;
        }
        if (hl != (size_t)u.n * JA_NTRACK * 2) {
            printf("%-20s oracle hex is %u bytes for %d frames\n",
                   line, (unsigned)hl, u.n);
            bad++;
            ja_utt_free(&u);
            continue;
        }
        for (i = 0; i < u.n * JA_NTRACK; i++) {
            int want = hexval(hex[i * 2]) * 16 + hexval(hex[i * 2 + 1]);

            if (want != u.frames[i]) {
                if (first < 0)
                    first = i;
            }
        }
        if (first >= 0) {
            bad++;
            if (shown < 12) {
                int fi = first / JA_NTRACK;

                printf("%-20s frame %d differs; ours / oracle:\n", line, fi);
                printf("    ");
                for (k = 0; k < JA_NTRACK; k++)
                    printf(" %3d", u.frames[fi * JA_NTRACK + k]);
                printf("\n    ");
                for (k = 0; k < JA_NTRACK; k++) {
                    int o2 = (fi * JA_NTRACK + k) * 2;

                    printf(" %3d", hexval(hex[o2]) * 16 + hexval(hex[o2 + 1]));
                }
                printf("\n");
                shown++;
            }
        }
        ja_utt_free(&u);
    }
    free(line);
    fclose(f);
    printf("\n%d words, %d frames, %d bytes compared\n",
           words, frames, frames * JA_NTRACK);
    if (bad == 0)
        printf("every byte matches the Python front end.\n");
    else
        printf("%d of %d words differ.\n", bad, words);
    return bad != 0;
}

/* Source calibration may reduce only aspiration and parallel noise. It must
 * not change the schedule, formants, voicing, pitch, or unaffected voices. */
static int do_voice_sources(void)
{
    static const char *words[] = {
        "kakikukeko", "kya kyu kyo", "aQkyua", "akyea", "apyua",
        "kitte", "kippu", "aoiueo", "sa shi tsu cha za ji ta da",
        "ha hi fu he ho", "konnichiwa", "aQkyaa"
    };
    int sr, fast, wi, voice, changed = 0, bad = 0;
    for (sr = 11025; sr <= 16000; sr += 4975)
    for (fast = 0; fast < 2; fast++)
    for (wi = 0; wi < (int)(sizeof words / sizeof words[0]); wi++) {
        ja_mora m[128];
        ja_opts o;
        ja_utt ref;
        int n = ja_to_morae(words[wi], strlen(words[wi]), m, 128);
        ja_opts_default(&o);
        o.sr = sr;
        o.rate_scale = fast ? 0.5 : 1.0;
        if (ja_build(m, n, &o, &ref)) return 1;
        for (voice = 0; voice < 10; voice++) {
            ja_utt got;
            int i, k;
            o.engine_voice = voice;
            if (ja_build(m, n, &o, &got)) return 1;
            if (got.n != ref.n || got.n_ends != ref.n_ends || got.n_q != ref.n_q)
                bad++;
            else {
                for (i = 0; i < got.n_ends; i++)
                    if (got.ends[i] != ref.ends[i]) bad++;
                for (i = 0; i < got.n_q; i++)
                    if (got.q_ends[i] != ref.q_ends[i]) bad++;
                for (i = 0; i < got.n; i++) for (k = 0; k < JA_NTRACK; k++) {
                    int a = ref.frames[i * JA_NTRACK + k];
                    int b = got.frames[i * JA_NTRACK + k];
                    if (a == b) continue;
                    changed++;
                    if ((voice != 1 && voice != 2 && voice != 8 && voice != 9) ||
                        (k != 2 && k != 5 && k != 6) || b > a)
                        bad++;
                }
            }
            ja_utt_free(&got);
        }
        ja_utt_free(&ref);
    }
    printf("voice-source invariants: %s (%d reduced source values, %d errors)\n",
           !bad && changed ? "passed" : "FAILED", changed, bad);
    return bad || !changed;
}

/*
 * The analyser, against front_oracle.tsv.
 *
 * Each line is a text and what the Python front end made of it: the morae,
 * one accent type per accent phrase, a devoicing flag per mora, and the
 * question flag.  Every one of those 1,900 texts also agrees with the
 * compiled Open JTalk 1.11 reference word for word
 * (tools/ja_stage_parity.py), so matching this file is not merely agreeing
 * with a second implementation of the same reading -- it is agreeing with the
 * thing the rules were read from.
 *
 * Needs data/ja/jadic.bin.  Without it the analyser is skipped entirely and
 * the kana path is unaffected, so this reports that rather than failing.
 */
/* The 32-bit build links a minimal msvcrt import list, and mingw's snprintf
 * wants __ms_vsnprintf, which is not in it.  An accent type is a small
 * non-negative integer, so this is all the formatting that is needed. */
static int put_int(char *buf, int v)
{
    char tmp[12];
    int n = 0, w = 0;
    if (v < 0) {
        buf[w++] = '-';
        v = -v;
    }
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0)
        buf[w++] = tmp[--n];
    buf[w] = '\0';
    return w;
}

static int do_front(const char *path)
{
    char dict[1024];
    const char *dp;
    ja_dict d;
    FILE *f;
    char *line;
    size_t cap = 1 << 16;
    int lines = 0, bad = 0, shown = 0, morae = 0, rc;

    dp = ja_dict_path(dict, sizeof dict);
    if (dp == NULL) {
        printf("front: no dictionary found; set TVTTS_JA_DICT or run "
               "tools/gen_ja_dict.py\n");
        return 2;
    }
    rc = ja_dict_open(&d, dp);
    if (rc != 0) {
        fprintf(stderr, "front: %s: %s\n", dp, ja_dict_error(rc));
        return 1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        ja_dict_close(&d);
        return 1;
    }
    line = (char *)malloc(cap);
    if (line == NULL) {
        fclose(f);
        ja_dict_close(&d);
        return 1;
    }
    while (fgets(line, (int)cap, f) != NULL) {
        char *fld[5], *p;
        char got_m[4096], got_a[1024], got_d[2048];
        ja_front fr;
        int n = 0, i, at = 0, aat = 0;
        size_t l;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        fld[n++] = line;
        for (p = line; *p != '\0' && n < 5; p++)
            if (*p == '\t') {
                *p = '\0';
                fld[n++] = p + 1;
            }
        if (n != 5)
            continue;
        lines++;

        rc = ja_front_text(&d, fld[0], &fr);
        if (rc < 0) {
            fprintf(stderr, "front: out of memory on line %d\n", lines);
            bad++;
            break;
        }
        got_m[0] = got_a[0] = got_d[0] = '\0';
        for (i = 0; i < fr.n_morae; i++) {
            char sym[8];
            int sl = ja_mora_symbol(&fr.morae[i], sym, sizeof sym);
            if (sl <= 0) {
                sym[0] = '?';
                sym[1] = '\0';
                sl = 1;
            }
            if (at != 0 && (size_t)(at + 1) < sizeof got_m)
                got_m[at++] = ' ';
            if ((size_t)(at + sl + 1) < sizeof got_m) {
                memcpy(got_m + at, sym, (size_t)sl);
                at += sl;
            }
            if ((size_t)i + 1 < sizeof got_d)
                got_d[i] = fr.devoiced[i] ? '*' : '.';
        }
        got_m[at] = '\0';
        got_d[fr.n_morae < (int)sizeof got_d ? fr.n_morae : 0] = '\0';
        for (i = 0; i < fr.n_accents; i++) {
            if ((size_t)aat + 16 >= sizeof got_a)
                break;
            if (i != 0)
                got_a[aat++] = ',';
            aat += put_int(got_a + aat, fr.accents[i]);
        }
        got_a[aat] = '\0';
        morae += fr.n_morae;

        if (strcmp(got_m, fld[1]) != 0 || strcmp(got_a, fld[2]) != 0
            || strcmp(got_d, fld[3]) != 0
            || fr.question != atoi(fld[4])) {
            bad++;
            if (shown < 10) {
                printf("  %s\n", fld[0]);
                if (strcmp(got_m, fld[1]) != 0)
                    printf("    morae    C %s\n           py %s\n",
                           got_m, fld[1]);
                if (strcmp(got_a, fld[2]) != 0)
                    printf("    accents  C %s   py %s\n", got_a, fld[2]);
                if (strcmp(got_d, fld[3]) != 0)
                    printf("    devoiced C %s\n           py %s\n",
                           got_d, fld[3]);
                if (fr.question != atoi(fld[4]))
                    printf("    question C %d   py %s\n",
                           fr.question, fld[4]);
                shown++;
            }
        }
        ja_front_free(&fr);
    }
    free(line);
    fclose(f);
    ja_dict_close(&d);
    printf("front end: %d texts, %d morae, %d disagree -- %s\n",
           lines, morae, bad, bad == 0 ? "passed" : "FAILED");
    return bad != 0;
}

/*
 * One line of text in, everything the analyser made of it out.  For looking
 * at a case by hand when `front` says one disagrees.
 *
 * It takes a FILE rather than the text itself because on Windows argv arrives
 * through the ANSI code page, so a Japanese argument never survives the
 * command line.  A file is bytes.
 */
static int do_say(const char *path)
{
    char dict[1024], line[4096];
    const char *dp = ja_dict_path(dict, sizeof dict);
    ja_dict d;
    FILE *f;
    int rc;

    if (dp == NULL) {
        fprintf(stderr, "no dictionary found; set TVTTS_JA_DICT\n");
        return 2;
    }
    rc = ja_dict_open(&d, dp);
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", dp, ja_dict_error(rc));
        return 1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        ja_dict_close(&d);
        return 1;
    }
    while (fgets(line, (int)sizeof line, f) != NULL) {
        ja_front fr;
        size_t l = strlen(line);
        int i;
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        if (l == 0)
            continue;
        if (ja_front_text(&d, line, &fr) < 0) {
            fprintf(stderr, "out of memory\n");
            fclose(f);
            ja_dict_close(&d);
            return 1;
        }
        printf("%s\n", line);
        printf("  morae   ");
        for (i = 0; i < fr.n_morae; i++) {
            char sym[8];
            ja_mora_symbol(&fr.morae[i], sym, sizeof sym);
            printf("%s%s", i != 0 ? " " : "", sym);
        }
        printf("\n  accents ");
        for (i = 0; i < fr.n_accents; i++)
            printf("%s%d", i != 0 ? "," : "", fr.accents[i]);
        printf("\n  devoiced ");
        for (i = 0; i < fr.n_morae; i++)
            putchar(fr.devoiced[i] ? '*' : '.');
        printf("\n  question %d   words %d, unreadable %d, dropped %d\n",
               fr.question, fr.words, fr.unreadable, fr.dropped);
        ja_front_free(&fr);
    }
    fclose(f);
    ja_dict_close(&d);
    return 0;
}

/*
 * Each line of a file through the ROMAJI parser, with the count of characters
 * it could not place.  That count is the whole of the romaji test -- a Latin
 * string is read as Japanese only when nothing was dropped -- so being able
 * to see it over a list is how the test is checked against real data rather
 * than against a model of the parser.
 */
static int do_romaji(const char *path)
{
    FILE *f = fopen(path, "rb");
    char line[4096];
    int lines = 0, clean = 0;

    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    while (fgets(line, (int)sizeof line, f) != NULL) {
        ja_mora m[1024];
        int n, skipped = 0, i;
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        if (l == 0)
            continue;
        lines++;
        n = ja_to_morae_ex(line, l, m, (int)(sizeof m / sizeof m[0]),
                           &skipped);
        if (skipped == 0 && n > 0)
            clean++;
        printf("%s\t%d\t%d\t", line, skipped, n);
        for (i = 0; i < n; i++) {
            char sym[8];
            ja_mora_symbol(&m[i], sym, sizeof sym);
            printf("%s%s", i != 0 ? " " : "", sym);
        }
        putchar('\n');
    }
    fclose(f);
    fprintf(stderr, "%d lines, %d consumed whole (%.1f%%)\n",
            lines, clean, lines ? 100.0 * clean / lines : 0.0);
    return 0;
}

/*
 * The Latin-word adaptation on its own: each line is a word and the engine's
 * phoneme string for it, tab-separated, and out comes the Japanese reading.
 *
 * Taking the phonemes as INPUT is what makes this testable here -- the
 * adaptation needs no engine, only the string an engine produced, so this
 * binary can check it while still linking the Japanese front end alone.  The
 * caller supplies the strings, which is how tools/ja_latin_table.py compares
 * the C against the Python it was translated from.
 */
static int do_g2p(const char *path)
{
    FILE *f = fopen(path, "rb");
    char line[4096];
    int lines = 0;

    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    while (fgets(line, (int)sizeof line, f) != NULL) {
        char kana[512], *tab;
        int acc = 0, rc;
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        if (l == 0)
            continue;
        tab = strchr(line, '\t');
        if (tab == NULL)
            continue;
        *tab = '\0';
        lines++;
        rc = ja_g2p_read(tab + 1, line, kana, sizeof kana, &acc);
        printf("%s\t%s\t%s\t%d\n", line,
               rc == JA_G2P_WORD ? "word"
               : rc == JA_G2P_LETTERS ? "letters" : "none",
               rc == JA_G2P_WORD ? kana : "", acc);
    }
    fclose(f);
    fprintf(stderr, "%d words\n", lines);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "voices"))
        return do_voice_sources();
    if (argc == 3 && !strcmp(argv[1], "morae"))
        return do_morae(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "oracle"))
        return do_oracle(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "front"))
        return do_front(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "say"))
        return do_say(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "romaji"))
        return do_romaji(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "g2p"))
        return do_g2p(argv[2]);
    fprintf(stderr, "usage: ja_check morae <file>\n"
                    "       ja_check oracle <oracle_frames.tsv>\n"
                    "       ja_check front  <front_oracle.tsv>\n"
                    "       ja_check say    <file>\n"
                    "       ja_check voices\n");
    return 2;
}
