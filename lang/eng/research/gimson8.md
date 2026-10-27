# The primary specification: Cruttenden, *Gimson's Pronunciation of English*, 8th ed.

Confirmed from the book itself, not from a publisher page: **Eighth edition
published 2014 by Routledge, © 2014 Alan Cruttenden**, ISBN 978-0-415-72174-5
(hbk) / 978-1-4441-8309-2 (pbk). First edition published as *An Introduction to
the Pronunciation of English*, Edward Arnold 1962.

The book is copyrighted and is **not** in this repository; `.gitignore` keeps
`lang/eng/research/*` out except these notes. Page numbers below are the
book's own.

## GB and RP are the same accent, renamed (§7.6, p.80)

> "Because of this narrow use by many of the name RP, and the frequent
> hostility to it, the name of the accent described in this book has been
> changed to General British (GB). But it has to be made clear that, compared
> with previous editions of this book, **it is not a different accent that is
> being described, but an evolved and evolving version of the same accent under
> a different name.**"

and on the choice of name: "It is now indeed to be preferred, paralleling
GENERAL AMERICAN and its abbreviation GA."

So Roach 2004, whose title says Received Pronunciation, describes the same
accent at an earlier point in its evolution. It stays a supporting reference;
where a detail differs, this book decides.

## Which variety we target (§7.7, §7.8, pp.80–81)

The book names three, and we build **mainstream GB** — not the other two:

- **CGB, Conspicuous General British** — the 'posh' variety, "considerably
  declined in the last fifty years and now mainly limited to older speakers",
  and "often regarded as affected and a figure of fun".
- **RGB, Regional General British** — GB with a small admixture of local
  characteristics.

CGB is useful precisely because the book lists its markers, which gives us a
**do-not-implement list**:

- /ɪ/ word-finally in *city, happy, fully*
- a very open word-final /ə/, including in /ɪə/ and /ʊə/ (*bitter, here, sure*)
- a very open /ɜː/ in all positions (*burn, occur, certain*)
- /a/ diphthongised to [ɛə] (*mad, matter*); /əʊ/ to [ɛʊ] (*bone, open, window*)
- **"the maintenance of /tj,dj,sj/ in words like *tube, duty, suit* is
  notable"**

That last one corrects an assumption made before the book arrived: yod
retention in *tube/duty/suit* is a **CGB** feature, so mainstream GB is not
where we should put it.

## The inventory has changed since Roach 2004

From the contents (§8.9, §8.12) and §8.5 (p.104):

> "the short vowels /ɪ,a,ʌ,ɒ,ʊ,ə/ are given different symbols from the long
> vowels /iː,ɛː,ɑː,ɔː,uː,ɜː/ to show that they are different in quality as well
> as length."

Two differences from Roach, both material to us:

| | Roach 2004 | Gimson 8th |
|---|---|---|
| TRAP | /æ/ | **/a/** |
| SQUARE | /eə/, a centring diphthong | **/ɛː/, a pure long vowel** (§8.9.4) |

so there are **two** centring diphthongs, /ɪə/ and /ʊə/ (§8.12), not three.
Roach anticipated exactly this: he warned that *day* vs *dare* "may appear to
be due to the difference between [eː] and [ɛː]".

**SQUARE is therefore a steady long vowel for us, not a glide to schwa.**

## The formant tables (§8.6 'Acoustics of GB vowels', pp.104–105)

Three tables, each with its own speakers and condition. Those differ, so the
condition is recorded with every number.

Cruttenden's own caveats, which travel with the data:

- "Even though all the informants were speakers of GB, some had slight regional
  influences": Table 4, one male and one female with slight London influence;
  Table 5, two males and one female likewise; Table 6, one speaker with
  influence from the north of England.
- "The spread of values from which the averages are computed is in general
  greater for the females than for the males."
- **"There are no figures for /ə/, whose quality varies greatly according to
  phonetic environment, and whose average values may be taken to be equivalent
  to those for /ɜː/."**

### Where the numbers came from — footnote 3, p.159

Read 2026-10-09, after being open for most of the English work. The notes to
chapter 8 are at the chapter's end, on p.159 under a plain **Notes** heading,
which is why the contents and the §8.6 spread never led to them. Footnote 3
says three things, and each one matters:

1. **Tables 4 and 6, and Figures 10 and 11, are Deterding (1990, 1997).** Not
   Cruttenden's own measurements, and one researcher for both — which is good
   news for the citation-versus-connected comparison the two tables carry,
   since the contrast is within one body of work rather than across two.
