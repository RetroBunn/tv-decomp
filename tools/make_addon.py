"""Package the NVDA add-on.

Usage: python tools/make_addon.py [--32]

An add-on is a zip with manifest.ini at the root, so this copies the tree in
nvda-addon/ and drops the built library in beside the driver.  NVDA is a
64-bit process, so the 64-bit library is the one that goes in; --32 builds
the package for a 32-bit host (an older NVDA, or JAWS) instead.
"""
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "nvda-addon")
OUT = os.path.join(ROOT, "build")


def main():
    bits = 32 if "--32" in sys.argv[1:] else 64
    dll = os.path.join(ROOT, "build", "harness",
                       "tvtts.dll" if bits == 32 else "tvtts64.dll")
    if not os.path.isfile(dll):
        sys.exit("%s is not built; run sh harness/build.sh first" % dll)

    manifest = open(os.path.join(SRC, "manifest.ini"), encoding="utf-8").read()
    m = re.search(r"^version\s*=\s*(\S+)", manifest, re.M)
    version = m.group(1) if m else "0.0.0"

    stage = os.path.join(OUT, "addon")
    shutil.rmtree(stage, ignore_errors=True)
    # Running the test leaves bytecode behind, and it is both useless to
    # NVDA and built by whichever Python happened to be to hand.
    shutil.copytree(SRC, stage, ignore=shutil.ignore_patterns("__pycache__"))
    # The driver asks for "tvtts.dll" whatever it was built as, so the
    # package never has to know which one it got.
    shutil.copy2(dll, os.path.join(stage, "synthDrivers", "tvtts.dll"))

    name = "truvoice-%s%s.nvda-addon" % (version, "" if bits == 64 else "-x86")
    path = os.path.join(OUT, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, _, files in os.walk(stage):
            for f in sorted(files):
                full = os.path.join(dirpath, f)
                z.write(full, os.path.relpath(full, stage).replace(os.sep, "/"))
    print("built %s (%d-bit library, %d bytes)"
          % (os.path.relpath(path, ROOT), bits, os.path.getsize(path)))
    with zipfile.ZipFile(path) as z:
        for n in z.namelist():
            print("   ", n)


if __name__ == "__main__":
    main()
