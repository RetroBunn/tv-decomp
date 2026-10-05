/*
 * Tests for the library surface the corpus cannot reach.
 *
 * tools/difftest.py already drives tv.exe -- and so the whole library --
 * over hundreds of configurations and demands byte-identical PCM, which
 * covers synthesis itself.  What it never exercises is a synth used more
 * than once, a synth interrupted part way, index marks, or the text
 * conversions, and those are exactly what a screen reader leans on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/* ---- a collector the tests can point a synth at ------------------------- */

#define MAX_MARKS 32

typedef struct {
    int16_t *pcm;
    size_t   n, cap;
    uint32_t mark[MAX_MARKS];
    uint32_t mark_pos[MAX_MARKS];
    int      marks;
    int      out_of_order;
    int      ended;
    /* abort after this many samples, 0 for never */
    size_t   stop_after;
} sink;

static int on_event(const tvtts_event *ev, void *user)
{
    sink *s = (sink *)user;

    if (ev->type == TVTTS_AUDIO) {
        if (s->n + ev->count > s->cap) {
            s->cap = (s->cap ? s->cap * 2 : 0x8000);
            while (s->cap < s->n + ev->count)
                s->cap *= 2;
            s->pcm = (int16_t *)realloc(s->pcm, s->cap * sizeof(int16_t));
        }
        memcpy(s->pcm + s->n, ev->samples, ev->count * sizeof(int16_t));
        s->n += ev->count;
        if (s->stop_after && s->n >= s->stop_after)
            return 1;
    } else if (ev->type == TVTTS_MARK) {
        if (s->marks < MAX_MARKS) {
            s->mark[s->marks] = ev->mark;
            s->mark_pos[s->marks] = ev->sample_pos;
        }
        /* the audio up to the mark must already have been handed over */
        if (ev->sample_pos != (uint32_t)s->n)
            s->out_of_order++;
        s->marks++;
    } else if (ev->type == TVTTS_END) {
        s->ended = 1;
    }
    return 0;
}

static void sink_free(sink *s)
{
    free(s->pcm);
    memset(s, 0, sizeof *s);
}

/* The loudest sample, for telling sound from silence. */
static int peak(const sink *s)
{
    size_t i;
    int m = 0;

    for (i = 0; i < s->n; i++) {
        int v = s->pcm[i] < 0 ? -(int)s->pcm[i] : (int)s->pcm[i];
        if (v > m)
            m = v;
    }
    return m;
}

static int same(const sink *a, const sink *b)
{
    return a->n == b->n &&
           memcmp(a->pcm, b->pcm, a->n * sizeof(int16_t)) == 0;
}

static void say(tvtts_synth *s, const char *text, sink *out)
{
    memset(out, 0, sizeof *out);
    tvtts_speak_bytes(s, text, (uint32_t)strlen(text), on_event, out);
}

/* ---- tests --------------------------------------------------------------- */

static const char TEXT[] = "The quick brown fox jumps over the lazy dog.";

/* A fresh synth speaking TEXT: what every other rendering must equal. */
static void render_fresh(sink *out)
{
    tvtts_synth *s = tvtts_create(11025);
    say(s, TEXT, out);
    tvtts_destroy(s);
}

static void test_reuse(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink a, b, ref;

    render_fresh(&ref);
    say(s, TEXT, &a);
    say(s, TEXT, &b);
    tvtts_destroy(s);

    check(a.n > 0 && a.ended, "first utterance produced audio and ended");
    check(same(&a, &ref), "first utterance matches a fresh synth");
    check(same(&b, &ref), "second utterance on the same synth matches");
    sink_free(&a); sink_free(&b); sink_free(&ref);
}

static void test_abort(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink cut, after, ref;
    int r;

    render_fresh(&ref);

    memset(&cut, 0, sizeof cut);
    cut.stop_after = 4000;
    r = tvtts_speak_bytes(s, TEXT, (uint32_t)strlen(TEXT), on_event, &cut);
    check(r == 1, "aborting mid-utterance reports it");
    check(cut.n < ref.n, "aborted utterance is shorter than a whole one");
    check(!cut.ended, "no end event after an abort");

    say(s, TEXT, &after);
    check(same(&after, &ref), "utterance after an abort is unaffected");
    tvtts_destroy(s);
    sink_free(&cut); sink_free(&after); sink_free(&ref);
}

static void test_marks(void)
{
    tvtts_synth *s = tvtts_create(11025);
    char buf[512], m[16];
    sink out;
    int i, ordered = 1;

    buf[0] = 0;
    tvtts_mark_sequence(m, sizeof m, 11); strcat(buf, m); strcat(buf, "Alpha ");
    tvtts_mark_sequence(m, sizeof m, 22); strcat(buf, m); strcat(buf, "beta ");
    tvtts_mark_sequence(m, sizeof m, 33); strcat(buf, m); strcat(buf, "gamma.");
    say(s, buf, &out);

    check(out.marks == 3, "three marks reported");
    check(out.marks == 3 && out.mark[0] == 11 && out.mark[1] == 22 &&
          out.mark[2] == 33, "marks arrive in order with their own values");
    for (i = 1; i < out.marks && i < MAX_MARKS; i++)
        if (out.mark_pos[i] < out.mark_pos[i - 1])
            ordered = 0;
    check(ordered, "mark positions do not go backwards");
    check(out.out_of_order == 0,
          "a mark arrives with its audio already delivered");
    check(out.marks > 0 && out.mark_pos[out.marks - 1] < out.n,
          "the last mark lands inside the audio");
    tvtts_destroy(s);
    sink_free(&out);
}

static void test_text(void)
{
    tvtts_synth *s = tvtts_create(11025);
    /* "caf" + e-acute + ".": cp1252 0xe9, U+00E9, UTF-8 c3 a9 */
    static const char raw[]  = { 'c', 'a', 'f', (char)0xe9, '.', 0 };
    static const char u8[]   = { 'c', 'a', 'f', (char)0xc3, (char)0xa9, '.', 0 };
    static const uint16_t u16[] = { 'c', 'a', 'f', 0x00e9, '.', 0 };
    /* a smart quote is cp1252 0x92 but U+2019, which plain Latin-1 loses */
    static const char rawq[] = { 'i', 't', (char)0x92, 's', 0 };
    static const char u8q[]  = { 'i', 't', (char)0xe2, (char)0x80,
                                 (char)0x99, 's', 0 };
    sink a, b, c;

    say(s, raw, &a);
    memset(&b, 0, sizeof b);
    tvtts_speak_utf8(s, u8, on_event, &b);
    memset(&c, 0, sizeof c);
    tvtts_speak_utf16(s, u16, on_event, &c);
    check(same(&a, &b), "utf-8 converts to the engine's encoding");
    check(same(&a, &c), "utf-16 converts to the engine's encoding");
    sink_free(&b); sink_free(&c);

    say(s, rawq, &b);
    memset(&c, 0, sizeof c);
    tvtts_speak_utf8(s, u8q, on_event, &c);
    check(same(&b, &c), "cp1252-only characters survive conversion");

    tvtts_destroy(s);
    sink_free(&a); sink_free(&b); sink_free(&c);
}

/*
 * The Spanish engine's pitch ceiling, and TVTTS_EXT_PITCH moving it.
 *
 * Stage 2 clamps every node before storing half of it in a byte.  The 1995
 * engine clamps to 50..200 and the 1997 one to 50..500, and both DLLs ship the
 * same voice table -- Carlos is 203 and Josefa 208, so those two sit above their
 * own engine's ceiling and every node of their contour is pinned to it.  That is
 * the monotone the extension exists to fix.
 *
 * Measured through the audio rather than by reading the flag back: the point is
 * that the pitch a caller asks for reaches the output.
 */
/* ---- the Spanish pitch contour ------------------------------------------- */

/*
 * How far the pitch moves across an utterance, as the ratio between the
 * longest and shortest glottal period, in thousandths.  1000 is a monotone.
 *
 * A proper pitch tracker is not wanted here -- only whether the contour
 * reaches further with the extension than without -- so this is plain
 * autocorrelation over the loud windows, in integers so that the freestanding
 * 32-bit build needs no soft-float.  The period is taken as the earliest lag
 * that is nearly as good as the best one, because a multiple of the true
 * period correlates just as well and would otherwise be picked instead.
 */
#define F0_FRAME 768
#define F0_WIDTH 256
#define F0_HOP   256
#define F0_LAG_LO 22            /* 11025/22  = 501 Hz, the engine's ceiling */
#define F0_LAG_HI 280           /* 11025/280 =  39 Hz, under any real floor */

static long long f0_corr(const int16_t *x, int lag)
{
    long long r = 0;
    int k;
    for (k = 0; k < F0_WIDTH; k++)
        r += (long long)x[k] * x[k + lag];
    return r;
}

/*
 * The lag search runs over a wide band around the pitch that was asked for --
 * from 0.6 to 2.2 times it -- rather than the engine's whole 50..500 Hz.  An
 * unconstrained autocorrelation picks a multiple or a sub-multiple of the true
 * period often enough to swamp the thing being measured; every contour this is
 * used on sits well inside the band, so nothing real is clipped by it.
 */
