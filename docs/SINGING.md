# Singing

TruVoice can sing, and the engine has nothing to do with it.

MindMaker's TextAssist shipped a demo text with two songs in it, written the way
DECtalk writes one:

```
\Eng:[:phone arpa TruVoice]
[H<100,20>e<600,20>-L<100,20>O<1500,20>
 H<100,15>e<600,15>-L<100,15>O<1500,15>]
\
```

`PHONEME<duration_ms,pitch>`, in the engine's own one-character-per-phoneme
alphabet.  The second song in that file is "What shall we do with the drunken
sailor", 156 notes of it.

## The phonemes are nearly this engine's, and the gap is two characters

`[:phone arpa TruVoice]` says ARPABET and is not: `W v T s e L` is not a
sequence of two-letter names, it is the compact alphabet
`tvtts_speak_phonemes` already took, the one where "hello" is `HeLO`.  The
likeliest reason for the word `arpa` is that the people using it had come from
the Creative Labs build of TextAssist and expected DECtalk's spelling.

**An earlier version of this file said the two alphabets were identical, and
cited a measurement that did not show it.**  The demo's phoneme strings render
byte-identically through `CGRM_EN.DLL` and through this port -- which proves the
two *engines* agree, and says nothing at all about whether a character means
what the score intended.  Both drop the same ones.

Feeding every code in TextAssist's own help table to the engine and timing it
finds five that make no sound.  Three are expected -- `1` and `2` are the stress
marks, and `H` is aspiration, which has no length of its own -- and two are real:

| TextAssist | as in | this engine |
| --- | --- | --- |
| `6` | or**I**g**I**nal | `\|` |
| `7` | pi**NG** | `~` |

Confirmed from the other direction, by asking this engine to spell the very
words the table cites: "ping" comes back `Pi1~p` and "original" `@Ri1Jz\|N@j`.
Everything else in the table matches exactly -- `3` hurt, `4` hear, `5` fire,
`@` upon, `q` button, `l` little, `c` lure, `w` law -- so the translation is
those two characters and nothing more.  `sing.c` does it in `tv_phoneme`.

Left untranslated the effect is not subtle: `MgN67` renders 1060 ms against
`MgN|~`'s 1658, so "morning" loses two of its five sounds.

## The engine ignores the score entirely

Handed `H<100,20>e<600,20>-L<100,20>O<1500,20>` -- 2300 ms of notes -- the
original renders **1.16 s**, barely longer than plain "hello".  `phonetic.c`
gives `<` and `>` unrelated meanings inside the bracket syntax and throws away
what is between them.  Nor is there an escape for it: the command set has no
duration anywhere in it, and `ESC[l`, which does reach the configuration bytes,
does not reach the parameter tracks -- two attempts at track 17 rendered
byte-identically to sending nothing.

So that product parsed the score in its own layer and drove the synthesiser
directly, which is why it ships `Syn.dll` split out from `Clapi.dll`.  What
follows is OpenTV's, not a decompilation.

## The scale

DECtalk's, established by playing with the original:

| pitch | means |
| --- | --- |
| `0` | a rest |
| `1` to `37` | a chromatic scale, **C2 (65 Hz) to C5 (523 Hz)** |
| `38` and up | **hertz, directly** -- which is what lets a score glide |

`tvtts_note_hz` converts one.  Note 1 being low C is the same 65.41 Hz that
turns up as the Spanish engine's pitch clamp: this family of engines counts
from low C.  The table checks itself -- note 22 comes out at 220 Hz and note 34
at 440.

## How it is done here

Two escapes, one of them new, and a compiler in front of them.

**`ESC[<n>d`** holds the next phoneme for `n` hundredths of a second, instead of
whatever the duration rules and the speaking rate would have given it.  It is
spent in `Stage2_Flush`, right beside the override the engine already had, and
it travels as an escape node so it lands between the phoneme before it and the
phoneme after.  `ESC[0d` hands the phoneme back to the rules.

Calibrated through this very escape, by holding one duration over strings of
five and twenty-five phonemes and differencing so the utterance's own ends
cancel:

| `n` | 1 | 5 | 12 | 25 | 40 | 60 |
| --- | --- | --- | --- | --- | --- | --- |
| ms per phoneme | 20.5 | 60.4 | 130.2 | 259.9 | 409.6 | 609.1 |

**ms = 10n + 10**, straight and flat the whole way.  Every phoneme honours it
equally -- vowels, nasals, fricatives and stops all measured within 15 per cent
of each other at the same `n`.

An earlier reading of this said `10n + 30`, and it was wrong.  It came from an
instrumented build that forced the duration on *every* node rather than only on
the phonemes a score names, so the boundaries paid it too and the intercept came
out twenty milliseconds high.  Twenty milliseconds a note is nothing to hear and
three seconds across a song: **measure the mechanism you are going to ship, not
the one you used to find it.**

