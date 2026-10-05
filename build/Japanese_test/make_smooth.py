# -*- coding: utf-8 -*-
"""Smoothing out what the sweep had left at 248-368 Hz.

Run from the repository root:  python build/Japanese_test/make_smooth.py

I had argued these should stay: a stop release opens the mouth about as fast as
anything in speech, a glide is a movement by definition, and smoothing either
into something slower than the gesture would be wrong.  That argument was about
the wrong thing.  None of the three remaining steps was an articulation moving
fast.  All three were two parts of the code disagreeing about where a formant
was going, and the disagreement landing in a single frame.

  1. The offglide never arrived.  _offglide interpolated from the vowel to the
     consonant's posture with (i+1)/(n+1), so its last frame sat one full step
     short of the target however many frames it was given.  That remainder was
     itself a step, about 150 Hz of /iwa/'s 304.

  2. The offglide was aimed somewhere the consonant did not sit.  coda() keys
     F2 on the PRECEDING vowel and onset() on the following one.  For a stop
     that is right and it is Kashino's point -- there is a closure between them
     and the articulator really does travel through it.  A glide, a flap or a
     fricative has no closure to travel through; it is one gesture with one
     posture, and /iwa/ was aiming at 1013 while holding 887.

  3. F1 was aimed at an open tract and landed on a shut one.  A prevoiced
     closure forces F1 to 200 through voice_bar(), but coda() aimed the vowel's
     F1 at 480, so it glided down to about 540 and then dropped 344 Hz in one
     frame as the voice bar took over.

With those three fixed everything in the inventory sat at 208-248 Hz, which is
one rounding away from the limit -- and it was a rounding.  _glide_len sized a
transition with int(d/SLEW + 0.5), so SLEW was the average speed of a move
rather than its ceiling, and a move worth 1.2 frames got one frame and ran at
240 Hz.  Rounded up, SLEW means what it says.

One step is left and is not smoothed.  A prevoiced stop's closure runs its
formants from the preceding vowel's locus to the following one's -- /igo/ is
the velar pinch, and it is the place cue Kashino measures -- at about 240 Hz a
frame.  None of it is radiated: voice_bar() sets F1 to 200 and all three
bandwidths to 250, and the rendered closure measures 99.0% of its energy below
500 Hz, 0.6% between 1 and 3 kHz, 9.3 dB down on the vowel.  Smoothing it would
spend closure duration, which Homma measured, on an inaudible number.

   sweep.py, worst voiced-to-voiced step, Hz per 10 ms frame

      before the voiced-sibilant work   1776
      after it                           368
      after this                         200   = SLEW, for 13 of 27 onsets

   Thirteen onset categories reach the limit and fourteen stay under it --
   /f/ 192, the labials 184, /k/ and /ts/ 168, /g/ 144, /n/ and /t/ 136, /d/
   96.  sweep.py prints rows[:12] and thirteen are tied at 200, so the printed
   slice is all 200s; it is not the whole inventory.

Durations are intact: over 3,000 dictionary words 91.5% come out the same
length to the frame, the mean moves 1.06 ms on a 615 ms word, and the corpus
rate goes 7.35 to 7.34 morae/s.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_voice as J, jp_speak as S, jp_mora as M

SR = S.SR
GAP = np.zeros(int(SR * 0.45), dtype=np.int16)
_SLEW_CEIL = S._glide_len


def _slew_avg(a, b, base):
    d = max(abs(a[1] - b[1]), abs(a[0] - b[0]) * 2, abs(a[2] - b[2]) * 0.7)
    return max(base, min(S.TRANS_MAX, int(d / float(S.SLEW) + 0.5)))


def mode(on):
    S.SMOOTH_VC = on
    S._glide_len = _SLEW_CEIL if on else _slew_avg


def clip(text, accent=0):
    morae = M.to_morae(text)
    frames, ends, q_ends = S.build(morae)
    S.pitch(frames, ends, accent, q_ends=q_ends, morae=morae)
    return S.render(frames)


def write(name, parts, note=""):
    x = parts[0]
    for p in parts[1:]:
        x = np.concatenate([x, GAP, p])
    with wave.open(os.path.join(HERE, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())
    print("  %-30s %5.2f s  %s" % (name, len(x) / float(SR), note))


print("92  the three disagreements, on the words that showed them")
parts = []
for w in ("iwa", "aga", "ari", "igo", "kawa", "kagami", "tori", "nagai"):
    mode(False); parts.append(clip(w))
    mode(True);  parts.append(clip(w))
mode(True)
write("92-smooth-vc.wav", parts, "pairs: before, then after")

print("93  ordinary words, where it either reads as smoother or it does not")
parts = []
for w, a in (("arigatou", 0), ("ohayou", 0), ("sayounara", 0), ("tomodachi", 0),
             ("daidokoro", 0), ("gakkou", 0), ("tabemasu", 0), ("kirei", 1)):
    mode(False); parts.append(clip(w, a))
    mode(True);  parts.append(clip(w, a))
mode(True)
write("93-smooth-words.wav", parts, "pairs: before, then after")

print("94  connected speech, which is where it has to hold up")
mode(True)
write("94-sentences.wav",
      [clip(t, a) for t, a in (
          ("konnichiwa", 0),
          ("watashi wa | nihongo ga | wakarimasu", 0),
          ("kyou wa | ii tenki desu ne", 0),
          ("arigatou gozaimasu", 0),
          ("ashita | tomodachi to | eiga wo mimasu", 0))],
      "five utterances with phrase boundaries marked")
