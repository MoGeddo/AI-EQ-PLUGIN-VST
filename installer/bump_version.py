#!/usr/bin/env python3
"""
bump_version.py - change the version number in every place that carries it.

The version lives in four files, and a release is only correct when all four agree:

  installer/AEQ_Restore.iss              #define AppVersion "x.y.z"
  plugin/CMakeLists.txt                  project(AEQRestore VERSION x.y.z ...)
  installer/build_installer.ps1          [string]$Version = "x.y.z"
  .github/workflows/build-windows.yml    -Version x.y.z

CI refuses to build when they disagree (see the "Check the version" step), so this
script exists to make the mistake impossible rather than to fix it after the fact.

Usage:
    python installer/bump_version.py 1.2.0          # bump
    python installer/bump_version.py --check        # verify they agree (exit 1 if not)
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEMVER = re.compile(r"^\d+\.\d+\.\d+$")

# file -> (regex with the version in group 1, replacement template)
SITES = [
    (ROOT / "installer" / "AEQ_Restore.iss",
     re.compile(r'(#define AppVersion ")(\d+\.\d+\.\d+)(")')),
    (ROOT / "plugin" / "CMakeLists.txt",
     re.compile(r"(project\(AEQRestore VERSION )(\d+\.\d+\.\d+)( LANGUAGES)")),
    (ROOT / "installer" / "build_installer.ps1",
     re.compile(r'(\[string\]\$Version = ")(\d+\.\d+\.\d+)(")')),
    (ROOT / ".github" / "workflows" / "build-windows.yml",
     re.compile(r"(-Version )(\d+\.\d+\.\d+)(\s)")),
]


def read_versions():
    out = {}
    for path, rx in SITES:
        text = path.read_text(encoding="utf-8")
        m = rx.search(text)
        out[path.relative_to(ROOT).as_posix()] = m.group(2) if m else None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version", nargs="?", help="new version, e.g. 1.2.0")
    ap.add_argument("--check", action="store_true", help="only verify the files agree")
    args = ap.parse_args()

    found = read_versions()

    if args.check:
        uniq = {v for v in found.values()}
        for path, v in found.items():
            print(f"  {path:<42} {v or 'MISSING'}")
        if None in found.values() or len(uniq) != 1:
            print("FAIL: the version is not consistent across the four files")
            return 1
        print(f"OK: {uniq.pop()}")
        return 0

    if not args.version or not SEMVER.match(args.version):
        print("usage: bump_version.py X.Y.Z   (or --check)")
        return 2

    for path, rx in SITES:
        text = path.read_text(encoding="utf-8")
        if not rx.search(text):
            print(f"FAIL: cannot find the version in {path}")
            return 1
        new = rx.sub(lambda m: m.group(1) + args.version + m.group(3), text, count=1)
        path.write_text(new, encoding="utf-8")
        print(f"  {path.relative_to(ROOT).as_posix():<42} -> {args.version}")

    print(f"\nversion {args.version} written to {len(SITES)} files.")
    print("next: commit, push, and CI publishes the release once the gates pass.")
    print("      (a published version is never overwritten - this is how updates")
    print("       arrive one at a time under Releases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
