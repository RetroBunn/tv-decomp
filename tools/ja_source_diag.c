/* Trace the actual input tracks and fixed-point overflows for a source probe.
 * Link with TV_DIAG frame/generate objects and --wrap=Synth_Frame.
 * Usage: ja_source_diag en|ja|frames VOICE RATE TEXT|RAW_FRAME_FILE
 * VOICE is the public API index (engine index for en/frames).
 * TRACE=1 prints input tracks (one F row per Synth_Frame that advanced).
 * No production engine code or synthesis settings are changed by this tool.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tvtts.h"
#include "engine.h"

extern long tv_diag_over[TV_DIAG_STATES], tv_diag_q15_over[5];
extern long tv_diag_syn[10];
void __real_Synth_Frame(Engine *s);
void __wrap_Synth_Frame(Engine *s)
{
    int pos = s->trk_04, i;
    unsigned char p[22];
    for (i = 0; i < 22; i++) p[i] = s->trk_buf[i][pos & 255];
    __real_Synth_Frame(s);
    if (getenv("TRACE") && s->trk_04 != pos) {
        printf("F %d", pos);
        for (i = 0; i < 22; i++) printf(" %u", p[i]);
        printf("\n");
    }
}

static int peak, rails;
static unsigned long samples;
static double energy;
static int TVTTS_CALL audio(const tvtts_event *ev, void *user)
{
    uint32_t i;
    (void)user;
    if (ev->type != TVTTS_AUDIO) return 0;
    for (i = 0; i < ev->count; i++) {
        int v = ev->samples[i], a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        if (v == -32768 || v == 32767) {
            rails++;
            if (getenv("TRACE")) printf("CLIP %lu\n", samples);
        }
        energy += (double)v * v;
        samples++;
    }
    return 0;
}

int main(int argc, char **argv)
{
    tvtts_synth *s;
    int voice, rc, i;
    if (argc != 5) return 2;
    voice = atoi(argv[2]);
    s = tvtts_create_lang((uint32_t)atoi(argv[3]),
                         !strcmp(argv[1], "ja") ? "ja" : "en");
    if (!s) return 2;
    tvtts_set_voice(s, voice);
    tvtts_set_pitch(s, tvtts_voice_pitch(voice));
    if (getenv("WPM")) tvtts_set_rate(s, atoi(getenv("WPM")));
    if (!strcmp(argv[1], "frames")) {
        FILE *f = fopen(argv[4], "rb");
        long n;
        unsigned char *buf;
        if (!f) return 2;
        fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
        if (n <= 0 || n % 22) { fclose(f); return 2; }
        buf = (unsigned char *)malloc((size_t)n);
        if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) return 2;
        fclose(f);
        rc = tvtts_speak_frames(s, buf, (uint32_t)(n / 22), audio, NULL);
        free(buf);
    } else if (!strcmp(argv[1], "en") || !strcmp(argv[1], "ja")) {
        rc = tvtts_speak_utf8(s, argv[4], audio, NULL);
    } else return 2;
    printf("PCM %d %d %lu %.0f\n", peak, rails, samples, energy);
    printf("STATE");
    for (i = 0; i < TV_DIAG_STATES; i++) printf(" %ld", tv_diag_over[i]);
    printf("\nPOLE");
    for (i = 0; i < 5; i++) printf(" %ld", tv_diag_q15_over[i]);
    printf("\nTABLE %ld %ld %ld\n", tv_diag_syn[6], tv_diag_syn[7], tv_diag_syn[8]);
    tvtts_destroy(s);
    return rc;
}
