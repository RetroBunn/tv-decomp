# Decompilation workflow

The reference binary is `TruVoice/CGRM_EN.DLL` (Centigram TruVoice American
English SAPI 4 engine, PE timestamp 1997-10-16, MSVC 4.2).  The original
binaries are not part of this repository; place your own copy in
`TruVoice/`.  Nothing here ever runs the EXEs in that folder.

## Tools (Python 3 with `pefile`, `capstone`; mingw-w64 gcc/binutils)

| command | purpose |
|---|---|
| `python tools/disasm.py TruVoice/CGRM_EN.DLL work/cgrm_en` | recursive-descent analysis → `functions.txt`, `analysis.pickle` |
| `python tools/listing.py TruVoice/CGRM_EN.DLL work/cgrm_en [addr...]` | annotated listing (whole file or given functions) |
| `python tools/calltree.py TruVoice/CGRM_EN.DLL work/cgrm_en <addr> [depth]` | static call tree |
| `python tools/dataflow.py TruVoice/CGRM_ES.DLL work/cgrm_es <addr> [--from addr]` | the arithmetic as expressions rather than instructions, for the runs of imul/add/sar that have no calls to break them up |
| `python tools/blocklist.py ...` / `tvh.exe -c blocks -C hits` / `tools/covreport.py` | basic-block coverage of real runs |
| `python tools/covrun.py --decompiled --full [--lang es]` | which basic blocks of the decompiled functions the corpus never runs |
| `python tools/covgen.py ...` | grows `tests/corpus/` by keeping only generated lines that reach new blocks |
| `sh harness/build.sh` | builds `tvh.exe` (oracle), `tvh_hook.exe` (decompiled code hooked in), `tv.exe` (standalone), `tvtts.dll` (the library) and the API tests |
| `build/check/api_test.exe`, `api_test_dll.exe` | the library tests, statically linked and across the DLL boundary |
| `python tools/difftest.py [--full]` | byte-exact comparison of `tvh_hook.exe` against `tvh.exe` over the corpus |
| `python tools/difftest.py --port [--full]` | the same comparison for `tv.exe`, the standalone build |
| `python tools/difftest.py --port64 [--full]` | the same comparison for `tv64.exe`, the 64-bit build |
| `python tools/difftest.py --ref` | checks all three builds against recordings of the real installed engine in `ref/` |
| `python tools/voicedump.py [dll...]` | prints the per-voice parameter tables out of a language DLL (see docs/VOICES.md) |
| `python tools/make_addon.py [--32]` | packages the NVDA add-on (see docs/NVDA.md) |
| `python tests/nvda_binding_test.py` | drives the add-on's binding layer with NVDA's modules stubbed |

`work/`, `build/` and `ref/` are gitignored: they contain material derived
from the copyrighted binaries.

## Reference recordings

Everything above compares the decompiled code against `CGRM_EN.DLL` *as
loaded by this harness*.  That cannot catch a mistake in the harness
itself: if `tvh.c` reconstructed the SAPI engine thread wrongly -- a wrong
default, a missing init call, the wrong number of trailing NULs on the feed
-- every comparison would still pass, consistently wrong.

`ref/` closes that hole with audio captured from the engine as actually
installed, played through a normal SAPI client (Balabolka) and saved as
11025 Hz 16-bit mono PCM, which is the engine's native output and so needs
no resampling.  Each recording is `ref/NAME.wav` plus `ref/NAME.txt`, and
optionally `ref/NAME.opts` for harness options as in the corpus.

**Every line of the `.txt` is one utterance**: `--ref` renders each line as
its own `TextData` call and concatenates the PCM.  That is not a detail of
the file format but of the clients -- Balabolka splits text on sentences and
makes one call per sentence, and the engine's prosody spans a whole
utterance, so the same text spoken in one call and in two does not give the
same samples.  A client that speaks everything at once gets one line.

`ref/` is gitignored, and `--ref` prints a line and succeeds when it is
empty, so it is safe to leave in a test run on a machine that has no
recordings.

## The oracle

