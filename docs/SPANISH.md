# Spanish

A second decompilation, of a different engine. This file is the working
record: what has been established, how, and what is left.

## Why it is a second decompilation

`CGRM_ES.DLL` is not the English engine with Spanish tables in it. It is
two years older, a third of the code, and has a `.bss` section English does
not. Of the functions this project annotates in `CGRM_EN.DLL`, **12-16%**
appear in `CGRM_ES.DLL` at all, and by module the figure is **zero** for
stage 0 through stage 3, `synth.c`, `generate.c`, `engine.c` and
`preformat.c`. The four 1995 engines — German, Spanish, French, Italian —
share 40-47% with *each other*, so they are one family and English is
another. docs/VOICES.md has the table.

What does carry over is the synthesiser. These are byte-identical between
the two generations:

* `g_syn8k_*` and `g_syn11k_*`, the resonator tables
* `g_param_max`, `g_param_init`, `g_default_params`
* `g_hold_atten`

So the Klatt cascade and its 22-track parameter model are the same design in
both, and everything this project worked out about them applies to either.
What differs is the language front end and the prosody and timing on top.

## The method: anchor on shared data

Reading 766 unknown functions cold is not the way in. The shared tables give
a way to find things instead: whatever code reads a table we already
understand is the part of the engine we already understand.

Scan the relocations inside `.text` for entries pointing at an anchor table,
map each to its containing function, and the synthesiser falls out. That is
how `sub_1000e6f0` was found — the only function referencing all six
resonator tables.

It is `Synth_InitFilters`, beyond reasonable doubt:

* an 8 kHz branch and an 11 kHz branch, each assigning `syn_tab[6]`, `[7]`
  and `[8]` from `g_syn8k_*` or `g_syn11k_*`
* the ten `syn_tab` entries assigned in the order 9, 6, 7, 8, 0, 1, 2, 3, 4,
  5 — the same order as the English function
* two 40-entry coefficient arrays built on the stack from the English
  `filt_coef_8k` and `filt_coef_11k` values exactly, down to `30905`, the
  fixed 242 Hz resonator constant derived for the 16 kHz work
* the three per-rate constants written with the English values: -7870,
  7281, -6472 at 8 kHz and -7956, 7521, -6905 at 11 kHz

From one function that yields fifteen field offsets. Its caller, which sets
a flag to 2 and then runs a chain of resets ending in this one, is
`Engine_Reset` by the same reasoning, and `sub_10017850` — 22 iterations,
seeding each track's first 12 frames from a byte table, tracks 0x100 bytes
apart — is `Synth_ResetTracks`.

## The object

`es/engine.fields` holds what is established, in the format
`tools/gen_struct.py` already reads, so it generates a header the same way
the English one does.

The shape of the difference is worth stating plainly: **the same field set
in a different order.**

| | English | Spanish |
|---|---|---|
| 22 parameter tracks | 0x0000 | 0x6ddc |
| `filt_coef[40]` | 0x19b0 | 0x0004 |
| `syn_tab[10]` | 0x2044 | 0x0660 |
| `sample_rate` | 0x210c | 0x0720 |
| object size | 0x9184 | at least 0x8788 |

Within the track block the two agree in places and not others — `trk_data`
sits at block+0x3ac in both, while `trk_rd` and `trk_wr` are the other way
round — which is what two builds of a common ancestor look like, rather than
a rewrite.

## Functions identified so far

`Engine_Reset` is the lever.  The English one clears a run of flags, sets
`out_state` to 2 and then calls eleven reset routines in a fixed order.
`sub_10008700` does the same thing in the same order, so the call list maps
straight onto the English one:

