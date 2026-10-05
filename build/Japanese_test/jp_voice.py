# -*- coding: utf-8 -*-
"""Japanese morae -> parameter frames for tvtts_speak_frames.

Sourcing, which differs by segment and is marked on every entry below:
  [M] measured -- Mokhtari & Tanaka 2000, the five vowels, F1-F4 and B1-B3
  [T] measured -- Tanaka ICPhS 2023, F2 at release and centre of gravity, by
      following vowel, for /k p s h/ and their palatalised forms
  [L] from the general literature -- nasal murmur formants, stop VOT
  [E] estimated from place of articulation; no source found
"""
V = {  # [M]
 'a':(737,1225,2275,3304,170, 99,180), 'i':(298,2067,2951,3455, 61,108,126),
 'u':(356,1293,2224,3282, 52,111,106), 'e':(481,1873,2406,3381, 54, 88,222),
 'o':(456, 856,2343,3246, 57,101,107)}
# /u/ fronts after alveolars and palatals: F2 1323 against 1081.  [M]
U_FRONT, U_BACK = 1323, 1081
_FRONTING = set(['s','sh','ts','z','j','y','ky','gy','ch','ny','ry'])

LOCUS = {                                                        # F2 at release
 'k' : {'a':1530,'i':2280,'u':1500,'e':2150,'o':1350},           # [T]
 'ky': {'a':2200,'i':2280,'u':2300,'e':2280,'o':2220},           # [T]
 # Kochetov finds the velars differ only "slightly" -- closure width higher for
 # /k/ than /g/ but both under one row -- so /g/ moves 15% of the way toward
 # the vowel where /d/ moves 50%.  No change for /b/: a labial makes no
 # tongue-palate contact, so his method has nothing to say about it.
 'g' : {'a':1484,'i':2248,'u':1469,'e':2108,'o':1276},           # [K] + [T]
 'gy': {'a':2200,'i':2280,'u':2300,'e':2280,'o':2220},           # [T] via /ky/
 # Corrected from Tanaka's 1600/2250/1980/1930/1780.  Fitting a locus equation
 # to that row -- attained = V + k(L - V), regressing attained on the vowel's
 # own F2 -- gives a clean fit, k = 0.64, but a locus of L = 2157 Hz, which is
 # a palatal place.  A bilabial locus is about 720.  As a locus the row cannot
 # be right: it sits ABOVE the vowel for every vowel but /i/, so /po/ would
 # leave a bilabial by falling 924 Hz into /o/, where labials raise F2 and
 # barely move.  Whatever his number measures, using it to START A VOICED
 # TRANSITION is the error -- the same one as taking a fricative's frication
 # F2 for its locus, or a nasal's murmur N2 for its own.  Rebuilt at the
 # classic labial locus with the k his own row fits, so only the PLACE moves.
 'p' : {'a':921,'i':1224,'u':946,'e':1154,'o':788},              # [L] + [T] k
 'py': {'a':2080,'i':2250,'u':2110,'e':2250,'o':2150},           # [T]
 'b' : {'a':921,'i':1224,'u':946,'e':1154,'o':788},              # via /p/
 'by': {'a':2080,'i':2250,'u':2110,'e':2250,'o':2150},           # [T] via /py/
 's' : {'a':2100,'i':2450,'u':2120,'e':2140,'o':2130},           # [T]
 'sh': {'a':2420,'i':2450,'u':2580,'e':2450,'o':2520},           # [T] /sj/
 'z' : {'a':2100,'i':2450,'u':2120,'e':2140,'o':2130},           # [T] via /s/
 'j' : {'a':2420,'i':2450,'u':2580,'e':2450,'o':2520},           # [T] via /sj/
 'ch': {'a':2420,'i':2450,'u':2580,'e':2450,'o':2520},           # [T] via /sj/
 'ts': {'a':1800,'i':2000,'u':1800,'e':1800,'o':1700},           # [E] alveolar
 'h' : {'a':1680,'i':2380,'u':1950,'e':2120,'o':1650},           # [T]
 'hy': {'a':2200,'i':2380,'u':2180,'e':2380,'o':2300},           # [T] /hj/
 # [P] is bilabial, so the VOICED transition out of it leaves from where /p/
 # and /b/ leave: the labial row fitted above, not the estimate that was here.
 # Ruddell (2011) measures a prominent band at 1.7-1.8 kHz running through the
 # frication of /fu/ (his Figs. 7, 9 and 12, all three read off the same axis),
 # and it is tempting to put that number in this table.  It does not belong
 # here.  That band is measured IN THE NOISE; a bilabial locus is about 750, so
 # seeding the vowel from 1700 would start it ~750 Hz above where a labial
 # actually leaves it -- the fricative-F2-as-locus error, which is the same one
 # already corrected for /h/, for /s/, and for the nasal murmurs.  The band is
 # real and worth rendering, so it goes in SIB_POST, where noise postures live.
 'f' : {'a':921,'i':1224,'u':946,'e':1154,'o':788},              # via /p/
 # Alveolar stops, as ATTAINED values rather than the virtual locus.
 # Tanaka's figures for /k/ and /p/ are F2 measured *at release*, so they are
 # values the formants actually reach; 1800 for /t/ was locus theory's target,
 # which the formants only travel partway toward.  Mixing the two made the /a/
 # off-glide move 575 Hz where Yanagisawa & Arai's XKL stimulus -- which gave a
 # strong perceptual effect -- moves it 150, from 1250 to 1400.  Anchored on
 # their 1400 for /a/ and pulled toward each vowel from the ~1800 locus.
 't' : {'a':1400,'i':1900,'u':1450,'e':1750,'o':1250},           # [Y] + [E]
 # /d/ does NOT share /t/'s row.  Kochetov (2014), 64-electrode EPG, five
 # speakers: "the alveolar closure was on average wider for /t/ than for /d/
 # (more than 2 rows vs. less than 1 row)", and before high vowels it "was
 # often absent altogether for /d/", one speaker lacking a complete closure at
 # all, which he reads as lenition.  A constriction less than half as long
 # cannot pull the formants as far, so /d/'s attained values sit halfway
 # between /t/'s and the vowel's own F2.  [K] + [Y]
 'd' : {'a':1313,'i':1984,'u':1372,'e':1812,'o':1053},
 'r' : {'a':1500,'i':1939,'u':1535,'e':1838,'o':1307},           # [A] set_flap()
 'n' : {'a':1000,'i':1100,'u':1000,'e':1000,'o': 950},           # [L] N2 ~1000
 'ny': {'a':1300,'i':1400,'u':1350,'e':1350,'o':1300},           # [E] palatal
 'm' : {'a':1150,'i':1250,'u':1100,'e':1150,'o':1050},           # [L] N2 1000-1300
 'my': {'a':1400,'i':1450,'u':1400,'e':1400,'o':1350},           # [E]
 # Kariyasu (2003), J. Kyushu Univ. of Health and Welfare 4:275-281, eleven
 # speakers, /awa/ and /aja/ in syllable repetition and in sentences.  He gives
 # the rate of formant movement per glide side, and two of his numbers settle
 # this row without needing any female-to-male scaling, because they are a
 # RATIO within one speaker set:
 #
 #     F1 rate   4.49 kHz/s, and "fairly invariant for /w/ and /j/"
 #     F2 rate   /w/ 3.47    /j/ 8.61
 #
 # So /w/'s F2 moves SLOWER than its own F1, where /j/'s moves faster.  This
 # row had 700 Hz, which is an English [w]; it made /w/'s F2 rate exceed its F1
 # rate, the wrong way round.  Japanese /w/ is [M\], unrounded or barely
 # rounded, so its F2 does not fall as far.  Setting the F2 excursion to
 # (3.47 / 4.49) of the F1 excursion gives 338 Hz from /a/, hence 887 Hz, and
 # the row is scaled by that.  [K]
 'w' : {'a': 887,'i':1013,'u': 887,'e': 950,'o': 887},           # [K]
 'y' : {'a':2100,'i':2100,'u':2100,'e':2100,'o':2100},           # [E] palatal
 'N' : {'a':1000,'i':1100,'u':1000,'e':1000,'o': 950},           # [L]
}
# manner, voiced, and how many frames the consonant takes
#   VOT [L]: Japanese /p/ 24-30 ms, /t/ 28-32, /k/ 45-57; voiced stops ~12 ms
MANNER = {
 # VOT.  Journal of Phonetics, PII S0095447006000120 -- the article itself is
 # paywalled and returned 403, so these are the figures as the user reported
 # them, not read from the paper here: 13 monolingual Japanese speakers,
 # word-initial stops, mean VOT /p/ 30.0, /t/ 28.5, /k/ 56.7 ms.  The paper's
 # point is that Japanese voiceless stops sit BETWEEN the conventional
 # short-lag and long-lag categories -- neither plainly unaspirated nor
 # plainly aspirated -- which is a long-running disagreement it declines to
 # settle in either direction.
 #
 # /p/ at 3 frames and /t/ at 3 were already right to within 1.5 ms, so the
 # scale here is not inflated by their material being word-initial, the way
 # Yamakawa's fricative durations are.  Only /k/ was short, and it is the one
 # that carries the finding: a velar VOT nearly twice the other two.  5 frames
 # was 6.7 ms under, 6 is 3.3 over, and 6 makes the /k/-to-/p/ ratio 2.00
 # against their 1.89.  Costs 0.05 morae/s over 1,200 dictionary words.
 'k':('stop',0,5,6), 'ky':('stop',0,5,6), 'g':('stop',1,4,1), 'gy':('stop',1,4,1),
 't':('stop',0,5,3), 'd':('stop',1,4,1),
 'p':('stop',0,5,3), 'py':('stop',0,5,3), 'b':('stop',1,4,1), 'by':('stop',1,4,1),
 # Frication lengths from Yamakawa & Amano (2015) and Amano & Yamakawa (2021).
 # Both papers' finding is that FRICATIVES AND AFFRICATES SEPARATE IN TIME:
 # /s/ and [C] group together and are about 1.8 times /ts/ and [tC].  The 2021
 # measurements, two independent sets of 63 words each, agree closely:
 #
 #     /s/   rise 47.5 ms   steady+decay 87.1   total 134.6
 #     /ts/  rise 27.6      steady+decay 45.7   total  73.3     ratio 1.84
 #     [tC]  rise 32.1      steady+decay 46.6   total  78.7     ratio 1.71
 #
 # The base was 7 frames against the affricates' 5 and 6 -- a ratio of 1.27,
 # where the whole point of the papers is 1.8.  The ratio was written into the
 # comment and never into the numbers.  10 frames gives 1.82.
 #
 # These stay BELOW the absolute measurements because those are citation-form
 # word-initial tokens, where a 134 ms frication plus its vowel is far more than
 # one 140 ms mora.  What transfers between their context and connected speech
 # is the ratio, not the absolute.
 #
 # One correction of substance: the earlier comment here said these were
 # measured before a DEVOICED /u/.  They were not.  Both papers' criterion is
 # word-initial before an UNDEVOICED -- that is, a VOICED -- /u/, and I read
 # "undevoiced" as "devoiced" and halved the base on the strength of it.
 's':('fric',0,10,0), 'sh':('fric',0,10,0), 'h':('fric',0,5,0),
 'hy':('fric',0,6,0), 'f':('fric',0,6,0), 'z':('fric',1,5,0),
 'j':('affric',1,4,5), 'ch':('affric',0,4,6), 'ts':('affric',0,4,5),
 'n':('nasal',1,5,0), 'ny':('nasal',1,5,0), 'm':('nasal',1,5,0),
 'my':('nasal',1,5,0), 'N':('nasal',1,0,0),
 'r':('flap',1,1,0), 'ry':('flap',1,1,0),
 'w':('glide',1,4,0), 'y':('glide',1,4,0), '':('none',1,0,0),
}
# Arai, "Perceptual Cues of Japanese /r/ Sounds: Formant Transitions vs.
# Intensity Dip", LabPhon 14, NINJAL Tokyo.  195 /ara/ stimuli from a formant
# synthesiser, twenty listeners rating /r/-likeness on a 0-100% scale.  His
# carrier was F1 750 / F2 1200; Mokhtari's /a/ is 737 / 1225, so his numbers
# transfer to this voice unscaled.  Four parameters:
#
#   AVd  depth of the intensity dip      0 to -40 dB
#   AVg  duration of the intensity dip   20-100 ms
#   Fg   total duration of the formant transitions  20-100 ms, Fg >= AVg
#   F2p  peak of F2                      1200-1600 Hz
#
# The regions scoring 70% or better fall into three allophones -- what he calls
# "sub-phonemes" of real /r/, each a legitimate Japanese /r/:
#
#   (b) alveolar flap          AVd -10..-40  AVg 20-40  Fg 60-100  F2p 1400-1600
#   (c) lateral approximant    no dip                   Fg 80-100  F2p 1400
#   (d) lateral flap           no dip                   Fg 40-60   F2p 1600
#
# F1 falls 750 -> 500 through the constriction in all three.  The negative
# result is the useful one: at F2p = 1200, that is with no F2 movement at all,
# scores were LOW.  The rise toward 1400-1600 is the cue that carries the
# tongue's move to the alveolar ridge, and the dip is what separates a flap
# from an approximant -- "the intensity dip induces a sensation of the flap
# sound".  The flap is his typical intervocalic phone, so it is the default.
#
# What this code had was (c) almost exactly -- F2p 1404, Fg 80 ms, no dip --
# which is why it was not heard as broken, only as not quite a flap.
FLAP_F1 = 500                   # [A] F1 at the constriction
_FLAP_LOCUS = 1800              # [E] alveolar; k below is fitted to Arai's /a/
# Palatalised /r/, as in ryokou, ryouri, ryokan.  It had no MANNER and no LOCUS
# entry at all, which made it the one consonant the front end could emit and the
# synthesiser could not say -- rya/ryu/ryo came out as a bare glide from a
# default posture with no flap in them.  Over all 486,646 pronounced naist-jdic
# entries it is 1.02% of consonant onsets.  This comment used to call it 1.51%
# and level with /z/ and /j/; that was a 40,000-entry prefix, and /z/ 2.00% and
# /j/ 1.98% are twice its share.  Its locus is anchored on Tanaka's MEASURED
# palatalised stops,
# /kj/ 2200-2300 and /pj/ 2080-2250, rather than on his alveolo-palatal
# fricatives at 2420-2580: those hold a front-cavity constriction that a flap,
# which is one brief contact, never attains.
_PAL_FLAP_LOCUS = 2150          # [T]-anchored
FLAP_MODES = {                  # k, AVd (dB), AVg (frames), Fg (frames)
 'flap'   : (0.478, 25, 3, 8),  # (b) F2p(a) 1500 and AVd 25, mid of both ranges
 'approx' : (0.304,  0, 0, 9),  # (c) F2p(a) 1400
 'latflap': (0.652,  0, 0, 5),  # (d) F2p(a) 1600
}
FLAP = {}