/* The voiced periods, sorted, shortest first.  Returns how many were found. */
static int period_lags(const sink *s, int pitch, int *lags, int cap)
{
    long long gsum = 0, gms;
    size_t i, f;
    int n = 0, lag_lo, lag_hi;

    if (s->n < (size_t)F0_FRAME * 4 || pitch <= 0)
        return 0;
    lag_lo = 11025 * 10 / (pitch * 22);
    lag_hi = 11025 * 10 / (pitch * 6);
    if (lag_lo < F0_LAG_LO)
        lag_lo = F0_LAG_LO;
    if (lag_hi > F0_LAG_HI)
        lag_hi = F0_LAG_HI;
    if (lag_hi <= lag_lo + 2)
        return 0;

    for (i = 0; i < s->n; i++)
        gsum += (long long)s->pcm[i] * s->pcm[i];
    gms = gsum / (long long)s->n;
    if (gms <= 0)
        return 0;

    for (f = 0; f + F0_FRAME < s->n && n < 4096; f += F0_HOP) {
        const int16_t *x = s->pcm + f;
        long long e = 0, r0, best = 0, r;
        int lag, bestlag = 0, k;

        for (k = 0; k < F0_FRAME; k++)
            e += (long long)x[k] * x[k];
        /* louder than a third of the overall level, squared, so the quiet
         * closures between syllables get no pitch invented for them */
        if ((e / F0_FRAME) * 100 < gms * 9)
            continue;

        r0 = f0_corr(x, 0);
        if (r0 <= 0)
            continue;
        for (lag = lag_lo; lag <= lag_hi; lag++) {
            r = f0_corr(x, lag);
            if (r > best) {
                best = r;
                bestlag = lag;
            }
        }
        if (bestlag == 0 || best * 10 < r0 * 3)
            continue;                   /* not periodic enough to be voiced */
        /* Autocorrelation peaks at the period and again at twice it, so if
         * half of what was picked is nearly as good, half is the period. */
        while (bestlag / 2 >= lag_lo &&
               f0_corr(x, bestlag / 2) * 100 >= best * 85)
            bestlag /= 2;
        if (n < cap)
            lags[n++] = bestlag;
    }
    for (i = 1; i < (size_t)n; i++) {    /* insertion sort */
        int v = lags[i], j = (int)i - 1;
        while (j >= 0 && lags[j] > v) {
            lags[j + 1] = lags[j];
            j--;
        }
        lags[j + 1] = v;
    }
    return n;
}

/* The ratio between the longest and shortest period, in thousandths. */
static int period_spread(const sink *s, int pitch)
{
    static int lags[4096];
    int n = period_lags(s, pitch, lags, 4096);

    if (n < 8 || lags[n / 10] <= 0)
        return 0;
    return 1000 * lags[n - 1 - n / 10] / lags[n / 10];
}

/* The longest period reached, in samples -- the lowest note of the contour.
 * A period rather than a frequency so that lower reads as larger. */
static int lowest_period(const sink *s, int pitch)
{
    static int lags[4096];
    int n = period_lags(s, pitch, lags, 4096);

    return n < 8 ? 0 : lags[n - 1 - n / 10];
}

/* The ratio above, with the monotone subtracted off, so the numbers compare
 * as "how much contour is there" rather than "how close to 1000". */
static int excursion(const sink *s, int pitch)
{
    int r = period_spread(s, pitch);
    return r > 1000 ? r - 1000 : 0;
}

/*
 * How wide the contour is, as the ratio of the longest period to the shortest
 * in parts per thousand -- the same units as period_spread, by a different
 * estimator.
 *
 * period_spread picks the best autocorrelation peak, which is sound enough to
 * compare a voice against itself (the contour and rate checks do exactly
 * that), but it halves a doubled lag and never corrects one picked an octave
 * high, so harmonic picks sit in its tails.  How much contamination there is
 * depends on the formants, so comparing two *different* voices needs tails
 * that can be trusted: on Frank against Peter below, no percentile band from
 * 5/95 to 40/60 told them apart, while this estimator separates them cleanly.
 *
 * This is YIN's cumulative mean normalized difference, which is what tells it
 * apart: a period is accepted only when its difference function is small
 * *relative to the running mean of every shorter lag*, so an octave-high pick
 * is rejected for being no better than its own subharmonics.  Kept in
 * integers, with the threshold as a fraction, so the freestanding 32-bit build
 * needs no maths library.
 */
#define YIN_FRAME  1024
#define YIN_LO     (11025 / 700)        /* 15 samples, 700 Hz */
#define YIN_HI     (11025 / 60)         /* 183 samples, 60 Hz */
#define YIN_THRESH 15                   /* accept d' below 0.15 */
/*
 * And if nothing crosses that, take the best lag there was, provided the frame
 * was at least this periodic.  A voice with jitter never crosses 0.15 -- Frank
 * does not, once he has Grandpa Amos's perturbation -- so without a fallback
 * he measures as no frames at all; with an *ungated* one, fricatives and
 * transitions come in as octave errors and both voices measure at five times
 * their real spread.  Swept: 300 to 500 all agree, 600 and above blow up.
 */
#define YIN_GATE   500                  /* d' x 1000 */

static int yin_taus(const sink *s, int *out, int cap)
{
    static int tau_of[4096], smooth[4096];
    static long long d[YIN_HI + 1];
    long long gsum = 0, gms;
    size_t i, f;
    int n = 0;

    if (s->n < (size_t)YIN_FRAME * 4)
        return 0;
    for (i = 0; i < s->n; i++)
        gsum += (long long)s->pcm[i] * s->pcm[i];
    gms = gsum / (long long)s->n;
    if (gms <= 0)
        return 0;

    for (f = 0; f + YIN_FRAME + YIN_HI < s->n && n < 4096; f += F0_HOP) {
        const int16_t *x = s->pcm + f;
        long long e = 0, run = 0;
        int tau, best = 0, k;

        for (k = 0; k < YIN_FRAME; k++)
            e += (long long)x[k] * x[k];
        if ((e / YIN_FRAME) * 100 < gms * 9)
            continue;                   /* a closure between syllables */

        for (tau = 1; tau <= YIN_HI; tau++) {
            long long v = 0;
            for (k = 0; k < YIN_FRAME; k++) {
                long long t = (long long)x[k] - x[k + tau];
                v += t * t;
            }
            d[tau] = v;
        }
        /*
         * d'(tau) = d[tau] / (mean of d[1..tau]).  Scaled by a thousand it
         * stays inside 64 bits (4.4e12 * 183 * 1000), so no floating point and
         * no overflow, and two lags can be compared directly.
         */
        {
            long long lowest = 0;
            int at_lowest = 0;

            for (tau = 1; tau <= YIN_HI; tau++) {
                long long dp;

                run += d[tau];
                if (tau < YIN_LO || run <= 0)
                    continue;
                dp = d[tau] * (long long)tau * 1000 / run;
                if (at_lowest == 0 || dp < lowest) {
                    lowest = dp;
                    at_lowest = tau;
                }
                if (dp < YIN_THRESH * 10) {
                    best = tau;
                    break;
                }
            }
            /* YIN's own fallback: nothing crossed the threshold, so take the
             * best lag there was.  A rough voice -- one with jitter, which is
             * exactly what this file now has to measure -- never crosses. */
            if (best == 0 && lowest < YIN_GATE)
                best = at_lowest;
        }
        if (best == 0)
            continue;
        while (best < YIN_HI && d[best + 1] < d[best])
            best++;                     /* on to the bottom of the dip */
        tau_of[n++] = best;
    }

    /*
     * A median of five along the utterance first.  Intonation is a slow
     * trajectory and jitter is a fast one, and a voice with jitter has both;
     * without this the cycle-to-cycle spread would be counted as contour and
     * the two would be impossible to tell apart.
     */
    if (n < 8)
        return 0;
    for (i = 0; i + 5 <= (size_t)n; i++) {
        int w[5], a, b;

        for (a = 0; a < 5; a++)
            w[a] = tau_of[i + a];
        for (a = 1; a < 5; a++) {       /* insertion sort of five */
            int v = w[a];
            for (b = a - 1; b >= 0 && w[b] > v; b--)
                w[b + 1] = w[b];
            w[b + 1] = v;
        }
        smooth[i] = w[2];
    }
    n = n - 4;

    for (i = 1; i < (size_t)n; i++) {   /* insertion sort */
        int v = smooth[i], j = (int)i - 1;
        while (j >= 0 && smooth[j] > v) {
            smooth[j + 1] = smooth[j];
            j--;
        }
        smooth[j + 1] = v;
    }
    if (n > cap)
        n = cap;
    for (i = 0; i < (size_t)n; i++)
        out[i] = smooth[i];
    return n;
}

/*
 * The contour's width, as the ratio of the longest period to the shortest in
 * parts per thousand.
 *
 * The deciles are taken from the frames within a factor of 1.6 of the median
 * period, not from all of them.  A frame that lands on a harmonic, or on
 * YIN_HI because the voice dipped below sixty hertz, is off by a factor of two
 * or more, and one such frame in ten moves a decile further than any contour
 * does: measured across the lot, Francisco reads 5250 where the engine moves
 * him less far than Pedro, who reads 1296.  A deep, breathy voice is where YIN
 * is least sure, which is exactly the kind of voice this file now carries, so
 * the window is the difference between measuring the contour and measuring the
 * tracker.  1.6 separates the two comfortably: an octave error sits at 2.0 or
 * 0.5, and across all 22 voices the furthest a real decile strays from its own
 * median is 1.40.
 */
static int yin_spread(const sink *s)
{
    static int t[4096];
    int n = yin_taus(s, t, 4096);
    int m, a, b, lo, hi;

    if (n < 8)
        return 0;
    m = t[n / 2];                       /* yin_taus hands them back sorted */
    for (a = 0; a < n && t[a] * 16 < m * 10; a++)
        ;
    for (b = n - 1; b > a && t[b] * 10 > m * 16; b--)
        ;
    n = b - a + 1;
    if (n < 8 || t[a + n / 10] <= 0)
        return 0;
    lo = t[a + n / 10];
    hi = t[a + n - 1 - n / 10];
    return 1000 * hi / lo;
}

/* The same, with the monotone taken off, to read as "how much contour is
 * there" beside excursion() above. */
static int yin_excursion(const sink *s)
{
    int r = yin_spread(s);
    return r > 1000 ? r - 1000 : 0;
}

static void say_rate(const char *lang, int voice, int wpm, unsigned ext,
                     const char *text, sink *out)
{
    tvtts_synth *s;

    tvtts_set_extensions(ext);
    s = tvtts_create_lang(11025, lang);
    tvtts_set_voice(s, voice);
    tvtts_set_rate(s, wpm);
    say(s, text, out);
    tvtts_destroy(s);
}

static void say_at(const char *lang, int voice, int pitch, unsigned ext,
                   const char *text, sink *out)
{
    tvtts_synth *s;

    tvtts_set_extensions(ext);
    s = tvtts_create_lang(11025, lang);
    tvtts_set_voice(s, voice);
    tvtts_set_pitch(s, pitch);
    say(s, text, out);
    tvtts_destroy(s);
}

/*
 * Voices this project defines itself, past the ten in the DLL.
 *
 * Frank is the first: a deep male voice from MindMaker's TextAssist build,
 * which is Peter at 72 Hz with a vocal tract a tenth longer.  See
 * src/engine/voices.c for where those two numbers come from and NOTICE for
 * what was and was not taken from that product.
 */
