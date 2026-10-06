import ctypes, os, wave, struct, statistics as st
import numpy as np

os.add_dll_directory(os.path.abspath("build/bin"))
d = ctypes.CDLL(os.path.abspath("build/bin/tvtts64.dll"))
class EV(ctypes.Structure):
    _fields_=[("type",ctypes.c_int32),("count",ctypes.c_uint32),
              ("samples",ctypes.POINTER(ctypes.c_int16)),
              ("mark",ctypes.c_uint32),("sample_pos",ctypes.c_uint32)]
CB=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.POINTER(EV),ctypes.c_void_p)
d.tvtts_create_lang.restype=ctypes.c_void_p

E = chr(27)

# Sustained reference (long/doubled vowels), shared with the speech frontend.
from jp_voice import V as TARGET, U_FRONT, U_BACK

def rnd(x): return int(x + 0.5)

def tracks(F1, F2, F3, F4, B1, B2, B3):
    """Hz -> track bytes, using the scalings measured from the engine."""
    return {9:  rnd(F1 / 4.0),
            10: rnd((F2 - 500) / 8.0),
            11: rnd(F3 / 16.0),
            12: rnd(F4 / 16.0),
            13: rnd(B1 / 2.0),
            14: rnd(B2 / 2.0),
            15: rnd(B3 / 2.0)}

def back(t):
    """track bytes -> the Hz they actually ask for."""
    return (t[9]*4, t[10]*8+500, t[11]*16, t[12]*16, t[13]*2, t[14]*2, t[15]*2)

def seq(t, frames, pitch_track=None):
    s = "".join("%s[%d;%dl" % (E, k, v) for k, v in sorted(t.items()))
    if pitch_track is not None:
        s += "%s[17;%dl" % (E, pitch_track)
    return s + "%s[%dg" % (E, frames)

def render(text, sr, ext=0x7f):
    d.tvtts_set_extensions(ext)
    s = d.tvtts_create_lang(sr, b"en"); buf=[]
    def cb(ev,u):
        e=ev[0]
        if e.type==0 and e.count: buf.extend(e.samples[0:e.count])
        return 0
    c=CB(cb); b=text.encode('latin-1')
    d.tvtts_speak_bytes(ctypes.c_void_p(s), b, len(b), c, None)
    d.tvtts_destroy(ctypes.c_void_p(s))
    return np.array(buf, dtype=np.int16)

def formants(x, sr, order=None, n=4):
    if order is None: order = int(2 + sr/1000)
    x = x.astype(float)
    nz = np.nonzero(np.abs(x) > 20)[0]
    if len(nz) < 2048: return []
    a0 = nz[0] + int(0.10*sr); seg = x[a0:a0+int(0.35*sr)]
    if len(seg) < 1024: return []
    y = np.append(seg[0], seg[1:]-0.97*seg[:-1]) * np.hamming(len(seg))
    r = np.correlate(y,y,'full')[len(y)-1:][:order+1]
    if r[0] <= 0: return []
    a=np.zeros(order+1); a[0]=1.0; e=r[0]
    for i in range(1,order+1):
        k=(r[i]-np.dot(a[1:i],r[i-1:0:-1]))/e
        nw=a.copy(); nw[1:i]=a[1:i]-k*a[i-1:0:-1]; nw[i]=k
        a=nw; e*=(1-k*k)
        if e<=0: return []
    out=[]
    for z in np.roots(np.concatenate(([1.0],-a[1:]))):
        if np.imag(z)<=0: continue
        f=np.arctan2(np.imag(z),np.real(z))*sr/(2*np.pi)
        bw=-np.log(max(abs(z),1e-12))*sr/np.pi
        if 120<f<sr/2-200 and bw<1000: out.append(f)
    return sorted(out)[:n]

# ---- does the track->Hz mapping hold at 16 kHz as well as 11025? ----
print("Checking the scaling holds at both rates (vowel /a/):")
for sr in (11025, 16000):
    t = tracks(*TARGET['a'])
    x = render(seq(t, 70), sr)
    f = formants(x, sr)
    want = back(t)
    print("  %5d Hz  asked %4d %4d %4d %4d   got %s"
          % (sr, want[0], want[1], want[2], want[3],
             "  ".join("%4.0f" % v for v in f)))

# These two predate the shared-rate convention and hardcoded 16000, so
# render_11k.py could not re-render them and the 11 kHz folder kept the
# 16 kHz versions of samples 01-12.  The rate now comes from jp_speak,
# which is the one place it is set.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jp_speak as _S
SR = _S.SR
OUT = os.path.join("build", "Japanese_test")
os.makedirs(OUT, exist_ok=True)