def _vf2(v):
    return U_FRONT if v == 'u' else V[v][1]

def set_flap(mode='flap', avd=None):
    """Select one of Arai's three /r/ sub-phonemes.

    Only /a/ was tested, so the other four vowels are extrapolated the standard
    way: a locus equation through the alveolar locus, with k fitted to the one
    point he measured (1225 -> 1500 for the default flap)."""
    k, d, g, fg = FLAP_MODES[mode]
    LOCUS['r'] = dict((v, int(round(t[1] + k * (_FLAP_LOCUS - t[1]))))
                      for v, t in V.items())
    # /ry/ palatalises /u/ -- it is in _FRONTING -- so its /u/ starts from the
    # fronted F2, not the citation one, or the flap would be made to travel a
    # distance the vowel does not actually begin at.
    LOCUS['ry'] = dict((v, int(round(_vf2(v) + k * (_PAL_FLAP_LOCUS - _vf2(v)))))
                       for v in V)
    FLAP.clear()
    FLAP.update(mode=mode, AVd=d if avd is None else avd, AVg=g or 3, Fg=fg)
    return FLAP

set_flap()
# A fricative resonates in its own front cavity, NOT in the vowel's formants.
# /s/'s cavity sits at 5-8 kHz, which is above the engine's F4 ceiling of
# 4080 Hz, so it cannot be placed as a formant at all: it is pushed as high as
# the tracks reach and carried by parallel band 6.  [C]'s sits lower, around
# 3000/3700, and is carried by bands 5 and 6.
#
# CORRECTION, from Astra tracing frame.c and generate.c, verified here.  The
# track-to-resonator mapping these comments assume is wrong:
#
#     track 4 -> F3        track 5 -> F4
#     track 6 -> F5, a FIFTH resonator with no input track of its own
#     track 8 -> bypasses all of them
#
# and the engine derives the two upper ones:
#
#     effective F4 = max(requested F4, F3 + 320)
#     F5           = max(3970, effective F4 + 400)
#
# So the /s/ row below does not put its noise at 4080.  F3 4000 and F4 4080
# give effective F4 = 4320 and F5 = 4720, and track 6 -- the one actually
# carrying the sibilant -- resonates at 4720 Hz, ABOVE the input ceiling.
# "Pushed as high as the tracks reach" is right about the intent and wrong
# about where it lands.
#
# Verified behaviourally: holding F3 at 800 and sweeping F4, the rendered
# output is BIT-IDENTICAL for every effective F4 up to 3568 -- because F5 is
# pinned at its 3970 floor throughout -- and changes from 3600 up, which is
# exactly where effF4 + 400 crosses 3970.
#
# Taking the vowel's F3/F4 instead
# -- which is what the first version did -- pulls /s/'s peak down to about
# 3270 Hz, into [C] territory, so the two stop being distinguishable, and the
# raw track-8 hiss that is then needed to brighten it is heard as sharpness.
def effective_resonators(posture):
    """-> (F1, F2, F3, effective F4, F5) in Hz, as the ENGINE will use them.

    A posture says what is requested.  The engine derives the top two, so a
    requested value and an effective one are different things, and four
    successive conclusions in this project were drawn from sweeps where the
    requested value moved and the effective one did not:

        effective F4 = max(requested F4, F3 + 320)
        F5           = max(3970, effective F4 + 400)

    F5 has no input track.  Track 6 -- the one carrying sibilant noise -- drives
    it, so a posture's noise sits at F5 and not at F3 or F4.  For the shipped
    /s/ row that is 4720 Hz, above the 4080 the input byte can express.

    Quantisation is applied first, because the tracks are bytes: F3 and F4 in
    16 Hz units.  Two requests that round to the same byte are the same filter.
    """
    f1, f2, f3, f4 = posture[0], posture[1], posture[2], posture[3]
    q3 = max(1, min(255, int(f3 / 16.0 + 0.5)))
    q4 = max(1, min(255, int(f4 / 16.0 + 0.5)))
    e4 = max(q4, q3 + 20) * 16
    return (f1, f2, q3 * 16, e4, max(3970, e4 + 400))