**`ESC[<n>p`** already worked, and already worked *inline*: an escape between
two phonemes takes effect for the second and not the first.  It maps nearly one
to one onto hertz (85 gives 92, 300 gives 308).  Under `TVTTS_EXT_SING` its
ceiling moves from 200 to 250, which is 400 Hz to 500; the engine's own F0 clamp
is 500, so **C5 at 523 clamps** and the top semitone of the scale is not
reachable.

And the contour is switched off while a score is holding durations.  Without
that it rides on top of the note: a phrase written on G3 measured **251 Hz
against 196**, and the excess shrank as the pitch fell, which is a contour and
not an offset.  With it:

| note | wanted | sung |
| --- | --- | --- |
| 20 | 196 Hz | 193.3 Hz |
| 15 | 147 Hz | 144.9 Hz |
| 12 | 123 Hz | 121.0 Hz |
| 8 | 98 Hz | 97.6 Hz |

Within 1.6 per cent throughout, which is under a third of a semitone.

## Using it

```c
tvtts_sing(s, "H<100,20>e<600,20>L<100,20>O<1500,20>", cb, user);
```

A phoneme with no `<...>` keeps its ordinary length, so a score can mix singing
and speech.  `tvtts_sing_compile` is the compiler on its own, which is how the
tests check a score without listening to it.  `tv -G` sings a file.

English only, for the same reason the phoneme entry points are: the 1995 engines
do not share this alphabet.

## Timing

Both songs in that demo now render within one per cent of what they say:

| | written | rendered |
| --- | --- | --- |
| the four "hello"s | 9.40 s | 9.48 s |
| Drunken Sailor, 156 notes | 22.09 s | 21.89 s |

Getting there took fixing two things, and neither was in the engine.  The first
was the intercept above.  The second is that **a rest has to be caught before
the silence branch**: `Stage2_Flush` opens by handing a silent node to
`Stage2_Silence`, which gives it the pause the speaking rate calls for, and a
score that writes `_<1000,0>` means a full second of nothing.  The override sits
above that test now, which is worth 1.7 s of the sailor on its own.

## A note has to be held, not approached

Getting the total length right is not the same as getting the song right, and
for a while this had the first and not the second.  Tracking F0 through six flat
200 ms notes showed each one *climbing towards* its pitch and never arriving:

    99 ms 101 Hz -> 136 ms 105 -> 157 ms 108 -> 186 ms 111 -> 212 ms 115   (target 131)

A 400 ms note gets close enough that its median reads correctly, which is how
this hid behind phrase-length measurements.  A 100 ms note never lands at all,
and that is a melody smearing.

The cure is one line in `Stage3_Write`: while a score is holding durations,
`lead = start = target` for track 17, so the pitch is laid down flat rather than
travelled to.  Measured after:

| asked | got |
| --- | --- |
| 6 x 200 ms | 203, 209, 220, 206, 186 ms |
| 6 x 100 ms | 96, 107, 108, 105, 114 ms |
| 400 / 100 / 400 / 100 | 392, 113, 392, 87 ms |

**The awkward part was not the line, it was reaching it.**  The stages run as a
pipeline -- a node can be at stage 3 while a later node is already at stage 2 --
so the marker saying "a note is being held" cannot be one global.  Set only at
stage 2 it never reaches stage 3 and the change is a **silent no-op**, which
produced byte-identical output and was very nearly believed.  Set at every stage
it is clobbered, and stage 3 reads a *later* note's value: that sang a semitone
flat and failed the sung-pitch check.  It is `tv_sing_dur[5]`, one slot per
stage, written as the escape node passes each.

## The period is a whole number of samples, and nothing alternates

The engine makes F0 a whole number of sample periods, so only `sr/N` exists.
Near middle C at 11025 that is 262.50 Hz or 256.4 and nothing between, and every
C in the scale lands 5.8 cents sharp because 11025 divides into 42, 84 and 168
exactly.  No table of escapes can fix that -- 262.50 *is* the nearest.

`ESC[<lo>;<hi>q` names the pitch outright in quarter-hertz, and the period is
rounded to the nearest whole sample.  **Both period slots get the same value.**

### What was there before, and why it had to go

Coefficients 30 and 31 are two period slots that `Synth_Generate` swaps at every
period boundary -- that is where the original puts its jitter -- so setting them
a sample apart puts the *average* half a sample from either.  That is how this
sang for several rounds, and it bought real accuracy:

| at 11025 | whole scale | notes 8-25 |
| --- | --- | --- |
| half-sample alternation | 4.4 cents mean, 13.6 worst | 7.7 worst |
| **whole samples** | **9.1 cents mean, 29.3 worst** | **13.7 worst** |
| 16000, whole samples | 7.4 mean, 23.5 worst | 8.9 worst |

