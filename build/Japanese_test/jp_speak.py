# -*- coding: utf-8 -*-
"""Japanese text in, audio out.

    python jp_speak.py "konnichiwa" out.wav [accent]

Romaji or kana.  `accent` is the Tokyo accent type: 0 for heiban (no nucleus),
n for a nucleus on mora n.  Mora timing is the papers' ~7 morae/s and the F0
contour is the Fujisaki model (fujisaki.py).
"""
import math
import ctypes, os, sys, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jp_devoice_tab as DV
import jp_mora as M, jp_voice as J, fujisaki as FJ

SR = 16000
FR = SR // 100                      # 160 samples = one 10 ms frame
MORA_FRAMES = 14                    # ~7 morae/s, the rate the rules assume
# INTRINSIC VOWEL DURATION.  Yazawa & Kondo (2019), from their released
# per-token data (JPLongShortVowels.csv, 3,200 tokens), male speakers,
# EMBEDDED position, in ms:
#
#            /i/    /e/    /a/    /o/    /u/
#   short   63.2   72.2   74.4   70.6   63.7      mean 68.8
#   long   141.5  148.4  158.0  147.5  143.4      mean 147.7
#
# Male because this voice is male; the two genders differ by about 10%.
# EMBEDDED rather than isolated because a reader produces connected text, not
# citation forms -- their isolated tokens run 8-10 ms longer, and this
# synthesiser has no position model to switch between the two.  The check in
# check_vowel_length.py synthesises words alone, which is the nearest thing it
# can do, so it compares against this same column.
#
# This replaces LONG_FRAMES, which was a single constant for the second mora of
# every long vowel and had been refitted four times -- 9 -> 7 -> 6 -> and it
# would have been 8 or 9 again -- against a long/short RATIO measured four
# different ways.  Three faults, all of them structural:
#
#   * a ratio is not a duration.  Fitting one constant to one ratio cannot be
#     right for five vowels whose ratios run 2.05 to 2.17 and whose durations
#     run 63 to 74 ms.
#   * the constant absorbed everything upstream of it.  Any change to consonant
#     timing moved the short vowel, which moved the ratio, which demanded a
#     refit -- documented three times in this file before anyone noticed the
#     pattern.
#   * it made phonology out of arithmetic.  "Long = 2.1 x short" is not a fact
#     about Japanese; it is an average over five vowels that each have their own
#     duration.
#
# So the model states durations, not ratios, and derives frames from them.
V_DUR = {'i': (63.2, 141.5), 'e': (72.2, 148.4), 'a': (74.4, 158.0),
         'o': (70.6, 147.5), 'u': (63.7, 143.4)}
# The target is the WHOLE VOICED VOWEL, not the steady part.  Setting the
# steady part alone was tried first and inverted the result: the transition
# into the vowel is voiced and at full amplitude, so the measurement counts it,
# and its length is _glide_len of the formant travel from the consonant's
# locus.  After /b/ that is 5 frames for /i/ and 3 for /u/, because /i/'s F2
# has 843 Hz to cover and /u/'s has 135.  Give both the same steady count and
# /i/ comes out 9 frames of vowel against /u/'s 7 -- which is backwards, since
# /i/ and /u/ are the two SHORT vowels in the data.  The `qv` line below has
# always said this ("the paper measures the whole vowel, so the factor scales
# the glide and the steady part together"); it just was not true of the target.
#
# Milliseconds to frames: a frame is 10 ms, and the measurement loses a fixed
# amount at the end, where the taper into the following consonant decays below
# the threshold that stands in for Yazawa & Kondo's hand-placed boundary.
#
# Measured on the renderer by forcing the allocation from 7 to 13 frames and
# reading back what the detector reports, over five vowels and three stop
# contexts.  The implied tail is 35-36 ms from 9 frames up (30 at 7, where the
# vowel floor binds).
#
# THIS WAS 20 ms AND THAT WAS WRONG.  It was measured against a search region
# built from a stale span index -- _compensate rebuilt the frame list but never
# wrote the new positions back, so every region ran about two frames late and
# every reading came back ~13 ms long.  The per-vowel exception that used to
# sit here for /a/ was an artefact of the same bug: with the region corrected
# /a/'s tail is 33.8 ms against a 33.8-36.5 spread, which is no exception at
# all.
#
# Astra's caveat stands and is worth keeping in view: this is calibrated
# against the very detector under investigation, so it is provisional
# compensation rather than an independently established amount of acoustic
# tail.  If the detector is later found to under-read, this moves with it.
VOWEL_TAIL_MS = 35.0
VOWEL_TAIL = {}

V_INTRINSIC = True     # off restores the flat vowel and the LONG_FRAMES = 6
                       # constant this replaced, for A/B listening

# EXPLICIT-DURATION MODE, for testing the renderer and building controlled
# stimuli.  Astra's point: measuring the renderer's response, or making a pair
# of stimuli that differ in one segment only, both need the segment allocation
# to be exactly what was asked for.  Ordinary speech keeps contextual timing;
# these are for experiments, and both default off.
#
#   NO_COMPENSATE        bypass the word-level budget entirely
#   VOWEL_ALLOC          {mora index: frames} -- set ONE mora's vowel, which
#                        V_DUR cannot do because it is keyed by vowel identity
#                        and so moves every occurrence of that vowel in the
#                        word.  /bepe/ has /e/ in both morae.
NO_COMPENSATE = False
VOWEL_ALLOC = None

def vowel_frames(v, long_v=False):
    """The whole voiced vowel, in frames, from its duration target in ms.

    Quantisation is real and worth knowing: at 10 ms a frame the five vowels'
    11 ms spread is barely one frame wide, so this can express two levels, not
    five.  The data happens to fall into two groups -- the high vowels /i/ and
    /u/ at 63 ms, the other three at 70-74 -- so the distinction that survives
    the quantiser is the one that is actually there.  A shorter frame would
    carry more of it.
    """
    if not V_INTRINSIC:
        return (9 + 6) if long_v else 9      # what the flat model produced
    t = V_DUR.get(v, V_DUR['a'])[1 if long_v else 0]
    tail = VOWEL_TAIL.get(v, VOWEL_TAIL_MS)
    return max(MIN_VOWEL, int(round((t + tail) / 10.0)))

V_DEFAULT = 9          # frames for the mean vowel, 68.8 ms; the offset that
                       # keeps a mora of the average vowel at MORA_FRAMES

def long_extra(v):
    """Frames the length mark adds: the difference between the two targets,
    which is what the second mora is worth acoustically."""
    return max(1, vowel_frames(v, True) - vowel_frames(v, False))
# Idemaru & Guion (2008), J. Int. Phon. Assoc. 38(2): a geminate is not just a
# longer closure.  Closure 69 -> 206 ms, but also the PRECEDING vowel lengthens
# (59 -> 75 ms, +27%), the FOLLOWING vowel shortens (76 -> 63, -17%), the F0
# fall across the consonant is about 30 Hz greater, and V1 becomes louder than
# V2 where for a singleton V2 is the louder.  Ranked by how well each classifies
# the contrast on its own: F0 ~77%, V1 duration next, intensity ~66%, V2
# duration similar, voice quality (H1-H2) at chance -- so no voice-quality term.
Q_V1_LONGER = 1.27
Q_V2_SHORTER = 0.83
Q_F0_FALL = 21.0                    # Hz of extra fall across the closure
Q_INTENSITY = 2                     # dB, V1 above V2
NASAL_RELEASE = True                # give the nasal release a frame of its own
FINAL_PREDICATE = True              # Hirose: the last phrase takes the M grade
KAWAI_SANDHI = True                 # Kawai 7.1: only the first D takes AH
KA_DELAY = 0.070                    # Hirose: /ka/'s command onset, delayed 70 ms
PREVOICE = True                     # a voiced stop's closure carries a voice bar
PLOSIVE_R = True                    # Arai: phrase-initial /r/ is plosive-like
HIATUS = True                       # glide between adjacent vowels
HOMMA_VOT = True                    # VOT is shorter word-medially
WORD_BUDGET = True                  # Homma: compensate across the word
MIN_VOWEL = 2                       # frames of steady vowel never spent
# The least steady vowel a mora may keep.  It was 3 frames, and it BOUND for
# every voiceless obstruent -- after /p t k s sh ch ts j/ the vowel came out
# 30 ms against Yazawa's measured 65-75, where after a voiced stop or a
# sonorant it was 60.  The consonant and its transition had eaten the mora.
#
# Two targets pull against each other and cannot both be met by a floor:
#
#   Homma's voiceless/voiced vowel ratio   0.73   -> floor 4 gives 0.67
#   Yazawa's absolute short vowel       65-75 ms  -> floor 6-7
#
# At 6 the floor binds everywhere and the voiceless/voiced contrast disappears
# entirely, which is worse than either error.  5 keeps the contrast at 0.83 and
# puts the shortest vowel at 50 ms rather than an implausible 40.
#
# Raising it costs nothing in rate: the corpus runs at 7.26 morae/s for every
# floor from 3 to 7, because the word budget pays for a longer vowel out of the
# other morae.  That invariance is the clearest evidence the compensation works.
VOWEL_FLOOR = 5
# Homma Table II, MEDIAL single-stop closure: voiceless 67 ms, voiced 44.  The
# contrast here was 50 against 40, less than half of his 23 ms.
#
# His Table VI also has the vowel after a voiceless stop at 77 ms against 105
# after a voiced one, and that was briefly applied as a separate multiplier on
# the vowel.  It is NOT a separate fact: it is what the closure contrast
# PRODUCES inside a fixed budget.  Applying both double-counted and made
# /gaga/ 320 ms against /papa/ 280, where Homma measures 267 and 260 -- the
# multiplier lengthened the word instead of redistributing within it, which is
# the exact error his paper is about.  Widening the closure contrast and
# leaving the vowel as the remainder gives his compensation for free.
CLOSURE_MEDIAL = {0: 7, 1: 4}       # by voicing, in frames
# Kitazawa & Kiriyama (2004), "Acoustic and Prosodic Analysis of Japanese
# Vowel-Vowel Hiatus with Laryngeal Effect", Interspeech/ICSLP.  Where an
# accentual phrase ends in a vowel and the next begins with the SAME vowel --
# ga-aru, wa-ame, to-omou -- there is no formant movement to mark the boundary,
# and the glide added for aoi does nothing.  Their 45 hiatus from the Japanese
# MULTEXT corpus, by EGG and spectrogram:
#
#   17 clear glottalization, 23 weak, 5 phrase-final nasalization instead
#
# so about 89% carry glottalization.  Their worked example gives the size of it:
# the EGG open quotient falls 80% -> 45% -> 60% and F0 falls with it,
# 235 -> 142 -> 178 Hz.  As ratios of the pre-boundary F0 that is 1.00 -> 0.60
# -> 0.76, which is what is used here, over a few glottal periods.
#
# The 2006 follow-up, "Acoustic Features of Japanese Vowel-Vowel Hiatus at
# Prosodic Boundaries", corrects the size of this.  The 2004 figure above is
# from a phrase that paper describes as "focused and emphasized", so a 40% dip
# is the PROMINENT extreme, not the general case.  2006's own worked example is
# 249 -> 195 -> 239 Hz, a 22% dip recovering to 96%, and it adds the corpus
# average in its Table 1 -- but measured as the change between ADJACENT
# PERIODS, which is a different quantity from the depth of the whole dip:
#
#     hiatus    % decrease per period    % increase
#     a | a            0.96                 3.25
#     i | i            3.22                 1.70
#     o | o            3.46                 5.12
#
# "about 3% ... standard deviation 0.9%".  The two agree: a 22% dip spread over
# the seven or so periods the dip spans is about 3% a period.  So the default
# takes 2006's example and the emphasized case keeps 2004's.
#
# Their per-vowel spread is used as a scaling on the depth.  It is why a|a --
# which is ga|a and wa|a, much the commonest hiatus -- comes out subtle: he says
# in both papers that those "depict rather vague features of glottalization if
# the following phrase is not emphasized".  u|u and e|e he reports as too
# uncommon to measure, so they take the mean.
#
# The scale is anchored on the two examples whose VOWEL is identified, one for
# each condition, rather than on an unlabelled one:
#
#   2006 Fig. 3   "another a|a hiatus where F0 drops about 10 Hz"   ~4% at 250 Hz
#   2004 Fig. 1   kichinto | okonawarenakatta, so o|o, and that paper calls it
#                 focused and emphasized: 235 -> 142 -> 178 Hz, a 40% drop
#
# With the per-vowel scalings below that fixes the base depth at 10.5% neutral
# and 29.4% emphasized, which reproduces both anchors exactly.  The mean also
# agrees with Table 1 read as compounding: 3% a period over the five periods
# either side of the boundary that his excising experiment spans is about 12%.
# How far it recovers is taken from each paper's own example -- 2006 recovers
# 81% of the drop, 2004 only 39%.
GLOTTAL = True
GLOTTAL_F0 = (0.895, 0.980)         # [K06] base depth 10.5%, recovers 81%
GLOTTAL_F0_EMPH = (0.706, 0.821)    # [K04] base depth 29.4%, recovers 39%
GLOTTAL_VOWEL = {'a': 0.38, 'i': 1.26, 'o': 1.36, 'u': 1.0, 'e': 1.0}
GLOTTAL_FRAMES = 3                  # ~7 periods at their speaker's 250 Hz
GLOTTAL_DIP = 8                     # dB; creak is quieter as well as lower.
                                    # He measures open quotient, not amplitude,
                                    # so this one is authored.