2. **Table 4's /ɛː/ row is the first element of the diphthong [eə]**, not a
   measurement of a monophthong. We use that row for SQUARE as a steady long
   vowel per §8.9.4, so our SQUARE target is a diphthong onset by derivation.
   Appropriate — the onset of [eə] is where [ɛː] sits, and the book is doing
   the same equation we are — but it is derived, not observed, and is now
   labelled so.
3. **Table 5 was computed at Manchester** (Phonetics Laboratory, Department of
   Linguistics), so the diphthongs come from different speakers and a
   different laboratory than the pure vowels. Transferring a figure between
   Table 5 and Table 6 crosses studies.

**The consequence for /uː/ is the point of reading it.** The tables are older
than the book, so a change the 2014 text reports as already made cannot appear
in them. See §7.10.2(2) below.

### Table 4 — pure vowels, CITATION FORM, 8 male + 8 female

Monophthongal words, "mostly in the frame h–d".

| | F1 M | F1 F | F2 M | F2 F |
|---|---|---|---|---|
| iː | 275 | 319 | 2221 | 2723 |
| ɪ | 382 | 432 | 1958 | 2296 |
| e | 560 | 645 | 1797 | 2287 |
| ɛː | 538 | 691 | 1864 | 2210 |
| a | 732 | 1011 | 1527 | 1759 |
| ʌ | 695 | 813 | 1224 | 1422 |
| ɑː | 687 | 779 | 1077 | 1181 |
| ɒ | 593 | 602 | 866 | 994 |
| ɔː | 453 | 431 | 642 | 799 |
| ʊ | 414 | 414 | 1051 | 1203 |
| uː | 302 | 339 | 1131 | 1396 |
| ɜː | 513 | 650 | 1377 | 1593 |

Averages of this table, as the book states them: F1 512 (M), 594 (W); F2 1395
(M), 1655 (W).

### Table 5 — diphthongs, CITATION FORM, 3 male + 3 female

Both components, so this is the glide itself, not an endpoint guess.

| | 1st F1 M | 1st F1 F | 1st F2 M | 1st F2 F | 2nd F1 M | 2nd F1 F | 2nd F2 M | 2nd F2 F |
|---|---|---|---|---|---|---|---|---|
| eɪ | 587 | 581 | 1945 | 2241 | 413 | 416 | 2130 | 2204 |
| aɪ | 734 | 822 | 1117 | 1275 | 439 | 359 | 2058 | 2591 |
| ɔɪ | 477 | 428 | 824 | 879 | 443 | 334 | 1924 | 2520 |
| əʊ | 537 | 545 | 1266 | 1573 | 379 | 380 | 1024 | 1267 |
| aʊ | 780 | 901 | 1368 | 1538 | 372 | 403 | 1074 | 1088 |
| ɪə | 382 | 399 | 2096 | 2514 | 578 | 417 | 1643 | 1846 |
| ʊə | 426 | 420 | 1028 | 1157 | 587 | 485 | 1250 | 1258 |

Three speakers a sex is a small sample, and it is the only diphthong data the
book gives.

### Table 6 — pure vowels, CONNECTED SPEECH, 10 male + 10 female

| | F1 M | F1 F | F2 M | F2 F |
|---|---|---|---|---|
| iː | 280 | 303 | 2249 | 2654 |
| ɪ | 367 | 384 | 1757 | 2174 |
| e | 494 | 719 | 1650 | 2063 |
| a | 690 | 1018 | 1550 | 1799 |
| ʌ | 644 | 914 | 1259 | 1459 |
| ɑː | 646 | 910 | 1155 | 1316 |
| ɒ | 558 | 751 | 1047 | 1215 |
| ɔː | 415 | 389 | 828 | 888 |
| ʊ | 379 | 410 | 1173 | 1340 |
| uː | 316 | 328 | 1191 | 1437 |
| ɜː | 478 | 606 | 1436 | 1695 |

Eleven rows: **/ɛː/ is not in this table.**

## Which table a reader should be built from

Cruttenden states the difference himself:

> "A comparison of the values for citation forms and those for connected speech
> shows that the values for connected speech represent vowels considerably less
> peripheral (more centralised) in articulatory terms."

A screen reader produces connected text, not citation forms, so **Table 6 is
the reference for the eleven pure vowels it covers**, and building from Table 4
would make the voice over-articulated. This is the same choice the Japanese
work faced with Yazawa & Kondo's embedded versus isolated tokens, and this time
the book supplies both and says they differ.

That leaves two documented gaps, to be labelled as such wherever they are used:

