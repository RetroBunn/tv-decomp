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
| `g_synth_breath` | `0x100b52c0` | a one-tap tilt on the output (see below) |
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

## Voices of OpenTV's own

The engine reads every per-voice table as `TABLE[voice]`, and carries the voice
in four bits of track 21 -- `stage3.c` ORs it in there and `frame.c` takes it
back out -- so **sixteen voices are addressable where ten are defined**.  Only
two things stood in the way of using the rest: the tables stop at ten, and
`Engine_SetVoice` refuses anything past them.

`src/engine/voices.c` is what fills the gap.  Every read site in the engine now
goes through an accessor that answers from the DLL's tables below
`TV_STOCK_VOICES` and from definitions in that file above it; for a stock voice
an accessor is exactly the subscript it replaced, which is why the corpus is
unaffected.  `Engine_SetVoice`'s bound moves out to however many are defined,
and a number past *those* is still ignored, as it always was.

A definition gives only what it changes.  `TV_V_INHERIT` takes the value from
the stock voice it is based on, so **no table from the original binary is
copied into source** -- a voice here is a description of how it differs from one
of Centigram's, which is also how the voices it is ported from were described.

### Frank, and why it is Frank

The first, and a deep male voice: from MindMaker's TextAssist build of this
engine, where the Centigram engine sits under a voice layer of MindMaker's own
and each voice ships as a plain-text file of Klatt-style parameters.
`Frank.tav` is twenty-three of them, and against Bill -- that product's name for
voice 0, which is Peter -- **eleven** differ:

| | Bill | Frank | carried over? |
| --- | --- | --- | --- |
| `F0Def` | 100 | **72** | yes, as the voice's pitch |
| `HeadSize` | 1.0 | **1.1** | yes, as -9 on the formant columns |
| `IntonLevel` | 1.0 | **0.7** | yes, as the voice's intonation depth |
| `SingF0Rate` | 1.0 | 0.508666 | no |
| `FricRate` | 0.508666 | 0.7 | no |
| `Breath` | 0.0 | 0.1 | no -- not the same parameter, see below |
| `Rich` | 0.5 | 0.65 | no |
| `LBoostFreq` | 50 | 100 | no |
| `LBoostGain` | 1.0 | 9.791484 | no |
| `HBoostFreq` | 2750 | 3506 | no |
| `HBoostGain` | 1.0 | 9.248628 | no |

The last four are two shelf filters, and they are not small: a 9.8x boost at
100 Hz and a 9.2x boost at 3506 Hz.  Lows and highs lifted that far together is
much of what makes the TextAssist Frank sound like himself, and this engine has
no output equaliser to put them in.

A *longer* tract puts every formant down by the reciprocal of its length, and
1/1.1 is 0.9091, so the seven percentage columns go to -9.  The bandwidths go
with them.  Everything else is Peter's.

`IntonLevel` was the third, and it was added because of what Frank sounded like
once the first two were in: right in the throat, and reading as flatly as Peter
does.  It is the parameter that separates an animated reader from a level one
at the same pitch, and this engine turns out to have had somewhere to put it
all along -- see **IntonLevel** below.

**He was chosen by measurement, not by ear.**  This engine tears at 8 and
11 kHz when a voice's formants are raised far, and how badly follows the boost
exactly.  All eight of the TextAssist voices were built and rendered; counting
adjacent samples that jump more than 32000, which speech does not do:

| voice | formants | 8 kHz | 11 kHz | 16 kHz |
| --- | --- | --- | --- | --- |
| **Frank** | **-9%** | **0** | **0** | **0** |
| Harry | 0% | 4 | 0 | 0 |
| Wendy | +23% | 1360 | 262 | 0 |
| Rita | +23% | 1504 | 302 | 0 |
| Kit | +30% | 4637 | 1362 | 0 |
| Timmy | +30% | 4846 | 1528 | 0 |
| Johnny | +47% | 11453 | 3811 | 0 |

