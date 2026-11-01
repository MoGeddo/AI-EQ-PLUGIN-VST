"""Run the full corpus measurement -> analysis/out/corpus.json"""
from __future__ import annotations

import argparse
import json
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from aeq import corpus as cp  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", default=os.path.join(ROOT, "google_drive"))
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--only", default=None, help="substring filter on title")
    ap.add_argument("--no-cache", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    cache = None if a.no_cache else os.path.join(a.out, "cache")
    man = cp.build_manifest(a.audio)
    if a.only:
        man = [m for m in man if a.only in m["title"]]
    print(f"{len(man)} songs")
    songs = []
    for i, e in enumerate(man, 1):
        print(f"[{i}/{len(man)}] {e['title']}", flush=True)
        try:
            s = cp.analyze_song(e, cache_dir=cache)
        except Exception as ex:
            print("   FAILED:", ex)
            continue
        songs.append(s)
        print(f"     f0={s['med_f0_hz']:.1f}Hz tilt={s['tilt_db_oct']:+.2f}dB/oct "
              f"LUFS={s['dyn']['lufs']:.1f} PLR={s['dyn']['plr']:.1f} "
              f"wet/dry={s.get('wet',{}).get('wet_over_dry_db',float('nan')):+.1f}dB")
    if not songs:
        print("nothing analysed"); return
    agg = cp.aggregate(songs)
    with open(os.path.join(a.out, "corpus.json"), "w") as f:
        json.dump({"songs": songs, "agg": agg}, f, ensure_ascii=False, indent=1)
    print("wrote", os.path.join(a.out, "corpus.json"))


if __name__ == "__main__":
    main()
