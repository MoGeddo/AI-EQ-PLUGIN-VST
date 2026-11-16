#!/usr/bin/env python3
"""
check_name.py - the product has ONE name, and every file that spells it agrees.

The name was changed from "AEQ Restore" to "Geddo AEQ" in 1.2.37.  A rename is the kind of
change that half-happens: the installer says one thing, the window says another, the
workflow reads a path that no longer exists, and the build only fails on Windows, hours
later, with a message about a missing file rather than about a wrong name.  So the name is
read out of the files that decide it and compared, instead of trusted:

  * `PRODUCT_NAME` in plugin/CMakeLists.txt     - the bundle, the .exe and the VST3 folder
  * `#define AppName` in the installer          - every path and every visible string
  * `getName()` in PluginProcessor.h            - the name a DAW shows
  * the two words the editor draws in its header - the name people actually read
  * the artifact, the output file and the .iss path the workflow names

Then the files are searched for the OLD name.  It is allowed to survive in exactly two
kinds of place, and both are counted rather than ignored:

  1. the installer's `[InstallDelete]`, which must name the old bundle to remove it.  An
     upgrade that leaves the old plugin next to the new one gives the host two copies of
     the same plugin id and the user one stale name in the browser.
  2. dated records - audit logs, captured reports of older runs, archived renders.  A
     record that is edited to say a name it did not say is not a record.

Anywhere else, a stale name is a failure, and the file is printed.

    python3 scripts/check_name.py            # exit 0 only when the name is one name
"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OLD = ("AEQ Restore", "AEQ_Restore", "AEQRestore", "AEQ RESTORE")

# Files allowed to still say the old name, with the reason.  Missing files are fine: a
# report that was never regenerated is not an error here, a report that was edited is.
ALLOWED = {
    "installer/Geddo_AEQ.iss":
        "removes the old bundle on upgrade; the old name is the instruction",
    "plugin/validation_report.txt":
        "captured run over his corpus (13 mixes) - cannot be re-made without his audio",
    "analysis/out/amir15/plugin_reports_before.txt":
        "dated snapshot tied to that earlier run",
    "analysis/out/amir15/plugin_reports_after.txt":
        "dated snapshot tied to that earlier run",
    "design/preview/ui_v3.py": "archived render - it must keep drawing what was reviewed",
    "design/preview/ui_v7.py": "archived render - it must keep drawing what was reviewed",
    "docs/22_STATUS_VS_ASK_AR.md": "dated status table: the releases it lists had that file name",
    "docs/23_AUDIT_AR.md": "dated audit log",
    "docs/12_INSTALLER_AR.md":
        "documents the rename itself - it has to be able to say what the old name was",
}

SKIP_DIRS = {".git", "build", ".venv", "dist", "node_modules", "__pycache__", "cache"}
SKIP_EXT = {".png", ".jpg", ".jpeg", ".ico", ".bmp", ".wav", ".mp3", ".vst3", ".exe",
            ".pdf", ".zip", ".aeqprof", ".pyc", ".whl"}


def read(path):
    return open(os.path.join(ROOT, path), encoding="utf-8").read()


def one(pattern, path, what):
    m = re.search(pattern, read(path))
    if not m:
        raise SystemExit(f"FAIL {path}: no {what} found - the name has no source there")
    return m.group(1)


def main():
    problems = []

    product = one(r'PRODUCT_NAME\s+"([^"]+)"', "plugin/CMakeLists.txt", "PRODUCT_NAME")
    appname = one(r'#define AppName\s+"([^"]+)"', "installer/Geddo_AEQ.iss", "AppName")
    getname = one(r'getName\(\)\s*const\s*override\s*\{\s*return\s+"([^"]+)"',
                  "plugin/Source/PluginProcessor.h", "getName()")

    editor = read("plugin/Source/PluginEditor.cpp")
    drawn = re.findall(r'g\.drawText\s*\("([A-Z]+)"', editor)

    # The workflow's copies of the name are the ones that break silently: no compiler reads
    # them, and CI only complains once it reaches the step that uses them.
    wf = read(".github/workflows/build-windows.yml")
    iss_path = re.search(r'Get-Content\s+(\S+\.iss)', wf)
    artifact = re.search(r'\n\s+name:\s+(\S+_Setup)\s*$', wf, re.M)
    outbase = re.search(r'OutputBaseFilename\s*=\s*(\S+?)_\{#AppVersion\}',
                        read("installer/Geddo_AEQ.iss"))
    title = re.search(r'--title\s+"([^"$]+)\s', wf)

    slug = product.replace(" ", "_")

    def same(label, got, want):
        if got != want:
            problems.append(f"{label}: {got!r} != {want!r}")

    same("installer AppName", appname, product)
    same("getName()", getname, product)
    same("drawn header words", " ".join(drawn[:2]), product.upper())
    same("workflow artifact", artifact.group(1) if artifact else None, slug + "_Setup")
    same("OutputBaseFilename", outbase.group(1) if outbase else None, slug + "_Setup")
    same("release title", (title.group(1) if title else None), product)
    if not iss_path:
        problems.append("workflow: no installer path")
    elif not os.path.exists(os.path.join(ROOT, iss_path.group(1))):
        problems.append(f"workflow reads {iss_path.group(1)}, which does not exist")

    # The build scripts find the artefacts by name, so they are part of the name.
    for script in ("installer/build_installer.ps1", "plugin/build_windows.ps1"):
        text = read(script)
        for want in (f"{product}.vst3", f"{product}.exe"):
            if want not in text:
                problems.append(f"{script}: never mentions {want} - it will find nothing")

    # ---- the old name, everywhere else -------------------------------------
    stale = {}
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if os.path.splitext(name)[1].lower() in SKIP_EXT:
                continue
            full = os.path.join(base, name)
            rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
            if rel == "scripts/check_name.py":
                continue        # this file is where the old name is spelled out
            try:
                text = open(full, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            hits = sum(text.count(o) for o in OLD)
            if hits:
                stale[rel] = hits

    for rel in sorted(stale):
        if rel not in ALLOWED:
            problems.append(f"{rel}: still says the old name ({stale[rel]}x)")

    if problems:
        print("FAIL name check:")
        for p in problems:
            print("  -", p)
        return 1

    print(f"name ok: {product} - CMake, installer, getName(), header, workflow and "
          f"build scripts all agree; {len(drawn[:2])}/2 header words; "
          f"{len(stale)} files still say the old name, each one listed with its reason")
    return 0


if __name__ == "__main__":
    sys.exit(main())
