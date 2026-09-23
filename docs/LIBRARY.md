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

So the event reaches you before the audio it refers to.  Queue the samples,
remember `sample_pos`, and raise the bookmark when the play cursor passes
it -- the same shape as eSpeak's `espeak_EVENT_MARK`.

## Text

The engine reads cp1252, because that is what SAPI's `WideCharToMultiByte`
handed it.  `tvtts_speak_utf8` and `tvtts_speak_utf16` do the same
conversion, so a character with no cp1252 byte becomes `?` exactly as it did
then, and the 27 places cp1252 differs from Latin-1 (curly quotes, dashes,
the euro sign) survive rather than being mangled.  `tvtts_speak_bytes` takes
cp1252 directly.

## Voices

Ten, indexed 0..9, named from the engine's own table: Peter, Julia, Wanda,
Alex, Melvin, Grandpa Amos, Biff, Deep Douglas, Eager Eddie, Sidney.
`tvtts_voice_rate` and `tvtts_voice_pitch` give a voice's defaults, which is
what to reset to after the user has been moving sliders.  See docs/VOICES.md
for what a voice actually is.

## Rules

* A `tvtts_synth` is **not thread safe**.  Give each thread its own; they
  share nothing.
* `tvtts_add_lexicon` is **process-global** -- the engine's user lexicon is
  one static table -- and is not thread safe against anything.  Add entries
  before you start speaking.
* Settings persist across utterances, as SAPI's did.
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

## Not here yet

A SAPI5 shim, which would let applications that only speak SAPI5 -- book
readers, audio games -- use the engine without a driver of their own.  It
belongs on top of this API rather than beside it, and nothing here is in its
way.
