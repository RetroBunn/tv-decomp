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
does, producing `build/obj/gen/es_engine_struct.h` with a layout assertion per
field.  Two types it refers to, `TextIn` and `SapiCentral`, are forward
declared in `es/es_engine.h` and not laid out yet; declaring them keeps the
fields that hold them, and every offset after them, honest.

`data/es/` is empty on purpose.  `tools/extract_data.py` decides what to
pull from a DLL partly from the address annotations in the source, and there
are not yet enough Spanish ones to drive it.

## The first Spanish C, and proving it

`harness/build.sh` now produces `build/check/tvh_hook_es.exe`: the same
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
## The escape parser, and a bug the corpus was never going to find

`Preformat_Run` is 1968 bytes and twenty-three commands, by a distance the
largest thing written so far.  It is a three-state machine: state 1 is
ordinary text, folded and copied to `mid_ring`; ESC moves to state 2, which
expects `[`; state 3 collects decimal parameters until a letter arrives.
Commands the pipeline needs are re-emitted into `mid_ring` in the binary
form `Engine_InputStage` reads back.

Every handler matched English's shape, and the constants confirmed it one
by one: the A/D mask limit of 7 against N/F's 16, `p` defaulting to 42 and
clamping 25..200 before doubling, `r` flooring at 50 and computing
`(wpm - 46) >> 3`, `v` at `p0 * 8 + 50`, `l` bounded at 22 against
`g_param_max`.  Two things differ from 1997, and both were already known:
`ESC[w` restores 0x1780 and 0x40 where English restores 0x17c0 and 0x41,
and `mode_I` here has a companion flag that the text path consults, so that
in index mode a byte goes through without accent folding.

