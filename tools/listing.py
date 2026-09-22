"""Annotated listing generator.

Usage:
  python tools/listing.py <image> <workdir>                 full listing.asm
  python tools/listing.py <image> <workdir> <addr> [...]    print functions

Names are read from tools/names/<image-stem>.txt when present (lines of
"<hexaddr> <name> [# comment]").
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import load_analysis  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def load_names(image_path):
    stem = os.path.splitext(os.path.basename(image_path))[0].lower()
    p = os.path.join(HERE, "names", stem + ".txt")
    names, comments = {}, {}
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            body, _, cmt = line.partition("#")
            parts = body.split()
            if len(parts) >= 2:
                a = int(parts[0], 16)
                names[a] = parts[1]
                if cmt.strip():
                    comments[a] = cmt.strip()
    return names, comments


def printable_string(img, va):
    s = img.cstr(va, 200)
    if s is None or len(s) < 2:
        return None
    if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        t = s.decode("latin1")
        if len(t) > 60:
            t = t[:60] + "..."
        return '"' + t.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + '"'
    return None


class Lister:
    def __init__(self, an, image_path):
        self.an = an
        self.img = an.img
        names, comments = load_names(image_path)
        self.names = dict(an.names)
        self.names.update(names)
        self.comments = comments
        self.callers = an.callers()

    def name(self, va):
        if va in self.names:
            return self.names[va]
        if va in self.img.imports:
            return self.img.imports[va]
        if va in self.an.funcs:
            return "sub_%08x" % va
        sec = self.img.section_of(va)
        if sec and sec.is_code:
            own = self.an.owner.get(va)
            if own is not None:
                return "%s+%x" % (self.name(own), va - own)
            return "code_%08x" % va
        return "g_%08x" % va

    def annotate(self, ins):
        """Return (operand text, comment)."""
        img = self.img
        text = ins.ops
        cmts = []
        # find relocated dwords inside the instruction
        for off in range(ins.size - 3):
            va = ins.addr + off
            if va in img.relocs:
                tgt = img.u32(va)
                hx = "0x%x" % tgt
                nm = self.name(tgt)
                if hx in text:
                    text = text.replace(hx, nm)
                s = printable_string(img, tgt)
                if s:
                    cmts.append(s)
        if ins.mnem == "call" or ins.is_jcc() or ins.mnem == "jmp":
            m = re.fullmatch(r"0x([0-9a-f]+)", ins.ops)
            if m:
                t = int(m.group(1), 16)
                if ins.mnem != "call" and t not in self.an.funcs and                         self.an.owner.get(t) == self.an.owner.get(ins.addr):
                    text = "loc_%08x" % t
                else:
                    text = self.name(t)
        return text, cmts

    def function_text(self, entry):
        an, img = self.an, self.img
        f = an.funcs[entry]
        out = []
        callers = sorted(self.callers.get(entry, ()))
        out.append(";" + "-" * 76)
        out.append("; %s  (%08x, %d bytes)" % (self.name(entry), entry, f.size))
        if entry in self.comments:
            out.append("; " + self.comments[entry])
        if callers:
            out.append("; callers: " + ", ".join(self.name(c) for c in callers[:12]) +
                       (" ..." if len(callers) > 12 else ""))
        refs = sorted(an.code_ptr_refs.get(entry, ()))
        if refs:
            out.append("; address taken at: " + ", ".join("%08x" % r for r in refs[:8]))
        labels = set(f.blocks)
        tables = {}
        for jaddr, (tva, tg) in f.jumptables.items():
            labels.update(tg)
            tables[jaddr] = (tva, tg)
        for a in f.insn_addrs:
            ins = an.insns[a]
            if a in labels and a != entry:
                out.append("loc_%08x:" % a)
            text, cmts = self.annotate(ins)
            line = "  %08x  %-7s %s" % (a, ins.mnem, text)
            if cmts:
                line = "%-70s ; %s" % (line, " ".join(cmts))
            out.append(line)
            if a in tables:
                tva, tg = tables[a]
                out.append("      ; jumptable @%08x: %s" %
                           (tva, " ".join("%d:loc_%08x" % (i, t) for i, t in enumerate(tg))))
        return "\n".join(out)


COMPACT_SUBS = [
    (re.compile(r"\bdword ptr "), "d"), (re.compile(r"\bword ptr "), "w"),
    (re.compile(r"\bbyte ptr "), "b"), (re.compile(r"\bqword ptr "), "q"),
    (re.compile(r"\bsub_10([0-9a-f]{6})\b"), r"s\1"),
    (re.compile(r"\bloc_10([0-9a-f]{6})\b"), r"L\1"),
    (re.compile(r"\bg_10([0-9a-f]{6})\b"), r"g\1"),
    (re.compile(r"\bcode_10([0-9a-f]{6})\b"), r"c\1"),
    (re.compile(r" \+ "), "+"), (re.compile(r" - "), "-"), (re.compile(r" \* "), "*"),
]


def compact(text):
    """Shorter listing: no per-line addresses, short labels/operand sizes."""
    out = []
    for line in text.split(chr(10)):
        m = re.match(r"  ([0-9a-f]{8})  (\S+)\s*(.*)$", line)
        if m:
            line = " %s %s" % (m.group(2), m.group(3))
        elif line.startswith("loc_10") and line.endswith(":"):
            line = "L" + line[6:]
        for rx, rep in COMPACT_SUBS:
            line = rx.sub(rep, line)
        out.append(line.rstrip())
    return chr(10).join(out)


def main():
    args = sys.argv[1:]
    use_compact = "-c" in args
    if use_compact:
        args.remove("-c")
    image, workdir = args[0], args[1]
    an = load_analysis(image, os.path.join(workdir, "analysis.pickle"))
    L = Lister(an, image)
    if len(args) > 2:
        for a in args[2:]:
            va = int(a, 16)
            if va not in an.funcs:
                va = an.owner.get(va, va)
            t = L.function_text(va)
            print(compact(t) if use_compact else t)
            print()
        return
    with open(os.path.join(workdir, "listing.asm"), "w", encoding="utf-8") as fp:
        for e in sorted(an.funcs):
            fp.write(L.function_text(e) + "\n\n")


if __name__ == "__main__":
    main()
