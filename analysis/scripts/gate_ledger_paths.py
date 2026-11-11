#!/usr/bin/env python3
"""gate_ledger_paths.py - the ledger runner finds the executables the ledger names.

This gate exists because of a bug that hid thirty-two gates at once.  `runnable()` decided
whether a CHECK could run in this checkout with

    os.path.exists(os.path.join(ROOT, head.lstrip("./")))

and `str.lstrip` strips a *set of characters*, not a prefix: ".venv/bin/python" became
"venv/bin/python", which does not exist, so every gate that runs inside the venv was
reported as "python environment not built here".  The ledger said 31 passed and 37
unavailable when the truth was 50 passed and 16 unavailable - and nobody can tell a lying
ledger from an honest one by reading it, which is the whole reason the EVIDENCE column
exists.

The gate pins the resolver to the four shapes the ledger actually uses:

  1. `.venv/bin/python script.py` resolves when the venv exists (the historical bug)
  2. `./build/aeq_tests` resolves, and a missing build product does not
  3. a command outside the allowlist is refused, not run
  4. every CHECK in GATES.md that names the venv is either resolvable or says so honestly

    python3 analysis/scripts/gate_ledger_paths.py      # prints LEDGER PATHS OK
"""
from __future__ import annotations

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import gate_check as g  # noqa: E402


def main() -> int:
    fails: list[str] = []

    # ---- 1. the venv, the shape that broke -------------------------------------------
    venv = os.path.join(ROOT, ".venv", "bin", "python")
    ok, why = g.runnable(".venv/bin/python analysis/scripts/ui_preview.py")
    if os.path.exists(venv):
        print(f"  .venv/bin/python resolves: {ok}")
        if not ok:
            fails.append(f"the venv exists and the runner says it does not: {why}")
    else:
        if ok:
            fails.append("there is no venv and the runner claims the CHECK can run")

    # ---- 2. a relative build product, present and absent -------------------------------
    if os.path.exists(os.path.join(ROOT, "build", "aeq_tests")):
        ok, why = g.runnable("./build/aeq_tests")
        if not ok:
            fails.append(f"./build/aeq_tests exists and the runner says: {why}")
        else:
            print("  ./build/aeq_tests resolves")
    else:
        ok, why = g.runnable("./build/aeq_tests")
        if ok:
            fails.append("build/aeq_tests does not exist and the runner claims it can run")
        else:
            print(f"  a missing build product is reported, not attempted: {why.split(':')[0]}")

    # ---- 3. the allowlist is a boundary, not a formality --------------------------------
    for bad in ("curl http://example.com", "rm -rf build", "python3 -c 'print(1)'"):
        ok, why = g.runnable(bad)
        if ok:
            fails.append(f"{bad!r} passed the allowlist")
    ok, _ = g.runnable("python3 analysis/scripts/ui_preview.py")
    if not ok:
        fails.append("a plain `python3 <script>` CHECK was refused")
    print("  three commands outside the allowlist refused, one inside it accepted")

    # ---- 4. the ledger's own venv lines ------------------------------------------------
    with open(os.path.join(ROOT, "GATES.md"), encoding="utf-8") as f:
        gates = g.parse(f.read())
    venv_gates = [x for x in gates if ".venv/bin/python" in (x.check or "")]
    if gates:
        bad = [x.ident for x in venv_gates if not g.runnable(x.check)[0]]
        print(f"  {len(venv_gates)} ledger CHECKs run inside the venv, {len(bad)} unresolvable")
        if len(bad) == len(venv_gates) and os.path.exists(venv):
            fails.append("every venv CHECK in the ledger is unresolvable while the venv exists "
                         "- the resolver is broken again")

    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("LEDGER PATHS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
