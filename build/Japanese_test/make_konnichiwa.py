"""konnichiwa, synthesised as one continuous stream of parameter frames.

Rendered through tvtts_speak_frames: 22 tracks per 10 ms frame, stages 0-3
bypassed.  No splicing, so the glottal phase runs unbroken from the first frame
to the last.  The earlier version had to render one utterance per segment and
concatenate, which restarted the phase at every join -- measured steps of up to
46% of local peak, heard as roughness.

Vowel targets are measured (Mokhtari & Tanaka 2000).  Consonant postures are
from Tanaka (ICPhS 2023) where that paper covers them, and authored from
phonetic principle where it does not; each is marked below.
"""
import ctypes, os, sys, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fujisaki as FJ

os.add_dll_directory(os.path.abspath("build/bin"))
d = ctypes.CDLL(os.path.abspath("build/bin/tvtts64.dll"))
class EV(ctypes.Structure):
    _fields_=[("type",ctypes.c_int32),("count",ctypes.c_uint32),
              ("samples",ctypes.POINTER(ctypes.c_int16)),
              ("mark",ctypes.c_uint32),("sample_pos",ctypes.c_uint32)]
CB=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.POINTER(EV),ctypes.c_void_p)
d.tvtts_create_lang.restype=ctypes.c_void_p
# These two predate the shared-rate convention and hardcoded 16000, so
# render_11k.py could not re-render them and the 11 kHz folder kept the
# 16 kHz versions of samples 01-12.  The rate now comes from jp_speak,
# which is the one place it is set.
import jp_speak as _S
SR=_S.SR; OUT=os.path.join("build","Japanese_test")

def rnd(x): return int(x+0.5)
def cl(v,lo,hi): return max(lo,min(hi,int(v)))

V={'a':(737,1225,2275,3304,170, 99,180),'i':(298,2067,2951,3455, 61,108,126),
   'u':(356,1293,2224,3282, 52,111,106),'e':(481,1873,2406,3381, 54, 88,222),
   'o':(456, 856,2343,3246, 57,101,107)}
C={'n'  :(270,1000,2400,3300,140,220,280),  # N2 1000 (Fujimura), murmur damped
   'w'  :(300, 700,2200,3200, 90,120,180),  # unsourced estimate
   'k_o':(400,1350,2100,3200,110,140,200),  # Tanaka: /k/ before /o/
   'ch' :(300,2450,3000,3500,120,180,240)}  # Tanaka: /sj/ F2 ~2450
BURST={2:80, 0:0}                            # aspiration: CoG ~876, dark
FRIC ={1:80, 8:48, 5:80, 6:80, 0:0}          # [tC]: CoG ~4250, peak 3578
NAS  ={0:52}

def frame(sp, src=None, pitch=108):
    F1,F2,F3,F4,B1,B2,B3 = sp
    t=[0]*22
    t[0]=60; t[16]=14; t[18]=16; t[19]=8
    t[9]=cl(rnd(F1/4.),1,255);  t[10]=cl(rnd((F2-500)/8.),0,255)
    t[11]=cl(rnd(F3/16.),1,255);t[12]=cl(rnd(F4/16.),1,255)
    t[13]=cl(rnd(B1/2.),1,204); t[14]=cl(rnd(B2/2.),1,204)
    t[15]=cl(rnd(B3/2.),1,204)   # track 17 is filled from the contour
    if src:
        for k,v in src.items(): t[k]=v
    return t
def lerp(a,b,f): return tuple(a[k]+(b[k]-a[k])*f for k in range(7))

F=[]
MORA=[]
def mora(): MORA.append(len(F))
def steady(sp,n,src=None,p=108): F.extend(frame(sp,src,p) for _ in range(n))
def glide(a,b,n,src=None,p=108):
    F.extend(frame(lerp(a,b,i/float(max(n-1,1))),src,p) for i in range(n))
def silence(n): F.extend(frame(V['a'],{0:0},108) for _ in range(n))

#   KO            N        NI         CHI        WA
silence(5)                                   # k closure
steady(C['k_o'],4,BURST,100)                 # k burst + aspiration, VOT ~45 ms
glide(C['k_o'],V['o'],4,None,100)
steady(V['o'],6,None,104)
mora()
glide(V['o'],C['n'],4,NAS,112)
steady(C['n'],12,NAS,112)                    # N
mora()
steady(C['n'],5,NAS,110)                     # n
glide(C['n'],V['i'],5,None,110)
steady(V['i'],7,None,108)
mora()
silence(4)                                   # ch closure
steady(C["ch"],5,FRIC,106)                  # ch release
glide(C['ch'],V['i'],5,None,106)
steady(V['i'],5,None,105)
mora()
glide(V['i'],C['w'],3,None,102)
glide(C['w'],V['a'],6,None,100)
steady(V['a'],9,None,97)
mora()

# ---- the F0 contour, from the Fujisaki model -------------------------------
#
# naist-jdic gives konnichiwa as **0/5**: heiban, five morae, no accent nucleus.
# So by Kawai section 7.2 the accent command rises after mora 1 and, this being
# the end of the scope, falls after the final mora -- one broad accent component
# with no downstep inside it.  One phrase command, P1 = 0.35, placed before the
# utterance so its peak (1/alpha = 0.5 s after the command) lands near mora 2,
# which is where Japanese puts the rise.
#
# Fb = 72 Hz is in the range Hirose et al. measured for male readers (58-73).
MORA_END = [m / 100.0 for m in MORA]        # frame index -> seconds
T1 = MORA_END[0]                            # rise: after mora 1
T2 = MORA_END[-1]                           # fall: after the final mora
hz = FJ.contour(len(F), 72.0,
                phrases=[(-0.25, FJ.PHRASE['P1'])],
                accents=[(T1, T2, FJ.ACCENT['FH'])])
for k, f in enumerate(F):
    f[17] = cl(rnd(hz[k] / 2.0), 1, 255)
print("   F0 %.0f-%.0f Hz, heiban: rise after mora 1 at %.2f s, fall at %.2f s"
      % (min(hz), max(hz), T1, T2))

buf=bytes(bytearray(v for f in F for v in f))
arr=(ctypes.c_ubyte*len(buf)).from_buffer_copy(buf)
s=d.tvtts_create_lang(SR,b"en"); out=[]
def cb(ev,u):
    e=ev[0]
    if e.type==0 and e.count: out.extend(e.samples[0:e.count])
    return 0
c=CB(cb)
d.tvtts_speak_frames(ctypes.c_void_p(s), arr, len(F), c, None)
d.tvtts_destroy(ctypes.c_void_p(s))

x=np.array(out,dtype=float)
nz=np.nonzero(np.abs(x)>20)[0]
if len(nz): x=x[max(0,nz[0]-160):nz[-1]+320]
if len(x)>500: x[:160]*=np.linspace(0,1,160); x[-320:]*=np.linspace(1,0,320)
x=np.clip(x,-32768,32767).astype(np.int16)
with wave.open(os.path.join(OUT,"12-konnichiwa.wav"),'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(x.tobytes())
print("12-konnichiwa.wav  %d frames, one call, %.2f s, peak %d"
      % (len(F), len(x)/float(SR), int(np.max(np.abs(x)))))
