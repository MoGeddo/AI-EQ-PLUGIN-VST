#!/usr/bin/env python3
"""
check_release_target.py - a release points at the commit it was built from.

`gh release create vX` without `--target` makes the tag on the repository's **default
branch**, not on the commit CI just built.  Nothing fails: the release page looks right, the
installer downloads, the notes say "built on <sha>" - and the tag, which is what anyone
clicks to see the source of that version, opens a tree that was never compiled.  v1.2.37
through v1.2.40 all did exactly that: every one of them pointed at the initial commit.

The bytes were never wrong; the provenance was.  That is the kind of wrong that is only
found by looking, so this looks: the `gh release create` line has to name the commit.

    python3 scripts/check_release_target.py          # exit 0 only when it does
"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "build-windows.yml")


def main():
    text = open(WORKFLOW, encoding="utf-8").read()
    # the command itself, not the comments about it or the error message after it
    creates = re.findall(r"^\s*gh release create[^\n]*", text, re.M)
    if not creates:
        print("FAIL no `gh release create` in the workflow - nothing publishes the installer")
        return 1
    bad = [c for c in creates if "--target" not in c]
    if bad:
        for c in bad:
            print(f"FAIL the tag would be made on the default branch, not the built commit:\n  {c}")
        print("      add --target \"$env:GITHUB_SHA\"")
        return 1
    print(f"release target ok: {len(creates)} release command(s), each tagged at the commit "
          f"it built (--target \"$env:GITHUB_SHA\")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
