# British English: the sources, and what each one settles

The accent being built is **General British (GB)**, as described by Cruttenden,
*Gimson's Pronunciation of English*, 8th edition (2014). That book is the
**primary specification**: where sources disagree on which variant to
implement, it decides. It is a copyrighted book and is not in this repository.

**What the book settles is in [`gimson8.md`](gimson8.md)** — the GB/RP
question, the formant tables, BATH, SQUARE, and linking /r/. Read that first;
this page is the supporting reference and is superseded by it wherever the two
differ. Two places where they do are marked below.

Everything below is from the supporting reference, which **is** here:

> Peter Roach (2004). "British English: Received Pronunciation."
> *Journal of the International Phonetic Association* 34(2), 239–245.
> doi:10.1017/S0025100304001768 — `british_english_received_pronunciation.pdf`

Seven pages. Read in full on 2026-10-07; everything attributed to it below is
quoted or taken directly from the text, not from a summary of it.

## RP and GB are the same accent under two names

The paper's title says Received Pronunciation, which raised the question of
whether it conflicts with a GB specification. It does not. Roach names GB
himself as one of the alternatives for the accent he is describing:

> "Other names have been proposed, such as GENERAL BRITISH (GB) and EDUCATED
> SOUTHERN BRITISH ENGLISH. The present author's own preference is for the name
> BBC PRONUNCIATION or BBC ACCENT."

So it is a supporting reference for the same accent, not a competing one. What
it cannot do is settle the 8th edition's **updates**: Cruttenden revised
pronunciations and transcriptions as well as the label, so any detail that
differs is decided by the book, not by this paper.

## The vowel inventory

Seven short, five long, five closing diphthongs, three centring, two weak.

| | | |
|---|---|---|
| short | ɪ ʊ e ə ʌ æ ɒ | pit put pet about cut pat pot |
| long | iː uː ɜː ɔː ɑː | key coo cur paw half |
| closing | eɪ aɪ ɔɪ əʊ aʊ | bay buy boy go cow |
| centring | ɪə eə ʊə | peer pear poor |
| weak close | i u | happy throughout |

> **Superseded on two rows.** The 8th edition writes TRAP as **/a/**, not /æ/,
> and makes SQUARE a pure long vowel **/ɛː/**, leaving only two centring
> diphthongs. See `gimson8.md`.

**Non-rhoticity needs no separate rule: it is this table.** There is no
r-coloured vowel in the inventory at all. NURSE is /ɜː/, START is /ɑː/, and
NEAR, SQUARE and CURE are the three centring diphthongs. Where American has
r-colouring, GB has either a long monophthong or a glide to schwa.

## The warning that matters most to us

Roach states plainly that the symbols are not the sounds, and **names which
ones**:

> "It will be noticed that the position of some of the symbols is quite far
> removed from the Cardinal (or IPA) value of those symbols (see for example
> /æ/, /ɔː/, /uː/, /ʌ/, /e/); this is largely for historical reasons,
> illustrating the need for a modernized transcription."

Those five are where reading an IPA symbol as an acoustic target would be
actively wrong. This is the same shape as the Japanese vowel error: values
that were transcribed perfectly and then used as the wrong kind of thing.

A second warning applies directly to how this engine renders a diphthong, as
formant movement over time:

> "in many cases the vowel glide implied by the symbols in such vocalic
> elements is so small as to be almost undetectable, so that the contrast
> between, for example, *day* and *dare* may appear to be due to the difference
> between [eː] and [ɛː]."

and on triphthongs, that *fire* and *Ireland* are "often [faː], [aː] despite
usually being represented as /aɪə/". So a diphthong must not be given a large
glide merely because its symbol has two parts.

## Where the formant numbers are

Roach does not give any, and says where they are:

> "Typical formant frequency values for the vowels and diphthongs may be found
> in Gimson 1962 (ed. by Cruttenden, 2001, pp. 98–102)."

So the primary specification **does** carry formant values — the worry that it
might only give a cardinal-vowel quadrilateral is answered. Note the citation
is to the **2001** (Cruttenden-edited) edition: in the 8th edition the page
numbers will differ and the values may have been revised, so the pages are a
pointer, not a reference to copy.

## Consonants: the inventory is American, the realizations are not

The consonant set is the same as the American engine already has. What differs
is allophonic, and all of this is from the paper:

- **/l/ is clear ([i]-coloured) before vowels and dark ([u]-coloured)
  elsewhere.** American is darker across the board.
