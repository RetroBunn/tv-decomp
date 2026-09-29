# OpenTV as a SAPI 5 voice

`sapi5/` is a COM in-process server that presents the decompiled engine to any
SAPI 5 application: book readers, audio games, the Windows Speech control
panel, screen readers that have no driver of their own.  It is built at both
word widths and installs twenty voices, ten English and ten Spanish.

It sits on top of `include/tvtts.h` and knows nothing about the engine's
internals, so it gained Spanish for free and will gain French, German and
Italian the same way.

| | |
|---|---|
| `sapi5/sapi_ddk.h` | the SAPI declarations mingw-w64 does not ship |
| `sapi5/tvsapi.c` | the server: the class, the voices, `Speak` |
| `sapi5/tvsapi.def` | the four COM exports, undecorated |
| `sapi5/sapi_test.c` | drives the server without SAPI and without the registry |
| `sapi5/installer.nsi` | the installer, both word widths |
| `tools/check_sapi_ddk.py` | checks `sapi_ddk.h` against Microsoft's own header |

## Building and installing

`sh harness/build.sh` produces `build/bin/tvsapi.dll` and
`build/bin/tvsapi64.dll` along with everything else.  The installer is a
separate step and needs NSIS:

```
makensis sapi5/installer.nsi        # writes build/bin/OpenTV-SAPI5-Setup.exe
```

Or register a build in place, from an elevated prompt:

```
regsvr32 build\bin\tvsapi64.dll                       # 64-bit applications
C:\Windows\SysWOW64\regsvr32 build\bin\tvsapi.dll     # 32-bit applications
```

**Both word widths are worth installing.**  A SAPI voice is not shared between
them -- a 32-bit application can load only a 32-bit voice, a 64-bit one only a
64-bit voice, and each reads its own view of the registry.  Installing only the
64-bit voice is the usual way this goes wrong: the Speech control panel shows
the voices and a 32-bit reader shows none.

`DllRegisterServer` writes everything itself: the class under
`HKLM\SOFTWARE\Classes\CLSID`, and one token per voice under
`HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens`.  `regsvr32 /u` takes out
exactly what it wrote.

## How it is put together

SAPI knows about engines; applications know about *voices*.  One engine
implementation can stand for any number of voices, so there is **one CLSID and
twenty tokens**, each token naming the same server and carrying an
`OpenTVVoice` value that says which voice it is.  SAPI creates the object from
the token and then calls `ISpObjectWithToken::SetObjectToken`, which is where
that number is read.

Each token also carries the attributes SAPI filters on:

| | |
|---|---|
| `Name` | `OpenTV Peter (American English)` |
| `Language` | `409` or `40a`, the LANGID each original DLL states in its own version resource |
| `Gender` | the last two voices of every language are the female ones |
| `Age` | `Senior` for voice 5, the elderly one -- Grandpa Amos, Ezequiel |
| `Vendor` | `OpenTV` |

The name carries the `OpenTV` prefix because SAPI puts every vendor's voices
into one list, so a name has to say whose it is -- the same reason Microsoft's
own read `Microsoft David Desktop`.  The speak window and the NVDA driver
leave the prefix off, because there a voice is already listed under OpenTV.

Gender and age are not recorded anywhere in the engine; they are derived from
the position a voice holds within its own language, which is the same in all
five of the original's DLLs.  docs/VOICES.md establishes both.

The engine is **linked into the server statically** rather than loaded from
`tvtts.dll` beside it.  A COM server is loaded by full path and its own
directory is not searched for dependents, so a `tvsapi.dll` that needed
`tvtts.dll` next to it would load in some hosts and not others.  Each DLL is
about 3 MB, nearly all of it the two engines' data.

## Rate, pitch and volume

SAPI gives rate and pitch as -10..+10 around the voice's own default, and
volume as 0..100.  The NVDA driver already maps this engine onto a slider, and
the two should not disagree, so `sapi5/tvsapi.c` uses the same two scales:
rate linear to the ends of the engine's range, pitch logarithmic.

Twenty logarithmic steps from 50 to 500 is a ratio of `10^(1/20)` each, so one
SAPI step multiplies the pitch by about 1.122.  Josefa, whose own pitch is 208,
runs 65..500 across the range; Sidney at 50 is already on the floor and runs
50..158.

Rate reaches `TVTTS_RATE_MAX_EXT`, 400 wpm, at +10 -- which is OpenTV's
extension, not the original's ceiling of 253.  Everything at 253 and below is
bit-for-bit the 1997 engine.

## What it does and does not do

Implemented: plain text, `<silence>`, `<bookmark>`, `<pitch>`, `<rate>`,
`<volume>`, spell-out, `SPF_NLP_SPEAK_PUNC`, abort, and the bookmark event.

Not implemented, and each for a reason rather than an oversight:

* **`SPVA_Pronounce`** carries SAPI phone ids, whose alphabet is not this
  engine's.  The engine has its own one-character alphabet (docs/VOICES.md) and
  `tvtts_speak_phonemes` for it; a converter between the two is a separate
  piece of work and the fragment is passed over until it exists.
