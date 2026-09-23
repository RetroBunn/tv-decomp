# OpenTV

**Open TruVoice** — a decompilation of the Centigram TruVoice text-to-speech system into portable C, verified byte-for-byte against the 1997 binary.

TruVoice was a text-to-speech system that was originally developed by Centigram Communications Corp. as an evolution of the original Speech Plus Prose 2000 system, made famous by the late Stephen Hawking in 1985 up until his death in 2018.

It's most famous use is as the default voice in Microsoft Agent, as well as in Bonzi Buddy, but can also be heard in text-to-speech videos on YouTube that were made with Speakonia.

OpenTV is the same engine as ordinary C: no COM, no registry, no 32-bit
bridge. It builds as a command-line tool, a flat C library, and an NVDA
add-on, at both 32 and 64 bits.

## Status

| | |
|---|---|
| Engine | 26 source files, 223 functions hooked over the original for differential testing |
| Verified | 335/335 configurations byte-identical, in all three builds |
| Reference | 3/3 against audio recorded from the shipping engine |
| Word widths | 32-bit and 64-bit, both byte-identical to the original |
| Remaining | SAPI 4 COM glue, config dialogs and `waveOut` playback are documented but not ported |

As far as we know the 64-bit build is the first time TruVoice has ever run as
64-bit code.

## Building

Needs an `x86_64` mingw-w64 toolchain (used in `-m32` freestanding mode for
the 32-bit side), `dlltool`, and Python 3. `pefile` is only needed by the
tools that read a DLL, not by an ordinary build.

```sh
sh harness/build.sh
```

That produces, in `build/harness/`:

| | |
|---|---|
| `tv.exe`, `tv64.exe` | command line: text or a file in, WAV out |
| `tvtts.dll`, `tvtts64.dll` | the flat C library, 27 exports |
| `tvh.exe` | the oracle: maps the original DLL with its own PE loader and sandboxed imports |
| `tvh_hook.exe` | the same, with decompiled C patched in over the original |
| `api_test*.exe` | library tests |

`tools/extract_data.py` is how `data/` was made, kept in the tree so that
anyone with the original can check that what is committed is what comes
out of it. It is not part of a normal build.

```sh
./build/harness/tv64.exe -v 0 "Hello world." out.wav
```

## Using it

### As a library

```c
#include "tvtts.h"

tvtts_synth *s = tvtts_create(11025);      /* or 8000 */
tvtts_set_voice(s, 0);
tvtts_speak_utf8(s, "Hello world.", on_event, NULL);
tvtts_destroy(s);
```

Audio and index marks arrive through one callback in stream order. There are
also text-to-phoneme and phoneme-to-speech entry points, a user lexicon, and
inline escapes for marks, pauses, pitch and rate. See
[docs/LIBRARY.md](docs/LIBRARY.md).

### As an NVDA synthesizer

```sh
python tools/make_addon.py
```

Produces `build/truvoice-<version>.nvda-addon`. It is a native driver over
the library — no SAPI anywhere — and needs NVDA 2026.1 or later.
See [docs/NVDA.md](docs/NVDA.md).

### The voices

Ten, in the engine's own order:

| 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Peter | Sidney | Eager Eddie | Deep Douglas | Biff |

| 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|
| Grandpa Amos | Melvin | Alex | Wanda | Julia |

A voice is not a recording or a model. It is fifteen integers that bend the
synthesiser's 22 parameter tracks on the way past, stored as percentage
deviations from voice 0. [docs/VOICES.md](docs/VOICES.md) has the details,
and `tools/voicedump.py` prints them out of your own binaries.

## How it is verified

Decompiling roughly a thousand functions by eye does not converge. What makes
it tractable is a differential harness:

* `tvh.exe` loads the original DLL and calls the engine directly.
* `tvh_hook.exe` does the same but patches each decompiled C function over
  the original with a five-byte jump, so any one of them can be swapped in
  and compared.
* `tv.exe` is the pure C build, no original code at all.

`tools/difftest.py` runs 59 corpus inputs in 335 configurations — ten voices
at two sample rates, pitch, speed and volume variants, the two front-end
passes on and off, embedded escapes, cp1252 text, malformed escapes, phoneme
input, homographs, user-lexicon entries — and demands the PCM be identical.

```sh
python tools/difftest.py --full            # hooked against the original
python tools/difftest.py --full --port     # standalone 32-bit
python tools/difftest.py --full --port64   # standalone 64-bit
python tools/difftest.py --full --ref      # against a real recording
```

`--ref` is the one that checks the harness itself rather than only the
decompiled code: audio produced by the engine as installed, through an
ordinary SAPI client, matched against all three builds.

All of this needs a copy of the original, and is the only part that
does. Put `CGRM_EN.DLL` in `TruVoice/` and it runs; leave it out and it
says so and skips.

This is not ceremony. The corpus is what caught six real decompilation bugs
that were invisible to smaller tests — inverted conditions and one wrong
stack slot, each of which changed a handful of samples in one phoneme.

## Improvements

OpenTV is a decompilation, not a patched binary, so it can fix what the
original got wrong. Anything that changes engine behaviour sits behind a
flag, all of them on by default:

```c
tvtts_set_extensions(0);              /* the 1997 engine, exactly */
tvtts_set_extensions(TVTTS_EXT_ALL);  /* the default */
```

The corpus runs with them off, so byte-exactness keeps proving the engine
underneath is right while callers get the better behaviour by default.

* **`TVTTS_EXT_RATE`** — the engine's rate table has 26 rows, 46..253 wpm,
  and above them it indexed off the end: the same sentence took ten times
  *longer* at 254 wpm than at 253. OpenTV adds rows that shorten durations
  instead, reaching 400 wpm at about 3.4× the speed of 150. Everything at
  253 and below is bit-for-bit the original, so the voices still sound the
  way people know them.

## Documentation

| | |
|---|---|
| [docs/PROGRESS.md](docs/PROGRESS.md) | engine architecture, what is decompiled, what is left |
| [docs/WORKFLOW.md](docs/WORKFLOW.md) | the harness, hooks and how to work on it |
| [docs/LIBRARY.md](docs/LIBRARY.md) | the C API, its contract and its sharp edges |
| [docs/NVDA.md](docs/NVDA.md) | the add-on, and the engine quirks a driver has to handle |
| [docs/VOICES.md](docs/VOICES.md) | voices, the 22 parameter tracks, phoneme input |

## Licence

The OpenTV source is MIT — see [LICENSE](LICENSE). The NVDA add-on under
`nvda-addon/` is GPL v2 or later, as NVDA drivers must be.

**Neither covers `data/`.** Those tables are Centigram's work, included
so the engine can be built and studied without hunting down a 1997 DLL,
and for no other reason. See [NOTICE](NOTICE).

OpenTV is an independent reimplementation for interoperability and
preservation.