- **/r/ is a post-alveolar approximant [ɹ]** unless devoicing makes it a
  voiceless fricative.
- **Glottal stop**: syllable-final /p t k tʃ/ are frequently preceded by one
  unless a vowel follows; /t/ is sometimes *replaced* by one before a syllabic
  nasal, as in *button*. Its usual realization is "a few pulses of creaky voice
  at the end of the preceding vowel".
- /ð/ is often a weak dental plosive with no detectable friction; /nð/ often
  assimilates.
- /h/ is usually voiced [ɦ] between voiced sounds.
- Sonorants /m n ŋ l r/ may be syllabic in place of a weak syllable.
- Vowels are markedly shortened before fortis consonants; /p t k/ are aspirated
  before vowels, which partially devoices a following /l r w j/, and that
  devoicing is blocked when /s/ precedes the plosive.

The last two are shared with American and are already in the engine.

## Intonation: the paper does not contradict reusing the American model

> "Many transcription systems for English intonation have been proposed, but no
> accepted standard exists... it is difficult to identify any intonational
> features that are unique to English, and the nature and the number of
> pitch-patterns or pitch-accents is still a matter of dispute."

Rhythm is described as stress-timed, with "a very marked difference between
weak, unstressed syllables... and strong syllables". Carrying the American
intonation model over is therefore not ruled out by this source. Cruttenden may
say otherwise and would decide.

## The validation specimen

The paper ends with *The North Wind and the Sun* — the IPA's standard passage —
in both phonemic and allophonic transcription, from a named speaker (female,
born 1953, educated in southern England). That is a ready-made text for
checking the model against a described pronunciation.

## Can the engine express this inventory? Yes

Measured on 2026-10-07 by running Roach's own keyword for each of the 22 vowels
through the American engine with `tvtts_text_to_phonemes` and reading back the
symbol it chose. **Every one came back distinct — no two GB vowels collide.**

| GB | engine | from | | GB | engine | from |
|---|---|---|---|---|---|---|
| ɪ | `i` | pit | | eɪ | `A` | bay |
| e | `e` | pet | | aɪ | `I` | buy |
| æ | `a` | pat | | ɔɪ | `y` | boy |
| ʌ | `v` | cut | | əʊ | `O` | go |
| ɒ | `o` | pot | | aʊ | `f` | cow |
| ʊ | `u` | put | | ɪə | `4` | peer |
| ə | `@` | about | | eə | `k` | pear |
| iː | `E` | key | | ʊə | `c` | poor |
| uː | `b` | coo | | ɑː | `r` | start |
| ɔː | `w` | paw | | ɜː | `3` | cur |

### Non-rhoticity costs no deletion rule at all

The American engine **fuses post-vocalic /r/ into the vowel symbol** rather
than emitting a separate segment: `car` is `Kr1`, `hear` is `H41`, `bird` is
`B31Dp`, `four` is `Fg1`. The `R` consonant appears only before a vowel —
`red` `Re1Dp`, `around` `@Rf1NDp`, `bright` `BRI1Tp` — which is exactly the
position GB keeps it in.

So "drop coda /r/" is already true of the phoneme string. What makes the accent
non-rhotic is **retargeting six vowels**, and nothing else:

    3  NURSE -> /ɜː/     4  NEAR  -> /ɪə/     k  SQUARE -> /eə/
    c  CURE  -> /ʊə/     r  START -> /ɑː/     g  NORTH  -> merges into /ɔː/

`g` is the only merge: American keeps NORTH (`g`, *north force short*) apart
from THOUGHT (`w`, *paw caught law*), and GB has only /ɔː/ for both.

### Two things a transform cannot recover, and so need word lists

**BATH.** GB /ɑː/ in *bath path dance last laugh grass chance after*; the
American engine gives every one of them `a`, the TRAP vowel, and gives *cat
gather random anger* that same `a`. The split is lexical, so it is a list.

**Mary / marry / merry.** American merges all three to `Mk1E`. GB keeps
/ˈmɛəri/, /ˈmæri/ and /ˈmɛri/ apart. That distinction is gone before we ever
see the phoneme string, so no rule recovers it.

Other intervocalic /r/ words **are** recoverable, because the American vowel
still identifies the GB one — the r-coloured vowel becomes its short
counterpart with /r/ after it:

    sorry  Sr1E      -> /ɒ/      hurry  H31E     -> /ʌ/
    orange r1|NJz    -> /ɒ/      forest Fg1|STp  -> /ɒ/
    spirit SB41R|Tp  -> /ɪ/      mirror M41R3    -> /ɪ/

