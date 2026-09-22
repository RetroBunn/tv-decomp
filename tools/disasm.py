"""Recursive-descent disassembler for 32-bit MSVC-compiled PE images.

Usage:  python tools/disasm.py <image.dll> <outdir>

Produces (in outdir):
  functions.txt   one line per function: address, size, insn count, xrefs
  listing.asm     annotated listing, one block per function
  analysis.pickle cached analysis for other tools (see load_analysis)
"""
import os
import pickle
import sys
from collections import defaultdict

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86_const as X

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe32 import Image  # noqa: E402

NORETURN_IMPORTS = {"KERNEL32!ExitProcess", "KERNEL32!ExitThread",
                    "KERNEL32!FatalAppExitA", "KERNEL32!RaiseException"}


class Insn:
    """Compact copy of the capstone instruction fields we care about."""
    __slots__ = ("addr", "size", "mnem", "ops", "bytes", "groups", "operands",
                 "id")

    def __init__(self, ci):
        self.addr = ci.address
        self.size = ci.size
        self.mnem = ci.mnemonic
        self.ops = ci.op_str
        self.bytes = bytes(ci.bytes)
        self.id = ci.id
        self.groups = tuple(ci.groups)
        ops = []
        for o in ci.operands:
            if o.type == X.X86_OP_REG:
                ops.append(("reg", ci.reg_name(o.reg), o.size))
            elif o.type == X.X86_OP_IMM:
                ops.append(("imm", o.imm & 0xFFFFFFFF, o.size))
            elif o.type == X.X86_OP_MEM:
                m = o.mem
                ops.append(("mem",
                            ci.reg_name(m.base) if m.base else None,
                            ci.reg_name(m.index) if m.index else None,
                            m.scale, m.disp & 0xFFFFFFFF, o.size,
                            ci.reg_name(m.segment) if m.segment else None))
        self.operands = tuple(ops)

    @property
    def end(self):
        return self.addr + self.size

    def is_jcc(self):
        return X.X86_GRP_JUMP in self.groups and self.mnem != "jmp"

    def is_jmp(self):
        return self.mnem == "jmp"

    def is_call(self):
        return self.mnem == "call"

    def is_ret(self):
        return self.mnem in ("ret", "retf", "iret", "iretd")


class Function:
    def __init__(self, entry):
        self.entry = entry
        self.blocks = set()        # block start addresses
        self.insn_addrs = []       # sorted instruction addresses
        self.calls = set()         # direct call targets
        self.icalls = []           # (addr, text) indirect calls
        self.imports = set()       # import names called
        self.jumptables = {}       # jmp insn addr -> (table va, [targets])
        self.tailcalls = set()
        self.end = entry
        self.noreturn = False

    @property
    def size(self):
        return self.end - self.entry