And it is still the wrong trade, because of what it does between those numbers.
The oscillator ran 56 samples, then 57, then 56, for the whole length of a note,
and a sample is a wide interval -- 31 cents at G3.  Measured on a sustained
note with the waver off, the period is now a **single value**: 113 at G2, 89 at
B2, 75 at D3, 56 at G3, 42 at C4, with no second length anywhere.  Before, every
other period was a sample longer.

The spectrum said that artefact was 57 dB down, and that measurement is what
kept it alive through three rounds of listening reports.  It was the wrong
instrument: a two-period alternation has very little *energy* away from the
fundamental and is still plainly audible as a warble, because the ear tracks
period length directly.  **Dither is dither however it is dressed up**, and a
note a few cents flat and steady beats one that is exactly right on average.

The same reasoning had already retired a fractional-remainder version of the
same idea, which spent leftover 1/256ths of a sample as whole samples when they
came due; that one gave exact average tuning and a 31-cent swing period to
period.  Alternating two slots was the smaller version of the same mistake.

### The waver still moves the period, and that is not the same thing

With vibrato on, the period does change -- it has to, or there would be no
vibrato -- but it changes *slowly*.  Measured at G3, the oscillator holds 56 for
twelve or thirteen periods and 57 for three or four, repeatedly: a modulation at
the waver's own rate, not an alternation at the period rate.  With the waver off
it holds 56 and never moves.  That is the line between vibrato and dither, and
`tv -B 625,0` or `tvtts_set_vibrato(625, 0)` will show it.

Where the grid is too coarse to carry the waver at all it is suppressed rather
than allowed to snap; see below.  The result is that the waver works up to about
note 20 at 11025 and thins out above it.

## The scale is a measured table, not a formula

The engine turns pitch into a whole number of sample periods, so only certain
frequencies exist at all, and the escape carries half the pitch, which thins
them further.  Rendering a held tone at every escape from 25 to 243 and
measuring what comes out shows how coarse the grid is:

| near | step between neighbouring escapes |
| --- | --- |
| 125 Hz | 27.3 cents |
| 195 Hz | 30.4 cents |
| 300 Hz | 47.5 cents |
| 450 Hz | 70.7 cents |

No formula can beat the grid, and the one here before -- a flat ratio correcting
for the engine singing a little under -- did worse than the grid allows, because
it landed wherever the arithmetic fell.  **That is what makes a scale sound out
of tune: not the average error but its unevenness**, every note off by a
different amount and so every interval wrong.

`sing.c` now names, for each note, the escape whose *measured* frequency is
nearest true equal temperament -- **one table for every sample rate**.  The
pitch an escape asks for is in hertz, not samples, so the same escape means the
same note at any rate; only the rounding to a whole period differs.  Fitting a
table to each rate separately tunes each marginally better and makes them
disagree with *each other* more, which is worse than useless: a score should not
change key when it is rendered at a different rate.

| over notes 1 to 35 | 11025 | 16000 | the two rates apart |
| --- | --- | --- | --- |
| the old formula | 19.1 c | -- | -- |
| a table per rate | 11.1 c | 9.4 c | 11.3 c (worst 41.8) |
| **one table** | **11.1 c** | **10.3 c** | **7.1 c** |

Nine tenths of a cent given up at 16 kHz, and the disagreement halved.
Measured back through the finished compiler: 11.4 and 10.1 cents, the rates
9.7 apart.  That is about half a grid step, as close as this engine goes without
sub-sample periods.

A pitch written as hertz rather than a note -- 38 and above, which is what a
glide uses -- has no table entry and keeps the ratio correction.

## The ceiling is a byte, and 500 was a round number

The pitch escape used to stop at 243, which is 486 Hz, on the strength of a
reading that said 495 and 500 came out at **408** -- a wrap, four semitones
adrift.  That reading was wrong.  Swept properly, with the clamp out of the
way, the escape is clean the whole way up:

| escape | 241 | 242-252 | 253-255 | 256 |
| --- | --- | --- | --- | --- |
| asks for | 482 Hz | 484-504 | 506-510 | 512 |
| sings | 459.4 | 479.3 | **501.1** | **81.7** |

408 Hz is what escape 212 to 214 sings, which is where that number came from.
The step pattern is the `sr/N` grid -- 459.4, 479.3, 501.1 are 11025 over 24,
23 and 22 -- so the top of the range is coarse, but it is not wrong.

What is real is the wrap one step further on, and it is the storage: stage 2
ends `n->b15 = (uint8_t)(pitch >> 1)`, and track 17 is a byte of the same half
pitch, so **510 Hz is the last pitch that exists**.  256 is not 512 Hz, it is
nothing.

So `TVTTS_EXT_PITCH` moves the engine's clamp from 500 to 510 and the command's
from 243 to 255, which is the whole of what the byte holds.  500 was the round
number; 510 is the limit.

