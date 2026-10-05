/*
 * Which filter state a voice overflows, and by how much.
 *
 * The synthesiser's cascade saturates what each stage hands on, but several
 * stages store their *state* with a plain truncating cast, and a state that
 * wraps in a recursive filter inverts its own feedback -- heard as tearing
 * rather than as distortion.  None of the ten voices Centigram shipped comes
 * near it.  A voice of our own with a much shorter vocal tract can, and this
 * says which stage and how often rather than leaving it to be guessed at.
 *
 * Build it with TV_DIAG, which is the only thing that compiles the counters
 * in; see docs/VOICES.md.  Anything a custom voice is doing to the filter bank
 * shows up here before it shows up in somebody's ears.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "tvtts.h"
#include "engine.h"
#include "syn_hifi.h"

extern long tv_diag_max[TV_DIAG_STATES];
extern long tv_diag_over[TV_DIAG_STATES];
extern long tv_diag_syn[10];
extern long tv_diag_q15[5];
extern long tv_diag_q15_over[5];
extern long tv_diag_pole[9];
extern long long tv_diag_out[7];
static const char *const OUT_WHERE[7] = {
    "cascade sum arriving", "+ parallel sum", "after 6221/32768",
    "- de-emphasis state", "+ aspiration", "* format scale",
    "* 8, into sat16"
};
/* which state pair each section drives */
static const int POLE_STATE[9] = {10, 0, 2, 6, 8, 14, 16, 18, 20};
static const char *const POLE_WHERE[9] = {
    "cascade", "cascade", "cascade", "cascade", "cascade",
    "parallel", "parallel", "parallel", "parallel"
};
static const int Q15_COEF[5] = {5, 7, 9, 11, 15};

/* bytes in each resonator table: 6 and 7 are 180 int32, 8 is 700 */
static const long TABLE_BYTES[10] = {0,0,0,0,0,0, 720, 720, 2800, 0};

static int TVTTS_CALL swallow(const tvtts_event *ev, void *user)
{
    (void)ev;
    (void)user;
    return 0;
}

static void run(const char *name, int voice, int rate_index)
{
    static const char TEXT[] =
        "Hello there, my name is Johnny. I am a little kid.";
    tvtts_synth *s;
    int i, any = 0;

    memset(tv_diag_max, 0, sizeof tv_diag_max);
    memset(tv_diag_over, 0, sizeof tv_diag_over);
    memset(tv_diag_syn, 0, sizeof tv_diag_syn);
    memset(tv_diag_q15, 0, sizeof tv_diag_q15);
    memset(tv_diag_q15_over, 0, sizeof tv_diag_q15_over);
    memset(tv_diag_pole, 0, sizeof tv_diag_pole);
    memset(tv_diag_out, 0, sizeof tv_diag_out);

    s = tvtts_create(11025);
    if (s == NULL) {
        printf("cannot create a synth\n");
        return;
    }
    tvtts_set_sample_rate(s, rate_index);
    tvtts_set_voice(s, voice);
    tvtts_set_pitch(s, tvtts_voice_pitch(voice));
    tvtts_speak_utf8(s, TEXT, swallow, NULL);
    tvtts_destroy(s);

    printf("== %s at %u Hz ==\n", name,
           (unsigned)tvtts_sample_rate_hz(rate_index));
    for (i = 0; i < TV_DIAG_STATES; i++) {
        if (tv_diag_max[i] == 0)
            continue;
        if (tv_diag_over[i] == 0 && tv_diag_max[i] <= 32767)
            continue;
        printf("   o_20ae[%2d]  max |value| %8ld   wrapped %7ld times\n",
               i, tv_diag_max[i], tv_diag_over[i]);
        any = 1;
    }
    if (!any)
        printf("   every state stayed inside 16 bits\n");

    for (i = 6; i <= 8; i++) {
        long bytes = i == 8 && tvtts_sample_rate_hz(rate_index) == TV_SYNHIFI_RATE
                   ? (long)sizeof g_synhifi_8 : TABLE_BYTES[i];
        if (tv_diag_syn[i] > 0)
            printf("   table %d read to byte %5ld of %ld%s\n", i,
                   tv_diag_syn[i], bytes,
                   tv_diag_syn[i] >= bytes ? "   <-- PAST THE END" : "");
    }

    for (i = 0; i < 5; i++)
        if (tv_diag_q15_over[i] > 0)
            printf("   filt_coef[%2d] scaled to %6ld, past int16, %ld times"
                   "   <-- pole wraps\n",
                   Q15_COEF[i], tv_diag_q15[i], tv_diag_q15_over[i]);

    for (i = 0; i < 9; i++) {
        double r;

        if (tv_diag_pole[i] >= 0)
            continue;
        r = sqrt(-(double)tv_diag_pole[i] / 32768.0);
        if (r < 0.9)
            continue;
        printf("   o_20ae[%2d] %-8s pole radius %.5f   gain about %6.0f%s\n",
               POLE_STATE[i], POLE_WHERE[i], r,
               r < 1.0 ? 1.0 / (1.0 - r) : 0.0,
               r >= 0.999 ? "   <-- barely decays" : "");
    }

    printf("   output stage, widest value at each point:\n");
    for (i = 0; i < 7; i++)
        printf("     %-22s %12lld%s\n", OUT_WHERE[i], tv_diag_out[i],
               tv_diag_out[i] > 2147483647LL ? "   <-- past int32"
               : tv_diag_out[i] > 32767 ? "   <-- past int16" : "");

    /* the widest state either way, so a clean voice still says something */
    {
        int worst = 0;
        for (i = 1; i < TV_DIAG_STATES; i++)
            if (tv_diag_max[i] > tv_diag_max[worst])
                worst = i;
        printf("   widest: o_20ae[%d] at %ld (16 bits hold 32767)\n\n",
               worst, tv_diag_max[worst]);
    }
}

int main(int argc, char **argv)
{
    int i, n = tvtts_voice_count();
    const char *want = argc > 1 ? argv[1] : NULL;

    for (i = 0; i < n; i++) {
        const char *nm = tvtts_voice_name(i);

        if (nm == NULL)
            continue;
        if (want != NULL ? strcmp(nm, want) != 0
                         : (strcmp(nm, "Peter") != 0 &&
                            strcmp(nm, "Wanda") != 0 &&
                            strcmp(nm, "Johnny") != 0))
            continue;
        run(nm, i, TVTTS_SR_8K);
        run(nm, i, TVTTS_SR_11K);
        run(nm, i, TVTTS_SR_16K);
    }
    return 0;
}
