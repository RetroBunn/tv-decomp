/*
 * OpenTV Speak -- a window for typing text and hearing the engine say it.
 *
 * Plain Win32 in C, deliberately.  Standard Win32 controls carry their own
 * MSAA implementation, so NVDA, JAWS and Narrator read an edit box, a combo
 * box, a slider and a button correctly without this program doing anything
 * for them.  Anything drawn by hand, or any toolkit that draws its own
 * controls, has to reimplement that and usually gets some of it wrong.  The
 * rules this file follows to keep it that way:
 *
 *   - every control is a stock control;
 *   - every control is created immediately after the static that labels it,
 *     because that adjacency in z-order is what MSAA uses for the
 *     accessible name;
 *   - the message loop runs IsDialogMessage, so Tab, Shift+Tab, the arrow
 *     keys and Alt+mnemonics all navigate;
 *   - the font comes from the user's own SPI_GETNONCLIENTMETRICS settings,
 *     so it follows their chosen size rather than a hard-coded one;
 *   - colours are system colours, so high-contrast themes work.
 *
 * Keys: F5 speak, F6 pause or resume, F7 stop, F8 reset to defaults (which
 * says "Reset speech"), Ctrl+S export, Escape stop.
 *
 * Synthesis runs on a worker thread and the samples go to waveOut as they
 * are produced, so speaking starts immediately and a long text does not
 * freeze the window.  Pause and stop are waveOutPause and waveOutReset,
 * which is why pause resumes exactly where it stopped.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"

/* ---- control ids -------------------------------------------------------- */

#define ID_TEXT       101
#define ID_VOICE      102
#define ID_RATE       103
#define ID_RATEVAL    104
#define ID_PITCH      105
#define ID_PITCHVAL   106
#define ID_VOLUME     107
#define ID_VOLVAL     108
#define ID_SRATE      109
#define ID_SPEAK      110
#define ID_PAUSE      111
#define ID_STOP       112
#define ID_RESET      113
#define ID_EXPORT     114
#define ID_STATUS     115

#define WM_APP_DONE   (WM_APP + 1)

/* Defaults the Reset key restores.  Rate and pitch come from the engine's
 * own table for voice 0 rather than being written down here. */
#define DEF_VOICE   0
#define DEF_VOLUME  100
#define DEF_SRATE   TVTTS_SR_11K

/* ---- state -------------------------------------------------------------- */

static HINSTANCE     g_inst;
static HWND          g_main;
static HFONT         g_font;
static tvtts_synth  *g_synth;

/* what the worker will apply before it speaks; the UI thread owns these and
 * the worker only reads them, so the synth itself is touched from one thread */
static int           g_voice = DEF_VOICE;
static int           g_rate  = 150;
static int           g_pitch = 85;
static int           g_vol   = DEF_VOLUME;   /* percent */
static int           g_srate = DEF_SRATE;

enum { ST_IDLE, ST_SPEAKING, ST_PAUSED };
static volatile LONG g_state = ST_IDLE;
static volatile LONG g_abort;
static HANDLE        g_thread;
static CRITICAL_SECTION g_lock;   /* guards g_wave */

/* what the worker was asked to do */
static wchar_t      *g_job_text;
static wchar_t      *g_job_file;  /* non-NULL: write a WAV instead of playing */

/* ---- audio out ---------------------------------------------------------- */

#define NBLK    8
#define BLKSAMP 2048

static HWAVEOUT g_wave;
static HANDLE   g_wave_event;
static WAVEHDR  g_hdr[NBLK];
static short    g_blk[NBLK][BLKSAMP];
static int      g_cur, g_fill;

/* the export buffer, grown as samples arrive */
static short   *g_pcm;
static size_t   g_pcm_len, g_pcm_cap;

static void pcm_append(const short *s, size_t n)
{
    if (g_pcm_len + n > g_pcm_cap) {
        size_t cap = g_pcm_cap ? g_pcm_cap * 2 : 1 << 16;
        short *p;
        while (cap < g_pcm_len + n)
            cap *= 2;
        p = (short *)realloc(g_pcm, cap * sizeof *p);
        if (p == NULL)
            return;
        g_pcm = p;
        g_pcm_cap = cap;
    }
    memcpy(g_pcm + g_pcm_len, s, n * sizeof *s);
    g_pcm_len += n;
}

