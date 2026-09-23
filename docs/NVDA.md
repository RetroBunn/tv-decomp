# The NVDA add-on

`nvda-addon/` is a native NVDA synthesizer driver over `tvtts.dll`.  No SAPI:
no COM, no registry entries, and no bridge process.

```
python tools/make_addon.py          ->  build/truvoice-0.1.0.nvda-addon
python tools/make_addon.py --32     ->  the same, for a 32-bit host
```

The packaged add-on is four files: `manifest.ini`, the two driver modules,
and the library, which goes in as `synthDrivers/tvtts.dll` whichever
bitness it was built from, so the driver never has to know which it got.
The library is not in the repository -- it is built from your own copy of
the original -- so the package has to be built rather than downloaded.

## Why it is 64-bit by default

NVDA is a 64-bit process (`user_docs/en/changes.md`: "NVDA is now built with
Python 3.13.12, 64-bit"), so a 64-bit library loads in process.  NVDA can
host a 32-bit synthesizer, but only by running `nvda_synthDriverHost.exe`
and marshalling over RPyC -- which is what `sapi4_32.py` and `sapi5_32.py`
do, and what this add-on exists to avoid.  The 32-bit package is there for
an older NVDA, or for another screen reader still running as a 32-bit
process.

## How it is put together

`synthDrivers/_truvoice.py` is the binding: ctypes over `tvtts.dll`, a
background thread, and NVDA's `nvwave.WavePlayer`.  `synthDrivers/
truvoice.py` is the `SynthDriver` NVDA talks to.  The split follows NVDA's
own eSpeak driver, which was the model for the threading and for how index
marks reach the user.

**Synthesis runs on the background thread.**  `tvtts_speak_utf16` blocks and
calls back with audio and marks; the callback returns non-zero to abort, so
cancelling is a flag rather than a lock.  `cancel()` sets that flag, empties
the queue of anything not yet started and stops the player.

**Index marks** ride the audio.  The library hands a mark over only once
every sample before it has been delivered, so the driver buffers the audio
it is given and flushes it with `onDone=` attached when a mark arrives --
NVDA then raises `synthIndexReached` when the play cursor actually reaches
that point.  Getting this right is what makes "say all" track the caret
instead of running ahead of it; see the `sample_pos` note in
docs/LIBRARY.md for why the library corrects the position by twelve frames.

**Rate, pitch and volume** are the 0-100 percentages NVDA gives every
driver.  The two sliders do not work the same way, because the two settings
are not alike across the ten voices.

**Rate is relative.**  Fifty percent is the voice's own default, so every
voice speaks at its intended speed with the slider centred.  There is
little to lose by this: nine of the ten default to 150 wpm and only Grandpa
Amos differs, at 120.

The slider reaches **400 wpm**, which is about three and a half times the
speed of 150.  The 1997 engine stopped at 253: its rate table has 26 rows
and above them it read off the end, so 254 wpm came out *ten times longer*
than 253 rather than faster.  OpenTV adds rows past the original's, which
shorten durations instead -- see the `TVTTS_EXT_RATE` section of
docs/LIBRARY.md.  Everything at 253 and below is bit-for-bit the original,
so the voices still sound the way people know them.

**Pitch is absolute.**  One scale for all ten, so picking a voice moves the
slider to wherever that voice sits.  It is logarithmic -- pitch is heard in
ratios, not in steps -- which matters more over a range this wide.  Mapped
linearly all ten voices would sit below 35%; logarithmically they use the
lower two thirds:

| voice | pitch | slider | | voice | pitch | slider |
|---|---|---|---|---|---|---|
| Sidney | 50 | 0% | | Eager Eddie | 125 | 40% |
| Deep Douglas | 73 | 16% | | Biff | 129 | 41% |
| Peter | 85 | 23% | | Julia | 152 | 48% |
| Grandpa Amos | 89 | 25% | | Alex | 203 | 61% |
| Melvin | 117 | 37% | | Wanda | 208 | 62% |

The slider spans **50 to 500**, which is the engine's whole range and not a
chosen subset of it: Stage 2 clamps every node it emits to 50..500 and then
stores the value halved in a byte, so 500 is the largest pitch whose half
still fits.  Sidney sits exactly on the floor.

That the ten voices only reach 62% is the point rather than a flaw -- the
engine goes a good deal higher than any voice Centigram shipped, and the
top third of the slider is pitch nothing else will give you.

Outside 50..500 the base pitch is not simply ignored, because it is
arithmetic on the way in rather than the clamped value: below 50 it still
lifts the accented nodes, and the audio keeps changing down to about 28 and
up to about 516 before saturating.  None of that is a pitch the engine can
hold, so the slider stops at the range it can.

One asymmetry to know about.  `PitchCommand` goes inline as `ESC[<n>p`, and
that escape takes n 25..200 and doubles it (`preformat.c:297`), so inline
pitch reaches 400 and no further however high the slider is set.  A capital
spoken by a voice already near the top is therefore raised less than one
spoken by Peter.  `tvtts_set_pitch`, which is what the slider uses, has no
such limit.

Changing voice takes the pitch straight from `tvtts_voice_pitch` rather
than back through the percentage, so a voice picked and left alone has
exactly its intended pitch; the percentage is only what the slider reads.
Moving the slider then quantises to its 101 steps, which over a 210-unit
range is at worst a unit or two.

This replaces an earlier scheme where fifty percent was the voice's own
default for pitch as well.  That kept every voice at its natural pitch with
the slider centred, but the slider then read 50% whatever voice was chosen
and never moved when one was changed -- and the travel either side was
badly lopsided, since Sidney's bottom half covered ten units of pitch while
his top half covered two hundred.

The rate slider spans 46 to 253 words per minute, which is the whole of
what the engine has.  Its rate is a 26-row table picked by
`(wpm - 46) >> 3` (`Engine_SetSpeed`, `src/engine/engine.c:97`), so 46 is
the first row and 253 the last.  Outside that it does not merely clamp:

* below 46 the subtraction is unsigned and wraps, giving an index of
  about `0x1fffffff` and a wild read.  The original segfaults there and
  so does this, verified against `CGRM_EN.DLL` at 10, 20, 30, 40 and 45,
  so `tvtts_set_rate` floors the value.
* above 253 the index runs off the end of the table and the engine reads
  whatever follows.  The same sentence takes 38,544 bytes at 253 and
  391,864 at 254 -- ten times *longer* for asking it to go faster.

The driver used to map 100% to 400 wpm, so everything above about 70% of
the slider was in that second case: it made speech slower and stranger
rather than faster.  46..253 reaches all 26 rows with none repeated more
than the table's own 8-wpm granularity forces, so the slider is now
monotonic end to end.

**Commands**: `IndexCommand`, `BreakCommand`, `PitchCommand` and
`RateCommand`.  All of them go inline into the text as engine escapes,
built by the `tvtts_*_sequence` helpers so the driver does not have to know
the encoding.  Each escape was measured rather than assumed:

| command | escape | |
|---|---|---|
| `IndexCommand` | `ESC [ n i` | |
| `BreakCommand` | `ESC [ n s` | n is hundredths of a second biased by 49, so it caps at 2060 ms |
| `PitchCommand` | `ESC [ n p` | the engine's pitch is exactly 2n, so n is half the value `tvtts_set_pitch` takes |
| `RateCommand` | `ESC [ n r` | words per minute |

`CharacterModeCommand` is accepted and ignored, and is deliberately not in
`supportedCommands`.  There is nothing for it to do: the engine already
names a letter that is given on its own, so "a" is `&A1` -- "ay" -- and not
the article.  An earlier version of this driver mapped it to `ESC[2N` /
`ESC[2F` believing that to be spell mode.  It is not.  Flag 2 is *speak
punctuation*: with it on, "Hi, there." gains the spoken words "comma" and
"period", and no word is ever spelled.  The "about twice as long" that made
it look like spelling was the added "period".

### Nothing may follow the last word

The engine's letter-to-sound rules read the final word of an utterance
differently from one with more text after it.  Put any escape after a lone
"a" and it stops being final, so the rules give the article -- schwa, `%@`
-- instead of the letter's name.  Measured, on a fresh synth each time:

```
a                 -> &A1     the letter, "ay"
ESC[42i  a        -> &A1     a leading escape is harmless
a  ESC[42i        -> %@.     the article, "uh"
a  ESC[2F         -> %@.     any escape will do it
```

That is not a corner case, it is every typed letter.  NVDA appends an index
to the end of *every* utterance (`speech/manager.py:318`, "Add an index so
we know when we've reached the end of this utterance"), and closes a capital
with a `PitchCommand()` after the character
(`speech/speech.py:_getSpellingCharAddCapNotification`).  Both land exactly
there, which is why lowercase letters were affected too.

Only "a" actually changes its phonemes, since it is the one letter whose
name and word differ.  The rest keep their name and merely gain a
sentence-final pause -- "I" goes from `%I` to `%I.` -- so "a" was the whole
of what could be heard going wrong.

So `speak` holds back every escape it has built since the last piece of
text.  If more text follows, the held escapes are emitted ahead of it and
nothing is lost.  If none does, they were trailing: index marks among them
are reported at the end of the utterance instead, through the library's
`trailingMarks`, and the rest is dropped, having nothing left to apply to.

### Inline prosody outlives its utterance

`ESC[<n>p` and `ESC[<n>r` do not merely change the current utterance: they
write the same state `tvtts_set_pitch` and `tvtts_set_rate` do, and it
persists into every later utterance on that synth.  A capital's raised
pitch would therefore stay raised for good.

It cannot be closed with a trailing escape, for the reason above, and it
should not be: `ESC[<n>p` carries half the pitch, so restoring an odd pitch
that way lands a unit low and stays there.  Instead the driver hands
`_truvoice.speak` the pitch and rate to restore, and the setters put both
back exactly once the text has been spoken.

An ESC in the text itself would start a command, so `speak` replaces any
with a space.

## Testing

`python tests/nvda_binding_test.py` drives both halves with NVDA's modules
stubbed: a player that records what it was fed and runs each `onDone`.  It
covers the ctypes signatures, the callback ABI, the background thread, the
order audio and marks arrive in, cancelling and resuming, and the escapes.

It also calls the driver's `speak` directly.  Constructing the class wants
NVDA's whole settings stack, but `speak` reads only four attributes, so it
runs against a stand-in -- and `speak` is where both of this add-on's bugs
have been.  The checks that matter are that the text never ends with an
escape, that a held mark is still reported, and that a sequence shaped like
NVDA's own capital -- raise pitch, letter, close pitch -- leaves the letter
last and restores through the setters.
It needs a 64-bit Python, since that is the library it loads.

What it does not cover is `truvoice.py` itself, which needs NVDA's speech
machinery to import, and real audio output.  Those want NVDA.