1. **/ɛː/** has citation values only (Table 4).
2. **The diphthongs** have citation values only (Table 5), from three speakers
   a sex.
3. **/ə/** has no values at all; the book says to take /ɜː/'s.

## BATH is a rule with a list of exceptions (§8.9.7, pp.123–125)

The worry was that BATH is purely lexical and needs a hand-built word list.
The book gives an **environment**:

> "Where an /ɑː/ is followed in GB by a voiceless fricative or by a nasal plus
> a second consonant, General American and the north of England have /a/, e.g.
> in *laugh, pass, branch, advance*."

**Read as a productive rule this fails, and the failure was measured rather
than argued.** Running the candidate environment over the American engine's
own phoneme strings puts these in the BATH set, every one of which keeps /a/
in GB:

    before a voiceless fricative   gas  mass  maths
    before a nasal + consonant     ant  hand  random  ample  bang  anger

and *ant* `&a1NTp` is **character-for-character identical** to *aunt*
`&a1NTp`, so no rule over the phoneme string can ever separate them.

The quoted sentence is a statement of **correspondence** between accents — of
where GB's /ɑː/ shows up as GA's /a/ — not a generative rule, and it should not
have been written up here as one. The environment is a **filter that narrows
the candidates**; membership inside it is lexical. So BATH does need a word
list after all, and the book supplies most of one below.

The book names both kinds of exception, which is what makes the list bounded:

> "there are examples where GB itself has /a/ in these contexts, e.g.
> *passage, finance, gas, romance*."

> "In a number of other words GB is undecided; thus *lather, transfer,
> elastic, plastic* are words in which either /a/ or /ɑː/ may be heard."

So: carry the **BATH word list** below, use the environment only to bound what
needs checking, and for the undecided four either choice is defensible — the
book says both are heard, so they are not bugs.

The spellings table (p.124) gives the words the rule must cover, and its own
frequency figures. `TF` and `LF` are the book's columns; where they come from
is **still unlocated** — §8.14 was the guess and it is wrong, see below — so
these are quoted, not used:

| spelling | examples | TF | LF |
|---|---|---|---|
| ar | arch, bar, car, card, cart, carve, Charles, dart, far, farm, gnarled, hard, harm, larch, large, lark, march, part, parse, snarl | | |
| ear | heart, hearth | | |
| er | clerk, Derby, sergeant | | |
| | *ar, ear, er together* | 60% | 60% |
| a | **bath, dance, last, pass, past, plaster, raft** | | |
| al | **calf, calm, half, palm** | | |
| au | **aunt, laugh** | | |
| | *a, al, au together* | 34% | 32% |

Note to the table: *memoir, repertoire, reservoir*.

The `ar/ear/er` group — 60% of them — is **already correct**: the American
engine returns `r` for those words, because its own analysis fused the /r/ into
the vowel. Only the bolded `a/al/au` group is the split we have to create, and
the `al` subgroup (*calf, calm, half, palm*) is a silent-l case the rule above
does not reach, so those are a list.

The Sources table (p.125) adds more of the rule's members: *after, staff; bath,
path; ask, cast, pass; rather, father; dance* (ME [a] before [f,θ,s,ð,n]) and
*aunt, branch, chant, command*.

Two incidental facts from the same section, both useful:

- "/ɑː/ does not normally occur before /ŋ/."
- Smoothing (§8.11): [aɪə] and [aʊə] may become a long [aː] — *fire*, *tower*.

## SQUARE, TRAP, and Mary / marry / merry (§8.9.4, §8.9.5, pp.118–119)

The book confirms /ɛː/ is a **pure long vowel** and tells us how to build it:

> "For GB /ɛː/ the tongue is in the open-mid front position, i.e.
> approximately C.[ɛ]. The lips are neutrally open throughout."

> "A suitable realisation of this phoneme can be achieved starting from /e/ and
> adding length and opening the mouth somewhat."

Table 4 bears out the length but not the opening: /e/ is F1 560, /ɛː/ is F1
538 for the male group — essentially the same height. **So /ɛː/ is a lengthened
/e/, and the "opening" is advice to learners rather than a measured
difference.** The diphthongal [eə] is given as older-speaker and CGB, which is
another reason not to glide it.

The Mary/marry/merry problem now has a handle, because the book states a
**spelling** criterion:

> "All words having /ɛː/ have an <r> in the spelling, e.g. *bear, fare, pair,
> fairy*."

and the three words fall out of the book's own lists as:

| | vowel | section |
|---|---|---|
| Mary | /ɛː/ | 8.9.4, the `ar, are` spelling row |
| marry | /a/ | 8.9.5, listed by name in Spellings of /a/ |
| merry | /e/ | 8.9.5, the *merry, marry* minimal pair |

**Tested, and the book's own data confirms it (read 2026-10-07).** The
inference was that a doubled `<rr>` goes with the short vowel and a single
`<r>` with /ɛː/. §8.9.4's "Spellings of /ɛː/" table is the test: across its
four spelling rows it gives twenty-nine example words — *care* and *parent*
among them — and **not one of them doubles the `<r>`**. So a doubled `<rr>`
never takes /ɛː/, which is enough to key on, and every one of those
twenty-nine still comes out SQUARE in our engine after the rule went in.

A single `<r>` stays ambiguous, as expected: *Mary* and *vary* take /ɛː/ while
*very, parish, baron, carol, America, character, Paris, peril, merit* do not.
That side is a short list of ours, marked as a judgement in `src/engine/gb.c`
rather than attributed to the book, because its spelling rows are examples and
not an inventory, so a word's absence from them proves nothing. *very* is the
reason the list exists: too common a word to leave as /ˈvɛːri/.

## Linking and intrusive /r/: a graded rule (§9.7.2, §12.4.7, pp.222, 315–317)

This was open question 4, and the book answers it completely.

**The environment.** §12.4.7(1):

> "GB introduces word-final post-vocalic /r/ as a linking form when the
> following word begins with a vowel (and in some cases in morpheme-final
> position before a suffix as in *bore* /bɔː/ *boring* /ˈbɔːrɪŋ/). The vowel
> endings to which an /r/ link may be added are **/ɛː, ɑː, ɔː/** and those
> single or complex vowels which may have a final [ə] (**/ə, ɜː, ɪə, ʊə/**),
> e.g. in *far off, four aces, answer it, wear out, fur inside, near it, secure
> everything*."

Those seven are **exactly the six vowels non-rhoticity retargets, plus schwa**:

    ɛː = k    ɑː = r    ɔː = w, g    ə = @    ɜː = 3    ɪə = 4    ʊə = c

So the trigger set is already identified in the phoneme string and costs no
extra analysis.

**When to insert.** The book grades it, and the grading splits cleanly into
what we do and what we do not:

| | case | book's word | ours |
|---|---|---|---|
| (a) | before a vowel-initial **suffix**, /r/ historical (*boring*) | "obligatory" | **do** |
| (b) | before a vowel-initial **word**, /r/ historical (*pour it*, *over and over again*) | "optional, though generally present" | **do** |
| (c) | after **[ə]**, not historical (*vanilla essence*, *vodka and tonic*) | "generally used" | **do** |
| (d) | after **/ɑː/ and /ɔː/**, not historical (*nougat and chocolate*, *straw in the wind*) | "often avoided" | **don't** |
| (e) | before a **suffix**, not historical (*strawy*, *gnawing*) | "often strongly stigmatised" | **don't** |

(a)–(c) are linking plus the one intrusive case the book says is normal;
(d) and (e) are the intrusive cases it says speakers avoid or condemn. A reader
should produce the first three and never the last two.

**And a duration detail**, which matters because this engine renders segments:

> "Phonetically (as well as historically) the resulting /r/ closes the syllable
> rather than being initial in the next, e.g. the /r/ of *more ice* /mɔːr ˈaɪs/
> is **shorter** than that of *more rice* /mɔː ˈraɪs/, the latter also being
> associated with accent onset and possible pitch change."

So a linking /r/ is shorter than a word-initial /r/ and carries no accent onset.

**The [j] and [w] links are a separate thing, and we already do them.**
§12.4.7(2): after /iː ɪ i eɪ aɪ ɔɪ/ a slight linking [ʲ]; after /uː əʊ aʊ/ a
slight linking [ʷ]. The American engine already inserts a `Y` glide in *idea
of*, so this behaviour carries over unchanged and only the /r/ cases are
missing.

### /r/'s own acoustics (§9.7.2(2)(b), p.225)

> "F1 is between 120 and 600 Hz, the lower the frequency the greater the
> perceived impression of lip-rounding; F2 is between 700 and 1,200 Hz; F3 is
> notable by being very close to F2. However, since the steady-state formants
> are likely to be very weak, /r/ is more easily identifiable on spectrograms
> by its steeply rising transitions (for F1, F2, F3 and F4) to a following
> vowel."

**F3 very close to F2 is the whole character of English /r/**, and the
transitions matter more than the steady state — so the target matters less than
the slew into the following vowel.

## Which changes to implement: the book grades them (§7.10, pp.83–85)

§7.10 sorts recent changes into three tiers by how far they have spread, and
says so explicitly, so the tiers are a work order rather than a judgement of
ours. "The decision to regard a change as 'almost complete', 'well
established', or 'recent trend' is largely based on the judgement of
phoneticians although collected data is referred to wherever it exists."

### 7.10.1 Almost complete — "typical of almost all speakers". **Implement.**

1. **The /ɔː/–/ɔə/ distinction is lost**: *paw* and *pour*/*pore* are the same,
   usually [ɔː]. This is the NORTH/THOUGHT merge, i.e. engine `g` → `w`.