/* Block until the block we are about to fill is no longer being played.
 * The producer is much faster than real time, so without this it laps
 * the device after NBLK blocks and rewrites the samples the card is
 * reading -- a glitch every second and a half rather than a crash.
 * The wait has a timeout so that a stop always gets through. */
static void wait_block_free(void)
{
    while (g_hdr[g_cur].dwFlags & WHDR_INQUEUE) {
        if (g_abort)
            return;
        WaitForSingleObject(g_wave_event, 50);
    }
}

/* Hand the current block to the device and move on to the next, which
 * wait_block_free has made safe to write into. */
static void submit_block(void)
{
    WAVEHDR *h = &g_hdr[g_cur];

    if (g_fill == 0)
        return;
    EnterCriticalSection(&g_lock);
    if (g_wave != NULL) {
        if (h->dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(g_wave, h, sizeof *h);
        memset(h, 0, sizeof *h);
        h->lpData = (LPSTR)g_blk[g_cur];
        h->dwBufferLength = (DWORD)(g_fill * sizeof(short));
        waveOutPrepareHeader(g_wave, h, sizeof *h);
        waveOutWrite(g_wave, h, sizeof *h);
    }
    LeaveCriticalSection(&g_lock);
    g_cur = (g_cur + 1) % NBLK;
    g_fill = 0;
    wait_block_free();
}

static int TVTTS_CALL on_audio(const tvtts_event *ev, void *user)
{
    (void)user;
    if (g_abort)
        return 1;
    if (ev->type == TVTTS_AUDIO) {
        uint32_t i = 0;
        if (g_job_file != NULL) {
            pcm_append(ev->samples, ev->count);
            return 0;
        }
        while (i < ev->count) {
            uint32_t room = BLKSAMP - (uint32_t)g_fill;
            uint32_t take = ev->count - i < room ? ev->count - i : room;
            memcpy(&g_blk[g_cur][g_fill], ev->samples + i, take * sizeof(short));
            g_fill += (int)take;
            i += take;
            if (g_fill == BLKSAMP) {
                submit_block();
                if (g_abort)
                    return 1;
            }
        }
    }
    return 0;
}

static int write_wav(const wchar_t *path, const short *pcm, size_t n, uint32_t hz)
{
    FILE *f = _wfopen(path, L"wb");
    unsigned char h[44];
    uint32_t data = (uint32_t)(n * 2), riff = data + 36;

    if (f == NULL)
        return 0;
    memcpy(h, "RIFF", 4);
    h[4] = (unsigned char)riff; h[5] = (unsigned char)(riff >> 8);
    h[6] = (unsigned char)(riff >> 16); h[7] = (unsigned char)(riff >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;         /* fmt chunk size */
    h[20] = 1; h[21] = 0;                          /* PCM */
    h[22] = 1; h[23] = 0;                          /* mono */
    h[24] = (unsigned char)hz; h[25] = (unsigned char)(hz >> 8);
    h[26] = (unsigned char)(hz >> 16); h[27] = (unsigned char)(hz >> 24);
    h[28] = (unsigned char)(hz * 2); h[29] = (unsigned char)((hz * 2) >> 8);
    h[30] = (unsigned char)((hz * 2) >> 16); h[31] = (unsigned char)((hz * 2) >> 24);
    h[32] = 2; h[33] = 0;                          /* block align */
    h[34] = 16; h[35] = 0;                         /* bits */
    memcpy(h + 36, "data", 4);
    h[40] = (unsigned char)data; h[41] = (unsigned char)(data >> 8);
    h[42] = (unsigned char)(data >> 16); h[43] = (unsigned char)(data >> 24);
    fwrite(h, 1, sizeof h, f);
    fwrite(pcm, 2, n, f);
    fclose(f);
    return 1;
}

static DWORD WINAPI worker(LPVOID arg)
{
    uint32_t hz;
    int i;

    (void)arg;
    /* The synth is only ever touched here, so the settings the UI collected
     * are applied on this side of the fence. */
    tvtts_set_sample_rate(g_synth, g_srate);
    tvtts_set_voice(g_synth, g_voice);
    tvtts_set_rate(g_synth, g_rate);
    tvtts_set_pitch(g_synth, g_pitch);
    tvtts_set_volume(g_synth, (uint32_t)(g_vol * 0xffff / 100));
    hz = tvtts_sample_rate_hz(g_srate);

    g_pcm_len = 0;
    g_cur = g_fill = 0;
    memset(g_hdr, 0, sizeof g_hdr);

    if (g_job_file == NULL) {
        WAVEFORMATEX wf;
        memset(&wf, 0, sizeof wf);
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 1;
        wf.nSamplesPerSec = hz;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 2;
        wf.nAvgBytesPerSec = hz * 2;
        EnterCriticalSection(&g_lock);
        if (waveOutOpen(&g_wave, WAVE_MAPPER, &wf, (DWORD_PTR)g_wave_event, 0,
                        CALLBACK_EVENT) != MMSYSERR_NOERROR)
            g_wave = NULL;
        LeaveCriticalSection(&g_lock);
        if (g_wave == NULL) {
            PostMessageW(g_main, WM_APP_DONE, 2, 0);
            return 0;
        }
    }

    tvtts_speak_utf16(g_synth, (const uint16_t *)g_job_text, on_audio, NULL);

    if (g_job_file != NULL) {
        int ok = write_wav(g_job_file, g_pcm, g_pcm_len, hz);
        PostMessageW(g_main, WM_APP_DONE, ok ? 3 : 4, 0);
        return 0;
    }

    submit_block();
    /* let whatever is already queued finish playing */
    for (;;) {
        int busy = 0;
        if (g_abort)
            break;
        for (i = 0; i < NBLK; i++)
            if (g_hdr[i].dwFlags & WHDR_INQUEUE)
                busy = 1;
        if (!busy)
            break;
        WaitForSingleObject(g_wave_event, 50);
    }
    EnterCriticalSection(&g_lock);
    if (g_wave != NULL) {
        waveOutReset(g_wave);
        for (i = 0; i < NBLK; i++)
            if (g_hdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof g_hdr[i]);
        waveOutClose(g_wave);
        g_wave = NULL;
    }
    LeaveCriticalSection(&g_lock);
    PostMessageW(g_main, WM_APP_DONE, g_abort ? 1 : 0, 0);
    return 0;
}

/* ---- small helpers ------------------------------------------------------ */

static void set_status(const wchar_t *s)
{
    SetDlgItemTextW(g_main, ID_STATUS, s);
}

static void stop_speech(void)
{
    g_abort = 1;
    EnterCriticalSection(&g_lock);
    if (g_wave != NULL) {
        /* a paused device is restarted first: reset on a paused device
         * is not something to rely on, and the worker has to be able to
         * get out of its wait for a free buffer */
        if (g_state == ST_PAUSED)
            waveOutRestart(g_wave);
        waveOutReset(g_wave);
    }
    LeaveCriticalSection(&g_lock);
    if (g_thread != NULL) {
        WaitForSingleObject(g_thread, 4000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    g_state = ST_IDLE;
}

static void start_job(wchar_t *text, wchar_t *file)
{
    DWORD id;

    stop_speech();
    free(g_job_text);
    free(g_job_file);
    g_job_text = text;
    g_job_file = file;
    g_abort = 0;
    g_state = ST_SPEAKING;
    g_thread = CreateThread(NULL, 0, worker, NULL, 0, &id);
    if (g_thread == NULL) {
        g_state = ST_IDLE;
        set_status(L"Could not start the synthesis thread.");
    }
}

/* The text to speak, or a copy of a fixed phrase for the engine to announce. */
static wchar_t *dup_text(const wchar_t *s)
{
    size_t n = wcslen(s) + 1;
    wchar_t *p = (wchar_t *)malloc(n * sizeof *p);
    if (p != NULL)
        memcpy(p, s, n * sizeof *p);
    return p;
}

static wchar_t *read_edit(void)
{
    HWND e = GetDlgItem(g_main, ID_TEXT);
    int n = GetWindowTextLengthW(e);
    wchar_t *p = (wchar_t *)malloc(((size_t)n + 1) * sizeof *p);
    if (p == NULL)
        return NULL;
    GetWindowTextW(e, p, n + 1);
    return p;
}

static void show_slider_value(int id, int val, const wchar_t *unit)
{
    wchar_t buf[64];
    _snwprintf(buf, 64, L"%d %s", val, unit);
    buf[63] = 0;
    SetDlgItemTextW(g_main, id, buf);
}

static void sync_sliders(void)
{
    SendDlgItemMessageW(g_main, ID_RATE, TBM_SETPOS, TRUE, g_rate);
    SendDlgItemMessageW(g_main, ID_PITCH, TBM_SETPOS, TRUE, g_pitch);
    SendDlgItemMessageW(g_main, ID_VOLUME, TBM_SETPOS, TRUE, g_vol);
    show_slider_value(ID_RATEVAL, g_rate, L"wpm");
    show_slider_value(ID_PITCHVAL, g_pitch, L"Hz");
    show_slider_value(ID_VOLVAL, g_vol, L"%");
}

static void apply_voice_defaults(int voice)
{
    g_rate = tvtts_voice_rate(voice);
    g_pitch = tvtts_voice_pitch(voice);
    if (g_rate < TVTTS_RATE_MIN)
        g_rate = TVTTS_RATE_MIN;
    if (g_pitch < TVTTS_PITCH_MIN)
        g_pitch = TVTTS_PITCH_MIN;
    sync_sliders();
}

/* ---- the window --------------------------------------------------------- */

static int g_ch, g_cw;   /* character cell, for laying out in the user's font */

static HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, g_main, (HMENU)(INT_PTR)id, g_inst, NULL);
}

static void layout(void)
{
    RECT rc;
    int pad, row, lab, ctl, val, y, bw, x, i;
    static const int IDS[] = {ID_SPEAK, ID_PAUSE, ID_STOP, ID_RESET, ID_EXPORT};

    GetClientRect(g_main, &rc);
    pad = g_ch / 2;
    row = g_ch * 3 / 2;
    lab = g_cw * 26;
    val = g_cw * 12;
    ctl = rc.right - pad * 2 - lab - val - pad * 2;
    if (ctl < g_cw * 10)
        ctl = g_cw * 10;

    /* the settings block and the buttons sit at the bottom; the edit takes
     * whatever is left, so the window can be resized to suit the text */
    y = rc.bottom - pad - row            /* status */
        - pad - row                      /* buttons */
        - pad - (row + pad) * 5;         /* voice, rate, pitch, volume, rate */

    MoveWindow(GetDlgItem(g_main, ID_TEXT + 1000), pad, pad, rc.right - pad * 2, g_ch, TRUE);
    {
        int eh = y - pad * 2 - g_ch - 2;
        if (eh < g_ch * 2)
            eh = g_ch * 2;
        MoveWindow(GetDlgItem(g_main, ID_TEXT), pad, pad + g_ch + 2,
                   rc.right - pad * 2, eh, TRUE);
    }

    {
        struct { int lab, ctl, val; } rows[] = {
            {ID_VOICE + 1000, ID_VOICE, 0},
            {ID_RATE + 1000, ID_RATE, ID_RATEVAL},
            {ID_PITCH + 1000, ID_PITCH, ID_PITCHVAL},
            {ID_VOLUME + 1000, ID_VOLUME, ID_VOLVAL},
            {ID_SRATE + 1000, ID_SRATE, 0},
        };
        for (i = 0; i < 5; i++) {
            MoveWindow(GetDlgItem(g_main, rows[i].lab), pad, y + (row - g_ch) / 2,
                       lab, g_ch, TRUE);
            MoveWindow(GetDlgItem(g_main, rows[i].ctl), pad + lab, y, ctl,
                       rows[i].val ? row : row * 8, TRUE);
            if (rows[i].val)
                MoveWindow(GetDlgItem(g_main, rows[i].val), pad + lab + ctl + pad,
                           y + (row - g_ch) / 2, val, g_ch, TRUE);
            y += row + pad;
        }
    }

    bw = (rc.right - pad * 6) / 5;
    x = pad;
    for (i = 0; i < 5; i++) {
        MoveWindow(GetDlgItem(g_main, IDS[i]), x, y, bw, row, TRUE);
        x += bw + pad;
    }
    y += row + pad;
    MoveWindow(GetDlgItem(g_main, ID_STATUS), pad, y, rc.right - pad * 2, row, TRUE);
}

static void build_controls(void)
{
    int i, n;
    HWND h;

    /* Each label is created immediately before the control it names: that
     * adjacency is what a screen reader reads as the control's name. */
    mk(L"STATIC", L"&Text to speak", 0, ID_TEXT + 1000);
    mk(L"EDIT", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                     ES_WANTRETURN | ES_AUTOVSCROLL, ID_TEXT);

    mk(L"STATIC", L"&Voice", 0, ID_VOICE + 1000);
    h = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_VOICE);
    n = tvtts_voice_count();
    for (i = 0; i < n; i++) {
        wchar_t w[64];
        MultiByteToWideChar(CP_UTF8, 0, tvtts_voice_name(i), -1, w, 64);
        SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)w);
    }
    SendMessageW(h, CB_SETCURSEL, DEF_VOICE, 0);

    mk(L"STATIC", L"&Rate, words per minute", 0, ID_RATE + 1000);
    h = mk(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, ID_RATE);
    SendMessageW(h, TBM_SETRANGE, FALSE, MAKELPARAM(TVTTS_RATE_MIN, TVTTS_RATE_MAX_EXT));
    SendMessageW(h, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(h, TBM_SETTICFREQ, 50, 0);
    mk(L"STATIC", L"", 0, ID_RATEVAL);

    mk(L"STATIC", L"&Pitch, hertz", 0, ID_PITCH + 1000);
    h = mk(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, ID_PITCH);
    SendMessageW(h, TBM_SETRANGE, FALSE, MAKELPARAM(TVTTS_PITCH_MIN, TVTTS_PITCH_MAX));
    SendMessageW(h, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(h, TBM_SETTICFREQ, 50, 0);
    mk(L"STATIC", L"", 0, ID_PITCHVAL);

    mk(L"STATIC", L"Vol&ume, percent", 0, ID_VOLUME + 1000);
    h = mk(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, ID_VOLUME);
    SendMessageW(h, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessageW(h, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(h, TBM_SETTICFREQ, 10, 0);
    mk(L"STATIC", L"", 0, ID_VOLVAL);

    mk(L"STATIC", L"Sa&mple rate", 0, ID_SRATE + 1000);
    h = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_SRATE);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"8 kHz");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"11 kHz");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"16 kHz");
    SendMessageW(h, CB_SETCURSEL, DEF_SRATE, 0);

    mk(L"BUTTON", L"&Speak (F5)", WS_TABSTOP | BS_PUSHBUTTON, ID_SPEAK);
    mk(L"BUTTON", L"P&ause (F6)", WS_TABSTOP | BS_PUSHBUTTON, ID_PAUSE);
    mk(L"BUTTON", L"St&op (F7)", WS_TABSTOP | BS_PUSHBUTTON, ID_STOP);
    mk(L"BUTTON", L"R&eset (F8)", WS_TABSTOP | BS_PUSHBUTTON, ID_RESET);
    mk(L"BUTTON", L"E&xport to WAV...", WS_TABSTOP | BS_PUSHBUTTON, ID_EXPORT);

    /* The status line is read-only and not a tab stop; it is there to be
     * read with the review cursor, not landed on. */
    mk(L"STATIC", L"Ready.", 0, ID_STATUS);
}

