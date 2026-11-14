#!/usr/bin/env python3
"""fetch_amir_audio.py - put his 15 dry stems back where the measurement gates expect them.

Why this script exists at all
----------------------------
Eleven gates in this ledger are UNAVAILABLE in a fresh checkout for one reason: the audio
they measure is not in the repository, on purpose.  It is 128 MB of someone else's
recording, it is copyrighted, and `analysis/out/amir15/wav/` is gitignored.  Everything
that can be committed *is* committed - the measurement of every stem (`stems/*.aeqprof`,
104 bands each, taken by this plugin's own analyser), the blended target, the statistics
the thresholds were derived from - so the *numbers* survive a fresh checkout and only the
re-measurement needs the audio back.

The ids of the 15 files live in `analysis/out/amir15/drive_ids.json`, so restoring the
corpus is one command instead of a hunt through a Drive folder:

    .venv/bin/python analysis/scripts/fetch_amir_audio.py            # -> analysis/out/amir15/wav
    .venv/bin/python analysis/scripts/fetch_amir_audio.py --check    # what is missing, no network
    .venv/bin/python analysis/scripts/fetch_amir_audio.py --from ~/Downloads

After it finishes, the gates that were waiting for audio run in order:

    .venv/bin/python analysis/scripts/measure_amir_stems.py analysis/out/amir15/wav --name "AMIR 15"
    .venv/bin/python analysis/scripts/render_loo.py --audio analysis/out/amir15/wav     # G86
    .venv/bin/python analysis/scripts/audit_decisions.py                                  # G9/G12/G13
    .venv/bin/python analysis/scripts/calibrate_warn.py                                   # G84
    python3 scripts/gate_check.py

Honest limits, stated here rather than discovered later:

  * Google serves large files through an interstitial ("cannot scan this file for
    viruses"), so the download needs a confirm token.  This script follows that handshake,
    and says so plainly when a file comes back as HTML instead of audio.
  * It cannot run inside the agent sandbox: outbound HTTP there is limited to package
    registries and github.com, so a sandbox run fails at the TLS handshake.  That is why
    the sandbox reports these gates as "his audio not on this machine" instead of green -
    a missing input is not a passing test.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import sys
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
IDS = ROOT / "analysis" / "out" / "amir15" / "drive_ids.json"
OUT = ROOT / "analysis" / "out" / "amir15" / "wav"


def load_ids() -> list[dict]:
    with open(IDS, encoding="utf-8") as f:
        return json.load(f)["files"]


def safe(name: str) -> str:
    """The stem's own file name, minus anything a filesystem would refuse."""
    return re.sub(r'[\\/:*?"<>|]', "_", name)


def download(file_id: str, dest: pathlib.Path) -> str:
    """Fetch one Drive file, following the large-file confirm handshake."""
    base = "https://drive.google.com/uc?export=download"
    url = f"{base}&id={urllib.parse.quote(file_id)}"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=60) as r:
        blob = r.read()
        if not blob[:4] == b"RIFF" and b"confirm" in blob[:4000].lower():
            # the interstitial: a form with a token and the uuid
            token = re.search(rb'name="confirm"\s+value="([^"]+)"', blob)
            uuid = re.search(rb'name="uuid"\s+value="([^"]+)"', blob)
            if token:
                q = f"https://drive.usercontent.google.com/download?id={file_id}&export=download&confirm={token.group(1).decode()}"
                if uuid:
                    q += f"&uuid={uuid.group(1).decode()}"
                req2 = urllib.request.Request(q, headers={"User-Agent": "Mozilla/5.0"})
                with urllib.request.urlopen(req2, timeout=300) as r2:
                    blob = r2.read()
    if not blob[:4] == b"RIFF" and not blob[:3] == b"ID3" and blob[4:8] != b"ftyp":
        head = blob[:120]
        if head.lstrip().startswith(b"<"):
            return "the server answered with a web page, not audio (sign-in or quota)"
        return "the file is not audio"
    dest.write_bytes(blob)
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="list what is missing and stop")
    ap.add_argument("--from", dest="src", default="", help="a folder you already have the mp3s in")
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args()

    files = load_ids()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    missing = [f for f in files if not (out / safe(f["name"])).exists()]

    print(f"  {len(files) - len(missing)}/{len(files)} stems present in {out.relative_to(ROOT)}")
    if args.check or (not args.src and not missing):
        for f in missing:
            print(f"   missing: {f['name'][:60]}...  ({f['bytes'] / 1e6:.1f} MB)")
        return 0

    if args.src:
        src = pathlib.Path(os.path.expanduser(args.src))
        copied = 0
        for f in files:
            want = src / f["name"]
            if want.exists():
                shutil.copy2(want, out / safe(f["name"]))
                copied += 1
        print(f"  copied {copied} files from {src}")
        if copied < len(files):
            print("  the folder did not hold all 15 names - see analysis/out/amir15/drive_ids.json")
        return 0 if copied else 1

    failed = 0
    for f in missing:
        dest = out / safe(f["name"])
        try:
            why = download(f["id"], dest)
        except Exception as e:                       # noqa: BLE001 - report, do not traceback
            why = f"{type(e).__name__}: {e}"
        if why:
            print(f"  ! {f['name'][:52]}... - {why}")
            failed += 1
        else:
            print(f"  ok {f['name'][:52]}...  ({dest.stat().st_size / 1e6:.1f} MB)")

    if failed:
        print(f"\n  {failed} file(s) did not arrive.  If every one says 'web page', the folder is not")
        print("  shared publicly - download it from Drive once and pass --from <that folder>.")
        return 1
    print("\n  all 15 present.  Next: measure_amir_stems.py, then render_loo.py (G86).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