# "The ratio of duration of vowel segment in the preceding phrase-final position
# to the following phrase-initial position was 1.7 in average.  In cases the
# following word is emphasized ... the ratio becomes as low as 0.76."  The pair
# totals about two morae either way, so this redistributes rather than adds.
HIATUS_RATIO = 1.7
HIATUS_RATIO_EMPH = 0.76
NASALIZE = True                     # the +/-nasal contrast across a hiatus
NASAL_BW = 60                       # Hz on each bandwidth; he measures the
                                    # contrast spectrographically, as "lower
                                    # high frequency energy", not numerically,
                                    # so the size of it is authored.
# Caveat on the a|a anchor: his Fig. 3, the one example whose vowel is named
# AND whose size is given, is labelled BI = 1 -- the WEAKEST break index of the
# five.  So 4% is a floor for a|a rather than its mean, and a|a at a stronger
# boundary should be deeper than this gives.  He publishes no BI-by-depth
# table, so there is nothing to scale it with; noted rather than guessed.
SMOOTH_VC = True                    # offglide aims where the consonant lands
BRIDGE_F3 = True                    # F3/F4 bridged across voiced continuants
FINAL_N = True                      # utterance-final /N/ is uvular, not a
                                    # nasalised copy of the vowel before it
PHI = True                          # Ruddell's four realizations of /f/, and
                                    # the initial lip closure that tells the
                                    # commonest two apart.  Off restores the
                                    # single aspirated posture /f/ had before.
FRIC_GEMINATE = True                # a geminate before a fricative is noise,
#                                     not silence: see the geminate branch
FRIC_GROWN = 4                      # frames a fricative gains before a devoiced
                                    # vowel: base 10 + 4 fills the mora exactly.
                                    # Neither Yamakawa paper measured a devoiced
                                    # vowel -- both chose undevoiced ones -- so
                                    # this is sized to the mora, not measured.
# Downstep is no longer a fitted multiplier.  It was one -- 0.80, fitted to
# Kubozono's Table 22 inter-peak ratio of 0.898 -- but that only ever had his
# accented-to-accented case to answer to, and it got his accented-to-heiban
# case badly wrong: 0.904 against a measured 0.799.  Kawai's accent sandhi
# (section 7.1, `_sandhi`) covers BOTH from a rule rather than a fit, because
# only the FIRST accented word takes the full AH and a later heiban word takes
# FM rather than FH.  Measured against Kubozono's own two numbers:
#
#                                accented->accented   accented->heiban
#     Kubozono measured                 0.898               0.799
#     fitted DOWNSTEP 0.80              0.871               0.904
#     Kawai sandhi, no downstep         0.838               0.790
#
# Mean absolute error halves, 0.066 to 0.035, and the remaining gap on the
# accented pair is speaker difference: Kawai's AM of 0.35 is a little low for
# Kubozono's speakers, who want about 0.44, which is not one of the six
# symbols.  Stacking a multiplier on top of the grading moves BOTH further off,
# so this stays at 1.0.  Chaining over three or more phrases still happens --
# 137.0, 118.3, 109.5 -- through the decay of the phrase component, which is
# where Fujisaki puts it rather than in the command amplitudes.
DOWNSTEP = 1.0
# Kawai rules (2)-(5) share a clause: if the distance from the preceding phrase
# symbol is L1 morae or less, use the weak symbol instead, or omit it.  L1 = 5
# at about 7 morae/s, the rate this runs at.  This was used to stop a second
# major phrase peaking ABOVE the first, but that was treating the symptom:
# Kawai's own remedy is in section 5.2 and is a signal-level rule, that a phrase
# command's magnitude is whatever reaches a target PEAK LEVEL given whatever is
# already there (FJ.phrase_amp).  That holds at any spacing, so it is used
# instead.  L1 is a text-level rule about where PAUSES go, which is a separate
# question and needs the parser we do not have.
KAWAI_L1 = 5
TRANS = 3                           # Morikawa: the vocalic transition is ~26 ms
OFFGLIDE = 3                        # frames of glide from a vowel INTO one
SLEW = 200                          # Hz of F2 a formant will move in one frame
TRANS_MAX = 8

def _cl(v, lo, hi): return max(lo, min(hi, int(v)))
def _rnd(x): return int(x + 0.5)

# RATE COMPENSATION FOR THE PARALLEL NOISE BRANCH.
#
# The engine's resonators are normalised at zero frequency, which does not
# preserve noise RMS across sample rates, and the output shaping behaves
# differently at the same physical frequency when the rate changes.  So the
# same noise tracks come out louder at 11025 than at 16000, by an amount that
# grows with the resonance TRACK 6 ACTUALLY DRIVES -- F5, which the engine
# derives and which no input track sets.  Over postures verified to have
# distinct effective F5 the penalty runs +8.58 dB at F5 3970 to +12.45 at 4720.
#
# This compensates OUR settings, which were fitted in the 16 kHz mode.  11025
# is the engine's historical rate; nothing here says the engine is wrong.
#
# THE OFFSETS ARE MEASURED, NOT DERIVED.  A first attempt subtracted the dB
# penalty from the track values on the assumption that a unit is about a
# decibel.  It is not.  Track 6 at 70 and at 66 produce IDENTICAL output -- the
# band saturates above ~66 -- so on /s/, whose shipped value is 70, the first
# four units of a 12-unit cut did nothing and the cut bought 5 dB instead of
# 12.  Between 62 and 50 it is roughly a decibel a unit; below that it is not.
#
# So each offset is found by rendering the posture at both rates and searching
# for the unit offset whose 11025 level matches its own 16000 level.  Residuals
# are 0.05-0.31 dB.  They differ per segment because saturation depends on the
# source's starting values as well as on F5 -- /s/ and /ts/ share F5 4720 and
# need 20 and 35, because SIBIL_S starts at 70 and SIBIL at 80.
#
# Track 8 is NOT trimmed: it bypasses the resonators and measures +0.05 dB
# across the two rates.  Track 2 (aspiration, through the cascade) gains 3.3 dB
# and is left alone -- a different path, wanting its own treatment rather than
# being swept in here.
NOISE_RATE_TRIM = True
# Aspiration, track 2, through the CASCADE rather than the parallel branch.
# Its rate penalty varies strongly with the posture's own formants -- +8.65 dB
# on /i/ against +0.74 on /o/ -- so it is one table per posture, not one
# number.  Measured the same way: render at both rates, search for the track-2
# offset whose 11025 level matches its own 16000 level.  Residuals 0.04-0.43 dB.
#
# This matters more than its size suggests.  Track 2 carries every DEVOICED
# VOWEL, and over all 486,646 pronounced naist-jdic entries devoiced vowels are
# 10.1% of CV morae -- 9.0% counting bare vowels into the denominator too --
# and appear in 30.5% of entries.  /i/ is both the worst-affected posture and
# one of the two vowels that devoices.  (This comment used to print 9.0% and
# 30.3% "over 20,000 dictionary pronunciations"; both values are the whole
# dictionary's, and that sample actually gives 7.3% and 25.9%.)
ASPIR_TRIM_UNITS = {'Vi': 9, 'Ve': 4, 'Va': 4, 'Vo': 1, 'Vu': 3,
                    'h': 4, 'hy': 6, 'f': 2}
NOISE_TRIM_UNITS = {'s': 20, 'sh': 26, 'z': 13,          # held-frication match
                    'ts': 30, 'ch': 17, 'j': 14}       # matched IN CONTEXT:
# the affricates' noise region includes their stop closure, which does not
# scale with rate, so a held-frication calibration overcorrected them by 3-6 dB
# in a real word.  Measured the same way, on the ratio the word actually shows.


_trim_for = 0        # parallel-branch units, set by _noise_trim()
_aspir_for = 0       # cascade/track-2 units, same mechanism

