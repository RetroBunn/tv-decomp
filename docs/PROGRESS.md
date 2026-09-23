# Decompilation progress

Verification: `python tools/difftest.py --full` compares the decompiled code
(hooked into the running original) against the original over the corpus, and
`--port` does the same for the standalone build, which loads nothing.  Both
are byte-exact on all 335 configurations.

`--port64` runs the same corpus through the 64-bit build.  All three are
byte-exact, and byte-exact with each other.

`--ref` checks something else: audio recorded from the engine as installed,
spoken through an ordinary SAPI client, against all three builds.  That is
the only test that covers the harness's own reconstruction of the SAPI
engine thread rather than just the decompiled code, and all three builds
reproduce the recording byte for byte.  See `ref/` in WORKFLOW.md.

`docs/NVDA.md` describes the NVDA add-on in `nvda-addon/`: a native
synthesizer driver over the library, with no SAPI in the way.

`docs/LIBRARY.md` describes `include/tvtts.h`, the flat C API the engine is
packaged behind: `tvtts.dll` for screen readers and anything else that wants
the synthesizer without SAPI.  The command line front end is built on it, so
the corpus test covers the library too.

`docs/VOICES.md` describes the voice system: the 22 synthesis parameter
tracks, how a voice is stored as percentage deviations from voice 0, and
the escape command that drives the tracks directly.

## Engine architecture (as understood so far)

```
SAPI TextData item
  -> Engine_Feed          splits into lines, tidies them, fills in_ring
  -> TextIn               tokenises, classifies, expands  (optional: "TextIn")
  -> Preformat_Run        folds accents, parses ESC[..X commands -> mid_ring
  -> Engine_InputStage    characters/commands -> work-list nodes
  -> Stage 0 (0x10031560) rule interpreter (g_10120a10 rule program)
  -> Stage 1 (0x10062830) word pronunciation / lexicon
  -> Stage 2 (0x1002b2b0)
  -> Stage 3 (0x1002c980) phonetics -> 22 synthesis parameter tracks
  -> Synth   (0x10025cb0) formant synthesizer -> PCM in out_buf
  -> Stage 4 (0x1002bc10) releases nodes in step with the audio
```

The work list is a doubly linked list of 618 pooled `Node`s inside the
engine object; each stage owns a window into it and hands finished nodes to
the next stage.  `StageCtx.type_mask` selects the node types a stage
handles; other node types are control commands it executes in passing.

## Done (223 functions)

* **Node/stage core** `node.c`: list insert/unlink, pool reset, node
  alloc/free, stage begin/end/next/prev, append.
* **Rings** `ring.c`: the two 4 KB character rings.
* **Lifecycle** `engine.c`: construct, init, reset, pitch/speed/volume/voice
  setters, step scheduler.
* **Synthesizer state** `synth.c`: parameter tracks, filter coefficient
  selection (8000/11025 Hz), parameter scaling, track position ops,
  plus `frame.c`: `Synth_Frame`, which reads the 22 parameter tracks,
  applies the per-voice adjustments, derives the two half periods with a
  jitter LFSR, and fills in the 40 filter coefficients.
* **Synthesizer core** `generate.c`: `Synth_Generate`, the sample loop --
  a table-interpolated glottal pulse, the cascade of formant resonators,
  the parallel fricative branch, de-emphasis and the output scaling, all
  in the original 16-bit fixed point.
* **Parameter tracks** `track.c`: the four ways stage 3 lays a value into a
  parameter track -- a straight line, one of the twelve decay curves, and the
  two directions of blending a curve into what is already there.
* **Stage 3** `stage3.c`: so far the driver -- the three-cursor walk that
  moves one phoneme per step, asks the tracks for room for its frames and
  decides when the utterance has ended -- plus the vowel index and the
  neighbour-phoneme accessor; `Stage3_Next`, which looks past the control
  nodes for the next phoneme and makes a silence to stop on when there is
  none; `Stage3_Pause` and `Stage3_Hold`, which write a held sound one
  repeated frame at a time; `Stage3_Phone`, the per-phoneme setup;
  `Stage3_Write`, which turns a parameter's travel into calls on the track
  shapers; and `Stage3_Emit`, which commits the frames a step wrote.  The
  stage keeps its working state in `s3_param[22]`, a 28-byte record per
  synthesis parameter -- how it travels, along which curve, from where to
  where -- that sits between the track bookkeeping and the tracks.
  `Stage3_Rules` and `Stage3_Apply` are the rule machinery: for each pair of
  phoneme classes a list of rules is tried in order, each a small byte-code
  program of conditions followed by parameter edits and a list of routines
  to run.  All sixteen of those routines are done, along with
  `Track_Nudge`, the shaper they use to fade a correction back into frames
  that have already been written.  `Stage3_Targets` is the table loader: every
  phoneme's four formants, their bandwidths, the cascade and parallel
  amplitudes and the pitch come out of tables read with the phoneme's class,
  followed by the stop-burst and release rules the tables cannot hold.
  The larger routines are `Stage3_Op4` (the nasals, which split their run
  into a murmur and a release and write each half separately),
  `Stage3_Op9` (the stop bursts), `Stage3_Op14` (where every parameter
  starts the phoneme, and the exceptions to the halfway rule) and
  `Stage3_Op16`, which is a dispatcher: fourteen per-phoneme routines, one
  for each consonant that does something nothing else does.  The glide
  helpers are there too -- `Stage3_LayGlide` writes a formant and its
  bandwidth across a glide, `Stage3_SetGlide` installs the end points, and
  `Stage3_GlideTab` looks a pair of phonemes up in the packed glide tables,
  falling back on stand-ins when the pair is not in them.  `Stage3_Reduce`
  is the vowel-reduction pass: an unstressed vowel between two consonants
  does not hold its own formants but follows a path the tables keep for that
  pair of neighbours, bit-packed four fields at a time, which it unpacks and
  hands to `Stage3_LayPath`.