Only `k` is ambiguous, and that is exactly the Mary/marry/merry set.

### What the string does not yet carry

**Linking /r/ is not produced.** `far away` comes back `Fr1%@WA1` with no `R`
at the junction, and `idea of` inserts a `Y` glide rather than an intrusive
`R`. Both are right for American and both are additions GB needs. The engine
has an `R` to insert, so this is a rule in the transform, not a missing
capability.

## What was open here, and where it went

All five went to the primary specification and four came back answered. The
answers are in `gimson8.md`; this is the index.

1. **Formant targets** — answered. Three tables in §8.6, each with its own
   speakers and condition. Table 6 (connected speech, 10+10) drives the pure
   vowels, Table 4 supplies /ɛː/, Table 5 the diphthongs, and /ə/ takes /ɜː/'s
   values on the book's own instruction.
2. **The BATH word list** — answered, and better than feared: it is a rule
   (/a/ → /ɑː/ before a voiceless fricative, or a nasal plus a consonant) with
   a named exception list, not a bare lexicon.
3. **Mary / marry / merry** — the vowels are settled (/ɛː/, /a/, /e/). What is
   *not* settled is how to recover them, since the American engine merges all
   three; the `<rr>` versus `<r>` spelling cue is our inference and is still to
   be tested.
4. **Linking and intrusive /r/** — answered in full: a seven-vowel trigger set
   that coincides with the retargeted vowels plus schwa, and a five-level
   grading of which insertions to make and which to refuse.
5. **Whether the 8th edition revises Roach** — yes, twice, both marked above.

A sixth, opened later and closed on 2026-10-09:

6. **Whose measurements the tables are** — footnote 3 to §8.6, which the
   §8.6 spread does not carry. The notes to chapter 8 are at the chapter's
   end, p.159, under a plain **Notes** heading that the contents does not
   list. Tables 4 and 6 are **Deterding (1990, 1997)**; Table 5 was computed
   at Manchester; Table 4's /ɛː/ is the first element of [eə]. That the
   tables are older than the prose, and by a third party, is what released
   §7.10.2(2)'s fronting of /ʊ, uː, ʊə/ — the last item of the tier the notes
   committed to implementing. Rendering it cost one page: `pypdf` **is**
   available to the system Python (6.17.0), so `build/check/g4_to_png.py`
   opens this book as well as the Japanese scans, and an earlier note here
   saying otherwise was wrong.
7. **Whether the unread sections hid anything** — §§8.4, 8.13, 8.14, 9.2.8 and
   9.8, read 2026-10-11 now that the renderer opens the book, and none of them
   changed any code. §8.4's pre-fortis clipping is already in the engine and
   close to the book's own ratios; §8.13 and §9.8 are distributional; §9.2.8's
   glottal uses are optional throughout, the §7.10.3 verdict. §8.14 paid for
   the reading twice over: it corroborates the transform from the frequency
   side, and it shows that this file's pointer for the `TF`/`LF` columns was
   wrong. Details in `gimson8.md`.

### What is built, and what is not

The accent is the `en-GB` language (`lang/eng/port/tvtts_engb.c`), voices
32–42, and it is the American engine throughout — same text analysis, same
lexicon, same letter-to-sound rules, same prosody, same consonants.

**Built, in nine layers.**

1. **The vowel system** (`src/engine/gb.c`, table from
   `tools/gen_gb_vowels.py`). All 21 vowels re-aimed at Cruttenden's measured
   values. Applied **to the track buffer** at the end of `Stage3_Phone`, over
   the frames the phoneme just wrote.

   Not to stage 3's *targets*, which was the first attempt and is nearly a
   no-op: an r-coloured vowel's body is drawn by the glide tables straight
   into the tracks and never passes through `s3_param` at all. Measured with
   `build/check/gb_formants.exe`, which wraps `Synth_Frame` and reports the
   exact formants the filter bank is handed — shifting the targets moved 6
   frames of *nurse*'s approach, left all 19 frames of the vowel body
   identical, and changed F2 by exactly 0 Hz.

   And not as a **shift**, which was the second attempt and the one that
   reached the audio without sounding British. A vowel here is two targets,
   not one — where it starts (`g_pt_*`) and where it glides to (`g_at_*`) —
   so adding a constant moves both ends together and the vowel keeps its
   American *shape*. For the six vowels American spells with an `<r>` that is
   exactly wrong, because their shape **is** the /r/: with the shift in place
   *far* still ran F2 from 1076 up to 1452, *four* from 812 to 1316 and
   *fair* down from 1772 to 1604, the r-gesture fully intact and only its F3
   colouring removed.

   What is there now **remaps** the two ends independently: a frame is read
   as a position on the American line from `g_pt_*` to `g_at_*` and written
   to the matching position on the GB line. That makes those six steady for
   free, since GB's glide target for them *is* its primary, so the American
   line collapses onto one GB value. Nothing in the file says "flatten", and
   nothing in it knows which vowels are r-coloured. Measured: *far* is now
   flat at F2 1156 against Cruttenden's 1155, *four* at 828 against 828,
   *fair* at 1868 against 1864. Across the inventory F2 lands within the
   8 Hz quantiser step at both ends of every vowel but two.

   The same remap carries F3, which is why there is no longer a floor and no
   longer a 2000 Hz r-colouring threshold — a number that was nobody's
   measurement. F3 is a no-op for fifteen of the twenty-one vowels because
   Cruttenden publishes F1 and F2 only and the rest keep the engine's own
   value; the six that differ are exactly the r-coloured ones. The clamp it
   replaced stepped NURSE's F3 by 720 Hz in a single frame. Over 38 words GB
   now moves F3 *less* per frame than American does (p99 160 Hz against
   208 Hz), which is the r-gesture being gone, while F1 and F2 move at the
   same rate as American (p99 56→72 and 216→224 Hz) — so the remap adds no
   discontinuity of its own.

   **Two vowels are not aimed at Table 6.** §7.10.2(2) fronts /ʊ, uː, ʊə/ to
   [ʉ, ʉː, ʉə] and footnote 3 shows Table 6 predates the change, so for these
   the goal is a band rather than a figure: in front of the measured back
   value, no further than the central landmark /ɜː/ F2 1436. GOOSE already
   sat inside it at 1364 and keeps its target; FOOT sat at 1172, exactly on
   Table 6, and its target moves +173 Hz so that it lands at 1348 — 16 Hz
   behind GOOSE, where Deterding measured the pair 18 Hz apart. CURE's onset
   takes the same offset. `tools/gb_vowel_audit.py` prints the band and says
   whether each of the two is inside it, so this cannot quietly rot.
2. **The lexicon** (`lang/eng/data/gb_lexicon.h`, from
   `tools/gen_gb_lexicon.py`): 191 words where the two accents choose a
   *different* vowel, which no re-aiming can reach — BATH, PALM, CLOTH and yod
   coalescence. Applied as a phoneme substitution in the middle of stage 1,
   not as a pronunciation, so the word class, the stress and every
   context-sensitive allophone survive.

3. **Non-rhoticity and the /r/ links** (§12.4.7), which turned out to be one
   rule and not two: **/r/ survives only before a vowel.** The silent /r/ of
   *card*, the linking /r/ of *far off*, the intrusive /r/ of *vanilla
   essence* and the refusal to intrude in *straw in the wind* are all that
   one statement, plus the question of where American put an /r/ to begin
   with.

   Almost none of it had to be written. The American engine already folds a
   post-vocalic /r/ into the vowel in front of it and then drops the
   consonant — `Stage1_Vowel` in `src/engine/vowel.c` — so a finished
   American string carries no post-vocalic `R` at all: *card* is `Kr1Dp`,
   *bird* `B31Dp`, *four* `Fg1`. And the vowel it folds to is exactly the one
   GB uses for that lexical set, so *card park bird north start large heart
   word work first* need nothing from us and `gb_check` asserts they come out
   identical to American.

   So the change is `gb_keep_r`: **decline that deletion when a vowel
   follows.** Markers are skipped, which is what makes the word-boundary case
   work — the next word's `&` or `%` sits between, and those are type-3 nodes
   like any phoneme, so `Node_NextWord` stops on them rather than stepping
   over. That one predicate gives (a) *boring*, (b) all six of §12.4.7(1)'s
   own examples, and (c) *vanilla essence*.

   The only thing stage 1 adds is a **candidate** `R` at the end of a word
   American spells with the r-coloured vowel from the start — *far* `Fr`,
   *wear* `Wk`, *answer* `aNS3`, *near* `N4` — and after schwa, which is case
   (c). There is nothing there for `gb_keep_r` to decline deleting
   otherwise. `Stage1_GbRhotic` is nine lines.

   **The candidate after schwa leaves a visible trace, and it is the right
   one.** Appending `R` to a word-final `@` lets the engine's own
   `Stage1_Vowel` fold the pair into `3`, exactly as it already does for
   American *better* `&Be1t3`; `vowel.c` then drops the `R` again unless a
   vowel follows, so *vanilla* ends on `3` whether or not the intrusion
   fires. Nothing chose that, but it is correct: GB merges lettER and commA
   on one /ə/, `gen_gb_vowels.py` hands `3` and `@` the same target, and the
   fold gives the final vowel of *vanilla* the same length as *better*'s —
   15 frames against 18, where an unfolded `@` would be 6. Function words are
   untouched, so *a* keeps its 6-frame weak form. The one cost is that the
   audit's NURSE row counts these alongside real NURSE, which is part of why
   NURSE and comma disagree about F2 from an identical target.

   **(d) is refused with no list and no spelling lookup**, because a rhotic
   accent is itself the record of which /r/s are historical: American
   pronounces those and only those. Measured, and the separation is exact —
   *floor store for nor* end on `g`, American's r-coloured THOUGHT, while
   *law saw straw thaw jaw draw raw claw* end on plain `w`; *far* ends on `r`
   while *spa bra ma pa schwa* end on `o`. **(e) is refused by
   construction**, since the candidate only ever goes at a word end, so no
   suffix can attract one.

   `build/check/gb_links.exe` prints any phrase in both accents side by side,
   and `gb_check` holds all twenty cases.