def frame(sp, src=None):
    F1, F2, F3, F4, B1, B2, B3 = sp
    t = [0]*22
    t[0] = 60; t[16] = 14; t[18] = 16; t[19] = 8; t[17] = 50
    t[9]  = _cl(_rnd(F1/4.), 1, 255);  t[10] = _cl(_rnd((F2-500)/8.), 0, 255)
    t[11] = _cl(_rnd(F3/16.), 1, 255); t[12] = _cl(_rnd(F4/16.), 1, 255)
    t[13] = _cl(_rnd(B1/2.), 1, 204);  t[14] = _cl(_rnd(B2/2.), 1, 204)
    t[15] = _cl(_rnd(B3/2.), 1, 204)
    if src:
        for k, v in src.items():
            t[k] = v
    if NOISE_RATE_TRIM and SR != 16000:
        if _trim_for and any(t[k] for k in (4, 5, 6)):
            for k in (4, 5, 6):
                if t[k] > 0:
                    t[k] = max(0, t[k] - _trim_for)
        if _aspir_for and t[2] > 0:
            t[2] = max(0, t[2] - _aspir_for)
    return t

class _noise_trim(object):
    """Scope the rate compensation to what is actually being emitted.

    `c` keys the parallel branch (sibilants); `aspir` keys track 2, either a
    consonant name or 'V' plus a vowel for a devoiced vowel.
    """

    def __init__(self, c=None, aspir=None):
        on = NOISE_RATE_TRIM
        self.n = NOISE_TRIM_UNITS.get(c, 0) if on and c else 0
        self.a = ASPIR_TRIM_UNITS.get(aspir, 0) if on and aspir else 0

    def __enter__(self):
        global _trim_for, _aspir_for
        self.old = (_trim_for, _aspir_for)
        _trim_for, _aspir_for = self.n, self.a

    def __exit__(self, *a):
        global _trim_for, _aspir_for
        _trim_for, _aspir_for = self.old


def _lerp(a, b, f):
    return tuple(a[k] + (b[k] - a[k]) * f for k in range(7))

def _fric_noise(c, v, voiced):
    """The source, posture and track a fricative onset raises.

    One place, because a geminate before a fricative has to raise exactly the
    noise the fricative itself will: the two are one continuous sound and any
    disagreement between them is a step in the middle of it.
    """
    if c == 's':
        src = J.SIBIL_S
    elif c == 'sh':
        src = J.SIBIL
    elif c in ('h', 'hy', 'f'):
        src = J.ASPIR
    elif voiced and J.VOICED_SIB:
        src = dict(J.SIBIL_Z)          # /z/: noise on track 8, not on F3
    else:
        src = dict(J.SIBIL)
        src[0] = 40
    key = 2 if c in ('h', 'hy', 'f') else 8
    return src, key


def _fric_env(n, peak, rise_frac=0.40, depth=18, fall_frac=0.22):
    """Yamakawa & Amano (2015): a fricative's intensity envelope is a trapezoid
    -- a rise, a steady part, then a decay -- fitted by three straight lines.

    Their Fig. 2 puts the rise at about 40% of the frication for both manners
    (/s/ 55 of 145 ms, /ts/ 32 of 74), so the shape scales and only the total
    length distinguishes a fricative from an affricate.  A flat switch on and
    off, which is what this did before, is the one shape the measurement rules
    out.  Track 8 is roughly a decibel a unit, so `depth` is in dB.
    """
    # A geminate splits one trapezoid across two morae, so each end can be
    # asked for separately: the geminate raises the noise and holds it, the
    # mora after it holds and lets it go.  rise_frac or fall_frac at 0 drops
    # that end.
    nr = int(n * rise_frac + 0.5) if rise_frac > 0 else 0
    nd = int(n * fall_frac + 0.5) if fall_frac > 0 else 0
    if rise_frac > 0:
        nr = max(1, nr)
    if fall_frac > 0:
        nd = max(1, nd)
    out = []
    for i in range(n):
        if nr and i < nr:
            g = peak - depth * (1.0 - (i + 1) / float(nr))
        elif nd and i >= n - nd:
            g = peak - depth * 0.45 * ((i - (n - nd) + 1) / float(nd))
        else:
            g = peak
        out.append(int(g + 0.5))
    return out

VOICELESS = set(['k', 'ky', 't', 'p', 'py', 's', 'sh', 'h', 'hy', 'f',
                 'ch', 'ts'])

def _glide_len(a, b, base):
    """How many frames a move of this size needs.

    A fixed count is wrong at both ends: /a/ to /k/ moves F2 by 272 Hz and
    wants three frames, /i/ to /w/ moves it by 1367 and crammed into four that
    is 340 Hz a frame, which is heard as a step.  Articulators have a speed, so
    the glide is sized from the distance -- about 200 Hz of F2 per 10 ms frame.
    """
    # F3 counts too: a sibilant carries its own front-cavity resonance, so
    # going from /sh/ at 3000 to a vowel at 2275 is a real move even when F2
    # barely shifts.  Weighted below F2, which is the one the ear follows.
    d = max(abs(a[1] - b[1]), abs(a[0] - b[0]) * 2, abs(a[2] - b[2]) * 0.7)
    # Rounded to NEAREST this was an average, not a limit: a move of 1.2 frames'
    # worth got one frame and ran at 240 Hz, and once the glide landed on its
    # target instead of a step short of it, every consonant in the inventory sat
    # at 208-248 Hz for exactly that reason.  Rounding up makes SLEW mean what
    # it says.  TRANS_MAX still caps it, so a move over 8 frames' worth is still
    # faster than the limit -- there is no such move in the inventory.
    return max(base, min(TRANS_MAX, int(math.ceil(d / float(SLEW)))))

def _taper(out, n, floor, voiced_through=False):
    """Bring the vowel's level down into a closure instead of cutting it off.

    Kashino (1992), section 4.1: the amplitude envelope either side of the
    closure cues the consonant's manner and voicing.  A vowel running at full
    level until the instant of closure is the one envelope that cannot happen --
    the mouth is already closing through the end of it.  A voiced stop keeps
    some voicing into the closure, so it tapers less far.
    """
    n = min(n, max(0, len(out) - max(1, floor)))
    if n <= 0:
        return
    drop = 6 if voiced_through else 14          # dB; track 0 is ~1 dB a unit
    for i in range(n):
        idx = len(out) - n + i
        if out[idx][0] <= 0:
            continue
        out[idx][0] = max(16, int(out[idx][0] - drop * (i + 1) / float(n) + 0.5))

def _dip(out, center, n, depth):
    """Arai's AVd and AVg: a short V in voicing amplitude across the flap's
    contact.  Without it the identical formant movement is heard as an
    approximant -- a legitimate Japanese /r/, but not the flap, which is the
    typical intervocalic phone.  "The intensity dip induces a sensation of the
    flap sound."  The dip straddles the apex, so it has to be applied after the
    transition out of the flap has been written, not inside the flap branch.
    """
    if n <= 0 or depth <= 0:
        return
    half = n // 2
    for i in range(-half, n - half):
        idx = center + i
        if idx < 0 or idx >= len(out) or out[idx][0] <= 0:
            continue
        f = 1.0 - abs(i) / float(half + 1)
        out[idx][0] = max(16, int(out[idx][0] - depth * f + 0.5))

def _offglide(out, prev_sp, osp, n, floor=0):
    """Bend the tail of the vowel already written toward the consonant coming.

    Without this every V-C boundary is a step -- measured on `sakura`, F2 moved
    616 Hz in a single frame going into the flap, and F1 336 going into /k/.  It
    is not only a smoothness matter: the formant transition in the *preceding*
    vowel is the main cue to where a consonant is articulated, so leaving it out
    makes place depend on the burst alone.

    The lerp lands ON the target, not one step short of it.  It used to divide
    by n+1, which leaves the last vowel frame a full step away from the posture
    the consonant then starts at -- so the move was always one step incomplete
    however many frames it was given, and that remainder was itself a step.  On
    /iwa/ it was about 150 Hz of the 304 that showed.
    """
    # Never reach back past `floor`: the frames before it are the previous
    # mora's own glide, and bending those reverses a move already in flight --
    # measured on `shashin`, that turned a 171 Hz-a-frame descent into a 504 Hz
    # step where the two met.
    avail = max(0, len(out) - max(1, floor))
    use = min(n, avail)
    extra = n - use                     # a move too big for the steady vowel
    if use <= 0 and extra <= 0:
        return
    base = len(out) - use
    keep = None
    for i in range(use):
        idx = base + i
        if out[idx][0] == 0:            # already silent; nothing to bend
            continue
        keep = dict((k, out[idx][k]) for k in (0, 1, 2, 3, 4, 5, 6, 8))
        out[idx] = frame(_lerp(prev_sp, osp, (i + 1) / float(n)), keep)
    # If the vowel was too short to hold the whole move, the move takes longer
    # rather than going faster: /i/ to /w/ travels 1367 Hz of F2, and squeezing
    # that into whatever frames happened to be free is what left a 304 Hz step.
    for i in range(use, n):
        if keep is None:
            break
        out.append(frame(_lerp(prev_sp, osp, (i + 1) / float(n)), keep))

# Rule 1's /masu/ and /desu/, as far as a mora string can tell.  The analyser
# also requires the SU to end a verb, auxiliary or interjection; nothing here
# has a part of speech, so this takes the two preceding morae and accepts the
# false positives that follow from that.
POLITE_SU = 'su'
POLITE_BEFORE = ('ma', 'de')

# Rule 5's candidate classes, generated by tools/gen_ja_devoice.py from
# upstream's own tables.  A mora devoices before the morae of its class and
# no others, and the exclusions are the point: /su/ not before another s-row
# mora, /fu hi fi/ not before an h-row one, so that two fricatives are never
# left with nothing between them.
_DV_CLASS = dict((m, i) for i, cs in enumerate(DV.CAND, 1) for m in cs)


def _polite_su(morae, n, question):
    """Is morae[n] the /su/ of /masu/ or /desu/, and allowed to devoice?"""
    if morae[n] != POLITE_SU or n == 0 or morae[n - 1] not in POLITE_BEFORE:
        return False
    # "unless a question mark or a long vowel follows, where the question has
    # to rise on it".  A long vowel is its own mora, so one following already
    # means this is not phrase-final and we never got here; the question mark
    # is stripped by to_morae and has to be passed in.
    return not (question and n + 1 >= len(morae))


