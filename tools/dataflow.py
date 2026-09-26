"""Read a function's arithmetic as expressions instead of instructions.

Some of what is left in the 1995 engines is signal processing: long runs of
imul/add/sar with no calls to break them up, where the instruction listing
tells you everything except what is being computed.  This walks the listing
keeping a symbolic value for each register and each stack slot, and prints
the expression at every point the function commits one -- by shifting it
down, by storing it, or by saturating it.

The names it prints are where the value came from, not what it means:

    W0x5a     the int16 at [esp+0x5a], sign-extended
    D0x90     the int32 at [esp+0x90]
    E0x06ae   the int16 at [ecx+0x06ae], which is the object

so a line like

    ((W0x5a*W0x36)+2*((W0x12*D0xb8)+(W0x1c*D0x90)))

is one biquad section: two coefficients times the two delayed samples,
doubled, plus a third times the second delay.

Usage:
  python tools/dataflow.py <image> <workdir> <addr> [--from ADDR] [--width N]

--from skips everything below an address, which is how to leave out a
prologue that only shuffles state into locals.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import load_analysis  # noqa: E402
from listing import Lister  # noqa: E402

MOVSX_W = re.compile(r"movsx\s+(\w+),\s*word ptr \[esp \+ (0x[0-9a-f]+)\]$")
MOVSX_E = re.compile(r"movsx\s+(\w+),\s*word ptr \[(?:ecx|esi) \+ (0x[0-9a-f]+)\]$")
MOV_D = re.compile(r"mov\s+(e\w\w),\s*dword ptr \[esp \+ (0x[0-9a-f]+)\]$")
MOV_DE = re.compile(r"mov\s+(e\w\w),\s*dword ptr \[(?:ecx|esi) \+ (0x[0-9a-f]+)\]$")
ST_D = re.compile(r"mov\s+dword ptr \[esp \+ (0x[0-9a-f]+)\],\s*(e\w\w)$")
ST_W = re.compile(r"mov\s+word ptr \[esp \+ (0x[0-9a-f]+)\],\s*(\w\w)$")
IMUL2 = re.compile(r"imul\s+(e\w\w),\s*(.+)$")
IMUL3 = re.compile(r"imul\s+(e\w\w),\s*(\S+),\s*(.+)$")
ADD = re.compile(r"add\s+(e\w\w),\s*(.+)$")
SUB = re.compile(r"sub\s+(e\w\w),\s*(.+)$")
LEA2 = re.compile(r"lea\s+(e\w\w),\s*\[(\w+) \+ (\w+)\*(\d)\]$")
SAR = re.compile(r"sar\s+(e\w\w),\s*(0x[0-9a-f]+|\d+)$")
SHL = re.compile(r"shl\s+(e\w\w),\s*(0x[0-9a-f]+|\d+)$")

#: the 32-bit register a 16-bit name belongs to, so a "mov word [..], di"
#: can be matched up with the "sar edi" that produced it
WIDE = {"ax": "eax", "bx": "ebx", "cx": "ecx", "dx": "edx",
        "si": "esi", "di": "edi", "bp": "ebp"}


def run(path, work, entry, start, width):
    an = load_analysis(path, os.path.join(work, "analysis.pickle"))
    text = Lister(an, path).function_text(entry)
    reg = {}

    def val(x):
        x = x.strip()
        if x in reg:
            return reg[x]
        m = re.match(r"^dword ptr \[esp \+ (0x[0-9a-f]+)\]$", x)
        if m:
            return "D%s" % m.group(1)
        m = re.match(r"^word ptr \[esp \+ (0x[0-9a-f]+)\]$", x)
        if m:
            return "W%s" % m.group(1)
        return x

    n = 0
    for line in text.split("\n"):
        m = re.match(r"^  ([0-9a-f]{8})  (.*)$", line)
        if not m:
            continue
        addr, t = int(m.group(1), 16), m.group(2).strip()
        if addr < start:
            continue
        mm = MOVSX_W.match(t)
        if mm:
            reg[mm.group(1)] = "W%s" % mm.group(2)
            continue
        mm = MOVSX_E.match(t)
        if mm:
            reg[mm.group(1)] = "E%s" % mm.group(2)
            continue
        mm = MOV_D.match(t)
        if mm:
            reg[mm.group(1)] = "D%s" % mm.group(2)
            continue
        mm = MOV_DE.match(t)
        if mm:
            reg[mm.group(1)] = "E%s" % mm.group(2)
            continue
        mm = IMUL3.match(t)
        if mm:
            reg[mm.group(1)] = "(%s*%s)" % (val(mm.group(2)), val(mm.group(3)))
            continue
        mm = IMUL2.match(t)
        if mm:
            reg[mm.group(1)] = "(%s*%s)" % (val(mm.group(1)), val(mm.group(2)))
            continue
        mm = ADD.match(t)
        if mm:
            reg[mm.group(1)] = "(%s+%s)" % (val(mm.group(1)), val(mm.group(2)))
            continue
        mm = SUB.match(t)
        if mm:
            reg[mm.group(1)] = "(%s-%s)" % (val(mm.group(1)), val(mm.group(2)))
            continue
        mm = LEA2.match(t)
        if mm:
            reg[mm.group(1)] = "(%s+%s*%s)" % (val(mm.group(2)), mm.group(4),
                                               val(mm.group(3)))
            continue
        mm = SHL.match(t)
        if mm:
            reg[mm.group(1)] = "(%s<<%s)" % (val(mm.group(1)), mm.group(2))
            continue
        mm = SAR.match(t)
        if mm:
            e = val(mm.group(1))
            print("  %08x  %-4s = (%s) >> %s" % (addr, mm.group(1), e[:width],
                                                 mm.group(2)))
            reg[mm.group(1)] = "R%08x" % addr
            n += 1
            continue
        mm = ST_D.match(t)
        if mm:
            reg["D0x%x" % int(mm.group(1), 16)] = val(mm.group(2))
            continue
        mm = ST_W.match(t)
        if mm:
            wide = WIDE.get(mm.group(2))
            if wide:
                reg["W0x%x" % int(mm.group(1), 16)] = val(wide)
            continue
        # anything else invalidates whatever it writes
        mm = re.match(r"\w+\s+(e\w\w)\b", t)
        if mm:
            reg.pop(mm.group(1), None)
    print("%d committed expressions" % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("work")
    ap.add_argument("addr")
    ap.add_argument("--from", dest="start", default="0")
    ap.add_argument("--width", type=int, default=160)
    a = ap.parse_args()
    run(a.image, a.work, int(a.addr, 16), int(a.start, 16), a.width)


if __name__ == "__main__":
    main()
