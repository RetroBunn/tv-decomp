# -*- coding: utf-8 -*-
"""Segment the synthesised waveform the way the literature segments speech.

Every duration reported for this synthesiser so far has been read off the
parameter tracks, which is the easy measurement and not the one any paper
makes.  Idemaru, Holt & Seltman (2012), JASA 132(6) 3950-3964, state the
procedure explicitly, and they credit it to Peterson & Lehiste (1960), Klatt
(1976), Lahiri & Hankamer (1988), Hankamer et al. (1989) and Ham (2001):

    "The vowel duration was measured from the first complete cycle of periodic
    oscillation to the last complete cycle in the waveform.  Onset of
    periodicity in the waveform and voicing energy of a time-locked spectrogram
    were referred to in order to determine the onset of the vowel.  Closure
    duration was measured from the end of the last periodic cycle of the
    preceding vowel to the onset of stop burst.  VOT was measured from the
    onset of the visible burst in the waveform to the onset of the first
    complete periodic cycle in the following vowel."

So: periodicity decides the vowel edges, and the burst decides the closure /
VOT boundary.  All three are read from the rendered audio here, not the tracks.
"""
import numpy as np

SR = 16000
# The window has to be long enough that a whole pitch period still leaves a
# usable overlap.  At this voice's ~90 Hz the period is 178 samples, so a
# 256-sample window leaves 78 and the raw autocorrelation at that lag is
# attenuated below any sensible threshold -- it measured 0.59 peak on a fully
# voiced /ata/.  512 samples, with the correlation normalised over the overlap
# rather than by the window energy, fixes it.
WIN = 512          # 32 ms
HOP = 32           # 2 ms step, finer than one 10 ms frame


def _periodicity(x, sr=SR, win=WIN, hop=HOP):
    """Normalised autocorrelation peak per window, over 60-400 Hz."""
    lo, hi = int(sr / 400.0), int(sr / 60.0)
    out = []
    for i in range(0, max(1, len(x) - win), hop):
        w = x[i:i + win] - np.mean(x[i:i + win])
        if float(np.dot(w, w)) < 1e-6:
            out.append(0.0)
            continue
        best = 0.0
        for k in range(lo, min(hi, win - 40)):
            a, b = w[:win - k], w[k:]
            d = np.sqrt(float(np.dot(a, a)) * float(np.dot(b, b)))
            if d > 1e-9:
                best = max(best, float(np.dot(a, b)) / d)
        out.append(best)
    return np.array(out)


def _energy(x, win=WIN, hop=HOP):
    return np.array([float(np.sqrt(np.mean(x[i:i + win] ** 2)))
                     for i in range(0, max(1, len(x) - win), hop)])


def _zcr(x, sr=SR, win=WIN, hop=HOP):
    """Zero crossings per second.  The classic voiced/unvoiced discriminator,
    and needed here because the autocorrelation measure takes the best of ~227
    candidate lags, so broadband noise scores 0.5-0.7 on it by chance alone --
    enough for a /t/ burst carried on parallel band 6 to read as voiced."""
    out = []
    for i in range(0, max(1, len(x) - win), hop):
        w = x[i:i + win]
        out.append(float(np.count_nonzero(np.diff(np.signbit(w)))) * sr / win)
    return np.array(out)


def segment(x, sr=SR, voiced_at=0.45):
    """-> dict of boundaries in ms, by the definitions above.

    `voiced` is the periodic region: onset of periodicity to its last cycle.
    `burst` is the first energy onset after a silent stretch that precedes a
    voiced region, which is what 'onset of the visible burst' amounts to.
    """
    x = np.asarray(x, dtype=float)
    p = _periodicity(x, sr)
    e = _energy(x)
    ms = 1000.0 * HOP / sr
    # Periodicity ALONE is not voicing: a normalised autocorrelation computed
    # over near-silence is meaningless and comes out near 1, so a silent stop
    # closure read as fully voiced and every closure measured 6 ms.  Voicing
    # needs periodicity AND energy.
    z = _zcr(x, sr)
    emax = e.max() if e.max() else 1.0
    n = min(len(p), len(e), len(z))
    p, e, z = p[:n], e[:n], z[:n]
    voiced = (p > voiced_at) & (e > 0.08 * emax) & (z < 3000.0)
    quiet = e < 0.04 * emax

    runs, i = [], 0
    while i < len(voiced):
        if voiced[i]:
            j = i
            while j < len(voiced) and voiced[j]:
                j += 1
            if (j - i) * ms >= 12:            # ignore sub-12 ms flickers
                runs.append((i, j))
            i = j
        else:
            i += 1
    return {'ms': ms, 'voiced': voiced, 'quiet': quiet, 'runs': runs,
            'energy': e, 'periodicity': p}


