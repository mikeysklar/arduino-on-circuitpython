#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Mikey Sklar for Adafruit Industries
# SPDX-License-Identifier: MIT
"""Build an Arduino sketch (.ino) into a CircuitPython native module (.mpy).

    ardbuild.py Blink.ino --drive /Volumes/CIRCUITPY
    ardbuild.py Blink.ino --arch armv7emsp

With --drive, the architecture is read from the drive's boot_out.txt and the
module plus the arduino.py runner are copied to its lib/ folder. Then code.py
needs two lines (--code-py writes them for you):

    import arduino
    arduino.run("blink")
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "core"
ARCHES = ("armv6m", "armv7m", "armv7emsp", "armv7emdp", "xtensawin", "rv32imc")

MAKEFILE = """\
MPY_DIR = {mpy_dir}
CORE = {core}
SKETCH_DIR = {sketch_dir}
MOD = {mod}
ARCH = {arch}
SRC = ard_mp.c
LINK_RUNTIME = 1
include $(MPY_DIR)/py/dynruntime.mk
include $(CORE)/cxx.mk
"""

FUNC_DEF = re.compile(
    r"^(?P<ret>(?:[A-Za-z_][\w:<>,\s\*&]*?[\s\*&]))(?P<name>[A-Za-z_]\w*)\s*"
    r"\((?P<args>[^()]*)\)\s*(?:const\s*)?$",
    re.S,
)
NOT_FUNCS = {"if", "for", "while", "switch", "return", "else", "do", "sizeof"}


def blank_comments_and_strings(src):
    """Same length as src, with comments, literals and preprocessor lines blanked."""
    out = []
    i, n = 0, len(src)
    line_start = True
    while i < n:
        c = src[i]
        if line_start and src[i:].lstrip(" \t").startswith("#"):
            # Preprocessor line, including backslash continuations.
            j = i
            while j < n and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j += 1
            out.append(c + " " * (j - i - 2) + c)
            i = j
            continue
        out.append(c)
        line_start = c == "\n" or (line_start and c in " \t")
        i += 1
    return "".join(out)


def find_prototypes(src):
    """Prototypes for top-level function definitions, like the Arduino IDE adds.

    Returns (prototypes, offset of the first function definition).
    """
    clean = blank_comments_and_strings(src)
    protos, first = [], None
    depth, seg_start = 0, 0
    for i, c in enumerate(clean):
        if c == "{":
            if depth == 0:
                head = clean[seg_start:i].strip()
                m = FUNC_DEF.match(head)
                if m and m["name"] not in NOT_FUNCS:
                    ret = " ".join(m["ret"].split())
                    if not re.search(r"\b(class|struct|enum|union|namespace|typedef)\b|=", ret):
                        args = " ".join(m["args"].split())
                        protos.append("{} {}({});".format(ret, m["name"], args))
                        if first is None:
                            first = seg_start + (len(clean[seg_start:i]) - len(clean[seg_start:i].lstrip()))
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                seg_start = i + 1
        elif c == ";" and depth == 0:
            seg_start = i + 1
    return protos, first


def wrap_sketch(ino_path):
    """sketch_all.cpp: the core, then the sketch with prototypes, then the entry points."""
    src = ino_path.read_text()
    protos, first = find_prototypes(src)
    name = ino_path.name
    parts = ['#include "Arduino.h"', '#include "ard_core.cpp"']
    if first is None:
        parts += ['#line 1 "{}"'.format(name), src]
    else:
        line = src.count("\n", 0, first) + 1
        # Start the first definition on its own line so #line stays exact.
        head, tail = src[:first], src[first:]
        parts += ['#line 1 "{}"'.format(name), head]
        parts += protos
        parts += ['#line {} "{}"'.format(line, name), tail]
    parts += [
        "",
        '#line 1 "ardbuild-entry"',
        'extern "C" void ard_call_setup(void) { setup(); }',
        'extern "C" void ard_call_loop(void) { loop(); }',
        "",
    ]
    return "\n".join(parts)


def arch_from_drive(drive):
    boot_out = Path(drive) / "boot_out.txt"
    try:
        text = boot_out.read_text()
    except OSError as e:
        sys.exit("ardbuild: cannot read {}: {}".format(boot_out, e))
    m = re.search(r"^architecture:\s*(\S+)", text, re.M)
    if not m:
        sys.exit(
            "ardbuild: {} has no 'architecture:' line. This needs CircuitPython firmware "
            "that can load native modules; pass --arch to override.".format(boot_out)
        )
    return m.group(1)


def module_name(ino_path):
    name = re.sub(r"\W", "_", ino_path.stem.lower())
    return name if not name[0].isdigit() else "sketch_" + name


def main():
    ap = argparse.ArgumentParser(description="Build an Arduino sketch into a CircuitPython .mpy")
    ap.add_argument("sketch", help="the .ino file, or the sketch folder")
    ap.add_argument("--drive", help="CIRCUITPY drive: read its architecture and install there")
    ap.add_argument("--arch", choices=ARCHES, help="target architecture (default: from --drive)")
    ap.add_argument("--code-py", action="store_true", help="also write code.py to run the sketch")
    ap.add_argument(
        "--mpy-dir", default=os.environ.get("MPY_DIR"), help="CircuitPython source tree (or set $MPY_DIR)"
    )
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not args.mpy_dir or not (Path(args.mpy_dir) / "py" / "dynruntime.mk").is_file():
        sys.exit("ardbuild: point --mpy-dir or $MPY_DIR at a CircuitPython source tree")

    ino = Path(args.sketch).resolve()
    if ino.is_dir():
        ino = ino / (ino.name + ".ino")
    if not ino.is_file():
        sys.exit("ardbuild: no sketch at {}".format(ino))

    arch = args.arch or (arch_from_drive(args.drive) if args.drive else None)
    if arch is None:
        sys.exit("ardbuild: pass --drive (the CIRCUITPY drive) or --arch")
    if arch not in ARCHES:
        sys.exit("ardbuild: unsupported architecture {}".format(arch))
    try:
        import ar  # noqa: F401  (mpy_ld.py, to link libgcc)
        import elftools  # noqa: F401  (mpy_ld.py)
    except ImportError:
        sys.exit("ardbuild: needs two Python packages: pip install pyelftools ar")

    mod = module_name(ino)
    build = ino.parent / "build-ard" / arch
    build.mkdir(parents=True, exist_ok=True)
    (build / "sketch_all.cpp").write_text(wrap_sketch(ino))
    shutil.copy(CORE / "ard_mp.c", build / "ard_mp.c")
    (build / "Makefile").write_text(
        MAKEFILE.format(
            mpy_dir=args.mpy_dir,
            core=CORE,
            sketch_dir=ino.parent,
            mod=mod,
            arch=arch,
        )
    )

    # dynruntime.mk assigns PYTHON itself, so only the command line overrides it.
    cmd = ["make", "-C", str(build), "PYTHON=" + sys.executable] + (["V=1"] if args.verbose else [])
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        sys.exit("ardbuild: build failed")
    if args.verbose:
        sys.stdout.write(result.stdout)

    out = ino.parent / (mod + ".mpy")
    shutil.copy(build / (mod + ".mpy"), out)
    print("built {} ({} bytes, {})".format(out, out.stat().st_size, arch))

    if args.drive:
        lib = Path(args.drive) / "lib"
        lib.mkdir(exist_ok=True)
        shutil.copy(out, lib / out.name)
        shutil.copy(CORE / "arduino.py", lib / "arduino.py")
        print("installed lib/{} and lib/arduino.py".format(out.name))
        run = 'import arduino\narduino.run("{}")\n'.format(mod)
        if args.code_py:
            code = Path(args.drive) / "code.py"
            if code.exists():
                shutil.copy(code, code.with_suffix(".py.bak"))
                print("saved the old code.py as code.py.bak")
            code.write_text(run)
            print("wrote code.py")
        else:
            print("\nTo run it, put this in code.py:\n\n" + run)


if __name__ == "__main__":
    main()