def build(morae, emph=(), devoiced_in=None, question=False):
    """-> (frames, mora end times in seconds, geminate closure end times)

    devoiced_in, when given, is one flag per mora from njd_set_unvoiced_vowel
    and overrides the string-level rule below.  Default None keeps the
    string-level rule, which is what the oracle and the romaji path use.

    question matters only to that rule, and only for a phrase-final /su/.
    """
    # Which morae sit either side of a geminate, so the vowels can be given the
    # durations and levels Idemaru & Guion measured.
    before_q = set()
    after_q = set()
    for i, mo in enumerate(morae):
        if mo == 'Q':
            j = i - 1
            while j >= 0 and morae[j] in (' ',):
                j -= 1
            if j >= 0:
                before_q.add(j)
            if i + 1 < len(morae):
                after_q.add(i + 1)
    # Kitazawa: an accentual-phrase boundary where a vowel meets a vowel.  The
    # phrase-final mora lengthens and the phrase-initial one shortens, keeping
    # the pair at about two morae; the phrase-initial vowel is glottalized.
    # "In cases the following word is emphasized, the preceding vowel is not
    # lengthened ... then the ratio becomes as low as 0.76 for example."  He
    # gives that as an example rather than a mean, so it is used as one.
    ph, k = [], 0
    for mo in morae:
        ph.append(k)
        if mo in (' ', '|', '||'):
            k += 1
    em = list(emph) + [0] * (len(ph) + 1)
    glottal, same_v1, same_v2, nasal_v1 = set(), {}, {}, set()
    for i, mo in enumerate(morae):
        if mo != ' ' or i == 0 or i + 1 >= len(morae):
            continue
        v1 = M.split(morae[i - 1])[1]
        c2, v2 = M.split(morae[i + 1])
        if not v1 or c2 or v2 not in 'aiueo':
            continue                       # needs a vowel then a bare vowel
        glottal.add(i + 1)                 # the phrase-initial vowel
        # Which cue marks the boundary follows from the particle, and his own
        # grouping gives it away: "ga|a, ni|i, no|o" show the +/-nasal
        # contrast, while "wa|a, shika|a, te|e, to|o" are glottalized.  The
        # first group is exactly the particles whose consonant is NASAL -- ni
        # and no have /n/, and ga is [Na] in Tokyo Japanese -- so the vowel
        # before the boundary is nasalized by its own consonant and the one
        # after it is not.  The two cues mostly co-occur rather than compete:
        # of his 16 nasal-contrast cases, 4 used it alone and 12 had
        # glottalization as well, so this is added and nothing suppressed.
        if M.split(morae[i - 1])[0] in ('n', 'm', 'ny', 'my'):
            nasal_v1.add(i - 1)
        if v1 == v2:                       # the case with no formant cue left
            r = HIATUS_RATIO_EMPH if em[ph[i + 1]] > 0 else HIATUS_RATIO
            same_v1[i - 1] = 2.0 * r / (1.0 + r)
            same_v2[i + 1] = 2.0 / (1.0 + r)
    q_ends = []
    q_fric = False      # a geminate raised the next fricative's noise already
    spans = []
    out, ends = [], []
    prev_sp = J.V['a']
    prev_v = ''          # the vowel before the mora being built
    steady_at = 0        # where the current mora's steady vowel began
    for n, mo in enumerate(morae):
        c, v = M.split(mo)
        # Homma: temporal compensation works within the WORD.  Each mora
        # records where it starts, which stretch of it is plain steady vowel
        # and therefore adjustable, and what it is nominally worth; the
        # redistribution at the end of this function spends the difference.
        mora_start = len(out)
        spans.append({'start': mora_start, 'steady': None, 'len': 0,
                      'head': 0, 'nominal': 0, 'adjustable': False,
                      'budget_break': mo in ('|', '||')})
        sp_rec = spans[-1]
        if mo == ' ':
            # An accent phrase boundary is a PITCH event, not a silence: the
            # articulation runs straight through it.  It gets an entry in
            # `ends` so the mora times stay parallel to `morae`, and no frames.
            ends.append(len(out) / 100.0)
            continue
        if mo in ('|', '||'):                          # major phrase: a pause
            _taper(out, 3, steady_at, False)
            n = int(round(FJ.PAUSE['S2' if mo == '||' else 'S3'] * 100))
            out.extend(frame(prev_sp, {0: 0}) for _ in range(n))
            sp_rec['nominal'] = len(out) - mora_start    # a pause is not spent
            ends.append(len(out) / 100.0)
            steady_at = len(out)
            continue
        if mo == ':':                                  # long vowel: hold it
            steady_at = len(out)
            extra = long_extra(prev_v)
            out.extend(frame(prev_sp) for _ in range(extra))
            sp_rec.update(steady=steady_at, len=extra, nominal=extra)
            ends.append(len(out) / 100.0)
            continue
        if mo == 'Q':                                  # geminate: a closure
            # The vowel before a geminate still has an off-glide toward the
            # consonant being doubled, and it matters: Yanagisawa & Arai (2015)
            # found the 50% "geminate" crossover moves from 184 ms of closure
            # WITH the transition to 240 ms without -- the transition alone is
            # worth 56 ms of closure -- and without it even a 380 ms closure
            # only reached 80% identification.  This branch had no off-glide at
            # all, which is exactly their no-transition condition.
            nxt = morae[n + 1] if n + 1 < len(morae) else ''
            nc = M.split(nxt)[0] if nxt else ''
            csp = J.coda(nc, prev_v) if nc else None
            if csp:
                _offglide(out, prev_sp, csp,
                          _glide_len(prev_sp, csp, OFFGLIDE), steady_at)
            _taper(out, 3, steady_at, False)
            steady_at = len(out)
            nq = MORA_FRAMES
            nman = J.MANNER.get(nc, ('none', 1, 0, 0))[0]
            if FRIC_GEMINATE and nman == 'fric':
                # A GEMINATED FRICATIVE IS LONG FRICATION, NOT A CLOSURE.
                # /ʃː/ in スラッシュ has no closure phase at all -- the noise
                # simply runs for about twice as long.  Filling this with
                # silence and letting the mora after it raise a singleton's
                # worth of noise builds the one thing a fricative is not: a
                # silence followed by a short fricative is an AFFRICATE, which
                # is what the ear reported, スラッシュ arriving as "surachu".
                # Yamakawa & Amano, whose trapezoid _fric_env is, put it
                # plainly -- the shape scales and only the total length
                # distinguishes a fricative from an affricate.
                #
                # So the trapezoid is split across the two morae instead: this
                # one raises the noise and holds it, the next holds it and
                # lets it go.  No duration changes; only what fills the slot.
                nv_ = M.split(nxt)[1]
                qvoiced = J.MANNER.get(nc, ('none', 1, 0, 0))[1]
                if nc == 'f' and PHI:
                    peak = J.PHI[J.phi_mode(nv_, '', False)][0]
                    qsp = J.phi_post(nv_)
                    with _noise_trim(aspir='f'):
                        for g in _fric_env(nq, peak, fall_frac=0.0):
                            out.append(frame(qsp, {2: g, 0: 0}))
                else:
                    qsrc, qkey = _fric_noise(nc, nv_, qvoiced)
                    qsp = J.noise_post(nc, nv_) or J.onset(nc, nv_)
                    with _noise_trim(nc, aspir=nc):
                        for g in _fric_env(nq, qsrc[qkey], fall_frac=0.0):
                            fq = dict(qsrc)
                            fq[qkey] = g
                            out.append(frame(qsp, fq))
                q_fric = True
            else:
                out.extend(frame(prev_sp if not csp else csp, {0: 0})
                           for _ in range(nq))
            # The geminate closure is the length cue itself, so it is budgeted
            # but never spent against -- compensating here would eat the
            # contrast the closure exists to carry.
            sp_rec['nominal'] = len(out) - mora_start
            ends.append(len(out) / 100.0)
            continue
        if mo == 'N':                                  # the moraic nasal
            nxt = morae[n + 1] if n + 1 < len(morae) else ''
            nc = M.split(nxt)[0] if nxt else ''
            # Final before a pause as well as at the end of the utterance: a
            # phrase boundary is a pause, and the nasal closes into it.
            fin = FINAL_N and nxt in ('', ' ', '|', '||')
            place = J.moraic_n(nc, final=fin)
            sp = J.onset('m' if place == 'N_m' else 'N', 'a')
            if place == 'N_nas':
                # No closure, so the murmur is not a nasal-cavity resonance
                # with an F2 of its own: the oral tract is still open in the
                # vowel's shape.  The posture oral() gives for this place IS
                # the murmur -- F1 down, the vowel's F2 and F3 kept.  A closed
                # place keeps onset()'s measured N2 instead, 1000 Hz alveolar
                # and 1150 bilabial, which is what the ear has passed.
                nas = J.oral(place, prev_v)
                if nas:
                    sp = nas
            # The transition out of the preceding vowel takes the place the
            # nasal assimilates to, not always the alveolar one.  Where there
            # is no oral closure -- before a vowel, before /h/, utterance
            # finally -- there is no locus and the vowel keeps its own posture.
            csp = J.oral(place, prev_v) if place else None
            if csp:
                _offglide(out, prev_sp, csp,
                          _glide_len(prev_sp, csp, OFFGLIDE), steady_at)
            else:
                sp = (prev_sp[0], prev_sp[1], prev_sp[2], prev_sp[3],
                      prev_sp[4] + 80, prev_sp[5] + 80, prev_sp[6] + 80)
            steady_at = len(out)
            out.extend(frame(sp, J.NASAL_A) for _ in range(MORA_FRAMES))
            sp_rec['nominal'] = len(out) - mora_start
            ends.append(len(out) / 100.0)
            prev_sp = sp
            continue

        man, voiced, hold, vot = J.MANNER.get(c, ('none', 1, 0, 0))
        # Homma: VOT is far shorter word-medially than word-initially -- the
        # voiceless mean falls from 37 ms to 16.  The MANNER value is the
        # initial one; medial takes VOT_MEDIAL.
        if (HOMMA_VOT and man == 'stop' and c in J.VOT_MEDIAL
                and n > 0 and morae[n - 1] not in (' ', '|', '||')):
            vot = J.VOT_MEDIAL[c]
            hold = CLOSURE_MEDIAL[voiced]
        # Arai: "while phrase-initial /r/ is often produced as a plosive-like
        # sound".  Every /r/ here was the intervocalic flap wherever it fell,
        # including at the start of an utterance where there is no preceding
        # vowel for the flap to interrupt -- the dip had nothing to dip into
        # and the first half of its transition did not exist.  His allophone
        # list gives the retroflex plosive for this position, so it becomes a
        # short voiced stop, which the prevoicing above now fills.
        if (man == 'flap' and PLOSIVE_R
                and (n == 0 or morae[n - 1] in (' ', '|', '||'))):
            man, hold, vot = 'stop', 3, 2
        # A long vowel is one gesture of double length, so BOTH its morae take
        # the peripheral target, not just the held half -- which needs a look
        # at the next mora, since the ':' that marks it comes afterwards.
        longv = (n + 1 < len(morae) and morae[n + 1] == ':')
        vsp = J.vowel(v, c, long=longv) if v else prev_sp
        osp = J.onset(c, v) or vsp
        bridge = None
        if (BRIDGE_F3 and c and prev_v
                and (man in ('glide', 'flap')
                     or (man in ('fric', 'affric') and voiced))):
            # F3 and F4 are not place cues for a voiced continuant.  Nothing in
            # LOCUS specifies them -- that table is F2 only -- so onset() just
            # takes the FOLLOWING vowel's and coda() the PRECEDING one's.  For
            # a stop that is invisible, because the closure between them is
            # silent.  For a glide, a flap or a voiced fricative the frames are
            # voiced and continuous, so the disagreement lands as a step on the
            # consonant's first frame: /iwa/ moved F3 672 Hz in 10 ms, /uri/,
            # /iyu/ and /iryu/ 720, /izu/ 720.
            #
            # Which of the two vowels is right?  Neither: the consonant has no
            # F3 of its own to be right about.  The midpoint splits the move in
            # half and hands each half to the offglide and the transition,
            # which already exist and already interpolate all seven values.
            t0, t1 = J.V.get(prev_v, J.V['a']), J.V.get(v, J.V['a'])
            bridge = ((t0[2] + t1[2]) / 2.0, (t0[3] + t1[3]) / 2.0)
            osp = (osp[0], osp[1], bridge[0], bridge[1], osp[4], osp[5], osp[6])
        # Where the transition into the vowel STARTS.  For every other manner
        # that is the consonant's own posture, but a nasal's posture is its
        # MURMUR -- a nasal-cavity resonance measured with the oral tract shut
        # -- and the release is an oral event.  The step from the murmur to the
        # oral locus is the release itself and belongs there.
        tsp = (J.oral(c, v) or osp) if man == 'nasal' else osp
        # The burst posture, settled once so the closure, the burst and the
        # transition out of it all use the same one.
        bsp = (J.burst(c, v) or osp) if man == 'stop' else osp
        if man == 'stop' and voiced:
            # Kochetov: a voiced stop makes less contact than its voiceless
            # partner, so its release is weaker and its cavity resonance less
            # sharply defined.  Pulling the posture halfway to the locus says
            # that, and it halves a step nothing can mask -- a voiced stop is
            # voiced throughout, where a voiceless one has a silent closure to
            # hide behind.  For /d/ the raw posture is the 4000/4080 ceiling
            # that only exists to place noise, so it is dropped entirely.
            bsp = osp if c in J.BURST_SRC else _lerp(bsp, osp, 0.5)
            # A voiced stop has one frame of VOT -- Homma's medial figure -- so
            # there is no room between the burst and the vowel for the formants
            # to travel, and starting the glide at the locus left a step.
            # Starting it AT the burst posture makes the path continuous.
            tsp = bsp
        if NASALIZE and n in nasal_v1:
            # A vowel nasalized by its own nasal consonant, which is what makes
            # the contrast with the un-nasalized vowel across the boundary.
            # Shows as "relatively lower high frequency energy", so it is the
            # bandwidths that widen.
            vsp = (vsp[0], vsp[1], vsp[2], vsp[3],
                   vsp[4] + NASAL_BW, vsp[5] + NASAL_BW, vsp[6] + NASAL_BW)
        if c:
            # Kashino: the VC transition carries the consonant's place, so it
            # aims at a locus keyed on the vowel BEFORE it, not the one after.
            csp = J.coda(c, prev_v) or osp
            if bridge:                    # the offglide aims at the same F3/F4
                csp = (csp[0], csp[1], bridge[0], bridge[1],
                       csp[4], csp[5], csp[6])
            if SMOOTH_VC:
                # coda() keys F2 on the PRECEDING vowel and onset() on the
                # following one, so the two disagree and the difference lands
                # as a step on the consonant's first frame.  For a stop that
                # disagreement is right and is Kashino's whole point: there IS
                # a closure between them, the articulator really does travel
                # through it, and the burst at the far end is keyed to the
                # vowel after.  A glide, a flap or a fricative has no closure
                # to travel through -- it is one gesture with one posture --
                # so for those the vowel should simply aim at the posture the
                # consonant actually holds.  /iwa/ aimed at 1013 and held 887.
                if man in ('glide', 'flap', 'fric', 'affric'):
                    csp = (csp[0], osp[1], csp[2], csp[3],
                           csp[4], csp[5], csp[6])
                # F1 is not a place cue; it tracks how open the tract is, and
                # a voiced closure is shut.  The closure frames force F1 to
                # 200 through voice_bar(), but coda() was aiming the vowel at
                # 480, so F1 glided down to ~540 and then dropped 344 Hz in one
                # frame as the voice bar took over.  Aim at where it lands.
                if PREVOICE and man == 'stop' and voiced:
                    csp = (200, csp[1], csp[2], csp[3],
                           csp[4], csp[5], csp[6])
            # Arai measures Fg as the WHOLE transition, in and out, so half of
            # it belongs to the preceding vowel.  Only a floor: a bigger vowel
            # move still gets the frames _glide_len says it needs.
            og = _glide_len(prev_sp, csp, OFFGLIDE)
            if man == 'flap':
                og = max(og, J.FLAP['Fg'] // 2)
            _offglide(out, prev_sp, csp, og, steady_at)

        # High vowels devoice between voiceless consonants, or finally after
        # one -- desu as [des].  njd_set_unvoiced_vowel is the same rule.
        # A following BARE VOWEL is not one of those environments, and this
        # devoiced before one: M.split('a') is ('', 'a'), so the `nxt_c == ''`
        # arm that used to be here fired on every vowel-initial mora.  It was
        # presumably meant to catch a phrase boundary, but M.split(' ') is
        # (' ', ''), so boundaries never reached it and bare vowels always did
        # -- /fuan/ devoiced its /u/, and so did /shiai/ and /suugaku/ their
        # high vowels.  Boundaries are now listed explicitly, which also makes
        # a phrase-final high vowel devoice, as "finally after one" intends.
        # PHRASE-FINALLY the rule is not "devoice any eligible vowel".
        # Upstream's own apply_unvoice_rule opens with `if nxt is None:
        # return 0`, so with nothing following, only rule 1 (/masu/, /desu/)
        # and rule 2 (/shi/, which needs a part of speech) devoice at all.
        # Den & Koiso measure the same shape in 107 spontaneous narratives --
        # masu 81.33%, desu 79.54% against /shi/ 1.81% and /ku/ 1.66% -- so
        # devoicing every eligible final vowel was wrong twice over, and it
        # made romaji `sushi` and the kanji 寿司 two different words.
        # The MEDIAL arm below is the ordinary environment and is unchanged.
        # MEDIALLY it is upstream's rule 5: the mora's candidate class
        # decides, and each class has its own list of morae it devoices
        # before.  Testing the next ONSET for voicelessness instead -- which
        # is what this did -- devoices /su/ before /shi/, where upstream's
        # list for that class has no s-row in it at all, so romaji `sushi`
        # came out with a whispered first mora and 寿司 did not.
        nxt_m = morae[n + 1] if n + 1 < len(morae) else None
        if nxt_m is None or nxt_m in (' ', '|', '||'):
            devoiced = _polite_su(morae, n, question)
        else:
            _cls = _DV_CLASS.get(morae[n], 0)
            devoiced = _cls > 0 and nxt_m in DV.NEXT[_cls - 1]
        # The rule above is a generalisation over the mora string, and it is
        # measurably coarser than the lexicon: njd_set_unvoiced_vowel blocks
        # two adjacent devoiced morae and protects the one carrying the accent
        # nucleus, and naist-jdic marks devoicing outright for the words it
        # knows.  Of the 179,679 morae this devoices over all 486,646
        # pronounced naist-jdic entries, 11.2% are the first of an adjacent
        # pair and 11.3% are accent nuclei.  So when
        # the caller has been through the analyser it passes the answer in and
        # this is not consulted; jp_front.say is that caller.  Without it --
        # romaji input, or the oracle -- this rule decides, which is why its
        # phrase-final arm is conditioned above rather than firing on every
        # eligible vowel.
        if devoiced_in is not None and n < len(devoiced_in):
            devoiced = bool(devoiced_in[n])

        # These COMPOSE rather than one winning.  A vowel can be both
        # phrase-initial, which shortens it, and pre-geminate, which lengthens
        # it -- `ni itte` is exactly that -- and both effects are measured, so
        # neither has a claim to override the other.
        qv = 1.0
        if n in before_q:
            qv *= Q_V1_LONGER
        if n in after_q:
            qv *= Q_V2_SHORTER
        qv *= same_v1.get(n, 1.0) * same_v2.get(n, 1.0)
        amp = (60 + Q_INTENSITY if n in before_q else
               60 - Q_INTENSITY if n in after_q else 60)
        ntr = _glide_len(tsp, vsp, TRANS) if c else 0
        if (HIATUS and not c and n > 0
                and morae[n - 1] not in (' ', '|', '||', 'Q')):
            # A vowel-initial mora had NO transition at all, because the
            # transition loop keyed on there being a consonant.  Where the
            # mora before it also ended voiced -- another vowel, a long vowel,
            # or the moraic nasal -- that is simply wrong: nothing interrupts
            # the voicing, so the tongue travels continuously from one target
            # to the next and there is no boundary for a step to hide at.
            # `aoi` stepped F2 1228 -> 860 -> 2068, 1208 Hz in a single frame,
            # and `shiai`, `kaeru`, `iu` and the rest did the same.  Japanese
            # is full of these: aoi, ie, ue, iu, ai, kaeru, shiai, ookii.
            #
            # /N/ before a vowel is the same case for a different reason --
            # utterance-medial /N/ there is a nasalised vowel or uvular, not an
            # oral closure that releases, so again there is nothing to break.
            # Only a pause or a geminate's silence licenses a step.
            tsp = prev_sp
            ntr = _glide_len(tsp, vsp, TRANS)
        if man == 'flap':
            ntr = max(ntr, J.FLAP['Fg'] - J.FLAP['Fg'] // 2)
        # When the vowel devoices there is no voiced part left, so the frication
        # fills the mora instead.  With the base now 10 frames, 4 more make the
        # mora exactly: neither paper measured a devoiced vowel -- both chose
        # undevoiced ones -- so this is sized to the mora, not to a measurement.
        grown = (FRIC_GROWN if (devoiced and man == 'fric') else
                 2 if (devoiced and man == 'affric') else 0)
        # A mora is measured vowel-onset to vowel-onset, so a *silent* closure
        # belongs to the boundary rather than to the mora that follows it:
        # counting it here made every stop-initial mora 50 ms too long and held
        # the whole utterance to 6.2 morae/s against the papers' 7.  Frication
        # and nasal murmur do sound, so those stay inside the budget.
        # A mora has to hold the whole consonant, not have it added on top.
        # Idemaru's singleton closure of 69 ms plus ~30 ms of VOT plus a short
        # vowel is about one 143 ms mora, which is what the rate works out to;
        # budgeting the closure separately left every stop-initial mora 50 ms
        # long and the utterance at 6.2 morae/s against the papers' 7.
        # The vowel asks for its own duration instead of taking whatever the
        # mora has left over.  That residual is why every vowel came out the
        # same length: nothing in the chain knew which vowel it was.
        # `want` is the whole voiced vowel; the transition is already part of
        # it, so the steady part is what remains after the glide is paid for.
        want = vowel_frames(v) if v else VOWEL_FLOOR + TRANS
        if VOWEL_ALLOC and n in VOWEL_ALLOC:
            want = VOWEL_ALLOC[n]            # explicit, this mora only
        need = hold + vot + grown + want
        total = max(MORA_FRAMES, need)
        nv = max(MIN_VOWEL, want - ntr)
        # the paper measures the whole vowel, so the factor scales the glide
        # and the steady part together, not the steady part alone
        nv = max(1, int(round((nv + ntr) * qv)) - ntr)

        # Set by a geminate that has already raised this consonant's noise,
        # so the fricative below holds it rather than ramping up again.
        from_q = q_fric
        q_fric = False

        if man == 'stop':
            # The closure takes the locus posture (that is where the formants
            # were heading as the vowel before it ended); the burst takes its
            # own, which is a cavity resonance and a different thing.
            _taper(out, 3, steady_at, voiced)
            # Prevoicing: a voiced stop's closure carries a voice bar, where a
            # voiceless one is silent.  Both were silent here.
            if voiced and PREVOICE:
                # The bar takes the CODA posture, not the onset's.  During the
                # closure the tongue is where it was when it closed, so the
                # posture has to be continuous with the vowel that just ended
                # -- Kashino's point that the VC transition is keyed on the
                # preceding vowel.  Taking the onset posture, which is keyed on
                # the FOLLOWING vowel, made the bar jump: `age` moved F2 624 Hz
                # on a voiced frame, from the coda's 1484 to the onset's 2108.
                # It is a voiced frame, so nothing masks it.
                # and it GLIDES across the closure rather than sitting still:
                # the tongue travels from where it closed to where it releases,
                # and with every frame voiced a step anywhere in here is heard.
                base = csp if c else osp
                for i in range(hold):
                    out.append(frame(J.voice_bar(
                        _lerp(base, bsp, (i + 1) / float(hold + 1))),
                        J.VOICE_BAR))
            else:
                out.extend(frame(osp, {0: 0}) for _ in range(hold))
            # The high alveolar source belongs to the BURST ALONE.  Kitazawa &
            # Doshita measure the burst spectrum, which Morikawa takes over the
            # first 12.8 ms; the aspiration that follows is glottal noise shaped
            # by the tract, a different thing.  Putting the sibilant source on
            # every VOT frame gave /ta/ 30 ms of /s/-type noise and no
            # aspiration at all -- 43% of the frication of the affricate /ts/,
            # at a similar level -- so /ta/ was heard as [tsa].  That is the
            # stray phoneme, and the earlier fix missed it entirely by looking
            # only at formant steps between voiced frames.
            after = J.ASPIR if not voiced else {0: 40}
            src = J.BURST_SRC.get(c) or after
            # The floor here was 2 frames, which silently overrode any VOT
            # below 20 ms -- so Homma's medial /p/ at 7 ms came out 20, the
            # same as /t/ and /k/, and the position effect was invisible.
            # One frame is still an audible burst.
            # and it lasts ONE frame, which is about the 12.8 ms the burst
            # spectrum is measured over.
            nb = 1 if c in J.BURST_SRC else max(1, vot // 2)
            out.extend(frame(bsp, src) for _ in range(nb))
            # then the formants leave the burst for the locus.
            #
            # Except where the burst posture is not a vocal-tract state.  /t/
            # and /d/ have their F3 and F4 pinned at the 4000/4080 ceiling
            # purely to place the 5.3 kHz band-6 noise Kitazawa & Doshita
            # measured -- it is a noise-shaping device, exactly like SIB_POST,
            # and gliding voiced frames out of it is the same mistake as
            # starting a transition from a fricative's frication F2 or a
            # nasal's murmur N2.  Measured on `ata`, F3 ran
            # 2272 -> 4000 -> 3136 -> 2272, a 1728 Hz excursion over 20 ms,
            # which is heard as an extra segment between the /t/ and the vowel.
            # For those the post-burst frames go straight to the locus; the
            # step from the burst to it lands on a noise frame, where it is
            # masked, as the nasal release step is.
            rest = vot - nb
            for i in range(max(0, rest)):
                if c in J.BURST_SRC:
                    out.append(frame(osp, after))
                else:
                    out.append(frame(_lerp(bsp, osp,
                                           (i + 1) / float(rest + 1)), src))
        elif man == 'affric':
            nsp = J.noise_post(c, v) or osp
            if bridge:            # the bridge has to reach these too
                nsp = (nsp[0], nsp[1], bridge[0], nsp[3],
                       nsp[4], nsp[5], nsp[6])
            _taper(out, 3, steady_at, voiced)
            out.extend(frame(osp, {0: 0}) for _ in range(hold))
            sib = dict(J.SIBIL_Z if (voiced and J.VOICED_SIB) else J.SIBIL)
            if voiced and not J.VOICED_SIB:
                sib[0] = 40
            nf = vot + grown       # same count the budget reserved
            with _noise_trim(c):
                for g in _fric_env(nf, sib[8]):
                    f8 = dict(sib)
                    f8[8] = g
                    out.append(frame(nsp, f8))
        elif man == 'fric' and c == 'f' and PHI:
            # Ruddell: /f/ is not one sound but four, and which one appears is
            # decided by what follows it.  See jp_voice.PHI.
            nx = morae[n + 1] if n + 1 < len(morae) else ''
            if nx in ('', ' ', '|', '||', ':'):
                nc = ''                       # a boundary, or a long /fu:/
            elif nx in ('N', 'Q'):
                nc = 'N' if nx == 'N' else ''
            else:
                nc = M.split(nx)[0]
            mode = J.phi_mode(v, nc, devoiced)
            peak, b2_on, b2_hold, b3 = J.PHI[mode]
            q = J.phi_post(v)
            nf = hold + grown      # same count the budget reserved
            # after a geminate the lips have already arrived and the noise is
            # already up, so there is no second ramp
            nr = 0 if from_q else max(1, int(nf * 0.40 + 0.5))
            _ft = _noise_trim(aspir='f'); _ft.__enter__()
            for i, g in enumerate(_fric_env(nf, peak,
                                            rise_frac=0.0 if from_q else 0.40)):
                # The lips arrive as the noise reaches level: it is one
                # gesture, so the band sharpens on the same ramp the amplitude
                # opens on.  F2 is held still throughout -- a moving F2 here
                # would be heard as a transition, which is the opposite of what
                # he describes.
                f = (i + 1) / float(nr) if nr and i < nr else 1.0
                b2 = b2_on + (b2_hold - b2_on) * f
                out.append(frame((q[0], q[1], q[2], q[3],
                                  q[4], int(b2 + 0.5), b3), {2: g, 0: 0}))
            _ft.__exit__()
        elif man == 'fric':
            src, key = _fric_noise(c, v, voiced)
            # the frication is shaped by the noise posture; the glide after it
            # starts from the articulatory locus, which is a different place
            nsp = J.noise_post(c, v) or osp
            if bridge:            # the bridge has to reach these too
                nsp = (nsp[0], nsp[1], bridge[0], nsp[3],
                       nsp[4], nsp[5], nsp[6])
            nf = hold + grown      # same count the budget reserved
            with _noise_trim(c, aspir=c):
                for g in _fric_env(nf, src[key],
                                   rise_frac=0.0 if from_q else 0.40):
                    fk = dict(src)
                    fk[key] = g
                    out.append(frame(nsp, fk))
        elif man == 'flap':
            # Arai's F2 track is a triangle: the contact is instantaneous and
            # there is no steady top, so the apex is a single frame.
            out.extend(frame(osp) for _ in range(hold))
            apex = len(out) - 1
        elif man in ('nasal', 'glide'):
            src = J.NASAL_A if man == 'nasal' else None
            rel = (NASAL_RELEASE and man == 'nasal' and hold > 1
                   and tsp is not osp)
            out.extend(frame(osp, src) for _ in range(hold - 1 if rel else hold))
            if rel:
                # The release.  The murmur's resonances belong to the nasal
                # cavity and the vowel's to the oral tract, so one does not
                # glide into the other -- they swap, and that step is real.
                # What keeps it from being heard as a click in speech is that
                # it happens while the mouth is still nearly shut and the
                # sound is weak.  So the step is placed at murmur amplitude,
                # one frame before the amplitude recovers, instead of landing
                # on the same instant as it.  Taken out of the murmur's own
                # frames so the mora keeps its length.
                # At the murmur's own level, so the frame carrying the formant
                # step carries no amplitude step and vice versa: the two
                # discontinuities land on different frames instead of stacking.
                out.append(frame(tsp, J.NASAL_A))

        _dv = _noise_trim(aspir=('V' + v) if (devoiced and v) else None)
        _dv.__enter__()
        for i in range(ntr):        # `ntr` is 0 unless a transition is wanted,
            out.append(frame(_lerp(tsp, vsp, (i + 1) / float(ntr + 1)),
                             J.DEVOICED if devoiced else None))
        if man == 'flap':
            _dip(out, apex, J.FLAP['AVg'], J.FLAP['AVd'])
        steady_at = len(out)
        vsrc = J.DEVOICED if devoiced else ({0: amp} if amp != 60 else None)
        if n in after_q:
            # recorded relative to the mora, because the redistribution below
            # moves every absolute index
            q_ends.append((len(spans) - 1, len(out) - ntr - mora_start))
        out.extend(frame(vsp, vsrc) for _ in range(max(nv, 1)))
        _dv.__exit__()
        # The budget shifts with the vowel too, or compensation undoes the
        # whole thing: with nominal fixed at MORA_FRAMES, a mora holding a
        # short /i/ underruns it and _compensate inflates the vowel back
        # proportionally, flattening exactly the distinction being drawn.
        # Homma's rule is preserved -- the budget is still fixed before the
        # fact, and an expensive consonant is still paid for by the word's
        # vowels -- it is just no longer the same number for every vowel.
        sp_rec.update(steady=steady_at, len=max(nv, 1),
                      adjustable=True, steady_frame=list(out[steady_at]),
                      want=want, ntr=ntr,
                      requested_ms=(V_DUR.get(v, (0, 0))[0] if v else 0.0),
                      nominal=MORA_FRAMES + vowel_frames(v) - V_DEFAULT,
                      head=GLOTTAL_FRAMES if (GLOTTAL and n in glottal) else 0)
        if GLOTTAL and n in glottal:
            # Creak is quieter as well as lower.  The F0 half of it is applied
            # in pitch(), which owns track 17; this is the amplitude half, at
            # the very start of the phrase-initial vowel.
            _dip(out, steady_at + GLOTTAL_FRAMES // 2, GLOTTAL_FRAMES,
                 GLOTTAL_DIP)
        ends.append(len(out) / 100.0)
        prev_sp = vsp
        prev_v = v or prev_v

    # Preserve pre-compensation coordinates explicitly. Published legacy fields
    # below describe the returned frame array, never a mixture of timelines.
    for s in spans:
        s.update(original_start=s['start'], original_steady=s['steady'],
                 original_len=s['len'], give=0)
    if WORD_BUDGET:
        out, ends, q_ends = _compensate(out, spans, q_ends)
    else:
        q_ends = [(spans[i]['start'] + r) / 100.0 for i, r in q_ends]
    for i, s in enumerate(spans):
        s['len'] = s['original_len'] + s.get('give', 0)
        s.update(final_start=s['start'], final_steady=s['steady'],
                 final_len=s['len'], final_end=int(round(ends[i] * 100)))
    # Diagnostic hook. These are control allocations, not acoustic boundaries.
    # Durations have been measured three different indirect
    # ways in this file -- longest constant run, trailing constant run, frames
    # at the vowel target -- and each one was wrong for a different reason: the
    # nasal murmur is itself a constant run, the next mora's off-glide eats the
    # tail, and the taper drops the amplitude below any gate.
    global LAST_SPANS
    LAST_SPANS = spans
    _final_tail(out)
    return out, ends, q_ends


LAST_SPANS = []


#: Frames of amplitude ramp at the very end of an utterance.  The engine's own
#: output ends at zero -- English and Spanish both measure 0 for the last
#: sample -- because their frames decay; these stopped dead, so the last sample
#: sat wherever the waveform was, up to 40% of the loudest one in the word, and
#: the step to silence was heard as a click.  Three frames is 30 ms, which is
#: the 320-sample fade jp_speak.render has always applied to its WAVs.
FINAL_FADE = 6
#: Decibels of fall per frame.  Tracks 0, 1 and 2 are about a decibel a unit,
#: so this is subtracted from them: a linear decibel slope, where scaling them
#: towards zero instead gives -20 dB a frame and is heard as a gate.  Spanish
#: falls about 5 dB a cell, which is what this matches.
FINAL_FADE_STEP = 5
#: Below this an utterance is too short to give six frames away to a ramp.
FINAL_FADE_MIN = 6
#: And silent frames after it, because the ramp alone changed nothing: the
#: engine renders the frames it is given and stops, so the filter is still
#: ringing when the samples run out.  Five frames is where the last sample
#: reaches exactly zero for every word measured; with the ramp but no padding
#: it sat at up to 58% of the loudest sample in the word, and that step is the
#: click.  It is 50 ms of silence the listener never waits for -- it is the
#: tail of audio already playing, not latency before the next utterance.
FINAL_PAD = 6


def _final_tail(out):
    """Ramp the amplitude down, then let the filter ring out into silence.

    Tracks 0, 1 and 2 are voicing, frication and aspiration -- the same three
    the volume attenuation works on, because they are the three that carry
    level.  The ramp is linear in track units, which are about a decibel each,
    so it is heard as a decay rather than a cut.  The padding is all zeros;
    holding the last posture instead measures identically, because the engine
    reads no formant from a frame with no amplitude in it.
    """
    n = len(out)
    if not n:
        return
    if FINAL_FADE and n >= FINAL_FADE_MIN:
        # At most half the utterance, so a one-mora word still has a word in
        # it; the ramp is a tail, not the whole thing.
        k = min(FINAL_FADE, max(1, n // 2))
        for i in range(k):
            fr = out[n - k + i]
            sub = FINAL_FADE_STEP * (i + 1)
            for t in (0, 1, 2):
                fr[t] = max(0, fr[t] - sub)
    ntrack = len(out[-1])
    for _ in range(FINAL_PAD):
        out.append([0] * ntrack)


def _compensate(out, spans, q_rel):
    """Homma 1981: temporal compensation works within a WORD, not a mora.

    Homma reports /papa/ at 260 ms against /gaga/ at 267, despite a 37-ms
    difference in their first syllables. This motivates sharing a nominal
    budget across morae, but does not establish exact isochrony.
    Redistribution here remains a modelling approximation.

    The frontend supplies accent phrases, not lexical-word boundaries. Keep
    redistribution within each continuous stretch between explicit pauses;
    a later phrase must not change an earlier phrase's segment allocations.

    Only unchanged steady vowel is adjustable. Offglides and tapers have
    already been written into its tail. Protect those frames, glottal heads,
    and the explicit long-vowel increment, just as we protect geminate closure.
    A budget that cannot fit these cues is allowed to overrun.
    """
    if NO_COMPENSATE:
        q = [(spans[i]['start'] + r) / 100.0 for i, r in q_rel]
        return out, _ends(spans, out), q

    first = 0
    for last in range(len(spans) + 1):
        if last < len(spans) and not spans[last]['budget_break']:
            continue
        group = spans[first:last]
        if group:
            end = spans[last]['start'] if last < len(spans) else len(out)
            delta = sum(s['nominal'] for s in group) - (end - group[0]['start'])
            adj = [s for s in group if s['adjustable']]
            cap = []
            for s in adj:
                at = s['steady'] + s['head']
                stable = 0
                for f in out[at:s['steady'] + s['len']]:
                    if f != s['steady_frame']:
                        break
                    stable += 1
                cap.append(max(0, stable - (MIN_VOWEL if delta < 0 else 0)))
            tot = sum(cap)
            if tot and delta:
                want = max(delta, -tot)
                give = [int(round(want * (c / float(tot)))) for c in cap]
                err = want - sum(give)
                # Preserve the exact feasible budget despite frame rounding.
                while err:
                    eligible = [i for i, c in enumerate(cap) if c and
                                (give[i] > (-c if want < 0 else 0) if err < 0 else
                                 give[i] < 0 if want < 0 else True)]
                    j = max(eligible, key=lambda i: cap[i])
                    step = 1 if err > 0 else -1
                    give[j] += step
                    err -= step
                for s, g in zip(adj, give):
                    s['give'] = g
        first = last + 1

    new, ends, pos = [], [], {}
    for idx, s in enumerate(spans):
        seg = out[s['start']:s['start'] + _span_len(spans, out, idx)]
        g = s.get('give', 0)
        if g and s['steady'] is not None:
            at = s['steady'] - s['start'] + s['head']
            if g > 0:
                seg = seg[:at] + [list(seg[at]) for _ in range(g)] + seg[at:]
            else:
                seg = seg[:at] + seg[at - g:]
        pos[idx] = len(new)
        new.extend(seg)
        ends.append(len(new) / 100.0)
    # Write the new positions BACK into the spans.  This did not happen, so
    # every caller reading LAST_SPANS[i]['start'] after compensation got the
    # PRE-compensation index -- 1 to 2 frames early, which is 10-20 ms.  The
    # comment where LAST_SPANS is published calls the span record "ground
    # truth"; it was not, for exactly the callers most likely to trust it.
    # Found by Astra while auditing the region bookkeeping.
    for idx, s in enumerate(spans):
        if s['steady'] is not None:
            s['steady'] = pos[idx] + (s['steady'] - s['start'])
        s['start'] = pos[idx]
    q = []
    for i, r in q_rel:
        s = spans[i]
        off = r + (s.get('give', 0) if r >= (s['steady'] - s['start'] +
                                             s['head']) else 0)
        q.append((pos[i] + off) / 100.0)
    return new, ends, q


def _span_len(spans, out, i):
    return (spans[i + 1]['start'] if i + 1 < len(spans)
            else len(out)) - spans[i]['start']


def _ends(spans, out):
    return [(spans[i]['start'] + _span_len(spans, out, i)) / 100.0
            for i in range(len(spans))]

def _phrases(morae, ends):
    """-> [(major_index, [(start, end) per mora])], one entry per accent phrase.

    Morae that are boundary marks carry no time of their own; they split the
    list.  A bar also increments the major index, which is what resets both the
    phrase command and downstep."""
    groups, cur, major = [], [], 0
    starts = [0.0] + list(ends[:-1])
    for i, mo in enumerate(morae):
        if mo in (' ', '|', '||'):
            if cur:
                groups.append((major, cur))
            cur = []
            if mo != ' ':
                major += 1
            continue
        cur.append((starts[i], ends[i]))
    if cur:
        groups.append((major, cur))
    return groups


def _sandhi(acc, emph):
    """Kawai section 7.1, accent sandhi: which of the six accent symbols each
    prosodic word takes, from the sequence of accent types and the emphasis
    specification.  D is an accented word, F a heiban one.

    Only the first D takes the full amplitude.  His worked example 7 settles
    it beyond the OCR -- six words, accent types D D D D D D with -Emph on the
    fourth and sixth, giving AH AM AM AL AM AL -- so:

      (3) the FIRST D of the sequence takes AH, or AM if it is word 1 and
          -Emph
      (4) every later D takes AH at +Emph, AM at 0Emph, AL at -Emph
      (1a)(2a) a heiban word 1 takes FM, or FL at -Emph
      (5) a later F takes FM, or FL at -Emph or when what follows it is weak

    +Emph also demotes every following 0Emph to -Emph, which is how Kawai gets
    a focused word to stand out: the focus is not raised so much as everything
    after it is lowered.
    """
    e = list(emph) + [0] * (len(acc) - len(emph))
    for i, v in enumerate(e):                      # +Emph suppresses what follows
        if v > 0:
            for j in range(i + 1, len(e)):
                if e[j] == 0:
                    e[j] = -1
    out, seen_d = [], False
    for i, a in enumerate(acc):
        if a != 0:
            if not seen_d:
                g = 'AM' if (i == 0 and e[i] < 0) else 'AH'
                seen_d = True
            else:
                g = 'AH' if e[i] > 0 else ('AL' if e[i] < 0 else 'AM')
        else:
            g = 'FL' if e[i] < 0 else ('FM' if i == 0 else 'FM')
        out.append(g)
    for i in range(len(out) - 1):                  # rule (5) lookahead
        if out[i] == 'FM' and i > 0 and out[i + 1] in ('AL', 'FL'):
            out[i] = 'FL'
    return out


def pitch(frames, ends, accent, Fb=72.0, q_ends=(), morae=None, final=True,
          emph=(), question=False):
    """Kawai section 7.2: the accent command rises before mora 1 if atamadaka
    and after it otherwise; it falls at the nucleus, or after the final mora
    when the word is heiban.

    `accent` is one accent type for a single phrase, or a list of them, one per
    accent phrase in order.  Each MAJOR phrase gets its own Fujisaki phrase
    command; within a major phrase the accent commands chain and are scaled by
    downstep.
    """
    if not ends:
        return [Fb]
    if morae is None or not any(m in (' ', '|', '||') for m in morae):
        groups = [(0, list(zip([0.0] + list(ends[:-1]), ends)))]
    else:
        groups = _phrases(morae, ends)
    if not groups:
        return [Fb]
    acc = list(accent) if isinstance(accent, (list, tuple)) else [accent]
    acc += [0] * (len(groups) - len(acc))
    # which boundary mark opened each major phrase; the first one opens nothing
    mark = [''] + [m for m in (morae or []) if m in ('|', '||')]
    grades = _sandhi(acc, emph)

    phrase_cmds, accent_cmds = [], []
    last_major, ds, since = None, 1.0, 0
    for gi, (major, ms) in enumerate(groups):
        a = acc[gi]
        if major != last_major:
            # Kawai: a phrase command is an impulse shortly BEFORE the phrase
            # it lifts, so the response has risen by the time voicing starts.
            # His rules grade the boundary: a sentence boundary takes P1, a
            # clause boundary P2, an ICRLB boundary P3.  So the start of the
            # utterance is P1, '||' is a clause and '|' the weaker ICRLB.
            # Giving every later major phrase P2 made the second one peak
            # ABOVE the first: at ALPHA = 2.0 the phrase response does not
            # peak until 0.5 s after its impulse, so a second command fired
            # half a second in lands on a first that has not begun to decay.
            ap = ('P1' if last_major is None else
                  'P2' if mark[major] == '||' else 'P3')
            T0 = ms[0][0] - 0.25
            # Kawai 5.2: the magnitude is whatever reaches the peak level an
            # isolated command of that nominal size would, so a command
            # landing on an undecayed residue is reduced rather than stacking.
            amp = FJ.phrase_amp(FJ.PHRASE[ap], T0, phrase_cmds)
            if amp > 1e-6:
                phrase_cmds.append((T0, amp))
                ds, since = 1.0, 0
        last_major = major
        T1 = ms[0][0] if a == 1 else ms[0][1]
        T2 = ms[-1][1] if a == 0 else ms[min(a, len(ms)) - 1][1]
        # Hirose, Sakata, Osame & Fujisaki section 3: "in the case of
        # declarative sentences, the accent command for the predicate phrase
        # usually takes a reduced amplitude toward the end of an utterance."
        # Kawai's symbol set already grades for this -- FM/AM against FH/AH --
        # so the last phrase of a multi-phrase declarative takes the M grade.
        # This, not an unbounded P0, is what brings a sentence down at the end:
        # Kawai clamps the phrase component at zero, so the final fall has to
        # come from the accent command, which is where they say it comes from.
        # Hirose's reduction is "in the case of declarative sentences", so a
        # question does not get it -- it rises at the end instead.
        last = (final and FINAL_PREDICATE and not question
                and gi == len(groups) - 1 and len(groups) > 1)
        if KAWAI_SANDHI and len(groups) > 1:
            g = grades[gi]
            if last and g[1] == 'H':       # the predicate is reduced one step
                g = g[0] + 'M'
            Aa = FJ.ACCENT[g]
        else:
            Aa = FJ.ACCENT[('F' if a == 0 else 'A') + ('M' if last else 'H')]
        accent_cmds.append((T1, T2, Aa * ds))
        # Kubozono 1987, ch. 5: downstep is ACCENT-induced -- a word is lowered
        # when it follows an ACCENTED word, not merely another word -- it
        # chains within the major phrase, and it is a shift of pitch RANGE, so
        # a downstepped phrase can still be phonetically higher than the one
        # that lowered it.  His Table 22 puts the inter-peak ratio at
        # 141.1/157.7 = 0.895 and finds it near constant across tonal
        # structures, which is the case for a fixed multiplier rather than one
        # that tracks the accentual fall.
        if a != 0:
            ds *= DOWNSTEP
        since += len(ms)
    # Kawai rule (1): a sentence boundary takes P1 and SP1, but at the END of
    # the text only P0 -- which is NEGATIVE, -0.50, and is the sentence-final
    # lowering.  Nothing was emitting it, so every utterance ended as high as
    # its last accent left it.
    # Hirose et al. section 4: "the command for the interrogative particle /ka/
    # is also assigned a large value (i.e., DH = 0.6), and its onset timing is
    # delayed by 70 msec as compared with the onset timing of the command for
    # the lexical accent."  0.6 is their DIALOGUE value -- they say the dialogue
    # rules raise every command, FH from 0.50 to 0.6 -- so a reader takes the
    # reading-style equivalent, Kawai's AH.  It is an EXTRA command on the final
    # mora, on top of the phrase's own lexical accent, which is why a question
    # rises where a statement falls.
    if question and groups:
        t0, t1 = groups[-1][1][-1]
        accent_cmds.append((t0 + KA_DELAY, t1, FJ.ACCENT['AH']))
    if final:
        phrase_cmds.append((groups[-1][1][-1][1] - 0.25, FJ.PHRASE['P0']))
    hz = FJ.contour(len(frames), Fb, phrase_cmds, accent_cmds)
    # Idemaru & Guion: F0 falls about 30 Hz further across a geminate than a
    # singleton, and this was their strongest secondary cue (~77% on its own).
    # Applied as a step down at the closure that recovers over ~0.3 s.
    for qe in q_ends:
        k0 = max(0, int(qe * 100))
        for k in range(k0, len(hz)):
            d = (k - k0) / 25.0
            if d > 1.0:
                break
            hz[k] -= Q_F0_FALL * (1.0 - d)
    # Kitazawa: the phrase-initial vowel of a V-V hiatus is glottalized, and the
    # F0 falls with the open quotient -- 235 -> 142 -> 178 Hz in their worked
    # example, a trough at 0.60 of the pre-boundary value recovering to 0.76.
    # Where the two vowels are the same there is no formant movement at all, so
    # this and the duration asymmetry are the only things marking the boundary.
    if GLOTTAL and morae:
        for i, mo in enumerate(morae):
            if mo != ' ' or i == 0 or i + 1 >= len(morae):
                continue
            if M.split(morae[i - 1])[1] == '' or M.split(morae[i + 1])[0]:
                continue
            if M.split(morae[i + 1])[1] not in 'aiueo':
                continue
            k0 = int(round(ends[i] * 100))
            base = hz[k0 - 1] if 0 < k0 <= len(hz) else None
            if base is None:
                continue
            # prominent where the following phrase is emphasised, which is what
            # he observed; and scaled by vowel, which is why a|a is subtle
            nxt = sum(1 for m in morae[:i + 1] if m in (' ', '|', '||'))
            e2 = list(emph) + [0] * (nxt + 1)
            lo, back = GLOTTAL_F0_EMPH if e2[nxt] > 0 else GLOTTAL_F0
            g = GLOTTAL_VOWEL.get(M.split(morae[i + 1])[1], 1.0)
            lo, back = 1.0 - (1.0 - lo) * g, 1.0 - (1.0 - back) * g
            for j in range(GLOTTAL_FRAMES):
                k = k0 + j
                if k >= len(hz):
                    break
                f = j / float(max(GLOTTAL_FRAMES - 1, 1))
                hz[k] = base * (lo + (back - lo) * f)
    for k, f in enumerate(frames):
        f[17] = _cl(_rnd(max(hz[k], 40.0) / 2.0), 1, 255)
    return hz

_dll = None
# Synth_ResetTracks (src/engine/synth.c) initializes 12 control frames before
# caller frames are appended. Raw PCM retains that prefix. This is a control
# timestamp mapping, not a claim about the acoustic boundary of a phoneme.
RAW_PREFIX_FRAMES = 12

def raw_frame_sample(frame_index):
    return (RAW_PREFIX_FRAMES + frame_index) * (SR // 100)

def _lib():
    global _dll
    if _dll is None:
        os.add_dll_directory(os.path.abspath("build/bin"))
        _dll = ctypes.CDLL(os.path.abspath("build/bin/tvtts64.dll"))
        _dll.tvtts_create_lang.restype = ctypes.c_void_p
    return _dll

class _EV(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int32), ("count", ctypes.c_uint32),
                ("samples", ctypes.POINTER(ctypes.c_int16)),
                ("mark", ctypes.c_uint32), ("sample_pos", ctypes.c_uint32)]
_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(_EV), ctypes.c_void_p)

def render(frames, postprocess=True):
    """Render PCM. Diagnostics disable trimming/fades to retain sample origin.

    A schedule is still only control timing, not an acoustic boundary label.
    The default preserves the existing listening/export behavior.
    """
    d = _lib()
    buf = bytes(bytearray(v for f in frames for v in f))
    arr = (ctypes.c_ubyte * len(buf)).from_buffer_copy(buf)
    s = d.tvtts_create_lang(SR, b"en")
    out = []
    def cb(ev, u):
        e = ev[0]
        if e.type == 0 and e.count:
            out.extend(e.samples[0:e.count])
        return 0
    c = _CB(cb)
    d.tvtts_speak_frames(ctypes.c_void_p(s), arr, len(frames), c, None)
    d.tvtts_destroy(ctypes.c_void_p(s))
    x = np.array(out, dtype=float)
    if not postprocess:
        return np.clip(x, -32768, 32767).astype(np.int16)
    nz = np.nonzero(np.abs(x) > 20)[0]
    if len(nz):
        x = x[max(0, nz[0] - 160): nz[-1] + 320]
    if len(x) > 500:
        x[:160] *= np.linspace(0, 1, 160)
        x[-320:] *= np.linspace(1, 0, 320)
    return np.clip(x, -32768, 32767).astype(np.int16)

def say(text, path, accent=0, emph=()):
    question = text.rstrip().endswith('?')
    morae = M.to_morae(text)
    frames, ends, q_ends = build(morae, emph=emph, question=question)
    hz = pitch(frames, ends, accent, q_ends=q_ends, morae=morae, emph=emph,
               question=question)
    x = render(frames)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(x.tobytes())
    return morae, len(frames), (min(hz), max(hz)), len(x) / float(SR)

if __name__ == '__main__':
    t = sys.argv[1] if len(sys.argv) > 1 else "konnichiwa"
    p = sys.argv[2] if len(sys.argv) > 2 else "out.wav"
    a = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    mo, nf, rng, dur = say(t, p, a)
    print("%-22s %-28s %2d morae  %3d frames  F0 %.0f-%.0f  %.2f s -> %s"
          % (t, " ".join(mo), len([m for m in mo if m != ' ']), nf,
             rng[0], rng[1], dur, os.path.basename(p)))