/* How many times a character appears, for checking a compiled score. */
static int count_char(const char *s, char c)
{
    int n = 0;
    while (*s != '\0')
        if (*s++ == c)
            n++;
    return n;
}

/* The value of the pitch escape in a compiled score -- the one ending in 'p',
 * which is not the first escape in the string. */
static int pitch_esc(const char *s)
{
    const char *e;

    for (e = strchr(s, '['); e != NULL; e = strchr(e + 1, '[')) {
        const char *q = e + 1;

        while (*q >= '0' && *q <= '9')
            q++;
        if (*q == 'p' && q > e + 1)
            return atoi(e + 1);
    }
    return 0;
}

/* Sing a score into a sink, at the default voice. */
static void sing_at(const char *score, sink *out)
{
    tvtts_synth *s;

    memset(out, 0, sizeof *out);
    tvtts_set_extensions(TVTTS_EXT_ALL);
    s = tvtts_create_lang(11025, "en");
    tvtts_sing(s, score, on_event, out);
    tvtts_destroy(s);
}

/*
 * The pitch a sung note came out at, in whole hertz: the median of the
 * per-frame periods yin_spread already finds, turned back into a frequency.
 */
static int sung_hz(const sink *s)
{
    static int tau[4096];
    int n = yin_taus(s, tau, 4096);

    return n < 8 ? 0 : 11025 / tau[n / 2];
}

static void test_custom_voices(void)
{
    static const char *const TXT = "Hello there, my name is Frank.";
    int i, n = tvtts_voice_count(), kit = -1, peter = -1;

    for (i = 0; i < n; i++) {
        const char *nm = tvtts_voice_name(i);
        if (nm == NULL)
            continue;
        if (strcmp(nm, "Frank") == 0)
            kit = i;
        else if (strcmp(nm, "Peter") == 0)
            peter = i;
    }
    check(kit >= 0, "Frank is one of the voices");
    check(peter == 0, "and Peter is still voice 0");
    if (kit < 0)
        return;

    check(kit >= 10, "Frank sits past the ten the DLL carries");
    check(strcmp(tvtts_voice_language(kit), "en") == 0, "Frank speaks English");
    check(tvtts_voice_pitch(kit) == 72, "Frank's pitch is F0Def from Frank.tav");
    check(tvtts_voice_rate(kit) == tvtts_voice_rate(peter),
          "and what Frank.tav does not give is Peter's");

    {
        sink k = {0}, p_same = {0}, p_own = {0};
        tvtts_synth *s;

        s = tvtts_create(11025);
        tvtts_set_voice(s, kit);
        say(s, TXT, &k);
        tvtts_destroy(s);

        /* Peter at Kit's pitch.  If only the pitch were being applied the two
         * would be identical -- which is exactly what happened while
         * Engine_SetVoice still refused anything past its ten, and the voice
         * silently stayed as it was.  This is the check for that. */
        s = tvtts_create(11025);
        tvtts_set_voice(s, peter);
        tvtts_set_pitch(s, 72);
        say(s, TXT, &p_same);
        tvtts_destroy(s);

        s = tvtts_create(11025);
        tvtts_set_voice(s, peter);
        say(s, TXT, &p_own);
        tvtts_destroy(s);

        check(k.n > 4000, "Frank speaks");
        check(!same(&k, &p_own), "and does not sound like Peter");
        check(!same(&k, &p_same),
              "nor like Peter moved to the same pitch -- the tract differs too");

        sink_free(&k);
        sink_free(&p_same);
        sink_free(&p_own);
    }

    /*
     * IntonLevel: how far the contour moves.  Frank.tav gives 0.7 where Bill
     * gives 1.0, so at the same pitch his contour has to reach less far than
     * Peter's -- measured out of the audio, not read back from the field.
     */
    {
        sink f = {0}, p = {0};
        int ef, ep;

        say_at("en", kit, 72, TVTTS_EXT_ALL, TXT, &f);
        say_at("en", peter, 72, TVTTS_EXT_ALL, TXT, &p);
        ef = yin_excursion(&f);
        ep = yin_excursion(&p);
        printf("    [inton, contour in parts per thousand of period: Frank %d, "
               "Peter %d, both at pitch 72]\n", ef, ep);
        check(ef > 0 && ep > 0, "both voices have a contour at pitch 72");
        check(ef * 10 < ep * 9,
              "Frank's contour reaches less far than Peter's, as IntonLevel asks");
        sink_free(&f);
        sink_free(&p);
    }

    /* Selecting a voice that does not exist still does nothing, as it always
     * did; the bound moved out to the voices defined, not away entirely. */
    {
        sink a = {0}, b = {0};
        tvtts_synth *s = tvtts_create(11025);

        tvtts_set_voice(s, peter);
        say(s, TXT, &a);
        tvtts_set_voice(s, n + 50);
        say(s, TXT, &b);
        tvtts_destroy(s);
        check(same(&a, &b), "a voice number nobody has leaves the voice alone");
        sink_free(&a);
        sink_free(&b);
    }
}

/*
 * Francisco: the same mechanism on the 1995 Spanish engine.
 *
 * es/voices.c mirrors src/engine/voices.c rather than sharing it, because the
 * two generations keep their per-voice data in differently named tables, so
 * each of the three things that can quietly fail -- the voice being named, the
 * tract reaching the synthesiser, IntonLevel reaching the contour -- is worth
 * checking twice.  Francisco is Frank's definition over Pedro.
 */
static void test_es_custom_voices(void)
{
    static const char *const TXT = "Hola, me llamo Francisco.";
    int i, n = tvtts_voice_count(), fran = -1, pedro = -1;

    for (i = 0; i < n; i++) {
        const char *nm = tvtts_voice_name(i);
        if (nm == NULL || strcmp(tvtts_voice_language(i), "es") != 0)
            continue;
        if (strcmp(nm, "Francisco") == 0)
            fran = i;
        else if (strcmp(nm, "Pedro") == 0)
            pedro = i;
    }
    check(fran >= 0, "Francisco is one of the Spanish voices");
    check(pedro >= 0, "and Pedro is still there beside him");
    if (fran < 0 || pedro < 0)
        return;

    /* He is named Francisco, not Frank: a Spanish name among Spanish names,
     * and clear of the Italian engine's Franco. */
    check(strcmp(tvtts_voice_name(fran), "Francisco") == 0,
          "under the name Francisco");
    check(tvtts_voice_pitch(fran) == 72, "his pitch is Frank's 72, not Pedro's");
    check(tvtts_voice_rate(fran) == tvtts_voice_rate(pedro),
          "and what the definition leaves out is Pedro's");

    {
        sink f = {0}, p_same = {0}, p_own = {0};

        say_at("es", fran, tvtts_voice_pitch(fran), TVTTS_EXT_ALL, TXT, &f);
        /* Pedro moved to Francisco's pitch.  Were Engine_SetVoice still
         * refusing anything past its ten -- silently, as it did -- these two
         * would come out identical. */
        say_at("es", pedro, 72, TVTTS_EXT_ALL, TXT, &p_same);
        say_at("es", pedro, tvtts_voice_pitch(pedro), TVTTS_EXT_ALL, TXT,
                &p_own);

        check(f.n > 4000, "Francisco speaks");
        check(!same(&f, &p_own), "and does not sound like Pedro");
        check(!same(&f, &p_same),
              "nor like Pedro moved to the same pitch -- the tract differs too");

        sink_free(&f);
        sink_free(&p_same);
        sink_free(&p_own);
    }

    /* IntonLevel, out of the audio: 0.7 against Pedro's 1.0, both at pitch 72
     * so that only the contour differs. */
    {
        sink f = {0}, p = {0};
        int ef, ep;

        say_at("es", fran, 72, TVTTS_EXT_ALL, TXT, &f);
        say_at("es", pedro, 72, TVTTS_EXT_ALL, TXT, &p);
        ef = yin_excursion(&f);
        ep = yin_excursion(&p);
        printf("    [inton, contour in parts per thousand of period: Francisco "
               "%d, Pedro %d, both at pitch 72]\n", ef, ep);
        check(ef > 0 && ep > 0, "both voices have a contour at pitch 72");
        check(ef * 10 < ep * 9,
              "Francisco's contour reaches less far than Pedro's, as "
              "IntonLevel asks");
        sink_free(&f);
        sink_free(&p);
    }
}

/*
 * TVTTS_EXT_RATE on the Spanish engine, which never had it until now.
 *
 * Engine_SetSpeed hands (wpm - 46) >> 3 to three tables of 26, 24 and 16 rows,
 * and the index reaches 44 at 400 wpm, so the original reads off the end of all
 * three.  What that does is not a graceful slowdown: at 254 wpm it comes out
 * *longer* than at 253, and by 300 the phoneme percentage it lands on leaves a
 * whole sentence taking a tenth of a second -- the engine, in effect, refusing
 * to speak.
 */