* **Word and sentence boundary events** need a character offset per word, and
  `tvtts.h` reports index marks but not offsets.  A host that wants word
  highlighting will not get it.
* **Phoneme and viseme events** would need the same, plus the mapping above.
* **Sentence skipping** cannot be done: the engine renders an utterance in one
  pass with no way back into it.  `GetSkipInfo` is answered by reporting none
  skipped and abandoning the call, which is what the specification says to do
  when the whole amount cannot be managed.
* **`ISpTokenUI`**, the Settings button in the Speech control panel, has no
  dialog behind it.
* **SAPI lexicons** are not consulted.  The engine's own user lexicon is
  reachable through `tvtts_add_lexicon`.

## Two traps

Both cost time, so both are written down where they are relied on.

**`Speak` comes first in `ISpTTSEngine`'s vtable, `GetOutputFormat` second.**
Microsoft's TTS Engine Vendor Porting Guide describes them the other way round,
because that is prose order, not vtable order.  Transposing them puts SAPI's
`GetOutputFormat` call onto `Speak`, with four arguments where five are
expected, and the voice crashes its host on the first utterance.
`sapi5/sapi_ddk.h` states the order and `tools/check_sapi_ddk.py` checks it.

**A bookmark fragment's text is the bookmark's name, not words to speak.**
JAWS sends every word as its own `SPVA_Speak` fragment with an `SPVA_Bookmark`
between each pair.  Appending fragment text blindly reads the bookmark names
aloud; dropping them without putting anything back runs the neighbouring words
together.  The name is kept for the event and a space is restored at the seam
when neither side brought one.  This one is Panthera Speech's finding, adapted
with it -- see NOTICE.

A third, smaller: **the engine ignores mark 0.**  `ESC[0i` produces no event
where `ESC[1i` does, which is of a piece with the rest of its escapes using
zero to mean off, so the numbers handed to it start at one.

## Verifying it

```
python tools/check_sapi_ddk.py     # the declarations, against the Windows SDK
build\check\sapi_test.exe   build\bin\tvsapi64.dll
build\check\sapi_test32.exe build\bin\tvsapi.dll
```

`sapi_test` reaches the server through its own `DllGetClassObject` and drives
it with an `ISpTTSEngineSite` and an `ISpObjectToken` of its own, so it needs
no registry entries and no administrator.  It checks the exports, the
reference counting, the output format, audio, bookmarks by name and by number,
the JAWS pattern, abort, silence, an empty utterance, both languages, and that
a foreign CLSID and an unknown token are refused -- 47 checks, at both word
widths.

The last fourteen of those check what `DllRegisterServer` actually writes, which
would otherwise be the one untested part.  Setting `OPENTV_SAPI_TEST_HIVE`
sends the registration to `HKEY_CURRENT_USER` under a key of this project's
own instead of to `HKEY_LOCAL_MACHINE`, so the real registration path runs --
key names, labels, attributes -- without an administrator and without
disturbing the machine's actual voice list.  `DllUnregisterServer` then takes
the whole test root out again, and the test checks that too.  SAPI does not
read that location: it is a test hook, not a per-user install.

What it cannot check is SAPI's own half: that the tokens are where SAPI looks
and that the fragments arrive shaped as expected.  For that, register the DLL
and use any SAPI client -- the Speech control panel's *Preview Voice* is the
shortest path, and the voices appear there as `Peter (American English)` and
the rest.

## Provenance

The interface definitions are not written from memory.  `sapi.h`, `sapi51.h`
and their `DEFINE_GUID` IIDs come from the mingw-w64 toolchain the project
already compiles against, which supplies `SPVSTATE`, `SPVACTIONS`, `SPVPITCH`,
`SPVCONTEXT`, `SPEVENT`, `ISpEventSink`, `ISpObjectToken`, `ISpObjectWithToken`
and the token registry constants.

What mingw-w64 has no counterpart for is `sapiddk.h`, and mixing the two header
sets is not possible -- Microsoft's `windows.h` stops a mingw compile at once
with `#error "No Target Architecture"`.  So `ISpTTSEngine`, `ISpTTSEngineSite`,
`SPVTEXTFRAG`, `SPVSKIPTYPE` and `SPVESACTIONS` are restated in
`sapi5/sapi_ddk.h`, read out of Windows Kits 10.0.26100.0, and
`tools/check_sapi_ddk.py` compares the two so the restatement is checked rather
than trusted.  `SPDFID_WaveFormatEx` is declared by mingw but defined by no
mingw library; its value was taken from the byte image of the SDK's `sapi.lib`,
where it occurs once, in the object MIDL built from `sapiint_i.c`.

The semantics -- what `Speak` must do, when to call `GetActions`, that events
are queued before the audio they belong to -- are from Microsoft's published
[TTS Engine Vendor Porting
Guide](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee431802(v=vs.85)).
