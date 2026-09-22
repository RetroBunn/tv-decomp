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

## Done (83 functions)

* **Node/stage core** `node.c`: list insert/unlink, pool reset, node
  alloc/free, stage begin/end/next/prev, append.
* **Rings** `ring.c`: the two 4 KB character rings.
* **Lifecycle** `engine.c`: construct, init, reset, pitch/speed/volume/voice
  setters, step scheduler.
* **Synthesizer state** `synth.c`: parameter tracks, filter coefficient
  selection (8000/11025 Hz), parameter scaling, track position ops.
* **Stage resets** `stages.c` + stage 4 driver.
* **Feeding** `feed.c`: `Engine_Feed`, `Engine_Flush`.
* **Preformatter** `preformat.c`: accent folding, `ESC[..X` command parser.
* **TextIn** `textin.c`: token list, bit sets, tokenizer, escape reader,
  splitter, classifiers, expansion, emit/flush driver.
* **Input stage** `input.c`.
* **Control commands** `control.c`: the per-stage executor for the embedded
  ESC commands, plus `sapi.c` for the few call-backs into the SAPI layer.
* **Stage 0** `stage0.c`: the rule interpreter (19 condition opcodes, 23
  action opcodes, rule call stack, word-list matching, emit/finish).

## Next

1. `Stage0_Spell` (0x10032230) - letter-by-letter spelling, reached when
   stage 0 runs in spell mode.
2. Stage 1 (0x10062830) - word pronunciation / lexicon, and its subtree.
3. Stage 2 (0x1002b2b0).
4. Stage 3 (0x1002c980) - phonetics to the 22 parameter tracks.
5. Synthesizer (`0x10025cb0`, `0x10002a40`) - the DSP core.
6. Portable build: MSVC-compatible CRT pieces, data extraction from the DLL,
   `Engine_Read32/Write32` for the raw-offset accesses (see layout.c).

## Test corpus

`tests/corpus/*.txt` (30 inputs), run in 257 configurations: ten voices at
11025 and 8000 Hz, pitch/speed/volume variants, PreFormat and TextIn on and
off, embedded ESC commands, quoted-mail mode, cp1252 text, malformed
escapes, and the sample texts shipped with TruVoice when present.  A
`NAME.opts` file next to an input pins its harness options.

## Known deviations

* `TextIn_ReadEscape` bounds its buffer; the original overruns it for
  `ESC[` sequences longer than 17 characters (no reference output exists).
