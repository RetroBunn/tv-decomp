# Decompilation progress

Verification: `python tools/difftest.py --full` compares the decompiled code
(hooked into the running original) against the original over the corpus.
All listed functions are byte-exact on every corpus input.

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
the MSVC C runtime and the SAPI/COM/UI layer, the engine is 226 functions
and 151,143 bytes.  223 of them are decompiled: 98.2% by function, 99.7% by
byte.  What is left is the four functions that are the SAPI glue itself --
the COM-allocated byte buffer at `0x10031120`/`0x100311d0` (the phoneme
trace, which needs a portable replacement) and the SAPI queue helpers at
`0x10038530`/`0x100385b0`.  Everything else the tree still references is the
MSVC C runtime, which the portable build takes from the host.

## Next

1. Portable build: MSVC-compatible CRT pieces, data extraction from the DLL,
   `Engine_Read32/Write32` for the raw-offset accesses (see layout.c).

## Test corpus

`tests/corpus/*.txt` (42 inputs), run in 302 configurations: ten voices at
11025 and 8000 Hz, pitch/speed/volume variants, PreFormat and TextIn on and
off, embedded ESC commands, quoted-mail mode, cp1252 text, malformed
escapes, phoneme input with `/pitch;duration/` annotations, skim mode
(`ESC[2f`), `ESC[..N`/`ESC[..F` flag changes, bracket spell mode, English
morphology and contractions, homographs in context, user-lexicon entries
added with `-L`, rate and phrase commands embedded mid-sentence, sonorant-dense text, and
the sample texts shipped with TruVoice when present.  A
`NAME.opts` file next to an input pins its harness options.

## Known deviations

* `TextIn_ReadEscape` bounds its buffer; the original overruns it for
  `ESC[` sequences longer than 17 characters (no reference output exists).
* Four functions are written from the disassembly but the corpus never
  reaches them, so they are the ones not verified by execution:
  `Stage1_VowelAux` (0x10064200), `Stage2_DurFast` (0x1005aa90),
  `Stage3_NasalPole` (0x10048ef0) and `TextIn_Error` (0x1001ae80).
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
* The duration tables are wide decision trees and the corpus does not reach
  every leaf: `Stage2_DurFric` runs about 60% of its 680 basic blocks,
  `Stage2_DurVowel` about 80% and `Stage2_DurStop` about 84%.  The rest are
  written from the disassembly.
* `Stage2_Begin` has two paths the corpus cannot reach.  The phoneme
  trace it writes to the COM byte list needs `w_212c`, which only the SAPI
  layer sets, and the boundary marks with argument 7 and 8 (the rising and
  rise-fall intonation marks the bracket syntax writes as "/" and "/\\")
  never survive to stage 2, which also leaves the contour branches that
  need `s2_1d7c` unexecuted.