| Spanish | English | how it was confirmed |
|---|---|---|
| `sub_10008700` | `Engine_Reset` | clears the same flags, `out_state = 2`, then the chain |
| `sub_10007790` | `Preformat_Reset` | position in the chain |
| `sub_10008ef0` | `Engine_ResetNodes` | position in the chain |
| `sub_10017850` | `Synth_ResetTracks` | 22 tracks x 12 frames from `g_param_init`, 0x100 apart |
| `sub_1000e6f0` | `Synth_InitFilters` | the resonator tables, in English's own assignment order |
| `sub_1000e240` | `Engine_ResetRings` | position in the chain |
| `sub_1000ad50` | `Output_Reset` | writes 0xaaaa, then divides its pushed rate argument by 100 |
| `sub_10004810` | `Stage4_Reset` | position in the chain |
| `sub_1000f070` | `Prosody_Reset` | position in the chain |
| `sub_1001b810` | `Stage3_Reset` | position in the chain |
| `sub_1001a8b0` | `Stage2_Reset` | position in the chain |
| `sub_1000fa10` | `Stage1_Reset` | position in the chain |
| `sub_10013310` | `Stage0_Reset` | sets `s0_ip` to the rule bytecode, `s0_need_test = 1`, `s0_pending_ptr` |
| `sub_10007810` | `Preformat_Run` | the whole `ESC [` command set, with English's constants |
| `sub_100086a0` | `Engine_Init` | the ten defaults, 22 bytes of `g_default_params`, then tail-jumps to `Engine_Reset` |
| `sub_100087d0` | `Engine_Step` | checks `reset_pending`, runs stage 4, cascades the stages, returns `and eax, 2` |
| `sub_1000e5a0` | `Engine_InFree` | `rd - wr - 1`, `+= 0x1000` |
| `sub_1000e5c0` | `Engine_InGet` | -1 when empty, else advance and read |
| `sub_1000e4f0` | `Engine_MidFree` | the same shape on the mid ring |
| `sub_1000e550` | `Engine_MidPut` | writes `mid_ring[mid_wr]`, wraps at 0x1000 |
| `sub_10013360` | `Stage0_Run` | called by `Engine_Step`; sits 0x50 past `Stage0_Reset` |
| `sub_1000fa30` | `Stage1_Run` | 0x20 past `Stage1_Reset` |
| `sub_1001a070` | `Stage2_Run` | called by `Engine_Step` in stage order |
| `sub_1001ad30` | `Stage3_Run` | called by `Engine_Step` in stage order |
| `sub_10004820` | `Stage4_Run` | 0x10 past `Stage4_Reset` |
| `sub_10017900` | `Synth_Step` | 0xb0 past `Synth_ResetTracks` |
| `sub_1000ddc0` | `Engine_Construct` | sets 8000, 85, 150, 0xffff in English's own order |
| `sub_1000e600` | `Engine_InUnget` | backs `in_rd` up one, wrapping at 0x1000 |
| `sub_1000e630` | `Engine_InPut` | needs 10 free, folds 0x92 to an apostrophe |
| `sub_1000e680` | `Engine_InPutEnd` | pushes `ESC [ 0 i`, `ESC [ C` and two spaces |
| `sub_1001c310` | `Engine_Feed` | `ret 0xc`, six `Engine_InPut` calls and an `InPutEnd` |
| `sub_1001c6c0` | `Engine_CreateTextIn` | allocates 0xac, stores `engine` and `textin` |
| `sub_1001c710` | `Engine_Flush` | the `textin_on` branch, else `InGet` to `PutChar` up to 400 |
| `sub_1001c790` | `TextIn_Construct` | the object `Engine_CreateTextIn` builds |
| `sub_1001c850` | `TextIn_Reset` | called on a new item |
| `sub_1001c8a0` | `TextIn_Flush` | called with 0 on every flush |
| `sub_1001dc10` | `TextIn_GetChar` | -1 with no engine, -2 at end of input |
| `sub_10008060` | `Preformat_PutChar` | the non-TextIn path out of `Engine_Flush` |
| `sub_10008a40` | `Engine_SetPitch` | writes `pitch`, then five `stage_ctx` at stride 0x44 |
| `sub_10008a70` | `Engine_SetSpeed` | `idx = (wpm - 46) >> 3`, exactly English's |
| `sub_10008aa0` | `Engine_SetVolume` | `-10 * log10(vol / 65535)`, capped at 15 |
| `sub_10008b10` | `Engine_SetVoice` | rejects >= 10, then the same five writes |
| `sub_1000ead0` | `Stage_Emit` | reads `stage`, compares it against `stage_ctx[2]` |
| `sub_10001370` | `Lexicon_Add` | caps at 5000 entries, stores both strings at `0x1002e000` |
| `sub_100014f0` | `Lexicon_Remove` | frees the pair and decrements the count |

All twelve are now confirmed by their contents, not by their position.
What settled the five stage resets was the `type_mask` each one writes:
**0x17, 0x06, 0x1c, 0x28, 0x3f -- identical to English's, in the same
order** -- and `StageCtx` measures 0x44 in both engines, so the struct
itself is very likely shared and only its base has moved.

The others carry their own evidence.  `Engine_ResetRings` zeroes three
wr/rd pairs, writes -1, then two flag defaults.  `Prosody_Reset` sets three
fields with 85 first.  `Stage4_Reset` is a single store of 0x3f.
`Output_Reset` writes 0xaaaa and divides its pushed rate argument by 100.

