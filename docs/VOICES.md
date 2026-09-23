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

The signs read as vocal-tract length.  Deep Douglas, who is voice 3, scales
every formant *down* -- a longer tract, a bigger speaker -- and Wanda and
Julia scale them up.  Nothing else about the voice is stored: no spectral
envelope, no recorded data.

### The names are not in voice order

The speaker names are stored as an ANSI string followed by the same name in
UTF-16, but reading them in the order they sit in memory gets the voices
wrong.  The compiler emitted the literals for every name after the first in
the reverse of the order the engine registers them, so the block reads
Peter, Julia, Wanda, Alex, Melvin, Grandpa Amos, Biff, Deep Douglas, Eager
Eddie, Sidney while the voices are

| 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Peter | Sidney | Eager Eddie | Deep Douglas | Biff |

| 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|
| Grandpa Amos | Melvin | Alex | Wanda | Julia |

Peter and Grandpa Amos are the only two that land in the same place under
both orders, which is a good way to be fooled: checking those two alone
says nothing.  The authority is the initialisation code, which pushes each
voice's two names in turn, and `tools/voicedump.py` takes the order from
that rather than from the addresses.  The same reversal holds in all five
language DLLs.

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
German -- and both sit at voice 5, which is another way to check that a
name list has been put in the right order.

`g_voice_pitch` tracks what the names suggest once the voices are in the
right order: Deep Douglas is the lowest of the male voices at 73, Wanda and
Julia the highest at 208 and 152.  So it reads as a frequency rather than
the period track 17 holds, though that has not been confirmed by
measurement.

## Rate is 26 rows, not a number

`g_voice_speed` is a words-per-minute figure, but the engine does not use
it as one.  `Engine_SetSpeed` (`src/engine/engine.c:97`) turns it straight
into a table index:

```c
int32_t idx = (int32_t)((uint32_t)(wpm - 46) >> 3);
```

and `idx` subscripts `g_dur_rate` (`0x100ee630`) and `g_pause_rate`
(`0x100ee698`).  Those two addresses are `0x68` apart, which is 26 `int32`,
so there are 26 rows and the engine has 26 speeds: **46 to 253 words per
minute in steps of eight**, and nothing in between or outside.

Both ends fail rather than clamp, and both were confirmed against
`CGRM_EN.DLL` itself:

* **Below 46** the subtraction is unsigned, so 45 gives `idx` 0x1fffffff
  and a wild read.  The original segfaults at 10, 20, 30, 40 and 45, and so
  does the port -- identically.  46 is the lowest value that works.
* **Above 253** `idx` passes 25 and the engine reads whatever follows the
  table.  The same sentence is 38,544 bytes at 253 and 391,864 at 254: ten
  times longer for asking it to go faster.  Oracle and port agree byte for
  byte at 260, 275, 300, 350 and 400, so this is the original's behaviour
  and not a decompilation fault.

The boundaries are the same for all ten voices, which follows from the
index being computed before the voice is consulted at all.  46..76 map to
rows 0..3, which hold the same duration, so the four slowest settings are
indistinguishable.

This is what `TVTTS_RATE_MIN` and `TVTTS_RATE_MAX` in `include/tvtts.h`
are, and why the NVDA driver's rate slider spans exactly 46..253.

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

## Speaking phonemes directly

```
ESC[1I   <phonemes>   ESC[0I
```

`ESC[<0|1>I` sets `mode_I` (`src/engine/preformat.c:210`), and inside it the
text stream is read as the engine's own phoneme alphabet rather than as
words.  This is not the bracket mode in `phonetic.c`, which spells ARPABET
names like `[HH AH L OW]`; it takes the compact one-character-per-phoneme
form -- `HeLO1` for "hello" -- that the `w_212c` trace emits.

