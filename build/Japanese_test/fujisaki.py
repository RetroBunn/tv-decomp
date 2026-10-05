"""The Fujisaki command-response model for Japanese F0 contours.

    ln F0(t) = ln Fb
             + SUM_i  Api * Gp(t - T0i)                     phrase commands
             + SUM_j  Aaj * [ Ga(t - T1j) - Ga(t - T2j) ]    accent commands

Both control mechanisms are critically-damped second-order systems; the phrase
command is an impulse and the accent command a step (Hirose, Sakata, Osame &
Fujisaki, SSW2 1994, p.168).  So

    Gp(t) = a^2 * t * exp(-a*t)                       t >= 0, else 0
    Ga(t) = min[ 1 - (1 + b*t) * exp(-b*t), theta ]   t >= 0, else 0

a and b are the natural angular frequencies of the two mechanisms.  They are not
in the papers in jp_res/ -- they are referenced to Fujisaki & Hirose, JASJ(E)
5(4) 233-242 (1984), which we do not have.  Published values across studies run
a = 1.7-3.0 /s and b = 20-25 /s with theta = 0.9.

a is pinned here from the figure instead of taken on trust: in SSW2 Fig.1(a) the
phrase component turns up at t = 0.78 s and peaks at t = 1.25 s, and since
Gp peaks at exactly t = 1/a, that delay of 0.47 s gives a = 2.1.  2.0 is used.
"""
import math

ALPHA = 2.0      # phrase mechanism, /s   -- 1/ALPHA is the peak delay
BETA  = 20.0     # accent mechanism, /s
THETA = 0.9      # accent ceiling


def Gp(t, a=ALPHA):
    return 0.0 if t < 0 else a*a * t * math.exp(-a*t)


def Ga(t, b=BETA, theta=THETA):
    if t < 0:
        return 0.0
    return min(1.0 - (1.0 + b*t) * math.exp(-b*t), theta)


def phrase_amp(nominal, T0, prior, a=ALPHA):
    """Kawai section 5.2: a phrase command's magnitude is NOT a fixed number.

    "In the processing that actually generates phrase commands, P1 does not use
    a fixed value; the magnitude used is whatever value achieves the peak level
    equivalent to that of a phrase command of Ap = 0.35 occurring in
    isolation."  His reason is the case where a weaker symbol stands at the
    sentence head and a stronger one follows it: without the rule the second
    command lands on the first's residue and pushes the phrase component well
    above the normal sentence-head level.

    That is exactly the overshoot this code hit -- a second major phrase
    peaking ABOVE the first -- and this is his remedy, which is better than
    thresholding on phrase length because it holds for any spacing.
    """
    gain = Gp(1.0 / a, a)                     # Gp at its own peak
    peak_t = T0 + 1.0 / a
    residue = sum(ap * Gp(peak_t - t0, a) for t0, ap in prior)
    return max(0.0, (nominal * gain - residue) / gain)


def contour(n_frames, Fb, phrases, accents, step=0.010,
            a=ALPHA, b=BETA, theta=THETA):
    """phrases: [(T0, Ap)]   accents: [(T1, T2, Aa)]   -> [F0 in Hz per frame]"""
    out = []
    seen_pos = zeroed = False
    for k in range(n_frames):
        t = k * step
        ph = 0.0
        for T0, Ap in phrases:
            ph += Ap * Gp(t - T0, a)
        # Kawai section 5.2: "after the phrase component reaches zero through
        # the negative phrase command corresponding to P0, the influence of
        # that command and of every preceding phrase command is nullified --
        # it is held at zero."  Without this the final lowering keeps pulling
        # and F0 is dragged below the baseline, which the model does not allow.
        if ph > 0.0:
            seen_pos = True
        elif seen_pos:
            zeroed = True
        if zeroed:
            ph = 0.0
        lf = math.log(Fb) + ph
        for T1, T2, Aa in accents:
            lf += Aa * (Ga(t - T1, b, theta) - Ga(t - T2, b, theta))
        out.append(math.exp(lf))
    return out


# ---- the symbol values, Kawai, Hirose & Fujisaki 1994, at ~7 morae/s --------
# Verified against the paper: his section 5.2 gives the phrase symbols and 5.3
# the six accent symbols, with these normalised values "at a speech rate of
# about 7 morae/s".  P1 re-establishes the phrase component at a SENTENCE head,
# P2 adds one at a CLAUSE head, P3 between and within ICRLBs, and P0 resets it.
PAUSE  = {'S1': 0.700, 'S2': 0.300, 'S3': 0.100}          # seconds
PHRASE = {'P1': 0.35, 'P2': 0.25, 'P3': 0.15, 'P0': -0.50}
ACCENT = {'FH': 0.50, 'FM': 0.25, 'FL': 0.10,             # heiban
          'AH': 0.50, 'AM': 0.35, 'AL': 0.15}             # accented
MORA_S = 1.0 / 7.0                                        # the papers' rate


def accent_times(mora_starts, accent_type, n_mora, final=True):
    """Kawai section 7.2.  accent_type 0 is heiban, n is a nucleus on mora n.

    Rise:  before mora 1 if atamadaka (type 1), after mora 1 otherwise.
    Fall:  at the nucleus if accented; for heiban, after the final mora when
           this is the end of the scope.
    """
    end = mora_starts[-1][1]
    if accent_type == 1:
        T1 = mora_starts[0][0]
    else:
        T1 = mora_starts[0][1]
    if accent_type == 0:
        T2 = end if final else mora_starts[0][1]
    else:
        T2 = mora_starts[accent_type - 1][1]
    return T1, T2