static void do_speak(void)
{
    wchar_t *t = read_edit();
    if (t == NULL)
        return;
    if (t[0] == 0) {
        free(t);
        set_status(L"Nothing to speak: the text box is empty.");
        return;
    }
    start_job(t, NULL);
    set_status(L"Speaking.");
}

static void do_pause(void)
{
    if (g_state == ST_SPEAKING) {
        EnterCriticalSection(&g_lock);
        if (g_wave != NULL)
            waveOutPause(g_wave);
        LeaveCriticalSection(&g_lock);
        g_state = ST_PAUSED;
        set_status(L"Paused.");
    } else if (g_state == ST_PAUSED) {
        EnterCriticalSection(&g_lock);
        if (g_wave != NULL)
            waveOutRestart(g_wave);
        LeaveCriticalSection(&g_lock);
        g_state = ST_SPEAKING;
        set_status(L"Speaking.");
    } else {
        set_status(L"Nothing is speaking.");
    }
}

static void do_reset(void)
{
    stop_speech();
    g_voice = DEF_VOICE;
    g_vol = DEF_VOLUME;
    g_srate = DEF_SRATE;
    SendDlgItemMessageW(g_main, ID_VOICE, CB_SETCURSEL, DEF_VOICE, 0);
    SendDlgItemMessageW(g_main, ID_SRATE, CB_SETCURSEL, DEF_SRATE, 0);
    apply_voice_defaults(DEF_VOICE);
    set_status(L"Reset to default settings.");
    /* the engine says so itself, which is the announcement that matters */
    start_job(dup_text(L"Reset speech."), NULL);
}