Frank is the one of the eight that asks the filter bank for *less* than Peter
does rather than more, and he is clean everywhere.  Harry would do as well but
is Peter's tract at Peter's pitch and would be hard to tell from him.  The
interesting voices -- the child and female ones -- are exactly the ones that
tear, which is the trade this engine currently imposes.

### The Voice Editor's six sliders, and where each one lives

TextAssist's help screen shows its Voice Editor with Bill loaded, and that
screenshot is what pins the `.tav` keys to what they mean.  Its General tab
reads Volume 100, Head size 100, Frication rate 50, Richness 50, Breathiness 0,
Creakiness 0, against `Bill.tav`'s `Volume` 1.0, `HeadSize` 1.0, `FricRate`
0.508666, `Rich` 0.5, `Breath` 0.0 and `Larynx` 0.0.  So:

| slider | `.tav` key | Frank | where it lives here |
| --- | --- | --- | --- |
| Volume | `Volume` | 100 | the utterance volume |
| Head size | `HeadSize` | **110** | `adjust[0..6]`, the formant percentages |
| Frication rate | `FricRate` | 70 | nowhere yet |
| Richness | `Rich` | 65 | nowhere exactly; see the tilt below |
| **Breathiness** | `Breath` | **10** | `aspir`, new -- see below |
| **Creakiness** | `Larynx` | **0** | `adj[10]` and `adj[11]` |

Two of those are worth their own note, because the engine turns out to have had
somewhere to put both all along and Frank was inheriting a zero in each.

### Creakiness: jitter and shimmer, which Frank does not want

Two of the fifteen adjustment columns drive cycle-to-cycle perturbation, and
`frame.c` applies both the same way -- a depth from the voice, a magnitude from
a random draw:

| column | table | what it perturbs | range |
| --- | --- | --- | --- |
| `adj[10]` | `g_tab_5648` | the pitch period, `filt_coef[30]`/`[31]` -- **jitter** | 0 to 0.504 |
| `adj[11]` | `g_tab_5688` | the source amplitude, `filt_coef[34]`/`[35]` -- **shimmer** | 0 to 0.729 |

Sixteen evenly spaced steps each, index 0 being off, with `g_tab_56c8` supplying
the draw: 0.161 once, 0.121 three times, 0.081 six times, 0.040 six times, so
most perturbations are small.  Each is drawn twice per frame, once for each of a
pair of consecutive periods, which is the structure that makes creak rather than
hiss.  Centigram's ten:

| voice | jitter | shimmer |
| --- | --- | --- |
| **Peter** | **0** | **0** |
| Eager Eddie, Deep Douglas, Biff, Wanda | 1 | 1 |
| Sidney, Julia | 1 | 2 |
| Melvin, Alex | 2 | 2 |
| Grandpa Amos | 5 | 3 |

**Peter is the only one of the ten with both switched off**, so any voice based
on him inherits a larynx that never wavers.  Frank inherited exactly that, and
the first instinct was to give him Grandpa Amos's amounts, which measures as
periodicity 0.769 dropping to 0.593.

That was wrong, and the screenshot is why.  `Larynx` is **0.0 in all eight** of
the TextAssist `.tav` files, so not one of those voices asks for creak -- Frank
least of all, since what makes him sound as he does is the glottal wave below.
So jitter and shimmer stay at Peter's zero here.  The columns are named in the
definition anyway, because they are exactly what a voice of one's own would want
and there is no other way to reach them.

### Breathiness, and why `adj[12]` is not it

`adj[12]` is added to track 2, the aspiration amplitude, and Centigram's ten use
it: Peter and Melvin 0, Deep Douglas and Biff 5, Grandpa Amos 6, Eager Eddie 7,
Alex 12, Julia 13, Sidney and Wanda 15.  It looks like the breathiness control
and it is not, for a reason worth writing down:

    g_tab_1239bc[0..13]  =  0

Track 2 runs through that curve, which is **flat zero for its first fourteen
entries** and then climbs about a decibel a step to 32612 at the 0x6b clamp.  An
*offset* of ten on a vowel, whose track 2 is 0, therefore still asks for exactly
nothing.  What `adj[12]` does is lift the sounds that are aspirated already and
leave the vowels alone -- a real parameter, but not the slider's.