The source for this is `TV_ENG32.DLL`, the 5.1 build of the same engine
that exposes Centigram's flat C API instead of COM (see "The other
English builds" at the end).  Its `tts_SpeakPhoneme` is a nine-line wrapper: it
allocates `strlen(arg) + 0x28`, copies the literal at `0x100ff380` in front
of the caller's string and the one at `0x100ff378` after it, and hands the
result to `tts_Speak`.  Those two literals are `ESC[1I` and `ESC[0I`.  Its
companion `tts_Phoneme` is the other direction -- text in, phoneme string
out, with a size-probe convention (`buf` may be NULL to ask for the length,
and `0xfc07` comes back when the buffer was too small).

So the alphabet is a documented input, not just an internal trace, and the
round trip closes.  Measured against `CGRM_EN.DLL`, which supports the same
escape:

```
hello              -> 24024 bytes
ESC[1I HeLO1 ESC[0I -> 24024 bytes, byte for byte identical
```

Oracle and port agree byte for byte on phoneme input, so the path is
covered by the decompilation as well as the text path is.  A leading `&` or
`%` is ignored; a trailing `.` adds the sentence-final pause, about 8,580
bytes of it at the default rate.  The correspondence is not exact for every
word -- "hi" against `HI1` matches in length and differs by at most 192 of
a full-scale 32,768, which is a small difference in the final fall rather
than a different word -- so the trace is very close to, but not provably
identical with, what the text path feeds the synthesiser.

Nothing else exposes any of it.  `ENGLISH.INI` is COM registration andnothing more, and SAPI surfaces only pitch, speed, volume and the voice
index.

## The other language DLLs

Out of scope for the decompilation, which targets American English only,
but they share the layout, and `tools/voicedump.py` reads them too.  Each
has its own speaker names, and the roles line up by index: 0 is the
baseline adult male, 5 is the one that is slowed (Grandpa Amos in English,
"Opa" in German), and the last two are the female voices -- Wanda and
Julia, Petra and Marga, Josefa and Isabel, Grazia and Licia, and in French,
which has eight, Madeleine and Jacqueline.

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

## The other English builds

Three builds of the English engine exist, and they are two product lines
rather than three versions:

| file | version | linked | interface |
|---|---|---|---|
| `CGRM_EN.DLL` | 5.0.0.51 | Oct 1997 | COM, SAPI 4 -- two exports |
| `TV_ENG32.DLL` | 5.1.0.16 | May 1997 | flat C, 24 `tts_*` exports |
| `TV_EN32P.DLL` | 5.1.0.15 | Feb 1997 | flat C, the same 24 |

`CGRM_EN.DLL` is this project's reference and is the newest by date despite
the lower version: 5.0.x is the SAPI-wrapped line, 5.1.x Centigram's own.

The engine data is shared.  With relocations masked, 152 of the 208 data
symbols this project names are byte-identical in `TV_ENG32.DLL`, and every
one tested at its exact declared size matches: the voice adjustment table,
`g_voice_pitch`, `g_voice_speed`, `g_dur_rate`, `g_pause_rate`,
`g_param_max`, `g_default_params`, `g_hold_atten`, `g_stop_rel_kind` and
the `g_pt_a*` phoneme parameter targets.  Where those last ones appear to
differ it is only in their first four bytes, a self-link shifted by the
relocation delta; the payload is untouched.  The code was recompiled and
relaid out, so addresses are unrelated, but about half the functions this
project annotates are still byte-identical once relocations are masked.

Two things the flat-API builds do not have: the COM entry points, so the
harness cannot load them without new code, and the speaker names.  None of
Sidney, Eager Eddie, Grandpa Amos, Wanda or Julia appears in either, in
ANSI or UTF-16 -- those strings exist only in the SAPI build, which needs
them for the mode-info block.  The ten voices and their parameters are
identical regardless.

What the flat API is good for is documentation: `tts_SpeakPhoneme` is how
the `ESC[1I` phoneme mode above was found, and the rest of the 24 name the
operations the COM build hides behind interface slots.