static void test_es_rate(void)
{
    static const char *const TXT =
        "Uno dos tres cuatro cinco seis siete ocho nueve diez.";
    const unsigned NOR = TVTTS_EXT_ALL & ~TVTTS_EXT_RATE;
    const char *es = NULL;
    int i, pedro = -1;

    for (i = 0; i < tvtts_language_count(); i++)
        if (strcmp(tvtts_language(i), "es") == 0)
            es = tvtts_language(i);
    if (es == NULL)
        return;
    for (i = 0; i < tvtts_voice_count(); i++)
        if (strcmp(tvtts_voice_language(i), "es") == 0 && pedro < 0)
            pedro = i;
    if (pedro < 0)
        return;

    /*
     * Every row the phoneme table actually has is untouched.  That table is 24
     * rows where English's is 26, so the range this holds over is 46..237 wpm
     * rather than 46..253; above it the original was already reading past the
     * end, which is the thing being fixed rather than behaviour to preserve.
     */
    {
        static const int SAME[] = { 46, 100, 150, 200, 230, 237 };
        int all_same = 1;

        for (i = 0; i < (int)(sizeof SAME / sizeof SAME[0]); i++) {
            sink a = {0}, b = {0};
            say_rate(es, pedro, SAME[i], NOR, TXT, &a);
            say_rate(es, pedro, SAME[i], TVTTS_EXT_ALL, TXT, &b);
            if (!same(&a, &b))
                all_same = 0;
            sink_free(&a);
            sink_free(&b);
        }
        check(all_same, "up to 237 wpm the rate extension changes nothing");
    }

    /* Past them it stops breaking and starts going faster. */
    {
        sink off253 = {0}, off400 = {0};
        sink on253 = {0}, on300 = {0}, on400 = {0};

        say_rate(es, pedro, 253, NOR, TXT, &off253);
        say_rate(es, pedro, 400, NOR, TXT, &off400);
        say_rate(es, pedro, 253, TVTTS_EXT_ALL, TXT, &on253);
        say_rate(es, pedro, 300, TVTTS_EXT_ALL, TXT, &on300);
        say_rate(es, pedro, 400, TVTTS_EXT_ALL, TXT, &on400);

        printf("    [rate, samples at 253/300/400 wpm: off %u/-/%u, "
               "on %u/%u/%u]\n",
               (unsigned)off253.n, (unsigned)off400.n,
               (unsigned)on253.n, (unsigned)on300.n, (unsigned)on400.n);

        /* the fault: the top of the range says almost nothing at all */
        check(off400.n * 8 < off253.n,
              "without it the top of the range barely speaks");
        /* and the fix: it speaks, and it speaks faster */
        check(on400.n * 8 > off253.n, "with it the top of the range speaks");
        check(on400.n < on300.n && on300.n < on253.n,
              "and each step up in rate is genuinely shorter");

        sink_free(&off253);
        sink_free(&off400);
        sink_free(&on253);
        sink_free(&on300);
        sink_free(&on400);
    }

    tvtts_set_extensions(TVTTS_EXT_ALL);
}

/*
 * TVTTS_EXT_FLOOR: the bottom of the Spanish engine's range.
 *
 * That engine calls a pitch track under 60 an error and puts 65 in its place --
 * a low C, and audibly where its bottom stops.  English has no such check, and
 * Stage 2 has already clamped the track to 50, so nothing needs it.
 *
 * Jorge is the case: the lowest Spanish voice, pitch 50, the same as English's
 * Sidney, and only one of the two could reach it.
 */
static void test_pitch_floor(void)
{
    /* Long and sonorant, so the contour has room to fall at the end of a
     * phrase, which is the only place the bottom of it is reached. */
    static const char *const TXT =
        "Mama mia, mama mia, la luna llena la mano, mama mia.";
    const char *es = NULL;
    int i, jorge = -1, lowest = 1000;

    for (i = 0; i < tvtts_language_count(); i++)
        if (strcmp(tvtts_language(i), "es") == 0)
            es = tvtts_language(i);
    if (es == NULL)
        return;
    /* found by its pitch rather than by name, since what matters is that it
     * is the one sitting at the bottom of the range */
    for (i = 0; i < tvtts_voice_count(); i++)
        if (strcmp(tvtts_voice_language(i), "es") == 0 &&
            tvtts_voice_pitch(i) < lowest) {
            lowest = tvtts_voice_pitch(i);
            jorge = i;
        }
    check(jorge >= 0 && lowest == 50,
          "the lowest Spanish voice sits at pitch 50");
    if (jorge < 0)
        return;

    {
        sink off = {0}, on = {0}, hi_off = {0}, hi_on = {0};
        int p_off, p_on;

        say_at(es, jorge, 50, TVTTS_EXT_ALL & ~TVTTS_EXT_FLOOR, TXT, &off);
        say_at(es, jorge, 50, TVTTS_EXT_ALL, TXT, &on);
        p_off = lowest_period(&off, 50);
        p_on = lowest_period(&on, 50);
        printf("    [floor, longest period in samples: off %d, on %d "
               "(= %d Hz and %d Hz)]\n",
               p_off, p_on, p_off ? 11025 / p_off : 0, p_on ? 11025 / p_on : 0);

        check(!same(&off, &on), "the extension changes the low end");
        check(p_off > 0 && p_on > 0, "both renderings have a lowest note");
        check(p_on > p_off * 11 / 10, "with it the voice reaches lower");

        /*
         * And where the engine never goes near the substitution, it changes
         * nothing -- which is what says this lifts a limit rather than
         * retuning the voice.  At pitch 150 the contour stays far above 60.
         */
        say_at(es, jorge, 150, TVTTS_EXT_ALL & ~TVTTS_EXT_FLOOR, TXT, &hi_off);
        say_at(es, jorge, 150, TVTTS_EXT_ALL, TXT, &hi_on);
        check(same(&hi_off, &hi_on),
              "well above it the extension changes nothing at all");

        sink_free(&off);
        sink_free(&on);
        sink_free(&hi_off);
        sink_free(&hi_on);
    }

    tvtts_set_extensions(TVTTS_EXT_ALL);
}

/*
 * TVTTS_EXT_CONTOUR: the Spanish engine's intonation as the pitch moves.
 *
 * That engine adds fixed numbers of hertz to the base pitch to make its
 * contour, so raising the pitch shrinks the interval the contour spans and the
 * voice goes flat.  The extension scales the excursion by the pitch against
 * the voice's own, which is the shape the 1997 English engine has.
 *
 * Three things are checked, and the middle one is the point: that the audio
 * really does move further, rather than the flag merely reading back as set.
 */
static void test_contour(void)
{
    static const char *const TXT = "Mala mala mala, mala mala mala.";
    const char *es = NULL;
    int i, pedro = -1;

    for (i = 0; i < tvtts_language_count(); i++)
        if (strcmp(tvtts_language(i), "es") == 0)
            es = tvtts_language(i);
    if (es == NULL)
        return;                         /* no Spanish in this build */
    for (i = 0; i < tvtts_voice_count(); i++)
        if (strcmp(tvtts_voice_language(i), "es") == 0 && pedro < 0)
            pedro = i;
    if (pedro < 0)
        return;

    /*
     * The contour must not depend on which voice is speaking.  Neither engine's
     * does -- nothing in either Stage2_Contour reads the voice, and English's
     * g_voice_pitch_scale is 32766 for all ten -- so at one pitch every voice
     * has to be affected the same way.  An earlier version of this extension
     * scaled against each voice's own pitch and broke exactly that: Josefa and
     * Carlos, the two highest, came out flattest at a shared pitch and their
     * centre moved with it.
     *
     * Two things say it is voice-independent now.  At the reference pitch, 85,
     * the scale is one and every voice is byte-identical either way; away from
     * it, every voice changes, so none is being quietly left alone.
     */
    {
        int same_at_ref = 1, changed_away = 1, tested = 0, n_es = 0;

        /* However many Spanish voices there are -- the DLL's ten and any of
         * OpenTV's own -- the loop below has to cover all of them, so the count
         * is taken rather than written down. */
        for (i = 0; i < tvtts_voice_count(); i++)
            if (strcmp(tvtts_voice_language(i), "es") == 0)
                n_es++;

        for (i = 0; i < tvtts_voice_count(); i++) {
            sink a = {0}, b = {0}, c = {0}, d = {0};

            if (strcmp(tvtts_voice_language(i), "es") != 0)
                continue;
            say_at(es, i, 85, TVTTS_EXT_ALL & ~TVTTS_EXT_CONTOUR, TXT, &a);
            say_at(es, i, 85, TVTTS_EXT_ALL, TXT, &b);
            if (!same(&a, &b))
                same_at_ref = 0;

            say_at(es, i, 300, TVTTS_EXT_ALL & ~TVTTS_EXT_CONTOUR, TXT, &c);
            say_at(es, i, 300, TVTTS_EXT_ALL, TXT, &d);
            if (same(&c, &d))
                changed_away = 0;

            tested++;
            sink_free(&a);
            sink_free(&b);
            sink_free(&c);
            sink_free(&d);
        }
        check(tested == n_es && n_es >= 10,
              "every Spanish voice was tried, the DLL's ten at least");
        check(same_at_ref,
              "at the reference pitch every voice is byte-identical either way");
        check(changed_away,
              "away from it every voice is affected, none left behind");
    }

    /*
     * Moved off that pitch it does change, and changes in the direction
     * claimed: the contour reaches further.  Measured out of the audio.
     */
    {
        sink off160 = {0}, off300 = {0}, on160 = {0}, on300 = {0};
        int eoff160, eoff300, eon160, eon300;
        const unsigned NOC = TVTTS_EXT_ALL & ~TVTTS_EXT_CONTOUR;

        say_at(es, pedro, 160, NOC, TXT, &off160);
        say_at(es, pedro, 300, NOC, TXT, &off300);
        say_at(es, pedro, 160, TVTTS_EXT_ALL, TXT, &on160);
        say_at(es, pedro, 300, TVTTS_EXT_ALL, TXT, &on300);

        eoff160 = excursion(&off160, 160);
        eoff300 = excursion(&off300, 300);
        eon160 = excursion(&on160, 160);
        eon300 = excursion(&on300, 300);
        printf("    [contour, parts per thousand of period: "
               "off %d->%d, on %d->%d, pitch 160->300]\n",
               eoff160, eoff300, eon160, eon300);

        check(!same(&off300, &on300),
              "above its own pitch the extension changes the audio");
        check(eoff160 > 0 && eon300 > 0, "both renderings have a contour");
        /* the bug: the original's excursion shrinks as the pitch climbs */
        check(eoff300 * 4 < eoff160 * 3,
              "without it the contour collapses as the pitch rises");
        /*
         * The fix: it keeps a much larger share of its range.  Not all of it,
         * and the reason is worth knowing -- at 300 the widened contour now
         * reaches the 0x1f4 ceiling and has its peaks taken off, which is the
         * same limit the English engine runs into and not a failure of the
         * scaling.  Below the ceiling the ratio is held exactly, since the
         * excursion and the pitch are multiplied by the same number.
         */
        check(eon300 * eoff160 > eoff300 * eon160,
              "with it far more of the range survives the climb");
        check(eon300 > eoff300 * 2,
              "and at a raised pitch it reaches more than twice as far");

        sink_free(&off160);
        sink_free(&off300);
        sink_free(&on160);
        sink_free(&on300);
    }

    tvtts_set_extensions(TVTTS_EXT_ALL);
}

