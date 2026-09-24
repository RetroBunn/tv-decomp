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

1. More anchored functions, to grow the object. `Engine_Reset`'s other
   callees are the obvious next ones, then whatever reads `g_param_max`.
2. The `.bss` question. Spanish has 103 KB of it and English none, so some
   of what English keeps per-engine is global here. Working out which
   changes how much of the English source can be reused in shape.
3. A harness that can drive `CGRM_ES.DLL`. The existing one loads a DLL with
   its own PE loader and sandboxed imports and is not English-specific, but
   it calls the engine through the SAPI COM object, and the Spanish entry
   points have not been found yet.
4. Only then the front end: letter-to-sound rules, the dictionary, and
   stages 0 to 2, which is the bulk of the work and none of which is shared.