class Analysis:
    def __init__(self, img):
        self.img = img
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        self.insns = {}            # addr -> Insn
        self.funcs = {}            # entry -> Function
        self.owner = {}            # insn addr -> function entry
        self.jumptable_ranges = []  # (start, end) of tables inside .text
        self.indextable_ranges = []
        self.names = {}            # va -> name
        self.code_ptr_refs = defaultdict(set)   # target -> set(ref locations)
        self.data_refs = defaultdict(set)       # data va -> set(insn addr)
        self.text_data_refs = defaultdict(set)  # data inside .text -> insns
        self.bad = set()

    # ------------------------------------------------------------------
    def decode(self, addr):
        ins = self.insns.get(addr)
        if ins is not None:
            return ins
        if not self.img.is_code(addr):
            return None
        code = self.img.bytes(addr, 16)
        for ci in self.md.disasm(code, addr, count=1):
            ins = Insn(ci)
            self.insns[addr] = ins
            return ins
        self.bad.add(addr)
        return None

    # ------------------------------------------------------------------
    def jumptable_targets(self, ins, block_insns):
        """Return (table_va, targets) for `jmp dword ptr [reg*4 + table]`."""
        if len(ins.operands) != 1 or ins.operands[0][0] != "mem":
            return None
        _, base, index, scale, disp, size, seg = ins.operands[0]
        if base is not None or index is None or scale != 4:
            return None
        img = self.img
        if not img.is_code(disp) and img.section_of(disp) is None:
            return None
        # Bound from a preceding `cmp reg, N ; ja default` if present.
        bound = None
        idxtable = None
        for prev in reversed(block_insns[-8:]):
            if prev.mnem == "cmp" and len(prev.operands) == 2 and \
                    prev.operands[1][0] == "imm" and prev.operands[0][0] == "reg":
                bound = prev.operands[1][1] + 1
                break
        # Byte index table: mov cl, byte ptr [reg + idx]
        for prev in reversed(block_insns[-4:]):
            if prev.mnem in ("mov", "movzx") and len(prev.operands) == 2 and \
                    prev.operands[1][0] == "mem" and prev.operands[1][5] == 1:
                m = prev.operands[1]
                if m[1] is not None and m[2] is None and img.section_of(m[4]):
                    idxtable = m[4]
                    break
        targets = []
        if idxtable is not None and bound is not None:
            n = max(img.u8(idxtable + i) for i in range(bound)) + 1
            self.indextable_ranges.append((idxtable, idxtable + bound))
        else:
            n = bound
        va = disp
        i = 0
        while True:
            if n is not None and i >= n:
                break
            if va not in img.relocs:
                break
            t = img.u32(va)
            if not img.is_code(t):
                break
            targets.append(t)
            va += 4
            i += 1
            if n is None and i > 1024:
                break
        if not targets:
            return None
        self.jumptable_ranges.append((disp, disp + 4 * len(targets)))
        return disp, targets

    # ------------------------------------------------------------------
    def explore_function(self, entry, func_entries):
        f = Function(entry)
        work = [entry]
        seen_blocks = set()
        f_seen = set()
        while work:
            b = work.pop()
            if b in seen_blocks or b in f_seen:
                f.blocks.add(b)
                continue
            seen_blocks.add(b)
            f.blocks.add(b)
            addr = b
            block = []
            while True:
                if addr != b and addr in func_entries and addr != entry:
                    # Fell through into another function (shouldn't happen
                    # often); treat as tail.
                    break
                if addr != b and addr in f_seen:
                    break
                ins = self.decode(addr)
                if ins is None:
                    break
                f_seen.add(addr)
                block.append(ins)
                if addr in self.owner and self.owner[addr] != entry:
                    # shared code; still record
                    pass
                self.owner.setdefault(addr, entry)
                f.insn_addrs.append(addr)
                # Record references.  Only a relocated *immediate* that points
                # into code is a code pointer (push offset f / mov r, offset f);
                # a relocated memory displacement into .text is embedded data
                # (switch index tables, value lookup tables).
                relocated = set()
                for va in range(addr, ins.end):
                    if va in self.img.relocs:
                        relocated.add(self.img.u32(va))
                for op in ins.operands:
                    if op[0] == "mem" and op[4] in relocated:
                        self.data_refs[op[4]].add(addr)
                        if self.img.is_code(op[4]):
                            self.text_data_refs[op[4]].add(addr)
                    elif op[0] == "imm" and op[1] in relocated:
                        self.data_refs[op[1]].add(addr)
                        if self.img.is_code(op[1]) and not ins.is_call() \
                                and not ins.is_jmp() and not ins.is_jcc():
                            self.code_ptr_refs[op[1]].add(addr)

                if ins.is_ret():
                    break
                if ins.is_call():
                    op = ins.operands[0] if ins.operands else None
                    if op and op[0] == "imm":
                        f.calls.add(op[1])
                        if op[1] in self.noreturn_funcs:
                            break
                    elif op and op[0] == "mem" and op[1] is None and \
                            op[2] is None and op[4] in self.img.imports:
                        name = self.img.imports[op[4]]
                        f.imports.add(name)
                        if name in NORETURN_IMPORTS:
                            break
                    else:
                        f.icalls.append((addr, ins.mnem + " " + ins.ops))
                    addr = ins.end
                    continue
                if ins.is_jmp():
                    op = ins.operands[0]
                    if op[0] == "imm":
                        t = op[1]
                        if t in func_entries and t != entry:
                            f.tailcalls.add(t)
                        else:
                            work.append(t)
                    elif op[0] == "mem" and op[1] is None and op[2] is None \
                            and op[4] in self.img.imports:
                        f.imports.add(self.img.imports[op[4]])
                    else:
                        jt = self.jumptable_targets(ins, block)
                        if jt:
                            f.jumptables[addr] = jt
                            work.extend(jt[1])
                        else:
                            f.icalls.append((addr, "jmp " + ins.ops))
                    break
                if ins.is_jcc():
                    op = ins.operands[0]
                    if op[0] == "imm":
                        work.append(op[1])
                    addr = ins.end
                    continue
                if ins.mnem in ("int3", "hlt"):
                    break
                addr = ins.end
        f.insn_addrs = sorted(set(f.insn_addrs))
        # Contiguous extent starting at entry (chunks far away are reported
        # separately via f.chunks).
        end = entry
        f.chunks = []
        for a in f.insn_addrs:
            if a < entry:
                f.chunks.append(a)
                continue
            if a <= end + 16:
                end = max(end, self.insns[a].end)
            else:
                f.chunks.append(a)
        f.end = end
        return f

    # ------------------------------------------------------------------
    def run(self, extra_seeds=()):
        img = self.img
        self.noreturn_funcs = set()
        seeds = set([img.entry]) | set(img.exports) | set(extra_seeds)
        # Code pointers stored in data (vtables, callback tables, etc.)
        for r in img.relocs:
            s = img.section_of(r)
            tgt = img.u32(r)
            if s is not None and not s.is_code and img.is_code(tgt):
                seeds.add(tgt)
                self.code_ptr_refs[tgt].add(r)
        func_entries = set(seeds)
        pending = list(seeds)
        while pending:
            e = pending.pop()
            if e in self.funcs:
                continue
            f = self.explore_function(e, func_entries)
            self.funcs[e] = f
            for c in f.calls | f.tailcalls:
                if c not in func_entries and img.is_code(c):
                    func_entries.add(c)
                    pending.append(c)
        # Code pointers from within code (push offset func / mov reg, offset)
        changed = True
        while changed:
            changed = False
            for tgt in list(self.code_ptr_refs):
                if tgt not in self.funcs and tgt not in self.owner and \
                        img.is_code(tgt) and not self.in_table(tgt):
                    func_entries.add(tgt)
                    self.funcs[tgt] = self.explore_function(tgt, func_entries)
                    for c in self.funcs[tgt].calls:
                        if c not in self.funcs and img.is_code(c):
                            func_entries.add(c)
                            pending.append(c)
                    changed = True
            while pending:
                e = pending.pop()
                if e in self.funcs:
                    continue
                f = self.explore_function(e, func_entries)
                self.funcs[e] = f
                for c in f.calls | f.tailcalls:
                    if c not in func_entries and img.is_code(c):
                        func_entries.add(c)
                        pending.append(c)
                changed = True
        # default names
        for va, nm in img.exports.items():
            self.names[va] = nm
        for e in self.funcs:
            self.names.setdefault(e, "sub_%08x" % e)
        self.names.setdefault(img.entry, "DllEntry")

    def in_table(self, va):
        for s, e in self.jumptable_ranges:
            if s <= va < e:
                return True
        return False

    # ------------------------------------------------------------------
    def callers(self):
        res = defaultdict(set)
        for e, f in self.funcs.items():
            for c in f.calls | f.tailcalls:
                res[c].add(e)
        return res

    def gaps(self):
        """Ranges of .text not covered by instructions or tables."""
        covered = bytearray(self.img.size)
        for a, ins in self.insns.items():
            if a in self.owner:
                for i in range(ins.size):
                    covered[a - self.img.base + i] = 1
        for s, e in self.jumptable_ranges + self.indextable_ranges:
            for i in range(s, e):
                covered[i - self.img.base] = 1
        out = []
        for sec in self.img.sections:
            if not sec.is_code:
                continue
            start = None
            for va in range(sec.va, sec.va + sec.vsize):
                c = covered[va - self.img.base]
                if not c and start is None:
                    start = va
                elif c and start is not None:
                    out.append((start, va))
                    start = None
            if start is not None:
                out.append((start, sec.va + sec.vsize))
        return out