8. **The *orange* class**, which §7.12.1 names: GA has /ɔː/ or /ɑː/ where GB
   has /ɒ/, so the engine gave *sorry* START, *porridge* and *moral* NORTH,
   *forest* and *origin* THOUGHT — the whole class long where GB is short.
   §8.9.8's own /ɒ/ spelling table carries *sorry* and *quarrel*, and its
   prose *borrow*.

   It falls out of the machinery layer 7 already built: the vowel letter in
   front of the `<r>` picks the vowel, so `<orr>` simply means LOT, and the
   single-`<r>` members join the short list. One rider from the same table's
   note — an `<a>` after `w` or `qu` is Middle English [a] rounded, so
   *quarrel* and *warrant* are /ɒ/ and not the TRAP the `<arr>` rule would
   otherwise give them.

   The engine spells that vowel `O`, GOAT, which had to be added to the set
   of vowels the rule will touch; *goat*, *boat* and *home* are unaffected,
   being reached by neither the spelling rule nor the list.

7. **Mary, marry and merry**, which §7.12.1 says GA merges and GB keeps
   apart, and which was the oldest open question here. The engine merged them
   harder than the note recorded: `Stage1_Vowel` folds `a`, `e` and `A` in
   front of an `R` into `k`, so *Mary marry merry very carry parent parish
   baron carol berry cherry terrible* all came out /ɛː/ — two thirds of the
   class wrong, *very* included.

   The note said the `<rr>` cue had to be measured before it went in. It has
   been, against the book's own data: §8.9.4's spelling table for /ɛː/ gives
   twenty-nine examples and not one doubles the `<r>`. So a doubled `<rr>`
   takes the short vowel, and the vowel letter picks which — `<arr>` TRAP,
   `<err>` DRESS. A single `<r>` keeps SQUARE unless the word is on a short
   list of ours, which exists mainly for *very*.

   **Stopping the fold was the hard part**, because it runs a pass later and
   cannot see spelling. It tests the *immediately* next node for `R` while
   every rule around it asks for the next phoneme, so a syllable marker
   between vowel and `R` hides it from that one rule and from nothing else —
   and that is where the syllable boundary of /ˈme.ri/ falls anyway.

   One trap worth recording: *merry* is `MeRE` and has an `R` to keep, but
   *berry* is `BkE` and has none, the r-coloured `k` being the /r/ by itself,
   so turning it into DRESS without supplying one would have said /ˈbeɪ/.
   This took a debug print to find, because `gb_spans.exe` prints the span
   *after* `Stage1_GbRhotic` has already inserted that `R` — the trace and
   the rule were looking at different node lists.

