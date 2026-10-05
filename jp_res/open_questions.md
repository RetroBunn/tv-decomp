# Open questions — experimental Japanese

Started 2026-10-03. A running list of what is unsettled in the Japanese
synthesis, so it stops living in conversation and session summaries.

Each item says what is actually established, what is not, and what would settle
it. Items are not ordered by difficulty — section 1 is things that may be
**wrong right now**, and those come first.

**When a question is answered, delete it from here.** This file is only what is
still open; it is not a changelog and it does not keep a "closed" list. Nothing
is lost by deleting — `build/Japanese_test/README.txt` is the long-form record
of what was found, how, and from which paper, and it keeps the history. The
implementation is in `build/Japanese_test/`; the papers are in this folder.

---

## 1. Measurement debts — things that may be wrong now

### 1.1 The duration residual is unattributed; measurement bias comes first

**Current implementation (2026-10-04).** The historical numbers below are
outputs of earlier detector/rendering versions, not validated acoustic vowel
durations. `detector_diag.py` now uses raw PCM and accounts for the engine's
12 initial control frames. The listening renderer's content-dependent trim
previously changed the sample origin; its fades also changed the measurement
signal. Ordinary listening rendering remains unchanged.

The diagnostic qualifies the selected energy component using an energetic
periodic window containing the interior seed. Regions select rather than clip:
`boundary_truncated`, `merged_candidate` (another supplied vowel seed reached),
and insufficient reference/context are explicit failures without durations.
An across-cutoff summary is `indeterminate` if any setting fails. A range over
valid settings is parameter sensitivity, not a confidence interval, and no
setting is automatically selected. Merge detection requires supplied exclusion
seeds; it is not autonomous phoneme identification.

The next open work is acoustic validation and held-out context testing. Keep
this assisted diagnostic separate from `measure.py`; comparing them establishes
disagreement, not calibrated bias. Neither the 35 ms allowance currently in
code nor the earlier 20 ms allowance has been validated by these repairs.
`python tests/japanese_diagnostic_test.py` checks complete-window support,
truncation/merge rejection, zero padding, gain, F0/sample-rate handling, known
increments, seed stability, metadata, and actual stimulus arrays/PCM counts.


The model does need explicit rate and prosodic-position controls; it has
neither. An earlier version of this entry said the remaining residual *was*
rate. That was unjustified, and the correction is not that rate is excluded —
it has not been — but that attributing the residual to rate was unsupported.

**The reference did not match the measurement.** `check_vowel_length.py`
averages our output over `/bVp/`, `/dVt/` and `/gVk/`, the three contexts whose
boundaries it can place, and compared that against targets averaged over all
five of Yazawa & Kondo's. Restricted to the same three, `/i/` rises 63.2 → 67.8
and `/a/` 74.4 → 76.3, and the residual stops being uniform:

| | /i/ | /e/ | /a/ | /o/ | /u/ |
| --- | --- | --- | --- | --- | --- |
| short | −12% | −6% | **+1%** | −2% | −7% |
| long | −8% | −4% | −3% | −2% | −5% |

A uniform error would not have identified rate anyway: a shared measurement or
rendering bias produces one too.

**The residual is strongly associated with sensitivity to the detector's growth
cap.** That makes boundary placement a major confound: the current measurements
cannot separate synthesis error from detector bias. The detector's documented
knob (`ONSET_DB`) is inert over 14–26 dB, and so is the periodicity threshold
over 0.35–0.55. What moves the boundary is the growth cap — the limit on how
far the fine grid may grow out from the periodic core, in milliseconds:

| cap | /i/ | /e/ | /a/ | /o/ | /u/ |
| --- | --- | --- | --- | --- | --- |
| 8 ms | 50.9 | 63.7 | 74.9 | 63.3 | 52.8 |
| 16 ms (current) | 59.5 | 69.5 | 77.1 | 68.0 | 61.0 |
| 32 ms | 64.6 | 72.6 | 77.1 | 69.2 | 64.2 |
| range | 13.7 | 8.9 | 2.2 | 5.9 | 11.3 |

Correlating that range against the short-vowel error (−12.3, −6.1, +1.0, −1.5,
−7.4 %) gives **r = −0.982, n = 5**.

Three limits on what that number can carry, and none of them is a reason to
ignore it: five vowel identities is a very small sample; both quantities come
out of the same detector, so they are not independent; and sensitivity to an
arbitrary 8–32 ms range is not a calibrated uncertainty interval. The +0.15
against transition length likewise does not exclude a phonetic contribution.
What it establishes is the confound, not the attribution.

**A stale index, and what fell out of it.** `_compensate` rebuilds the frame
list and returns updated `ends`, but never wrote the new positions back into
the spans — so every caller reading `LAST_SPANS[i]['start']` after compensation
got a pre-compensation index, 1–2 frames early. The comment publishing
`LAST_SPANS` calls it "ground truth"; it was not, for exactly the callers most
likely to trust it. Found by Astra auditing the region bookkeeping. Fixed.

Three things followed:

* **A retracted slope.** An earlier entry reported the detector reading back
  0.79–0.87 of the allocation after alveolar and velar consonants, growing with
  length, and before that an even earlier one blamed word-level compensation.
  Both were artefacts of the stale index: the search region ran ~20 ms into the
  next mora. With it corrected the slope is 0.95–1.09 in every context and what
  remains is an offset. Astra predicted this before the measurement — a fixed
  cap produces an offset, not a slope, so a slope meant something else was
  changing, and she named region bookkeeping as the place to look.
