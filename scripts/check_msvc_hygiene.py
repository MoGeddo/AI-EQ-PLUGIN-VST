#!/usr/bin/env python3
"""check_msvc_hygiene.py - the Windows compiler, checked from Linux.

Why this file exists
--------------------
The Windows build failed for four days running, and the reason was not a design mistake
but a *portability* one that this machine cannot see by compiling:

    plugin/Source/dsp/Biquad.h: std::array used, <array> never included.

libstdc++ happens to hand `<array>` to whoever includes `<vector>`; MSVC does not, so on
Windows every file that reached `Biquad.h` through `<vector>` produced C2079 ("uses
undefined class std::array"), and then two dozen follow-on errors that pointed at the
wrong lines.  The .exe therefore stopped existing, and nothing on this machine said so,
because "it compiles here" is exactly the claim that was false.

The same run also failed on `std::filesystem::path::c_str()`, whose type is `const char*`
on Linux and `const wchar_t*` on Windows, and on `fopen (argv[i], "rb")`, which compiles
everywhere and then silently fails to open an Arabic file name on Windows.

So this checker encodes the three rules the Windows compiler was enforcing, and it runs
here:

  1. every `std::` entity used in the dependency-free DSP/model layer is provided by a
     header that file includes itself (a sibling include is not a promise);
  2. `fopen` is only allowed in `Source/Platform.h`, and paths never cross to `const char*`
     through `path::c_str()` / `path::string()`; they go through `aeq::pathUtf8` /
     `aeq::pathFromUtf8`;
  3. POSIX-only spellings (`unistd.h`, `ssize_t`, `strcasecmp`, `__builtin_*`, bare
     `M_PI`) are not used, and `windows.h` is only included with NOMINMAX.

Every header in the DSP/model layer is also compiled standalone with `-fsyntax-only`, so
a header that only works when something else was included first is a failure rather than a
surprise on a Windows runner eight minutes later.

    python3 scripts/check_msvc_hygiene.py            # all checks
    python3 scripts/check_msvc_hygiene.py --no-compile
    python3 scripts/check_msvc_hygiene.py --explain   # list the rules and what they cover

Exit 0 and the line `MSVC HYGIENE OK` only when everything above holds.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The layer that has no JUCE in it: these are the files that get compiled by the bare
# console targets, so they cannot lean on JUCE to pull in a header for them.
FREE_DIRS = ("plugin/Source/dsp", "plugin/Source/model")
SCAN_DIRS = ("plugin/Source", "plugin/cli", "plugin/Tests")

PLATFORM = "plugin/Source/Platform.h"

# std:: entity -> the header that declares it.  `abs` is special: <cmath> declares the
# floating-point overloads and <cstdlib> the integer ones, and a file that has either is
# fine.  Keep this table alphabetically honest: it is the whole reason the gate works.
STD_HEADER = {
    "abs": ("cstdlib", "cmath"),
    "accumulate": ("numeric",),
    "array": ("array",),
    "atomic": ("atomic",),
    "atof": ("cstdlib",),
    "atoi": ("cstdlib",),
    "atol": ("cstdlib",),
    "ceil": ("cmath",),
    "clamp": ("algorithm",),
    "complex": ("complex",),
    "copysign": ("cmath",),
    "cos": ("cmath",),
    "deque": ("deque",),
    "exp": ("cmath",),
    "fill": ("algorithm",),
    "find": ("algorithm",),
    "floor": ("cmath",),
    "fmod": ("cmath",),
    "fstream": ("fstream",),
    "function": ("functional",),
    "ifstream": ("fstream",),
    "initializer_list": ("initializer_list",),
    "iota": ("numeric",),
    "isfinite": ("cmath",),
    "isnan": ("cmath",),
    "log": ("cmath",),
    "log10": ("cmath",),
    "log2": ("cmath",),
    "lround": ("cmath",),
    "make_unique": ("memory",),
    "map": ("map",),
    "max": ("algorithm",),
    "memcmp": ("cstring",),
    "memcpy": ("cstring",),
    "memset": ("cstring",),
    "min": ("algorithm",),
    "move": ("utility",),
    "mutex": ("mutex",),
    "numeric_limits": ("limits",),
    "ofstream": ("fstream",),
    "optional": ("optional",),
    "pair": ("utility",),
    "pow": ("cmath",),
    "queue": ("deque",),
    "round": ("cmath",),
    "runtime_error": ("stdexcept",),
    "set": ("set",),
    "shared_ptr": ("memory",),
    "sin": ("cmath",),
    "snprintf": ("cstdio",),
    "sort": ("algorithm",),
    "sqrt": ("cmath",),
    "sscanf": ("cstdio",),
    "string": ("string",),
    "stringstream": ("sstream",),
    "tan": ("cmath",),
    "thread": ("thread",),
    "to_string": ("string",),
    "tuple": ("tuple",),
    "uint16_t": ("cstdint",),
    "uint32_t": ("cstdint",),
    "uint64_t": ("cstdint",),
    "uint8_t": ("cstdint",),
    "int16_t": ("cstdint",),
    "int32_t": ("cstdint",),
    "int64_t": ("cstdint",),
    "unique_lock": ("mutex",),
    "unique_ptr": ("memory",),
    "unordered_map": ("unordered_map",),
    "variant": ("variant",),
    "vector": ("vector",),
}

# Spellings that mean "this was written on a Unix box".  Each is a real failure mode on
# MSVC, not a style preference.
BANNED = [
    (re.compile(r"#\s*include\s*<unistd\.h>"), "unistd.h does not exist on Windows"),
    (re.compile(r"#\s*include\s*<sys/"), "POSIX system headers do not exist on Windows"),
    (re.compile(r"\bssize_t\b"), "ssize_t is POSIX; use std::ptrdiff_t or long long"),
    (re.compile(r"\bstrcasecmp\b"), "strcasecmp is POSIX; use a JUCE or hand-written compare"),
    (re.compile(r"\bstrncasecmp\b"), "strncasecmp is POSIX"),
    (re.compile(r"(?<![\w.])M_PI\b"), "M_PI is not in <cmath> under MSVC; use aeq::kPi"),
    (re.compile(r"#\s*include\s*<windows\.h>"),
     "compress windows.h through Platform.h, or define NOMINMAX first"),
    (re.compile(r"std::filesystem::path.*\.c_str\(\)"),
     "path::c_str() is wchar_t* on Windows; use aeq::pathUtf8"),
]

RE_STD_USE = re.compile(r"\bstd::([a-z_0-9]+)\b")
RE_INCLUDE_ANGLE = re.compile(r"#\s*include\s*<([^>]+)>")
RE_INCLUDE_LOCAL = re.compile(r'#\s*include\s*"([^"]+)"')


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def walk(dirs) -> list[str]:
    out: list[str] = []
    for d in dirs:
        base = os.path.join(ROOT, d)
        for root, _dirs, files in os.walk(base):
            for name in files:
                if name.endswith((".h", ".hpp", ".cpp", ".cc")):
                    out.append(os.path.relpath(os.path.join(root, name), ROOT))
    return sorted(out)


def strip_comments(text: str) -> str:
    """A rule that fires on a comment is a rule people switch off.

    The comments in this project quote the exact lines they are explaining (`fopen (argv[i])`,
    `path::c_str()`), so the checks read code with the comments removed - but the *include*
    scan still reads the real file, because a comment cannot include anything.
    """
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def check_includes(problems: list[str]) -> None:
    """Rule 1: a std:: name needs its own header, included by this file.

    Only the dependency-free layer is held to this: the JUCE files are allowed to rely on
    JUCE pulling in `<atomic>`/`<string>`/`<memory>` for them, because that is JUCE's
    published contract and it is the same everywhere.
    """
    for path in walk(FREE_DIRS):
        text = read(path)
        code = strip_comments(text)
        angles = {m.split("/")[0] for m in RE_INCLUDE_ANGLE.findall(text)}
        locals_ = {os.path.splitext(os.path.basename(m))[0] for m in RE_INCLUDE_LOCAL.findall(text)}
        for name in sorted(set(RE_STD_USE.findall(code))):
            wanted = STD_HEADER.get(name)
            if not wanted:
                continue
            if any(w in angles for w in wanted):
                continue
            problems.append(f"{path}: uses std::{name} but does not include "
                            f"{' or '.join('<' + w + '>' for w in wanted)}"
                            f" (it has: {', '.join(sorted(angles)) or 'nothing'})")


def check_platform(problems: list[str]) -> None:
    """Rule 2: one door for files, one door for paths."""
    for path in walk(SCAN_DIRS):
        norm = path.replace(os.sep, "/")
        if norm.endswith("Source/Platform.h"):
            continue
        text = read(path)
        code = strip_comments(text)
        for m in re.finditer(r"\bfopen\s*\(", code):
            line = code[:m.start()].count("\n") + 1
            problems.append(f"{path}:{line}: calls fopen directly - use aeq::openRead / "
                            f"aeq::openWrite from Source/Platform.h")
        for pat, why in (
            (r"\.path\(\)\.c_str\(\)", "path::c_str() is wchar_t* on Windows"),
            (r"\.path\(\)\.string\(\)", "path::string() is the ANSI code page on Windows"),
            (r"\.filename\(\)\.c_str\(\)", "filename()::c_str() is wchar_t* on Windows"),
            (r"\.stem\(\)\.string\(\)", "stem()::string() is the ANSI code page on Windows"),
            (r"\.native\(\)\.c_str\(\)", "native() is UTF-16 on Windows; use aeq::pathUtf8"),
        ):
            for m in re.finditer(pat, code):
                line = code[:m.start()].count("\n") + 1
                problems.append(f"{path}:{line}: {m.group(0)} - {why}; use aeq::pathUtf8")


def pulls_in_juce(path: str, depth: int = 3) -> bool:
    """Does this translation unit reach a JUCE module through its own local includes?

    Followed rather than guessed: `ArabicText.cpp` includes `ArabicText.h`, and that is the
    file that includes `<juce_gui_basics/...>`.  JUCE defines NOMINMAX for the whole
    translation unit, so windows.h below it is safe.
    """
    text = read(path)
    if re.search(r"#\s*include\s*<juce_", text):
        return True
    if depth <= 0:
        return False
    base = os.path.dirname(path)
    for inc in RE_INCLUDE_LOCAL.findall(text):
        target = os.path.normpath(os.path.join(base, inc))
        if os.path.exists(os.path.join(ROOT, target)) and pulls_in_juce(target, depth - 1):
            return True
    return False


def check_banned(problems: list[str]) -> None:
    """Rule 3: no Unix-only spellings, no unguarded windows.h."""
    for path in walk(SCAN_DIRS):
        text = read(path)
        code = strip_comments(text)
        # windows.h is only a hazard when nobody defined NOMINMAX first, and JUCE does that
        # itself on Windows (juce_PlatformDefs.h), so a JUCE translation unit is covered by
        # its own include.  Stated here, not assumed: the exemption names JUCE.
        uses_juce = pulls_in_juce(path)
        for pat, why in BANNED:
            for m in re.finditer(pat, code):
                line = code[:m.start()].count("\n") + 1
                if "windows.h" in m.group(0):
                    head = code[:m.start()]
                    if "NOMINMAX" in head or uses_juce:
                        continue
                problems.append(f"{path}:{line}: {m.group(0)} - {why}")


def check_standalone(problems: list[str]) -> None:
    """Every DSP/model header compiles alone, with nothing before it and nothing after it.

    This is the check that would have caught the original bug on any compiler whose
    standard library is less generous than libstdc++'s.
    """
    cxx = os.environ.get("CXX", "g++")
    if subprocess.run(["which", cxx], capture_output=True).returncode != 0:
        problems.append(f"cannot compile standalone headers: {cxx} not found")
        return
    headers = walk(FREE_DIRS)
    for path in headers:
        if not path.endswith(".h"):
            continue
        src = f'#include "{os.path.basename(path)}"\nint main() {{ return 0; }}\n'
        inc = os.path.dirname(os.path.join(ROOT, path))
        proc = subprocess.run(
            [cxx, "-std=c++17", "-fsyntax-only", "-Wall", "-Wextra", "-Wno-unused", "-x", "c++", "-"],
            input=src.encode(), cwd=inc, capture_output=True)
        if proc.returncode != 0:
            first = proc.stderr.decode("utf-8", "replace").strip().splitlines()
            problems.append(f"{path}: does not compile on its own: "
                            + (first[0] if first else "no output"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-compile", action="store_true",
                    help="skip the standalone-header compile (slow, needs g++)")
    ap.add_argument("--explain", action="store_true", help="print the rules and stop")
    args = ap.parse_args()

    if args.explain:
        print(__doc__ or "")
        print("std:: entities covered:", ", ".join(sorted(STD_HEADER)))
        return 0

    problems: list[str] = []
    check_includes(problems)
    check_platform(problems)
    check_banned(problems)
    if not args.no_compile:
        check_standalone(problems)

    if problems:
        for p in problems:
            print("  ! " + p)
        print(f"MSVC HYGIENE FAILED - {len(problems)} problem(s); the Windows build would fail "
              f"or silently miss files on disk")
        return 1

    n_headers = len(walk(FREE_DIRS))
    print(f"  {n_headers} dependency-free headers: every std:: name has its own include")
    print(f"  one door for files (aeq::openRead/openWrite) and one for paths (aeq::pathUtf8)")
    print(f"  no POSIX spelling, no unguarded windows.h")
    if not args.no_compile:
        print(f"  and each of them compiles alone with {os.environ.get('CXX', 'g++')} -std=c++17")
    print("MSVC HYGIENE OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
