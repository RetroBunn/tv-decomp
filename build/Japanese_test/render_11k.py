# -*- coding: utf-8 -*-
"""Re-render every sample at 11025 Hz, the rate TruVoice actually shipped.

Run from the repository root:  python build/Japanese_test/render_11k.py

All of this work was developed at 16 kHz, which the engine supports but is not
what it was built for.  include/tvtts.h is explicit: 11025 Hz is "the engine's
native rate", "what TruVoice shipped as its desktop rate".  So the 11 kHz
rendering is what anyone running TruVoice will actually hear, and it gets a
folder of its own rather than replacing the 16 kHz set.

This re-renders NATIVELY rather than resampling, and the difference is not
cosmetic.  Nyquist falls from 8000 Hz to 5512, which is inside the band the
sibilants and stop bursts live in: /s/ peaks at 4746 and survives, but track 8
carries 86% of its energy above 4 kHz and loses whatever sits past 5512.
Resampling a 16 kHz render would low-pass that away after the fact;
synthesising at 11025 lets the engine do whatever it does, which is the thing
worth listening to.

Frame timing is unaffected -- a frame is 10 ms at any rate -- so durations, the
sweep and the vowel-length regression all stand at either rate.

ONE THING TO KNOW: the engine is substantially louder at 11025.  The same /ki/
peaks at 4616 at 16 kHz and 11568 at 11025, about 2.5 times.  Nothing here
compensates for that, because scaling the output would hide a property of the
engine rather than report it.  One sample clips as a result -- see below.
"""
import os, runpy, shutil, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jp_speak as S

OUT = os.path.join(HERE, '11 kHz')
RATE = 11025


def run_all(gens, rate, label):
    S.SR = rate
    print('\n--- rendering %d generators at %d Hz (%s)' % (len(gens), rate, label))
    for g in gens:
        runpy.run_path(g, run_name='__main__')


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    gens = sorted(glob.glob(os.path.join(HERE, 'make_*.py')))

    run_all(gens, RATE, 'TruVoice native')
    moved = 0
    for w in sorted(glob.glob(os.path.join(HERE, '*.wav'))):
        shutil.move(w, os.path.join(OUT, os.path.basename(w)))
        moved += 1

    # The generators write beside themselves, so collecting a run means moving
    # it afterwards -- which empties the 16 kHz set.  Put it back, or whichever
    # rate was rendered last is the only one that exists.
    run_all(gens, 16000, 'restoring')
    S.SR = 16000

    at11 = sorted(glob.glob(os.path.join(OUT, '*.wav')))
    at16 = sorted(glob.glob(os.path.join(HERE, '*.wav')))
    print('\n%d files at 11025 Hz in %r, %d at 16000 Hz beside the scripts'
          % (len(at11), '11 kHz', len(at16)))

    try:
        import wave
        import numpy as np
        hot = []
        for f in at11:
            with wave.open(f) as w:
                x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
            if len(x) and np.abs(x).max() >= 32700:
                hot.append((os.path.basename(f), int(np.abs(x).max())))
        if hot:
            print('\nclipping at 11025 Hz:')
            for n, p in hot:
                print('   %-30s peak %d' % (n, p))
    except Exception:
        pass


if __name__ == '__main__':
    main()