* **A corrected region definition.** The mora boundary is *also* wrong: it cuts
  the vowel's taper, because compensation can move the boundary into it.
  `/bipe/` tapers across frames 11–13 while mora 1 begins at 12. The region now
  ends at the following **closure**, which is where the vowel acoustically is
  over.
* **A recalibrated tail.** `VOWEL_TAIL_MS` was 20 ms, measured against the
  buggy region. Re-measured over 7–13 frames × five vowels × three contexts it
  is 35 ms, stable from 9 frames up. The per-vowel exception for `/a/` was an
  artefact of the same bug and is gone — `/a/` now sits at 33.8 against a
  33.8–36.5 spread.

After all three, short vowels land at −0% mean (−5% to +5% per vowel) and long
at −7%. **The corpus rate falls from 7.30 to 6.63 morae/s**, which is the cost
of hitting these durations and belongs to the rate question below rather than
being absorbed silently.

The tail remains calibrated against the detector under investigation, so it is
provisional compensation and not an independently established acoustic tail.

**The measurement is not window-stable.** Astra's check — extending a search
window through irrelevant material should not change the target measurement —
fails. `/bape/` has its vowel in frames 5–14 and its closure at 15.

| window ends at frame | 13 | 15 | 17 | 19 | 20 |
| --- | --- | --- | --- | --- | --- |
| detected duration | 63.5 | 84.4 | 98.1 | 103.5 | 114.2 ms |

**The mechanism is not the one first proposed here**, and Astra reproduced the
experiment to show it. The energy threshold does *not* move — `rms.max()` is
5216.11 and the threshold 521.61 at every endpoint. The onset does not move
either, staying at 50.81 ms; only the offset does. And the extension is not
through silence: frames 15→17 add 317 **nonzero** samples, because a scheduled
closure is not digital silence once the filters are ringing.

The actual mechanism is feature-window handling. `measure.py` crops the
waveform first and computes features on the crop, so extending the crop makes
additional complete 32 ms periodicity windows available and the last qualifying
one moves. Worse, the periodic core is selected on **periodicity alone**, with
no energy requirement, so its last seed can be a low-energy periodic tail — its
RMS falls from 3660 at the frame-13 endpoint to 114 at frame 20 while still
qualifying. Three different temporal supports are then treated as
commensurable: a 32 ms periodicity window keyed by its *start*, a 16 ms growth
cap, and a 4 ms RMS window.

**Assisted diagnostic status.** See the current implementation note above.
Earlier tables at 93/110/169 ms used the old diagnostic, which computed
periodicity but did not use it to qualify candidates. Those values also mixed
schedule coordinates with trimmed audio. They are not current results and must
not be used to fit a tail allowance. Run the diagnostic to obtain results for
the present source and local DLL. Short reference intervals deliberately remain
unmeasurable instead of borrowing the following vowel.

**Scope.** "Every measurement is untrustworthy" was too broad. These stand
independently of acoustic boundary accuracy: the reference originally pooled
different consonantal contexts from the synthesised measurements; `_compensate`
originally published stale span coordinates; frame allocation and parameter
quantisation can be inspected directly; the detector's output depends strongly
on the supplied endpoint; and the F4 lower bound exists in the engine source.
These do not: absolute synthesised vowel duration under the paper's convention;
whether 20 or 35 ms is the right compensation; the claimed percentage agreement
with natural speech; and how much of the residual is rendering versus
detection. The detector's values are reproducible outputs *of that algorithm* —
what fails is their interpretation as validated acoustic durations.

Both 20 ms and 35 ms are kept as identifiable experimental versions and the
allowance is not retuned again until the detector is repaired.

**Open, in this order:**

1. **Boundary placement.** The visual annotation workflow below is parked:
   the available listener is blind. Automated checks and auditory experiments
   are the supported workflow; no visual annotation is required. The following
   describes the archived proposal. `boundary_audit.py` builds the experiment and
   `boundary_analyse.py` holds the decision rule, written before any annotation
   exists so it cannot be chosen to suit the result. 18 tokens under neutral
   IDs, order shuffled independently per pass, with earliest / preferred /
   latest for both onset and offset on each of two passes.

   **Pre-registered rule.** A detector boundary shows *repeatable directional
   disagreement* when it falls outside **both** passes' plausible intervals, on
   the same side. Falling inside either interval makes it **unresolved by this
   audit**, not "correct". Distance to the nearest edge of the combined
   interval is reported in ms. Onset and offset are judged separately as well
   as through duration, since two misplaced boundaries can produce an
   apparently correct duration; the conservative duration interval is
   `[e_early − s_late, e_late − s_early]`. An error-to-uncertainty *ratio* was
   considered and rejected — it goes unstable when the annotator gives a very
   narrow interval.

   **Primary hypothesis**, also pre-registered: the detector underestimates
   short `/i/` across the three stop contexts. Agreement in direction across
   all three supports a systematic problem *in the contexts tested*; mixed
   results support a context-dependent one; neither establishes population-wide
   bias.

   **Limits, on the record.** Every result is "detector bias relative to this
   annotator and protocol". Two passes by one person establish repeatability,
   not inter-annotator agreement, and a person can repeat a systematic mistake.
   The earliest–latest interval is the annotator's stated ambiguity on that
   occasion, not a confidence interval. The annotator is already familiar with
   this discussion and cannot be fully blinded to the hypothesis — the neutral
   IDs, shuffling and withheld rationale reduce the cue, they do not remove it.
   And the 20 ms tail allowance in the renderer is calibrated against the very
   detector under investigation, so it is provisional compensation rather than
   an independently established amount of acoustic tail.

   Three separate comparisons, each answering one thing:

   | comparison | what it tells us |
   | --- | --- |
   | detector vs manual boundaries | how the detector differs from human annotation |
   | manual duration vs matched reference | a duration discrepancy from a *selected reference condition* |
   | manual boundaries vs synthesis schedule | how scheduled control timing becomes acoustic boundaries |

   Only the second bears on "is the synthesis short", and it does not settle it
   on its own: it establishes a discrepancy from a *chosen* baseline, not
   automatically an error in Japanese synthesis, because speakers vary and the
   reference condition is a design choice. The between-study annotation
   difference is unmeasured — the reference was annotated by other people, on
   natural speech, in another lab. The schedule is **intended control timing,
   not an independent measurement of the acoustic vowel**, so it locates a
   discrepancy once one is established and never decides whether there is one.
   Reference values must be matched to each token's exact context and V2, not
   to the six-token means above.

   In the single tokens the audit builds — V2 is `/e/` throughout, so these are
   not the six-token means the table above reports — `/bape/` reads 74.4 ms at
   every cap against an intended 74.4, while `/bipe/` swings 48.9 → 72.2
   against an intended 63.2.
