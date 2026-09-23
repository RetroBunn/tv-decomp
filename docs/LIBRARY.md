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
* **Do not put an escape after the last word.**  The engine reads the
  final word of an utterance by different rules, and an escape after it
  means it is no longer final: a lone "a" becomes the article rather than
  the letter's name.  Emit marks before the text they follow, and report
  any that would trail once the audio has been delivered.
* **Rate is 46..253 words per minute and nothing else.**  The engine picks
  a 26-row table with `(wpm - 46) >> 3`, unsigned.  Below 46 that wraps
  to an index of about `0x1fffffff` and reads wildly -- the original
  crashes and so does this, so `tvtts_set_rate` floors it.  Above 253 it
  runs off the end of the table: a sentence at 254 wpm comes out ten
  times longer than at 253, not faster.  That end is deliberately *not*
  clamped, because it is the original's own behaviour and the corpus
  checks it at 260 and 400; `tvtts_rate_sequence` clamps both ends, since
  it builds text to speak rather than reproducing anything.  46..76 all
  select the slowest row.
* `tvtts_set_compat` is for reproducing the original exactly, not for normal
  use: it turns off the two front-end passes the original exposed through the
  registry, and sets how many NUL bytes follow the text.  That last one
  changes the audio, because the feed routine branches on the total length.

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