def distinct_postures(postures):
    """Which of these are actually different filters?  Returns the effective
    tuples, so a sweep can assert it varied what it meant to vary."""
    return [effective_resonators(p) for p in postures]


SIB_POST = {
 's' : (300, 2100, 4000, 4080, 110, 150, 210),   # peak ~4650
 'z' : (300, 2100, 4000, 4080, 110, 150, 210),
 'ts': (300, 1800, 4000, 4080, 110, 150, 210),
 'sh': (300, 2450, 3000, 3700, 120, 180, 240),   # peak ~3770
 'j' : (300, 2450, 3000, 3700, 120, 180, 240),
 'ch': (300, 2450, 3000, 3700, 120, 180, 240),
}
# noise sources.  Track 2 is aspiration through the cascade (stop bursts, /h/);
# track 8 is raw 4-8 kHz noise and the parallel bands put noise on the formants.
ASPIR = {2:80, 0:0}
# An alveolar burst peaks at 5.3 kHz (Kitazawa & Doshita 1984), which the
# cascade cannot reach, so it is carried the way /s/ is.  Quieter than /s/
# because a burst is a transient, not a held frication.
# /d/ keeps its VOICING through the release.  It did not: when /t/ was given
# this band-6 burst, /d/ was given the same entry with track 0 at zero, which
# made a voiced stop release into voiceless noise -- acoustically a /t/.  The
# place cue (a high, alveolar burst) belongs on both; the voicing does not.
# /b/ and /g/ were never in this table and so kept their {0: 40} all along,
# which is why only /d/ lost the contrast.
BURST_SRC = {'t': {1:80, 8:40, 6:66, 0:0},
             'd': {1:80, 8:30, 6:56, 0:40}}