2. **Frame quantisation.** All five long-vowel increments round to the same 8
   frames. The long *durations* are still vowel-specific — 16/17/18/17/16
   frames, from different short baselines — so what quantisation erases is the
   vowel-specific *lengthening amount*, not the long duration itself. And a
   mean error below one frame is not automatically negligible: averaging many
   tokens can expose systematic bias under the quantum.
3. **Then** rate, as a relative control with 1.0 preserving the present
   baseline, applied before frame allocation and covering consonants and
   transitions — changing `MORA_FRAMES` alone will not do it now that vowel
   durations are assigned independently.
4. **Then** prosodic position, as a separately calibrated factor rather than
   folded into `V_DUR`. It is not one constant: isolated minus embedded is
   +6.3 to +9.0 ms short and +11.1 to +16.0 long. The measured vowel is the
   word's *first*, so this is an isolation effect, not phrase-final lengthening.

Keep `V_DUR` fixed as an explicitly defined baseline while those are measured.

### 1.2 Japanese noise-source levels are not calibrated across sample rates

At 11025 Hz — the engine's **historical** rate, what TruVoice shipped — one
sample clips outright and the next is 0.1 dB from it. `/ts/ /s/ /t/` sh `/ch/`
measure 6–9 dB hotter than at 16 kHz while vowels gain 0.9 dB and `/k/ /p/`
nothing.

**The direction of that statement matters and I had it backwards.** 11025 is
the original implementation; 16000 is an added mode, and the Japanese noise
settings were fitted in the added mode. "Hot relative to our 16 kHz tuning" is
established. "The native engine is wrong" is not.

**The track-to-resonator mapping in the Japanese code was wrong**, traced by
Astra through `frame.c` and `generate.c` and verified here:

| noise track | resonator |
| --- | --- |
| 4 | F3 |
| 5 | F4 |
| 6 | **F5** — a fifth resonator with no input track of its own |
| 8 | bypasses all of them |

with the upper two derived: `effective F4 = max(requested F4, F3 + 320)` and
`F5 = max(3970, effective F4 + 400)`. So the shipped `/s/` posture does not sit
at the 4080 ceiling — F3 4000 and F4 4080 give effective F4 4320 and **F5
4720**, and track 6, which carries the sibilant, resonates *above* the input
ceiling. Every description here of "F3/F4 parked at 4080" named the wrong
resonator.

**Both of my frequency sweeps were void, for the same reason twice.** The first
held F3 at 3000 and clamped three rows together. The "corrected" one held F3 at
800 and clamped *four* — F5 pins at its 3970 floor for every effective F4 below
3570, so F4 1200/1800/2400/3000 render **bit-identical waveforms at both sample
rates**, confirmed by hash. The flat +8.6 dB was one data point repeated, and
my explanation of it as expected resonator behaviour was explaining an
artefact. The split into "a broad +8.6 dB effect plus a separate ceiling
contribution" is not established.

**Astra's measurements, with longer windows on raw PCM after the startup
prefix**, and a linear white-noise approximation using the engine's own
resonator gain and output shaping:

| track-6 resonance | predicted | measured |
| --- | --- | --- |
| 3968 Hz | +8.62 dB | +8.74 dB |
| 4480 Hz | +10.48 dB | +10.66 dB |
| 4720 Hz | +11.86 dB | +11.85 dB |

Agreement to ~0.2 dB. The filters normalise at zero frequency, which does not
preserve noise RMS across sample rates, and the output shaping behaves
differently at the same physical frequency when the rate changes. She also
verified the coefficient formulas reproduce all 2,120 original 8/11 kHz table
entries exactly — so there is no evidence of a coefficient-table bug.

**A valid sweep, finally.** `jp_voice.effective_resonators()` now reports what
the engine will actually use, applying the F4 floor, the F5 derivation and the
byte quantisation — and it reproduces the bit-identical boundary found
behaviourally (F5 pinned at 3970 up to effective F4 3568, moving from 3600).
A sweep can now assert its F5 values are distinct before measuring, which is
the check all four void experiments lacked.