* **User lexicon** `lexicon.c`: `UserLex_Add`, the entry point the SAPI
  lexicon calls, which upper-cases the spelling, copies both strings onto
  the engine's heap and keeps the table sorted for the binary search.
* **Standalone build** `src/port/`: `main.c`, the command-line driver that
  does what the SAPI engine thread did for one `TextData` call; `msvcrt.c`,
  the MSVC 4.2 runtime functions the engine calls -- the character classes
  and `atol` read the "C" locale table the original CRT built into its own
  data, so bytes over 0x7f classify the same way; and `stubs.c` for the one
  call the engine makes back into the layer above it.  The constant tables
  come from `data/en/engine.tvdata`, which is in the repository, so both
  the build and the program need no Centigram binary: the program loads no
  DLL, talks to no SAPI and reads no registry, and its only import is the
  C runtime.
* **Stage resets** `stages.c` + stage 4 driver.
* **Feeding** `feed.c`: `Engine_Feed`, `Engine_Flush`.
* **Preformatter** `preformat.c`: accent folding, `ESC[..X` command parser.
* **TextIn** `textin.c`: token list, bit sets, tokenizer, escape reader,
  splitter, classifiers, expansion, emit/flush driver.
* **Input stage** `input.c`.
* **Control commands** `control.c`: the per-stage executor for the embedded
  ESC commands, plus `sapi.c` for the few call-backs into the SAPI layer.
* **Stage 0** `stage0.c`: the rule interpreter (19 condition opcodes, 23
  action opcodes, rule call stack, word-list matching, emit/finish), and
  `phonetic.c` for the bracket mode that reads phoneme names instead of
  words ("[HH AH L OW]").
* **Stage 1** `stage1.c`: the driver, span gathering, the prosody pass
  (`Stage1_Pronounce`), stress marking and syllable numbering, the affix
  rule machinery -- `Lts_TestContext` (the context-condition byte code),
  `Lts_MatchAffix`, `Lts_ApplyAffix` (stem spelling repair) and the
  `Stage1_Lookup` prefix/suffix stripping driver -- and the phrase prosody
  (`Stage1_Phrase`, `Stage1_PhraseEnd`).
* **Lexicon** `lexicon.c`: the closed-class word classifier and the user
  lexicon (the sorted table the SAPI lexicon calls fill in).
* **Dictionary** `dict.c`: the built-in pronunciation dictionary -- a
  bit-packed blob walked with two small state machines -- plus the homograph
  disambiguation that picks a reading from the surrounding words.
* **Letter to sound** `lts.c`: `Stage1_Rules`, the rule engine that walks a
  word backwards turning letters into phonemes and placing the stress, plus
  `Lts_Syllable` (yod coalescence and vowel reduction).
* **Stage 2** `stage2.c`: the driver and its look-ahead state machine
  (`Stage2_Next` rewinds over a clause break and makes a second pass), the
  phoneme emitter, the context cache (`Stage2_Context`), the attribute and
  stress scanners, gemination (`Stage2_Merge`), the pause inserter
  (`Stage2_Break`), the pitch-contour pass and the duration rules
  (`Stage2_DurRules`, `Stage2_DurStress`, `Stage2_DurFast`, `Stage2_MinDur`),
  plus `Stage2_Begin`, the scan that gathers a phrase's worth of nodes and
  picks the intonation contour from the boundary marks it passed, and
  `Stage2_Phrase`, which turns those marks into clause breaks and breaths,
  and `Stage2_Contour`, which walks that contour to give each node its pitch.
  `Stage2_DurNasal` and `Stage2_DurVowel` are two of the four duration
  tables: they size the sonorants and the vowels from where they sit in the
  word, what is either side of them and whether a stress follows, then apply
  the closed-syllable and word-class corrections on top.  `Stage2_DurFric`
  sizes the fricatives, which need a decision tree of their own before the
  grid can be read, and `Stage2_DurStop` sizes the stops and decides how each
  one is released (aspirated, unreleased, flapped), which it leaves in the
  node for stage 3.
