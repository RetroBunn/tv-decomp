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
| `sub_10017900` | `Tracks_Op` | `Engine_Step` passes it 1 and `trk_38` |
| `sub_100077b0` | `Synth_Step` | English's guard and body, instruction for instruction |
| `sub_10009bf0` | `Synth_Generate` | called with `sample_rate` and `&filt_coef` |
| `sub_1000e290` | `Engine_InputStage` | the last arm of the stage cascade |
| `sub_1001dc70` | `TextIn_PutString` | `xmatch` 1.00 |
| `sub_1001dc50` | `TextIn_Unget` | `xmatch` 1.00 |
| `sub_1001dbc0` | `AllocString` | `xmatch` 1.00 |
| `sub_1001da50` | `Bits_Test` | `xmatch` 1.00 |
| `sub_1001da90` | `Bits_Set` | `xmatch` 1.00 |
| `sub_1001daf0` | `Bits_Next` | `xmatch` 0.82 |
| `sub_1001a930` | `Node_PrevBoundary` | `xmatch` 1.00 |
| `sub_1001a9b0` | `Node_NextWord` | `xmatch` 1.00 |
| `sub_1000aee0` | `Synth_MulShr12` | `xmatch` 1.00 |
| `sub_10008b60` | `Engine_AppendNode` | `xmatch` 1.00 |
| `sub_100141d0` | `Stage0_CharClass` | `xmatch` 0.85 |
| `sub_10014010` | `Stage0_Finish` | `xmatch` 0.79 |
| `sub_1001aa60` | `Stage2_Scan` | `xmatch` 0.76 |
| `sub_1001c950` | `TextIn_Tokenize` | `xmatch` 0.74 |
| `sub_1001a960` | `Phone_TestMask` | `xmatch` 0.69 |
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
| `sub_1000ead0` | `Engine_RunControl` | English's body line for line; reads `stage`, `stage_ctx[2]` |
| `sub_1000e510` | `Engine_MidGet` | `Engine_InGet` on the mid ring, instruction for instruction |
| `sub_10001370` | `Lexicon_Add` | caps at 5000 entries, stores both strings at `0x1002e000` |
| `sub_100014f0` | `Lexicon_Remove` | frees the pair and decrements the count |

The twelve resets are confirmed by their contents rather than their
position, and so are the five `*_Run` functions: `Engine_Step`'s cascade
calls each of them in English's own order, each gated on `free_nodes`
(though not on the same thresholds).  One entry that rested on position
turned out to be wrong -- see the coverage section below.

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

Under `es/`, deliberately **not** under `src/`.  `tools/gen_hookmap.py` and
`tools/gen_data.py` walk a directory recursively, so a Spanish file carrying
`@0x...` annotations inside `src/` would have its addresses bound into the
English build -- and the two engines use overlapping address ranges, so that
would not even fail loudly.  `gen_hookmap.py` already takes the directory as
an argument, so pointing it at `es/` was all that was needed.

`es/engine.fields` feeds `tools/gen_struct.py` exactly as the English one
does, producing `build/gen/es_engine_struct.h` with a layout assertion per
field.  Two types it refers to, `TextIn` and `SapiCentral`, are forward
declared in `es/es_engine.h` and not laid out yet; declaring them keeps the
fields that hold them, and every offset after them, honest.

`data/es/` is empty on purpose.  `tools/extract_data.py` decides what to
pull from a DLL partly from the address annotations in the source, and there
are not yet enough Spanish ones to drive it.

## The first Spanish C, and proving it

`harness/build.sh` now produces `build/harness/tvh_hook_es.exe`: the same
trick the English decompilation is built on, aimed at `CGRM_ES.DLL`.  Every
function written in `es/` is patched over the original with a five-byte
jump, the engine runs, and the audio has to come out identical.  It is
skipped when `es/` has no C in it, so the build works either way.

The first eight are the character rings -- `Engine_InFree`, `InGet`,
`InUnget`, `InPut`, `InPutEnd`, `MidFree`, `MidGet`, `MidPut` -- chosen
because they are small and completely understood, which makes them a test
of the plumbing rather than of the reading.  All eight install, and the
recording still comes out sample for sample.  `tools/es_reftest.py` now
runs both the oracle and the hook build, so one command checks that the
harness still drives the original correctly *and* that the C is right.