Run that way, over genuinely distinct F5:

| F5 | 16 kHz | 11 kHz | difference |
| --- | --- | --- | --- |
| 3970 | 1007 | 2706 | +8.58 dB |
| 4096 | 1092 | 2902 | +8.49 dB |
| 4400 | 1283 | 4116 | +10.12 dB |
| 4480 | 1339 | 4389 | +10.31 dB |
| 4720 | 1590 | 6667 | +12.45 dB |

Astra's linear approximation predicted +8.62 at 3968, +10.48 at 4480 and
+11.86 at 4720; measured +8.58, +10.31, +12.45.

So the penalty is **frequency-dependent and monotonic in F5**, not the flat
+8.6 dB claimed twice before — and the shipped `/s/` and `/ts/` postures sit at
F5 4720, the worst point measured. `/sh/` at 4096 is 4 dB better off, which is
why it is the least hot of the sibilants.

**Done, in Astra's order: source-specific gain compensation, no resonance
moved.** Targets stated first — the fricative-to-vowel RMS ratio each segment
shows at 16 kHz, which is the rate its levels were fitted in — then the 11025
noise tracks trimmed to match. `NOISE_TRIM_UNITS` in `jp_speak`.

|  | /s/ | sh | /ts/ | /ch/ | /z/ | /j/ |
| --- | --- | --- | --- | --- | --- | --- |
| before, 11 kHz − 16 kHz | +12.4 | +8.7 | +6.1 | +0.4 | +10.8 | +3.3 |
| after | −1.2 | +0.1 | +0.3 | −0.1 | +0.1 | +0.1 |

Worst residual 1.2 dB, five of six within 0.3. 16 kHz output is **bit-identical**
with the compensation disabled, so nothing moved at the rate the levels came
from. No resonance frequency was changed.

**The offsets are measured, not derived, and the first attempt at deriving them
was wrong.** Subtracting the dB penalty from the track values assumes a unit is
about a decibel. It is not: track 6 at 70 and at 66 produce *identical* output,
because the band saturates above ~66, so on `/s/` — shipped at 70 — the first
four units of a 12-unit cut did nothing and the cut bought 5 dB instead of 12.
Each offset is now found by rendering the posture at both rates and searching
for the unit offset whose 11025 level matches its own 16000 level.

The affricates needed a second pass. Calibrated on held frication they
overcorrected by 3–6 dB in a real word, because their noise region includes a
stop closure that does not scale with rate. Re-matched on the ratio the word
actually shows: `/ts/` 35 → 30, `/ch/` 26 → 17, `/j/` 23 → 14.

**Clipping.** One file still clips at 11 kHz, `78-k-front.wav`, and it is a
diagnostic that forces `K_FRONT = 4700` — a non-default variant. The default
`/k/` path peaks at 11904. `/k/` and `/p/` bursts use their own postures, not
`SIB_POST`, and measured −1.1 and −0.1 dB across the rates, so they are
correctly outside this compensation. The forced variant pushes the burst to
4700 Hz, which raises its F5 and its rate penalty with it.

**Aspiration too, which mattered more than its size suggested.** Track 2 runs
through the cascade rather than the parallel branch, and its penalty varies
strongly with the posture's own formants — **+8.65 dB on `/i/` against +0.74 on
`/o/`** — so it is a table per posture, not one number. It carries every
devoiced vowel, and over all 486,646 pronounced naist-jdic entries devoiced
vowels are **10.1% of CV morae** — 9.0% counting bare vowels into the
denominator too — **and appear in 30.5% of entries**. `/i/` is both the
worst-affected posture and one of the two vowels that devoices.

Measured over eight devoiced vowels in real words, mean absolute residual falls
from **5.64 dB to 1.00 dB**. The in-context search returned the same offsets as
the held-posture one (3 for `/u/`, 9 for `/i/`), so unlike the affricates this
path transfers — the residual is simply looser in context, 1.0 dB against the
0.04–0.43 of held postures.

**Still open here:** stop-burst VOT aspiration uses the same track 2 and is
*not* compensated — it is 1–6 frames per voiceless stop, short enough that it
was left rather than guessed at, and it is why a naive measurement of "all
unvoiced aspiration frames" in a word still reads several dB out. And all of
this is validated on isolated words; running speech beyond the sample set has
not been checked by ear.

---

## 2. Gaps in the inventory

### 2.1 Bidakuon (medial `/g/` → [ŋ]) is absent

Not implemented, and not obviously implementable from kana alone. The velar
nasal machinery already exists — `NASAL_ORAL['N_k']` is a velar place, which is
[ŋ] — so the acoustics are not the problem. The conditioning is:

- it applies to word-*medial* `/g/`, but not across a compound boundary, and
  not in the second element of a compound;
- not in numerals, loanwords, reduplications, or the particle が in some
  analyses;
- it is strongly **generation- and region-dependent and declining** — NHK still
  trains announcers in it, younger Tokyo speakers largely do not use it.

Every one of those exceptions needs morphological information the kana input
does not carry (see §4). And because it is receding, using it may make the
voice sound dated rather than natural. **Open: is it wanted at all?** If yes it
belongs behind a flag, defaulting off, and waits on the parser.

