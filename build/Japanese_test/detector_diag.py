"""Assisted segmentation diagnostic, separate from measure.py.

Run: python build/Japanese_test/detector_diag.py
Features use full, untrimmed PCM and explicit half-open sample supports.
A fixed reference interval supplies median RMS; a separate seed selects the
component. Other supplied vowel seeds detect merging. Selection limits never
clip an estimate: merged/truncated candidates have no reported duration.
Energy edges are operational estimates, not the paper's boundary convention.
14/20/26 dB sensitivity is not a confidence interval. No tail or rate is fitted.
"""
from dataclasses import dataclass
import numpy as np

SR = 16000
DB = (14., 20., 26.)
P_MIN = .45

@dataclass(frozen=True)
class Result:
    status: str
    start: int | None = None
    end: int | None = None
    sr: int = SR
    reason: str = ''
    component: tuple | None = None
    core_support: tuple | None = None

    @property
    def duration_ms(self):
        if self.status != 'ok':
            return None
        return 1000. * (self.end-self.start) / self.sr

class Features:
    """Complete windows only, with support on a fixed sample grid."""
    def __init__(self, x, sr=SR):
        self.x = np.asarray(x, dtype=float).copy()
        if sr < 1000 or self.x.ndim != 1 or not np.isfinite(self.x).all():
            raise ValueError('Expected finite mono PCM and sample rate >= 1000')
        self.sr = sr
        self.ew, self.eh = max(1, round(sr*.004)), max(1, round(sr*.001))
        self.pw, self.ph = max(1, round(sr*.032)), max(1, round(sr*.004))
        self.e_start = np.arange(0, max(0, len(self.x)-self.ew+1), self.eh)
        self.e_end = self.e_start+self.ew
        self.rms = np.array([self._rms(self.x[a:b])
                             for a,b in zip(self.e_start,self.e_end)])
        self.p_start = np.arange(0, max(0, len(self.x)-self.pw+1), self.ph)
        self.p_end = self.p_start+self.pw
        windows = [self.x[a:b] for a,b in zip(self.p_start,self.p_end)]
        self.per = np.array([self._nac(w) for w in windows])
        self.p_rms = np.array([self._rms(w) for w in windows])

    @staticmethod
    def _rms(w):
        return float(np.sqrt(np.mean(w*w)))

    def _nac(self, w):
        w = w-np.mean(w)
        if np.dot(w,w) < 1e-9:
            return 0.
        best = 0.
        for lag in range(max(1,int(self.sr/400)), min(int(self.sr/60)+1,len(w))):
            a,b = w[:-lag],w[lag:]
            den = np.sqrt(np.dot(a,a)*np.dot(b,b))
            if den > 1e-9:
                best = max(best,float(np.dot(a,b)/den))
        return best

def reference_level(f, lo, hi):
    """Median of at least three complete energy windows in a fixed interval."""
    if not (0 <= lo < hi <= len(f.x)):
        return None,0
    mask = (f.e_start >= lo) & (f.e_end <= hi)
    n = int(mask.sum())
    if n < 3:
        return None,n
    level = float(np.median(f.rms[mask]))
    return (level if level > 0 else None),n