That claim is only worth as much as its control, and the first control I
tried was worthless.  Changing `Engine_MidPut`'s guard from `< 1` to `< 2`
is a real off-by-one, and the test passed anyway: the mid ring is 0x1000
bytes and the preformatter drains it, so it never comes near full and the
two conditions never differ on this input.  A bug that cannot be reached is
not evidence about anything.  Flipping the low bit of the character
`Engine_MidGet` returns does reach: the hooked run produces 202950 frames
against the recording's 73920, and `es_reftest.py` exits 1.  The hooks are
installed, they are executed, and the test can tell.
## What the coverage says, and a correction

With the harness working, `tools/blocklist.py` and the harness's own `-c`
and `-C` gave the first real measurement: of 13488 basic blocks in 766
functions, **3314 blocks in 178 functions** run for `spanish_test.txt`.
The speech path is a quarter of the binary, and 140 of those 178 functions
are still unnamed.  That is the work queue, and it is a much better one
than the function list, because it is ordered by what actually matters.

It also caught an error.  `sub_10017900` was in the table as `Synth_Step`
on the strength of sitting 0xb0 past `Synth_ResetTracks` -- position, not
content.  `Engine_Step` calls it with **two** pushed arguments, 1 and
`[esi+0x6ec4]`, which is English's `Tracks_Op(self, 1, self->trk_38)`.
The real `Synth_Step` is `sub_100077b0`, and it is unmistakable once read:

    if (!synth_hold && !synth_busy && synth_19ad) {
        if (w_212e) Synth_Generate(self, sample_rate, filt_coef);
        synth_19ad = 0;
        if (trk_04 == trk_08) synth_busy = 1;
    }

Every term of that guard is a Spanish offset: `synth_hold` at 0, `synth_busy`
at 0x6ddc, `synth_19ad` at 1, `w_212e` at 0x742, `filt_coef` at 4, `trk_04`
and `trk_08` at 0x6de0 and 0x6de4.  `w_212e` had only been placed by
extrapolation from the host block; this confirms it.  The lesson is the one
the `E9` tail call taught earlier: a function named by where it sits is a
guess, and this time the guess was wrong.

Reading the rest of `Engine_Step` closed the cascade, and it is English's
exactly -- `Stage2_Run` above `Stage1_Run` above `Stage0_Run` above
`Engine_InputStage`, each gated on `free_nodes` -- with one real
difference.  The thresholds are **0x69, 0x37, 0x37, 0xf** where English
uses **0x69, 0x25, 0x25, 0xf**: the 1995 engine demands half again as many
free nodes before it will run stage 0 or stage 1.

One more thing fell out.  The constructor's `rep stosd` of 0x66b dwords is
0x19ac bytes, starting at 0x6ddc and stopping at 0x8788 -- and English's
constructor does `memset(self, 0, offsetof(Engine, synth_hold))`, which is
0x19ac bytes starting at 0.  The same block, the same size, zeroed the same
way, moved from the front of the object to the back.  That is what the
constant offsets between the two layouts have been measuring all along.
## Matching the two engines by shape

`tools/xmatch.py` scores a function in one engine against every function in
another.  It normalises away what a different build changes -- every
immediate or displacement of 0x100 or more becomes `N`, so struct offsets
and absolute addresses stop mattering while small shared constants like a
0x44 stride or a loop count of 5 still carry signal -- and then compares
4-gram token sets.  Pointed at what the coverage says runs:

    python tools/xmatch.py TruVoice/CGRM_ES.DLL work/cgrm_es \
                           CGRM_EN.DLL work/cgrm_en \
                           --only cov_es.txt --blocks work/cgrm_es/blocks.txt

It is a ranking and not a proof, so the first thing to ask is whether it
agrees with what was already known the slow way.  It does: **eleven
functions identified earlier by reading come back correct**, among them
`Engine_Flush`, `Engine_InPut`, `Engine_InPutEnd`, `Stage0_Reset`,
`TextIn_Flush`, `TextIn_GetChar`, `Preformat_PutChar` and `Engine_Step`.
None of those names was fed to it.  That is the reason to trust the ones
it produced on its own.

It also corrected a name, and the name was mine.  `sub_1000ead0` was in the
table as `Stage_Emit`, which is not an English name at all -- I coined it
from what the function appeared to do.  It is `Engine_RunControl`, and
reading the two side by side they are the same function line for line, down
to the compiler folding `st - self->stage_ctx == 2` into a comparison of
`st - self` against 0x7dc.  Inventing a name rather than finding one is how
that got past me.  It brought two struct facts with it: `StageCtx.ctl` is
at +8, and a node's type is the low three bits of its flags at +8.