### 2.2 Intervocalic `/f/` is unmeasured

Ruddell is explicit: *"Due to the limited scope of this paper, intervocalic
/f/ sounds were not observed."* Every token in that study is word-initial. The
four-realization rule in `jp_voice.phi_mode` is therefore applied in a position
it was never measured in. In practice `/f/` is 0.99% of the 2,004,913
vowel-bearing morae over all 486,646 pronounced naist-jdic entries, and medial
`/f/` is a fraction of that, so the exposure is small — but it is an
extrapolation and should be named as one.

### 2.3 `クヮ` and `グヮ` are two morae, and should be one

The labialised velars have no consonant in the inventory, so `クヮルテット`
comes out `ku wa ru te Q to` — six morae where naist-jdic stores five, and
"kuwarutetto" where it should be "kwarutetto". This is the only entry in all
486,757 whose stored mora count disagrees with what the mora reader counts
(`build/check/morphology_followup.py` reports it), so the exposure is one
dictionary entry plus whatever a user types.

Two separate things are missing:

- **A one-mora /kʷ/.** `ja.h`'s consonant enum has the palatalised series
  (`JA_C_MY`, `JA_C_BY`, `JA_C_PY`) and no labialised one. What is wanted is a
  velar stop with lip rounding released into the vowel, which is a locus and a
  burst spectrum, not a new manner — so it is a tractable addition, just one
  nobody has measured a target for.
- **`ヮ` in an unknown word is dropped.** Open JTalk's
  `njd_set_pronunciation_list` has no row for it, so a kana string the
  dictionary does not know loses it silently: `グヮテマラ` becomes `グ` plus
  `テマラ`, said "gutemara". Mapping `ヮ` to `ワ` there would be one table row
  and strictly better than dropping a mora, at the cost of diverging from
  upstream's list.

Worth knowing while this is open: upstream's devoicing stage does not merely
mispronounce `ヮ`, it *gives up* on the utterance — `get_mora_information`
cannot tokenise the character, prints a warning and returns, so nothing after
it is devoiced. `tools/ja_stage_parity.py` declares that case rather than
counting it; see `declared_abort`.

---

## 3. Applied on thin or absent evidence

### 3.1 `/f/` before `/a i e o/` has no measured band

Those four contexts take the vowel's own F2, on the argument that a bilabial
slit has no front cavity of its own. That is reasoning, not data. Ruddell
measures only `/fu/` and explicitly declines to conclude whether `[ɸ]` differs
before the other four, so he rules a band neither in nor out.

**Settling it needs consonant spectra from those four contexts.** A different
guessed centre frequency would not be an improvement, so the values stand.
Evidence: `README.txt`, 2026-10-05.

### 3.2 `SLEW = 200` is a round number

Two things are open. **Is 200 Hz per frame right at all?** The only two
measurements to hand bracket it rather than confirm it — Kariyasu's glide
averages fall well under, Imaizumi and Kiritani's stop transitions well over —
and neither validates the F1 ×2 and F3 ×0.7 weights in `_glide_len`. The honest
status is engineering smoothing, not a physiological limit.

**And which Kariyasu condition should the glides be tuned to?** His VCV and
sentence conditions disagree about the very relation the `/w/` retune was built
on. Evidence and the table: `README.txt`, 2026-10-05.

---

## 4. Morphology

### 4.1 The dictionary is 26 MB and it has to be found at run time

The analyser is in C (`ja_port/ja_dict.c`, `ja_njd.c`, `ja_digit.c`,
`ja_front.c`) and reproduces every field of every line of `front_oracle.tsv`
at both word widths, so the shipped library reads kanji. What is left is a
packaging question rather than a linguistic one, and it has two parts.

**Finding the file.** `data/ja/jadic.bin` is read from disk, not compiled in,
because 26 MB in the DLL would be paid for by every English user. The search
is `$TVTTS_JA_DICT`, then beside the loaded module, then beside the
executable; the NVDA add-on and the SAPI installer both put it beside the DLL.
If it is not found, Japanese falls back to the kana and romaji path exactly as
it worked before, which is a quiet degradation — the voice still speaks, it
just drops every kanji. **Open: should that be quiet?** A host has no way to
ask whether the dictionary loaded, and a user whose kanji vanishes has no way
to find out why.

**Its size.** 26 MB compresses to about 10 MB in the add-on, which is most of
the package. naist-jdic carries fields nothing here reads — the base form, the
inflection type, the cost model's full precision — and the connection matrix
is 3.6 MB of the total. Nobody has measured what a trimmed build would cost in
accuracy, so nothing has been trimmed.

**And what a port cannot settle.** Agreement with `front_oracle.tsv` is
agreement with the Python, and every one of those 1,900 texts also agrees with
the compiled 1.11 reference word for word — but that comparison deliberately
feeds both sides the same input nodes, so it says nothing about normalisation,
unknown-word grouping or Viterbi tie-breaking. Two of those were wrong in the
C and the oracle caught them (a prefix test read as an exact one, and a
prepended candidate list that reversed every cost tie), which is the argument
for the oracle rather than against it. The third has never been tested.

### 4.2 There is no corpus to measure coverage against

`to_front` reports words analysed, words with no reading, morae dropped and
whether the utterance came out empty. Nothing has been run against real text:
the robustness corpus is pseudo-sentences built from dictionary surface forms,
which are known words by construction, so its 0.104% unreadable rate measures
nothing.