static void test_pitch_ceiling(void)
{
    static const char *const TXT = "Hola, buenos dias. Que tal?";
    tvtts_synth *s;
    sink lo = {0}, mid = {0}, hi = {0}, flat1 = {0}, flat2 = {0};
    const char *es = NULL;
    int i, josefa = -1;

    for (i = 0; i < tvtts_language_count(); i++)
        if (strcmp(tvtts_language(i), "es") == 0)
            es = tvtts_language(i);
    if (es == NULL)
        return;                 /* no Spanish in this build */
    /* Voices are numbered across the languages, so Josefa is not voice 8 but
     * the ninth Spanish one; found rather than counted, so that adding a
     * language ahead of Spanish would not quietly test Wanda instead. */
    for (i = 0; i < tvtts_voice_count(); i++)
        if (strcmp(tvtts_voice_language(i), "es") == 0 &&
            strcmp(tvtts_voice_name(i), "Josefa") == 0)
            josefa = i;
    check(josefa >= 0, "Josefa is one of the voices");
    if (josefa < 0)
        return;

    /* With the original's ceiling, two pitches above it give the same audio:
     * the contour is flat at the ceiling either way. */
    tvtts_set_extensions(0);
    s = tvtts_create_lang(11025, es);
    tvtts_set_pitch(s, 260);
    say(s, TXT, &flat1);
    tvtts_set_pitch(s, 400);
    say(s, TXT, &flat2);
    check(same(&flat1, &flat2),
          "the 1995 ceiling makes two pitches above it sound the same");
    tvtts_destroy(s);

    /* With the extension they differ, and a pitch under the old ceiling is
     * untouched -- the flag lifts a limit rather than changing the voice. */
    tvtts_set_extensions(TVTTS_EXT_ALL);
    s = tvtts_create_lang(11025, es);
    tvtts_set_pitch(s, 100);
    say(s, TXT, &lo);
    tvtts_set_pitch(s, 260);
    say(s, TXT, &mid);
    tvtts_set_pitch(s, 400);
    say(s, TXT, &hi);
    check(!same(&mid, &hi), "with TVTTS_EXT_PITCH they do not");
    check(!same(&lo, &mid), "and the range below is still a range");
    tvtts_destroy(s);

    /*
     * A pitch the old ceiling never reached is the same either way, which is
     * what says the flag lifts a limit and changes nothing else.
     *
     * TVTTS_EXT_PITCH on its own, not the whole mask: TVTTS_EXT_CONTOUR is
     * also about this engine's pitch and does change the audio at 100, since
     * 100 is not the voice's own pitch.  test_contour checks that one.
     */
    {
        sink a = {0}, b = {0};

        tvtts_set_extensions(0);
        s = tvtts_create_lang(11025, es);
        tvtts_set_pitch(s, 100);
        say(s, TXT, &a);
        tvtts_destroy(s);
        tvtts_set_extensions(TVTTS_EXT_PITCH);
        s = tvtts_create_lang(11025, es);
        tvtts_set_pitch(s, 100);
        say(s, TXT, &b);
        tvtts_destroy(s);
        check(same(&a, &b),
              "and below the old ceiling the extension changes nothing");
        sink_free(&a);
        sink_free(&b);
    }

    /*
     * Josefa, the voice the ceiling silenced.  Her own pitch is 208, above the
     * 1995 ceiling, so with the original she sounds the same as any higher pitch
     * would -- her contour has nowhere to move.
     *
     * A voice number does not carry its pitch: the engine keeps a default per
     * voice and the caller applies it, which is what tvtts_voice_pitch is for and
     * what the NVDA driver does.  So the pitch is set here too, or this would be
     * testing Pedro's 85 under Josefa's name.
     */
    {
        sink j1 = {0}, j2 = {0}, j3 = {0};

        check(tvtts_voice_pitch(josefa) > 200,
              "Josefa's pitch is above the 1995 ceiling");

        tvtts_set_extensions(0);
        s = tvtts_create_lang(11025, es);
        tvtts_set_voice(s, josefa);
        tvtts_set_pitch(s, tvtts_voice_pitch(josefa));
        say(s, TXT, &j1);
        tvtts_set_pitch(s, 300);
        say(s, TXT, &j2);
        check(same(&j1, &j2),
              "so the 1995 engine gives her the same audio as a higher pitch");
        tvtts_destroy(s);

        tvtts_set_extensions(TVTTS_EXT_ALL);
        s = tvtts_create_lang(11025, es);
        tvtts_set_voice(s, josefa);
        tvtts_set_pitch(s, tvtts_voice_pitch(josefa));
        say(s, TXT, &j3);
        check(!same(&j1, &j3), "and the extension gives her a contour again");
        tvtts_destroy(s);
        sink_free(&j1);
        sink_free(&j2);
        sink_free(&j3);
    }

    sink_free(&lo);
    sink_free(&mid);
    sink_free(&hi);
    sink_free(&flat1);
    sink_free(&flat2);
    tvtts_set_extensions(TVTTS_EXT_ALL);
}

static void test_voices(void)
{
    int n = tvtts_voice_count(), i, distinct = 1, named = 1;
    sink a, b;
    tvtts_synth *s;

    /* The decompiled languages retain their ten stock voices. Japanese has
     * its own, smaller roster; test each language instead of a pooled count
     * that could hide an empty roster behind another language's extras. */
    {
        int lang, ok = 1;
        for (lang = 0; lang < tvtts_language_count(); lang++) {
            const char *code = tvtts_language(lang);
            int count = 0;
            for (i = 0; i < n; i++) {
                const char *vl = tvtts_voice_language(i);
                if (vl != NULL && strcmp(vl, code) == 0)
                    count++;
            }
            if (count < ((!strcmp(code, "en") || !strcmp(code, "es")) ? 10 : 1))
                ok = 0;
        }
        check(ok, "each language has voices; decompiled stock rosters retained");
    }
    for (i = 0; i < n; i++) {
        const char *nm = tvtts_voice_name(i);
        int j;

        if (nm == NULL || nm[0] == 0)
            named = 0;
        for (j = 0; j < i; j++)
            if (nm && tvtts_voice_name(j) && !strcmp(nm, tvtts_voice_name(j)))
                distinct = 0;
    }
    check(named, "every voice has a name");
    check(distinct, "the names are distinct");
    {
        /* The order the engine registers them in, which is not the order the
         * strings sit in memory.  Peter and Grandpa Amos land in the same
         * place under either order, so checking only those would have missed
         * the whole thing -- and did. */
        static const char *const want_en[10] = {
            "Peter", "Sidney", "Eager Eddie", "Deep Douglas", "Biff",
            "Grandpa Amos", "Melvin", "Alex", "Wanda", "Julia"
        };
        /* Spanish's, in the order 0x1000830e registers them, which is likewise
         * not the order they sit in memory.  Ezequiel is the 120 wpm voice,
         * which is the slot English gives Grandpa Amos. */
        static const char *const want_es[10] = {
            "Pedro", "Jorge", "Ricardo", "Paco", "Luis",
            "Ezequiel", "Rogelio", "Carlos", "Josefa", "Isabel"
        };
        int ok = 1;

        for (i = 0; i < 10; i++)
            if (tvtts_voice_name(i) == NULL ||
                strcmp(tvtts_voice_name(i), want_en[i]) != 0)
                ok = 0;
        check(ok, "the English voices are named in the engine's own order");
        if (tvtts_language_count() > 1) {
            /* Where Spanish starts is found rather than assumed: it was 10
             * until Kit was added to English ahead of it. */
            int es_base = -1;

            for (i = 0; i < n && es_base < 0; i++)
                if (strcmp(tvtts_voice_language(i), "es") == 0)
                    es_base = i;
            check(es_base >= 10, "the Spanish voices come after the English");
            ok = es_base >= 0;
            for (i = 0; ok && i < 10; i++)
                if (tvtts_voice_name(es_base + i) == NULL ||
                    strcmp(tvtts_voice_name(es_base + i), want_es[i]) != 0)
                    ok = 0;
            check(ok, "and so are the Spanish ones");
        }
    }
    check(tvtts_voice_name(-1) == NULL && tvtts_voice_name(n) == NULL,
          "out-of-range voices give no name");

    /* Every voice says which language it speaks, and the languages are in
     * voice order, so a voice number keeps its meaning when one is added. */
    {
        int ok = 1, j;

        for (i = 0; i < n; i++) {
            const char *lg = tvtts_voice_language(i);
            int found = 0;

            for (j = 0; j < tvtts_language_count(); j++)
                if (lg != NULL && strcmp(lg, tvtts_language(j)) == 0)
                    found = 1;
            if (!found)
                ok = 0;
        }
        check(ok, "every voice names a language the library has");
        check(tvtts_voice_language(-1) == NULL &&
              tvtts_voice_language(n) == NULL,
              "out-of-range voices name no language");
    }

    s = tvtts_create(11025);
    say(s, "Testing.", &a);
    tvtts_set_voice(s, 4);
    say(s, "Testing.", &b);
    check(!same(&a, &b), "changing the voice changes the audio");
    sink_free(&b);

    tvtts_set_voice(s, 0);
    tvtts_set_rate(s, 250);
    say(s, "Testing.", &b);
    check(b.n < a.n, "a higher rate makes it shorter");
    tvtts_destroy(s);
    sink_free(&a); sink_free(&b);
}

static void test_edges(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink out;
    char buf[8];

    check(tvtts_create(44100) == NULL, "an unsupported rate is refused");
    check(tvtts_speak_utf8(NULL, "x", on_event, NULL) < 0,
          "speaking through a null synth is refused");
    memset(&out, 0, sizeof out);
    tvtts_speak_bytes(s, "", 0, on_event, &out);
    check(out.ended, "empty text still ends cleanly");
    sink_free(&out);

    say(s, "Still here.", &out);
    check(out.n > 0, "the synth still works after empty text");
    sink_free(&out);

    check(tvtts_mark_sequence(buf, 2, 5) == 0, "a short mark buffer is refused");
    check(tvtts_mark_sequence(buf, sizeof buf, 5) == 4, "a mark is four bytes");
    tvtts_destroy(s);
    tvtts_destroy(NULL);
}

