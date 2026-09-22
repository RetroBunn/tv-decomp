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

## Done (120 functions)

* **Node/stage core** `node.c`: list insert/unlink, pool reset, node
  alloc/free, stage begin/end/next/prev, append.
* **Rings** `ring.c`: the two 4 KB character rings.
* **Lifecycle** `engine.c`: construct, init, reset, pitch/speed/volume/voice
  setters, step scheduler.
* **Synthesizer state** `synth.c`: parameter tracks, filter coefficient
  selection (8000/11025 Hz), parameter scaling, track position ops,
  plus the fixed-point helpers and frame gate in `frame.c`.
* **Synthesizer core** `generate.c`: `Synth_Generate`, the sample loop --
  a table-interpolated glottal pulse, the cascade of formant resonators,
  the parallel fricative branch, de-emphasis and the output scaling, all
  in the original 16-bit fixed point.
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
* **Allophones** `vowel.c`: `Stage1_Vowel` (7.6 KB, the largest function in
  the engine) - the per-vowel rule pass that darkens "L", drops "H", flaps
  and glottalises the stops, colours the vowels before "R", and inserts the
  pauses and glides, followed by a shared clean-up pass.

## Next

1. Stage 2 (0x1002b2b0) and its subtree (38 functions, 33 KB).
2. Stage 3 (0x1002c980) and its subtree (57 functions, 57 KB).
3. `Synth_Frame` (`0x10002a40`, 2.6 KB) - builds filt_coef for each frame
   from the 22 parameter tracks.
4. Portable build: MSVC-compatible CRT pieces, data extraction from the DLL,
   `Engine_Read32/Write32` for the raw-offset accesses (see layout.c).

## Test corpus

`tests/corpus/*.txt` (40 inputs), run in 294 configurations: ten voices at
11025 and 8000 Hz, pitch/speed/volume variants, PreFormat and TextIn on and
off, embedded ESC commands, quoted-mail mode, cp1252 text, malformed
escapes, phoneme input with `/pitch;duration/` annotations, skim mode
(`ESC[2f`), `ESC[..N`/`ESC[..F` flag changes, bracket spell mode, English
morphology and contractions, homographs in context, user-lexicon entries
added with `-L`, and the sample texts shipped with TruVoice when present.  A
`NAME.opts` file next to an input pins its harness options.

## Known deviations

* `TextIn_ReadEscape` bounds its buffer; the original overruns it for
  `ESC[` sequences longer than 17 characters (no reference output exists).
* `Stage1_VowelAux` (0x10064200) is written from the disassembly but the
  corpus never reaches it, so it is the one function not verified by
  execution.
