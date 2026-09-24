# tvtts: TruVoice as a library

`include/tvtts.h` is a flat C API over the decompiled engine, for screen
readers and anything else that wants the synthesizer without SAPI.  No COM,
no registry, no window messages, no threads, no audio device.  The only
thing it asks of the host is a C runtime.

```
build/harness/tvtts.dll      the library (imports msvcrt.dll and nothing else)
build/harness/libtvtts.a     its import library
build/harness/tv.exe         the command line front end, built on the API
build/harness/api_test.exe   the tests, linked statically
build/harness/api_test_dll.exe   the same tests across the DLL boundary
```

Exports are plain cdecl names, undecorated, so `ctypes` and P/Invoke bind
them without a mangled-name dance.

## The contract

The caller drives synthesis on its own thread and plays the samples itself.
That is deliberate: a screen reader already owns its output device, its
ducking and its cancellation, and a library that owns a thread and a device
fights it for all three.

```c
tvtts_synth *s = tvtts_create(11025);
tvtts_set_voice(s, 0);
tvtts_speak_utf8(s, "Hello world.", on_event, NULL);
tvtts_destroy(s);
```

`tvtts_speak_*` blocks until the utterance finishes.  One callback receives
audio and index marks interleaved in stream order:

```c
static int on_event(const tvtts_event *ev, void *user)
{
    if (ev->type == TVTTS_AUDIO)
        queue(ev->samples, ev->count);        /* int16 mono */
    else if (ev->type == TVTTS_MARK)
        bookmark_at(ev->mark, ev->sample_pos);
    return should_stop();                     /* non-zero aborts at once */
}
```

Returning non-zero aborts synthesis and `tvtts_speak_*` returns 1.  There is
no lock and no race: cancellation is just the callback saying no.  The synth
is left ready for the next call, which is tested -- an utterance spoken
after an abort is byte-identical to the same utterance on a fresh synth.

## Index marks

The engine takes marks inline in the text, so a caller with a sequence of
(text, bookmark) pieces writes an escape between them:

```c
char esc[16];
tvtts_mark_sequence(esc, sizeof esc, 42);     /* ESC [ 4 2 i */
```

**Mark 0 is reserved.**  The engine uses it as its own end-of-item marker,
and it also tells the node to stop reporting, so the library never passes it
on.  Number your marks from 1.

### What sample_pos means

`sample_pos` is where the mark's audio lands in the stream, which is *not*
where the event arrives.  A mark fires when stage 3 reaches it, and stage 3
runs ahead of the synthesizer -- it has already written the mark's frames
into the parameter tracks, but they have not been turned into samples yet.
The queue is twelve frames deep, about 120 ms, at either sample rate.

Reporting the raw output position would put a screen reader's caret 120 ms
early on every bookmark, so the library corrects it: the earliest of the 22
track write positions is where the mark's frames sit, and a frame is a
hundredth of a second of output.  Checked against a mark placed before the
first word of an utterance, the corrected position lands within one frame of
where speech actually starts.

A mark is then held until the stream reaches it, so when one arrives every
sample before `sample_pos` has already been handed over and `sample_pos` is
exactly how much audio you have been given.  That is the whole reason it is
done here: otherwise every caller would have to keep its own list of
pending marks and split an audio buffer it had not received yet.  Buffer
the audio, flush it with a completion callback when a mark arrives, and let
the host raise the bookmark when playback reaches that point -- which is
what `nvda-addon/` does.

## Text

The engine reads cp1252, because that is what SAPI's `WideCharToMultiByte`
handed it.  `tvtts_speak_utf8` and `tvtts_speak_utf16` do the same
conversion, so a character with no cp1252 byte becomes `?` exactly as it did
then, and the 27 places cp1252 differs from Latin-1 (curly quotes, dashes,
the euro sign) survive rather than being mangled.  `tvtts_speak_bytes` takes
cp1252 directly.

## Phonemes

Both directions of the engine's own phoneme alphabet -- one character per
phoneme, "1" and "2" for primary and secondary stress:

```c
char p[256];
tvtts_text_to_phonemes(s, "hello", p, sizeof p);   /* p = "&HeLO1." */
tvtts_speak_phonemes(s, "HeLO1", on_event, NULL);  /* says "hello" */
```

`tvtts_text_to_phonemes` is snprintf-shaped: it returns the bytes needed
including the terminator, truncates rather than overruns a short buffer,
and takes a NULL buffer to measure.  It works by running the text through
the engine and discarding the audio, because the trace is a side effect of
synthesising, so it costs what speaking costs and advances the synth the
same way -- settings are untouched, and the utterance after one is
byte-identical to the utterance before.

`tvtts_speak_phonemes` brackets the string with `ESC[1I` and `ESC[0I`,
which is precisely what `tts_SpeakPhoneme` does in the 5.1 builds of the
original.  This is not the bracket notation (`[HH AH L OW]`) the rule
interpreter also understands; that is a separate path.

The round trip is close but not guaranteed exact.  Speaking the phonemes
this returns for "hello" gives byte-identical audio to speaking the word;
for "hi" it matches in length and differs by at most 192 of a full-scale
32,768, a small difference in the final fall.  docs/VOICES.md has the
measurements and where the escape was found.