static void do_export(void)
{
    OPENFILENAMEW ofn;
    wchar_t path[MAX_PATH];
    wchar_t *t;

    t = read_edit();
    if (t == NULL)
        return;
    if (t[0] == 0) {
        free(t);
        set_status(L"Nothing to export: the text box is empty.");
        return;
    }
    path[0] = 0;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"wav";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) {
        free(t);
        return;
    }
    start_job(t, dup_text(path));
    set_status(L"Writing the WAV file...");
}

static INT_PTR CALLBACK dlgproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG:
        g_main = h;
        build_controls();
        {
            HWND c = GetWindow(h, GW_CHILD);
            while (c != NULL) {
                SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
                c = GetWindow(c, GW_HWNDNEXT);
            }
        }
        apply_voice_defaults(DEF_VOICE);
        layout();
        /* Returning TRUE hands focus to the first control with
         * WS_TABSTOP, which is the text box.  Doing it this way rather
         * than with SetFocus is the whole reason this is a dialog: a
         * SetFocus during WM_CREATE happens before the window is
         * visible, the activation that follows puts focus back on the
         * frame, and Tab then has no control to move on from. */
        return TRUE;

    case WM_SIZE:
        layout();
        return TRUE;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = g_cw * 70;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = g_ch * 28;
        return TRUE;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);

    case WM_HSCROLL: {
        HWND c = (HWND)lp;
        int pos = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
        if (c == GetDlgItem(h, ID_RATE)) {
            g_rate = pos;
            show_slider_value(ID_RATEVAL, pos, L"wpm");
        } else if (c == GetDlgItem(h, ID_PITCH)) {
            g_pitch = pos;
            show_slider_value(ID_PITCHVAL, pos, L"Hz");
        } else if (c == GetDlgItem(h, ID_VOLUME)) {
            g_vol = pos;
            show_slider_value(ID_VOLVAL, pos, L"%");
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:                         /* Enter */
        case ID_SPEAK:  do_speak();  return TRUE;
        case ID_PAUSE:  do_pause();  return TRUE;
        case ID_STOP:
            stop_speech();
            set_status(L"Stopped.");
            return TRUE;
        case ID_RESET:  do_reset();  return TRUE;
        case ID_EXPORT: do_export(); return TRUE;
        case IDCANCEL:                     /* Escape */
            stop_speech();
            set_status(L"Stopped.");
            return TRUE;
        case ID_VOICE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                wchar_t buf[96], name[64];
                g_voice = (int)SendDlgItemMessageW(h, ID_VOICE, CB_GETCURSEL, 0, 0);
                /* the engine keeps a default rate and pitch per voice, and
                 * adopting them is what makes each voice sound like itself */
                apply_voice_defaults(g_voice);
                MultiByteToWideChar(CP_UTF8, 0, tvtts_voice_name(g_voice), -1, name, 64);
                _snwprintf(buf, 96, L"%s: rate %d, pitch %d.", name, g_rate, g_pitch);
                buf[95] = 0;
                set_status(buf);
            }
            return TRUE;
        case ID_SRATE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                stop_speech();
                g_srate = (int)SendDlgItemMessageW(h, ID_SRATE, CB_GETCURSEL, 0, 0);
                set_status(L"Sample rate changed.");
            }
            return TRUE;
        }
        return TRUE;

    case WM_APP_DONE:
        g_state = ST_IDLE;
        switch ((int)wp) {
        case 0: set_status(L"Ready."); break;
        case 1: set_status(L"Stopped."); break;
        case 2: set_status(L"No audio device is available."); break;
        case 3: set_status(L"WAV file written."); break;
        case 4: set_status(L"Could not write the WAV file."); break;
        }
        return TRUE;

    case WM_CLOSE:
        stop_speech();
        DestroyWindow(h);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