2. **/j/ is lost before /uː/ after /l, s, z/**: *luminous* /ˈluːmɪnəs/, *suit*
   /suːt/, *exhume* /ɪɡˈzuːm/.
3. **/r/ is a post-alveolar approximant in all positions**, not a tap between
   vowels: *very* [veɹi], *error* [eɹə].
4. **/əʊ/ is [əʊ]**, not the older [öʊ]: *over, boat, comb*.
5. **/tj, dj/ become /tʃ, dʒ/**: *culture* [kʌltʃə], *soldier* [səʊldʒə],
   *tune* /tʃuːn/, *endure* [ɪnˈdʒʊə]. Longer established unaccented than
   accented.

Items 2 and 5 together settle the yod question, and confirm from the other
direction what §7.7 implied: coalescence is near-universal GB, and keeping
/tj, dj, sj/ is CGB. So *tube* is /tʃuːb/, *duty* /ˈdʒuːti/, *suit* /suːt/.

### 7.10.2 Well established — "typical of the majority". **Implement.**

1. **/æ/ lowered to /a/** in *bad, land, mad, rat, flank, bang, cap* — the
   reason for the changed symbol. "Retention of the [æ] quality is a marker of
   CGB." Still longer than the northern vowel.
2. **/ʊ, uː, ʊə/ are fronted** to [ʉ, ʉː, ʉə] — *good, soon, cure, poor* —
   often with unrounding to [ɨ, ɨː]. May also front the [ʊ] of /əʊ/.