`harness/tvh.c` is a 32-bit program (built freestanding with `gcc -m32`)
that maps the DLL with its own PE loader (`harness/peload.c`).  Every
import goes through a sandbox thunk (`harness/sandbox.c`): the registry and
file system are stubbed to "not found" (so the engine sees factory
defaults and never touches the installed engine's settings), window/COM
calls trap.  It then replicates what the SAPI engine thread
(`sub_1002e240`) does for one `ITTSCentral::TextData` call: construct the
engine object, feed text, step until idle, collect PCM.

## Hooking decompiled code into the original

Sources in `src/` annotate every function and global with its address:

```c
/* @0x10029670 */
Node *TV_THISCALL Engine_NodeAlloc(Engine *self, Node *ref, ...);
```

`tools/gen_hookmap.py` inspects the compiled 32-bit objects: annotated
functions *defined* in C are hooked (the original entry is patched with a
`jmp` to the C version); annotated symbols only *referenced* are bound with
`--defsym` to their address in the DLL, so C code can call not-yet-decompiled
functions and use the DLL's globals directly.  Hooks can be enabled
selectively (`tvh_hook.exe -H all|none|name1,name2|-name`) to bisect a
mismatch.

Because a hooked C function calls its C callees directly, enabling one hook
pulls that function's whole subtree in with it; only leaf-ish functions
isolate cleanly, so a bisection over the hook list names the top of a
subtree, not the culprit.  What does localise a mismatch is a temporary
dump of engine state from a function that is hooked in *both* runs (the
node list at `Stage2_Run`, the parameter block at `Stage3_Emit`, the
alloc/free order in `Engine_NodeAlloc`/`Engine_NodeFree`), comparing
`-H <dump-fn>` against `-H <dump-fn>,<suspect>`.  Put the `extern`
declarations such a dump needs *above* the `/* @0xADDR */` annotation of
the function that follows, or `gen_hookmap.py` will bind the address to the
declaration instead of the function.

`Engine_NodeAlloc` does not clear `Node.d10`, and `Stage2_DurAdjust` reads
it, so the engine's output depends on which recycled node lands where: an
allocation the original does not make shifts every later node by one slot
in the free list and changes the audio even when the phoneme list is
identical.  Allocation order has to match exactly.

Struct layouts are described in `src/*.fields` (offset, type, name) and
generated by `tools/gen_struct.py`; in the hook build every field offset is
statically asserted against the original layout.

## The library

`include/tvtts.h` and `src/port/tvtts.c` are the layer the engine calls "the
central object" -- what SAPI used to be.  It owns the engine, holds the
settings block the engine reads back, and terminates the two paths the
engine uses to talk upwards: the bookmark queue and the window
notifications.  `src/port/main.c` is a client of it rather than a second
copy of the driving loop, so the corpus difference test exercises the
library.  See docs/LIBRARY.md.

## The standalone build

`build/check/tv.exe` is the engine on its own: it loads no DLL, talks to
no SAPI and reads no registry -- its only import is the C runtime.  The
driver is `src/port/main.c`, which does what the SAPI engine thread did for
one `TextData` call; `src/port/msvcrt.c` supplies the handful of MSVC 4.2
runtime functions the engine calls, and `src/port/stubs.c` the one call it
makes back into the layer above it.

The constant tables have to come from somewhere, and they come from
`data/en/engine.tvdata`, which is committed -- an ordinary build needs no
Centigram binary at all.  `tools/extract_data.py` is what produced it out
of the original, and `tools/gen_data.py` reads either that file or a DLL,
interchangeably, writing an assembly file that puts the same bytes at the
same relative offsets, with a label wherever an annotated symbol sits.
NOTICE says whose work the tables are.  Two whole sections come out at once (`.rdata` and
`.data`, which are nothing but data), because the engine indexes past the
ends of individual tables and expects to land on the next one -- `g_cls2`
and `g_phone_attr` are both read with a signed index that reaches in front
of them.  Everything else is cut to the size its C declaration gives, which
is why the jump-table indices the compiler left between functions in
`.text` carry an explicit array bound.  Addresses stored inside the data
are re-emitted as expressions the assembler resolves against the new
labels; the 259 that point into code (the C++ vtables in `.rdata`) keep
their original values, so the bytes match even where a table overlaps one.

The generated assembly lands in `build/`, which is gitignored: it is the
original's data, and it never enters the repository.

## Pitfalls found so far

* **Implicit return values.**  MSVC callers sometimes use `EAX` after a
  call to a function that "returns nothing" in the obvious reading
  (e.g. `List_Unlink` leaves its argument in `EAX` and `Engine_NodeAlloc`
  uses it).  Always check how call sites use `EAX`/`AL` after the call.
* **Harness must be linked at a fixed base** (`--disable-dynamicbase
  --disable-reloc-section`): hook code calls absolute DLL addresses.
* **Zero-initialised engine object.**  In the original the engine object
  lives on a fresh thread's stack, so untouched fields are zero.  The
  harness and the portable build must zero-initialise it too.
* **Inputs the original cannot handle.**  An embedded `ESC[` sequence
  longer than 17 characters overruns a fixed buffer in the original's
  TextIn front end and the engine fails, so there is no reference output
  for such text.  The decompiled code bounds the copy; the test corpus keeps
  escape sequences within the limit.
* **Excess precision.**  The 32-bit build does its floating point on the
  x87, where an intermediate carries 80 bits; SSE2 on any 64-bit target
  does not.  Where a float result is truncated to a small integer that
  reaches the audio, the two can disagree at a step boundary.  Both places
  the original did that -- the volume `pow`/`log10` -- are now tables in
  `src/engine/volume.c`, and the engine no longer links libm at all.
* **Text length matters.**  SAPI `TextData` enqueues the caller's text
  plus one NUL of its own (SDK callers usually include their own NUL too);
  the feed routine branches on the total length (0x28, 0x82).