SIBIL = {1:80, 8:36, 5:80, 6:80, 0:0}       # [C]: peak 3766, 3% above 6 kHz
SIBIL_S = {1:80, 8:44, 6:70, 0:0}           # /s/: peak 4648, 1% above 6 kHz
# /z/ and /j/, the two voiced sibilants.  Bands 5 and 6 sit ON F3 and F4, so
# using them means pushing F3/F4 to the ceiling -- which is fine for a
# voiceless fricative and wrong for a voiced one, because the voicing would
# drive those poles too.  See noise_post.
#
# Band 5 is the one that has to go: it rides F3, and F3 is where the damage
# was.  Dropping BOTH bands and leaning on track 8 instead -- which is not
# tied to a formant -- was tried first and put the noise peak at 8000 Hz, hard
# against the Nyquist edge, because unshaped track 8 simply rises to the top of
# the band.  /s/ peaks at 4746 and /z/ is its voiced partner, so it has to peak
# near there, not an octave above.  Band 6 therefore stays, riding an F4 that
# is still pushed up: F4 is the weakest and highest of the four poles, and the
# vowels' own F4 sits at 3282-3455 already, so moving it to the ceiling is an
# ~800 Hz change to a pole that is nearly there, where F3 was a 1776 Hz change
# to one that carries place.
SIBIL_Z = {1:80, 8:40, 6:70, 0:40}
VOICED_SIB = True                           # off restores the shared posture

# ---------------------------------------------------------------- /f/ = [P]
# Ruddell (2011), "An acoustic study of the Japanese voiceless bilabial
# fricative", San Francisco State University.  Ten native speakers, /f/ before
# all five vowels, in loan, Sino-Japanese and native words, read in the carrier
# "sore wa ___ desu"; the word-initial consonant was identified by ear and then
# checked against the spectrogram.
#
# The paper's own conclusion is partly negative -- "there can be no concise
# label of the phonetic realization of /f/" -- because FOUR sounds turned up
# where he expected two: [f], [P], [h], and a blend he had to give its own
# category, [P/h].  What is NOT negative is the distribution.  His Fig. 3 sorts
# the word list by which sound was heard and the sort comes out systematic:
#
#   [f]    "much higher in /fa/, /fi/, /fe/, /fo/" -- loanwords, and uttered by
#          the speakers with the most English.  He calls it "an irregularity of
#          Japanese ... due to linguistic overlap caused by bilingualism", so a
#          Japanese voice should NOT use it.  Kept here only to be heard.
#   [P]    before every vowel; before /u/, favoured ahead of an alveolar
#          (futon, futarusan, fushigi)
#   [P/h]  before /u/, favoured ahead of a velar (fuku, fukadakku)
#   [h]    "nearly exclusively before /u/" (one token otherwise), and most
#          common before a nasal (fune) or another vowel (fuan, fuufu)
#
# Each realization is (track-2 peak, B2 at onset, B2 held, B3).
#
# The levels come from his Figs. 5, 6, 11 and 13, which report amplitude not in
# dB but as the share of tokens in four bins -- Prominent >=20 dB, Moderate
# 10-20, Weak 1-10, None <=0 -- because he could not hold mic distance constant.
# Taking each bin at its midpoint (22 / 15 / 5.5 / 0) and averaging over his
# 1-7 kHz column, the only one of his three bands that fits under this
# synthesiser's 8 kHz Nyquist at all, gives [f] 10.8 dB, [P/h] 9.3, [P] 9.0,
# [h] 4.6.  That reproduces his prose as a sanity check -- he says [P] and
# [P/h] are "very similar in the overall level of amplitude" and that [h] "by
# far had the lowest amount of overall frication" -- so the derived numbers are
# used as offsets from [P] at the engine's 80.
#
# The bandwidths carry his main finding, which is not a level at all but a
# SHAPE IN TIME.  Figs. 7 and 9 are the same fricative with and without it: a
# prominent band at 1.7-1.8 kHz, which he reads as lip rounding, is missing
# from the first part of [P] and present from the first instant of [P/h].  He
# counts it (Fig. 10) and it is nearly categorical -- 33 of 36 [P] tokens show
# the onset with no band, 31 of 33 [P/h] tokens show it with one.  He calls the
# cause "initial lip closure": in [P] the lips are still travelling when the
# noise starts, in [P/h] they are already set.
#
# A constriction that is still closing damps its own resonance, so the band is
# broad at first and sharpens as the lips arrive.  That is a bandwidth, which
# is the one way to render this without moving F2 -- and F2 must not move, or
# the "initial lip closure" would be heard as a formant transition, i.e. as a
# stray phoneme, which is the defect this project keeps having to take back out.
#
# The engine stores a bandwidth as B/2 in one byte capped at 204, so 408 Hz is
# the widest any of these can actually be; 400 is used rather than a larger
# number that would be silently clamped to it.
PHI = {
 'f'    : (82, 120, 120, 250),   # steady, bright, abrupt start (his Fig. 4)
 'P'    : (80, 400, 120, 320),   # the band fades in: 33/36 of his tokens
 'P/h'  : (80, 120, 120, 320),   # steady lips: 31/33 of his tokens
 'h'    : (76, 140, 140, 400),   # "little to no frication besides" the band
}
# The band itself.  1.7 kHz (Fig. 7), 1.8 (Fig. 9), 1.7 (Fig. 12) -- three
# spectrograms read off the same axis by eye, agreeing inside 100 Hz, which is
# finer than this engine's F2 step of 8 Hz needs.  Taken as 1700.
#
# It is established for /fu/ ONLY: section 4.2 says "since the most significant
# variation occurred before /u/, from this point on I will focus on the
# fricatives in their /u/ environment", and every spectrogram after that is a
# /fu/.  He attributes the band to rounding "in anticipation of the following
# /u/", so before /a i e o/ there is no reason to expect it and no measurement
# of one.  Those four are loanword-only anyway; they keep a coarticulated
# bilabial noise posture instead.
PHI_BAND = 1700