static void test_phonemes(void)
{
    tvtts_synth *s = tvtts_create(11025);
    char buf[256], small[4];
    sink spoken, viaphon;
    int n;

    n = tvtts_text_to_phonemes(s, "hello", buf, sizeof buf);
    check(n > 0, "text converts to phonemes");
    check(n == (int)strlen(buf) + 1, "the return counts the terminator");
    check(strcmp(buf, "&HeLO1.") == 0, "and is the engine's own alphabet");
    if (strcmp(buf, "&HeLO1.") != 0)
        printf("     got: [%s]\n", buf);

    /* snprintf-style: NULL asks the size, a short buffer truncates but
     * still reports what was wanted. */
    check(tvtts_text_to_phonemes(s, "hello", NULL, 0) == n,
          "a null buffer just measures");
    check(tvtts_text_to_phonemes(s, "hello", small, sizeof small) == n,
          "a short buffer still reports the full size");
    check(strlen(small) == sizeof small - 1, "and is truncated, not overrun");

    /* The round trip: speaking the phonemes matches speaking the word. */
    say(s, "hello", &spoken);
    memset(&viaphon, 0, sizeof viaphon);
    tvtts_speak_phonemes(s, "HeLO1", on_event, &viaphon);
    check(viaphon.n > 0, "phonemes produce audio");
    check(same(&spoken, &viaphon), "and say the same thing, byte for byte");

    check(tvtts_speak_phonemes(NULL, "HeLO1", on_event, NULL) < 0,
          "a null synth is refused");
    check(tvtts_text_to_phonemes(s, NULL, buf, sizeof buf) < 0,
          "so is null text");

    /* Collecting the trace must not disturb the synth. */
    sink_free(&spoken);
    say(s, "hello", &spoken);
    tvtts_text_to_phonemes(s, "something else entirely", buf, sizeof buf);
    sink_free(&viaphon);
    say(s, "hello", &viaphon);
    check(same(&spoken, &viaphon), "a conversion leaves the synth as it was");

    tvtts_destroy(s);
    sink_free(&spoken); sink_free(&viaphon);
}

/* The first OpenTV extension: rate above the original's 26-row table.
 * What matters is that it speeds speech up where the original produced
 * nonsense, and that it changes nothing at or below 253 wpm. */
static void test_rate_extension(void)
{
    tvtts_synth *s;
    sink slow, fast, classic, a, b;
    int r;

    /*
     * TVTTS_EXT_DEFAULT and not TVTTS_EXT_ALL: the Japanese romaji reading is
     * a CHOICE rather than a fix, and its other side is the one three
     * shipping Japanese systems make, so it is the one extension that is off
     * until a caller asks.  See TVTTS_EXT_JA_ROMAJI.
     */
    check(tvtts_get_extensions() == TVTTS_EXT_DEFAULT,
          "extensions are on by default, except the one that is a choice");

    /* Below the original's ceiling nothing may change. */
    for (r = 46; r <= 253; r += 23) {
        tvtts_set_extensions(TVTTS_EXT_ALL);
        s = tvtts_create(11025);
        tvtts_set_rate(s, r);
        say(s, TEXT, &a);
        tvtts_destroy(s);

        tvtts_set_extensions(0);
        s = tvtts_create(11025);
        tvtts_set_rate(s, r);
        say(s, TEXT, &b);
        tvtts_destroy(s);

        if (!same(&a, &b))
            break;
        sink_free(&a); sink_free(&b);
    }
    check(r > 253, "46..253 wpm is untouched by the extension");
    if (r <= 253)
        printf("     first difference at %d wpm\n", r);
    sink_free(&a); sink_free(&b);

    tvtts_set_extensions(TVTTS_EXT_ALL);
    s = tvtts_create(11025);
    tvtts_set_rate(s, 253);
    say(s, TEXT, &slow);
    tvtts_set_rate(s, 400);
    say(s, TEXT, &fast);
    tvtts_destroy(s);
    check(fast.n < slow.n, "400 wpm is shorter than 253, not longer");
    /* 1.4x rather than the 1.8x uniform compression used to give.  The
     * difference is deliberate: squeezing every phoneme alike went straight
     * through the per-phoneme minimum durations and made fast speech
     * mumble, so the minimums now keep most of their length and the time
     * comes out of the steady parts instead. */
    check(slow.n * 100 / fast.n >= 140,
          "and appreciably so -- at least 1.4x");

    /* Which is precisely what the original got wrong. */
    tvtts_set_extensions(0);
    s = tvtts_create(11025);
    tvtts_set_rate(s, 400);
    say(s, TEXT, &classic);
    tvtts_destroy(s);
    check(classic.n > fast.n,
          "the original reads off the table and is slower at 400");

    /* Past the last added row it holds rather than running away again. */
    tvtts_set_extensions(TVTTS_EXT_ALL);
    s = tvtts_create(11025);
    tvtts_set_rate(s, 5000);
    say(s, TEXT, &a);
    tvtts_destroy(s);
    check(a.n == fast.n, "an absurd rate clamps to the fastest row");

    sink_free(&slow); sink_free(&fast); sink_free(&classic); sink_free(&a);
    tvtts_set_extensions(TVTTS_EXT_ALL);
}

/* Three output rates.  8 kHz and 11.025 are the original's; 22.05 is
 * OpenTV's, built from resonator tables computed rather than lifted. */
static void test_sample_rate(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink a, b;

    check(tvtts_sample_rate_hz(TVTTS_SR_8K) == 8000 &&
          tvtts_sample_rate_hz(TVTTS_SR_11K) == 11025 &&
          tvtts_sample_rate_hz(TVTTS_SR_16K) == 16000, "three rates");
    check(tvtts_sample_rate_hz(-1) == 0 && tvtts_sample_rate_hz(3) == 0,
          "and nothing else");
    check(tvtts_get_sample_rate(s) == TVTTS_SR_11K, "11 kHz by default");

    say(s, TEXT, &a);
    check(tvtts_set_sample_rate(s, TVTTS_SR_16K) == 0, "16 kHz is accepted");
    check(tvtts_get_sample_rate(s) == TVTTS_SR_16K, "and reported back");
    say(s, TEXT, &b);
    /* Twice the rate, same speech: about twice the samples and no more. */
    /* 16000/11025 = 1.45, so the same words at the same speed. */
    check(b.n > a.n * 14 / 10 && b.n < a.n * 15 / 10,
          "16 kHz gives 1.45x the samples for the same words");

    check(tvtts_set_sample_rate(s, TVTTS_SR_11K) == 0, "switching back works");
    sink_free(&b);
    say(s, TEXT, &b);
    check(same(&a, &b), "and returns byte for byte to where it was");

    check(tvtts_set_sample_rate(s, 3) < 0 && tvtts_set_sample_rate(s, -1) < 0,
          "an unknown rate is refused");
    check(tvtts_set_sample_rate(NULL, TVTTS_SR_8K) < 0, "so is a null synth");

    /*
     * The decompiled languages have all three rates. The original offered two and 16 kHz
     * is OpenTV's, computed from the formulas that reproduce both of the
     * original's sets exactly -- which they do for the 1995 engines as well as
     * the 1997 one, all 2,120 values, the two being byte-identical here -- so
     * the extension is not English's alone.  A rate that were quietly refused
     * would leave the setting doing nothing, which is worse than failing.
     */
    {
        int lang, which;

        for (lang = 0; lang < tvtts_language_count(); lang++) {
            const char *code = tvtts_language(lang);
            char what[96];

            for (which = 0; which < 3; which++) {
                tvtts_synth *t = tvtts_create_lang(11025, code);
                sink c = {0}, d = {0};
                int ok;

                /* Japanese's current frontend explicitly refuses 8 kHz.
                 * Refusal must leave a working 11 kHz synth unchanged. */
                if (!strcmp(code, "ja") && which == TVTTS_SR_8K) {
                    tvtts_synth *unsupported = tvtts_create_lang(8000, code);
                    check(unsupported == NULL, "Japanese refuses creation at 8000 Hz");
                    tvtts_destroy(unsupported);
                    say(t, "kakikukeko", &c);
                    check(tvtts_set_sample_rate(t, which) < 0,
                          "Japanese refuses switching to 8000 Hz");
                    say(t, "kakikukeko", &d);
                    check(tvtts_get_sample_rate(t) == TVTTS_SR_11K && same(&c, &d),
                          "refused Japanese rate change preserves settings and audio");
                    sink_free(&c);
                    sink_free(&d);
                    tvtts_destroy(t);
                    continue;
                }

                _snprintf(what, sizeof what, "%s speaks at %u Hz",
                          tvtts_language_name(code),
                          tvtts_sample_rate_hz(which));
                what[sizeof what - 1] = 0;
                say(t, TEXT, &c);
                ok = tvtts_set_sample_rate(t, which) == 0;
                if (ok) {
                    say(t, TEXT, &d);
                    ok = d.n > 0;
                }
                check(ok, what);
                /* The same words take the same time, so the samples go with
                 * the rate and nothing else. */
                if (ok && which != TVTTS_SR_11K) {
                    double want = (double)c.n *
                        (double)tvtts_sample_rate_hz(which) / 11025.0;
                    _snprintf(what, sizeof what,
                              "and at %u Hz takes the same time to say it",
                              tvtts_sample_rate_hz(which));
                    what[sizeof what - 1] = 0;
                    check((double)d.n > want * 0.99 &&
                          (double)d.n < want * 1.01, what);
                }
                sink_free(&c);
                sink_free(&d);
                tvtts_destroy(t);
            }
        }
    }
    tvtts_destroy(s);

    /* Changing rate re-initialises the engine, which puts its own defaults
     * back: voice 0, pitch 85, 150 wpm, full volume.  Everything the caller
     * asked for has to survive that.  Wanda came back as Peter once. */
    {
        sink here, there, back;
        tvtts_synth *t = tvtts_create(11025);

        tvtts_set_voice(t, 8);            /* Wanda */
        tvtts_set_pitch(t, 133);
        tvtts_set_rate(t, 190);
        say(t, TEXT, &here);

        tvtts_set_sample_rate(t, TVTTS_SR_16K);
        check(tvtts_get_voice(t) == 8, "the voice survives a rate change");
        check(tvtts_get_pitch(t) == 133, "so does the pitch");
        check(tvtts_get_rate(t) == 190, "and the rate");
        say(t, TEXT, &there);

        tvtts_set_sample_rate(t, TVTTS_SR_11K);
        say(t, TEXT, &back);
        check(same(&here, &back),
              "and the audio is identical going out and back again");

        /* The giveaway if the settings had silently reset: the engine's own
         * defaults would make a different, shorter utterance at 16 kHz. */
        check(there.n > here.n * 14 / 10 && there.n < here.n * 15 / 10,
              "16 kHz of the same voice is 1.45x the samples, not a reset one");
        tvtts_destroy(t);
        sink_free(&here); sink_free(&there); sink_free(&back);
    }

    s = tvtts_create(16000);
    check(s != NULL, "16000 can be asked for at creation too");
    tvtts_destroy(s);
    sink_free(&a); sink_free(&b);
}