That companion flag is where the interesting part is.  The corpus gained a
file for it -- index mode around accented Spanish, which is the case it
exists for -- and passed at 204/204 with the function as first written.
The unit case did not:

    Preformat_Run[modeI=1 ESC[2I]: state differs

`ESC[2I` is out of range and the command does nothing.  Except that it
does: the jump that rejects a parameter above 1 lands **after** the store
to `mode_I` and **before** the store to `mode_I_on`, so an out-of-range
`ESC[2I` still turns index mode off.  Written from English's structure,
where there is no such flag, the natural C breaks out of the case and skips
both stores.

The corpus could not have found it.  It has `ESC[2I` in it, but the file
reaches that command with index mode already off, so setting it to off
again changes nothing.  What found it was the unit case running every one
of its 78 strings twice, once with the flag set and once clear -- a state
the input cannot choose for itself.  A whole 1968-byte function came out
right except for one fall-through, and the thing that caught the
fall-through was two lines of loop in the test.
## Choosing what to write next, by measurement

The plan after the escape parser was `Stage3_Run` and the subtree beneath
it, on the grounds that it had the most bytes executing.  Measuring first
said otherwise.  `Stage3_Run` matches English at only 0.21, and its four
children -- `sub_1001b880`, `sub_10010ba0`, `sub_10016460`, `sub_1001b440`,
nearly 8 KB between them -- match **nothing**, in English or in any
sibling.  Writing them means reading 9 KB of assembly with no reference at
all.  Worth doing eventually; a poor place to go next.

Ranking the executing, unwritten functions by how well they match English
gives a much better queue: **32 functions and 4491 bytes at 0.45 or
better**, ten of them at 1.00.  Those are the ones where the English source
is a working draft rather than a hint.

It also corrected the `--near` advice from two rounds ago.  The Stage 3
region sits 0x3ba0 away in Italian, not the 0xa38 the median suggested --
`sub_1001b810` matches `sub_10017c70` at 1.00 across that gap.  The
displacement really is constant in blocks, but there are more blocks than
the quartiles showed, so a window tight enough to be useful will miss whole
regions.  Search without it first, then use it to cut false positives.

## The leaf utilities

Seven of the queue's easiest: the three-word bit sets the stages carry
(`Bits_Test`, `Bits_Set`, `Bits_Clear`, `Bits_Next`), the two node walks
that find word and phrase boundaries, the phoneme attribute test, and one
piece of fixed-point arithmetic.  Two convention notes: `Phone_TestMask` is
stdcall here and cdecl in English, and it reads the attribute table
directly where English calls `Phone_Attr`.  `Bits_Clear` has no English
counterpart at all under that name.

The bit sets are indexed from the far end -- bit 0 lives in `bits[2]` --
which is why all four compute `2 - bit / 32`.  A consequence worth writing
down: `Bits_Next` returns 0 when it finds nothing, so a caller cannot tell
that from finding bit 0, and the engine relies on bit 0 never being used.

These take no engine, so they can be swept outright: every bit index from
-33 to 127 against five bit patterns, all 256 phoneme values against eleven
masks and three signs, a hundred multiplier pairs.  **8849 comparisons, all
identical.**  Changing the attribute row shift from `>>1` to `>>2` fails
680 of them -- and 192 of the 204 corpus configurations, which is the
loudest a control has been yet.
## The TextIn edges

`TextIn` sits between `Engine_Feed` and the preformatter whenever the SAPI
TextIn option is on, which is the default.  It pulls characters out of the
input ring itself, splits them into tokens, rewrites some of them, and puts
the result back through `Preformat_PutChar`.  Four of its parts are the
ones that touch the engine, and all four match English exactly:
`TextIn_GetChar`, `TextIn_Unget`, `TextIn_PutString` and the string
allocator `AllocString`.

They needed no new structure -- `engine` at 0 and `input_done` at 8 were
already placed -- but they did need the C runtime.  The engine calls a
statically linked MSVC 4.2, and the hook build binds `tv_malloc`,
`tv_free` and `tv_new` to the DLL's own copies so that memory from its heap
is always freed by its heap.  The addresses are Spanish ones; `src/crt.h`
has the English set.

The control was the loudest so far.  Returning -3 instead of -2 for end of
input, and moving `TextIn_PutString`'s wake-up from `i > 0` to `i > 1`,
drops the corpus to 50/204 and makes the engine spin: one case produces
5.4 MB of audio where the original produces 483 KB.  These four are on the
hot path for every character of every input, which is worth knowing before
trusting a quiet result from them.

Spanish's `TextIn` is 0xac bytes against English's 0x70 -- the one place so
far where the 1995 object is the larger of the two.  What the extra 0x3c
holds is not worked out; the tokenizer proper is next.
## The tokenizer, and one constant that is read but not tested

`TextIn_Flush`, `TextIn_Tokenize`, `TextIn_InsertAfter` and
`TextIn_RemoveToken` bring the token list itself.  `TextIn_InsertAfter`
allocates 0x3c bytes and then zeroes every field in turn, which lays the
whole `Token` out in one function -- and it is **English's Token, field for
field**, the first structure so far that did not move at all between 1995
and 1997.  Only `w34` starts as something other than zero.

`TextIn_Construct` then explains a difference.  It sets `head` to
`this + 0x1c`, an **embedded Token sentinel** inside the object, and
0x1c + 0x3c is exactly 0x58 where the head pointer lives.  That is why
`TextIn_InsertAfter` and `TextIn_RemoveToken` treat a null neighbour as a
caller error and give up, where English updates `self->head`: here the list
always has that sentinel in front of it, so a null previous token cannot
happen and is not worth handling.  The allocation on the failing path is
leaked, and the C leaks it too.

The mode the tokenizer runs in comes from the SAPI object at 0xbb0.  The
1997 engine passes a literal 0 to the constructor and ignores the field
entirely; the 1995 engines read it.  `tvh -M` now sets it, so mode 4 is
reachable from the corpus, and `tests/corpus_es/25_modo4.opts` uses it.

Which is where this round leaves something honestly unfinished.
`TextIn_Tokenize`'s mode-4 branch tests **bit 0x53** where English tests
0x45.  The number is read straight out of the disassembly and is not in
doubt -- it is a literal `push 0x53` -- but it is **not covered by any
test**: bit 83 is set by `TextIn_ReadToken` and `TextIn_Split`, neither of
which is written, and nothing in the corpus produces a token carrying it.
Substituting English's 0x45 passes all 205 configurations even with mode 4
switched on.

Building a unit case for it would mean constructing a token list by hand
and comparing two heaps of malloc'd tokens at different addresses, which is
a great deal of machinery for one constant that the disassembly already
settles.  The proportionate thing is to leave it and say so, in the source
as well as here, so that it is a known gap rather than an unexamined one.
It closes by itself once the tokenizer proper is written and it becomes
possible to say which tokens get the bit.
## The engine's lifecycle

`Engine_Construct`, `Engine_Init`, `Engine_Reset`, `Engine_ResetRings`,
`Engine_ResetNodes` and `Engine_Step` -- the object's whole life.  Most of
this was read in earlier rounds and written up before a line of it was
compiled, so writing it was largely transcription: the cascade with its
0x69, 0x37, 0x37 and 0xf thresholds, the ten defaults, the twelve-call
reset chain, the pool loop.  With the rings and the node pool underneath it,
the engine's skeleton is now OpenTV's code end to end, with the five stages
and the synthesiser still running as the original inside it.

One deviation from English had to be reproduced deliberately.
`Engine_ResetNodes` hands each of the five stage contexts **eight** of the
nine preformat fields; English hands over nine.  The one it leaves alone is
`p_38`, which takes `flags_A`.  So in this engine a reset does not undo an
`ESC[A` or `ESC[D`, and whatever the escape parser last put there survives.

The corpus cannot see that.  Adding the copy English makes passes all 205
configurations, because nothing in the corpus has a flag set by `ESC[A`
still in force across a reset.  Unlike the 0x53 bit from the last round,
though, this one is cheap to test: set the nine preformat fields and all
five `p_38` slots to distinctive values, call the function, compare.  With
the English copy added the unit case fails four of six cases and names the
difference outright -- `p_38 0xbad/0x8888` -- the original leaving the
planted value where a faithful English translation would write `flags_A`.

That is the difference between the two gaps.  A constant that only a
not-yet-written function can set has to wait; a field that any caller can
set is testable now, and leaving it untested would have been laziness
rather than proportion.
## Stage windows

Five functions that every stage sits on.  Each stage owns a window into the
one work list -- `first` and `last` bound it, `cur` marks how far the stage
has got, `ctl` is the next control node to execute, `scan` the next node of
a type it cares about -- and brackets its work with `Engine_StageBegin` and
`Engine_StageEnd`, between which `self->stage` points at it.

`Engine_StageEnd` is where work moves down the pipeline: whatever a stage
finished becomes the next stage's window.  It recognises the last stage by
comparing the window pointer against `self + 0x864`, which is
`stage_ctx[4]` at base 0x754 and stride 0x44 -- the third independent
confirmation of that base and stride.  Stage 4 has nowhere to pass work to,
so it frees instead.

Three differences from English, all small.  `Engine_StagePrev` and
`Engine_StageNext` check their argument and call the stubbed error reporter
with 0x2b and 0x2a, which English does not.  And the mask test that English
writes inline at three places is a function here, `sub_10009020`, which
takes a null node rather than making its callers check -- so
`Engine_StageBegin` tests the type before testing for null, the opposite of
English's order.  The two come to the same thing, since the function
returns 0 for null, but the C is written the way the assembly runs.

The control was loud, as it should be for code every stage executes:
moving the last-stage test from `stage_ctx[4]` to `stage_ctx[3]` drops the
corpus to 137/205.
## Control nodes

`Engine_RunControl` is 1296 bytes and the second largest thing written so
far.  It is where an escape command takes effect: the node carries the
letter and its arguments through the pipeline, and each stage executes it
as its cursor passes, so a pitch change lands at the point in the audio
where it was written rather than when it was parsed.  Most commands act in
exactly one stage, and which stage is running is worked out by subtracting
the engine pointer from the window pointer -- 0x754, 0x798, 0x7dc, 0x820
and 0x864 for stages 0 to 4.

Seventeen handlers behind a jump table, and the command set is English's.
`g`, `s` and `t` have three separate handlers here that are identical to
the byte, where English groups them into one case -- the same code the
1995 compiler did not fold.  Three differences that are not cosmetic:

* The index-mark queue record is **two words** -- the notify context and
  the argument -- where English queues three and clears the node's notify
  when the argument is zero.  This one does not clear it.
* There is no lock around the queue push.  English brackets it with
  `Sapi_Lock` and `Sapi_Unlock`.
* `ESC[..a` computes its volume as `pow(10, a * -0.1) * 65535` in x87
  floating point.  1997 replaced that with a lookup table.

The volume conversion is worth dwelling on, because `src/engine/volume.c`
already explains why a table is the right answer: the result is truncated
to an integer, so it is a step function, and a libm that rounds the last
bit differently moves a step boundary and changes the audio in a way that
looks like a decompilation bug.  Computing the 256 values here gives
**exactly the English table** -- unsurprising, since it is the same
expression -- so the same table is used, and `unit_es` checks every one of
the 256 against the engine itself.  That is better evidence than the
English table has, which was checked against the expression rather than
against the code.

The SAPI object also came out of this.  It is English's as far as volume
and then diverges: `ctx` sits at 0xba8 where English has its format field,
and the window handle at 0xbdc against English's 0xbe0.  So the earlier
note that the two engines share the SAPI layout outright was too strong --
they share the first three quarters of it.  Every notification is a plain
`PostMessageA` to that handle.

The unit case runs all seventeen letters at all five stages and all 256
attenuation values: 1176 comparisons.  Two deliberate faults -- one step of
the volume table off by one, and `ESC[V` acting at stage 2 instead of 3 --
fail ten of them and pass all 205 corpus configurations.

Writing the test found one thing too.  Feeding `ESC[V` an argument of 255
segfaults both engines: the handler indexes a 2800-byte-per-voice table
with it and walks off the end of the image.  The escape parser clamps the
argument to 0..9 before a node is ever made, so nothing real reaches it --
but it is a reminder that a unit case can construct states the engine is
built never to see, and that a crash there is the test being wrong rather
than the code.
## Stage 0 helpers, and the accent marks

Stage 0 is the letter-to-sound pass: a bytecode interpreter over the rule
table with a twenty-frame call stack, matching its window against word
lists and character classes.  The interpreter is 3074 bytes and matches
nothing in any other engine; these are the four pieces around it, and all
four had English to work from.

`Stage0_Reset` settled four fields that were the wrong way round.  It makes
the same seven assignments the English one does, and two of them are a
buffer and a pointer into it.  The arithmetic names them: 0x2e4 - 0x244 is
0xa0, which is twenty `S0Frame`s, so those are `s0_stack` and `s0_sp`; and
0x310 - 0x2f4 is 0x1c, which is **one Spanish Node**, so those are
`s0_pending` and `s0_pending_ptr`.  The English pair measure 0xa0 and
**0x20** -- twenty frames and one English Node.  The same two structures,
each sized in its own engine's node, which is a pleasing way to have the
Node size confirmed a third time.

`Stage0_CharClass` is where the interesting difference is.  Sixteen class
numbers map onto six tests through an index table, and that table is byte
for byte the English one.  The tests are not: **the letter class here also
accepts `~` and `` ` `` alongside the apostrophe**.  Those are what
`FoldAccent` leaves behind -- it splits an accented character into a base
letter and a mark -- so in this engine the mark is part of the word as far
as the rules are concerned.  It is the clearest thing found so far that
exists because the language has accents.

It is also load-bearing, and both tests say so loudly.  Reducing the class
to English's letters-and-apostrophe fails **exactly two** of the 12288 new
unit comparisons -- `CharClass(4, 0x60)` and `CharClass(4, 0x7e)`, the two
characters and no others -- and drops the corpus from 205 to 92.  A sweep
that fails in exactly the two places it should is worth more than one that
merely fails.

Two smaller differences: `Stage0_MatchWord` does not raise `s0_1c1c` for
lists 1, 0xd and 0xe the way English's does, and every range test in
`Stage0_CharClass` is compiled signed, which comes to the same answer for
every byte -- a byte of 0x80 or more fails the lower bound signed and the
upper bound unsigned.
## Stage 4 and the parameter tracks

Stage 4 is the last stage and the only one the host drives directly:
`Engine_Step` calls it before and after the synthesis loop.  It walks its
window executing control nodes and counting off the phonemes the
synthesiser has already taken, and whatever it finishes goes back to the
pool, because there is nowhere below it to pass work to.

The three track writers came with it.  Each of the 22 parameter tracks is
a 256-byte ring, which is why every write is masked with 0xff.
`Track_Fill` writes a constant; the two blends walk a shape curve, easing
the track from where it is towards a target, forwards from a position or
backwards from one.  A shape is a run of weights terminated by a zero, so
the curve decides its own length and the count is only a cap.  All three
are English's, and so is `Synth_MulQ15`, the `(a * b) / 0x7fff` both blends
run every sample through.

`Stage4_Run` differs from English in one line and it took a purpose-built
test to hold it.  English's loop is

    while (st->ctl != NULL && st->last != st->ctl)

and this engine's is just the first half: it **processes the last node of
the window** and leaves on the null `Engine_StageNext` returns afterwards.
Substituting English's condition passes all 205 corpus configurations.

So the window was built by hand instead -- a few nodes appended with the
original allocator, `last` pointed at a chosen one, and the phoneme count
set -- and the difference is immediate and legible:

    Stage4_Run[4 nodes, last=3, type=4, trk34=9]: returned 2/2 trk34 5/6

One phoneme not consumed, in every case whose window reaches its last node.
The return value is the same either way, which is presumably why the audio
is: the count catches up somewhere downstream.  That makes it invisible to
a differential test and obvious to a direct one, which is the fourth time
that has happened and the first where the state that diverges is a plain
integer rather than a pointer or a flag.
## Stages 2 and 3, and a difference you can hear

Three functions, one in stage 2 and two in stage 3, which puts OpenTV code
in all five stages for the first time.

`Stage2_Scan` is how stage 2 asks about context: walk a number of words
forwards or boundaries backwards and say whether the first mask matched
before the second stopped the search.  The masks are signed and the sign is
not part of the value -- a negative mask is the same test inverted -- and
modes 1 and 2 swap the phoneme-attribute test for a look at the node's own
stress bits, which is how one function answers both "is there a vowel
before the next boundary" and "is the next word stressed".  It is English's
exactly, and it is the first thing written that rests on four functions
already decompiled rather than on the original.

`Stage3_Reset` is English's minus its first two assignments, to fields the
1995 engine does not have, and every field it touches lands on an English
`s3_*` at exactly +0x19e4.

`Stage3_Insert` puts a pause into the stream as two silence nodes, and it
carries **the first difference in a while that is plainly audible**.  Where
English gives the first node a length of 4, this engine gives it 15.
Substituting English's 4 drops the corpus to **8 of 205** -- pauses are
duration, and duration is the one thing a differential test cannot miss.
Everything else about the function matches, down to the order of the two
stores at the end.

Worth putting beside that: relaxing `Stage2_Scan`'s mode-1 test from
`mask1 > 0` to `>= 0` passes all 205 configurations and fails the unit
sweep only where `mask1` is zero, which is the single value that
distinguishes them.  Two controls in the same round, one that the corpus
catches outright and one it cannot see at all, is a fair picture of why
both tests are kept.

The sweep is 2304 cases -- both directions, six counts, eight masks each
way, three modes -- over a window of eight nodes with assorted types and
stress bits.  A five-parameter function is where a corpus stops being
enough.
## The parameter setters, and a step function checked over four billion inputs

`Engine_SetPitch`, `Engine_SetSpeed` and `Engine_SetVoice` are the English
engine's instruction for instruction: store the value, then copy it into all
five stage contexts, because a stage reads its own context and never the
engine.  `Engine_SetSpeed` computes the same `(wpm - 46) >> 3` rate index
with the same two faults, running off the end of a 26-row table above row 25
and wrapping unsigned below 46.  `TVTTS_EXT_RATE`, which clamps those for
English, is deliberately not wired up: without the duration scaling that
lives in stage 2 it would be half a fix, and Spanish's stage 2 duration rules
are not written.

`Engine_SetVolume` is the one that differs.  English scans a table of
boundaries; this computes the decibels in x87 and truncates --

    fldlg2 ; fild vol ; fmul 1/65535 ; fyl2x ; fmul -10.0 ; _ftol

which is `log10(vol * (1/65535)) * -10.0`, truncated toward zero, then capped
at 15.  That is the same step function `src/engine/volume.c` tabulated for
English, reached a different way, so `es/params.c` reuses those boundaries
rather than recomputing them.

Reusing them needed proof rather than an argument, because the two engines
reach the value by different routes and the difference between them would be
a single volume at one step edge.  The unit case bisects the **original's**
own curve -- it is non-increasing, so for each attenuation there is a largest
volume that still reaches it -- and checks ours on both sides of every one of
the 64 boundaries it finds, which is a test that knows nothing our table
could also be wrong about.  Then `-U volumefull` sweeps the whole domain:
**4,294,967,216 volumes, 0x50 to 0xffffffff, every one identical**.  It takes
a few minutes, so it is not in `all`.

## The reset chain, and the field English has that this engine does not

`Engine_Reset` calls eleven functions and `Engine_Init` calls the same set.
Seven were unwritten; all seven are now in `es/reset.c`, which finishes the
chain -- `Engine_Reset` and `Engine_Init` are complete subtrees.

They are almost all stores, which makes them cheap to read and cheap to test:
poison both objects with 0x5a, run one on each, compare whole.  That catches
more than it looks like.  A field written that should not be shows up as a
difference; a field the original writes that we miss shows up as our poison
against its value.  A reset names fields by writing them, so the comparison
is a direct check on that part of `es/engine.fields`.

`Output_Reset` earned its keep.  It writes exactly the fields the English
`Output_Reset` writes, in the same order, each at +0x19e4: the same two it
skips, the same `0xaaaa` into `o_2088`, the same two arrays, the same
sample-rate divide.  What it does not write is the point.  English ends with
`o_20e8 = 0` and there is no such store here, because **there is no `o_20e8`
in this engine** -- which is why everything from `preformat` onward sits at
+0x19e8 rather than +0x19e4.  That step had been read off the constructor's
field order; the reset function arrives at it from the other direction.  The
whole run 0x0692..0x0700 is now placed field by field instead of by analogy.

`Synth_InitFilters` carries the other find.  Both engines build two 40-entry
coefficient sets on the stack as int32 immediates and narrow the chosen one
into `filt_coef`; all twenty synthesis tables they select between are
byte-identical between the two images, and so are the six resonator
constants.  The 11 kHz coefficients are English's to the bit.  **The 8 kHz
ones are not**: this engine has -23934 and 17481 in slots 2 and 3 where the
1997 build has -24759 and 20770 -- which are its own 11 kHz values in those
two slots.  Two coefficients out of forty, in the rate the phone-quality
output uses.  English's tables were re-read out of `CGRM_EN.DLL` to be sure
this was a difference between the engines and not a slip in `src/`; they
match `src/engine/synth.c` exactly.  The unit case runs `Synth_InitFilters`
twice, once on a poisoned object (which takes the not-8000 branch) and once
with `sample_rate` set to 8000, so the differing set is actually reached.

`Synth_ResetTracks` has a third: after computing `trk_08` from `trk_0c`
exactly as English does, it stores -1 over it.  The first store is dead.  It
is written out anyway, because a reader comparing the two engines should be
able to see that the 1995 build has a store the 1997 build does not.

## Two stubbed diagnostics, and the only names the image gives up

`sub_10008b40` is one byte, `c3`.  `sub_10008b50` is three, `c2 04 00`.  Both
are diagnostics compiled down to a bare return, and both are called from real
code: the first variadically with a format string, the second with a small
integer.  They are written out rather than left bound to the DLL because the
port has to link and because a stage that calls one must not be left with a
hole.  Patching a five-byte jump over a one-byte function is safe here --
both are followed by `cc` alignment padding to the next sixteen-byte
boundary.

Two call sites still carry their format strings, and they are the only place
in the image where a name from the original source survives:

    sub_1000af20   "ERROR: Arith.c Extend():   TCon=%d "
    sub_1000f090   "ParL[P_F0]= %d"

So `sub_1000af20` is `Extend()` and it came from a file called `Arith.c`, and
`sub_1000f090` builds a parameter list indexed by symbolic names of which
`P_F0` is one.  A sweep of every call site of both stubs found no others: the
rest of the tracing was compiled out along with its strings.  1,566 printable
strings in the data sections and exactly one names a `.c` file.

## Leaves: two set tests, a scaler, and the .bss question

With the English-leverage queue down to ten functions -- seven of them C
runtime, which is bound rather than decompiled -- the productive ground is
the leaves: functions that call nothing, so they can be tested exhaustively
with no engine at all.

* `Bits_AllIn` and `Bits_AnyIn` ask what `Bits_Test` cannot: is every bit of
  one 96-bit set present in the other, and do the two share any bit.  Both
  walk the three words from the low address up, so unlike the single-bit
  operations they do not care which end bit 0 lives at.  The unit case gives
  every word position its turn at being the one that decides, with the other
  two filled all-ones and all-zero so neither function can short-circuit
  before reaching it.
* `Vowel_Index` scans "AEIOU" for a letter and returns its position or -1;
  `Is_Vowel` answers the same question as a flag.  English has no
  counterpart -- its letter-to-sound code tests vowels by open comparison,
  its vowel set not being five things in a row.  `Vowel_Index` is emitted
  **twice**, at 0x100132b0 and 0x100121f0, byte for byte the same 35 bytes
  down to the absolute address of the table: one copy per translation unit,
  which is what a static function in a shared header looks like afterwards.
  Both are written out, because the hook build patches by address and a
  decompilation that covered only one would leave the other quietly running
  the original's code while the tests passed.
* `Synth_ScaleParam` is English's `Synth_ScaleParam` case for case -- and
  **stdcall here where English is cdecl**, the same split `Phone_TestMask`
  has.  Written as cdecl first, it produced garbage from both sides at once:
  the original pops its eight bytes and the caller popped them again.  The
  unit case said so on the first run, with the exact argument.  The corpus
  would have found it too, but not as quickly and not as precisely.
* `Synth_MulShr11` and `Synth_Gate` are English's, line for line.  Neither
  was found by `xmatch`: both are small enough that normalising the
  immediates leaves too few 4-grams to score, so the ranking put them at
  zero.  Reading `src/engine/frame.c` found them in a minute.  Worth
  remembering that the matcher's silence on a short function means nothing.

## The lexicon, and the .bss question

Two functions turned out to be the same function twice: a table copied out
of `.data` into `.bss` the first time something needs it, behind a flag
nothing ever clears.  That is the `.bss` question answered for 2 KB of the
103 KB, and the reason is in the image's own strings -- "Save Lexicon
in: %s", "Cannot save lexicon file: %s".  The tables can be added to at
runtime, so the build ships a read-only master and duplicates it.

Each is a list of cumulative byte offsets into a blob of records that
follows it, entry 0 forced to zero, so record *i* runs from `index[i]` to
`index[i+1]`.  Every record's own length field equals that gap, for all 583
records across both tables, which is what pins the layouts down.

`Abbrev_Init` builds the 507-entry index for the text-to-text expansions the
tokenizer applies:

    "$"       -> "peso"
    "%"       -> "por ciento"
    "\x10:-)" -> "Sonrisa."

`Lexicon_Init` builds the 78-entry index for the pronunciation lexicon that
stage 1 consults before its letter-to-sound rules:

    "ANO"    -> "Anj"          (with N-tilde, cp1252 0xd1)
    "BABY"   -> "BEbI"
    "BIRDIE" -> "B1IrRRrDI"

which is a foreign-word exception list, as the entries suggest.  The two
loaders differ in one way worth keeping: the abbreviation one returns 1
whether or not it did the work, the lexicon one returns 1 only when the
table was already there and 0 when it has just built it.  The unit case runs
each both ways round and puts the table back afterwards.

## The rule interpreter, and what TextIn's extra 0x3c holds

`TextIn` is 0xac bytes here against English's 0x70, and the 0x3c difference
has been an open question since the tokenizer was first read.  It holds the
state of a bytecode interpreter.

`TextIn_Advance` looks a token up, gets back a list of rules, and hands each
to `sub_1001e5d0`.  That is a **recursive interpreter over a stream of int16
words**: one opcode per word, dispatched through an 88-entry jump table for
opcodes 4..0x5b, with two values handled outside it.  Operands that are
single bytes occupy the low half of the following word, which is why every
handler advances the instruction pointer by two and reads one byte.  With
its handlers it comes to about 11 KB -- the largest coherent thing left.

That gives four fields:

| | |
|---|---|
| 0x68 | `rule_ip`, the instruction pointer |
| 0x6c | `rule_trail`, where `Rule_MatchTrail` leaves the token's trailing character, sign-extended |
| 0x80 | `errors[10]` |
| 0xa8 | `err_count` |

The dispatcher itself is not written.  What is written is the part of it that
lives in separate functions, so each can be replaced and checked while the
arms that call it are still the original's.  The smallest three are
`Rule_TestBits`, which asks whether a token carries any or all of a 96-bit
flag set, and the pair `Rule_SetTrail` and `Rule_MatchTrail`.
`Rule_MatchTrail` records the character sign-extended and compares it
unsigned, so a trailing character above 0x7f is stored as a negative number
and still matches.  The larger handlers are in the next section.

`TextIn_Detach` is `TextIn_RemoveToken`'s other half: it unlinks a token and
**keeps** it, clearing its links and parking it in `TextIn.detached`, where
`RemoveToken` unlinks and frees.  It makes the same refusal -- a token with
nothing in front of it is a caller error -- and, like `Synth_ResetTracks`, it
computes a return value for the refused case that the refusal then throws
away.

`TextIn_Error` is the sink all of this reaches for: it keeps the first ten
codes in a ring, counts them, and always answers -1 so a caller can return
its result straight out.  **Nothing in the corpus reaches it** -- no input
the differential test carries makes the tokenizer give up -- so it is the
first function here whose evidence is entirely the unit case.  That is also
the argument for having decompiled it: when an input does trip it, the codes
are readable from the object instead of lost inside the DLL.

## What the rule opcodes do

Six more of the interpreter's handlers are written, which is enough to see
what the rules are for.  They rewrite a token's text into something sayable.

`Rule_Scan` is the context test.  It walks away from a token, forwards or
backwards, looking for one that carries the wanted flags, and leaves the
signed number of steps in `rule_trail`.  Two flags steer the walk: 0x51
makes a token transparent, so the walk steps over it, and 0x54 is a wall
that abandons the search.  A count of two means "the second real token
along", not "within two", because each pass steps at least once and then
skips.  Backwards, the head node ends it -- the same boundary
`TextIn_Detach` and `TextIn_RemoveToken` refuse to cross.

`Rule_InsertWord` links a new token in beside a reference and gives it one
of twenty-six fixed words.  `Rule_SpellOut` names a token's characters one
at a time from a 256-entry table, separated by spaces, either every
character above 0x1f or only the letters and digits.  `Rule_SayNumber`
turns a token's number into words, steered by two more flags, and gives a
number that ran to the end of its text a trailing space so the next token
has something to sit against.

Three things worth recording about the data those three read.

**The word table is not fully translated.**  Among the Spanish there is
"tiret", which is French; "minutes", which is French or English but not
Spanish; and "un mitad", which is not how a half is said.  The character
names have their own: "signo de pocentaje" is missing its r, "Dollar" never
made it out of English, and the exclamation mark is named with a French
apostrophe.  All of it is audible and all of it stays -- what the engine
says is what these tables say, and correcting them would change the output.

**`Rule_SpellOut` cannot overflow**, and it is worth having checked rather
than assumed: the buffer is 132 bytes, the guard stops appending at 100
characters, and the longest of the 256 names is 22, so the worst append
starts at 99 and ends at 123.  The one name-table entry that is empty is at
index 0x1f, which the function's own "0x1f and below" test puts out of
reach.

**The number formatter needs its digits writable.**  Above three digits it
writes a NUL three characters from the end, recurses on the leading part,
names the group it cut off, and then puts the three digits back.  So the
string a caller gets back is the one it passed in -- but handing it a string
literal still faults.  The unit case found that by crashing, and the fix was
in the test rather than the code.

The restoration is worth stating carefully because I first wrote it down
wrong, as "the buffer comes back truncated".  It is now asserted over every
input the unit case tries, which matters: two of the handlers hand the
formatter a token's own text rather than a copy.

`TextIn_InsertBefore` came out of the same subtree, and it refuses too late.
By the time it decides the reference token has no predecessor it has already
written `ref->prev = t`, so the refused token is left linked in front of
`ref` with a null `prev`, owned by nothing, and never freed.
`TextIn_InsertAfter` refuses before it links, which is why the note there
only has to mention the leak.  Neither refusal is reachable from the corpus;
both need a caller that has already lost the head.

The unit suite for these does not use the whole-object comparison the rest
of the file uses, because the inserts allocate and the two sides end up
holding different addresses.  It compares contents instead: a token's body
with its three string pointers taken as strings, and the list as a walk from
the head recording, for each node, whether it is one of the block's and
which.

## The rest of the rule opcodes

Five more handlers, and with them the shape of what the rules are for is
complete enough to state: they decide, for each token, whether it is said as
a word, said as a number, spelled out letter by letter, or replaced from a
record the tokenizer attached to it.

Three of them are numbers, and the differences between them are the point:

| | source | length limit | style | if too long |
|---|---|---|---|---|
| `Rule_SayNumber` | `Token.num` | int32 | 1 or 4 by flag 0x31 | -- |
| `Rule_SayNumberText` | `Token.text` | 17 characters | 1, and sets 0x31 | gives up |
| `Rule_SayNumberOrSpell` | `Token.text` | 7 characters | 4 | spells it out |

The two that read `Token.text` hand it to the formatter directly, so that
text has to be writable.  They also differ on the trailing character:
`Rule_SayNumberText`
overwrites whatever was there with a space, the other two fill one in only
when there was none.

`Rule_Acronym` is the one with judgement in it.  `Word_IsAcronym` builds a
consonant/vowel pattern for a three- or four-letter token and applies Spanish
phonotactics: no vowel after the first consonant means initials (CBS, IBM); a
doubled letter means a word; an H at either end means initials, H being
silent; two consonants in front are a word if the second is R or L, initials
if the first is S and the second is P, T or C.  SOL and USA come out as
words.  The unit case runs it against **every one of the 17,576 three-letter
combinations of A to Z**, which is the whole domain its rules actually
decide, and 38,416 four-letter ones besides.

`Rule_Acronym` then softens that: a token that looks like initials is still
said as a word if a neighbour carrying flag 0x19 reads as a word itself, the
token in front being consulted first.  Spelled out, a trailing full stop
becomes a space first, so the letters do not end on a sentence break that was
really an abbreviation mark.

`Rule_SayRecord` says the text of a record hanging off `Token.d18`, and only
if the record's own key matches the one the rule passes -- one opcode serving
several kinds of attachment.  It also pluralises, under the engine's own
conditions: flag 0x37, something in front, and a number on the token in front
greater than one, with the number behind added in first for key 13 (so "2
metros 50" counts as more than one).  A word ending in a vowel takes "s",
anything else "es".  The vowel set it uses for that is `"aoieuAOIEU"`, which
is **not** the `"AEIOUYaeiouy"` `Word_IsAcronym` uses -- no Y.

Where the `d18` record comes from is not established, so `RuleRec` names only
the three fields this handler touches and says so.

### A buffer overflow in the shipping engine

`Rule_Acronym` lowercases through a 104-byte stack buffer with no length
check, straight into the return address.  The tokenizer splits on spaces, so
it takes an unbroken run of 104 characters to reach -- a URL, or a row of
symbols.  It is reproduced as written, because the decompilation's job is to
be the original; past that length neither has defined behaviour, and the unit
cases stay well inside it for the same reason.  It is a candidate for an
OpenTV extension later, where a fix can sit behind a flag and be tested
against the corpus with the flag off.

### Two notes on the tests

The whole-object comparison the rest of `unit_es` uses does not work here.
The inserts allocate, so the two sides hold different addresses; the records
are built one per side; and `Token.d18` points into whichever block built it.
The comparison is by content instead -- a token's body with `d18` reduced to
null-or-not, the strings compared as strings, and the list walked from the
head recording which block node each one is.

The number formatter's in-place truncation was found by the test crashing,
not by reading: the first version passed string literals.  The fix was in the
test, and the property is now something the test checks rather than a trap
for the next reader.

## The number grammar, and the dispatcher mapped

`Number_WordsEx` is written.  It is where six of the interpreter's handlers
end up, and it is the piece of Spanish grammar in the engine: three-digit
groups, recursion one level per group, and the scale word appended on the way
back up.

The scale tables are indexed by **level % 4, not % 3** -- mil, millon, mil,
billon, which is the long scale Spanish uses.  Reading that as % 3 was my
first mistake and it made the fourth entry look dead; the unit sweep caught
it on a seventeen-digit input, which is the shortest one that reaches level
three.  The same misreading had hidden a second condition: the plural scale
word is used when the part above is greater than one **or** when the level is
3, whatever is above it.

Being a pure function of six arguments, it can be swept properly, and it is:
every value from 0 to 9999 against all four styles, both genders, all four
group levels and both flag values -- 640,512 comparisons, in under a second.

The styles turn out to be four: ordinals, cardinals-without-a-standalone-one,
fractions, and plain cardinals.  Gender is a separate argument that rewrites
the last letter of a word in place, at a fixed distance from the end;
"doscientos " becomes "doscientas " by stepping back over the s first.

The tables are misspelled in places -- "quarto", "quatroscientos",
"setescientos", "ochoscientos", "novescientos", "cuadragstimo" -- and there
is no "y" between twenty and its unit, because the tens entry is "veinti "
and the unit follows with a space.  Twenty-one comes out "veinti un".  All of
it is audible and all of it stays.

### The dispatcher, read but not yet written

`sub_1001e5d0` is mapped.  Its 88 arms are 85 distinct targets -- opcodes 30,
31, 32 and 67 share the error arm -- and they fall through each other in
chains, which is what makes it compact:

| opcodes | what they do |
|---|---|
| 3 | logical NOT of one sub-expression |
| 4..8 | AND over two to six sub-expressions |
| 9..12 | OR over two to five |
| 13, 14 | constant true, constant false |
| 15..22 | set, compare and range-test `rule_trail` |
| 23..28 | move the cursor, by count or by `rule_trail`, over all tokens or only significant ones |
| 29 | compare `Token.w08` with an immediate |
| 33..38 | build a flag set from one to three immediates and test the token |
| 39..59 | build a flag set and scan for it, in either direction |
| 60..91 | the handlers: say, spell, insert, remove, detach |

Two things the map settled.  Four of the scan opcodes pass a `check` of 2
rather than 1, which makes `Rule_Scan` walk and always answer no -- the note
in `rule.c` claiming every call site passes 1 was wrong, and the unit case
now covers every check value the dispatcher is seen to use.  And opcode 28
walks the token list using the low half of `ebx` as its counter, which is the
register holding `self`; it gets away with it because nothing reads `self`
again before the return.

## The interpreter itself, and what `detached` was for

`Rule_Eval` is written.  It passed the whole corpus on its first run, which
is the reward for having done the handlers first: by the time the dispatcher
went in, every piece it calls that the corpus reaches was already byte-exact,
so the only thing under test was the dispatch.

It is checked two ways.  The corpus exercises the handler opcodes, because
those need a token with text and a lexicon behind it and the corpus has
both.  Everything else -- the combinators, the `rule_trail` comparisons, the
cursor moves, the flag tests and scans, the four token readers, and the
opcodes that are errors -- is driven by hand-written bytecode: sixty-four
short programs against four arrangements of the token flags, with the whole
block compared afterwards so a program that moves the cursor or rewrites
`rule_trail` is checked on its effect as well as its answer.

Two things came out of writing it.

**`TextIn.detached` has a second half.**  `sub_1001d850` is
`TextIn_Reattach`: it takes the token parked by `TextIn_Detach` and links it
back beside another one, clearing the slot.  Between them, opcodes 74, 75 and
76 are how a rule *moves* a token rather than rewriting it.  Reattaching
after the last token sets `TextIn.cur`, where `TextIn_InsertAfter` sets
`TextIn.tail` in the same situation -- two different fields four bytes apart,
and the asymmetry is the original's.

**The rewind in opcodes 58 to 61 is not a slip.**  Those arms subtract two
from `rule_ip` between their two passes, which looked wrong until
`sub_100211b0` turned out to read an operand from `rule_ip` itself.  The
rewind lets the second pass read the same operand again.

Eight handlers stay bound to the DLL: nothing in the corpus reaches them, so
there is nothing to check a decompilation against.  They are declared as
`Rule_Op60`, `Rule_Op64` and so on -- named for the opcode that calls them,
which is the only thing established about them.

`Synth_Step` and `Tracks_Op` went in alongside.  `Tracks_Op` is the one entry
point the rest of the engine uses to ask the parameter tracks four questions:
is there room, slide the window down, is the read cursor still behind, and
step it.  Sliding subtracts 0x800 from all forty-four per-track cursors and
the four global ones, and leaves `trk_08` alone when it holds the -1
`Synth_ResetTracks` puts there.

## ParL, and a coverage report worth running

`Prosody_Build` is written.  It runs after every synthesis step: it reads the
current byte of all twenty-two parameter tracks, applies the voice's
percentage adjustments, works out the pitch and the four formants, and fills
`filt_coef`, which is what `Synth_Generate` then runs the filter from.  It is
the function the image's one surviving trace string calls ParL, and the trace
fires -- alongside error 0x65 -- when the pitch comes out below 0x3c, which
is also where the pitch gets forced to 0x41.

At 2,427 bytes it passed the corpus on its first run, which was surprising
enough to be worth distrusting.  A negative control settled it: adding one to
the pitch takes the corpus from 205/205 to **12/205**.  The first control
tried -- moving a clamp from 0x6b to 0x6c -- changed nothing at all, which
was not a sign the hook was dead but a sign that particular clamp is never
reached.  That is the distinction the coverage report makes, so it got run:

    python tools/covrun.py --lang es --full --decompiled

It lists every decompiled function with blocks the corpus never executes, and
it found two things.

**`TextIn_Reattach` had no evidence at all** -- 0 of its 11 blocks, and no
unit case either, because it was written in the same pass as the dispatcher
that calls it and nothing in the corpus reaches that opcode.  It has a unit
case now: every reference position, both directions, the head it refuses to
go in front of, the null reference and the empty slot.

**Ten of `Prosody_Build`'s 119 blocks are unreached**, and they are exactly
the ones written from the disassembly alone.  Six of them are the arms
guarded by the `s3_1fbd` bit, which no frame the corpus produces ever sets --
so the `(x * 5 * 2) & ~6) >> 1` table indexing in the four formants and the
three amplitudes rests on the reading and nothing else.  One is the
`row != 0` arm: no phoneme in the corpus puts anything in the top nibble of
track 21, so every frame uses voice row 0.  The other three are clamps.  All
ten are named in the file.

The same report is worth reading for the rest: `Rule_Eval` executes 128 of
296 blocks from the corpus, with the remainder covered by the bytecode unit
case, and `Number_WordsEx` 65 of 103, with the remainder covered by the
sweep.  Between the three tests there is very little that nothing looks at,
and the report is how to tell which is which.

## The TextIn object's own lifecycle

Four more went in around the tokenizer: `TextIn_Construct`, `TextIn_Reset`,
`TextIn_Advance` and `Engine_CreateTextIn`.  They are small, but they close
the loop on the object -- it is now made, reset, advanced and filled entirely
by decompiled code, with only the mode-4 reset and the rule runner left bound
to the DLL.

They also name four more fields: `TextIn.ti_04`, which `TextIn_Advance` takes
a different path on when it is 1; `ti_6e` and `ti_74`, which both the
constructor and the reset clear; and `Token.w32`, cleared when a token is
made.  That leaves 0x6f..0x73 and 0x75..0x7f of the `TextIn` still
unexamined.

`Engine_CreateTextIn` settles one small thing: the tokenizer's mode comes
from the SAPI object when there is one and is zero when the engine is
standalone, which is the only place the tokenizer's behaviour depends on the
host.

## Reading the signal processing

`Synth_Generate` is written and byte-exact.  What stopped the first attempt
was not its size -- 4,436 bytes -- but that the instruction listing tells you
everything about it except what it computes.  It is a long run of
imul/add/sar with no calls to break it up, and the compiler has interleaved
the state rotation of the delay lines through the arithmetic, so reading it
line by line means holding a dozen stack slots in your head at once.

`tools/dataflow.py` is the answer to that, and it is worth having for the
front end too.  It walks the listing keeping a symbolic value for every
register and stack slot and prints the expression at each point the function
commits one.  What took a page of assembly becomes:

    1000a701  edi  = (((W0x5a*W0x36)+2*((W0x12*D0xb8)+(W0x1c*D0x90)))) >> 0xf
    1000a76b  edi  = (((W0x58*W0x3c)+2*((W0x34*W0x1c)+(W0x1a*D0x80)))) >> 0xf
    1000a7c9  ebp  = (((D0xb0*W0x56)+2*((W0x3a*W0x1a)+(D0xb4*W0x18)))) >> 0xf

`W0x5a` is the int16 at `[esp+0x5a]`, `D0x90` the int32 at `[esp+0x90]`, and
`E0x06ae` would be a field of the object.  The names say where a value came
from, not what it means, which is the point: they are what the listing
actually knows.

That makes the shape plain.  The function is a **cascade of saturating Q15
biquads**, each one

    y = (2 * (x * a + y1 * b) + y2 * c) >> 15

clamped to int16 -- at 0x7fff on the way up and 0x8000 on the way down, with
the boundary tested at -32767 rather than -32768.  Thirty-four expressions
commit in the loop body, of which about eight are the resonator cascade and
the rest are the excitation and the output stage.  The prologue copies
thirty-nine int16 fields of the output block into locals and the epilogue
puts them back, which is why the state offsets in the expressions are stack
slots rather than object fields.

### What the reading found

Most of the reading is done and is recorded here so it is not done twice.

**Arguments.**  `Synth_Generate(Engine *self, uint16_t rate, const int16_t
*coef)`, where `coef` is `filt_coef` and `rate` is compared against 0x1f40
inside the sample loop.

**State.**  The prologue copies thirty-nine int16 fields of the output block
into locals and the epilogue puts them back.  `o_20ae[22]` is the delay line,
interleaved so that `[esp+0x12]`, `[esp+0x1e]`, `[esp+0x1c]`, `[esp+0x5a]`,
`[esp+0x1a]`, `[esp+0x58]` ... are `o_20ae[0]`, `[1]`, `[2]`, `[3]`, `[4]`,
`[5]`; the pairs `(0,1)`, `(2,3)`, `(4,5)` and so on are one resonator's two
delayed samples each.  `o_2092[0..9]` at `[esp+0x44]` down to `[esp+0x32]`
hold the coefficients the filter is currently running with.

**Coefficients.**  `coef[0]` and `coef[1]` are read out first, then
`coef[2..29]` are copied to `[esp+0xcc]` upward and `coef[32..39]` to
`[esp+0x108]` upward, with `coef[30]` and `coef[31]` kept separately as the
two pitch periods.  A rearrangement block then negates seven of them and
widens the set the biquads use:

| slot | value | slot | value |
|---|---|---|---|
| `D0xc4` | `coef[37]` | `D0xa4` | `coef[18]` |
| `D0xc0` | `coef[38]` | `D0xa0` | `coef[20]` |
| `D0xbc` | `-coef[39]` | `D0x9c` | `coef[22]` |
| `D0xb8` | `coef[23]` | `D0x98` | `coef[24]` |
| `D0xb4` | `coef[12]` | `D0x94` | `coef[26]` |
| `D0xb0` | `-coef[13]` | `D0x8c` | `coef[4]` |
| `D0xac` | `coef[2]` | `D0x88` | `-coef[5]` |
| `D0xa8` | `-coef[3]` | `D0x78` | `coef[6]` |
| | | `D0x74` | `-coef[7]` |

`coef[0]` is a control word, not a coefficient: bit 7 is the mute flag
`Prosody_Build` ORs in, and bits 0..4 index `g_10049b78` and decide whether a
constant of 0x4000 is used.  `coef[1]` is scaled by `(x * 20861) >> 15`,
which is two over pi in Q15.

**Excitation.**  `o_2076` counts down one per sample; when it goes negative a
new pitch pulse starts, `o_2082` is set to -1, the ten `o_2092` coefficients
are reloaded from `coef[14]`, `-coef[15]`, `coef[36]`, `coef[10]`,
`-coef[11]`, `coef[27]`, `coef[8]`, `-coef[9]`, `coef[25]`, `coef[32]`, and
the two pitch periods in `coef[30]` and `coef[31]` swap.  So the filter's
coefficients are held constant across a pitch period and only change on a
pulse.  The pulse is differentiated against `o_208c`, shifted up three and
scaled by `coef[17]`; the noise is an LFSR in `o_208a` scaled by `coef[16]`;
the two are added.

**Two dead fragments**, both `test x, 0x8000; je skip; or x, 0x8000` -- a bit
set that is already set.  One is in the coefficient copy loop and one is in
the noise generator.

**Output.**  One sample per iteration, appended to `out_buf` at `out_count`,
which advances by two.  The loop runs **`o_rate_div100`** times -- the
sample rate over a hundred, which `Output_Reset` computes, so one call is ten
milliseconds of audio.  That is a correction: an earlier reading of this had
`coef[19]` as the sample count, because the register holding the count was
loaded from a coefficient slot and then reloaded from the object two
instructions later.  The unit harness found it in its first run, by
generating one sample instead of eighty.

### The harness

`unit_es -U synth` drives the function directly, which is the thing four
reading passes did not give.  Both engines get the same seeded output block,
the same coefficients and their own output buffer; the comparison reports the
first sample that differs and every byte range of the object that differs,
named where the field is known.  Two hundred and fifty-six seeded states
against both sample rates, plus twenty-four runs of four hundred consecutive
frames: 537 comparisons in all.

The coefficients start from the real 11 kHz filter table, because feeding the
generator junk mostly tests the guards: `o_rate_div100` decides how long it
runs, `coef[30]` and `coef[31]` are pitch periods used as counters, and
`coef[0]` is a control word whose low five bits index a table.

It has a control of its own, and getting that to fire took three tries.  A +1
on a Q15 coefficient rounds away when the excitation is small; so does a
single changed delay-line word when that resonator happens to be quiet.  What
does fire is perturbing the whole state block, and the harness refuses to
report success unless it does -- a comparison that cannot see a difference is
not evidence of anything.

A single call cannot see everything.  The pitch counters `o_2076` and
`o_2078`, the phase accumulator `o_20e0`, the noise in `o_2088` and the
`o_207a` state machine are all carried from one frame to the next, and a
real utterance is thousands of consecutive calls.  So the suite also walks
both engines through runs of four hundred frames with the coefficients
changing under them, stopping at the first frame whose state or output
diverges.  That is what found the `c30`/`c31` swap, which every single-call
comparison passes.

With that in place the transcription could go in a piece at a time, and
every piece was bisectable.  The harness found three bugs on the first pass,
and each was a class rather than a typo:

* the five parallel sections double only their first product, where every
  section in the serial chain doubles the sum of its first two;
* the aspiration tap reads `o_20ae[6]` in two states at once -- the value
  from before the resonator above moved it along, and the value after;
* the pulse-mode branch tests the high byte of `o_2080`, not a bit of
  `coef[0]` as the instruction's operand first suggested.

The harness has since found six more, and the list is worth keeping because
none of them is the kind of thing a careful reading catches:

* `o_208e` takes the last sample's shaped pulse and `o_2092[10]` is simply
  `coef[33]` -- neither is touched inside the loop, so both look like state
  the function ignores until the epilogue writes them;
* `coef[34]` and `coef[35]` are a pair that swaps on every pitch pulse, the
  way `coef[30]` and `coef[31]` do;
* **the noise generator's state lives in `o_2088` between calls.**  Starting
  it at zero passes every unit case whose seed leaves `o_2088` at zero, and
  fails the corpus from the second frame onward.  That one is the argument
  for having both tests: the harness could not see it and the corpus could
  not say where it was;
* the negations are done in a 16-bit register before being widened, so
  -32768 negates to itself rather than becoming 32768;
* the intermediates wrap, and signed overflow is undefined in C, so they are
  written unsigned -- a decompilation that only matches on the inputs it
  happens to be given is not a decompilation, which is why the harness feeds
  it pseudo-random coefficients as well as real ones.

Three more closed it, and each was found by a test rather than by reading:

* **the parallel sections do not get the same noise the serial chain got.**
  Past the half-way point of a pitch period -- `o_2076` non-zero and
  `o_2078` gone negative -- the noise is halved, in 16 bits so the sign
  propagates, and `coef[28]`'s term at the bottom of the branch takes the
  halved value too.  The engine is quieting the aspiration in the closed
  phase of the glottal cycle.  This is why every pseudo-random set failed
  and every real one passed: the real coefficient sets in the corpus reach
  that branch with state that hides it;
* `ser`, the feedback the aspiration tap contributes to the output stage,
  is **saturated** like every other stage.  It shows up in one place only
  -- when the output is muted, because then the sample is forced to zero
  and `o_207c` is the one field where the value survives;
* **`c30` and `c31` swap on every pitch pulse**, exactly as `coef[34]` and
  `coef[35]` do.  Assigning `coef[31]` to `c30` instead gets the first two
  periods right and every one after that wrong.  No single-call comparison
  can see this, which is what the chained test below exists for.

With those in, `Synth_Generate` is byte-exact: 537 of 537 unit comparisons
and 205 of 205 corpus configurations with it hooked in.

## A second build of the same engine

Lernout & Hauspie shipped TruVoice inside their TTS3000 system, and two of
those DLLs are worth knowing about.  Neither is in the repository -- both
are covered by the `*.dll` line in `.gitignore` -- but what was read out of
them is recorded here.

`SPMct160.dll` (124 KB, 1999) is the **language control** and is no use at
all: its exports are the TTS3000 front-end API (`TtsEgConvertText`,
`TtsEgGetPcmData`, `TtsEgProsodyHndl`), `xmatch` scores 0 of 766 functions
against it at any threshold, and it shares no engine data.  Where it does
arithmetic it works on 32-bit state with the coefficients compiled in as
strength-reduced constants -- `0xf`, `0x11`, `0x81`, `0x101`, `0x1ff`, all
2^n +/- 1 -- which is a different algorithm from anything here.  It does
carry the pipeline configuration in plain text, e.g.
`T="Pm(TTS);G(TTS);Pos(TTS);Morph(TTS);M(number);D(number)"`, which says
which stages TTS3000 handles and which are L&H's own.

`SPMtv160.dll` (268 KB, 1998) is **the synthesiser**, and it is the same
engine.  It exports `vfOpen`, `vfGenPcm`, `vfSetSpkr`, `vfTuneSpkr`,
`vfP2Tic`, `vfSetSegDb`, `vfSetSynthRange` and their fellows; its `.text` is
only 64 KB across 175 functions, because the whole text front end is
TTS3000's rather than its own.

**The data is shared, in bulk.**  Comparing `.data` and `.rdata` against
`CGRM_ES.DLL` finds 85,561 bytes in runs of 48 bytes or more.  One of them
settles what it is: the run at `CGRM_ES 0x10049970` is 584 bytes long, and
`0x10049970 + 584` is exactly the end of `g_pulse_gain`, so the block is
`g_pulse_shape` and `g_pulse_gain` back to back and byte-identical.  In
`CGRM_ES.DLL` nothing reads it but `Synth_Generate`; in `SPMtv160.dll`
nothing reads its copy but `sub_1000266a`.

**The code is not shared, but it corresponds.**  `xmatch` finds only two
matches at 0.30 and both are thunks, so the two were built by different
compilers and token-level matching is useless.  The structure survives
anyway.  Four shared data blocks have exactly the same number of consumers
on each side -- 2 and 2, 4 and 4, 2 and 2, 1 and 1 -- which pairs the
functions off:

| block | CGRM_ES.DLL | SPMtv160.dll |
| --- | --- | --- |
| `0x10049970` pulse tables | `Synth_Generate` `10009bf0` 4436 B | `1000266a` 3491 B |
| `0x10049d03` | `Synth_InitFilters` `1000e6f0` 884 B | `1000b580` 811 B |
| `0x10049d03` | `Prosody_Build` `1000f090` 2427 B | `1000b921` 2549 B |
| `0x10058812` | `Segment_Apply` `100117e0` 2441 B | `1000e803` 1922 B |
| `0x10058812` | `Cluster_SetTracks` `10012f30` 848 B | `1000f9f6` 701 B |
| `0x1004d028` | `BitTable_Rank` `10012220` 188 B | `1000efc8` 139 B |
| `0x1004d028` | `BitTable_Count` `100122f0` 132 B | `1000f053` 115 B |

The pairing was calibrated against functions already decompiled, which is
the only way to trust it.  `Synth_InitFilters` shares 95 of its 109
immediates with `1000b580`, the filter coefficients themselves included
(`0x7b21`, `0x78c2`, `0x7625` ...).  `Synth_Generate` shares 87 with
`1000266a`, `0x1f40` -- the 8000 Hz test -- among them.

**It is easier to read than the original.**  `SPMtv160.dll` was built with a
compiler that emits `imul` with a real immediate where MSVC 4.2 emits a
chain of `lea`s, and that uses `ebp` frames where MSVC 4.2 indexes
everything off `esp`.  Run `dataflow.py` on `1000266a` and the output stage
reads

    10003181  edx  = ((edx*0x184d)) >> 0xf
    10003192  eax  = ((eax*0x1d71)) >> 0xf

-- 6221 and 7537, the two scalings that in `CGRM_ES.DLL` have to be
recovered from `lea eax, [ebx + eax*4]` chains five instructions long.  The
same listing shows six serial biquads and five parallel sections in the same
order, and the `>> 0xe` of the aspiration tap.  None of that was used to
write `es/generate.c`, which was finished first; it is recorded because it
independently confirms that reading and because it made the rows above
cheaper to take on, and every one of them is now decompiled.

To reproduce any of this, disassemble it once with
`python tools/disasm.py SPMtv160.dll work/spmtv160`.

## What is next

There are now three tests with different reach: `difftest --lang es` asks
whether the engine still sounds the same, `unit_es` asks whether a function
is the same function, and `es_reftest` asks whether the harness still
reproduces a recording made through SAPI.  Work that is not covered by one
of them is work that is not finished.

Where it stands, counting only code the corpus actually executes and
excluding the C runtime, which is bound rather than decompiled (the MSVC 4.2
objects begin at 0x1002345a; everything below that address is the engine's
own): **130 of 195 functions and 43,985 of 87,061 bytes, 50.5%.**  Rerun the
measurement with `python tools/covrun.py --lang es --full`, which writes
`work/cov_es_merged.txt`, and rank what is left with `tools/xmatch.py`.

1. The leaves.  They call nothing at all, so they can be tested
   exhaustively with no engine, which is the cheapest ground there is.
   `Synth_Generate` was the biggest of them and is done.
2. Take the 1995 core before the Spanish-only code: `xmatch --near`
   against Italian and German makes those cheap to read, and each one
   decompiled serves four languages.  The two subtrees with the most bytes
   executing are `Stage3_Run` -> `sub_1001b880` -> `sub_10010ba0`,
   `sub_10016460` and `sub_1001b440`.
3. Follow the *other* rule bytecode, at `0x1005fb88`, which `Stage0_Reset`
   points `s0_ip` at.  There are two machines, not one: `Rule_Eval` runs on
   the `TextIn` and rewrites tokens, and this one runs in stage 0 and is
   the doorway to the front end.  Whether they share an
   encoding is not established -- `s0_ip` steps through its own stack of
   frames and nothing has been read across yet.
4. The rest of the `.bss` question.  `Abbrev_Init` and `Lexicon_Init` show
   one shape it takes -- a table shipped read-only and duplicated into
   `.bss` on first use so it can be added to -- but the two together
   account for 2 KB of the 103 KB.
5. Find the live SAPI object count, the last of the fifteen harness
   addresses.  Nothing needs it, so it is the least urgent thing here.

Two earlier items are done.  The escape-sequence gap is closed:
`tests/corpus_es` now carries six `ESC [` inputs, 17 through 22.  The output
block 0x0692..0x0700 is placed field by field, above.