def phi_mode(v, nxt, devoiced=False):
    """Which of Ruddell's four sounds to use before vowel `v`, where `nxt` is
    the consonant beginning the following mora ('' at a word or phrase end).

    His Fig. 3 ordering, read as a decision table.  [f] is deliberately
    unreachable: it is the one realization he attributes to bilingualism rather
    than to Japanese.
    """
    if v != 'u':
        return 'P'                       # loanword /fa fi fe fo/
    if nxt in ('n', 'ny', 'm', 'my', 'N') or nxt == '':
        # Saito Yoshio (2003, cited in Watanabe 2009 and quoted in Ruddell's
        # 2.6): "[P] always appears when followed by a voiceless vowel", his
        # example being /huku/ "clothes".  It is a secondary citation, so where
        # it meets Ruddell's own measurements they win -- and they disagree
        # with the obvious reading of it.  Taken as "devoicing forces [P]" it
        # erases the velar rule below, because /fu/ before a velar is nearly
        # always a devoicing environment; but /fuku/ is exactly that and
        # Ruddell's modal answer there is [P/h], not [P].  What he actually
        # writes is that "[P] AND [P/h] occurred more often in environments
        # which vowel devoicing occurred", so devoicing argues against [h] --
        # not against [P/h].  Applied that way: it blocks [h] and nothing else.
        #
        # The case it decides is word-final /fu/, as in toufu, where the vowel
        # is voiceless and the frication is the whole mora.  [h] there would
        # leave almost nothing to hear, which is the situation Saito's claim is
        # about.  Ruddell has no word-final /fu/ in his list to check it with.
        return 'P' if devoiced else 'h'  # fune, fuan, fuufu
    if nxt in ('k', 'ky', 'g', 'gy'):
        return 'P/h'                     # fuku, fukadakku
    return 'P'                           # futon, futarusan, fushigi

def phi_post(v):
    """The posture that shapes /f/'s frication.

    Before /u/ this is the measured band.  Before the other four there is no
    measurement, so it takes the VOWEL's own F2: a bilabial slit has no front
    cavity of its own to resonate, so the noise is excited into a tract already
    shaped for the vowel and is otherwise flat -- which is what his Figs. 5 and
    6 show, weak energy spread over 1-13 kHz with nothing standing out.  Not
    PHI_BAND: he ties that band to rounding for a following /u/, and before /a/
    it would push the noise 475 Hz ABOVE the vowel, where a lip constriction
    can only pull F2 down.
    """
    t = V.get(v, V['a'])
    return (300, PHI_BAND if v == 'u' else t[1], t[2], t[3], 200, 150, 320)
BREATHY = {2:72}                             # voiced frication keeps its voice
NASAL_A = {0:52}
DEVOICED = {0:0, 2:76}                       # a devoiced vowel: aspiration only
# A voiced stop's closure is NOT silent.  Japanese /b d g/ are prevoiced: the
# glottis keeps vibrating into a shut tract and the low frequencies radiate
# through the walls as a "voice bar".  This code wrote track 0 = 0 for voiced
# and voiceless stops alike, so the only thing separating /b/ from /p/ was VOT,
# where in Japanese prevoicing is the stronger cue of the two.  g + b + d are
# 11.12% of consonant onsets over all 486,646 pronounced naist-jdic entries;
# the 8.76% this comment used to print is their share of all MORAE.
#
# Idemaru & Guion fold VOT into the closure and so give no figure for the bar's
# level; what is categorical is that it is THERE, so the level is authored.  A
# shut tract radiates only its lowest resonance, heavily damped, which is what
# the posture below is: F1 pulled to 200 and every bandwidth wide.
VOICE_BAR = {0:32}                           # [E]

def voice_bar(sp):
    return (200, sp[1], sp[2], sp[3], 250, 250, 250)

# Homma (1981), J. Phonetics 9, 273-281, Table III: VOT depends on POSITION IN
# THE WORD, and strongly.  Four speakers, /CaCa/ and /CaCCa/ words in a carrier
# sentence:
#
#     initial   /p/ 24-29   /t/ 32   /k/ 45-61      mean of voiceless 37 ms
#     medial    /p/  7      /t/ 16   /k/ 24         mean of voiceless 16 ms
#
# A single VOT per consonant was used here regardless of position, taken from
# word-INITIAL measurements, so every medial stop ran two to four times long.
# Medial is much the commoner position.  "Gemination of stops did not affect
# VOT" -- /pp/ 11, /tt/ 13, /kk/ 28, mean 17, which is the medial value -- so
# the geminate branch takes the medial figure too.
VOT_MEDIAL = {'p': 1, 'py': 1, 't': 2, 'k': 2, 'ky': 2,
              'b': 1, 'by': 1, 'd': 1, 'g': 1, 'gy': 1}

# The moraic nasal takes the place of whatever follows it.  This was half done:
# the MURMUR already went bilabial before /p b m/, but the transition out of the
# preceding vowel was keyed on 'n' whatever followed, so every moraic nasal
# pulled the vowel toward the ALVEOLAR ridge -- including before /k g/, and
# including utterance-finally, where Tokyo Japanese has no alveolar closure.
# /v/ is /b/.  The romaji reader accepts `v` as an onset but nothing ever gave
# it a MANNER row or a LOCUS row, so `va` fell through to manner 'none' and
# rendered as a bare vowel with no consonant at all.  Japanese has no /v/: the
# kana was invented to transcribe one and most speakers say /b/, which is what
# the kana table now produces.  Aliasing the romaji path to match keeps the two
# inputs agreeing instead of leaving a hole in one of them.
MANNER['v'] = MANNER['b']
LOCUS['v'] = LOCUS['b']

def moraic_n(nc, final=False):
    """-> the place the moraic nasal takes.  Never None.

    It used to return None wherever there is no oral closure to assimilate to
    -- before a vowel, a glide, /h/ or another /N/ -- and the caller then fell
    back to the PRECEDING VOWEL'S OWN POSTURE, which is not a nasal at all.
    Measured on the frames: /sa N po/ holds F1 272, a murmur, where /sa N yo/
    held 736, which is /a/'s own F1, and it was reported by ear as "san sounds
    like saa".  That is the same defect as the word-final one, and it hid for
    as long as it did because its audibility depends entirely on the vowel
    before it -- /a/ at 737 is grossly wrong, /e/ at 481 is merely wrong, and
    /i/ at 298 is close enough to a murmur to pass unnoticed.
    """
    if nc in ('m', 'b', 'p', 'my', 'by', 'py', 'v'):
        # /v/ was missing.  The kana and romaji readers both send ヴ to /b/,
        # so a moraic nasal before it is bilabial like any other; without the
        # row it fell through to the no-closure branch instead.
        return 'N_m'                                 # bilabial
    if nc in ('k', 'g', 'ky', 'gy'):
        return 'N_k'                                 # velar
    if nc in ('t', 'd', 'n', 'r', 'ny', 'ry', 'ts', 'z', 'j', 's', 'sh', 'ch'):
        return 'N_n'                                 # alveolar
    # No oral closure to assimilate to: before a vowel, before a glide, before
    # /h/, or before another special mora.  The realisation is a NASALISED
    # VOWEL -- the tract stays in the vowel's shape and the velum opens -- so
    # the formants above F1 stay where the vowel left them, and inventing a
    # place would put a segment there that is not.
    #
    # But F1 does not stay.  "The formants stay where the vowel left them" was
    # taken to mean all of them, and F1 is the one that carries nasality: a
    # nasalised vowel has a strong low pole and a weakened first formant, so
    # keeping /a/'s 737 produces a quiet /a/ and nothing else.  /sa N yo/ held
    # exactly that and was heard as "saa".  F1 comes down to the murmur, which
    # is what every assimilated case already did and what the ear has passed.
    #
    # Utterance-finally is a different articulation -- uvular, a real
    # constriction -- and it coincides with this one in everything this
    # synthesiser can render, so the two share a posture and not a name.
    # Word-final /N/ had the same bug first and for the same reason: /obasan/
    # held F1 736 for the whole mora, reported as "the N sounds like a softer
    # A", which is exactly what that is.
    return 'N_q' if final else 'N_nas'