def save_analysis(an, path):
    img = an.img
    an.img = None
    an.md = None
    with open(path, "wb") as fp:
        pickle.dump(an, fp, protocol=pickle.HIGHEST_PROTOCOL)
    an.img = img


def load_analysis(image_path, pickle_path):
    with open(pickle_path, "rb") as fp:
        an = pickle.load(fp)
    an.img = Image(image_path)
    an.md = Cs(CS_ARCH_X86, CS_MODE_32)
    an.md.detail = True
    return an


def main():
    path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    img = Image(path)
    an = Analysis(img)
    an.run()
    callers = an.callers()
    with open(os.path.join(outdir, "functions.txt"), "w") as fp:
        for e in sorted(an.funcs):
            f = an.funcs[e]
            fp.write("%08x %6d %5d callers=%-3d calls=%-3d imports=%s%s\n" % (
                e, f.size, len(f.insn_addrs), len(callers.get(e, ())),
                len(f.calls), ",".join(sorted(x.split("!")[1] for x in f.imports)),
                " [%s]" % an.names[e] if not an.names[e].startswith("sub_") else ""))
    gaps = an.gaps()
    with open(os.path.join(outdir, "gaps.txt"), "w") as fp:
        for s, e in gaps:
            data = img.bytes(s, e - s)
            if all(b == 0xCC for b in data) or all(b == 0x90 for b in data):
                continue
            fp.write("%08x-%08x %6d %s\n" % (s, e, e - s, data[:24].hex()))
    save_analysis(an, os.path.join(outdir, "analysis.pickle"))
    print("functions:", len(an.funcs), "insns:", len(an.insns),
          "jumptables:", len(an.jumptable_ranges), "gaps:", len(gaps))


if __name__ == "__main__":
    # Re-import under the module name so pickled classes resolve as
    # disasm.Analysis rather than __main__.Analysis.
    import disasm
    disasm.main()