3. **happY tensing**: the final vowel of *city, happy, flabby, witty, daddy,
   lackey, sparky, baggy* is [i], not [ɪ]; "[ɪ] is now a marker of CGB". It is
   shorter than /iː/ and not diphthongised.
4. **[eə] becomes monophthongal [ɛː]** — the reason for the other changed
   symbol.
5. **/t/ becomes [ʔ] pre-consonantally**: *not very* [nɒʔ veɹi]. But
   glottalling before /l/ (*little* [lɪʔl]) "is inclined to be stigmatised".
6. **/j/ after /n/ is increasingly lost**: *news* [nuːz], *neuter* [nuːtə].
7. /ʒ/ in imports formerly anglicised to /dʒ/: *beige, rouge, adagio, genre*.
8. **/ɔː/ may replace /ʊə/** in *sure, poor, cure, moor, tour*. "This change is
   lexically conditioned" — impossible in *dour, gourd, lure, Ruhr, Ure* and in
   /uː/ + /ə/ words (*doer, fewer, newer, viewer, sewer*); *pure* stays
   [pjʊə] while *poor* is [pɔː]. **So this one is a word list, not a rule.**
9. A 'checking' high rise on declaratives in conversational narrative.

**Item 2 looked as though it contradicted the book's own formant tables.**
Table 6 gives /uː/ F2 1191 Hz (male), barely in front of /ʊ/ at 1173 — a back
vowel, not the fronted [ʉː] the prose describes. That held the item back for
most of the work, on the principle that the numbers are what we can act on.

**Footnote 3 dissolves it, and item 2 is now implemented.** The tables are
Deterding (1990, 1997); the prose is Cruttenden's own, in 2014. So this was
never the book disagreeing with itself — it is older measurements against a
later description of a change, and for a vowel the book says has moved, the
later evidence wins. Narrowly: **only the three vowels §7.10.2(2) names are
taken out of Table 6's hands.** Every other vowel still comes from it.

Roach 2004 agrees from the other side, and ambiguously. His prose names /uː/
among the symbols whose RP position is "quite far removed from the Cardinal
(or IPA) value of those symbols", alongside /æ, ɔː, ʌ, e/ — cardinal [u] is
fully back, so that is a claim of fronting. But his own Figure 1 still plots
/uː/ at the back-close corner, and his allophonic transcription of the North
Wind passage marks every /uː/ token with a quality diacritic the phonemic line
does not carry (the Kiel font's encoding does not tell us which diacritic).
2004 again predates 2014, so a back chart there is consistent with the change
being later.

**How far forward, given no published figure for [ʉː].** The landmark is the
central vowel of the same table, same speakers, same condition: /ɜː/ F2 1436,
which §8.6's note also makes the value for /ə/. Fronting has to land between
the measured back value and that. Measured with `tools/gb_vowel_audit.py`:

| | Table 6 F2 | measured before | after |
|---|---|---|---|
| GOOSE | 1191 | 1364 | 1364 (unchanged) |
| FOOT | 1173 | 1172 | 1348 |

GOOSE already overshot its target by 173 Hz — an engine miss like the three
F1 misses, but in the direction the book asks for and still 72 Hz short of
central, so it is kept rather than corrected. FOOT tracked its target exactly
and so was not fronted at all. In Table 6 the two sit 18 Hz apart in F2; in
our output they were 192 apart, which was the real defect. FOOT moves by the
same +173 and the pairing is restored at 16 Hz. CURE's first element is /ʊ/
and takes the same offset, 1028 → 1201 — but its base is Table 5, so that
transfers an offset between studies, which is why the result is still behind
Table 6's unfronted /ʊ/. Not applied: the clause that fronting "may also"
reach the [ʊ] of /əʊ/. "May" is not "does", and GOAT is signed off by ear.

### 7.10.3 Recent trends — "not yet typical of a majority". **Do not implement.**

/ɪə, ʊə/ as [ɪː, ʊː]; /r/ as the labiodental [ʋ]; /e/ lowered; [ə] plus a
non-syllabic consonant where a **syllabic** consonant has been the norm
(*garden, bitten, middle, bottle* — so GB keeps the syllabic consonant); and
wider dark-l vocalisation.

The one exception is the book's own: (6) [ɒʊ] for /əʊ/ before dark l in *goal,
bold, moult* "has now spread so widely that it is reasonable to consider it
part of GB". Listed as a trend, described as already GB — so it is optional for
us, and low priority.

## What matters most, by the book's own ranking (§13.3.2–13.3.3, pp.332–333)

These are the book's priorities for a learner, which is the closest thing it
offers to a specification for an imitation.

**Weak forms are the top item, and that is a surprise worth acting on:**

> "The weak forms of most function words occur overwhelmingly more frequently
> than the strong forms and **should be considered the usual pronunciation**."

> "**Lack of use of weak forms and pronouncing a full vowel in those unaccented
> syllables of words that should have an /ə/ will produce a rhythm which is
> seriously different from that of native speaker GB**" — and it adds that this
> holds for almost all other varieties too, UK, Ireland, North America,
> Australia and New Zealand.

So *and, but, at, of* are /ən, bət, ət, əv/, and the book notes native speakers
deny this about their own speech. With monosyllables "more than 80 per cent of
words" in conversation (§12.6), this is the highest-leverage single thing in
the chapter — **and it is shared with the American voice, so it is a check on
what we already have rather than a GB-specific change.** Correct syllable
accent is called "a high priority" alongside it.

On the diphthongs specifically:

- **/əʊ/: "the GB form [əʊ] is most widespread and should thus be
  recommended."** A fronted onset [ë] is "excessively CGB and affected".