6. **What §7.12.1 says, which is the section that should have been read
   first.** It is the book's own GA/GB comparison, and it states the
   differences we are implementing rather than grading GB's internal changes
   the way §7.10 does — which is why the intervocalic tap, named there
   explicitly, never appeared in the §7.10 work order. Full table in
   `gimson8.md`. Everything in it was already done except two things.

   **/l/ is dark in all positions in GA; GB has clear [l] before a vowel and
   dark elsewhere.** The engine mostly had the GB distribution already —
   *lesson* `Le1S@N` against *field* `FE1jDp` — but one branch darkened an
   /l/ after a vowel whatever followed, so *follow* was `Fo1jO`, *colour*
   `Kv1j3`, *salad* `Sa1j@D`, *lily* `Li1jE`, all with a dark l standing in
   front of a vowel. GB now keeps those clear and leaves *field, feel, milk*
   dark. Before /j/ — *million*, *value* — is left alone, /j/ not being a
   vowel.

   **And a defect the /r/ work had left behind.** §7.12.1's /ɒ/ examples
   *orange* and *porridge* are a class the engine spells with an r-coloured
   vowel and no `R` node at all: *orange* is `r|NJz`, *forest* `Fg[|ST`. So
   making those vowels plain dropped the /r/ outright and *orange* came out
   /ɑːɪndʒ/. The engine's own insertion covers *sorry*, *borrow* and *moral*
   but not these, because they sit before `|`. Putting the /r/ back is
   §12.4.7's case (a), which the book calls obligatory, so it is not a
   choice — and it closes the *during* gap at the same time, which had been
   recorded here as too risky to attempt. Only the r-coloured vowels qualify;
   schwa stays at the word boundary, since its /r/ is the intrusive one and
   intruding mid-word is case (e).

   Their **vowel** is a separate matter and still open: *orange, porridge,
   horrid, forest, origin, florist, moral* have GB /ɒ/ where we give /ɔː/ or
   /ɑː/.