# ---- stop bursts -----------------------------------------------------------
#
# Morikawa (1997), J. Acoust. Soc. Jpn. (E) 18(4), analysed 270 Japanese CV
# syllables with a pole-zero (ARMA) model and found the burst is described by a
# real-axis pole Fp0, a first pole Fp1 and its bandwidth Bp1 -- which is a
# formant specification, so it ports straight across.  His Fig. 1 gives the
# shapes, before /i/:
#
#   /p/  diffuse-falling, a constant -8 dB/octave INDEPENDENT of the vowel,
#        carried by a real pole under 300 Hz
#   /t/  nearly flat across 0-5 kHz
#   /k/  a sharp strong peak at 3 kHz -- the most resonant of the three
#
# and /t/ and /k/ shift by +-3 dB/octave with the following vowel where /p/
# does not.  Tanaka supplies the centre frequency per vowel that Morikawa's
# figure does not, so the two together fix both shape and place.  [T] [Mo]
#
# Caveat on Morikawa: his material was low-passed at 4.8 kHz and sampled at
# 10 kHz, so nothing above 5 kHz is in it -- a real /t/ burst carries energy
# higher than that.
BURST_PEAK = {                                                   # Hz  [T]
 'k' : {'a':1300,'i':3100,'u':1100,'e':2100,'o': 800},
 'ky': {'a':2500,'i':3100,'u':3200,'e':3100,'o':2600},
 'p' : {'a': 600,'i':1200,'u':1000,'e': 700,'o': 600},
 'py': {'a':1000,'i':1200,'u':1600,'e':1200,'o': 950},
 't' : {'a':3000,'i':3000,'u':3000,'e':3000,'o':3000},           # flat  [Mo]
 'g' : {'a':1300,'i':3100,'u':1100,'e':2100,'o': 800},
 'gy': {'a':2500,'i':3100,'u':3200,'e':3100,'o':2600},
 'b' : {'a': 600,'i':1200,'u':1000,'e': 700,'o': 600},
 'by': {'a':1000,'i':1200,'u':1600,'e':1200,'o': 950},
 'd' : {'a':3000,'i':3000,'u':3000,'e':3000,'o':3000},
}
# How sharp that peak is.  /k/ is the resonant one, /t/ is flat, /p/ falls.  [Mo]
P_FLAT = False      # Kitazawa's flat [p] instead of Morikawa's diffuse-falling
K_FRONT = 0         # Kitazawa's 4.7 kHz velar peak before front vowels; his
                    # figure is a front/back summary of an averaged spectrum,
                    # where BURST_PEAK below is per vowel, so it is offered
                    # rather than imposed.  Set to 4700 to use it.
BURST_BW = {'k':110, 'ky':110, 'g':110, 'gy':110,
            't':520, 'd':520,
            'p':380, 'py':380, 'b':380, 'by':380}

def burst(c, v):
    """The posture for the release burst -- not the locus, which is where the
    formants are heading afterwards.  The burst is a cavity resonance.

    The peak goes on whichever track can reach it: F2 stops at 2540 Hz in this
    engine, so a velar burst before /i/ at 3100 has to sit on F3.  Whatever
    does not carry the peak is damped hard, or it forms a competing lump --
    putting F3 and F4 just above the peak is what first turned /ki/ into a
    4.3 kHz hump instead of Morikawa's 3 kHz resonance.
    """
    if c not in BURST_PEAK:
        return None
    f = BURST_PEAK[c].get(v, 1500)
    if K_FRONT and c in ('k', 'g') and v in 'ie':
        f = K_FRONT
    bw = BURST_BW.get(c, 300)
    if c in ('t', 'd'):
        # Morikawa offers an either/or here -- the postdental closure "brings
        # either a flat spectrum OR increasing of energy in the higher
        # frequencies" -- and this took the first arm.  Kitazawa & Doshita
        # (1984) settle it on 28 speakers: [t] is the high-rising one, with its
        # peak at 5.3 kHz.  That is ABOVE the 4080 Hz this engine's F4 reaches,
        # exactly as /s/'s cavity is, so the burst cannot be made in the
        # cascade at all: the formants go to the ceiling and the peak is
        # carried by parallel band 6 and track 8.  Same place of articulation
        # as /s/ and, reassuringly, nearly the same measured peak.
        return (300, 1800, 4000, 4080, 110, 150, 210)
    if c in ('p', 'py', 'b', 'by'):
        # TWO JAPANESE SOURCES DISAGREE HERE and neither is obviously wrong.
        # Morikawa (270 utterances, pole-zero fit): diffuse-falling, a constant
        # -8 dB/octave carried by a real pole under 300 Hz, not moved by the
        # following vowel.  Kitazawa & Doshita (28 speakers, critical-band
        # spectra): "[p]'s spectrum is normally FLAT, and unless spoken
        # carefully the low-frequency energy is small, so cases that could be
        # regarded as the falling type of Blumstein et al.'s template were
        # few."  He offers the reconciliation himself -- falling needs the
        # low-frequency energy that only careful speech has.  Morikawa's are
        # read CV syllables, which is careful, so both can be right of their
        # own material.  Left on Morikawa, with the flat shape available.
        if P_FLAT:
            return (900, 1800, 2800, 3800, 420, 520, 620)
        f = max(600, min(f, 1800))
        return (280, f, f + 1400, f + 2200, 260, bw, 700)
    # Velar: one sharp resonance.  There is no B4 track in this engine, so F4
    # cannot be damped -- it always rings wherever it is put.  Setting it just
    # above the peak therefore makes it *reinforce* the peak instead of forming
    # a second lump: leaving it at the 4080 ceiling is what put /ke/'s burst at
    # 4062 Hz when it should be at 2100.
    if f <= 2400:
        return (350, f, f + 700, f + 1200, 420, bw, 700)
    return (350, max(600, f - 1400), f, min(f + 700, 4080), 460, 900, bw)