/* Centre on the work area of whichever monitor the window is on, so it does
 * not end up under the taskbar and does not assume one screen. */
static void centre_on_work_area(HWND h)
{
    RECT r, wa;
    MONITORINFO mi;
    int w, ht;

    GetWindowRect(h, &r);
    w = r.right - r.left;
    ht = r.bottom - r.top;
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi))
        wa = mi.rcWork;
    else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0))
        return;
    SetWindowPos(h, NULL, wa.left + (wa.right - wa.left - w) / 2,
                 wa.top + (wa.bottom - wa.top - ht) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* A dialog template with no controls in it: the children are created in
 * WM_INITDIALOG exactly as they were before.  All that is wanted from the
 * template is a real dialog, so that the dialog manager owns Tab, the arrow
 * keys, the mnemonics and the initial focus -- and so that a screen reader
 * sees a dialog rather than a bare window. */
static void *dialog_template(const wchar_t *title, DWORD style)
{
    static WORD buf[128];
    DLGTEMPLATE *t = (DLGTEMPLATE *)buf;
    WORD *p;
    size_t i;

    memset(buf, 0, sizeof buf);
    t->style = style;
    t->dwExtendedStyle = 0;
    t->cdit = 0;
    t->x = t->y = 0;
    t->cx = 200;
    t->cy = 150;
    p = (WORD *)(t + 1);
    *p++ = 0;                      /* no menu */
    *p++ = 0;                      /* the standard dialog class */
    for (i = 0; title[i] != 0; i++)
        *p++ = (WORD)title[i];
    *p++ = 0;
    return buf;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    INITCOMMONCONTROLSEX icc;
    NONCLIENTMETRICSW ncm;
    MSG msg;
    HACCEL acc;
    ACCEL a[6];
    HDC dc;
    TEXTMETRICW tm;

    (void)prev; (void)cmd;
    g_inst = inst;
    InitializeCriticalSection(&g_lock);

    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    /* the user's own interface font, so this follows their size settings */
    ncm.cbSize = sizeof ncm;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (g_font == NULL)
        g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    dc = GetDC(NULL);
    SelectObject(dc, g_font);
    GetTextMetricsW(dc, &tm);
    g_ch = tm.tmHeight;
    g_cw = tm.tmAveCharWidth;
    ReleaseDC(NULL, dc);

    g_wave_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_synth = tvtts_create(11025);
    if (g_synth == NULL) {
        MessageBoxW(NULL, L"The TruVoice engine could not be started.",
                    L"OpenTV Speak", MB_ICONERROR | MB_OK);
        return 1;
    }

    if (CreateDialogIndirectParamW(inst,
            (LPCDLGTEMPLATEW)dialog_template(L"OpenTV Speak",
                /* no DS_SETFONT: that would make the dialog manager read
                 * a font out of the template, and there is none in it.
                 * Each control is given the user's font individually. */
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN),
            NULL, dlgproc, 0) == NULL || g_main == NULL)
        return 1;
    /* size from the user's own font, then centre, then show: centring has to
     * come after the final size or it centres the template's size */
    SetWindowPos(g_main, NULL, 0, 0, g_cw * 90, g_ch * 34,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    centre_on_work_area(g_main);
    ShowWindow(g_main, show);
    UpdateWindow(g_main);

    a[0].fVirt = FVIRTKEY; a[0].key = VK_F5; a[0].cmd = ID_SPEAK;
    a[1].fVirt = FVIRTKEY; a[1].key = VK_F6; a[1].cmd = ID_PAUSE;
    a[2].fVirt = FVIRTKEY; a[2].key = VK_F7; a[2].cmd = ID_STOP;
    a[3].fVirt = FVIRTKEY; a[3].key = VK_F8; a[3].cmd = ID_RESET;
    a[4].fVirt = FVIRTKEY | FCONTROL; a[4].key = 'S'; a[4].cmd = ID_EXPORT;
    a[5].fVirt = FVIRTKEY; a[5].key = VK_ESCAPE; a[5].cmd = IDCANCEL;
    acc = CreateAcceleratorTableW(a, 6);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (TranslateAcceleratorW(g_main, acc, &msg))
            continue;
        if (IsDialogMessageW(g_main, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    stop_speech();
    tvtts_destroy(g_synth);
    DeleteCriticalSection(&g_lock);
    return 0;
}
