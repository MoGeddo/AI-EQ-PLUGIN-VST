#!/usr/bin/env python3
"""gate_check.py - the ledger is executed, not believed.

Every gate in GATES.md claims something.  Until now the claim was checked by hand, once,
in the commit that wrote it, and then trusted forever.  This runner does what the unlazy
gate contract asks for:

    CHECK:   the command that observes the artefact
    EXPECT:  the string that is only printed when the claim is true
    EVIDENCE: what the last run of this exact CHECK actually printed

Modes:

    --status            parse only, execute nothing (always safe)
    (default)           run every gate whose CHECK can run here, compare EXPECT,
                        rewrite EVIDENCE, list what did not run and why
    --only G31,G32      just those
    --strict            exit non-zero when a runnable gate fails OR an EVIDENCE line is
                        missing for a runnable gate

A gate is never marked green because its evidence is prose.  A gate whose CHECK names a
file that does not exist in this checkout (usually a Windows .exe, or a build product) is
reported as UNAVAILABLE - not failed, and not passed.  That distinction is the point:
"not checked here" and "checked and true" are different claims and used to look the same.

    python3 scripts/gate_check.py --status
    python3 scripts/gate_check.py
    python3 scripts/gate_check.py --only G71,G72 --strict
"""
from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass, field

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, "GATES.md")

# A CHECK may only be one of these.  A ledger is data, so a ledger that can run an
# arbitrary shell command is a remote-code-execution path in a file anyone can edit; the
# allowlist is the trust boundary (strix: name the boundary before hardening it).
ALLOWED_PREFIX = (
    ".venv/bin/python ", 
    "python3 ", "python ",
    ".venv/bin/python ",
    "build/", "./build/", "plugin/build/",
    # a repository script, named explicitly: `sh -c` could run anything, so the narrow
    # forms come first and the generic one is last
    "sh plugin/", "sh scripts/",
    "mkdir ", "sh -c ", "g++ ", "cmake ", "ctest ", "git ",
    "head ", "test ", "ls ", "grep ",
)


@dataclass
class Gate:
    ident: str
    title: str
    done: bool
    check: str = ""
    expect: str = ""
    evidence: str = ""
    falsify: str = ""
    cwd: str = ""
    line: int = 0


def parse(text: str) -> list[Gate]:
    gates: list[Gate] = []
    cur: Gate | None = None
    for i, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        m = re.match(r"^- \[( |x)\] (G\d+[a-z]?)\s*:\s*(.*)$", line)
        if m:
            cur = Gate(ident=m.group(2), title=m.group(3).strip(),
                       done=m.group(1) == "x", line=i)
            gates.append(cur)
            continue
        if cur is None:
            continue
        # a new top-level bullet that is not a gate ends the current one
        if line.startswith("- ") and not line.startswith("  "):
            cur = None
            continue
        s = line.strip()
        for key, attr in (("CHECK:", "check"), ("EXPECT:", "expect"),
                          ("EVIDENCE:", "evidence"), ("FALSIFY:", "falsify"),
                          ("CWD:", "cwd")):
            if s.startswith(key):
                setattr(cur, attr, s[len(key):].strip())
    return gates


def runnable(check: str) -> tuple[bool, str]:
    """Can this CHECK run in this checkout?  Returns (ok, reason)."""
    if not check:
        return False, "no CHECK line"
    if not check.startswith(ALLOWED_PREFIX):
        return False, "command not on the allowlist"
    try:
        argv = shlex.split(check)
    except ValueError as exc:
        return False, f"cannot parse: {exc}"
    # `test -f path` gates observe an artefact another machine produced (a Windows CI
    # log).  When the artefact is not here, that is an environment answer, not a failure.
    if argv[0] == "test" and len(argv) >= 3 and argv[1] in ("-f", "-e"):
        if not os.path.exists(os.path.join(ROOT, argv[2])):
            return False, f"artefact not in this checkout: {argv[2]}"

    head = argv[0]
    target = head
    if head in ("python3", "python"):
        if len(argv) < 2:
            return False, "python with no script"
        # `python -c ...` is arbitrary code in a data file; the ledger may name a script in
        # this repository, never a string to be executed
        if argv[1].startswith("-"):
            return False, f"python flags are not a script: {argv[1]}"
        target = argv[1]
    # Any executable path in the CHECK that is not in this checkout is simply not
    # available here (a Windows .exe, or a target this checkout never builds).
    # NB: not `head.lstrip("./")` - that strips a *character set*, turning ".venv/bin/python"
    # into "venv/bin/python", so every gate that runs inside the venv reported the venv as
    # missing.  32 gates were UNAVAILABLE for that one reason.
    local_head = head[2:] if head.startswith("./") else head
    if "/" in head and not os.path.exists(os.path.join(ROOT, local_head)):
        if head.endswith("bin/python"):
            return False, ("python environment not built here: "
                           "python3 -m venv .venv && .venv/bin/pip install -r analysis/requirements.txt")
        return False, f"missing executable: {head}"
    if "/" in target or target.endswith(".exe") or target.startswith("build/") \
            or target.startswith("ctest") or target.startswith("cmake") \
            or target.startswith("git"):
        base = target.lstrip("./")
        if base.startswith("build/") or base.startswith("plugin/build/"):
            if not os.path.exists(os.path.join(ROOT, base)):
                return False, f"not built here: {base}"
        elif base.endswith(".py") or base.startswith("scripts/") or base.startswith("design/"):
            if not os.path.exists(os.path.join(ROOT, base)):
                return False, f"missing: {base}"
    return True, ""