def stop_measures(x, sr=SR):
    """Closure, VOT and the following vowel, for a word with ONE medial stop.

    Closure: end of the last periodic cycle before the gap -> burst onset.
    VOT:     burst onset -> first periodic cycle after it.
    V2:      that cycle -> the last cycle of the run.
    """
    s = segment(x, sr)
    ms, runs, e = s['ms'], s['runs'], s['energy']
    if len(runs) < 2:
        return None
    v1_end = runs[0][1]
    v2_start = runs[1][0]
    gap = e[v1_end:v2_start]
    if len(gap) < 2:
        return None
    # The burst is a RISE out of the closure, not simply the first energy in
    # the gap: scanning forward from the vowel's end finds the vowel's own
    # decay, which put every closure at 0 ms.  So find the quietest point --
    # the closure proper -- and take the burst as the first rise after it.
    floor = int(np.argmin(gap))
    rest = gap[floor:]
    thr = rest.min() + 0.15 * (rest.max() - rest.min())
    rel = next((k for k, v in enumerate(rest) if v > thr), 0)
    burst = v1_end + floor + rel
    return {'closure_ms': (burst - v1_end) * ms,
            'vot_ms': (v2_start - burst) * ms,
            'v1_ms': (runs[0][1] - runs[0][0]) * ms,
            'v2_ms': (runs[1][1] - runs[1][0]) * ms}


# ---------------------------------------------------------------------------
# Yazawa & Kondo (2019), ICPhS, "Acoustic characteristics of Japanese short and
# long vowels" -- the vowel-duration definition, quoted from their 2.3:
#
#     "The start and end boundaries of each vowel token were first determined
#     automatically using SPPAS, and then manually modified to correspond to
#     the first and last positive zero crossings in Praat.  Vowel duration was
#     measured as the time between these boundaries."
#
# This is the canonical definition for this project.  Vowel length had drifted
# and been refitted four times against numbers nobody had written down, and
# three plausible readings of "vowel duration" gave 1.18, 1.50 and 2.50 on the
# same utterance.  A measurement that cannot be stated cannot be regressed.
#
# Their manual adjustment is the part that cannot be reproduced here, so an
# energy threshold stands in for the eye that found the first glottal pulse.
# The zero-crossing snap afterwards is exact, and `onset_db` is reported with
# the result so the stand-in is visible rather than buried.
ONSET_DB = 20.0        # below the vowel's own peak


def _positive_crossings(x):
    """Indices i where x goes from <= 0 to > 0 -- Praat's positive zero
    crossing.  The sign of the crossing matters: taking either direction would
    put the boundary up to half a pitch period off, 5 ms at this voice."""
    return np.where((x[:-1] <= 0) & (x[1:] > 0))[0] + 1


