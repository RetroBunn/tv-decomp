"""Check sapi5/sapi_ddk.h against Microsoft's own sapiddk.h.

Usage: python tools/check_sapi_ddk.py [path/to/sapiddk.h]

mingw-w64 ships no sapiddk.h, so the four declarations a TTS engine needs are
restated in sapi5/sapi_ddk.h by hand.  Two of them are ordering traps that a
compiler cannot catch -- Speak precedes GetOutputFormat in ISpTTSEngine's
vtable, though Microsoft's porting guide describes them the other way round,
and ISpTTSEngineSite extends ISpEventSink so two inherited methods come first
-- and getting either wrong is a crash on the first utterance rather than a
build error.  This reads both files and compares them, so the hand-written one
does not have to be taken on trust.

It needs a Windows SDK, and looks only where one is normally installed.  With
no SDK it says so and exits 0: the check is not always available, and its
absence is not a failure.
"""
import glob
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OURS = os.path.join(HERE, "sapi5", "sapi_ddk.h")

#: Where a Windows SDK or the old Speech SDK puts the header.  Nothing else on
#: the machine is looked at.
SDK_GLOBS = [
    r"C:\Program Files (x86)\Windows Kits\10\Include\*\um\sapiddk.h",
    r"C:\Program Files\Windows Kits\10\Include\*\um\sapiddk.h",
    r"C:\Program Files (x86)\Microsoft Speech SDK 5.1\Include\sapiddk.h",
]
LIB_GLOBS = [
    r"C:\Program Files (x86)\Windows Kits\10\Lib\*\um\x64\sapi.lib",
    r"C:\Program Files\Windows Kits\10\Lib\*\um\x64\sapi.lib",
]

INTERFACES = ["ISpTTSEngine", "ISpTTSEngineSite"]
#: What each interface inherits, whose methods precede its own in the vtable.
BASE_METHODS = {
    "ISpTTSEngine": ["QueryInterface", "AddRef", "Release"],
    "ISpTTSEngineSite": ["QueryInterface", "AddRef", "Release",
                         "AddEvents", "GetEventInterest"],
}


def find(globs):
    for pattern in globs:
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]          # the newest SDK, if several
    return None


def sdk_interface(text, name):
    """(uuid, [method names in declaration order]) from the SDK header."""
    m = re.search(r'MIDL_INTERFACE\("([0-9A-Fa-f-]+)"\)\s*\n\s*'
                  + name + r'\s*:\s*public\s+(\w+)\s*\{(.*?)\n\s*\};',
                  text, re.S)
    if not m:
        return None, None, None
    uuid, base, body = m.group(1), m.group(2), m.group(3)
    methods = re.findall(r'virtual\s+[\w\s\*]+?STDMETHODCALLTYPE\s+(\w+)\s*\(',
                         body)
    return uuid.lower(), base, methods


def ours_interface(text, name):
    """(uuid, [method names in vtable order]) from sapi5/sapi_ddk.h."""
    m = re.search(r'typedef struct ' + name + r'Vtbl\s*\{(.*?)\n\}\s*'
                  + name + r'Vtbl;', text, re.S)
    methods = re.findall(r'\*(\w+)\)\(', m.group(1)) if m else []
    g = re.search(r'DEFINE_GUID\(IID_' + name + r'\s*,\s*([^;]+)\);', text)
    uuid = None
    if g:
        parts = [p.strip() for p in g.group(1).replace("\n", " ").split(",")]
        v = [int(p, 16) for p in parts]
        uuid = "%08x-%04x-%04x-%02x%02x-%s" % (
            v[0], v[1], v[2], v[3], v[4],
            "".join("%02x" % x for x in v[5:]))
    return uuid, methods


def main():
    fails = []
    ours = open(OURS, encoding="utf-8").read()

    sdk_path = sys.argv[1] if len(sys.argv) > 1 else find(SDK_GLOBS)
    if not sdk_path or not os.path.exists(sdk_path):
        print("no sapiddk.h found; a Windows SDK is needed for this check.")
        print("sapi5/sapi_ddk.h is unverified here, which is not a failure --")
        print("it is only that the comparison cannot be made on this machine.")
        return 0
    print("against %s" % sdk_path)
    sdk = open(sdk_path, encoding="utf-8", errors="replace").read()

    for name in INTERFACES:
        sdk_uuid, base, sdk_methods = sdk_interface(sdk, name)
        our_uuid, our_methods = ours_interface(ours, name)
        if sdk_uuid is None:
            fails.append("%s: not found in the SDK header" % name)
            continue
        if our_uuid != sdk_uuid:
            fails.append("%s: IID is %s here, %s in the SDK"
                         % (name, our_uuid, sdk_uuid))
        else:
            print("  %-18s IID %s" % (name, our_uuid))

        want = BASE_METHODS[name] + sdk_methods
        if our_methods != want:
            fails.append("%s: vtable order is\n    %s\n  but the SDK declares\n"
                         "    %s" % (name, ", ".join(our_methods),
                                     ", ".join(want)))
        else:
            print("  %-18s %s" % ("", " -> ".join(sdk_methods)))
        if name == "ISpTTSEngineSite" and base != "ISpEventSink":
            fails.append("%s: the SDK says it extends %s, not ISpEventSink"
                         % (name, base))

    # SPVTEXTFRAG, field for field
    m = re.search(r'typedef struct SPVTEXTFRAG\s*\{(.*?)\}', sdk, re.S)
    n = re.search(r'typedef struct SPVTEXTFRAG \{(.*?)\} SPVTEXTFRAG;',
                  ours, re.S)
    if m and n:
        a = re.findall(r'(\w+)\s*;', m.group(1))
        b = re.findall(r'(\w+)\s*;', n.group(1))
        if a != b:
            fails.append("SPVTEXTFRAG: %s here, %s in the SDK"
                         % (", ".join(b), ", ".join(a)))
        else:
            print("  %-18s %s" % ("SPVTEXTFRAG", ", ".join(a)))

    # the two enumerations
    for enum in ("SPVSKIPTYPE", "SPVESACTIONS"):
        got = {}
        for src, text in (("sdk", sdk), ("ours", ours)):
            m = re.search(r'enum ' + enum + r'\s*\{(.*?)\}', text, re.S)
            vals = {}
            if m:
                for k, v in re.findall(r'(\w+)\s*=\s*\(?\s*([^,\n\)]+)', m.group(1)):
                    vals[k] = eval(v.replace("L", "").strip(), {}, {})
            got[src] = vals
        if got["sdk"] != got["ours"]:
            fails.append("%s: %s here, %s in the SDK"
                         % (enum, got["ours"], got["sdk"]))
        else:
            print("  %-18s %s" % (enum, got["sdk"]))

    # SPDFID_WaveFormatEx is data in sapi.lib rather than a value in a header
    g = re.search(r'DEFINE_GUID\(SPDFID_WaveFormatEx\s*,\s*([^;]+)\);', ours)
    lib = find(LIB_GLOBS)
    if g and lib:
        v = [int(p.strip(), 16) for p in g.group(1).replace("\n", " ").split(",")]
        pat = struct.pack("<IHH", v[0], v[1], v[2]) + bytes(v[3:])
        n = open(lib, "rb").read().count(pat)
        if n != 1:
            fails.append("SPDFID_WaveFormatEx: %d occurrences in %s, wanted 1"
                         % (n, lib))
        else:
            print("  %-18s found in %s" % ("SPDFID_WaveFormatEx",
                                           os.path.basename(lib)))

    print()
    if fails:
        for f in fails:
            print("FAIL " + f)
        return 1
    print("sapi5/sapi_ddk.h agrees with the SDK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