Breathiness in the sense people mean is noise under the voice throughout, so
`aspir` is a **floor** rather than an offset, applied while `p[0]` -- the voicing
amplitude -- is non-zero, which also leaves the silence test below it intact.  A
stock voice asks for 0 and the code does not run.

The units are track 2's own, so the useful range is narrow and high:

| `aspir` | table value | energy above 2 kHz | peak | clipped samples |
| --- | --- | --- | --- | --- |
| 0 | 0 | 0.00271 | 24872 | 0 |
| 68 | 365 | 0.00306 | 25480 | 0 |
| 72 | 579 | 0.00357 | 25768 | 0 |
| **76 -- Frank** | **919** | **0.00478** | 26408 | 0 |
| 80 | 1456 | 0.00751 | 27080 | 0 |
| 84 | 2308 | 0.01287 | 28616 | 0 |
| 88 | 3659 | 0.02105 | 32767 | 3 |

Nothing is audible below about 68 and 88 clips, so a voice wanting more than
about 84 should come down on `gain` to pay for it.  Frank asks for 10 of 100
where Wendy asks 50, which puts him at the gentle end: 76.

### The one thing that is not carried: the glottal wave

`Frank.tav` has a section `Bill.tav` does not -- a COM-embedded array of 225
doubles called `Wave`, a glottal pulse shape of its own.  Four of the eight
TextAssist voices carry one:

| has a `Wave[225]` | does not |
| --- | --- |
| Frank, Harry, Johnny, Rita | Bill, Kit, Timmy, Wendy |

This engine has exactly one pulse shape, `g_synth_pulse`, read 0..255 and
reflected about 0x80, global to all voices.  So there are two obstacles rather
than one: the table would have to become per-voice, and the array is **data out
of MindMaker's product rather than a description of it**, which is the line this
project has drawn everywhere else -- see NOTICE.  It is therefore not
reproduced, and a pulse shape remains something a voice here cannot change.

That is the honest limit of the port.  A glottal pulse shape is a large part of
what makes a voice's quality recognisable, and jitter, shimmer and the tilt
below approach it from the outside rather than reproducing it.

### The tilt that is called breath

`g_synth_breath[voice]` is worth a note because its name misleads, and this
document previously described it wrongly as scaling a noise source.  What
`generate.c` does with it is:

    out = si - breath / 100 * previous_out

A one-tap filter on the output, so it is a spectral tilt and nothing to do with
aspiration.  Negative darkens, positive brightens; the ten span -85 to +26 and
Peter is 0.  `Frank.tav`'s `Breath` of 0.1 is aspiration mixed into the source
in a different synthesiser, so there is no conversion between the two and Frank
leaves the tilt at Peter's 0.

### What a short vocal tract costs below 16 kHz

Johnny is clean at 16 kHz and tears below it, and so did Kit, less so in
proportion to how far its formants were raised.  Counting adjacent samples that
jump more than 32000, which speech does not do:

| | 8 kHz | 11.025 kHz | 16 kHz |
| --- | --- | --- | --- |
| Johnny (+47%) | 11630 | 9077 | **0** |
| Kit (+30%) | 4637 | 1362 | **0** |
| Peter | 0 | 0 | 0 |

`build/check/voicediag.exe` says why.  It is built with `TV_DIAG`, which is the
only thing that compiles its counters in, and it reports the widest value every
filter state reaches -- the bank keeps its state in 16 bits:

| | 8 kHz | 11.025 kHz | 16 kHz |
| --- | --- | --- | --- |
| Peter | 3240 | 6485 | 11285 |
| Wanda | 4814 | 5126 | 10800 |
| Johnny | **rails** | **rails** | 19379 |

**Where it goes wrong is now known to the line.**  `voicediag` probes the
output stage at each step; on "Hello there, my name is Johnny. I am a little
kid." at 8 kHz, the widest value reached at each point is

| | Peter | Johnny |
| --- | --- | --- |
| the parallel sum | 2251 | 13954 |
| after 6221/32768 | 427 | 2649 |
| less the de-emphasis state | 813 | 5174 |
| **plus the direct source path** | 1275 | **40292** |
| times eight, into sat16 | 10200 | 261896 |

