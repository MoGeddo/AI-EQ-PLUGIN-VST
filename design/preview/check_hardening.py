#!/usr/bin/env python3
"""check_hardening.py - the two attack surfaces this project actually has.

Strix and addyosmani's security-and-hardening skill both insist on the same first
step: name the trust boundaries before writing a single control, because controls
bolted on without a threat model are guesses.  This project has exactly two, and
neither is a web form:

  A. THE PREVIEW SERVER.  design/preview/serve.py serves the repository root - it
     has to, because the pages reference the embedded font with root-absolute URLs.
     The consequence is that "serve the design preview" and "serve .git/config,
     the audio corpus and every dotfile" are one careless line apart.  The trust
     boundary is the URL path.  This gate starts the real server on an ephemeral
     port and *attacks* it: traversal, dotfiles, .git, the corpus, and a control
     request for the panel that must still be 200.

  B. THE BUILD SCRIPTS.  A release script that passes user-supplied strings to a
     shell is a supply-chain bug (and this project ships one .exe per version, so
     the release script is the product).  The checks are static and total: no
     shell=True anywhere, every subprocess call is list-form, the installer picks
     its version apart with a regex, and the code-signing timestamp is fetched over
     TLS - an RFC3161 request answered by an on-path attacker is a timestamp that
     says nothing.

    /tmp/venv/bin/python design/preview/check_hardening.py
"""
from __future__ import annotations

import http.client
import pathlib
import re
import socket
import subprocess
import sys
import time
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[2]
problems: list[str] = []
notes: list[str] = []

# ------------------------------------------------------------------ static checks
# Third-party code is not our attack surface and is not ours to fix: a .venv full of
# numpy helpers that shell out would turn this gate red for ever and teach everyone to
# ignore it.  The boundary is *this repository's own code*.
SKIP = {".git", "build", "node_modules", ".venv", "venv", "dist", "__pycache__"}
PY = [p for p in ROOT.rglob("*.py") if not (SKIP & set(p.parts))]
for p in PY:
    src = p.read_text(errors="ignore")
    rel = p.relative_to(ROOT)
    for m in re.finditer(r"subprocess\.\w+\(([^\n]*)", src):
        call = m.group(1)
        if "shell=True" in call:
            problems.append(f"{rel}: subprocess with shell=True: {call.strip()[:60]}")
        elif call.strip().startswith(("'", '"')) or call.strip().startswith("f'"):
            problems.append(f"{rel}: subprocess with a string command: {call.strip()[:60]}")
    if re.search(r"os\.system\(|os\.popen\(", src):
        problems.append(f"{rel}: os.system/os.popen is used")
notes.append(f"{len(PY)} Python files: no shell and no string commands")

ps1 = ROOT / "installer" / "build_installer.ps1"
iss = ROOT / "installer" / "AEQ_Restore.iss"
if ps1.exists():
    s = ps1.read_text()
    if "Invoke-Expression" in s or re.search(r"\biex\b", s):
        problems.append("build_installer.ps1 uses Invoke-Expression")
    if "-notmatch '^\\d+\\.\\d+\\.\\d+$'" not in s:
        problems.append("build_installer.ps1 does not validate -Version against a semver regex")
    if "https://timestamp" not in s:
        problems.append("the code-signing timestamp is not fetched over TLS")
    else:
        notes.append("version validated, timestamp over TLS")
if iss.exists():
    s = iss.read_text()
    # an uninstaller that deletes a wildcard can delete a user's project
    for m in re.finditer(r"^Type:\s*(filesandordirs|files);\s*Name:\s*\"([^\"]+)\"", s, re.M):
        if "*" in m.group(2) or m.group(2) in ("{app}", "{commonpf64}", "{commonpf64}\\VST3"):
            problems.append(f"the uninstaller deletes too much: {m.group(2)}")

# ------------------------------------------------------------------ the live attack
def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


port = free_port()
proc = subprocess.Popen([sys.executable, str(ROOT / "design" / "preview" / "serve.py"),
                         "--port", str(port), "--host", "127.0.0.1"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    for _ in range(60):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                break
        except OSError:
            time.sleep(0.05)

    def get(path: str):
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        conn.request("GET", path)
        r = conn.getresponse()
        body = r.read(2048)
        conn.close()
        return r.status, body, dict(r.getheaders())

    ATTACKS = [
        ("/../../../../etc/passwd", "path traversal"),
        ("/%2e%2e/%2e%2e/etc/passwd", "encoded traversal"),
        ("/.git/config", "the repository config"),
        ("/.git/", "the git directory"),
        ("/.github/workflows/build-windows.yml", "CI config"),
        ("/google_drive/song.wav", "the audio corpus"),
    ]
    for path, what in ATTACKS:
        status, body, _ = get(path)
        if status == 200 and b"root:" in body:
            problems.append(f"path traversal works: {path}")
        elif status == 200 and what in ("the repository config", "the git directory"):
            problems.append(f"{what} is served at {path}")
        elif status == 200 and path.startswith("/google_drive"):
            problems.append(f"the audio corpus is served at {path}")
    notes.append(f"{len(ATTACKS)} attack requests answered without serving anything private")

    # the controls: the preview itself must still work, or the hardening is a regression
    for path in ("/design/preview/", "/design/preview/ui_v7_1180x700.png",
                 "/plugin/Source/assets/Cairo-Regular.ttf"):
        status, body, headers = get(path)
        if status != 200:
            problems.append(f"the preview is broken: {path} -> {status}")
        if headers.get("X-Content-Type-Options") != "nosniff":
            problems.append(f"{path} is served without nosniff")
    notes.append("3 preview controls still 200 with nosniff")

    # Prove the deny list fired, rather than the file merely not existing: every
    # deny target that is really on disk must answer 403, not 404.
    for rel in (".git/config", ".github/workflows/build-windows.yml"):
        if not (ROOT / rel).exists():
            continue
        status, _, _ = get("/" + rel)
        if status != 403:
            problems.append(f"/{rel} exists on disk and answered {status} - the deny list "
                            "did not fire (a 404 would be luck, not a control)")
    notes.append("deny list proven against files that exist")
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

# ------------------------------------------------------------------ verdict
if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print("hardening ok: " + "; ".join(notes))