/* Bandwidth widening at high rates, after TGSpeechBox.  It must be audible
 * where it applies, silent everywhere the original could reach, and never
 * change how long anything takes -- it is a timbre change, not a timing one. */
static void test_clarity(void)
{
    tvtts_synth *s;
    sink off, on;
    int r;

    check((tvtts_get_extensions() & TVTTS_EXT_CLARITY) != 0,
          "clarity is on by default");

    /* Nothing the original could reach may move.  The two halves of this
     * extension start at different places -- the pitch range narrows from
     * the first added row, the bandwidths widen from about 310 wpm -- but
     * both leave everything up to 253 exactly as it was. */
    for (r = 46; r <= TVTTS_RATE_MAX; r += 23) {
        tvtts_set_extensions(TVTTS_EXT_RATE);
        s = tvtts_create(11025); tvtts_set_rate(s, r); say(s, TEXT, &off);
        tvtts_destroy(s);
        tvtts_set_extensions(TVTTS_EXT_ALL);
        s = tvtts_create(11025); tvtts_set_rate(s, r); say(s, TEXT, &on);
        tvtts_destroy(s);
        if (!same(&off, &on))
            break;
        sink_free(&off); sink_free(&on);
    }
    check(r > TVTTS_RATE_MAX,
          "clarity leaves the whole of the original's range alone");
    if (r <= TVTTS_RATE_MAX)
        printf("     first difference at %d wpm\n", r);
    sink_free(&off); sink_free(&on);

    /* At the top of the range it must do something, without retiming. */
    tvtts_set_extensions(TVTTS_EXT_RATE);
    s = tvtts_create(11025); tvtts_set_rate(s, 400); say(s, TEXT, &off);
    tvtts_destroy(s);
    tvtts_set_extensions(TVTTS_EXT_ALL);
    s = tvtts_create(11025); tvtts_set_rate(s, 400); say(s, TEXT, &on);
    tvtts_destroy(s);
    check(!same(&off, &on), "and does change the sound at 400 wpm");
    check(off.n == on.n, "without altering the duration by a single sample");
    sink_free(&off); sink_free(&on);

    /* The flags are independent. */
    tvtts_set_extensions(TVTTS_EXT_CLARITY);
    check(tvtts_get_extensions() == TVTTS_EXT_CLARITY,
          "clarity can be had without the rate rows");
    tvtts_set_extensions(TVTTS_EXT_ALL);
}

/*
 * Singing.  The score compiler is pure, so most of this needs no audio: what
 * it produces is checked directly, and only the pitch of a sung note is
 * measured out of the sound.
 */
/*
 * `[:phone TruVoice on|off]`: the command in the text, against the phoneme
 * entry point it stands for.  Reachable from the API, from `tv` and from the
 * speak window; NVDA and the SAPI driver strip inline commands before the
 * text arrives, so neither route sees it.
 */
/*
 * A score sets the engine's pitch with ESC[<n>p, and nothing puts it back:
 * Engine_Reset does not touch self->pitch, and the port only re-applies pitch
 * when it differs from what it last sent.  So speech after a song has to be
 * checked, not assumed.
 */
static void test_pitch_after_song(void)
{
    tvtts_synth *s1 = tvtts_create(11025);
    tvtts_synth *s2 = tvtts_create(11025);
    sink sung, after, fresh;
    int fa, ff;

    memset(&sung, 0, sizeof sung);
    tvtts_sing(s1, "A<300,25>", on_event, &sung);   /* C4, well above speech */
    sink_free(&sung);

    say(s1, "hello there, this is a test", &after);
    say(s2, "hello there, this is a test", &fresh);

    fa = sung_hz(&after);
    ff = sung_hz(&fresh);
    printf("     [after a song %d Hz, on a fresh synth %d Hz]\n", fa, ff);
    check(fa > 0 && ff > 0, "both utterances have a pitch to measure");
    check(fa < ff * 5 / 4, "speech after a song is not pitched up by the song");

    tvtts_destroy(s1); tvtts_destroy(s2);
    sink_free(&after); sink_free(&fresh);
}

/*
 * Camel case read as the words it is made of.  The engine has no notion of it --
 * a capital inside a word changes nothing in the letter-to-sound rules -- so
 * this is the library splitting the text, and the test is that a split word says
 * what the same words with a space say.
 */
static void test_camel_case(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink a, b;
    int i;
    static const struct { const char *joined, *spaced; } cases[] = {
        { "CamelCase",  "Camel Case" },
        { "helloWorld", "hello World" },
        { "HTMLParser", "HTML Parser" },   /* not H T M L Parser */
        { "Camel2Case", "Camel2 Case" },
        { "HTML",       "HTML" },          /* all capitals stays whole */
        { "CAMELCASE",  "CAMELCASE" },
        { "camelcase",  "camelcase" },
    };

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        say(s, cases[i].joined, &a);
        say(s, cases[i].spaced, &b);
        if (!same(&a, &b))
            printf("     [%s did not match %s]\n",
                   cases[i].joined, cases[i].spaced);
        check(same(&a, &b), cases[i].joined);
        sink_free(&a); sink_free(&b);
    }

    /* Gated, and with it off a capital inside a word means nothing again. */
    tvtts_set_extensions(TVTTS_EXT_ALL & ~TVTTS_EXT_CAMEL);
    say(s, "CamelCase", &a);
    say(s, "camelcase", &b);
    tvtts_set_extensions(TVTTS_EXT_ALL);
    check(same(&a, &b), "with the extension off it is one word again");
    sink_free(&a); sink_free(&b);

    /*
     * Phonemes and scores are text to nobody: a capital names a different
     * phoneme there, so neither entry may be split.
     */
    memset(&a, 0, sizeof a);
    tvtts_speak_phonemes(s, "HeLO", on_event, &a);
    tvtts_set_extensions(TVTTS_EXT_ALL & ~TVTTS_EXT_CAMEL);
    memset(&b, 0, sizeof b);
    tvtts_speak_phonemes(s, "HeLO", on_event, &b);
    tvtts_set_extensions(TVTTS_EXT_ALL);
    check(same(&a, &b), "phonemes are never split");
    sink_free(&a); sink_free(&b);

    memset(&a, 0, sizeof a);
    tvtts_sing(s, "H<100,20>e<600,20>L<100,20>O<400,20>", on_event, &a);
    tvtts_set_extensions(TVTTS_EXT_ALL & ~TVTTS_EXT_CAMEL);
    memset(&b, 0, sizeof b);
    tvtts_sing(s, "H<100,20>e<600,20>L<100,20>O<400,20>", on_event, &b);
    tvtts_set_extensions(TVTTS_EXT_ALL);
    check(same(&a, &b), "and neither is a score");
    sink_free(&a); sink_free(&b);

    tvtts_destroy(s);
}

static void test_phone_command(void)
{
    tvtts_synth *s = tvtts_create(11025);
    sink viacmd, viaapi, a, b;

    memset(&viaapi, 0, sizeof viaapi);
    tvtts_speak_phonemes(s, "HeLO", on_event, &viaapi);

    say(s, "[:phone TruVoice on]HeLO[:phone TruVoice off]", &viacmd);
    check(viacmd.n > 0, "the command produces audio");
    check(same(&viacmd, &viaapi),
          "and says what tvtts_speak_phonemes says, byte for byte");

    /* Case and spacing are not the point of the command. */
    say(s, "[: PHONE   truvoice   ON ]HeLO[:phone TruVoice off]", &a);
    check(same(&a, &viaapi), "case and spacing do not matter");
    sink_free(&a);

    /*
     * Anything that is not the command is text and has to come through
     * untouched, which is strongest said this way: with the rewrite off, the
     * same text must render the same.  A fresh synth either side, because an
     * utterance in phoneme mode colours the first word of the next one -- see
     * docs/SINGING.md, which is a separate fault and not this one.
     */
    {
        tvtts_synth *f1, *f2;

        f1 = tvtts_create(11025);
        say(f1, "a [b] c, and [:phone arpa] as well", &a);
        tvtts_destroy(f1);
        tvtts_set_extensions(0);
        f2 = tvtts_create(11025);
        say(f2, "a [b] c, and [:phone arpa] as well", &b);
        tvtts_destroy(f2);
        tvtts_set_extensions(TVTTS_EXT_ALL);
    }
    check(same(&a, &b), "text that is not the command is left byte for byte");
    sink_free(&a); sink_free(&b);

    /* Off again hands what follows back to the letter rules. */
    say(s, "[:phone TruVoice on]HeLO[:phone TruVoice off]hello", &a);
    say(s, "[:phone TruVoice on]HeLO[:phone TruVoice off]", &b);
    check(a.n > b.n, "off ends phoneme input");
    sink_free(&a); sink_free(&b);

    /* Gated, like every other extension. */
    tvtts_set_extensions(0);
    say(s, "[:phone TruVoice on]HeLO[:phone TruVoice off]", &a);
    tvtts_set_extensions(TVTTS_EXT_ALL);
    check(!same(&a, &viaapi), "with the extensions off it is only text");
    sink_free(&a);

    tvtts_destroy(s);
    sink_free(&viacmd); sink_free(&viaapi);
}