- **/ɪə, ʊə/ must not finish below half-open** — lower "would smack too much of
  CGB, sounding posh and affected".
- /ɪə, ʊə/ "are increasingly heard as long monophthongs [ɪː, ʊː] and such
  pronunciations are entirely acceptable if aiming at GB" — so a small glide is
  safe, which matches Roach's warning about over-gliding.
- /aɪ, aʊ/ "can have a common (open central) starting-point", which Table 5
  bears out: 734/1117 and 780/1368 for the male group.
- Smoothing of /aɪə, aʊə/ "is less prevalent than it was and should be
  avoided" — so *fire* keeps its glide. **Do not implement the §8.11
  smoothing.**
- /ʊə/ "is now generally replaced by /ɔː/ in such words as *poor, sure, moor,
  tour*", agreeing with 7.10.2(8).

## §7.12.1 General American (pp.87-88) — the book's own GA/GB comparison

Read 2026-10-07, and it should have been read first: this is the section that
states the differences we are implementing, rather than grading GB's internal
changes the way §7.10 does. **The intervocalic tap is here and not in §7.10**,
which is exactly why working that list top to bottom missed it.

Cruttenden's points, with what we do about each:

| | GA against GB | ours |
|---|---|---|
| systemic | GA lacks GB's /ɪə ʊə ɛː/; they answer to GA vowel + /r/ (*beard, sure, fare*) | the non-rhotic fold, done |
| systemic | GA is rhotic, GB non-rhotic; GB /r/ essentially only before a vowel | done, and this is the one-line rule the /r/ work is built on |
| systemic | GA has no /ɒ/: most GB /ɒ/ is GA /ɑː/ (*cod, spot, pocket, bottle*), a subset GA /ɔː/ (*across, gone, often, cough, orange, porridge*), often before a voiceless fricative | CLOTH and PALM, done — but see the note below |
| lexical | GB /ɑː/ against GA /a/ before a voiceless fricative, or a nasal plus a consonant (*past, after, plant*) | BATH, done, 121 words |
| before /r/ | considerable re-alignment: *merry* and *marry* (sometimes *Mary* too) may fall together in GA | still open, and the reason *very* has the wrong vowel |
| realisational | GA /eɪ əʊ/ may be monophthongal [eː oː] | Table 5 gives both components, so ours glide |
| realisational | GA /r/ is more retroflex, or bunched | not modelled; it is a quality we have no figures for |
| **realisational** | **/t/ between vowels after an accent is usually a voiced tap in GA, sometimes [d], neutralising /t/ and /d/** | **done** — and *writer* no longer rhymes with *rider* |
| **realisational** | **/l/ is dark in all positions in GA; GB has clear [l] before a vowel and dark elsewhere** (§9.7.1) | **done** |

