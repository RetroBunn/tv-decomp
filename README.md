# OpenTV

**OpenTV** — a decompilation of the Centigram TruVoice text-to-speech system into portable C, verified byte-for-byte against the 1997 binary.

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
| Interfaces | a C library, an NVDA driver, and SAPI 5 voices at both word widths |
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

That produces three directories.  `build/bin/` is what you would use:

| | |
|---|---|
| `tvtts.dll`, `tvtts64.dll` | the flat C library, 27 exports |
| `libtvtts.a`, `libtvtts64.a` | import libraries, to link against either |
| `speakwin.exe` | the speak window |

`build/check/` is the verification machinery, which is not something you
run by hand -- `tools/difftest.py` drives it:

| | |
|---|---|
| `tvh.exe` | the oracle: maps the original DLL with its own PE loader and sandboxed imports |
| `tvh_hook.exe`, `tvh_hook_es.exe` | the same, with decompiled C patched in over the original |
| `tv.exe`, `tv64.exe` | command line: text or a file in, WAV out |
| `api_test*.exe` | library tests |

`build/obj/` is intermediates and can be deleted at any time.  The build
finishes by printing all of that, so there is no need to go looking.

`tools/extract_data.py` is how `data/` was made, kept in the tree so that
anyone with the original can check that what is committed is what comes
out of it. It is not part of a normal build.