One pair it could not separate, for a good reason.  `sub_1000e510` and
`sub_1000e5c0` both score 1.00 against both `Engine_InGet` and
`Engine_MidGet`, because those four functions are the same code over
different rings and the ring offset is exactly what the normalisation
throws away.  The displacements settle it: `0x4cbf` is the input ring, so
`sub_1000e5c0` is `Engine_InGet`, and `0x5dcf` is the mid ring, so
`sub_1000e510` is `Engine_MidGet`.

The negative result is worth as much as the positive one.  **Only 51 of the
178 functions that run have an English counterpart at all.**  The other 127
are the part with no shared ancestry -- the letter-to-sound rules, the
dictionary, the front end.

## The sibling engines, in place of a second Spanish build

There is only one Spanish DLL; whatever the installer wrote is all there is.
The usual way to corroborate a reading -- diff two versions of the same
binary -- is therefore not available.  German, French and Italian stand in
for it: they are the same generation, built within four weeks of Spanish
(ES and IT on the same day, 1995-11-29), and they turn out to be far closer
to it than English is.

Running `xmatch` from Spanish into each of them, and counting only matches
at 0.80 or better:

| | matches | median offset | within 0x2000 |
|---|---|---|---|
| Italian | 279 | +0xa38 | 97% |
| German | 253 | -0x378 | 94% |
| French | 263 | +0x5644 | -- |
| English | 65 | +0x511b0 | 0% |

**The four 1995 engines are laid out almost identically.**  A function sits
at very nearly the same address in all of them, and where it does not, the
displacement is one of a small number of constants rather than a spread --
the quartiles are +0x20 and +0xa38 for Italian, +0x20 and +0x5644 for
French.  That is what a shared source tree looks like when one module is
swapped for a larger one: everything before the swap keeps its address and
everything after it shifts by a fixed amount.  The 1997 English build
shares none of this, which is its own small piece of evidence that it was
relinked from the ground up rather than patched.

`xmatch --near` exploits this.  Restricting candidates to a window around
the source address makes a far lower `--min` safe between the 1995 engines,
because a coincidental match at the same address is much less likely than a
coincidental match anywhere.  The window has to be wide enough to cover the
shift, so 0x2000 serves for Italian and German and French needs 0x8000; it
is useless against English, which shares no layout at all.

## What is shared, and what is Spanish

Classifying the 165 covered functions large enough to fingerprint, by
whether they appear in English, only in the 1995 siblings, or nowhere else:

| | functions | bytes |
|---|---|---|
| shared with English | 38 | 4758 |
| 1995 core, siblings only | 53 | 19551 |
| Spanish only | 74 | 53784 |

The thresholds behind that table are crude -- `sub_1000f090` scores 0.59
against Italian's `sub_1000f0b0`, which is plainly the same function at
nearly the same address, and a 0.60 cut files it under "Spanish only".
Read the shape of it rather than the numbers: **most of the bytes that run
are Spanish's own**, the shared engine core is a fifth of the executing
code, and the part that survived from 1995 into the 1997 English engine is
smaller still.

The 1995 core is where the leverage is, because decompiling one of those
functions serves four languages at once.  The largest of them are
`Synth_Generate`, `Preformat_Run`, `Engine_RunControl`, `Engine_Feed`,
`Synth_InitFilters` and `Engine_InputStage` -- several of which are already
named, which is a good sign that the classification is sound.

The Spanish-only list is the front end, and it is blunt about the size of
the job: `sub_1001e5d0` at 4384 bytes, `Stage0_Run` at 3074, `sub_10016460`
at 2900, `sub_10018aa0` at 2540, with nothing to compare any of them
against.  Some genuinely share no 4-gram with any sibling function.
## A corpus, and the limit of differential testing

One recording is one input, so `tests/corpus_es/` now holds fifteen Spanish
inputs and `tools/difftest.py` takes `--lang es`.  Everything the English
run touches keeps its value, so its 335 configurations are unchanged by
construction; the Spanish side gets its own corpus directory, its own work
directory and `tvh_hook_es.exe`.  `--lang es --full` is 172 configurations
and they are all identical.

The corpus is aimed at what Spanish has and English does not -- the
inverted marks, the accents and the tilde, guillemets, ordinals like 1º and
1ª -- and at things the recording never reaches: a file of 6024 bytes
because the input ring is 4096 and a shorter one never fills it, and a file
built around the curly apostrophe because cp1252 0x92 is the one byte
`Engine_InPut` rewrites.