**This is not what makes C5 reachable, and C5 is reachable.**  523 Hz would
need 262 in that byte and no extension can put it there.  A sung note does not
go through the byte: `ESC[<lo>;<hi>q` carries quarter-hertz straight to the
period, so every note of the scale is in tune regardless of where the clamp
sits.  Measured on a sustained note, the whole top octave:

| note | 31 F#4 | 32 G4 | 33 G#4 | 34 A4 | 35 A#4 | 36 B4 | 37 C5 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| wanted | 369.99 | 392.00 | 415.30 | 440.00 | 466.16 | 493.88 | 523.25 |
| sings | 367.50 | 393.75 | 416.04 | 441.00 | 469.16 | 490.03 | 525.04 |
| cents | -11.7 | +7.7 | +3.1 | +3.9 | +11.1 | -13.6 | **+5.9** |

The top three do drop about 4 dB against the notes below them, and that is the
engine's own doing rather than the clamp's: the source amplitude is scaled by
`filt_coef[30] >> 2`, which is the period in samples over four, and it steps
from 6 to 5 between A4 and A#4.  Sending a different pitch escape changes
neither the level nor the pitch -- measured both ways at every note from 31 to
37, identical.

## Phoneme input from inside the text

`[:phone TruVoice on]` and `[:phone TruVoice off]`, gated on `TVTTS_EXT_SING`,
are rewritten into `ESC[1I` and `ESC[0I` before the engine sees the text.  That
pair is the engine's phoneme-input mode, and it is all `tvtts_speak_phonemes`
has ever done -- it wraps the caller's string in the two escapes, which is what
`tts_SpeakPhoneme` did in the 5.1 builds.  What the engine has not got is a way
to ask for it from inside a document.

    [:phone TruVoice on]HeLO[:phone TruVoice off]

renders byte for byte what `tvtts_speak_phonemes(s, "HeLO")` renders.  Case and
spacing are free; anything that is not exactly the command is left alone, so
`a [b] c` and even `[:phone arpa]` still read as text.

That product's own demo writes `[:phone arpa TruVoice]`, which this does not
accept.  The `arpa` is a fiction -- these are one-character phonemes, not
two-letter ARPABET names -- and the likeliest reason for the word is that its
users had come from the Creative Labs build and expected DECtalk's spelling.

**It reaches the engine from the library, from `tv` and from the speak window,
and not through NVDA or the SAPI driver**, both of which strip inline commands
out of the text before any of it arrives.

### A phoneme-mode utterance colours the first word of the next one

Found while testing the above, and it is **not** the command's doing -- plain
`tvtts_speak_phonemes` does it just as well, so it predates all of this.

On one synth, speak phonemes and then speak text: the text renders differently
from the same text on a fresh synth.  Same length, 1645 of 39490 samples
different, worst sample 3688, and the difference is confined to **121 ms to
405 ms** -- the first word, and nothing after it.  It latches rather than
accumulating: a second phoneme utterance gives the result the first already
gave, exactly.  `tvtts_sing` does the same thing, since a score is phonemes.

The shape of it says stage 2's cross-utterance phoneme context is not cleared,
so the first phoneme of the next utterance is still being bent toward the last
phoneme of the one before.  `Engine_Reset` evidently does not reach it.  Not
chased further; `test_reuse` shows text after text is byte-identical, so it is
phoneme mode specifically.

## A note glides and wavers, which is DECtalk's model

The first version of this held a note flat on purpose, on the grounds that the
TextAssist demo does not waver.  That was the wrong call, and the reason is in
the demo itself: the score syntax is DECtalk's, so the thing that drove this
engine was almost certainly built against DECtalk's behaviour as well as its
notation.  Two pieces of that behaviour are what a flat note is missing.

**A note is reached, not jumped to.**  DECtalk sets a target and walks to it
linearly over sixteen of its frames, which is 100 ms.  A frame here is 10 ms at
every sample rate -- durations come out the same at 11025 and 16000 -- so that
would be ten frames.

**The default here is 70 ms rather than 100**, which is a little quicker than
DECtalk by ear and still a slide rather than a switch.  `tvtts_set_portamento`
and `tv -N <ms>` change it; 0 steps straight to the note, which stops sounding
sung.  The ramp is linear in frequency, so the time asked for is the time taken:
timing the stretch where the pitch sits strictly between two notes an octave
apart gives 65 ms for 100 and 42 for 70, the two differing by the same constant
that the measurement's own 36 ms window costs.

A pitch written out in hertz is untouched by this.  That is what a written glide
uses, and it travels over its whole phoneme however the portamento is set.

**A note wavers at 6.25 Hz by plus and minus 2.05 Hz.**  A free-running cosine,
not restarted per note, added after the glide.  The depth is in *hertz*, so it
narrows as the scale climbs: 38 cents at the bottom of the range, 15 in the
middle, 7 at the top.  That is DECtalk's choice and it is kept rather than
corrected to a constant interval.