The Northern Cities Shift is also described, raising GA /a/ toward [ɛ]. Regional
rather than General American, so not modelled.

**One thing the /ɒ/ row turned up that is not in the book's list.** Its
examples *orange* and *porridge* are a class the engine spells with an
r-coloured vowel and no `R` node at all — *orange* is `r|NJz`, *forest*
`Fg[|ST` — so making those vowels plain for GB dropped the /r/ outright and
left *orange* as /ɑːɪndʒ/. Repaired by inserting the /r/ that the vowel was
carrying, which is §12.4.7's case (a). Their **vowel** was still GB /ɔː/ or
/ɑː/ where the book wants /ɒ/; `g_gb_short_r` in `src/engine/gb.c` now names
the vowel for each word of the class, so `gb_links.exe "orange forest
porridge"` gives `&o1R|NJz &Fo1R|ST &Po1R|Jz` — LOT, with the /r/.

## The sections that owe the British work nothing, read 2026-10-11

Five sections came off the reading list at once, now that the renderer opens
this book. None of them changed any code. Recording *why* each one is inert is
the point — an unread section is a risk, a read one that says nothing is not.

### §8.4 Vowel length (p.100) — and the engine already does it

§8.4.1 pairs the vowels phonetically: /ɪ–iː, ʊ–uː, a–ɑː, ɒ–ɔː, ə–ɜː/. Note (2)
says the opposition in each pair is "a complex of quality and quantity; and of
the two factors it is likely that quality is more important" — in *cad*/*card*
"both vowels may be equally long". Only /ə/–/ɜː/ is an opposition solely of
length. That is reassuring for us, since quality is the thing we retarget.

Notes (3) and (4) are pre-fortis clipping: /iː/ in *beat* is "only about half
as long as" in *bee* or *bead*, about the length of /ɪ/ in *bid*, and the same
holds for /uː/ (*boot*), /ɔː/ (*caught*) and all the diphthongs. Note (5):
before the nasals and /r, l/, length is about halfway between the voiced and
voiceless cases. Note (6) gives durations in centiseconds for accented
monosyllables, and its superscript points at chapter 8's note 1: **Wiik
(1965)**, carrying the caveat that Gonet & Stadnicka (2006) "give comparable
figures for citation forms but show the differences to be much less in
connected speech". A reader reads connected text, so Wiik's spread is an upper
bound for us, not a goal.

Measured against the engine — longest vowel run per word through `gb.c`'s
frame-trace hook, one frame being 10 ms and so one centisecond:

| | final | +voiced | +nasal | +voiceless | v'less ÷ voiced |
|---|---|---|---|---|---|
| **Wiik, long** | 30.6 | 31.9 | 23.3 | 16.5 | 0.52 |
| *bee / bead / bean / beat* | 30 | 23 | 27 | 13 | 0.57 |
| *do / food / boot* | 28 | 25 | — | 13 | 0.52 |
| *saw / cord / caught* | 32 | 37 | — | 24 | 0.65 |
| **Wiik, short** | — | 17.2 | 13.3 | 10.3 | 0.60 |
| *bid / bin / bit* | — | 19 | 17 | 12 | 0.63 |
| *cod / cot* | — | 30 | — | 22 | 0.73 |
| **Wiik, diphthong** | — | 35.7 | 26.5 | 17.8 | 0.50 |
| *play / played / plate* | 28 | 23 | — | 13 | 0.57 |
| *tie / tide / tight* | 44 | 32 | — | 21 | 0.66 |

So the engine already clips before voiceless consonants, at close to the
book's own ratio in every class, and its word-final long vowels land on Wiik's
30.6 almost exactly. It runs shorter than Wiik inside closed syllables, which
is the direction note 1's caveat predicts for anything less careful than a
citation form. **§8.4 names no GA/GB difference and §7.12.1 does not list
length among them**, so nothing here is owed to the British work. One
mismatch, recorded rather than acted on: in *bean* the engine makes the nasal
context longer than the voiced-stop one, where the book makes it shorter. One
word is not a rule.