Then the corpus failed to do the job, which is the useful part.  The
off-by-one from the last round -- `Engine_MidPut`'s guard changed from
`< 1` to `< 2` -- still passes all 172 configurations.  It is not that the
corpus is too small.  The preformatter moves at most 400 characters per
flush and the input stage drains the mid ring every step, so the ring never
comes within one slot of full and the two conditions never differ.  **No
amount of text can reach that branch**, because the interface cannot put
the engine in the state where it matters.

So `harness/unit_es.c` sets the ring indices directly and calls the
function, the way `harness/unit.c` already does for English:

    tvh_hook_es.exe -H none -U all TruVoice/CGRM_ES.DLL

Seventeen ring positions -- both ends, the wrap, the ten-slot reserve
`Engine_InPut` keeps, the middle -- crossed against each other and, for the
two that take a character, against eight bytes including 0x92.  That is
6378 comparisons across the eight functions, each one checking the return
value *and* the whole engine object afterwards, so a write to the wrong
field is caught as well as a wrong answer.  All 6378 match.

And it catches the off-by-one on the first boundary it reaches:

    Engine_MidPut(rd=0, wr=0xffe): orig returned 1, ours 0

rd 0 against wr 0xffe is exactly `mid_rd - mid_wr - 1 == -0xfff`, which
wraps to a free count of 1 -- the one value where `< 1` and `< 2` disagree.
The two tests answer different questions and neither substitutes for the
other: the differential test asks whether the engine still sounds the same,
and the unit test asks whether a function is the same function.
## Escape sequences, and the second function

The `ESC [` command set turned out to be worth checking before writing
inputs for it.  `Preformat_Run` dispatches through a 56-byte index table at
`0x10008020` and a jump table at `0x10007fc0`, on `letter - 'A'` bounded at
0x37, so the supported letters can be read straight out of the binary:

    A C D F H I N P S V a c f g i l p r s t v w x

That is **the same twenty-three letters English has**, exactly.  The two
engines differ in the flag defaults -- 0x1780 and 0x40 here against 0x17c0
and 0x41 there, so 1997 added a flag bit -- but not in the vocabulary.
`tests/corpus_es` gained six files covering them: the parameter commands,
the flag commands, hold and continue, reset, malformed sequences, and
escapes in the middle of a word.  196 configurations, all identical.

`Engine_Flush` is the second function decompiled, and it brought the
`TextIn` struct with it -- 0xac bytes, of which three fields are named
because they are the ones the function moves across the boundary.  The
three things it calls that are not written yet, `Preformat_PutChar`,
`TextIn_Reset` and `TextIn_Flush`, are declared with their addresses and
nothing else; `gen_hookmap.py` emits a `--defsym` for every annotated
symbol it cannot find a definition of, so the calls land in the original
DLL.  That is what makes it possible to decompile one function at a time
instead of a whole subsystem.

And the corpus went blind again, for a better reason than last time.
Changing the flush limit from 400 characters to 401 passes all 196
configurations -- not because the branch is unreachable, but because the
limit only decides how the work is **split across calls**.  The sequence of
characters reaching the preformatter is the same either way, so the audio
is the same.  There is nothing wrong with the corpus; the property simply
is not audible.

`unit_es` can see it, and getting it to took one more idea.  A whole-object
comparison between two engines fails immediately, because the engine holds
pointers into itself -- the node pool, the stage cursor, the per-track
buffers -- and two engines at different addresses never match byte for
byte.  The fix is to rewrite every aligned word that points inside the
object as its offset before comparing.  A non-pointer that happens to land
in the address range gets normalised on both sides, so it cannot turn a
difference into a match.

With that, ten cases either side of the limit, and the answer is exact:

    Engine_Flush(401 waiting): in_rd=0x190/0x191 mid_wr=0x190/0x191

The original moved 400 and ours moved 401.  Two rounds, two deviations the
differential test could not see, and both of them caught the moment the
function was called directly.  The pattern is worth stating plainly: the
corpus tests the engine, the unit cases test the functions, and a function
is not finished until something has actually looked at it.
## The third function, and a rule of thumb

`Preformat_PutChar` is `sub_10008060`, and it is English's line for line:
tidy the character, drop the ones that cannot be spoken, queue it in the
0x100-byte `pre_ring`, and then -- unless `Preformat_Run` is already on the
stack -- drain the queue.  It named two more fields, `s2_1d54` and
`s2_1d55` at 0x384 and 0x385, from the guard it opens with.