**A pitch written out in hertz gets neither treatment the same way.**  DECtalk
sets its vibrato switch for a note from the table and clears it for a straight
line, and a straight line travels over the whole phoneme rather than over
100 ms -- which is what makes a written glide a glide.  The compiler marks those
pitches so the engine can tell them apart.

Measured on a three-second note, against the 6.25 Hz and 2.05 Hz asked for:

| note | mean pitch | error | depth | rate |
| --- | --- | --- | --- | --- |
| 8 G2 | 97.88 | -2.1 c | +-2.18 Hz | 6.26 Hz |
| 12 B2 | 123.47 | +0.1 c | +-2.07 Hz | 6.26 Hz |
| 15 D3 | 146.91 | +1.0 c | +-1.96 Hz | 6.26 Hz |
| 20 G3 | 195.68 | -2.9 c | +-1.73 Hz | 6.26 Hz |
| 25 C4 | 261.91 | +1.9 c | +-3.05 Hz | 6.29 Hz |

`tvtts_set_vibrato` changes both, and a depth of 0 turns it off; `tv -B
<rate>,<depth>` does the same from the command line.

Because the depth is in hertz it is widest exactly where this demo sustains
longest.  "Hello" descends, so its last and longest note -- 2250 ms on note 8 --
is the one carrying **+-36 cents**, where the first line's note 20 carries 18:

| | G2 98 Hz | B2 123 Hz | D3 147 Hz | G3 196 Hz |
| --- | --- | --- | --- | --- |
| DECtalk's 2.05 Hz | +-36 c | +-29 c | +-24 c | +-18 c |
| 1.20 Hz | +-21 c | +-17 c | +-14 c | +-11 c |

A constant *interval* instead of a constant number of hertz would even that out,
and would be a departure from DECtalk rather than a reading of it.  It has not
been done.

### The waver runs out of grid near the top of the scale

The period is a whole number of samples, so the step between neighbouring
pitches widens as the scale climbs: about 15 cents low down, 31 at G3 and
**over 80 at C5**, where a period is only 21 samples.  Above roughly 210 Hz the waver is smaller
than one step, and quantising something smaller than a step does not make it
small -- it makes it snap between neighbours, slowly and erratically.  Measured
before this was guarded, C5 wavered **74 cents at 2 Hz** where 7 cents at
6.25 Hz was asked for.

So a period is taken only if the pitch it really gives falls inside the waver
that was asked for, with half as much again for rounding; where it does not, the
note is sung without one.  Notes 31 and 34 measure a flat 0 Hz of waver as a
result, and C4 gets it as occasional 20-cent steps rather than a smooth curve.
The waver thins out towards the top of the range, and that is the engine's pitch
grid showing through rather than something that can be tuned away.  DECtalk met
the same wall from the other side: its vocal tract model keeps extra fractional
bits specifically, by its own comment, to preserve vibrato at high notes.

## Where the DECtalk reading came from, and what was taken

A DECtalk checkout sits in `/dectalk/`, gitignored, and it was read -- the model
above is its model.  What was taken is the *shape*: a 100 ms linear approach to a
note, a 6.25 Hz cosine at 2.05 Hz either side, vibrato on table notes and not on
straight lines, and the fact that it disables its own 3 and 5 Hz speech flutter
while singing.  No code was copied, and the two implementations do not resemble
each other -- this one has a whole-sample period grid to fight, which DECtalk does
not.

The checkout's per-file headers assert that the code "embodies the confidential
technology of FONIX Corporation", and an earlier version of this document took
that at face value and said no source had been read, which was not true even as
it was written.  Its own README gives a different provenance: the files were
shared by their original developer, the late Edward Bruckert, on the DECtalk
mailing list in 2015 and have been publicly preserved since.  That is the basis
on which it is being read here; it is the repository owner's call, and it stays
out of this repository either way.

Its note table is worth recording, because it is an independent check on ours
rather than something to copy.  DECtalk stores notes 1 to 37 as F0 in tenths of
a hertz on a middle-C-is-256 scale, then multiplies by 4190/4096 in singing mode
to reach A440.  Worked through, that lands within **1.5 cents on average and
2.1 at worst** of equal temperament -- every note about 1.5 cents sharp, because
4190/4096 is 1.02295 where 440/430.4 is 1.02230.  So DECtalk's scale and ours
agree, and what makes DECtalk sound out of tune is its renderer and its vibrato,
not its table.  Ours stays exact equal temperament.

## Five rules the durations obey

A phoneme lasts **10 ms per hundredth, exactly** -- but only under conditions
that took a long time to pin down, because measuring the same thing three ways
gave three different laws, which is the tell that the *shape* of a measurement
is wrong rather than its arithmetic.

**1. The escape must precede every phoneme, not only when the value changes.**

| measured over | law |
| --- | --- |
| a string of the **same** phoneme | ms = 10n + 10 |
| differing phonemes, **one** escape at the head | ms = 10n - 16 |
| differing phonemes, an escape **before each** | **ms = 10n, exact** |

