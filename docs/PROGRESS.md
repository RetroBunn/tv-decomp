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

## Done (164 functions)

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
  neighbour-phoneme accessor, and `Stage3_Emit`, which commits the frames a
  step wrote.  The stage keeps its working state in `s3_param[22]`, a
  28-byte record per synthesis parameter that sits between the track
  bookkeeping and the tracks themselves.
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

## Next

1. Stage 2 is done apart from two stubs the CLI never reaches: the
   COM-allocated byte buffer at `0x10031120`/`0x100311d0` (the phoneme
   trace; needs a portable replacement) and the SAPI queue helpers at
   `0x10038530`/`0x100385b0`.  Everything else its call tree still
   references is the MSVC C runtime, which the portable build takes from
   the host.
2. The rest of stage 3: `Stage3_Phone` (0x1002da60) and the per-phoneme
   parameter rules below it (52 functions, 55 KB).
3. Portable build: MSVC-compatible CRT pieces, data extraction from the DLL,
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
* `Stage1_VowelAux` (0x10064200) is written from the disassembly but the
  corpus never reaches it, so it is the one function not verified by
  execution.
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