It also went in with its holes plugged in advance, because by now the
pattern was obvious.  The corpus had no control characters at all, and two
of this function's branches exist only for them, so `23_control.txt` now
carries 0x01, 0x0b, 0x0c, 0x0e, 0x1f and 0x7f along with a bare escape.
That is 200 configurations.  And `unit_es` covers the function completely:
all 256 character values against five values of `free_nodes` either side of
the 0x260 it tests, 1280 comparisons of the whole engine object.

Which was worth doing, because the differential test is blind here too.
Moving the threshold from `free_nodes > 0x260` to `> 0x261` passes all 200
configurations and fails the unit test on the first case that reaches it:

    Preformat_PutChar(0x00, free_nodes=0x261): s2_1d54=1/0

Three functions written, three deviations the corpus could not see, each
one caught immediately by calling the function directly.  They failed for
three different reasons -- a state the interface cannot produce, a property
that is real but inaudible, and a threshold no ordinary input lands on --
which is the point.  There is no single reason differential testing misses
things, so there is no shortcut for the unit case.

The working rule: **write the function, add it to the corpus if it has an
input class the corpus lacks, and add a unit case if it has a boundary or a
constant.**  Both, or the hooks are decoration.

Two things make the unit cases possible at all.  `fresh()` builds a real
engine with the original constructor and `Engine_Init`, because anything
that reaches `Preformat_Run` walks live state and a poisoned object is not
good enough the way it is for the ring accessors.  And `normalize()`
rewrites self-pointers as offsets before comparing, without which no two
engines ever match.
## The input stage, and the node pool

`Engine_InputStage` is `sub_1000e290`, the arm of the cascade that runs when
everything else is idle.  It turns preformatted characters into work list
nodes, stopping after ten or at the first non-space following a space so the
stages below get a turn between words.  English keeps `read_control` as its
own function and the 1995 compiler inlined it; the jump table at
`0x1000e4d4` is that switch, selecting on the length byte the preformatter
writes after each command letter.

Writing it needed the `Node` struct, and `Engine_ResetNodes` gives it away
completely.  It links `0x26a` nodes with `add esi, 0x1c`, and **618 times
0x1c starting at 0x928 ends at 0x4cc0, which is exactly where `in_ring`
begins** -- so the node size, the node count and the pool's extent all fall
out of one loop.  618 is English's count too, but its node is 0x20 wide:
`b15`, `value` and `notify` sit at 0x15, 0x16 and 0x1c there against 0x10,
0x11 and 0x14 here, so 1997 added fields in the middle of the node as well
as to the engine.

The same function confirmed the work list around the pool -- two sentinel
pairs at 0x8b0 and 0x8f0 with their head and tail pointers -- and,
incidentally, `stage_ctx[0]`, since `Engine_AppendNode` writes 0x754, 0x758,
0x75c, 0x760 and 0x764 in turn, which is `first`, `cur`, `ctl`, `scan`,
`last` at the offsets the layout already claimed.

The unit case puts byte strings straight into `mid_ring`, which reaches what
no text can: the ten-character limit exactly, a control record of every
length including one the jump table sends to its default arm, and the `[`
and `]` forms under each combination of the two flag words that gate them.
Twenty-six cases.  Changing the limit from ten to eleven passes all 200
configurations of the corpus and fails two unit cases, the only two long
enough to reach it.

That last run also caught a fault in the test rather than the code.  With
the limit broken, cases that had nothing to do with it were reported as
differing too, because `Engine_ResetNodes` relinks the node pool without
clearing what the nodes hold: once two engines diverged, the stale contents
made every later case look wrong.  Zeroing the object before building it
fixes the isolation, and with that the broken build fails exactly the two
cases it should.  A test that reports more than it has found is only
slightly better than one that reports nothing.
## The allocator, and the test that was wrong in the other direction

`Engine_NodeAlloc` and `Engine_AppendNode` are underneath the whole
pipeline: every stage works the same list of 618 nodes, and nothing ever
allocates.  Both are English's, with one addition -- the 1995 allocator
checks its arguments, calling `sub_10008b50` with 0x1e when the pool is
empty and 0x1f when the reference node is null.  That function is three
bytes, `ret 4`, so the checks report nothing and change nothing.  They are
in the instruction stream, so they are in the C.