The last two differ by 16.2 ms at *every* n from 3 to 45 -- one escape node's
worth, once per phoneme, independent of the duration.  The first differs again
because identical neighbours have no transition to overlap.  Emitting the escape
only on a change looked like an obvious economy and quietly shortened every note
whose length matched its predecessor's.

**2. Two durations the engine will not take.**  Sweeping every value from 1 to
60: **12 renders about 420 ms instead of 120**, behaving as though it were 42,
wherever it appears and whatever its neighbours are; and **1 crashes the
engine**.  Everything else is exact to a fifth of a millisecond.  In the sailor
the first was worth +299 ms at every note written `<120,...>`.  Why 12 is
special is not known; `safe_cs` steps around both and takes whichever neighbour
is nearer, which costs at most ten milliseconds.

**3. A space is layout.**  Every note says its own length, so a space must cost
nothing.  Left in, the engine makes it a word boundary and puts a pause of its
own choosing there -- *variable*, which wrecks a rhythm far more thoroughly than
it wrecks a total.  Perturbing one note at a time showed it plainly: notes away
from a space answered a +100 ms change to within 0.2 ms, the note after a space
answered +399 and the one before it -50.

**4. `-` is a tie, not a sound.**  This engine renders it as silence, so `He-LO`
puts a gap in the middle of "hello".  It holds the phoneme before it instead,
which is what a tie means and what keeps the bar its written length.

**5. An explicit zero is zero.**  The score rests between verses with
`_<0,100>`; left to the duration rules that became a pause of the engine's
choosing.

## `_` is a rest, and the engine has no such phoneme

Like `-`, `_` is that product's notation rather than a phoneme code -- it is not
in TextAssist's own table either -- and this engine has no character for it.
Written through as a phoneme it did nothing at all, and worse than nothing:
`A<300,20>_<1000,0>A<300,20>` came out **shorter** than the two notes with no
rest between them.

The engine does have a pause, and in phoneme mode its argument is simply
hundredths of a second of silence -- `ESC[10s` is 100 ms, `ESC[30s` 299,
`ESC[69s` 688, ten milliseconds a step with no offset.  (`tvtts_break_sequence`
adds 49 to its argument, which is right for the text path and wrong for this
one; that is why the first attempt looked like it had a 489 ms floor.)

A rest's pitch is read and thrown away, which is right: there is nothing to
sound.  Measured, a rest between two notes is exact -- 300 ms asked gives 299,
1000 gives 998, 2000 gives 1995 -- and a rest at the end of an utterance is a
flat 390 ms short, which is added back; see the trim section below.

### A zero length means "not said", and for a rest that is not nothing

The sailor writes `_<0,100>` twice, at the two places a phrase break belongs:

```
S<110,23>A<218,23>L<106,25>4<189,25>R<72,25>
_<0,100>
3<300,23>L<120,20>E<120,20>
```

This produced no pause at all for a long time, on the reading that a length of
zero is zero.  That reading is wrong, and DECtalk's own code says so in a
comment: in `ph_sort.c` the two fields are taken as "User-specified dur **if
non-zero**" and "User-specified f0 **if non-zero**", and a zero means the value
was not given, so the phoneme falls to the rules.  Its rules give a silence a
`dpause` of 14 or 15 frames depending on what follows, floored at 2 and scaled by
the speech rate -- about **90 to 96 ms** at 6.4 ms a frame.

So a rest with no length of its own gets 100 ms here.  That is DECtalk's number
rounded, and it is also what the demo's `_<0,100>` says if its two arguments are
read the other way round, which is the likelier typo.  Both readings land in the
same place, which is a comfortable thing for a guess to do.

A zero length on a *phoneme* is still nothing.  Left to the rules it became a
sound of the engine's choosing in the middle of a bar, and the demo never writes
one, so there is nothing to weigh against that.

### A pitch of 0 is not honoured on a phoneme, and is not faked

`user_f0[n]` is "User-specified f0 if non-zero" and only a non-zero value turns
DECtalk's singing mode on, so a pitch of 0 means *time this one, do not sing it*.
On a rest that is the whole of it -- a rest has no pitch to take, so `_<1000,0>`
and `_<0,100>` differ only in how long they are silent.

On a phoneme it is not implemented.  Handing the exact pitch back is one escape
and it changes nothing audible: the coarse pitch escape has already moved the
engine's *base* pitch to the last note, and the duration escape has already gated
the contour off, so the phoneme comes out flat at the previous note's pitch
either way -- measured, the period histogram is identical to a single period.
Doing it properly wants two things this layer has not got: a way to force a
duration without gating the contour, which is an engine change, and the pitch the
caller configured, which only the port knows.  The demo never writes a pitch of 0
on anything but a rest.