Two differences are worth recording.  The ring flag defaults are **0x1780
and 0x40** here against English's **0x17c0 and 0x41** -- one bit more in
each, so English added an escape flag.  And the 1995 stage 1 and stage 2
keep visibly less state: `Stage1_Reset` sets two fields where English sets
nine, and `Stage2_Reset` writes 1, 0xd, 5, 7, 0x14 where English writes 3,
-1, -1, 180, -1.

`Stage0_Reset` was briefly miscounted as a call English does not make; it
does, at `engine.c:81`, after `Stage1_Reset`.  The two chains are the same
twelve in the same order.

## The preformat parser, and what it gave up

Hunting for `Engine_SetSpeed` by its unmistakable `(wpm - 46) >> 3` found
something much larger: `sub_10007810`, the `ESC [` parser, which is where
that arithmetic actually lives.  It is the counterpart of English's
`Preformat_Run` and it carries the whole command set -- `ESC[..p` clamping
25..200 and doubling, `ESC[..r` flooring at 50 and defaulting to 150,
`ESC[..f` clamping the rate class against `g_rate_class_max`, `ESC[<i>;<v>l`
clamped against `g_param_max` -- all with the same constants as English.

Its reset-to-defaults handler is the valuable part.  It writes every
preformat field and then copies them into all five stage contexts, which
places two blocks of the object at once and settles what `StageCtx` is:

```
lea ecx, [edi + 0x754]        ; stage_ctx[0]
mov edx, 5
loop:  add ecx, 0x44          ; the English StageCtx stride
       ... writes ecx-0x28 .. ecx-4, stepping over ecx-8
```

Those nine writes land on +0x1c through +0x40 of each context and skip
+0x3c.  In English that is exactly `p_1c`, `p_20`, `rate_index`, `pitch`,
`volume_atten`, `p_30`, `p_34`, `p_38` and `voice`, with `type_mask` at
+0x3c left for the per-stage resets to set -- which is precisely what the
five `*_Reset` routines do.  **The Spanish `StageCtx` is English's, field
for field.**

That also corrected an assumption.  The type_mask writes made it look as
though `stage_ctx` began at 0x790 with the mask at +0; it begins at 0x754
with the mask at +0x3c, and the `lea` in this function says so outright.

## The rings, and a lesson about tail calls

`Engine_Step` gave up the four ring primitives, and they are English's
shape exactly -- `rd - wr - 1`, and if that went negative add the ring
size.  Their bases follow from where they read and write, and the result
is tidy: **each ring ends exactly where its own rd/wr pair begins.**

| ring | span | size | rd | wr |
|---|---|---|---|---|
| `in_ring` | 0x4cc0..0x5cc0 | 0x1000 | 0x5cc0 | 0x5cc4 |
| `pre_ring` | 0x5cc8..0x5dc8 | 0x100 | 0x5dc8 | 0x5dcc |
| `mid_ring` | 0x5dd0..0x6dd0 | 0x1000 | 0x6dd0 | 0x6dd4 |

`pre_ring` being 0x100 rather than 0x1000 is not a guess: `Preformat_Run`
masks its index with 0xff.  This closes the question left open last time
about which pair belonged to which ring.

`Engine_Init` was nearly missed.  Scanning for callers of `Engine_Reset`
found only `Engine_Step`, because `Engine_Init` reaches it with a `jmp`,
not a `call` -- the tail call of English's `return Engine_Reset(self)`.  A
caller scan that only looks for `E8` will keep missing these; it needs to
look for `E9` as well.

## Data symbols located

| symbol | Spanish address | how |
|---|---|---|
| `g_param_max` | 0x10061358 | byte-identical to English's |
| `g_param_init` | 0x100613b0 | read by `Synth_ResetTracks` |
| `g_default_params` | 0x100613c8 | byte-identical to English's |
| `g_rate_class_max` | 0x10049760 | indexed by the `ESC[..f` rate class |
| `g_stage0_rules` | 0x1005fb88 | what `Stage0_Reset` points `s0_ip` at |
| `g_syn8k_*`, `g_syn11k_*` | 0x10049d08.. | byte-identical to English's |

## The two layouts run in step

Once several fields were placed, a pattern appeared.  Through the
synthesiser and output region the Spanish offsets are the English ones less
a constant **0x19e4**:

| field | English | Spanish | difference |
|---|---|---|---|
| `filt_coef` | 0x19b0 | 0x0004 | 0x19ac |
| `syn_2038` | 0x2038 | 0x0654 | **0x19e4** |
| `syn_tab` | 0x2044 | 0x0660 | **0x19e4** |
| `o_2088` | 0x2088 | 0x06a4 | **0x19e4** |
| `o_rate_div100` | 0x2090 | 0x06ac | **0x19e4** |
| `sample_rate` | 0x210c | 0x0720 | 0x19ec |

That turns the English layout into a way of predicting where a Spanish
field will be: subtract 0x19e4, then look. It is a hint and not a rule --
where it breaks is exactly where English grew, 0x38 bytes before
`filt_coef` and another 8 after the output block -- but a hint that says
where to look is most of the work.

## The host interface block, and the four setters

The four parameter setters were found by shape rather than by position:
each writes one field of the preformat block and then walks the five
stage contexts at stride 0x44, so a scan for small functions stepping by
0x44 returned exactly four candidates and no false ones.

They are worth more than four addresses, because each one *proves* an
offset that had been placed by inference.  `Engine_SetSpeed` computes
`(wpm - 46) >> 3` -- the same 46 floor and the same three-bit
quantisation as the 1997 engine, into the same 26-row table -- and stores
the result at 0x5c with the word rate at 0x60.  `Engine_SetVolume`
computes `-10 * log10(vol / 65535)` from two doubles in `.rdata`, caps it
at 15, and writes 0x68.  `Engine_SetVoice` rejects anything past ten and
writes 0x78.  `Engine_SetPitch` writes 0x64.  All four then write
`stage_ctx` at +0x24, +0x28, +0x2c and +0x40, which are English's
`rate_index`, `pitch`, `volume_atten` and `voice`.

`Engine_Construct` closed the rest.  It writes `sample_rate`, `sapi`,
`item_notify`, `item_done`, `cur_voice`, `cur_bac`, `mode_I_on`, a flag
at 0x1d8, and then 85, 150 and 0xffff -- the same fields in the same
order as the English constructor, minus `w_2130` and `w_2132`.  Reading
that order against English's names the whole block; `Engine_Flush` and
`Engine_Step` then confirm six of them outright, and `Engine_Step`'s
`mov edi, 0x2ee0` followed by `cmp [esi + 0x738], edi` pins `out_count`
at 0x738 with no inference at all.

The block is English's 0x20ec..0x2140 with two fields missing: there is
no `fmt_2104`, and no `s2_bytes` -- the COM byte list the 1997 engine's
phoneme trace writes into, which is exactly the sort of thing a 1995
build would not have.  Each omission shifts what follows, so the English
offset less the Spanish one is 0x19e8, then 0x19ec, then 0x19f0.  The
last step is confirmed independently: `Stage_Emit` loads `stage` from
0x74c and compares it against 0x7dc, which is `stage_ctx[2]` at base
0x754 and stride 0x44 -- so the base, the stride and the new offset all
corroborate each other.

## The harness now drives the Spanish engine

`harness/tvh.c` used to carry the English addresses as `#define`s.  They
are now a `tv_abi` table with one row per engine, chosen by `-e en|es` or
by looking in the image for a voice name only one of them has.  Every
English value in `abi_en` is the constant its `#define` held, so the
English path is unchanged by construction -- and the corpus still reports
335/335 identical, standalone and 64-bit alike.

Fourteen of the fifteen addresses are filled in for Spanish.  The one
left is the live SAPI object count, which the English harness writes
because the first SAPI object construction does; the Spanish engine
renders without it, so it is skipped when zero.

The lexicon was worth chasing because it is testable.  `Lexicon_Remove`
gave away the table -- it frees two pointers out of `0x1002e000` at eight
bytes an entry and decrements a count at `0x10037c40` -- and the add
routine is then the function that bounds-checks that same count against
5000 and increments it.  Passing `-L HOLA=hh'olA` changes the audio, so
the address, the calling convention and the critical section are all
right, which is more than reading could have established.

One thing fell out for free: the SAPI central object is **the same
layout in both engines**.  `Stage_Emit` reads its owner at 0x64, 0xb60,
0xb90, 0xb94, 0xb9c and 0xba8 -- every one of them an offset the English
harness already fakes.  Whatever else changed between 1995 and 1997, the
interface to SAPI did not.

## Testing against the original