def candidate(f, seed, ref, db, region=None, excluded_seeds=()):
    """Assisted component or explicit failure, never a clipped duration.

    Excluded seeds identify other vowels, not the expected target endpoint.
    Without them, merging cannot always be detected. Bounds on failed results
    describe the rejected component and are not acoustic measurements.
    """
    def fail(status, reason, component=None):
        return Result(status,sr=f.sr,reason=reason,component=component)
    if ref is None or not np.isfinite(ref) or ref <= 0:
        return fail('insufficient_reference','No positive fixed reference level')
    lo,hi = region if region is not None else (0,len(f.x))
    if not (0 <= lo <= seed < hi <= len(f.x)) or not np.isfinite(db):
        return fail('invalid_selection','Seed must be inside a valid search region')
    if not len(f.rms) or not len(f.per):
        return fail('insufficient_context','No complete feature windows')
    threshold = ref*10.**(-db/20.)
    ok = f.rms > threshold
    k = int(np.argmin(np.abs((f.e_start+f.e_end)/2.-seed)))
    if not (f.e_start[k] <= seed < f.e_end[k]) or not ok[k]:
        return fail('seed_not_qualified','Seed has no energetic energy window')
    i = j = k
    while i > 0 and ok[i-1]:
        i -= 1
    while j+1 < len(ok) and ok[j+1]:
        j += 1
    a,b = int(f.e_start[i]),int(f.e_end[j])
    component = (a,b)
    if any(a <= other < b for other in excluded_seeds):
        return fail('merged_candidate','Component contains another supplied vowel seed',component)
    if a <= lo or b >= hi:
        return fail('boundary_truncated','Component reaches a selection limit',component)
    if i == 0 or j == len(ok)-1:
        return fail('insufficient_context','No observed energy gap on both sides',component)
    # A complete energetic periodic window must contain the seed and fit
    # inside the component. No window-start is mistaken for an offset.
    core = ((f.per >= P_MIN) & (f.p_rms > threshold)
            & (f.p_start <= seed) & (f.p_end > seed)
            & (f.p_start >= a) & (f.p_end <= b))
    matches = np.flatnonzero(core)
    if not len(matches):
        return fail('seed_not_periodic','No complete energetic periodic core containing seed',component)
    c = int(matches[np.argmax(f.per[matches])])
    support = (int(f.p_start[c]),int(f.p_end[c]))
    return Result('ok',a,b,f.sr,component=component,core_support=support)

def sensitivity(results):
    """An invalid candidate cannot become an apparent uncertainty interval."""
    if not results or any(r.status != 'ok' for r in results):
        return {'status':'indeterminate','range_ms':None,
                'statuses':[r.status for r in results]}
    values = [r.duration_ms for r in results]
    return {'status':'parameter_sensitivity','range_ms':(min(values),max(values))}

def steady_span(frames, spans, mora=0):
    """Bounded schedule assistance; never scan ahead into another mora.

    Find constant parameter frames within the allocated steady portion.
    No fallback borrows the following vowel when this portion is too short.
    """
    s = spans[mora]
    st = s['final_steady']
    if st is None:
        return None
    end = min(s['final_end'],st+s['final_len'],len(frames))
    best = None
    i = st+s.get('head',0)
    while i < end:
        j = i+1
        while j < end and frames[j] == frames[i]:
            j += 1
        if frames[i][0] > 32 and (best is None or j-i > best[1]-best[0]):
            best = (i,j)
        i = j
    return best

def report(word, morae):
    import jp_speak as S
    frames,ends,q = S.build(list(morae))
    spans = S.LAST_SPANS
    scheduled = [steady_span(frames,spans,i) for i in range(len(spans))]
    S.pitch(frames,ends,1,q_ends=q,morae=list(morae))
    # Retain the raw origin and allow the final vowel/filter tail to settle.
    silence = S.frame(S.J.V['a'], {0: 0})
    x = S.render(frames+[list(silence) for _ in range(12)],postprocess=False)
    f = Features(x,S.SR)
    fl = S.SR//100
    print(word+' -- assisted, raw PCM; schedule is not acoustic ground truth')
    if scheduled[0] is None:
        print('  insufficient_reference: no scheduled steady interval')
        return
    a,b = scheduled[0]
    lo,hi = S.raw_frame_sample(a+1),S.raw_frame_sample(b-1)
    ref,n = reference_level(f,lo,hi)
    if ref is None:
        print('  insufficient_reference: %d complete reference windows' % n)
        return
    seed = (lo+hi)//2
    others = [S.raw_frame_sample((span[0]+span[1])/2) for span in scheduled[1:] if span]
    results = []
    for db in DB:
        r = candidate(f,seed,ref,db,excluded_seeds=others)
        results.append(r)
        value = '%.2f ms' % r.duration_ms if r.status == 'ok' else r.reason
        print('  %2g dB: %s; %s; component=%s core=%s' %
              (db,r.status,value,r.component,r.core_support))
    print('  across settings:',sensitivity(results))

def main():
    for word,morae in [('bape',['ba','pe']),('bipe',['bi','pe']),
                       ('bupe',['bu','pe']),('gike',['gi','ke'])]:
        report(word,morae)
    print('Detector differences are disagreement, not calibrated bias. No tail or rate was fitted.')

if __name__ == '__main__':
    main()