One wrinkle, and it is the engine's: **a pitch change across a rest shortens
it**, by a flat 390 ms whether the rest is 300 ms or 3000.  It is not one escape
eating another -- every ordering of the two was tried and all four behave the
same -- so it is the pause being trimmed around a phrase boundary.  The
compiler adds the 390 back when the notes either side differ.  Measured after:

| rest asked | same pitch | pitch changes |
| --- | --- | --- |
| 300 ms | +299 | +299 |
| 1000 ms | +998 | +998 |
| 2000 ms | +1995 | +1995 |

## H cannot stand alone

`H` is aspiration on the onset of the vowel after it, and it will not survive
being made a node of its own:

| written | result |
| --- | --- |
| `[60d e` | 598 ms |
| `[60d He` | 1197 ms -- H sounds, but the escape feeds **both** |
| `[60d H [10d e` | 100 ms -- H silently thrown away |

So an escape between the H and its vowel loses the aspiration, and no escape
between them makes the pair cost twice what it says.  The compiler holds an H
back and writes it immediately before the phoneme it belongs to, giving the pair
the H's own length and letting the vowel make up the rest.

**What the vowel still owes is held, not written again**, and getting that wrong
is what "hey-ey" was.  The pair costs 2 x hcs of the note; for a while the
remainder fell through into the ordinary emission below, which wrote a *second
copy of the vowel* -- a re-articulation the score never asked for.
`H<120,20>A<600,20>` in the sailor said its vowel twice, and
`H<100,20>e<600,20>` did it to "hello", where it also showed up as the vowel not
being held smoothly: tracked through line one, the e's F2 wandered 1546, 1604,
1698, 1742, 1707, 1550 as the second copy articulated, where it now sits flat at
1555 for the whole 540 ms.
`H<100,20>e<600,20>` compiles to `[10d He [50d e` and costs exactly what
`e<700,20>` does.

## A note too long for one phoneme is held, not repeated

Sixty hundredths is the most a single phoneme will take -- the engine saturates
around 65 and stops producing anything above about 80.  The first answer here
was to sing a longer note as repeats of its phoneme, and that was wrong: a
repeat **re-articulates**.  Spectral flux through a held `O` is flat across one
600 ms chunk and spikes tenfold at 673 ms and 1219 ms once it is three, and that
is the seam you can hear.

### Singing a long note as repeats of its phoneme: built, measured, reverted

A note too long for one phoneme is held.  Singing the remainder as *more of the
same phoneme* is the obvious alternative and is what DECtalk effectively does,
and it has now been built twice and taken out twice.  This is what stopped it,
because the numbers are worth keeping even though the code is gone.

**The engine articulates across every pair of phonemes.**  Three 600 ms chunks
of `A` fall from -2.2 dB to -15.3 and take about 150 ms to recover -- **13 dB at
each join**, which on a sustained note is a pulse twice a second.

**What carries that is the formant frequencies, not the bandwidths.**  The
bandwidths look certain: B2 walks 60 to 125 across a join and B3 100 to 200, and
a wide bandwidth is a shallow resonance.  Holding them barely helps.  Freezing
one group of parameters at a time across the join settles it:

| held across the join | level range |
| --- | --- |
| nothing | 16.5 dB |
| p0-p2 amplitudes | 13.7 |
| p3-p8 | 16.5 |
| **p9-p12 formant frequencies** | **4.3** |
| p13-p15 bandwidths | 13.5 |
| p16-p21 | 16.5 |
| everything | 2.5 |

The engine walks the formants toward a boundary position -- F1 from 105 down to
74 -- which moves the resonances off the harmonics they were reinforcing.

**Stage 3 is the wrong place to stop it.**  Setting lead, start and target to
one value for all 22 parameters leaves the join exactly as it was: the travel at
a boundary is drawn by the rule passes, not from those three.

**So a repeat needs the parameters held, and that needs a frame to hold.**
Every rule for choosing one failed on some phoneme:

| how the frame was chosen | what it did |
| --- | --- |
| the note's first frame | sustained the transition *into* the phoneme -- `L<100,20>O<1500,20>` held an L for a second and a half, and the O was never heard |
| a fixed count of frames in | landed in the wrong phoneme, because one duration escape feeds both an H and its vowel; holding an H frame put "hay" 30 dB down |
| the rules' own `start` value | cannot be time-aligned: stage 3 runs far ahead, so a later chunk overwrote the answer before the frames needing it arrived.  Restricted to the first chunk it still left a 11 dB step |
| the frame where the stream settles | the threshold trades phonemes against each other.  At 3, `A` never settled and held its release; at 10, `A` came right at 4.9 dB and `O` went to 15.4 and `a` to 14.3 |

Summed over the 22 parameters a settled vowel drifts about five a frame and a
release moves fifteen to twenty, so the two do separate -- but not by one
threshold that suits every vowel.  The hold needs the same choice made once and
makes it with a single heuristic that measures well (see what `g` holds), so it
is the better engineering until something better than a heuristic turns up.