5. **Calibrating the targets against what comes out** (`tools/gb_vowel_audit.py`).
   Astra's objection: Table 6 is measured in *connected speech* and is
   therefore already centralised — §8.6 says so outright — while the engine
   reduces on its own account, so aiming Table 6 at it may centralise twice.
   The right goal for the **output** is not automatically the right **target**.

   Measured over the 22 sample sentences, attributing frames to vowels through
   `gb.c`'s `tv_gb_frame_trace` hook so this is read by vowel rather than
   guessed from the shape of the numbers. Two results.

   The risk is **not systemic**, and the obvious remedy is worse than the
   problem. Eight of the fourteen pure vowels already land within a few Hz of
   Table 6 — THOUGHT +1/+0, NORTH +1/+0, START +4/+1, FOOT +1/−1, KIT −5/−1,
   SQUARE +2/+4. Rebuilding the table from Table 4 (citation) was costed, not
   argued: summed error goes from 666 Hz to 1154 Hz, because it moves the
   eight that are right to help the three that are not.

   But three vowels **do** miss, all on F1, all in the same direction, and all
   of them short — TRAP −118, STRUT −96, DRESS −74 — while every long vowel
   lands. The engine undershoots F1 where there is not time to reach it. The
   misses never change sign and DRESS's quartiles span only 28 Hz, so the
   target is moved by the measured miss: aim past Cruttenden so the *output*
   arrives on him. TRAP went to −2, DRESS to −2, STRUT to −24, nothing else
   moved by a digit, and the total fell from 666 Hz to **406 Hz**. That the
   other eleven rows did not budge also confirms the arithmetic the comparison
   rested on — for a steady vowel the remap is affine in the target, so a
   table swap shifts output by exactly the difference between the tables.