# Where the formants are as VOICING BEGINS -- the articulatory locus.
#
# This is deliberately not Tanaka's F2 for the fricatives.  He measured F2 *of
# the frication noise*, which describes the hiss's own front-cavity spectrum
# and not where the tongue is.  Starting a voiced transition from the noise
# posture put /s/ at F2 2100 with F3 at 4000, which is a palatal glide: voicing
# from there is an audible [j], and "sensei" came out "syensyei".
#
# /sh/, /ch/ and /j/ ARE palatalised in Japanese -- [C], [tC], [dZ] -- so the
# glide belongs on those and only the plain alveolars had to be pulled back.
# A nasal's MURMUR and its TRANSITIONS are two different things, and the numbers
# for one are not the numbers for the other.  Fujimura's N2 ~1000 Hz is a
# resonance of the NASAL CAVITY, measured while the oral tract is shut; it
# describes the murmur and nothing else.  The formants that move into and out of
# the nasal are the ORAL tract's, and they are governed by where the oral
# closure is -- which for [n] is the alveolar ridge, the same place as /t/.
#
# Taking the murmur's N2 as the transition's locus made /ne/ sweep F2 from 1004
# to 1876 in 50 ms, straight through 1348 and 1524, and that sweep is heard as
# an extra segment between the /n/ and the /e/.  The mirror error was on the
# other side: the /a/ of `ane` bent DOWN to 1004 before jumping up.
#
# This is the same mistake as using a fricative's frication F2 as its locus --
# see VOICED_F2 below, whose comment already warns that a noise-shaping posture
# must never start a voiced transition.  A nasal shares its oral place with the
# homorganic stop, so the loci are taken from those rows rather than invented.
# /m/ does NOT borrow the /p/ row.  Doing so gives /mo/ a start of 1780 against
# the vowel's 856 -- a 924 Hz fall out of a bilabial, where locus theory has
# labials RAISING F2 into a back vowel, barely moving.  Move size is diagnostic:
# a locus is the virtual target the formants come from, so it should leave small
# transitions for most vowels, which /t/'s row duly does for /n/.  LOCUS['p']
# ran 1600-2250 and sat ABOVE the vowel for every vowel but /i/, which is not
# how a labial behaves, so it was rebuilt at the classic labial locus.
#
# The guess made at the time about WHY -- that it looked like a burst-spectrum
# measure -- does not survive.  A labial burst spectrum is vowel-INDEPENDENT:
# Morikawa says so outright ("independent of the following vowels"), and
# Kitazawa & Doshita's whole result is a vowel- and speaker-independent
# discriminant.  Tanaka's row varies by 650 Hz across the vowels, so whatever
# it measures, it is not that.  The move-size evidence against using it as a
# locus stands; the explanation offered for it was unsupported.
#
# These are a labial locus of 750 Hz -- the classic ~720 of locus theory, and
# consistent with LOCUS['w'] at 700-800, the labiovelar already in this table --
# attained halfway: F2 + 0.5 * (750 - F2).
_LABIAL = {'a': 921, 'i': 1224, 'u': 946, 'e': 1154, 'o': 788}    # = /p/, /b/
# 'N_q', the utterance-final allophone, is deliberately NOT in here -- see
# oral(), which handles it without a locus at all.
NASAL_ORAL = {'n': 't', 'm': _LABIAL, 'ny': 'ky', 'my': 'py', 'N': 't',
              'N_n': 't', 'N_m': _LABIAL, 'N_k': 'k'}

VOICED_F2 = {                                                  # [E] locus theory
 's' : {'a':1750,'i':2000,'u':1700,'e':1850,'o':1650},
 'z' : {'a':1750,'i':2000,'u':1700,'e':1850,'o':1650},
 'ts': {'a':1750,'i':2000,'u':1700,'e':1850,'o':1650},
 'sh': {'a':2000,'i':2200,'u':2050,'e':2100,'o':1950},
 'ch': {'a':2000,'i':2200,'u':2050,'e':2100,'o':1950},
 'j' : {'a':2000,'i':2200,'u':2050,'e':2100,'o':1950},
}

def coda(c, pv):
    """Where the preceding vowel's formants head as the consonant closes.

    Kashino (1990), ICSLP 90: with the release burst and most of the CV
    transition replaced by noise, 82% of Japanese intervocalic stops were still
    identified when the VC coarticulation was intact, against 41% without it --
    so this transition carries about as much place information as the burst,
    and when the two conflict the pre-closure takes over as the noise grows.

    It therefore has to carry the RIGHT place.  A velar's locus moves with
    whichever vowel is adjacent, so the transition out of the preceding vowel is
    keyed on THAT vowel, not on the one after the consonant: /ikou/ leaves /i/
    toward 2280 and enters /o/ from 1350, which is the velar pinch.
    """
    if c == 'h' or not pv:
        return None                      # glottal: nothing to head toward
    f2 = LOCUS.get(c, {}).get(pv)
    if f2 is None:
        return None
    if c in VOICED_F2:
        f2 = VOICED_F2[c].get(pv, f2)
    man = MANNER.get(c, ('none', 1, 0, 0))[0]
    if man == 'nasal':
        # the vowel bends toward the ORAL closure, not toward the murmur
        return oral(c, pv)
    t = V.get(pv, V['a'])
    # A closure pulls F1 down whatever its place, but it cannot pull it below
    # where the vowel already sits: Yanagisawa & Arai take /a/ from 750 to 500,
    # while /i/ starts at 298 and has nowhere to go.
    f1 = min(t[0], FLAP_F1 if man == 'flap' else
                   300 if man in ('fric', 'affric', 'glide') else 480)
    return (f1, f2, t[2], t[3], 110, 150, 210)

def noise_post(c, v):
    """The posture used only while frication is sounding, to place the noise
    peak.  Its F3/F4 are a device for shaping noise, not a vocal-tract state --
    see VOICED_F2 for why they must never start a voiced transition.

    "Only while frication is sounding" is the whole safety argument, and it
    held only because every consonant using this table was VOICELESS: with the
    voicing off, F3 and F4 at the engine's ceiling shape noise and nothing
    else.  /z/ and /j/ are voiced, their frication frames carry track 0 at 40,
    and on those frames a 4000 Hz F3 is a resonance the voice is driving.  In
    /uza/ that put F3 at 2224 -> 4000 -> 2272 across five voiced frames: a
    1776 Hz excursion up and 1728 back, which is the same defect as the burst
    posture, the nasal murmur and the fricative locus before it, and the ninth
    of its kind.  It also measured +20.8 dB of 4 kHz over 2 kHz against /s/'s
    +14.4, so the VOICED fricative was the brighter of the two, backwards.

    So a voiced sibilant keeps the tract's own F3/F4 and places its noise with
    SIBIL_Z instead, on track 8, which is not tied to a formant.  /s/ cannot do
    that -- the note above SIB_POST records that the raw track-8 hiss needed to
    brighten /s/ is heard as sharpness -- but /z/ does not need brightening to
    begin with, because a voiced fricative's frication is the weaker of the two
    by a wide margin.  The trick /s/ needs is exactly the trick /z/ cannot have.
    """
    if c not in SIB_POST:
        return None
    q = SIB_POST[c]
    f2 = LOCUS.get(c, {}).get(v, q[1])
    if VOICED_SIB and MANNER.get(c, ('none', 0, 0, 0))[1]:
        # F2 as well.  VOICED_F2 exists for precisely this -- the F2 a voiced
        # transition may start from, where LOCUS holds a frication measurement
        # -- and onset() and coda() both consult it.  noise_post did not, which
        # was right while every consumer was voiceless and wrong for /z/ and
        # /j/: /oze/ ran F2 to Tanaka's 2140 on voiced frames where the vowel
        # sits at 1873, a 648 Hz step, and /ajo/ 760.
        f2 = VOICED_F2.get(c, {}).get(v, f2)
        t = V.get(v, V['a'])              # F3 from the tract, F4 left at the
        return (q[0], f2, t[2], q[3], q[4], q[5], q[6])   # ceiling for band 6
    return (q[0], f2, q[2], q[3], q[4], q[5], q[6])