`di = (int16_t)((int16_t)di + (int16_t)si)` is where it leaves sixteen bits.
`si` is the pre-emphasised source fed straight to the output, already saturated
to 32767 on its own, and adding it to a de-emphasis term of 5174 cannot fit.
The truncation wraps, and `o_207c` -- the de-emphasis state -- is written from
the wrapped value, so the next sample starts from it.

**Saturating there does not help, which is worth knowing.**  Compared at the
same gain, it makes the count worse: 9077 jumps at 11 kHz become 13327.  By
that point the signal is six times too hot -- rms 18928 of a possible 32767 --
and clipping it produces a square wave that alternates rails as readily as
wrapping did.  The level has to come down, not the peaks.

And that is where it still stands, because the level does not come down enough:
at 1 per cent source gain the filter states are all in range at every rate, yet
8 and 11 kHz still tear.  So there is a path into the output that the voice's
`gain` does not scale.

Ruled out along the way, each measured on its own:

* **Not the resonators.**  Their pole radii were instrumented.  Johnny's are
  *lower* than Peter's at the rates that fail -- 0.98195 against 0.98753 at
  11 kHz, a gain of 55 against 80 -- because his bandwidths are raised with his
  formants.  At 16 kHz, where he is clean, they are *higher*.
* **Not a formant above Nyquist.**  F4 is clamped already; clamping F2 and F3
  too changes nothing.
* **Not a table overrun.**  Johnny reads within table 8 where he tears and past
  the end of it at 16 kHz, where he does not.
* **Not a wrapped pole coefficient.**  None of the five Q13-to-Q15 scalings
  leaves int16 for any voice at any rate.

**So: a voice with a tract this short is a 16 kHz voice**, and the next thing to
find is what feeds `si` so hard at the lower rates, since that is the one term
the source gain does not reach.

### What MindMaker changed, which is nothing

The obvious suspicion was that TextAssist sounded better because it had a
better synthesiser.  It does not have a different one at all.

Its `Syn.dll` carries **Centigram's synthesis tables byte for byte** -- tables 5,
6, 7 and 8 at both rates, the pulse table, and table 9, all found by content
rather than by address.  `Clapi.dll` beside it carries Centigram's ten-voice
adjustment table, equally unchanged.  The two DLLs together are the TruVoice
engine split in half: the linguistic front end in one, the synthesiser in the
other.

So the eight voices are not a different synthesiser's voices.  They are the
same resonators driven by MindMaker's own layer, which turns twenty-three
Klatt-style parameters into this engine's parameter tracks.  Whatever keeps a
voice at 308 Hz with a two-thirds vocal tract from tearing is in *that*
conversion, not in the filter bank, and a mapping that works must therefore
exist inside this engine too.  Ours is not it yet.

The measurement that says where ours goes wrong is this one.  At a source gain
of 1 per cent, with Johnny at 8 kHz:

| | gain 70% | gain 1% |
| --- | --- | --- |
| the parallel sum | 13954 | **13954** |
| plus the direct source path | 40292 | 6585 |

The source gain reaches the direct path and **does not reach the parallel branch
at all**: that branch is driven from the cascade output and the formant
amplitude tracks, neither of which a voice currently has any say over.  Scaling
it as well does bring the level down -- 8 kHz falls from 11453 jumps to 8636 at
30 per cent -- without removing them, and it costs a function call per sample
for every stock voice, so it is not in the engine.

That pointed at the formant *amplitudes*, and an amplitude trim was written and
measured: `filt_coef[20]`, `[22]`, `[24]`, `[26]` and `[28]` scaled once per
frame.  **It does almost nothing** -- at 12 per cent the jumps at 8 kHz fall
from 11630 to 11523 and the level by four parts in a thousand -- so it was taken
out again rather than shipped.