```sh
./build/check/tv64.exe -v 0 "Hello world." out.wav
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

Produces `build/bin/opentv-<version>.nvda-addon`. It is a native driver over
the library — no SAPI anywhere — and needs NVDA 2026.1 or later.
See [docs/NVDA.md](docs/NVDA.md).

### As a speak window

`build/bin/speakwin.exe` is a window for typing text and hearing it:
voice, rate, pitch, volume and sample rate, with F5 to speak, F6 to pause,
F7 to stop and F8 to reset. It exports to WAV as well.

It is plain Win32 and uses only stock controls, so NVDA, JAWS and Narrator
read it without the program implementing anything for them.
See [docs/SPEAKWIN.md](docs/SPEAKWIN.md).

### The voices

Ten per language, in the engine's own order:

| 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Peter | Sidney | Eager Eddie | Deep Douglas | Biff |
| Pedro | Jorge | Ricardo | Paco | Luis |

| 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|
| Grandpa Amos | Melvin | Alex | Wanda | Julia |
| Ezequiel | Rogelio | Carlos | Josefa | Isabel |

A voice is not a recording or a model. It is fifteen integers that bend the
synthesiser's 22 parameter tracks on the way past, stored as percentage
deviations from voice 0. [docs/VOICES.md](docs/VOICES.md) has the details,
and `tools/voicedump.py` prints them out of your own binaries.

Which means a voice can be *written*, and the engine carries the voice in four
bits, so sixteen fit per language where ten are defined. Two of OpenTV's own sit
above them: **Frank**, a deep male voice ported from MindMaker's TextAssist build
of this engine, and **Francisco**, who is Frank speaking Spanish. Each is a
description of how it differs from one of Centigram's, so no table from the
original binaries is copied into source, and each is inert below voice ten --
which is why the corpus is unaffected by either.

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
tvtts_set_extensions(0);              /* the original engines, exactly */
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

* **`TVTTS_EXT_CLARITY`** — widens the formant bandwidths as the rate
  climbs, which is what keeps fast speech from slurring: a narrow resonator
  rings for longer than a shortened phoneme lasts, so its energy smears into
  the next one. It does nothing at or below 253 wpm, so it only shapes the
  range `TVTTS_EXT_RATE` added.

* **`TVTTS_EXT_PITCH`** — the Spanish engine clamps every node's pitch to
  50..200 where the English one clamps to 50..500, and both DLLs ship the
  *same* ten-voice table, in which Carlos is 203 and Josefa 208. Those two
  voices sit above their own engine's ceiling, so every node of their
  contour was pinned to it and they spoke in a monotone; measured, Josefa
  held 197–208 Hz across a whole sentence. The flag moves the one constant
  to the value the other generation of the same engine uses — the byte it is
  stored in holds half the pitch, so 500 is what fits and 200 never needed
  the limit. Josefa now moves over 208–269 Hz in that sentence. It does
  nothing to English, which already clamps there.

## What is next

**Spanish is in.**  One library carries both engines, the add-on offers
twenty-two voices in two languages, and each is listed with the language it
speaks -- "Peter (American English)", "Pedro (Castilian Spanish)" -- and
tagged so NVDA's automatic language switching can pick it.  Voices are
numbered across the languages, so setting one from another language switches
the engine; `tvtts_set_language` is there for callers that would rather ask
directly.  The standalone build is byte-identical to the original across both
corpora at 32 and 64 bits.

**The Spanish engine is finished.**  Every function the corpus executes is
decompiled and byte-exact: 196 of 196 functions and 87,277 of 87,277 bytes,
100% of the code the 205 configurations reach, with the C runtime bound to
the DLL's own copy rather than rewritten.  The tokenizer, both rule
interpreters, the classifier, all five pipeline stages, the synthesiser and
the sample generator are all in `es/`, and 205 of 205 corpus configurations,
the whole `unit_es` suite and a SAPI recording all come back identical.  What
is left in the image is the DLL's SAPI 4 plumbing, which this project
replaces rather than reproduces; 212 functions are written in all, and the only
ones still resolved against the DLL are eleven C runtime entries and the two
calls the engine makes upward.  [docs/SPANISH.md](docs/SPANISH.md) ends with
what remains and how to remeasure.

The SAPI 5 interface is done, at both word widths: `sapi5/` is a COM server
presenting all twenty-two voices to any SAPI application, with an NSIS installer
beside it.  What a SAPI 5 voice has to implement was taken from Microsoft's
published porting guide and from its own `sapiddk.h`, and
`tools/check_sapi_ddk.py` checks the few declarations that had to be restated
by hand against that header rather than asking to be trusted.
[docs/SAPI5.md](docs/SAPI5.md) has the whole of it.

One thing remains.

1. **French, German and Italian.**  All four 1995 engines are one build
   with the same layout displaced in blocks, and `tools/xmatch.py` already
   puts a confident counterpart on 454 of Spanish's 766 functions in
   Italian, 439 in French and 423 in German -- before `--near` is used at
   all.  Finishing Spanish first is what makes these three cheap, and it is
   now done.

## Japanese

OpenTV speaks Japanese, and it is a different kind of thing from the other two
languages. English and Spanish are decompilations: a Centigram DLL exists, the
C reproduces it byte for byte, and the test is byte-identical audio. **There is
no Japanese TruVoice.** Its front end was built from the phonetics literature
— the papers and what was taken from each are recorded in
`build/Japanese_test/README.txt`, segment by segment — and it drives the 1997
synthesiser through `tvtts_speak_frames`, the layer below stage 3 that knows
nothing about any language. Nothing under `src/engine/` changes for it.

Because there is no original to be exact against, the test is the Python
prototype whose output has been signed off by ear, and it is tested in two
halves. `oracle_frames.tsv` holds 533 words as parameter frames — 36,013
frames, 792,286 bytes — and `build/check/ja_check.exe oracle` requires the C
to reproduce every one of them, at both word widths. `front_oracle.tsv` holds
1,900 texts with the morae, accents, devoicing flags and question flag the
analyser makes of them, and `ja_check front` requires every field of every
line, also at both widths. `tools/check_ja_front.py` checks the mora parser
separately against the Python over 60,055 inputs.

### It reads kanji, and it knows the accent

Both of those arrived late and they arrived together, because they come from
the same place. `data/ja/jadic.bin` is naist-jdic compiled down: for each of
486,757 entries a reading, an accent type, a mora count and an accent chain
rule, with the 1377x1377 connection matrix a morphological analyser needs. The
front end runs a Viterbi over it and then Open JTalk's rule stages, which are
plain tests on morphological category -- `njd_set_accent_phrase` is eighteen
numbered rules and nothing else. The rules were read as a specification and
written out again; no Open JTalk code is in this repository. Both projects are
BSD 3-clause. See NOTICE.

**All of it is in C**, in `ja_port/ja_dict.c`, `ja_njd.c`, `ja_digit.c` and
`ja_front.c`, so this is what the shipped DLL does and not what a prototype
does. `build/Japanese_test/` holds the Python the C was translated from and
remains the reference; the two are held together by `front_oracle.tsv`, and
they read their rule tables out of one generated file —
`tools/gen_ja_rules.py` extracts them from upstream's headers and
`tools/gen_ja_ojt.py` turns that into C — so they cannot drift. There is no
Japanese string literal typed by hand in any of the five rule stages, on
either side, which matters because every table that was once transcribed had
an error in it.

The renderer's own fallback devoicing, used where no analysis is available —
romaji input, and the frame oracle — reads upstream's rule 5 through a third
generator, `tools/gen_ja_devoice.py`. It projects those katakana tables into
the mora alphabet the renderer works in, which needs care in two places: the
following mora is matched by *prefix* upstream, where `kya` does not start
with `ki`, and four foreign morae collapse onto ordinary ones under the romaji
parser, which would otherwise make `/yu/` and `/to/` devoicing candidates. It
expands the first and checks every projected mora against the precondition it
has to satisfy, printing what it drops.

They are also checked against the real thing rather than against a reading of
it. `tools/ja_stage_parity.py` drives the checkout's own stages, compiled into
a reference library, and compares every word. **All five stages now agree
exactly**: over 60,013 utterances, 356,486 words and 1,476,548 morae,
`njd_set_pronunciation`, `njd_set_digit`, `njd_set_accent_phrase`,
`njd_set_accent_type` and `njd_set_unvoiced_vowel` produce the same output as
the compiled 1.11 release, and so does every one of the 1,900 texts in
`front_oracle.tsv`.

Two divergences are declared rather than counted, and both are deliberate.
`。` is kept where Open JTalk folds it into `、`, because its prosody grades
the boundary and has nowhere later to recover the distinction. And where
upstream's devoicing stage meets a character outside its 159-mora inventory —
`ヮ`, which its own dictionary uses in クヮルテット — it prints a warning and
returns, leaving the rest of the sentence unexamined; this keeps going.

Getting there took finding five rows that upstream had **commented out** and
this project had taken anyway, because the table extractor scanned for quoted
strings without stripping C comments. One of them was 三 in the native-numeral
table, which is why 三粒 is サンツブ and not ミツブ.

What that buys, concretely. `私は日本語を話します` used to reach the
synthesiser as `はをします` -- the particles, with every kanji dropped. It now
reaches it whole, as three accent phrases with their own accent types. And
`箸が`, `橋が` and `端が` are the same three morae and now differ: the fall
lands after the first mora, on the particle, and not at all.

Romaji input stays on the kana parser, because the analyser normalises ASCII
*to* fullwidth before it looks anything up -- that is how naist-jdic is keyed
-- so `konnichiwa` would reach it as ＫＯＮＮＩＣＨＩＷＡ and come back as the
Japanese names of eleven Latin letters.

**A Latin token is read in three steps, and the decision is per token.** The
lexicon first: every Japanese system reads a bare `take` as the English テイク
rather than the Japanese タケ -- Google TTS, Microsoft OneCore Japanese and
ETI-Eloquence all agree -- because they look the word up before considering
anything else. Trying romaji first took 142 of the 840 Latin words naist-jdic
knows, of which 107 differed segmentally (`AU` read /a.u/ against the
dictionary's エーユー) and 27 agreed segmentally but lost the accent type
(/a.ma.zo.N/ already *is* アマゾン; what went missing was its accent 1).

Then romaji, **and only if it consumes the whole token.** "All-ASCII letters"
is a test for the Latin alphabet, not for romaji, and the mora parser is
lenient by design -- the kana path feeds it generated romaji and it must not
reject a stray mark. So it accepted every English word and quietly dropped the
letters Japanese has no mora for: `hello` was /he.Q.o/, `computer` /o.pu.te/
with the c, m and r gone, `blorf` the single mora /o/. Over 20,280 dictionary
readings round-tripped through romaji, none fails the strict test.

Then spelling out. And the choice is made **per token**, not per utterance: it
used to be per utterance, which meant one known word suppressed the romaji
reading of every other, so `Windows konnichiwa` said ウィンドーズ and then the
Japanese names of `konnichiwa`'s ten letters. It now says
ウィンドーズ・コンニチワ, and `私はWindowsをつかいます` and `これはsakuraです`
work for the same reason.

What is still missing is a reading for an unfamiliar word -- `computer` is
spelled out where a Japanese system says コンピューター. Three shipping systems
were inspected to see how they do it, and what that establishes is that all
three pronounce beyond a fixed word list; how they divide the work between
rules and lexical data is not established by what can be read from the
outside. `jp_res/open_questions.md` §4.3 has the measurements, the
architecture precedent, and the useful part: `tvtts_text_to_phonemes` already
returns an English pronunciation string and already tells an invented word
from an initialism, at the cost of synthesising to get it.

The accent earns its place twice, which is the part worth knowing. It sets
where the pitch falls, and it also blocks devoicing on the mora that carries
it: `一` is accent 2 of 2, so its `/chi/` keeps its voice, where the old
string-level rule whispered it away. Measured over all 486,646 pronounced
naist-jdic entries, 11.2% of the 199,504 morae that rule devoiced were accent
nuclei and 11.5% were the first of an adjacent pair, which Japanese does not
allow either — 20.8% caught by one rule or the other.

### Numbers

Numbers are the part of a screen reader's day that a kana-only front end gets
entirely wrong, so the digit stage is ported whole. `1250円` says
センニヒャクゴジューエン and `1,250円` the same, because the comma grouping is
checked — every comma has to sit at a thousands position and every thousands
position has to carry one — while `1,25,0` fails that test and falls back to
イチ、ニジューゴ、ゼロ. `03-1234-5678` is read digit by digit as an identifier,
with 0, 2 and 5 lengthened to ゼロ ニー ゴー the way they are said over a line.
`1.5` is イッテンゴ. `12,345,678` is センニヒャクサンジューヨンマンゴセンロッピャクナナジューハチ.

The named exceptions are there too. The counters assimilate — `一本` イッポン,
`三本` サンボン, `六本` ロッポン — the native numerals apply, so `一粒` is
ヒトツブ and `二口` フタクチ, and the dates are right: `一日` is ツイタチ after
a month and イチニチ otherwise, `二十日` is ハツカ, `十四日` ジューヨッカ.

And the accent lands where it should, which is a separate rule for each place
name — the fall after mora *n*, where *n* is the accent type. `七百` is type 2,
ナナ↓ヒャク; `三百` is type 1, サ↓ンビャク; `三千` type 3, サンゼ↓ン; `一億`
and `六兆` type 2. `五十` is ゴ↓ジュー, but `五十一` is flat up to the one and
so is the `十` of `十二`. That branch of `njd_set_accent_type` was the last
piece of the number work.

### What it still does not do

* **The dictionary is a 26 MB file and has to be found at run time.** It is
  read from disk rather than compiled in, because otherwise every English user
  would pay for it: `$TVTTS_JA_DICT`, then beside the loaded module, then
  beside the executable. The NVDA add-on and the SAPI installer both put it
  beside the DLL. If it is missing, Japanese quietly falls back to the kana
  and romaji path — the voice still speaks, it just drops every kanji — and a
  host has no way to ask whether that happened.
* **8 kHz is refused.** The sibilant noise sits at and above an 8 kHz Nyquist,
  so that rate needs its own calibration and has never had one.
  `tvtts_set_language` falls back to 11025.
* **The ten voices are Centigram's.** A Japanese voice is one of the engine's
  own parameter blocks -- `Synth_Frame` reads the voice from the high nibble of
  track 21 -- so they line up index for index with English, which is also why
  SAPI gets their gender and age right. All ten have been measured for
  overflow and listened to. Two things about them are worth knowing:
  **Akira** takes 8 dB of source attenuation at 11025 Hz to stay clean, which
  leaves it quieter than the rest (RMS 620 against Taro's 1724); and the
  library does not apply a voice's own rate and pitch when the voice changes,
  by design and as English does not either, so a host that never asks
  `tvtts_voice_rate` and `tvtts_voice_pitch` gets every voice at Taro's
  baseline -- which makes the two female voices a short vocal tract at a male
  pitch. `src/port/main.c` shows the pattern a caller should follow.

`jp_res/open_questions.md` is the live list of what is unresolved.
`tools/ja_samples.py` renders the voices, the phonetic probes and the kanji
sentences through the library for listening, and
`build/Japanese_test/make_dict.py` renders a wider set through the Python.

## Documentation

| | |
|---|---|
| [docs/PROGRESS.md](docs/PROGRESS.md) | engine architecture, what is decompiled, what is left |
| [docs/WORKFLOW.md](docs/WORKFLOW.md) | the harness, hooks and how to work on it |
| [docs/LIBRARY.md](docs/LIBRARY.md) | the C API, its contract and its sharp edges |
| [docs/NVDA.md](docs/NVDA.md) | the add-on, and the engine quirks a driver has to handle |
| [docs/VOICES.md](docs/VOICES.md) | voices, the 22 parameter tracks, phoneme input |
| [docs/SPANISH.md](docs/SPANISH.md) | the second engine, and how far it has got |
| [docs/SAPI5.md](docs/SAPI5.md) | the SAPI 5 voices, and the two traps that cost time |
| [docs/SPEAKWIN.md](docs/SPEAKWIN.md) | the speak window, and how it stays accessible |

## Licence

The OpenTV source is MIT — see [LICENSE](LICENSE). The NVDA add-on under
`nvda-addon/` is GPL v2 or later, as NVDA drivers must be.

**Neither covers `data/`.** Those tables are Centigram's work, included
so the engine can be built and studied without hunting down a 1997 DLL,
and for no other reason. See [NOTICE](NOTICE).

OpenTV is an independent reimplementation for interoperability and
preservation.
