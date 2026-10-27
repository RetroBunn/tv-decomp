/*
 * Print what stage 1 hands the General British swap, one word per line.
 *
 * The substitution patterns in lang/eng/data/gb_lexicon.h are matched against
 * the phoneme nodes as they stand in the middle of stage 1, and that is not
 * what tvtts_text_to_phonemes prints: stress has not been assigned yet, stop
 * releases have not been added, and /st/ is still spelt with a plain T.  So
 * *student* is STbt here and SDb1t there, and a pattern written from the
 * printed form put the yod substitution on the wrong /t/.
 *
 * tools/gen_gb_lexicon.py runs this over its word lists and writes the
 * patterns against what comes back.  Words arrive on the command line or, if
 * there are none, one per line on stdin.
 *
 *     gb_spans.exe bath student tube
 *     BATH    &BaX
 *     STUDENT &STbt|NT
 *     TUBE    &TbB
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tvtts.h"

extern int tv_gb_span_trace;

static void run(tvtts_synth *s, const char *word)
{
    char buf[512];

    /* The analysis is what we are after; the trace prints from inside it. */
    tvtts_text_to_phonemes(s, word, buf, sizeof buf);
}

int main(int argc, char **argv)
{
    tvtts_synth *s = tvtts_create_lang(11025, "en");
    char line[256];
    int i;

    if (s == NULL) {
        fprintf(stderr, "no English synth\n");
        return 1;
    }
    tv_gb_span_trace = 1;
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        /* What each node IS -- type and attribute bits -- which the /r/
         * rules need in order to tell a phoneme from a marker. */
        tv_gb_span_trace = 2;
        argv++;
        argc--;
    }
    if (argc > 1) {
        for (i = 1; i < argc; i++)
            run(s, argv[i]);
    } else {
        while (fgets(line, sizeof line, stdin) != NULL) {
            size_t n = strlen(line);

            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = '\0';
            if (n > 0)
                run(s, line);
        }
    }
    tv_gb_span_trace = 0;
    tvtts_destroy(s);
    return 0;
}