`spanish_test.wav` and `spanish_test.txt` in the repository root are a
recording of the real Spanish engine, made through Balabolka, with the text
that produced it -- the Spanish counterpart of `ref/truvoice.*`.  They are
kept at the root rather than in `ref/` on purpose: `tools/difftest.py`
runs everything in `ref/` through the *English* engine, so filing them
there now would only produce a failure.  `ref/` will need a per-file
language, the way `tests/corpus/NAME.opts` pins options per input, before
they can move.

The first comparison has been made, and it is the one that matters:
the recording's first utterance, `Hola.`, is **identical sample for
sample** to what the harness produces -- all 9680 frames of it, lead
silence, speech and trailing silence.  Balabolka drove the engine
through SAPI; the harness drives it directly; the audio is the same
bytes.  That is the oracle validated, and it is the precondition for
testing any Spanish C that gets written.

The whole recording is now reproduced, sample for sample, by
`tools/es_reftest.py`.  Getting there took correcting an assumption, and
the correction is the interesting part.

The recording is **two** `TextData` items, not one, and the break does not
fall where it looks like it should:

    item 1   "Hola."
    item 2   "
Este es un ejemplo de sintesis de voz en espanol con TruVoice."

The cut is at the end of the first line's *text*, so the line terminator
leads the second item instead of ending the first.  That sounds like a
triviality and is not: the leading CRLF is worth 4730 frames -- 0.43 s --
of silence, and it shifts the pitch contour of everything after it.  Item 2
is 59510 frames without it and 64240 with it, and 9680 + 64240 is exactly
the 73920 frames of the recording.

That also explains the mismatch as it looked before the cause was known.
The durations were right to the millisecond and the trailing silence was
identical; what differed was confined to the voiced stretches, alternating
between windows that agreed perfectly and windows that did not.  That is a
pitch contour difference, and the leading newline is what moved the
contour.  A pitch sweep had already ruled out the level -- the voice
default of 85 beat all its neighbours -- which was the right answer to the
wrong question.

The second fact is about the engine rather than the recording: **it carries
state between items**.  Feeding both items to one engine gives 73920 frames
and agrees with the recording for the first 1.427 s, then drifts; rendering
each item in an engine in its starting state and concatenating gives the
recording exactly.  So Balabolka was starting each utterance clean.  Both
behaviours are reachable: `tvh -i` queues one item per line into a single
engine, and `tools/es_reftest.py` runs one `tvh` per item.

What settled it was arithmetic rather than more rendering.  Once the gap
was measured at 4730 frames, the question became which item is 4730 frames
longer than the obvious one, and a lone `
` item measuring 6380 frames
said that a leading newline is expensive enough to be the answer.  The
blind alleys are worth recording too: a trailing newline on an item is
absorbed and changes nothing, an empty item is 1100 frames, and the
PreFormat and TextIn options change nothing at all for text like this --
which a control run on the English engine confirmed before it could be
mistaken for a bad field mapping.
## Where the Spanish work lives

Under `es/`, deliberately **not** under `src/`. `tools/gen_hookmap.py` and
`tools/gen_data.py` are handed `src` and walk it recursively, so a Spanish
file carrying `@0x...` annotations inside `src/` would have its addresses
bound into the English build. When there is Spanish C to compile, the tools
will need a directory argument rather than a fixed tree; until then keeping
it outside costs nothing.

`data/es/` is empty on purpose. `tools/extract_data.py` decides what to pull
from a DLL partly from the address annotations in the source, and those are
English; pointing it at `CGRM_ES.DLL` today would produce that binary's data
sections plus meaningless fragments from English `.text` addresses. It
becomes useful once there are Spanish annotations to drive it.

## What is next

1. Follow the rule bytecode at `0x1005fb88`, which `Stage0_Reset` points
   `s0_ip` at.  Stage 0 is the rule interpreter, and its opcodes are the
   doorway to the front end -- the part none of which is shared.
2. Place the rest of the output block, 0x692..0x700 of int16 state that
   `Output_Reset` clears without naming, using the 0x19e4 relationship.
3. Build a Spanish corpus.  `tvh` can render now, so single-utterance
   cases can be recorded and kept the way `tests/corpus/` is for English.
   That needs `ref/` to carry a per-file language first.
4. The `.bss` question. Spanish has 103 KB of it and English none, so some
   of what English keeps per-engine is global here. Working out which
   changes how much of the English source can be reused in shape.
5. Find the live SAPI object count, the last of the fifteen harness
   addresses.  Nothing needs it -- the engine renders without it -- so
   it is the least urgent thing on this list.
6. Only then the front end: letter-to-sound rules, the dictionary, and
   stages 0 to 2, which is the bulk of the work and none of which is shared.