JSUT would supply external text, including counter and loanword subsets. It
will not supply a screen-reader set, and that is the actual workload:
interface labels, prices, identifiers, dates, mixed Japanese and Latin.

**The same gap makes every frequency figure in `README.txt` unweighted.** All
of them were re-measured on 2026-10-05 and now carry their sample and
denominator, but the sample is always naist-jdic with one contribution per
entry — a particle and a place name nobody says count the same. That is enough
to rank the consonants and to size a gap in the inventory; it is not enough to
answer "how often does a speaker produce this", which is the form the question
takes whenever an allophone is proposed. §2.1 is exactly that case: 4.87% of
onsets are `/g/` and 4.40% are in the position bidakuon could apply in, and
neither figure says how often it is realized nasally.

### 4.3 An unfamiliar Latin word is spelled out instead of pronounced

`computer` comes out シーオーエムピーユーティーイーアール -- the Japanese
names of its eight letters -- where a Japanese system says コンピューター. The
requirement is the one the listening comparisons establish: **an unfamiliar
Latin word should get a plausible Japanese pronunciation.**

**What the precedents establish, and what they do not.** Three shipping
systems -- Microsoft OneCore Japanese, L&H TTS3000, IBM/Eloquence ViaVoice --
all pronounce English words rather than spelling them, including invented
ones. That is evidence of a **productive fallback**: something beyond a fixed
list of familiar words.

It does not establish how they divide the work between rules and lexical
data, and the earlier version of this section claimed it did. Not finding
readable words in `MSTTSLocJaJP.dat` rules out only those plaintext
representations -- a lexicon could be compressed, prefix-shared, in another
encoding or in another resource -- and module sizes and strings like `ness` or
`i-n-g` say nothing about how they are executed. Nor does pronouncing an
invented word distinguish spelling-to-kana rules from English pronunciation
followed by Japanese adaptation. **The honest statement is: all three
demonstrate pronunciation beyond a fixed list; their precise division between
rules and lexical data is unverified.** Calling NVDA's 40,000-word dictionary
an outlier also went past the observations, and is withdrawn.

What the inspection does give, and it is worth having: the shape of the
architecture. L&H exported one interface per language behind a pluggable DLL --

    g2pOpen  g2pGetTranscription  g2pSetOutputMode  g2pSetUDCT_Engine  g2pClose

-- registered under `SOFTWARE\L&H\G2P\JPN_J\V3.00`, with a user-dictionary
hook in the interface itself. Their user dictionary is a plain INI whose
`[Data]` section substitutes **text, not phonemes**: `TTS` maps to
音声合成システム and `e-mail` to `email`, so the replacement goes back through
the normal pipeline. That is a cheaper override than a phoneme-level one.
Nothing here is derived from any of those systems; see NOTICE.

**WE ALREADY HAVE AN ENGLISH PHONEME API.** `tvtts_text_to_phonemes`
(include/tvtts.h) returns the string, and it makes exactly the discrimination
this needs:

    take      &TA1Kp.                 computer  &K|MPU1t3.
    blorf     &BLg1F.                 zindle    &Zi1ND|j.
    NASA      &Na1S@.                 NVDA      &e1N&VE1&DE1&A1.
    qzxv      &KU1&ZE1&e1KS&VE1.

Invented words get word pronunciations; initialisms get letter names. The
letters-versus-word classification is also inferable from the string -- an
initialism comes back as several `&`-prefixed units -- though inferring it is
weaker than being told, and the API does not expose it separately.

The real cost is in its implementation (src/port/tvtts.c): it runs synthesis
and discards the audio, which the header says plainly. So pronunciation
quality can be prototyped immediately at the price of synthesising twice, and
extracting the trace without synthesising is separate work -- and not trivial,
because the scheduler coordinates downstream processing and node reclamation.
It should not be promised as a small change before someone tries it.

**What is there now**, per Latin token, in this order:

- **The dictionary.** 840 entries keyed by a fullwidth-Latin string of two or
  more letters: 589 all-caps, 138 capitalised, 65 lowercase, 48 mixed. That is
  where `Windows` gets ウィンドーズ and `AIDS` エイズ. Initialisms and trade
  names, not vocabulary -- it does not have `take`, `hello`, `computer`,
  `email`, `mouse`, `file`, `open` or `save`.
- **Romaji, when it consumes the whole token** (`ja_read_latin`). Measured
  over 20,280 dictionary readings round-tripped through romaji, none fails the
  consumption test, so the strictness costs nothing; what it buys is that
  `computer` can no longer be read as /o.pu.te/ with the c, m and r silently
  gone.
- **Spelling out**, from Open JTalk's 369-row pronunciation table.

**The decision is per TOKEN, and it was per utterance until it was measured.**
One known word suppressed the romaji reading of every other: `Windows
konnichiwa` found `Windows`, kept the analyser, and said the Japanese names of
`konnichiwa`'s ten letters. A reading belongs to a word, so the decision now
sits between the pronunciation stage and the digit stage, where a dictionary
reading, a derived one and Japanese text can share a sentence.

