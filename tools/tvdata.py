"""The container the engine's constant data is committed in.

OpenTV builds without any Centigram binary, so the tables the engine reads --
letter-to-sound rules, phoneme data, durations, voice parameters -- are kept
in the repository under data/.  This is the format they are kept in, and a
reader that stands in for tools/pe32.Image so tools/gen_data.py cannot tell
the difference.

Only data is stored.  The engine's *code* is not: the .text section is never
copied here, except for the handful of jump tables the compiler left sitting
between functions, which C declarations in src/ name and size explicitly.

Layout, all little-endian:

    magic   8   "OPENTVD1"
    base    u32 the image base the addresses are relative to
    nsec    u32
      name  8   section name, NUL padded
      va    u32
      vsize u32
    nchunk  u32
      va    u32
      len   u32
      data  len bytes
    nreloc  u32
      va    u32  each dword here holds an absolute address

See tools/extract_data.py for how it is produced and NOTICE for whose work
the contents are.
"""
import struct

MAGIC = b"OPENTVD1"


class Section:
    def __init__(self, name, va, vsize):
        self.name = name
        self.va = va
        self.vsize = vsize
        self.raw_size = vsize

    @property
    def end(self):
        return self.va + self.vsize

    def __repr__(self):
        return "<%s %08x-%08x>" % (self.name, self.va, self.end)


class TvData:
    """Stands in for pe32.Image: .base, .sections, .relocs and .mem.

    .mem is the whole address space the image covered, but only the chunks
    that were extracted are populated; everything else, .text above all, is
    zero.  Nothing reads outside the chunks -- gen_data.py works from the
    same section list and symbol addresses the extractor did.
    """

    def __init__(self, path):
        with open(path, "rb") as fp:
            buf = fp.read()
        if buf[:8] != MAGIC:
            raise ValueError("%s is not an OpenTV data file" % path)
        off = 8
        self.base, nsec = struct.unpack_from("<II", buf, off)
        off += 8
        self.sections = []
        for _ in range(nsec):
            name = buf[off:off + 8].rstrip(b"\0").decode("latin1")
            va, vsize = struct.unpack_from("<II", buf, off + 8)
            off += 16
            self.sections.append(Section(name, va, vsize))
        nchunk, = struct.unpack_from("<I", buf, off)
        off += 4
        chunks = []
        top = 0
        for _ in range(nchunk):
            va, n = struct.unpack_from("<II", buf, off)
            off += 8
            chunks.append((va, buf[off:off + n]))
            off += n
            top = max(top, va + n)
        nreloc, = struct.unpack_from("<I", buf, off)
        off += 4
        self.relocs = set(struct.unpack_from("<%dI" % nreloc, buf, off))
        self.size = top - self.base
        self.mem = bytearray(self.size)
        for va, data in chunks:
            self.mem[va - self.base:va - self.base + len(data)] = data
        self.path = path


def write(path, base, sections, chunks, relocs):
    """chunks is [(va, bytes)]; sections is [(name, va, vsize)]."""
    out = [MAGIC, struct.pack("<II", base, len(sections))]
    for name, va, vsize in sections:
        out.append(name.encode("latin1")[:8].ljust(8, b"\0"))
        out.append(struct.pack("<II", va, vsize))
    out.append(struct.pack("<I", len(chunks)))
    for va, data in chunks:
        out.append(struct.pack("<II", va, len(data)))
        out.append(bytes(data))
    rel = sorted(relocs)
    out.append(struct.pack("<I", len(rel)))
    out.append(struct.pack("<%dI" % len(rel), *rel))
    with open(path, "wb") as fp:
        fp.write(b"".join(out))