* **Allophones** `vowel.c`: `Stage1_Vowel` (7.6 KB, the largest function in
  the engine) - the per-vowel rule pass that darkens "L", drops "H", flaps
  and glottalises the stops, colours the vowels before "R", and inserts the
  pauses and glides, followed by a shared clean-up pass.

## Where this stands

Taking the call tree below the twelve entry points the CLI uses, excluding
the MSVC C runtime and the SAPI/COM/UI layer, the engine is 227 functions
and 151,143 bytes.  223 of them are decompiled: 98.2% by function, 99.7% by
byte.  The four that are not, 437 bytes between them, are the SAPI glue
itself -- two containers, each with an append and a grow:

* `ByteList_Append` (`0x100311d0`) and its grow routine (`0x10031120`),
  which is the one genuinely awkward case: it allocates through
  `CoGetMalloc` and `IMalloc::Realloc`, so writing it would make the
  portable build depend on COM.  The list holds the phoneme trace for
  `ITTSDialogs`, and the engine only fills it when `Engine.w_212c` is set,
  which nothing outside SAPI does.  `src/port/stubs.c` defines the append
  as a no-op.
* `SapiQueue_Push` (`0x100385b0`) and its grow routine (`0x10038530`),
  which are plain `malloc`/`realloc` with 1.5x growth and would be easy;
  they are absent because the queue they manage belongs to the SAPI thread,
  which a CLI does not have.  `src/engine/sapi.c` stubs the push.

Neither structure reaches the audio, which is why stubbing both still gives
byte-identical output.  Everything else the tree still references is the
MSVC C runtime, which the portable build takes from the host.

Of the 10,816 basic blocks in those 223 functions the corpus executes
9,886 (91%); the 930 that are left sit in 87 functions and are written from
the disassembly without ever having been run.  Growing the corpus to close
that gap (see below) turned up six transcription errors that the earlier
302 configurations had never exercised, all now fixed: a wrong stack slot
in `Stage3_Op4` (`c_ctl` where the original reads `c_cur`), an inverted
attribute test in the `B` arm of `Stage2_DurAdjust`, an inverted
vowel-search condition and an inverted word-boundary test in
`Stage1_Vowel`, an inverted `prev_ch` test at the `[` in `Stage1_Rules`,
and an `H` test in `Lts_Syllable` that the original reaches only through
the `U` branch.  Five of the six are a single inverted condition, which is
what transcribing `jne`/`je` by hand gets wrong; the last is the one place
where a jump out of a branch skips code that reads like a separate
statement.

## Pointer width

The engine builds and runs at 32 and 64 bits, from the same sources, with
byte-identical output.  The 64-bit build is the first there has ever been.

What made it possible was *not* widening the data.  The engine reads past
the end of its tables on purpose -- `Stage3_GlideTab` asks `g_gt0_info` for
element 11 of a table of four and uses what it finds in the table after it
-- and the values that come back are load-bearing: forcing one to a
constant broke 328 of the 335 configurations.  Widening the stored
addresses moves everything after each one, so those reads land somewhere
else.

So the extracted data keeps the original's bytes exactly, and a stored
address stays four bytes wide whatever a pointer is on the host.  `tv_ref`
(src/tv_ref.h) is that stored address: an offset from `tv_data` in the
standalone build, the loader's own address in the hook build.  The five
structures the data holds keep the original's layout at any width for the
same reason, since their pointer members are `tv_ref` too.

The rest of the port was four width assumptions in the engine:

* `dict.c` walked the dictionary's links as `uint32_t` and strode its
  tables by a literal 4.
* `lts.c` and `stage2.c` stepped rule arrays by a literal `0x14`, which is
  `sizeof(LtsEntry)` and `sizeof(DurRule)` at four-byte pointers.
* The user lexicon is written as well as read, and at eight-byte pointers
  its entries no longer fit the room the image left for them, so outside
  the hook build the library owns that table (lexicon.c).  The original
  ships it empty, so it is the same table.
* The bookmark record was queued with a literal 4 for the size of a
  pointer.

Two raw-offset accesses are left, both unreachable: `Engine_ZeroDwordIfMinus1`
(layout.c), which the original only reaches on pathological input, and a
read at `textin.c:916` through `Token.d18`, which is only ever NULL.

## Next

The four SAPI glue functions, if the phoneme trace is ever wanted.  Nothing
else is outstanding: see docs/LIBRARY.md for the library the engine is
packaged behind.

## OpenTV extensions