def trim(x, sr, tail=0.06):
    nz = np.nonzero(np.abs(x) > 20)[0]
    if not len(nz): return x
    return x[nz[0]: min(len(x), nz[-1] + int(tail*sr))]

def fade(x, sr, ms=12):
    n = int(sr*ms/1000.0)
    if len(x) < 2*n: return x
    y = x.astype(float)
    y[:n] *= np.linspace(0, 1, n)
    y[-n:] *= np.linspace(1, 0, n)
    return y.astype(np.int16)

def write(name, chunks, sr=SR, gap=0.18):
    sil = np.zeros(int(sr*gap), dtype=np.int16)
    parts = []
    for i, c in enumerate(chunks):
        if i: parts.append(sil)
        parts.append(c)
    data = np.concatenate(parts) if parts else np.zeros(1, dtype=np.int16)
    with wave.open(os.path.join(OUT, name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(data.tobytes())
    return len(data)/float(sr)

def vowel(F1, F2, F3, F4, B1, B2, B3, frames=75, sr=SR):
    t = tracks(F1, F2, F3, F4, B1, B2, B3)
    return t, fade(trim(render(seq(t, frames), sr), sr), sr)

# ---- what the engine's own English /uw/ does, measured, for the A/B ----
eng = render("%s[1Ib%s[0I" % (E, E), SR)
ef = formants(eng, SR)
ENG_U_F2 = ef[1] if len(ef) > 1 else None
print()
print("The engine's own English /uw/ (phoneme `b`), measured: %s"
      % ("  ".join("%4.0f" % v for v in ef) if ef else "(no reading)"))

print()
print("Japanese vowels: target -> track byte -> what that asks for -> measured")
print("      F1              F2              F3              F4")
rows = []
for v in "aiueo":
    t, x = vowel(*TARGET[v])
    w = back(t); f = formants(x, SR)
    rows.append((v, t, x, w, f))
    line = "  %s " % v
    for k in range(4):
        got = f[k] if len(f) > k else 0
        line += "%4d>%3d>%4d=%4.0f  " % (TARGET[v][k], t[9+k], w[k], got)
    print(line)
    write("%02d-%s.wav" % ("aiueo".index(v)+1, v), [x])

write("06-aiueo.wav", [r[2] for r in rows])

# ---- /u/, the two allophones ----
uf = TARGET['u']
t_fr, x_fr = vowel(uf[0], U_FRONT, uf[2], uf[3], uf[4], uf[5], uf[6])
t_bk, x_bk = vowel(uf[0], U_BACK,  uf[2], uf[3], uf[4], uf[5], uf[6])
write("07-u-fronted.wav", [x_fr])
write("08-u-backed.wav",  [x_bk])
write("09-u-allophones.wav", [x_fr, x_bk, x_fr, x_bk])
print()
print("/u/ allophones: fronted F2 %d -> %d measured;  backed F2 %d -> %d measured"
      % (U_FRONT, formants(x_fr, SR)[1], U_BACK, formants(x_bk, SR)[1]))

# ---- Japanese /u/ against a borrowed back-rounded one ----
if ENG_U_F2:
    t_bor, x_bor = vowel(uf[0], ENG_U_F2, uf[2], uf[3], uf[4], uf[5], uf[6])
    _, x_jp = vowel(*uf)
    write("10-u-japanese-vs-borrowed.wav", [x_jp, x_bor, x_jp, x_bor])
    print("borrowed /u/ at the engine's own English F2 of %d Hz, against "
          "Japanese 1293" % ENG_U_F2)

print()
for fn in sorted(os.listdir(OUT)):
    print("   %s  %d bytes" % (fn, os.path.getsize(os.path.join(OUT, fn))))

# ---- mora pacing: Japanese runs ~7 morae/s, so ~140 ms a mora ----
mora = []
for v in "aiueo":
    t = tracks(*TARGET[v])
    mora.append(fade(trim(render(seq(t, 14), SR), SR), SR, ms=10))
write("11-aiueo-mora-paced.wav", mora, gap=0.02)

# ---- peak check ----
print()
print("peak levels (16 bits hold 32767):")
for fn in sorted(os.listdir(OUT)):
    if not fn.endswith('.wav'): continue
    with wave.open(os.path.join(OUT, fn), 'rb') as w:
        a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    print("   %-32s peak %6d   %5.2f s" % (fn, np.max(np.abs(a)), len(a)/float(SR)))
