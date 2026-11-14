#!/usr/bin/env python3
"""fetch_ci_ui.py - bring the Windows panel's own evidence into this checkout.

The panel is rendered by Windows, so "the controls work" and "the sentence fits" are
claims that only the Windows build can answer.  CI parks its screenshots and its
`controls.log.txt` on the `ci/ui` branch (the Actions artefact host is not reachable from
every client), and `dist/ui/` is where the build writes them - a gitignored folder, so the
pictures never enter the repository.

Two gates read that log (G64, G66) and were reported as UNAVAILABLE for months because the
log was parked on a branch and the gate looked in the checkout.  This script is the missing
step, named out loud instead of hidden inside a gate:

    python3 scripts/fetch_ci_ui.py             # newest parked run -> dist/ui/
    python3 scripts/fetch_ci_ui.py --print     # ... and show what it says

It is not a gate: it needs the network, and a gate that fails offline teaches people to
ignore it.  Run it, then run `python3 scripts/gate_check.py --only G64,G66`.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "dist", "ui")
BRANCH = "ci/ui"


def git(*args: str, **kw) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, **kw)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--print", dest="show", action="store_true",
                    help="print the control stage's first line and the screenshot names")
    args = ap.parse_args()

    proc = git("fetch", "origin", BRANCH, "--depth=1")
    if proc.returncode != 0:
        print("  cannot fetch " + BRANCH + ": " + proc.stderr.decode("utf-8", "replace").strip())
        return 1

    listing = git("ls-tree", "-r", "--name-only", "FETCH_HEAD")
    names = [n for n in listing.stdout.decode().splitlines()
             if n.startswith("ci-ui/") and n.endswith((".png", ".txt", ".log"))]
    if not names:
        print("  the parked branch has no ci-ui/ artefacts")
        return 1

    run = git("log", "-1", "--format=%s", "FETCH_HEAD").stdout.decode().strip()
    os.makedirs(OUT, exist_ok=True)
    for name in names:
        blob = git("show", f"FETCH_HEAD:{name}")
        if blob.returncode != 0:
            continue
        target = os.path.join(OUT, os.path.basename(name))
        with open(target, "wb") as f:
            f.write(blob.stdout)

    print(f"  {len(names)} artefacts from '{run}' -> dist/ui/")
    if args.show:
        log = os.path.join(OUT, "controls.log.txt")
        if os.path.exists(log):
            with open(log, "r", encoding="utf-8", errors="replace") as f:
                print("  control stage: " + (f.readline().strip() or "(empty)"))
        shots = sorted(n for n in os.listdir(OUT) if n.startswith("ui-") and n.endswith(".png"))
        print("  windows rendered: " + ", ".join(shots))
    return 0


if __name__ == "__main__":
    sys.exit(main())