### §8.13 Vowels in syllables without primary accent (p.157) — distributional

Table 7 sorts the vowels by what may appear Remote Preceding, Adjacent
Preceding, Adjacent Following and Remote Following the primary accent, in four
groups: Reduced (/ɪ, i/, /ʊ, u/, /ə/), Short, Long and Diphthongs, each with
example words. It is a statement about English word structure, which the
dictionary and the letter-to-sound rules already carry, not a synthesis
parameter. It does confirm the weak close vowels /i/ and /u/ as a class — the
/i/ half is happY tensing, §7.10.2(3), already built — and §8.13(3) defines a
reduced vowel as one of /i, ɪ, ə, ʊ/.

### §8.14 The frequency of GB vowels (p.158) — corroboration, and a correction

Table 8 gives percentages of all phonemes and of vowels only: /ə/ 26.91% and
/ɪ/ 21.03% of vowels, with vowels 39.4% of all phonemes. The source is chapter
8's note 22: **conflated from Fry (1947) and Knowles (1987)**, with four
further studies cited alongside. No sample size appears anywhere, and
"conflated" means these are not one count — both facts have to travel with the
numbers, per the lesson the Japanese frequency figures taught, so they stay
quoted and unused.

**A correction to this file.** The BATH spellings table above says the sample
behind its `TF`/`LF` columns "is recorded in §8.14". It is not. §8.14 is about
Table 8, the frequency of *vowels*, and defines neither TF nor LF; §9.8 does
the same for consonants and also defines neither. So that pointer was wrong,
not merely unread, and the basis of those two columns is still unlocated. They
remain quoted, not used.

**What §8.14 does give is corroboration of the whole transform, from a
direction that has nothing to do with formants.** On General American it says
frequency is similar "after allowance has been made for the absence of /ɛː, ɒ,
ɪə, ʊə/ (and /ʌ/ often combined with /ə/) and the corresponding higher
frequency of /r/ through its regular occurrence in coda positions". Those four
absent vowels are exactly the ones this work has to create — SQUARE as a pure
/ɛː/, LOT as an /ɒ/ distinct from THOUGHT, NEAR and CURE — and the /r/
difference is exactly non-rhoticity. The book's frequency comparison lands on
the same four places the transform does its work.

### §9.2.8 The glottal plosive (pp.182–183) — optional throughout

§9.2.8(2) splits the uses four ways: (a) regular glottal reinforcement in GB,
(b) extended reinforcement, (c) glottal replacement in GB, (d) wider
replacement in other dialects. Every case under (a) is hedged. A vowel hiatus
"may in careful speech be separated by [ʔ]" — *co-operate*, *geometry*,
*reaction*, *day after day*. The usage "is extended by some careful speakers"
where an intrusive /r/ is possible — *law and order*, *drama and music*. The
marker "is even applied by some speakers" where a linking /r/ is normal —
*later on*, *far off*, *four aces*. An initial accented vowel "may be
reinforced" under particular emphasis — *It's [ʔ] empty*.

That is the §7.10.3 pattern: described, optional, not the majority. So it is
recorded and not built, exactly as the trends were. The one glottal item the
book puts in the well-established tier, §7.10.2(5)'s pre-consonantal [ʔ], is
already implemented.

**It does settle a question left open by work already done.** We refuse to
intrude an /r/ in *straw in the wind*, which leaves the question of what GB
puts there instead. §9.2.8 answers it: a vocalic glide, or [ʔ] for careful
speakers. The refusal does not leave a gap the book says must be filled with
an /r/.

### §9.8 The frequency of GB consonants (p.235) — nothing owed

Table 14 is Table 8's shape for the consonants, with its own chapter-9
footnote for provenance, unread because no parameter depends on it. The
section's own point is that raw frequency matters less than functional load,
and that /θ/–/ð/ and /ʃ/–/ʒ/ carry very little of it. We use no consonant
frequencies.

## Still to read

- §11.6 Intonation (p.277), especially 11.6.1.3 types of nuclear tone. The
  only one left, and the only one that would be a day's work rather than a
  page: intonation reuses the American model (see `sources.md`), so reading it
  is a question of whether that reuse is defensible, not of a missing number.
- Where the `TF`/`LF` columns of the spelling tables come from. Not §8.14 and
  not §9.8, which was this file's guess and is wrong; still unlocated.

Footnote 3 to §8.6 came off this list on 2026-10-09, and §§8.4, 8.13, 8.14,
9.2.8 and 9.8 on 2026-10-11. All are recorded above.
