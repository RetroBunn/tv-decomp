"""Minimal PE32 image loader used by the analysis tools.

Maps a PE file into a flat bytearray at its preferred ImageBase and records the
information the disassembler needs: section layout, base relocations (which
dwords hold absolute addresses), imports (IAT slot -> "dll!name") and exports.
"""
import struct
import pefile


class Section:
    def __init__(self, name, va, vsize, raw_size, characteristics):
        self.name = name
        self.va = va
        self.vsize = vsize
        self.raw_size = raw_size
        self.characteristics = characteristics

    @property
    def end(self):
        return self.va + max(self.vsize, self.raw_size)

    @property
    def is_code(self):
        return bool(self.characteristics & 0x20000000)  # IMAGE_SCN_MEM_EXECUTE

    def __repr__(self):
        return "<%s %08x-%08x>" % (self.name, self.va, self.end)


class Image:
    def __init__(self, path):
        self.path = path
        pe = pefile.PE(path)
        self.pe = pe
        oh = pe.OPTIONAL_HEADER
        self.base = oh.ImageBase
        self.size = oh.SizeOfImage
        self.entry = self.base + oh.AddressOfEntryPoint
        self.mem = bytearray(pe.get_memory_mapped_image(ImageBase=self.base))
        if len(self.mem) < self.size:
            self.mem.extend(b"\0" * (self.size - len(self.mem)))

        self.sections = []
        for s in pe.sections:
            self.sections.append(Section(s.Name.rstrip(b"\0").decode("latin1"),
                                         self.base + s.VirtualAddress,
                                         s.Misc_VirtualSize, s.SizeOfRawData,
                                         s.Characteristics))

        # Base relocations: set of VAs holding an absolute 32-bit address.
        self.relocs = set()
        if hasattr(pe, "DIRECTORY_ENTRY_BASERELOC"):
            for blk in pe.DIRECTORY_ENTRY_BASERELOC:
                for e in blk.entries:
                    if e.type == 3:  # IMAGE_REL_BASED_HIGHLOW
                        self.relocs.add(self.base + e.rva)

        # Imports: IAT slot VA -> "dll!name"
        self.imports = {}
        if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
            for imp in pe.DIRECTORY_ENTRY_IMPORT:
                dll = imp.dll.decode("latin1")
                for i in imp.imports:
                    nm = i.name.decode("latin1") if i.name else "ord%d" % i.ordinal
                    self.imports[i.address] = "%s!%s" % (dll.split(".")[0].upper(), nm)

        # Exports: VA -> name
        self.exports = {}
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
            for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
                nm = e.name.decode("latin1") if e.name else "ord%d" % e.ordinal
                self.exports[self.base + e.address] = nm

    # ---- address helpers -------------------------------------------------
    def section_of(self, va):
        for s in self.sections:
            if s.va <= va < s.end:
                return s
        return None

    def in_image(self, va):
        return self.base <= va < self.base + self.size

    def is_code(self, va):
        s = self.section_of(va)
        return s is not None and s.is_code

    def off(self, va):
        return va - self.base

    # ---- readers ----------------------------------------------------------
    def u8(self, va):
        return self.mem[va - self.base]

    def s8(self, va):
        v = self.mem[va - self.base]
        return v - 256 if v & 0x80 else v

    def u16(self, va):
        return struct.unpack_from("<H", self.mem, va - self.base)[0]

    def s16(self, va):
        return struct.unpack_from("<h", self.mem, va - self.base)[0]

    def u32(self, va):
        return struct.unpack_from("<I", self.mem, va - self.base)[0]

    def s32(self, va):
        return struct.unpack_from("<i", self.mem, va - self.base)[0]

    def bytes(self, va, n):
        o = va - self.base
        return bytes(self.mem[o:o + n])

    def cstr(self, va, maxlen=4096):
        o = va - self.base
        end = self.mem.find(b"\0", o, o + maxlen)
        if end < 0:
            return None
        return bytes(self.mem[o:end])