static void test_singing(void)
{
    char buf[512];
    int n;

    printf("%csinging%c", 10, 10);

    /* The note table is DECtalk's: 1..37 chromatic from C2, 38 up is hertz. */
    check(tvtts_note_hz(1) == 65, "note 1 is low C at 65 Hz");
    check(tvtts_note_hz(22) == 220, "note 22 is A3 at 220 Hz");
    check(tvtts_note_hz(34) == 440, "note 34 is A4 at 440 Hz");
    check(tvtts_note_hz(37) == 523, "note 37 is C5, the top of the scale");
    /* Within the rounding: C2 is 65.41 and rounds down, C3 is 130.81 and
     * rounds up, so the whole-hertz table is an octave plus one. */
    check(tvtts_note_hz(13) - 2 * tvtts_note_hz(1) == 1 &&
          tvtts_note_hz(25) - 2 * tvtts_note_hz(13) == 0,
          "twelve semitones up is an octave, to the rounding");
    check(tvtts_note_hz(200) == 200, "200 is past the scale, so it is hertz");
    check(tvtts_note_hz(0) == 0, "0 is a rest");

    /* The compiler reports what it wants the way snprintf does. */
    n = tvtts_sing_compile("A<600,20>", NULL, 0);
    check(n > 0, "compiling with no buffer still says how much is needed");
    check(tvtts_sing_compile("A<600,20>", buf, sizeof buf) == n,
          "and writing it wants exactly as much");
    check((int)strlen(buf) == n - 1, "the count includes the terminator");

    /* A note becomes a pitch escape, a duration escape and its phoneme. */
    /*
     * A pitch escape is emitted, and a higher note emits a larger one.  The
     * exact number is deliberately not asserted: the compiler corrects for the
     * engine singing flat, so pinning it here would only re-state the
     * correction.  What the note is actually sung at is measured below, out of
     * the audio, which is the check that matters.
     */
    check(strstr(buf, "\x1b[") != NULL && strchr(buf, 'p') != NULL,
          "a note compiles to a pitch escape");
    {
        char lo[256], hi[256];

        tvtts_sing_compile("A<600,8>", lo, sizeof lo);
        tvtts_sing_compile("A<600,20>", hi, sizeof hi);
        check(pitch_esc(hi) > pitch_esc(lo) && pitch_esc(lo) > 0,
              "and a higher note compiles to a higher one");
    }
    check(strstr(buf, "\x1b[1I") != NULL && strstr(buf, "\x1b[0I") != NULL,
          "and the score is wrapped in phoneme mode");

    /* 600 ms fits one phoneme; 1500 does not, and is sung as repeats. */
    tvtts_sing_compile("A<600,20>", buf, sizeof buf);
    check(count_char(buf, 'A') == 1, "a 600 ms note is one phoneme");
    /*
     * A long note is one phoneme and then a hold.  Singing the remainder as
     * more of the same phoneme re-articulates it -- the engine treats every
     * pair of phonemes as a boundary -- and suppressing that needs a frame to
     * hold, which no rule picked reliably across phonemes.  See
     * docs/SINGING.md.
     */
    tvtts_sing_compile("A<1500,20>", buf, sizeof buf);
    check(count_char(buf, 'A') == 1,
          "a 1500 ms note is still one phoneme");
    check(strchr(buf, 'g') != NULL,
          "and the rest of it is held, not sung again");

    /*
     * A rest with no length of its own takes the default one, which is what the
     * demo's own `_<0,100>` asks for: a zero duration means "not specified", so
     * the rest is the length the rules would give it rather than nothing.  An
     * explicit length is still exactly itself.
     */
    tvtts_sing_compile("A<400,20>_<0,100>A<400,25>", buf, sizeof buf);
    check(strstr(buf, "[10s") != NULL,
          "a rest with no length given is the default 100 ms");
    tvtts_sing_compile("A<400,20>_<300,0>A<400,25>", buf, sizeof buf);
    check(strstr(buf, "[30s") != NULL,
          "and an explicit length is itself");
    tvtts_sing_compile("A<400,20>A<0,20>A<400,25>", buf, sizeof buf);
    check(count_char(buf, 'A') == 2,
          "a zero length on a phoneme is still nothing");

    /* A phoneme with no note keeps its ordinary length. */
    tvtts_sing_compile("HeLO", buf, sizeof buf);
    check(strstr(buf, "\x1b[98p") == NULL, "a score with no notes sets no pitch");

    /* And the sung pitch is the note, not the note plus an intonation
     * contour -- which is what it was before the contour was gated. */
    {
        sink lo = {0}, hi = {0};
        int flo, fhi;

        sing_at("A<600,8>A<600,8>A<600,8>", &lo);
        sing_at("A<600,20>A<600,20>A<600,20>", &hi);
        flo = sung_hz(&lo);
        fhi = sung_hz(&hi);
        printf("    [sung: note 8 wants 98 Hz got %d, note 20 wants 196 got %d]\n",
               flo, fhi);
        check(flo > 88 && flo < 108, "note 8 sings within a semitone of 98 Hz");
        check(fhi > 186 && fhi < 206, "note 20 sings within a semitone of 196 Hz");
        sink_free(&lo);
        sink_free(&hi);
    }
}

/*
 * The bare formant-frame synthesiser: set the tracks by hand, then hold them.
 *
 * docs/VOICES.md documents this and it is how a parameter frame is driven
 * without going through a phoneme at all.  TVTTS_EXT_SING changes what `g`
 * freezes -- the sung note rather than the configured defaults -- and a hold
 * with nothing sounding in front of it has no note to freeze, so for a while it
 * rendered **silence**.  The corpus cannot catch that, because difftest runs
 * with the extensions off and only ever sees the original arm.
 */
static void test_bare_hold(void)
{
    /* The three lines docs/VOICES.md prints, which it says render three
     * different waveforms. */
    static const char *const LINES[3] = {
        "\x1b[0;60l\x1b[9;100l\x1b[100g",
        "\x1b[0;60l\x1b[9;200l\x1b[100g",
        "\x1b[0;60l\x1b[9;100l\x1b[10;200l\x1b[100g"
    };
    sink off = {0}, on = {0};
    sink w[3];
    int i;

    /* A hold on its own sounds, and sounds the same either way: there is no
     * note to freeze, so the extension must defer to what the engine does. */
    say_at("en", 0, 0, 0, "\x1b[120g", &off);
    say_at("en", 0, 0, TVTTS_EXT_ALL, "\x1b[120g", &on);
    check(off.n > 4000, "a hold with nothing before it renders frames");
    check(peak(&off) > 100, "and they are not silence");
    check(same(&off, &on), "and the extensions leave it alone");
    sink_free(&off);
    sink_free(&on);

    /* The same once the tracks have been placed by hand. */
    say_at("en", 0, 0, 0, "\x1b[9;160l\x1b[120g", &off);
    say_at("en", 0, 0, TVTTS_EXT_ALL, "\x1b[9;160l\x1b[120g", &on);
    check(peak(&on) > 100,
          "a hold after a parameter escape is not silence");
    check(same(&off, &on), "and that is the same either way too");
    sink_free(&off);
    sink_free(&on);

    for (i = 0; i < 3; i++) {
        memset(&w[i], 0, sizeof w[i]);
        say_at("en", 0, 0, TVTTS_EXT_ALL, LINES[i], &w[i]);
    }
    check(!same(&w[0], &w[1]) && !same(&w[1], &w[2]) && !same(&w[0], &w[2]),
          "the three lines in VOICES.md render three different waveforms");
    for (i = 0; i < 3; i++)
        sink_free(&w[i]);
}

/*
 * tvtts_speak_frames: the synthesiser driven a parameter frame at a time.
 *
 * The escape path (test_bare_hold) places the resonators by hand too, but only
 * for a few frames -- past about ten the queue stops delivering what was asked,
 * and a run of bare holds replays the change sequence instead of holding.  This
 * entry point writes the frames where stage 3 would have, so a long move
 * arrives intact and in one continuous signal.
 */
static void test_speak_frames(void)
{
    enum { N = 60 };
    static uint8_t fr[N * TVTTS_FRAME_TRACKS];
    sink out = {0};
    tvtts_synth *s;
    int i, k;

    /* F1 and F2 sweeping from one vowel posture to another across all N. */
    for (i = 0; i < N; i++) {
        uint8_t *f = fr + (size_t)i * TVTTS_FRAME_TRACKS;
        int f1 = 737 + (298 - 737) * i / (N - 1);
        int f2 = 1225 + (2067 - 1225) * i / (N - 1);

        for (k = 0; k < TVTTS_FRAME_TRACKS; k++)
            f[k] = 0;
        f[0] = 60; f[16] = 14; f[17] = 50; f[18] = 16; f[19] = 8;
        f[9]  = (uint8_t)(f1 / 4);
        f[10] = (uint8_t)((f2 - 500) / 8);
        f[11] = 142; f[12] = 207; f[13] = 60; f[14] = 50; f[15] = 70;
    }

    s = tvtts_create(11025);
    memset(&out, 0, sizeof out);
    tvtts_speak_frames(s, fr, N, on_event, &out);
    tvtts_destroy(s);

    check(out.n > (size_t)N * 80, "the frames render about their own length");
    check(peak(&out) > 100, "and are not silence");

    /* The move has to survive to the end: the second half must differ from the
     * first.  The old escape path froze or looped well before here. */
    {
        size_t half = out.n / 2;
        long a = 0, b = 0;
        size_t j;

        for (j = 0; j + 1 < half; j++)
            a += out.pcm[j] > out.pcm[j + 1] ? 1 : 0;
        for (j = half; j + 1 < out.n; j++)
            b += out.pcm[j] > out.pcm[j + 1] ? 1 : 0;
        check(a > 0 && b > 0, "both halves carry signal");
        check(b * 100 > a * 115,
              "the second half is pitched higher in its zero crossings -- "
              "the sweep reached the end rather than freezing");
    }
    sink_free(&out);

    /* A null frame array with a zero count is not a crash, and a foreign
     * language refuses, as the other English-only entry points do. */
    s = tvtts_create_lang(11025, "es");
    check(tvtts_speak_frames(s, fr, N, on_event, NULL) == -1,
          "speak_frames is English only");
    tvtts_destroy(s);
}

int main(void)
{
    test_reuse();
    test_abort();
    test_marks();
    test_text();
    test_voices();
    test_edges();
    test_phonemes();
    test_rate_extension();
    test_sample_rate();
    test_clarity();
    test_pitch_ceiling();
    test_contour();
    test_pitch_floor();
    test_es_rate();
    test_custom_voices();
    test_es_custom_voices();
    test_singing();
    test_camel_case();
    test_phone_command();
    test_pitch_after_song();
    test_bare_hold();
    test_speak_frames();
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
