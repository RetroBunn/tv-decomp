/*
 * Print a phrase's phonemes in both accents, side by side.
 *
 *     gb_links.exe "far off" "far behind"
 *     far off        en  F&o1Rp &w1Fp          gb  F&r1R &w1Fp
 *     far behind     en  F&o1Rp B|H&IND        gb  F&r B|H&IND
 *
 * This is the oracle for the /r/ rules: non-rhoticity, the linking /r/ of
 * *far off* and the intrusive /r/ of *vanilla essence* are one rule -- /r/
 * survives only before a vowel -- and whether it fired is a question about
 * the phoneme string, not about the audio.  Phrases arrive on the command
 * line or, with none, one per line on stdin.
 *
 * Unlike gb_spans.exe this prints what tvtts_text_to_phonemes returns, which
 * is the finished string with stress and stop releases, after every stage-1
 * pass has run.  gb_spans.exe prints the span mid-analysis instead, which is
 * what the lexicon patterns have to be written against.
 */
#include <stdio.h>
#include <string.h>

#include "tvtts.h"

static void run(tvtts_synth *en, tvtts_synth *gb, const char *text)
{
    char a[512], b[512];

    a[0] = b[0] = '\0';
    tvtts_text_to_phonemes(en, text, a, sizeof a);
    tvtts_text_to_phonemes(gb, text, b, sizeof b);
    printf("%-28s en  %-24s gb  %s\n", text, a, b);
}

int main(int argc, char **argv)
{
    tvtts_synth *en = tvtts_create_lang(11025, "en");
    tvtts_synth *gb = tvtts_create_lang(11025, "en-GB");
    char line[256];
    int i;

    if (en == NULL || gb == NULL) {
        fprintf(stderr, "need both an en and an en-GB synth\n");
        return 1;
    }
    if (argc > 1) {
        for (i = 1; i < argc; i++)
            run(en, gb, argv[i]);
    } else {
        while (fgets(line, sizeof line, stdin) != NULL) {
            size_t n = strlen(line);

            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = '\0';
            if (n > 0)
                run(en, gb, line);
        }
    }
    tvtts_destroy(gb);
    tvtts_destroy(en);
    return 0;
}