# Hirata & Tsukada (2003), "The Effects of Speaking Rates and Vowel Length on
# Formant Movements in Japanese": "the long vowels occupied a more peripheral
# portion of the F1-F2 vowel space than the short vowels did.  This supports a
# suggestion that long vowels resist coarticulation to a greater extent than
# short vowels do."  A short vowel undershoots its target; a long one has the
# time to reach it.  This code used ONE target for both, so /o/ and /o:/ were
# the same vowel held for different lengths.
#
# Their study is two speakers and they call it tentative, so it is checked
# against Yazawa & Kondo's (2019) released dataset -- 16 speakers, 3,200 tokens,
# five vowels long and short -- where it holds for four vowels of five:
#
#   a  F1 687 -> 744, lower in the mouth      i  F2 2154 -> 2293, fronter
#   e  F2 1947 -> 2043, fronter               o  F2  949 ->  813, backer
#   u  F2 1435 -> 1442, which is no move at all
#
# /u/ not moving is itself right: Japanese /u/ is compressed and central and has
# nowhere peripheral to go.
#
# The two corpora disagree on ABSOLUTE values -- Yazawa's F2 runs 58 to 142 Hz
# above Mokhtari's, which is what two corpora normally do -- so the absolute
# numbers are not swapped in.  A WITHIN-corpus difference does transfer, so it
# is Yazawa's long-minus-short delta that is applied to Mokhtari's targets, and
# Mokhtari's values are kept as the short vowel.
LONG_DELTA = {                  # [H] + [Y]:  F1, F2, F3
 'a': (57, -46,  56), 'i': (5, 139, 145), 'u': (4,  7, -35),
 'e': (17,  95,  72), 'o': (-7, -136, 84),
}

def vowel(v, ctx='', long=False):
    """The vowel posture, with /u/ fronted after an alveolar or palatal, and
    pushed to the periphery when the vowel is long."""
    sp = V[v]
    if v == 'u':
        f2 = U_FRONT if ctx in _FRONTING else U_BACK
        sp = (sp[0], f2, sp[2], sp[3], sp[4], sp[5], sp[6])
    if long and v in LONG_DELTA:
        d = LONG_DELTA[v]
        sp = (sp[0] + d[0], sp[1] + d[1], sp[2] + d[2], sp[3],
              sp[4], sp[5], sp[6])
    return sp

def oral(c, v):
    """A nasal's ORAL posture: where the formants sit as the closure makes or
    breaks, as against the murmur that sounds while it is held.  This is what
    the transitions on either side must aim at; `onset`/`coda` give the murmur.
    """
    if c in ('N_q', 'N_nas'):
        # Two articulations with one posture.  N_q is utterance-final /N/,
        # which the literature calls uvular; N_nas is the nasalised vowel that
        # /N/ becomes before a vowel, a glide or /h/, where there is no closure
        # at all.  What they share is the only thing this synthesiser can
        # render of either: F1 comes down to the murmur and the formants above
        # it stay where the vowel left them.
        #
        # The closure lowers F1 whatever its place, so that part is real and is
        # kept.  F2 carries PLACE, and there is no measured uvular locus to aim
        # it at.
        #
        # Borrowing the velar row was tried first and points the wrong way: the
        # velar locus for /a/ is 1530, ABOVE /a/'s own 1225, so the velar pinch
        # RAISED F2 going into the nasal where a uvular -- further back than a
        # velar, not forward of it -- has to lower it.  That is not an
        # approximation, it is the opposite sign.
        #
        # And final /N/ has no place contrast to signal: it is the only nasal
        # that occurs there, so nothing is lost by declining to guess.  F2
        # stays where the vowel left it and F1 does the closing.
        t = V.get(v, V['a'])
        if not v:
            return None
        return (min(t[0], 300), t[1], t[2], t[3], 110, 150, 210)
    h = NASAL_ORAL.get(c)
    if h is None or not v:
        return None
    f2 = (h if isinstance(h, dict) else LOCUS.get(h, {})).get(v, 1500)
    t = V.get(v, V['a'])
    # the oral tract is shut, so F1 is as low as the vowel will let it go
    return (min(t[0], 300), f2, t[2], t[3], 110, 150, 210)


def onset(c, v):
    """The consonant's posture before vowel v: its locus with the vowel's
    upper formants, which is what a locus means."""
    if not c:
        return None
    f2 = LOCUS.get(c, {}).get(v, 1500)
    man = MANNER.get(c, ('none',1,0,0))[0]
    if man == 'nasal':
        return (270, f2, 2400, 3300, 140, 220, 280)      # [L] N1 250-300
    if c == 'h':
        # [h] is glottal: there is no supralaryngeal constriction, so the tract
        # is already in the shape of the vowel that follows and [h] is simply a
        # voiceless version of it.  Giving it a posture of its own inserts a
        # segment that is not there -- Tanaka's frication F2 for /ho/ is 1650
        # against the vowel's 856, a 794 Hz gap, and that gap is heard as an
        # extra phoneme in "nihon".  His number is not wrong; it is a property
        # of the noise, measured early, and not a vocal-tract state.
        #
        # /hy/ = [C] and /f/ = [P] keep their own postures: those really do
        # have a constriction, palatal and bilabial respectively.
        t = V.get(v, V['a'])
        return (t[0], t[1], t[2], t[3], t[4] + 50, t[5] + 50, t[6] + 50)
    if c in VOICED_F2:
        f2 = VOICED_F2[c].get(v, f2)
    tgt = V.get(v, V['a'])
    # [A] the flap's own F1 is measured, 500; it is not a full closure
    f1 = (FLAP_F1 if man == 'flap' else
          300 if man in ('fric','affric','glide') else 400)
    f1 = min(f1, tgt[0]) if man == 'flap' else f1
    return (f1, f2, tgt[2], tgt[3], 110, 150, 210)