The project is a decompilation rather than a patched binary, so it can fix
what the original got wrong.  Everything that changes engine behaviour sits
behind `tvtts_set_extensions`, on by default, and `tools/difftest.py` runs
the corpus with them off.  That is deliberate: the byte-exact corpus is the
only evidence the decompilation is correct, so it has to keep comparing
against a binary that has no extensions.

* **`TVTTS_EXT_RATE`** -- `Engine_SetSpeed` turns wpm into a row of a 26-row
  table with `(wpm - 46) >> 3`, and the original indexed off the end above
  row 25 and wrapped unsigned below 46.  The extension clamps the index and
  scales durations down for the rows it adds (`rate_row` and `rate_pct` in
  `stage2.c`), reaching 400 wpm.  Duration scaling is the lever rather than
  more table rows because `Stage2_DurRules` ends at
  `(max - min) * acc/100 + min`, so `g_phone_dur`'s minimum column is a
  floor the table cannot get under.  Rows 0..25 are untouched and 46..253
  wpm stays bit-for-bit identical to `CGRM_EN.DLL`.

## Test corpus

`tests/corpus/*.txt` (59 inputs), run in 335 configurations: ten voices at
11025 and 8000 Hz, pitch/speed/volume variants, PreFormat and TextIn on and
off, embedded ESC commands, quoted-mail mode, cp1252 text, malformed
escapes, phoneme input with `/pitch;duration/` annotations, skim mode
(`ESC[2f`), `ESC[..N`/`ESC[..F` flag changes, bracket spell mode, English
morphology and contractions, homographs in context, user-lexicon entries
added with `-L`, rate and phrase commands embedded mid-sentence,
sonorant-dense text, and the sample texts shipped with TruVoice when
present.  A `NAME.opts` file next to an input pins its harness options.

The last inputs (54 onwards) are generated rather than written:
`tools/covgen.py` makes random text, bracket-phoneme and escape-sequence
lines, runs each candidate through the oracle with block coverage on, and
keeps only the ones that reach blocks nothing else did.  `--args` passes
harness options through and writes the matching `.opts`.

## Known deviations

* `TextIn_ReadEscape` bounds its buffer; the original overruns it for
  `ESC[` sequences longer than 17 characters (no reference output exists).
* Three functions are written from the disassembly but the corpus never
  reaches them, so they are the ones not verified by execution:
  `Stage1_VowelAux` (0x10064200), `Stage2_DurFast` (0x1005aa90) and
  `TextIn_Error` (0x1001ae80).  `Stage1_VowelAux` in particular is guarded
  by an exact phoneme sequence ("& P R e S q n" with the accent flag set
  and `q->b14 != 2`); 400 randomised "press" sentences and targeted
  bracket-mode sequences all failed to reach it.  `Stage3_NasalPole`
  (0x10048ef0) runs 3 of its 9 blocks.
* A few places read a stack slot the original never writes on that path, so
  what they read is whatever the last call left there.  In `Stage3_Op4` and
  `Stage3_LayGlide` the value only reaches a comparison that cannot hold
  either way; in `Stage3_GlideTab` and `Stage3_Reduce` it stands in for
  table pointers that one arm of the group switch leaves unset, and the
  corpus never takes that arm.  Each is written to mirror what the original
  slot holds at that point and is commented where it appears.
* `Stage3_GlideTab` works out how wide each of four packed fields is and
  then never uses the answers; the four loops are left out, with a comment.
* Two branches are unreachable behind their own guards and are left out with
  a comment: the "D before a vowel" arm of `Stage3_Op9`, which is inside a
  test for a space that the same value cannot satisfy, and the "M" tail of
  `Stage3_Op4`, which compares a slot that only ever holds 3 or 13 with 9.
* The reference recordings in `ref/` cover the default settings only, so
  the voice, pitch, speed and volume variants are checked against the DLL
  but not against the shipping product.
* The duration tables are wide decision trees and the corpus does not reach
  every leaf: `Stage2_DurFric` runs 571 of its 680 basic blocks (84%),
  `Stage2_DurVowel` 361 of 398 (91%) and `Stage2_DurStop` 888 of 930 (95%).
  The rest are written from the disassembly.  For `Stage2_DurFric` an
  exhaustive sweep of 2,688 candidates (previous group x fricative x next
  group x stress x following context) showed the remaining blocks are not
  reachable by varying those: they need word-class context that bracket
  phoneme input bypasses, or are dead for English.
* `Stage2_Begin` has two paths the corpus cannot reach.  The phoneme
  trace it writes to the COM byte list needs `w_212c`, which only the SAPI
  layer sets, and the boundary marks with argument 7 and 8 (the rising and
  rise-fall intonation marks the bracket syntax writes as "/" and "/\\")
  never survive to stage 2, which also leaves the contour branches that
  need `s2_1d7c` unexecuted.