The reason is worth recording, because it corrects the reading above.  The
parallel resonators add their output *into* `dx`, which already holds the
cascade's; trimming the parallel amplitudes therefore trims a small part of a
large sum.  The 13954 arriving at the output stage is mostly **cascade**, not
parallel, and the earlier note that "the source gain does not reach the
parallel branch" -- while true -- identified the wrong branch as the problem.

So the energy is in the cascade, and what puts it there is still unaccounted
for: the source gain scales the cascade's input and yet the arriving level
barely moves with it.  That contradiction is the next thing to resolve, and it
is where this stands.

### Two things the diagnostic found on its way past

`voicediag` also reports how far each resonator table is read.  Table 8 is 700
int32, 2800 bytes, and **at 16 kHz both Wanda and Johnny read past the end of
it** -- to byte 3000 and 3016.  It is not what causes the tearing, since the
rates that tear stay inside it, but it is a real overrun in the 16 kHz path
this project added, and it wants fixing on its own account.

The `gain` field on a voice definition came out of this and is worth keeping.
It scales the voiced and the aspiration sources together, as a percentage, and
it is the knob a voice needs when its formants are raised far enough to drive
the bank harder than Centigram's ten ever do.  No voice defined here needs it
yet -- Frank asks the bank for less than Peter does -- so it sits at 100.

### IntonLevel

How far a voice's contour moves, as a percentage, and the second field OpenTV
adds to a voice definition.  A stock voice is 100 and the engine multiplies by
nothing, so this costs the corpus nothing.

It needed no new mechanism, because the engine already had one.  `stage2.c`
narrows the contour for fast speech -- `rate_pitch_pct` -- and does it in a way
worth copying: it scales the excursion and then **gives half of what it took
off back as a lift**, so the voice narrows around where it was instead of
sagging by half the difference.  `IntonLevel` multiplies into that same
expression, so the two narrowings compose and the lift covers both at once.
The accent step is intonation too, so it is scaled with the rest.

Measured out of the audio rather than read back from the field, on
"Is that really what you meant?  No, it was not.  I said something else
entirely." -- the 10th to 90th percentile of F0, by YIN:

| Frank at | median F0 | range |
| --- | --- | --- |
| 100 | 89.0 Hz | 8.15 semitones |
| **70** (`Frank.tav`) | 86.8 Hz | **6.09 semitones** |
| 55 | 85.9 Hz | 5.03 semitones |
| *Peter, for comparison* | 102.7 Hz | 8.15 semitones |

Two things to read off it.  At 100 Frank's range is Peter's to the hundredth of
a semitone, which is the field being genuinely inert at its default.  And the
median falls only 3.1 Hz across a 45 per cent narrowing, which is the lift
doing its job -- without it the voice would drop by half the excursion it lost
and the parameter would read as "quieter *and* lower".

`api_test` checks it through the audio, and needed a better instrument to do
it.  The autocorrelation tracker the contour and rate checks use is sound for
comparing a voice against itself, but it halves a doubled lag and never
corrects one picked an octave high, so harmonic picks sit in its tails -- and
how many depends on the formants.  Against Frank and Peter it could not tell
them apart at *any* percentile band from 5/95 to 40/60.  `yin_spread` is the
replacement, YIN's cumulative mean normalized difference in integer arithmetic
so the freestanding 32-bit build still needs no maths library; it puts the two
at 370 and 604 parts per thousand above monotone, a ratio of 0.61.

## The other language DLLs

American English and Castilian Spanish are decompiled; French, German and
Italian are not, yet.  All five share the *voice* layout, and
`tools/voicedump.py` reads them all, but English is not the same engine as the
other four, and that is worth knowing before assuming a language can be added
by swapping data.

### English is a different generation

| dll | linked | .text | .data | .bss |
|---|---|---|---|---|
| CGRM_DE | 1995-11-04 | 0x2bed0 | 0x1ef60 | 0x18398 |
| CGRM_FR | 1995-11-10 | 0x3188c | 0x198c0 | 0x19758 |
| CGRM_ES | 1995-11-29 | 0x2c248 | 0x24920 | 0x19628 |
| CGRM_IT | 1995-11-29 | 0x2cc80 | 0x25550 | 0x19948 |
| **CGRM_EN** | **1997-10-16** | **0x8a3f6** | **0xbbf50** | none |

