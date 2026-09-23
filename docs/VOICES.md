# Voices and synthesis parameters

TruVoice is a formant synthesizer: everything it says is 22 parameter
tracks -- amplitudes, four formant frequencies, their bandwidths, a pitch
period and a few source controls -- read one frame at a time and turned
into filter coefficients by `Synth_Frame` (`src/engine/frame.c`).  A
"voice" is not a recording or a separate model.  It is a row of numbers
that bends those 22 tracks on the way past.

The numbers themselves are the original's data and are not in this
repository.  `python tools/voicedump.py` prints them out of your own copy
of the binaries, and finds the tables by signature rather than by address,
so it works on any of the five language DLLs.

## Every voice is a deviation from voice 0

The per-voice table is `g_voice_adjust` (`0x100b5068` in the English DLL):
one row of fifteen `int32` per voice.  The first seven columns are
**percentages**, applied in `src/engine/frame.c:131` once per frame:

```c
t1c = (p[21] & 0xf0) >> 4;          /* the voice index rides in track 21 */
adj = &g_voice_adjust[t1c * 15];
p[9]  += (adj[0] * p[9])  / 100;    /* F1 */
p[13] += (adj[1] * p[13]) / 100;    /* B1 */
p[10] += (adj[2] * p[10]) / 100;    /* F2, clamped to 0xff */
p[14] += (adj[3] * p[14]) / 100;    /* B2 */
p[11] += (adj[4] * p[11]) / 100;    /* F3 */
p[15] += (adj[5] * p[15]) / 100;    /* B3 */
p[12] += (adj[6] * p[12]) / 100;    /* F4 */
```

**Voice 0's seven percentages are all zero**, in every language DLL.  Zero
percent is identity, so voice 0 falls through the block unchanged: it is
the voice the phoneme tables were written for, and the other nine are
stored as deviations from it.  In American English that voice is Peter.

The signs read as vocal-tract length.  Voices meant to sound larger and
deeper (Melvin, Alex) scale every formant *down*; smaller and higher ones
(Eager Eddie, Wanda) scale them *up*.  Nothing else about the voice is
stored -- no spectral envelope, no recorded data.

The remaining columns of the row are not percentages:

| column | used as |
|---|---|
| `adj[7]` | resonator table offset for the F1 branch (`frame.c:256`) |
| `adj[8]` | a frequency ceiling, compared against `sample_rate / 2` |
| `adj[9]` | resonator table offset for the nasal branch (`frame.c:240`) |
| `adj[10]` | index into the jitter table `g_tab_5648` |
| `adj[11]` | index into the shimmer/gain table `g_tab_5688` |
| `adj[12]` | added to track 2, clamped to 0x6b |
| `adj[13]` | written straight into track 18 |
| `adj[14]` | written straight into track 19 |

Note that the voice index is read from the high nibble of parameter track
21, which is a *track* -- it travels with the frames, so a voice change
takes effect frame by frame rather than utterance by utterance.

## The other per-voice tables

Laid out back to back after the adjustment block, one `int32` per voice:

| symbol | address (EN) | what it does |
|---|---|---|
| `g_synth_breath` | `0x100b52c0` | breathiness, scales the noise source |
| `g_voice_nasal_rate` | `0x100b52e8` | nasal pole shift |
| `g_voice_p18..p21` | `0x100b5310`.. | starting values for tracks 18-21 |
| `g_voice_pitch` | `0x100b5350` | base pitch |
| `g_voice_rate_index` | `0x100b5378` | rate class |
| `g_voice_speed` | `0x100b53a0` | words per minute |
| `g_voice_f4` | `0x100b53c8` | F4 offset |
| `g_voice_nasal_max` | `0x100b53f0` | ceiling on the nasal target |
| `g_voice_pitch_scale` | `0x100b5440` | Q15 scale on the pitch range |

In the American English DLL several of these are uniform across all ten
voices -- `g_voice_nasal_rate` and `g_voice_f4` are zero throughout,
`g_voice_pitch_scale` is 32766 (1.0 in Q15) throughout.  The per-voice
machinery exists; English simply does not use those axes.  `g_voice_speed`
and `g_voice_rate_index` vary for exactly one voice, the elderly one,
which is the quickest way to confirm that a speaker list lines up with the
table rows: voice 5 is Grandpa Amos in English and, helpfully, "Opa" in
German.

`g_voice_pitch` is lower for the voices that sound higher, so it reads as
a period rather than a frequency, which agrees with track 17 being the
pitch period.  That has not been confirmed by measurement.

## The 22 tracks

From `src/engine/synth.c`:

```
 0..8   amplitudes and source controls
 9..12  formant frequencies F1..F4
13..17  bandwidths, and 17 the pitch period
18..21  source parameters; 21 also carries the voice index
```

Each track has its own maximum and its own factory default, in
`g_param_max` (`0x100f83e0`) and `g_default_params` (`0x100f8530`); both
are enforced by the escape command below, in `src/engine/preformat.c:284`.

## Driving the parameters from the text stream

The raw tracks are exposed:

```
ESC[<index>;<value>l        index 0..21, clamped to g_param_max[index]
ESC[<index>l                reset that track to g_default_params[index]
```

`Preformat_Run` clamps and records it in `Engine.cfg_bytes`;
`Engine_RunControl` (`src/engine/control.c:151`) writes it into
`Engine.s3_param_raw`.

**It does not affect running speech.**  `s3_param_raw` is read only by the
held-frame path in `Stage3_Hold` (`src/engine/stage3.c:327`), because
ordinary speech rewrites all 22 targets from the phoneme tables at every
phoneme.  Sending `ESC[9;180l` before a sentence gives byte-identical
audio.  Paired with the hold command it works, and the three lines below
render three different waveforms:

```
ESC[0;60l ESC[9;100l               ESC[100g
ESC[0;60l ESC[9;200l               ESC[100g
ESC[0;60l ESC[9;100l ESC[10;200l   ESC[100g
```

So the engine will act as a bare formant-frame synthesizer on demand --
you can place the resonators by hand and hold them -- but there is no way
to modulate live speech through this interface.

Nothing else exposes any of it.  `ENGLISH.INI` is COM registration and
nothing more, and SAPI surfaces only pitch, speed, volume and the voice
index.

## The other language DLLs

Out of scope for the decompilation, which targets American English only,
but they share the layout, and `tools/voicedump.py` reads them too.  Each
has its own speaker names, and the roles line up by index: 0 is the
baseline adult male, 1 and 2 female, 5 elderly and slowed.

| | voice 0 | voices | adjustment table |
|---|---|---|---|
| English | Peter | 10 | the baseline |
| German | Dieter | 10 | English, 3 rows changed |
| Italian | Piero | 10 | English, 3 rows changed |
| Spanish | Pedro | 10 | byte-identical to Italian |
| French | Didier | 8 | independently tuned |

The degree of sharing is worth knowing before drawing conclusions from
any one of them.  German, Italian and Spanish differ from the English
adjustment table in exactly three of ten rows (6, 8 and 9); the other
seven are byte-identical, and Italian and Spanish are identical to each
other throughout.  French is the only one that was genuinely retuned:
eight voices, and only two of its eight rows match any English row -- the
all-zero baseline, and Henri, who is Alex unchanged.  Its speed table also
differs (160 wpm against 150, and 150 for the elderly voice against 120).