**And the order matters, which three categories show.** Trying romaji before
the lookup took 142 of the 840 Latin words the dictionary knows. Those 142
split:

    107  differ SEGMENTALLY -- different consonants or vowels.  `AU` read
         /a.u/ where the dictionary says エーユー, `AFA` /a.fa/ against
         エイエフエイ.
     27  agree segmentally but LOSE THE ACCENT TYPE.  /a.ma.zo.N/ already
         IS アマゾン; what the romaji path throws away is the dictionary's
         accent type 1, because it reads every phrase heiban.  `ASEAN`,
         `CHARA`, `CHIYODA` are the same case.
      8  identical, accent 0 either way -- nothing lost.

`DATE`, which romaji read /da.te/ against the dictionary's デイト, is in the
107 and is the same shape as reading a bare `take` as タケ.

**Romaji still has to be distinguishable from an English word.** Complete
consumption proves a spelling *can* be read as romaji, not that it was meant
as one -- `take` passes that test. Ordinary text wants an English-oriented
default; deliberate romaji wants an explicit way to keep タケ. Today the
explicit signal is that the whole utterance parses as romaji with nothing
known, which keeps the romaji path bit-identical to what has been signed off
by ear, and that is a placeholder rather than an answer.

**What the adaptation needs, and it is more than listening.** Mora counts,
permissible sound sequences, vowel insertion and accent placement can all be
stated as rules and tested; listening then judges whether the rules are any
good. Saying it was all ear judgement understated what is specifiable.

Two things to carry through the pipeline that the current API does not:

- **The original spelling, alongside the English phonemes.** Established
  Japanese loanwords reflect spelling and borrowing history as well as source
  pronunciation; Mao and Hulden (2016), "How Regular is Japanese Loanword
  Adaptation?", https://aclanthology.org/C16-1081/, found those multiple
  influences computationally.
- **Whether the token was classified as letters.** An initialism should get
  the existing Japanese letter names; adapting the English pronunciation of
  each letter would produce different ones.

**THE PROTOTYPE IS BUILT**, in `build/Japanese_test/jp_g2p.py`, with
`tools/ja_latin_table.py` printing the readings as kana so they can be read
before anything is rendered, and an `i`-series of samples from
`make_dict.py` so they can be heard. It takes the English front end's own
pronunciation through `tvtts_text_to_phonemes` and adapts it, and the
adaptation is rules rather than taste:

* the engine's alphabet, read out of `src/engine/phonetic.c` -- plus one
  symbol the bracket parser has no name for, `U` = /ju:/, found by asking the
  engine for every symbol it emits over 135 words. `docs/SINGING.md` records
  two such gaps; this is a third. Stress is `1` and `2` only -- `3`, `4` and
  `5` are phonemes, and treating every digit as stress ate them.
* every vowel PROBED rather than guessed, against words whose loanword is not
  in doubt: `AA` is /ɒ/ and so オ (bottle, rock, hot, box), `UX` is /ʌ/ and
  so ア (cup, up, love, run), `QQ` and `DT` are allophones of /t/ (button,
  water).
* vowel insertion, with the vowel the consonant takes: オ after /t d/, イ
  after /tʃ dʒ/, ウ otherwise.
* gemination, which needs a REAL short vowel before it -- `cat` is キャット and
  `test` テスト, because the /t/ of `test` follows an inserted vowel -- and
  which happens before /p t k tʃ d g dʒ/ and not before /b/ or /s/.
* the moraic nasal for a coda nasal, palatalisation for /ju:/ and for /æ/
  after a velar, and the antepenultimate accent.
* **the gaps in the target inventory, stated explicitly**: Japanese has no
  /si zi tu du/ and the kana reader has no ウィ ウェ ウォ チェ シェ ジェ フュ at
  all. `jp_g2p.self_test` walks every onset against every vowel the rules can
  reach and fails if any has no katakana, which is how ウォ was found -- after
  `water` came out with a hole in it.

**What it gets.** The invented words, which are the target, all read
plausibly: `blorf` ブローフ, `zindle` ジンドル, `frobnic` フロブニック, `kludge`
クラッジ, `thrimble` スリンブル. Initialisms stay spelled. 23 of 31 familiar
words match the established loanword exactly.

**AND THE SPELLING IS A RULE, not a reason to give up.** The first cut read
`computer` as キンピューター -- "kimputer" -- and no other system says that.
English reduces an unstressed vowel to something central, the engine writes it
IX or AX, and Japanese does not reduce at all: it has to put SOME vowel there
and the one it puts is the WRITTEN one. Stated as a rule and aligned
ordinally -- the word's runs of vowel letters counted off against its syllable
nuclei -- that is コンピューター, レモン, メソッド, バナナ, アバウト, パイロット,
ロンドン. It is a crude alignment and it does not need to be a good one: it
only has to find the vowel of the syllable the reduced one is in, and a
reduced vowel is almost always its own syllable.

Two refinements the first version of that rule needed, both found by scoring
it:

* **A final -er is アー whatever it is spelled** -- コンピューター, プリンター,
  ウォーター -- and so is one with no vowel after it, which is what makes
  `internet` インターネット and not インテネット. Only a MEDIAL one before a
  vowel takes the written vowel.
* **and its /r/ does not vanish, it starts the next mora**: /mərə/ is メラ,
  two morae, with the r opening the second. Losing it made `camera` キャメア
  and `america` アメイカ.

With those, **32 of 33** established loanwords come out exactly right, against
23 before. The remaining miss is `camera` キャメラ for カメラ, which is the
velar palatalisation of /æ/ -- right for `cat` キャット and `cap` キャップ, and
wrong for a word borrowed from Dutch before that pattern settled.