Reading the allocator corrected the layout.  `Engine_ResetNodes` writes
`node(0x90c)->next` as the first pool node and `node(0x8f0)->prev` as the
last, so 0x90c heads the free list and 0x8f0 ends it -- and
`Engine_NodeAlloc` taking its node from `[0x8ec]->next` agrees.  The
`free_head` and `free_tail` in `es/engine.fields` were the wrong way round,
written from the order the pointers are assigned rather than from what they
point at.  The four names now match English's, which has the same tail
pointer, head pointer, tail node, head node in the same order.

Then a control went the other way for the first time.  Changing the
per-stage flag clear from `~0xf8` to `~0xf0` **failed the corpus** at
194/200 and **passed every unit case**.  The reason is a flaw in the unit
cases rather than a virtue of the corpus: they allocate from a freshly
reset pool, where the per-stage bits are already clear, so the mask had
nothing to clear and a mistake in it could not show.  Real runs recycle
nodes that carry those bits.  Dirtying the node about to be handed out
fixes it, and with that the same bug fails four unit cases as well.

So the rule from the last three rounds needs its other half.  A unit case
is only as good as the state it sets up: if the input that makes a line of
code matter is never constructed, the case tests the lines around it and
reports success.  The corpus is what noticed, because real text does
construct that state -- which is the first time it has been ahead.
## List surgery, and what the unit cases are actually enforcing

`Engine_Unlink` and `Engine_InsertBefore` are twenty-two and twenty-four
bytes of pointer shuffling, and they settle a small question of style.  In
English they are free functions, `List_Unlink` and `List_InsertBefore`,
taking cdecl arguments.  Here they are members that take `this` in ecx as
thiscall requires and then ignore it completely, reading their real
arguments off the stack and returning `ret 4` and `ret 8`.  The `self`
parameter is kept in the C for exactly one reason: the callers inside the
DLL pass it, so the convention has to match.  Both leave the node in eax,
so both return it, even though the allocator only uses one of the two.

Neither checks anything, which is safe because the lists always have
sentinels at both ends -- and is also why handing `Engine_NodeAlloc` a
sentinel with `after == 1` walks off the end.  That one was found by
segfaulting the unit test, not by reading.

A control here drew the line between the two kinds of test more sharply
than anything so far.  Making `Engine_Unlink` also null the unlinked node's
own links passes all 200 corpus configurations, because the allocator
overwrites both fields before anyone reads them -- the change is genuinely
inaudible.  The unit case rejects it anyway, on five cases, because it
compares the whole object and the object is different.

That is the right answer and worth being explicit about.  **The corpus asks
whether the engine still sounds the same.  The unit cases ask whether a
function is the same function.**  A decompilation that only satisfied the
first would be a rewrite that happens to agree on the corpus; every input
outside it would be a guess.  The second is the stronger claim, and it is
the one worth making, so where the two disagree the unit case wins.
## What is next

There are now three tests with different reach: `difftest --lang es` asks
whether the engine still sounds the same, `unit_es` asks whether a function
is the same function, and `es_reftest` asks whether the harness still
reproduces a recording made through SAPI.  Work that is not covered by one
of them is work that is not finished.

1. Keep going outward along the call graph, hooking as you go, and add a
   unit case whenever a function has a boundary the corpus cannot reach.
   `Engine_InputStage`, `Engine_Flush`, `Preformat_PutChar` and
   `Engine_Feed` all sit one step from the rings and are already named.
2. Take the 1995 core before the Spanish-only code: `xmatch --near`
   against Italian and German makes those cheap to read, and each one
   decompiled serves four languages.  The two subtrees with the most bytes
   executing are `Stage3_Run` -> `sub_1001b880` -> `sub_10010ba0`,
   `sub_10016460`, `sub_1001b440`, and `Synth_Step` -> `Synth_Generate`.
3. Escape sequences are missing from the corpus.  `Preformat_Run` is the
   whole `ESC [` command set and nothing currently exercises it; the
   English corpus has several such inputs to copy the shape from.
4. Follow the rule bytecode at `0x1005fb88`, which `Stage0_Reset` points
   `s0_ip` at.  Stage 0 is the rule interpreter and the doorway to the
   front end, which is the bulk of the work and shares nothing.
5. Place the rest of the output block, 0x692..0x700 of int16 state that
   `Output_Reset` clears without naming.
6. The `.bss` question.  Spanish has 103 KB of it and English none, so some
   of what English keeps per-engine is global here.
7. Find the live SAPI object count, the last of the fifteen harness
   addresses.  Nothing needs it, so it is the least urgent thing here.