The other four are all November 1995, within a month of each other, around
180 KB of code, and carry `.bss` and `.edata` sections.  English is two
years later, three times the code, and has neither.  English got a rewrite
the others never received.

They confirm it by what they share.  Of the functions this project
annotates in `CGRM_EN.DLL`, only 12-16% appear verbatim in `CGRM_ES.DLL`,
and by module the figure is zero for every stage -- stage 0 through 3,
`synth.c`, `generate.c`, `engine.c`, `preformat.c` all score 0/n.  The four
1995 engines, on the other hand, share 40-47% of their code *with each
other*, which is the same signature as two builds of one source: they are
one family, compiled separately per language.

### What is shared is the synthesiser

The back end is common ground.  These are byte-identical between the 1997
English engine and the 1995 Spanish one:

* `g_syn8k_*` and `g_syn11k_*`, the resonator tables
* `g_param_max` and `g_default_params`, the 22 tracks' ceilings and defaults
* `g_hold_atten`

while `g_dur_rate`, `g_pause_rate` and the voice adjustment table are not.
So the Klatt cascade and its parameter model are the same design across
both generations -- the formulas in "16 kHz, which the original never had"
describe either -- and what differs is the language front end and the
prosody and timing tables on top of it.

Adding Spanish therefore means decompiling the 1995 engine, not feeding
Spanish data to this one.  It is a smaller engine than the English 1997
build by about two thirds, and the synthesiser understanding carries over,
but the `Engine` struct layout, every address annotation and the whole
front end would be new work.  One decompilation would cover all four, since
they are one family.  Each
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

### What the 1995 engine kept, and what it reads

Now that Spanish is decompiled the comparison can go further than the
adjustment table.  `python tools/voicedump.py --compare TruVoice/CGRM_EN.DLL
TruVoice/CGRM_ES.DLL` walks the whole block field by field, and **277 of the
280 per-voice values are identical**.  The layout is identical too: the same
fourteen tables in the same order at the same offsets.

The three differing rows differ in exactly one of their fifteen columns each:

| voice | column | EN | ES | and so |
|---|---|---|---|---|
| 6, Melvin / Rogelio | `p19` | 8 | 10 | `par[19] = adj[14]`, written outright |
| 8, Wanda / Josefa | `p2+` | 15 | 21 | 6 more on track 2, to the same `0x6b` clamp |
| 9, Julia / Isabel | `F3%` | 21 | 17 | Isabel's third formant scales 4 points less |

Track 2 is the aspiration amplitude -- `es/prosody.c` turns it into
`filt_coef[16]` through `g_par0_a`, and `es/generate.c` mixes it in as
`((noise * coef[16]) >> 15) + t` -- so of the three, the audible one is that
Josefa is breathier than Wanda.  All thirteen tables after the adjustment block
are byte-identical.

Three of those thirteen are also *dead*.  Every absolute address in a DLL's
code carries a base relocation, and in both DLLs every relocation pointing into
the voice block points at a table's first element -- nothing reaches the block
through a base pointer, and nothing outside `.text` points into it at all -- so
the relocations settle which tables the code reads:

| table | refs in CGRM_EN | refs in CGRM_ES |
|---|---|---|
| `breath` | 1 | **0** |
| `f4max` | 2 | **0** |
| `pitch_scale` | 2 | **0** |

`breath`, `f4max` and `pitch_scale` sit in CGRM_ES holding the English values
and are read by nothing.  The 1995 engine was handed the voice block wholesale
and wires up eleven of its fourteen tables.  Two cost nothing, being uniform
even in English -- `f4max` is 4090 for all ten voices and `pitch_scale` 32766.
`breath` is the real loss: it is the one of the three that varies by voice
(0, 2, -80, -22, 11, -42, 26, -85, -85, -36, the same in both DLLs), so the
Spanish voices are missing a breathiness axis that their own data describes.

## 16 kHz, which the original never had