**The per-phoneme ceiling is not where the comments said either.**  Raising the
duration escape's clamp to 255 is not enough: the node reaches stage 3 with its
full count and room available -- `Tracks_Op` passes, and stage 3 *backs off and
retries* rather than wrapping when it fails -- yet only sixty frames are ever
written.  The limit is inside the stage 3 rule passes and was not found.  So
"stage 3 asks `Tracks_Op`, which is why it is sixty" is wrong about the mechanism
even though sixty is the right number.

### The 390 ms trim belongs to the last rest, not to a pitch change

`SING_REST_PITCH_MS` existed because a pitch change after a rest or a hold was
measured to shorten it by a flat 390 ms.  That reading was wrong in both of its
parts, and the truth is simpler.  Measured on this build:

| a 500 ms hold, followed by | delivered |
| --- | --- |
| nothing | 1247 ms |
| a phoneme at the same pitch | 1098 ms |
| a phoneme after a pitch change | **1098 ms** -- the same |

so a hold loses nothing to a pitch change; and for a rest:

| rest asked | with a note after it | with nothing after it |
| --- | --- | --- |
| 300 ms | +299 | **-90** |
| 500 ms | +499 | **+110** |
| 1000 ms | +998 | **+609** |
| 2000 ms | +1995 | **+1606** |

A rest with a note after it is exact.  A rest with *nothing* after it is a flat
390 ms short at every length.  So it is the utterance ending rather than the
pitch moving, and the engine is trimming the silence it was about to end on.
The constant is now `SING_END_REST_MS` and it is added to the last rest, where
it is lost.  "Hello" lands on 11.47 s against the 11.46 its score asks for.

The sailor is 330 ms long where it used to be 180 short -- both inside one and a
half per cent, and the compensation is now spent where the engine actually takes
it rather than on whichever hold happened to precede a pitch change.

## A note needs its attack, which flattening the amplitude had taken away

Track 17 is held flat across a sung phoneme, because a note is held at its pitch
rather than approached.  Track 0 -- the voicing amplitude -- was held flat too
for a long time, and that turned out to be a mistake that only showed up once
the rests started working.

It went in to cure a 500 ms fade on a long note.  It no longer cures anything:
with the natural curve back, an 1800 ms note measures **6.5 dB of range either
way**, and the envelope differs only in its first 80 ms.  Whatever caused that
fade was fixed by the hold and glide work since.

What flattening the amplitude *does* do is remove the attack, because the attack
is exactly the move from `start` to `target` that flattening abolishes.  Into
another phoneme that costs nothing -- the level is continuous across the join.
Out of **silence** it is a note beginning at full level from nothing.  Measured
out of a rest, the level reached half its peak in:

| | |
| --- | --- |
| amplitude flattened | **6 ms** |
| the engine's own curve | 21 ms |

Six milliseconds from nothing to half level is a click, and that is what it
sounded like.  The curves are twelve decay tables indexed by how many frames the
move spreads over, so the engine's own choice is already short; capping it made
no difference to either number, which is how it became clear the flattening was
all cost and no benefit.

The release on the way *into* a rest came back with it, for the same reason.

## What is not right yet

**`SingF0Rate`** is a per-voice parameter in the `.tav` files -- Bill 1.0,
Frank 0.508666, Johnny 1.90365, and reciprocal pairs among the rest -- and is
still not implemented.  Reading DECtalk narrows the guess rather than settling
it: DECtalk's vibrato rate is a constant in its code and not a speaker
parameter, so a per-voice "F0 rate" is more likely a transposition than a
waver rate.  Frank at 0.5087 would put him most of an octave down, which suits
the voice.

**A phoneme-mode utterance colours the first word of the next one**, which is
written up above.  It predates the singing work and is not chased yet.

**A held note is steady in everything but pitch**, because the engine will not
give one phoneme more than 600 ms and the alternative -- repeating it -- does not
work here; the measurements are above.  Lifting the per-phoneme ceiling inside
stage 3 is the thing that would actually fix it.

**The sailor runs 330 ms long**, where before it ran 180 short -- 1.5 per cent
either way, and "hello" is now exact.  The 390 ms is spent where the engine
takes it; what is left is a residual spread across 156 notes.

**The scale is as in tune as whole sample periods allow**, which is 9.1 cents
mean and 29.3 worst at 11025, or 6.2 and 13.7 over the range a song actually
uses.  Getting closer means varying the period, and varying the period is
audible; that trade has now been made in both directions and steady wins.  A
higher sample rate is the one lever that improves both at once -- 16000 gives
7.4 mean and 23.5 worst -- and 22050 would halve the error again.

**The waver thins out above about note 20** at 11025, because a whole-sample
step there is already 31 cents against the 18 the waver asks for.  Below that it
comes out at the right rate and depth; above it the note is sung steady.