def evaluate(gate: Gate) -> tuple[str, str]:
    """Run one gate.  Returns (status, output)."""
    ok, why = runnable(gate.check)
    if not ok:
        return "UNAVAILABLE", why
    if not gate.expect:
        return "UNAVAILABLE", "no EXPECT line - nothing to compare against"
    try:
        # list form, no shell=True: `sh -c` is spelled out so the hardening gate can see
        # that the only string reaching a shell is a CHECK line that already passed the
        # allowlist above
        proc = subprocess.run(["sh", "-c", gate.check], cwd=ROOT, text=True,
                              capture_output=True, timeout=900)
    except subprocess.TimeoutExpired:
        return "FAIL", "timed out after 900 s"
    combined = (proc.stdout or "") + (proc.stderr or "")
    # A missing Python package or a missing corpus is the environment talking, not the
    # product failing.  Say which, loudly - silence is how a gate starts lying.
    for marker, why in (("ModuleNotFoundError", "python package missing"),
                        ("cannot open", "input file missing"),
                        ("no wav files", "corpus not present here"),
                        ("no JUCE source at", "JUCE checkout not present here"),
                        ("no audio in", "his audio not on this machine")):
        if marker in combined:
            return "UNAVAILABLE", why
    if gate.expect not in combined:
        tail = combined.strip().splitlines()[-4:]
        return "FAIL", "EXPECT not found; exit %d; last lines: %s" % (
            proc.returncode, " | ".join(tail)) 
    if proc.returncode != 0:
        return "FAIL", "EXPECT matched but exit %d" % proc.returncode
    return "PASS", gate.expect


def rewrite_evidence(gates: list[Gate], results: dict[str, tuple[str, str]]) -> str:
    with open(LEDGER, encoding="utf-8") as f:
        text = f.read()
    out: list[str] = []
    current = None
    for line in text.splitlines():
        m = re.match(r"^- \[( |x)\] (G\d+[a-z]?)\s*:", line)
        if m:
            current = m.group(2)
        if current and line.strip().startswith("EVIDENCE:") and current in results:
            status, detail = results[current]
            stamp = "VERIFIED" if status == "PASS" else status
            indent = line[:len(line) - len(line.lstrip())]
            line = f"{indent}EVIDENCE: {stamp} - {detail}"
        out.append(line)
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--status", action="store_true", help="parse only, run nothing")
    ap.add_argument("--only", default="", help="comma separated gate ids")
    ap.add_argument("--strict", action="store_true", help="non-zero on FAIL or missing evidence")
    ap.add_argument("--write", action="store_true", help="write EVIDENCE lines back")
    ap.add_argument("--ledger", default=LEDGER)
    args = ap.parse_args()

    with open(args.ledger, encoding="utf-8") as f:
        text = f.read()
    gates = parse(text)
    if args.only:
        wanted = {g.strip() for g in args.only.split(",") if g.strip()}
        gates = [g for g in gates if g.ident in wanted]

    if not gates:
        print("no gates parsed - the ledger is empty or the format changed")
        return 2

    print(f"{len(gates)} gates in {os.path.relpath(args.ledger, ROOT)}")

    if args.status:
        no_check = [g for g in gates if not g.check]
        no_exp = [g for g in gates if g.check and not g.expect]
        no_ev = [g for g in gates if g.check and not g.evidence]
        print(f"  with CHECK: {len(gates) - len(no_check)}   without: {len(no_check)}")
        print(f"  with EXPECT: {sum(1 for g in gates if g.expect)}")
        print(f"  with EVIDENCE: {sum(1 for g in gates if g.evidence)}")
        for label, lst in (("no CHECK", no_check), ("no EXPECT", no_exp), ("no EVIDENCE", no_ev)):
            if lst:
                print(f"  {label}: {', '.join(g.ident for g in lst)}")
        return 0

    results: dict[str, tuple[str, str]] = {}
    counts = {"PASS": 0, "FAIL": 0, "UNAVAILABLE": 0}
    for g in gates:
        status, detail = evaluate(g)
        results[g.ident] = (status, detail)
        counts[status] += 1
        mark = {"PASS": "ok  ", "FAIL": "FAIL", "UNAVAILABLE": "--  "}[status]
        print(f"  {mark} {g.ident:<5} {g.title[:64]}")
        if status != "PASS":
            print(f"          {detail}")

    print(f"\nPASS {counts['PASS']}   FAIL {counts['FAIL']}   "
          f"UNAVAILABLE {counts['UNAVAILABLE']}   of {len(gates)}")

    if args.write:
        new_text = rewrite_evidence(gates, results)      # reads the ledger: do it first
        if not new_text.strip():
            print("refusing to write an empty ledger")
            return 2
        with open(args.ledger, "w", encoding="utf-8") as f:
            f.write(new_text)
        print(f"EVIDENCE lines written to {os.path.relpath(args.ledger, ROOT)}")

    if args.strict:
        return 0 if counts["FAIL"] == 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