**What is left over** is two kinds. The inventory gap: `water` ウオーター and
`window` ウインド, where the kana reader cannot say ウォ or ウィ at all. And a
length choice: `phone` フォーン, `table` テイブル, `hello` ヘロー. Every one is a
word a dictionary should supply anyway.

**And it forced the order to change.** Romaji-before-rules read `mouse` as
モーセ, `fire` フィレ, `orange` オランゲ and `button` ブットン -- each consumes
whole as romaji, so romaji answered before the rules were asked. The order is
now dictionary, rules, spelling, with romaji as the explicit case: complete
consumption proves a spelling CAN be read as romaji and never that it was
meant as one.

**What is still open.**

1. **Where deliberate romaji goes.** With an English default, `sakura` reads
   シクアア and `sushi` スーシー. The whole-utterance escape keeps the romaji path
   for someone typing romaji on purpose, and it is a placeholder: it cannot
   express `これはsakuraです` meaning the flower.
2. **The rules are not in the C.** `ja_read_latin` does the dictionary and
   romaji; the adaptation is Python only, so the library still spells
   `computer` out. Wiring it in means either porting the rules or calling the
   English front end from the Japanese path, and the second needs the trace
   without the synthesis.
3. **The synthesis cost.** `tvtts_text_to_phonemes` gets the trace by
   synthesising and discarding the audio. Fine for a prototype. Removing it is
   not a small change -- the scheduler coordinates downstream processing and
   node reclamation -- and it should be measured before it is promised.
4. **The accent rule is MEASURED now, and it is the weakest part.**
   `tools/ja_accent_check.py` checks it against naist-jdic, which is an accent
   dictionary for the 19,907 katakana loanwords of three or more morae it
   holds. The rule agrees with **55.1%** of them.

   The placement is good: among the 15,007 ACCENTED ones it puts the fall in
   the right place **73.0%** of the time, against 31.5% for one mora from the
   end and 50.9% for three. The antepenultimate is the right choice.

   The shortfall is one thing: **24.6% of loanwords are heiban** and the rule
   never says heiban. That concentrates at FOUR morae, where heiban is 43.2%
   and the antepenultimate only 34.6%. Everywhere else the rule wins clearly
   -- 61.5% at three morae, 65.3% at five, 70.7% at six, 74.4% at seven.

   **Calling every four-mora word heiban** would take the total to 57.8%, and
   the measurement does not make the case for it: on an individual word it is
   close to a coin flip. naist-jdic gives ストップ accent 2 and ボックス 1,
   which the rule gets right and heiban would get wrong, against グーグル and
   アメリカ at 0, which heiban would get right. Trading one kind of error for
   another for +2.7 points is not an improvement anyone can hear, so the rule
   is left as it is.

   **Open: is there a rule that does better?** Nothing simple, on this
   evidence. What would is the thing the measurement cannot supply -- whether
   a particular word is one Japanese accents -- and that is lexical, which
   means a list and not a rule. The 840 Latin entries naist-jdic already has
   carry their accent, so the dictionary path gets it right; only a word
   nothing knows is guessed at, and for an invented word there is no truth to
   be wrong about.

## 5. Shipping

### 5.1 Overflow at off-default speaking rates

At 150 wpm, the rate everything was calibrated at, every voice is clean at
both supported sample rates — no clipped sample, no adjacent jump over 32000,
across 35 texts. Away from 150 a residue remains. Over 16 rates from 46 to
253 wpm, both rates, ten voices, 35 texts a cell:

| voice | events | where |
| --- | --- | --- |
| Tsuyoshi, Daichi, Takeshi, Keiko | 0 | clean at every rate |
| Hanako | 2 | one cell, 78 wpm |
| Taro, Ojiisan, Osamu | 1–13 per cell | scattered |
| Akira | 8–20 | grows above 174 wpm |
| Kenta | 22–35 | several rates, the worst by a wide margin |

280 events in 12,749 seconds, so one every 46 seconds overall — but unevenly,
and a Kenta cell at 35 events in roughly 40 seconds is frequent enough to
hear. Each is one or two samples, a click rather than a buzz.

The mechanism in outline: `ja_voice.c` matches each voice's noise sources
against Peter's at the default rate, and the rate changes how much frication
sits inside a mora. Whether the fix is a rate term in that table or a
transient allowance like the two in `ja_frame.c` is not settled. Kenta and
Akira are most of the total and are where to start.

### 5.2 Eight kilohertz has no noise calibration

`ja_set_rate_hz` rejects 8000. The sibilant postures put energy at F5 —
4720 Hz for /s/ — and track 8's raw band runs 4–8 kHz, all at or above an
8 kHz Nyquist. Before the refusal, even Taro tore 377 times and clipped 65
samples over 35 texts, and it is the noise tracks that cause it: attenuating
the voiced source changed nothing.

The 11025 offsets were fitted and the 8000 ones never were — the Python
applies the 11025 table at 8000, which is the wrong table. Nothing breaks,
because `tvtts_set_language` falls back to 11025, but a caller that wants
8 kHz Japanese cannot have it.

### 5.3 Consonant timing under the rate control is authored

The vowel targets and the mora budget scale with the rate from figures the
papers give. A consonant's closure and VOT have no rate model in anything this
project has read; they scale proportionally with a floor of one frame, which
is monotone and not wrong in an obvious direction, and is otherwise
unsupported. Nobody has listened to the fast end.