`Synth_InitFilters` picks between two sets of resonator tables, one for 8
kHz and one for 11.025, and there was no third.  They turn out to be
computable.  Tables 6, 7 and 8 are a Klatt two-pole section in Q13:

```
t6[i] = round( 8192 * exp(-4*pi*i/Fs) )     pole radius r, i = bandwidth/4 Hz
t7[i] = round( 8192 * exp(-8*pi*i/Fs) )     the r^2 term
t8[i] = round(16384 * cos(2*pi*8*i/Fs) )    2*cos(theta), i = frequency/8 Hz
```

Those reproduce **all 2120 values of both of the original's sets exactly**,
which is what makes evaluating them at a third rate trustworthy;
`python tools/gen_synth_hifi.py --verify` checks it.  The zero crossing of
table 8 lands on Fs/4 at both rates, which is what pins the 8 Hz step.

Three things are not tables and were missed at first, with an instructive
result.  `filt_coef[12]`, `[13]` and `[33]` are a fixed resonator, and
unlike every other coefficient `Synth_Frame` never rewrites them -- they
keep whatever the initial array gave them.  Left at the 11 kHz values a 22
kHz render rolled off 12 dB too steeply by 3 kHz.  Solving them across both
rates puts that resonator at 242 Hz with a 102 Hz bandwidth, and `syn_2038`
independently implies the same 102 Hz, which is what says the model is the
right shape rather than a curve fit.

Tables 0 to 5 are *not* per-rate.  The 11 kHz set is uniform where the 8
kHz one has substitutions in its first few entries -- table 2 entry 0 is
11354 at 11 kHz, which is also entry 7 and also the initial
`filt_coef[37]` at both rates, while 8 kHz uses 5000.  They are wideband
versus narrowband values, so the extra rate shares the wideband ones.  Tables 3,
4 and 5 and the constants `syn_203c`/`syn_2040` are set and never read at
all.

Table 9 is the one approximation: mildly rate-dependent, 0.25% across the
octave from 8 kHz to 11.025, with no exact formula found, so the wideband
values are used.

### And in the 1995 engine

The section above says the formulas describe either generation, and the Spanish
engine settled it: the same three formulas reproduce **all 2120 values of
CGRM_ES's own two sets exactly**, as they do CGRM_EN's.  In fact all twenty
tables the two engines select between are byte-identical, and so are the six
resonator constants, so the extension is not merely the same design but the same
numbers.  That is why there is one copy of the generated tables --
`src/syn_hifi.c`, behind `src/syn_hifi.h` -- rather than one per language, and
why each engine's `Synth_InitFilters` only has to point at them.

The three things that are not tables had to be ported with it.  Leaving
`filt_coef[12]`, `[13]` and `[33]` at the 11 kHz values is the same mistake in
Spanish as it was in English, and measurably so: the high band (4-7 kHz) against
the low (300 Hz-3 kHz) comes out at 1.14 with the 11 kHz resonator and 1.35 with
the 16 kHz one, on the same Spanish sentence -- the roll-off the English note
above describes, in the same direction.

### Does it work

Measured rather than assumed.  Pitch tracks correctly (F0 within 2% of the
11 kHz render at every point sampled) and duration is identical, so the
source and timing are right.  Spectrally, against the long-term average
spectrum of the same sentence at 11 kHz over 100 Hz to 3.5 kHz:

| | rms difference |
|---|---|
| 8 kHz vs 11 kHz, both Centigram's own | 4.0 dB |
| 16 kHz vs 11 kHz, ours | 3.7 dB |

Ours agrees with 11 kHz more closely than the original's own two rates
agree with each other.  That is not a fluke of tuning: what separates two
rates is largely fold-up, the energy above the narrower one's Nyquist
folding back into its top band, and 16 kHz is nearer 11.025 than 8 kHz is.
An earlier version of this ran at 22.05 kHz and measured 6.1 dB for the
same reason in reverse; the rate was brought down because the step from 11
kHz was larger than wanted, not because anything was wrong with it.  The
rate is one constant, `TV_SR_HIFI` in `src/engine.h`, plus a re-run of
`tools/gen_synth_hifi.py --rate`.

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