## Output rate

Three, as an index rather than hertz, because each needs its own resonator
tables and nothing between them is available:

```c
tvtts_set_sample_rate(s, TVTTS_SR_22K);   /* 0 = 8k, 1 = 11k, 2 = 22k */
```

11 kHz is the default and is what the voices are known to sound like; 8 kHz
is the original's narrowband set; 16 kHz is OpenTV's own, with tables
computed from formulas that reproduce both of the original's sets exactly.

Changing rate re-initialises the filters and the output stage, so it is
refused part way through an utterance -- call it between them.  Going out to
16 kHz and back returns byte for byte to where it was.

Voice, pitch, rate and volume survive the change.  They have to be put back
deliberately: the re-initialisation restores the engine's own defaults, and
the values the library caches to avoid redundant work would otherwise say
they were still applied.  Note that the getters report what was asked for
rather than what the engine holds, so they cannot themselves detect the two
drifting apart.

## Voices

Ten, indexed 0..9, named from the engine's own table: Peter, Sidney, Eager
Eddie, Deep Douglas, Biff, Grandpa Amos, Melvin, Alex, Wanda, Julia.
That is the order the engine registers them in, which is *not* the order
the name strings sit in memory -- see docs/VOICES.md.  Peter and Grandpa
Amos land in the same place under either order, so checking those two
alone proves nothing, and did not.

`tvtts_voice_rate` and `tvtts_voice_pitch` give a voice's defaults, which is
what to reset to after the user has been moving sliders.  docs/VOICES.md
has both the ordering and what a voice actually is.

## Rules

* A `tvtts_synth` is **not thread safe**.  Give each thread its own; they
  share nothing.
* `tvtts_add_lexicon` is **process-global** -- the engine's user lexicon is
  one static table -- and is not thread safe against anything.  Add entries
  before you start speaking.
* Settings persist across utterances, as SAPI's did -- and so do the
  inline escapes.  `ESC[<n>p`, `ESC[<n>r` and the `ESC[<n>N` flags write
  the same state the setters do and stay in effect on that synth until
  something changes them back.  Use the setters to restore; they take the
  full pitch where the escape takes half of it, so they can express odd
  values the escape cannot.
* **Pitch is 50..500.**  Stage 2 clamps every node it emits to that and
  stores the value halved in a byte, so 500 is the largest pitch whose
  half still fits.  Outside it the base value still moves the accented
  nodes -- the audio keeps changing down to about 28 and up to about 516
  -- but nothing there is a pitch the engine can hold.  `tvtts_set_pitch`
  does not enforce the range, because the corpus checks the original at
  40; `tvtts_pitch_sequence` reaches only 400, since the `ESC[<n>p` escape
  takes n 25..200 and doubles it.  `TVTTS_PITCH_MIN` and `TVTTS_PITCH_MAX`
  are in the header.
* **Do not put an escape after the last word.**  The engine reads the
  final word of an utterance by different rules, and an escape after it
  means it is no longer final: a lone "a" becomes the article rather than
  the letter's name.  Emit marks before the text they follow, and report
  any that would trail once the audio has been delivered.
* **Rate is 46..400 words per minute** with the extensions on, 46..253
  without.  The engine picks
  a 26-row table with `(wpm - 46) >> 3`, unsigned.  Below 46 that wraps
  to an index of about `0x1fffffff` and reads wildly -- the original
  crashes and so does this, so `tvtts_set_rate` floors it.  Above row 25
  the original ran off the end of the table -- a sentence at 254 wpm came
  out ten times longer than at 253 -- and `TVTTS_EXT_RATE` replaces that
  with rows of its own, up to `TVTTS_RATE_MAX_EXT`.  With the extension
  off the original's behaviour is still there, because the corpus checks
  it at 260 and 400.  46..76 all select the slowest row.
* `tvtts_set_compat` is for reproducing the original exactly, not for normal
  use: it turns off the two front-end passes the original exposed through the
  registry, and sets how many NUL bytes follow the text.  That last one
  changes the audio, because the feed routine branches on the total length.

## Extensions

OpenTV is a decompilation, not a patch, so it can fix things the 1997
engine got wrong.  Anything that changes what the engine does sits behind a
flag, all of them on by default:

```c
tvtts_set_extensions(0);              /* the 1997 engine, exactly */
tvtts_set_extensions(TVTTS_EXT_ALL);  /* the default */
```

This is not caution for its own sake.  The 335-configuration byte-exact
corpus is the only evidence the decompilation is correct -- it is what
caught six real decompilation bugs -- and it works by comparing against the
original binary, which has no extensions.  So `tools/difftest.py` passes
`-C` and the corpus keeps proving the engine underneath is right, while
callers get the better behaviour by default.  Improvements are tested
separately, in `tests/api_test.c`.

The flags are process-wide rather than per-synth, like `tvtts_add_lexicon`:
the engine's own Stage 2 keeps its working state in globals, so one synth
was never independent of another here.

### TVTTS_EXT_RATE