def vowel_duration(x, sr=SR, onset_db=ONSET_DB, region=None):
    """-> (duration_ms, start_sample, end_sample), by the definition above.

    `region` optionally restricts the search to (lo, hi) samples, for a word
    with more than one vowel in it.
    """
    x = np.asarray(x, dtype=float)
    lo, hi = region if region else (0, len(x))
    seg = x[lo:hi]
    if not len(seg):
        return 0.0, lo, lo

    # Short-time RMS, fine enough that the window is not itself the precision
    # limit: 4 ms window, 1 ms hop, against the 10 ms parameter frame.
    w = max(1, int(sr * 0.004))
    h = max(1, int(sr * 0.001))
    idx = range(0, max(1, len(seg) - w), h)
    rms = np.array([np.sqrt(np.mean(seg[i:i + w] ** 2)) for i in idx])
    if not len(rms) or rms.max() <= 0:
        return 0.0, lo, lo

    # Energy alone does not find a VOWEL, it finds a loud noise.  Two of
    # Yazawa & Kondo's five contexts are /zV1s/ and /hV1d/, and with an energy
    # threshold only, /z/'s voiced frication and /h/'s aspiration both read as
    # part of the vowel -- /zase/ measured 138.9 ms against their 78, with the
    # boundary landing in the frication.  The eye they used in Praat would not
    # make that mistake: frication has no periodic waveform to put a boundary
    # on.  The zero-crossing rate is the classic discriminator and is already
    # used in segment() for the same reason, at the same 3000/s.
    zw = max(1, int(sr * 0.010))     # longer window: a 4 ms one counts too few
    zcr = np.array([np.count_nonzero(np.diff(np.signbit(seg[i:i + zw])))
                    * sr / float(zw) for i in idx])
    thr = rms.max() * (10.0 ** (-onset_db / 20.0))
    voiced = (rms > thr) & (zcr < 3000.0)

    # The zero-crossing rate is not enough on its own either, and /hV1d/ is
    # why.  [h] is a VOICELESS VERSION OF THE FOLLOWING VOWEL -- that is how
    # onset() builds it, from the vowel's own posture, and it is why /h/ has no
    # locus of its own -- so its aspiration has the vowel's formants and
    # therefore the vowel's low zero-crossing rate.  /hade/ measured 126.7 ms
    # against their 78 with the boundary sitting in the aspiration.  What
    # separates them is the one thing [h] does not have: a periodic waveform.
    # Coarse, because a normalised autocorrelation needs a window long enough
    # to hold a pitch period with overlap to spare, so this only brackets the
    # region and the fine grid above still places the boundary inside it.
    # Used as a MASK this over-trims, because a 32 ms window cannot score high
    # until it sits wholly inside the vowel -- it cut every context by about
    # 20 ms.  So it seeds rather than masks: the periodic core says where a
    # vowel certainly is, the fine grid above grows the boundary outward from
    # it, and the growth is capped at the window's own half-width, which is the
    # resolution the periodicity measure actually has.  For a stop context that
    # cap is slack and the boundary lands where the energy says.  For /hV1d/ it
    # binds, so the error there is bounded at ~16 ms instead of running the
    # whole 50 ms of aspiration.
    phop = max(1, int(sr * 0.004))
    p = _periodicity(seg, sr, win=WIN, hop=phop)
    core = np.where(p > 0.45)[0] if len(p) else np.array([], int)
    if len(core):
        cap = int((WIN / 2.0) / h)
        c0 = int(core[0] * phop / h)
        c1 = min(len(voiced) - 1, int(core[-1] * phop / h))
        i = c0
        while i > 0 and voiced[i - 1] and c0 - (i - 1) <= cap:
            i -= 1
        j = c1
        while j + 1 < len(voiced) and voiced[j + 1] and (j + 1) - c1 <= cap:
            j += 1
        a, b = i * h, min(len(seg) - 1, j * h + w)
    else:
        above = np.where(voiced)[0]
        if not len(above):
            return 0.0, lo, lo
        a, b = above[0] * h, min(len(seg) - 1, above[-1] * h + w)

    # Snap outward to positive zero crossings: the first at or after the onset,
    # the last at or before the offset.
    zc = _positive_crossings(seg)
    if not len(zc):
        return 1000.0 * (b - a) / sr, lo + a, lo + b
    start = zc[zc >= a]
    end = zc[zc <= b]
    if not len(start) or not len(end) or end[-1] <= start[0]:
        return 1000.0 * (b - a) / sr, lo + a, lo + b
    s, e = int(start[0]), int(end[-1])
    return 1000.0 * (e - s) / sr, lo + s, lo + e