4. **The intervocalic tap** (§7.10 does not list it, because it is an
   *American* feature rather than a GB change, which is why it was missed on
   the first pass through the work order). American taps a /t/ or /d/ between
   vowels — *water* [ˈwɔːɾɚ], *city*, *writer* — and GB has a real [t]. It is
   about as loud as an accent marker gets, and it was still there.

   The engine taps in **two** places, which is what made this more than a
   one-line fix. Most of it is a rule in `src/engine/vowel.c`, five sites
   that rewrite the stop to `t`; GB does not enter four of them and takes the
   engine's own existing "keep the T" path at the fifth. That alone fixes
   *city, pretty, better, get up, not a lot*, and stops *writer* being a
   homophone of *rider* and *latter* of *ladder*.

   The rest arrive from the **dictionary already tapped**, before any rule
   has run: *water* `Wwt3R`, *little* `Litl`, *metal* `Metl`, *subtle*
   `Svtl`, *leader* `LEt3R` — five in fifty tap-prone words put through
   `gb_spans.exe`. A tap is one phoneme and the dictionary does not record
   which stop it came from, and four of those five are /t/ while *leader* is
   /d/, so **the spelling decides**: a `<t>` and no `<d>` gives [t], a `<d>`
   and no `<t>` gives [d], and a word with both is left tapped rather than
   guessed at. `Stage1_GbDeflap` is twenty lines.

   One regression on the way, worth keeping: *bottle* is `BoT@L` and American
   makes it `Bo1tl` — the tap and the syllabic /l/ are **one rule**, keyed on
   the tap. Suppressing the tap therefore also lost the syllabic /l/ and
   *little* came out `LiT@j`, a schwa plus a dark l, which §7.10.3 names as a
   recent trend GB has *not* adopted. Two conditions now admit a real `T`, and
   *little* is `Li1Tl` again.

   Also done from §7.10.2, the "well established" tier: **(8) /ɔː/ for /ʊə/**
   as a word list — *poor sure tour moor* move and *pure cure gourd lure doer
   fewer newer viewer sewer* do not — and the engine proves a word list is the
   only option, since *cure* `KYbR` and *pure* `PYbR` are identical after the
   first consonant, the same wall *ant* and *aunt* put up in BATH. *cure* is
   left out on purpose: the section lists it among the words that may take
   /ɔː/, but its own worked contrast puts the yod words on the conservative
   side. **(3) happY tensing, (6) /j/ after /n/ and (7) /ʒ/ in imports were
   measured and need nothing** — the American engine already gives *happy*
   `HaPE` with FLEECE, *news* `NbZ` with no yod, and *beige* `BAz` with /ʒ/.

   **§7.10.2(5), /t/ as [ʔ] before a consonant**, which completes the
   "well established" tier. The engine already had this rule and GB needed
   one condition dropped: the American version wants the following phoneme
   to be a *sonorant* (lo bit 1, set for the vowels and for L R W N M), so
   *that man* was `xaq&Ma1N` in both accents while *not very* stayed `No1T`.
   GB asks only that it not be a vowel, and the book's own example comes out
   `No1q%Vk1RE`. The two that must not glottal both hold: *not a lot* keeps
   [t] before a vowel, and *little* keeps a real [t] word-internally, which
   is where the section warns the glottal is stigmatised -- it never reached
   this rule, being word-internal, so nothing had to exclude it.

   And the `5` vowel, which was the one row the GB table had no entry for:
   the engine merges `I` + /r/ into a single phoneme for *fire* and *hire*,
   so those two words kept their American quality while everything round
   them moved. GB declines the merge and gets `I3`, PRICE plus schwa, both of
   which the table does have. The engine's own glide rule then makes it
   `FI1Y3` — which is §12.4.7(2)'s slight linking [ʲ] after /aɪ/, and is
   exactly the shape American already uses for *higher*.

9. **Syllabic consonants, and two items that measurement closed.**

   §7.10.3 lists schwa plus a non-syllabic consonant, where a syllabic one
   has been the norm, among the trends GB has *not* adopted — naming
   *garden, bitten, middle, bottle*. American splits them and the engine
   followed it, so *awful* was `w1F@j`, *uncle* `v1~K@j`, *whistle* `Wi1S@j`,
   *able* `A1B|j`. GB keeps them syllabic. `T` alone had been exempted
   earlier because de-flapping broke *little*; this is the rest of the same
   rule. *garden*, *camel* and *final* still split, by a different path.

   **Except after /r/, and that was caught by ear, not by reading.** The
   first version exempted every trigger, and *barrel* came out `Ba1Rl` with
   the /r/ inaudible — wedged between TRAP and a syllabic dark l with
   nothing to lean on. The engine had been right: §7.10.3's examples are all
   after stops, and English keeps the schwa after /r/, so *barrel*,
   *quarrel*, *moral*, *coral* and *laurel* are /-ərəl/ and not /-rl̩/. That
   one trigger stays live for GB.

   While there: `<irr>` joins the layer-7 cue as KIT — *squirrel, stirrup,
   mirror, irritate*, which were NURSE or NEAR. It took four goes at the
   vowel set, each found by reading the SPAN rather than the finished
   string: *squirrel* already had KIT and needed only the marker, since it
   is the fold that makes it NURSE; *stirrup* arrives as STRUT; and leaving
   FLEECE out did not make the rule do nothing to *irritate*, it made it
   walk past the first vowel and rewrite the wrong syllable. `<urr>` is
   deliberately not done: *hurry* and *curry* are STRUT but *furry* is
   NURSE, so there is no rule there.

   **The last three of the /r/ classes.** *laurel* was in the CLOTH lexicon
   and could never have worked there: `o` in front of an `R` folds straight
   back to START, so the substitution was undone a pass later. It moved to
   the short list, which can insert the marker. *worry* is /ˈwʌri/ and the
   engine gave NURSE; it has a doubled `<rr>` like *sorry*, so the list now
   runs **before** the spelling rule — an explicit exception should beat a
   general rule, and letting the rule go first had handed *worry* LOT
   without ever consulting the list. Each list row now names its own vowel
   rather than having stage 1 read it off the spelling, because the spelling
   does not always say: *laurel* is /ɒ/ while *Laura* and *aural* are /ɔː/.

   **And *garden*, *camel* and *final* are not a defect.** Their spans are
   `GrD|N`, `KaM@L`, `FIN@L` — a schwa in the dictionary from the start, not
   a syllabic consonant being destroyed, which is what *little* `LiTl` has.
   §7.10.3 says GB keeps a syllabic consonant where one has been the norm,
   not that one should be manufactured where the dictionary has a schwa, and
   /ˈkaməl/ and /ˈfaɪnəl/ are ordinary GB. Left alone.

   **The linking /r/'s duration needed no work.** §12.4.7 says the /r/ of
   *more ice* is shorter than that of *more rice* and carries no accent
   onset. Measured: 122 frames against 126, and the accent marker sits on
   the following word in one and not the other. The engine already does it,
   because the linking /r/ closes the preceding syllable rather than opening
   the next.

   **Nor did the weak vowels.** GB keeps /ɪ/ in unaccented syllables where
   much of GA has /ə/, and the engine already gives *rabbit* `Ra1B|T`,
   *honest* `o1N|ST`, *village* `Vi1L|Jz`, *useless* `Yb1SL|S` — with schwa
   where GB wants schwa, as in *private* `PRI1V@q`. §8.13 is confirmatory
   reading rather than work.

`build/check/gb_check.exe` holds the whole thing to changing every utterance
while leaving American byte for byte identical, and runs in the build.

### A trap worth remembering

The substitution patterns are matched against the phonemes **as stage 1 holds
them**, which is not what `tvtts_text_to_phonemes` prints: stress is not
assigned yet, stop releases are not added, and /st/ is still spelt with a
plain `T`. *student* is `STbD|NT` inside and `SDb1t|NTp` on the page, and
patterns written from the printed form put the yod substitution on the second
/t/. `build/check/gb_spans.exe` prints the real thing and the generator writes
every pattern against it, checking each occurs exactly once per word.

### Still genuinely open

- Whether *off* should keep a stress mark. It is the one CLOTH word whose
  mark disappears with the shorter vowel, which may well be right for an
  unaccented function word but has not been listened to.
- Everything in `gimson8.md`'s "Still to read" — §11.6 intonation, and the
  source of the spelling tables' `TF`/`LF` columns.

The NORTH/THOUGHT merge came off this list on 2026-10-11: both vowels take the
same line and both measure F2 828 against Cruttenden's 828, and the user
confirmed sample 04 by ear.

**Case (a) mid-word came off it too, and the entry that stood here was
stale.** It said *during* kept no /r/, because American spells it `Dci~` — an
r-coloured vowel straight onto a vowel with no `R` node to keep — and that
repairing it mid-word risked inventing an /r/ in *theatre* and *reality*.
`Stage1_GbRhotic`'s first loop does the repair, `gb_check` has asserted
`%Jzc1Ri~p.` since, and the feared invention does not happen: the loop keys on
an **r-coloured** vowel, while hiatus words are spelt with the engine's own
`Y` glide — *theatre* `&XE1Y@T3`, *idea* `&IDE1Y3`, *reality* `&REYa1j|TE` —
so it never fires on them. Measured over fourteen more witnesses on
2026-10-11: *curious, furious, various, jury, fury, purity, security, Europe*
all gain the /r/, *serious, hero, zero, spirit* already had it, and *tourist*
gains it and takes the CURE-to-NORTH swap as well.