The engine's rate table has 26 rows, 46..253 wpm in steps of eight, indexed
by `(wpm - 46) >> 3`.  Above row 25 it indexed off the end and read whatever
followed, which made speech *slower* and stranger rather than faster: the
same sentence took 38,544 bytes at 253 wpm and 391,864 at 254.

The extension clamps the index into the table and, for the rows past the
original's, shortens durations instead.  The final duration is
`(max - min) * acc/100 + min` from `g_phone_dur`, so the per-phoneme
*minimum* is a floor the rate table can never get under, and by row 25 every
duration is already sitting on it -- which is why the original's fastest row
only ever managed 1.86x.

Going faster therefore means taking some of the minimum too, and **how** that
is taken is what decides whether fast speech is intelligible.  Scaling the
whole duration uniformly reaches about 3.4x and mumbles, because a consonant
squeezed below its minimum stops reading as a consonant.  The minimum and the
part above it are scaled separately instead: the minimum keeps 70% of its
length at the top row while the variable part goes to 40%.  Consonants are
almost all minimum and vowels are mostly the part above it, so the time comes
out of the steady parts.  That costs top speed -- about 2.9x rather than
3.4x -- and measures 7% more spectral flux at identical duration, flux being
a proxy for how distinct one segment is from the next.

| wpm | extension on | original |
|---|---|---|
| 150 | 1.00x | 1.00x |
| 253 | 1.86x | 1.86x |
| 260 | 2.28x | 0.18x (ten times longer) |
| 330 | 2.86x | 0.52x |
| 400 | 3.39x | 1.80x |

Every row the original had is left alone, so 46..253 wpm is bit-for-bit what
it always was -- verified against `CGRM_EN.DLL` itself, not just against the
classic build.  The voices still sound the way people know them; only the
range that used to be broken behaves differently.

### TVTTS_EXT_CLARITY

Fast speech slurs for a physical reason: a formant resonator with a narrow
bandwidth has a long impulse response.  Shorten the phonemes enough and the
resonator is still ringing from the last one when the next arrives, and the
two smear together.

The fix is to widen the bandwidths as the rate climbs, so each resonator
settles inside its own phoneme.  That maps onto this engine directly: the
byte offset into tables 6 and 7 *is* the bandwidth in hertz, so widening is
a plain scale of the offset, applied to F1, the nasal branch and F2/F3/F4
in `Synth_Frame`.

The idea and its constants come from Tamas Geczy's TGSpeechBox, which does
the same to its cascade bandwidths above a speed threshold -- see NOTICE.
His ramp starts at 2.5x the base rate and reaches a 1.3x widening at 4.5x.
This engine tops out near 3.4x, so taking 4.5 literally would deliver only
44% of the effect at the fastest rate there is; the ramp is stretched to
finish at the last rate row instead.  Same curve and endpoints, fitted to
the range this engine has.

There is a second half to it, aimed at intonation rather than clarity.
`Stage2_Contour` lays its pitch contour across the phrase's *phoneme count*
-- `pitch -= (pos * v) / total` -- so the shape is rate-invariant by
construction and completes whatever the speed.  That is the opposite of
TGSpeechBox, whose declination is hertz per second and gets multiplied by
speed so that it finishes in time.  Nothing is missing here, then; the
problem is the other one.  The same excursions are traversed three times
faster, and a pitch movement crammed into a third of the time does not read
as the same intonation.  People flatten their pitch when speaking quickly
rather than moving it faster, so the range is narrowed as the rate climbs --
with the lost half given back as a lift, so the voice keeps its centre
instead of sinking.  Measured over two sentences at 400 wpm, the 10-90
percentile spread of F0 goes from 50.9 Hz to 32.9 Hz while the median stays
within 1.5 Hz.

The bandwidth half starts at about 310 wpm and the pitch half at the first
added row, so everything the original could reach is
untouched, and it changes timbre and intonation only -- the duration of an utterance at a
given rate is identical with it on or off, to the sample.  `TV_BW_ROW_START`
and `TV_BW_MAX_Q8` in `src/engine.h` are where to tune it.

## How this is tested

`tv.exe` is built on the API rather than beside it, so `tools/difftest.py`
-- which drives it over 335 configurations and demands byte-identical PCM
against the original engine -- exercises the library rather than stepping
around it.  The library layer is not new untested code sitting next to a
tested engine; it is on the path the whole corpus already covers.

`tests/api_test.c` covers what the corpus cannot reach: a synth used more
than once, a synth interrupted part way and reused, marks and their
positions, the text conversions, the voice table, and the error paths.  It
is built twice, once statically and once against the DLL, so the exports and
the calling convention are exercised rather than assumed.

## Consumers

`nvda-addon/` is a native NVDA synthesizer driver built on this API; see
docs/NVDA.md.  It is also the worked example of how the callback is meant
to be used -- buffer the audio, flush it with a completion callback when a
mark arrives, and let the host raise the bookmark when playback reaches it.

## Not here yet

A SAPI5 shim, which would let applications that only speak SAPI5 -- book
readers, audio games -- use the engine without a driver of their own.  It
belongs on top of this API rather than beside it, and nothing here is in its
way.
