#!/usr/bin/env python3
"""
verify_stems.py — inventory the raw/reverb/dry triplet and test whether the
reverb can be removed by power-domain subtraction.

Reads      google_drive/*.mp3
Writes     analysis/out/stem_audit.json   (integrity + alignment)
           analysis/out/reverb_alpha.json (the fitted subtraction model)

The question this answers: is  |raw|^2 ~= |dry|^2 + a*|reverb|^2  ?
If yes, the direct sound is recoverable from the raw + reverb stems, so the
target curve can be built on clean audio without any black-box de-reverb.
If no, we report the residual and say so.

Run:  .venv/bin/python analysis/scripts/verify_stems.py
"""
import glob
import hashlib
import json
import os
import re

import numpy as np
import soundfile as sf

ROOT = "/home/user/AI-EQ-PLUGIN-VST"
AUDIO = os.path.join(ROOT, "google_drive")
OUT = os.path.join(ROOT, "analysis", "out")
N_FFT, HOP = 8192, 2048
BANDS = np.array([40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                  1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                  10000, 12500, 16000], dtype=float)


def classify(name):
    """title + which of the three stems this file is."""
    title = re.split(r"\s*\(Vocals\)", name)[0].strip()
    if "(Reverb)" in name:
        kind = "reverb"
    elif "(Dry)" in name:
        kind = "dry"
    else:
        kind = "raw"
    models = re.findall(r"\((Mel-?Ro[Ff]ormer[^)]*|BS-RoFormer[^)]*)\)", name)
    part = re.search(r"-\s*(Part_\d+)", name)
    return title, kind, models, (part.group(1) if part else None)


def load(path, mono=True):
    x, sr = sf.read(path, always_2d=True, dtype="float32")
    if mono:
        x = x.mean(axis=1)
    return x, sr


def band_power(x, sr):
    """mean power spectral density per 1/3-octave band, in dB."""
    if len(x) < N_FFT:
        return np.full(len(BANDS), -np.inf)
    win = np.hanning(N_FFT).astype(np.float32)
    n_frames = 1 + (len(x) - N_FFT) // HOP
    acc = np.zeros(N_FFT // 2 + 1)
    for i in range(n_frames):
        seg = x[i * HOP: i * HOP + N_FFT] * win
        acc += np.abs(np.fft.rfft(seg)) ** 2
    psd = acc / max(n_frames, 1)
    freqs = np.fft.rfftfreq(N_FFT, 1 / sr)
    out = np.empty(len(BANDS))
    for k, fc in enumerate(BANDS):
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        m = (freqs >= lo) & (freqs < hi)
        out[k] = 10 * np.log10(psd[m].mean() + 1e-20) if m.any() else -np.inf
    return out


def db(x):
    return 20 * np.log10(np.sqrt(np.mean(x.astype(np.float64) ** 2)) + 1e-20)


def main():
    files = sorted(glob.glob(os.path.join(AUDIO, "*.mp3")))
    songs = {}
    for f in files:
        name = os.path.basename(f)
        title, kind, models, part = classify(name)
        ent = songs.setdefault(title, {"title": title, "variants": {}})
        # keep the playdasegunda/becruily run as canonical; a stray older model
        # (e.g. 'Mel-RoFormer by Gabox') must not overwrite it
        prev = ent["variants"].get(kind)
        canon = any("deux by becruily" in m or "playdasegunda" in m for m in models)
        if prev is None or (canon and not prev["canonical"]):
            ent["variants"][kind] = {"file": name, "models": models, "part": part,
                                     "canonical": canon, "path": f}

    audit = {"n_files": len(files), "n_songs": len(songs), "songs": {}, "problems": []}
    alpha_rows = []

    for title, ent in songs.items():
        rec = {"variants": {k: {"file": v["file"], "models": v["models"],
                                "part": v["part"], "canonical": v["canonical"]}
                            for k, v in ent["variants"].items()}}
        have = set(ent["variants"])
        rec["has_all_three"] = have >= {"raw", "reverb", "dry"}
        if not rec["has_all_three"]:
            audit["problems"].append(f"{title}: missing {sorted({'raw','reverb','dry'} - have)}")

        meta = {}
        for kind, v in ent["variants"].items():
            info = sf.info(v["path"])
            meta[kind] = {"dur_s": round(info.duration, 3), "sr": info.samplerate,
                          "ch": info.channels, "frames": info.frames,
                          "sha1_1k": hashlib.sha1(open(v["path"], "rb").read(1024)).hexdigest()[:12]}
        rec["meta"] = meta

        if rec["has_all_three"]:
            frames = {k: meta[k]["frames"] for k in ("raw", "reverb", "dry")}
            rec["frames_equal"] = len(set(frames.values())) == 1
            if not rec["frames_equal"]:
                audit["problems"].append(
                    f"{title}: LENGTH MISMATCH frames={frames} -> "
                    f"{(max(frames.values())/min(frames.values())-1)*100:.1f}% difference")
            rec["models_match"] = all(
                any("deux by becruily" in m for m in meta_set.get("models", []))
                for meta_set in ent["variants"].values())
            if not rec["models_match"]:
                audit["problems"].append(f"{title}: variants come from different separation models")

            # --- the calibration test ---
            if rec["frames_equal"]:
                n = meta["raw"]["frames"]
                raw, sr = load(ent["variants"]["raw"]["path"])
                rev, _ = load(ent["variants"]["reverb"]["path"])
                dry, _ = load(ent["variants"]["dry"]["path"])
                P_raw, P_rev, P_dry = band_power(raw, sr), band_power(rev, sr), band_power(dry, sr)
                lin_raw, lin_rev, lin_dry = 10 ** (P_raw / 10), 10 ** (P_rev / 10), 10 ** (P_dry / 10)
                # alpha that would turn raw into dry:  alpha = (|raw|^2 - |dry|^2) / |reverb|^2
                # (the earlier version used |raw|^2/|reverb|^2, which is algebraically
                #  wrong - it predicts zero and blows the residual up to -300 dB)
                alpha = (lin_raw - lin_dry) / np.maximum(lin_rev, 1e-30)
                usable = (BANDS >= 200) & (BANDS <= 12000)
                diff = P_raw - P_dry                      # what using raw instead of dry costs, in dB
                pred = 10 * np.log10(np.maximum(lin_raw - np.median(alpha) * lin_rev, 1e-30))
                alpha_rows.append({"title": title,
                                   "alpha_median": float(np.median(alpha[usable])),
                                   "alpha_per_band": alpha.tolist(),
                                   "P_raw": P_raw.tolist(), "P_dry": P_dry.tolist(),
                                   "P_rev": P_rev.tolist(),
                                   "raw_minus_dry_db": diff.tolist(),
                                   "pred_residual_db": (pred - P_dry).tolist(),
                                   "rms_raw_db": db(raw), "rms_dry_db": db(dry),
                                   "rms_rev_db": db(rev)})
                rec["reverb_added_db"] = round(db(raw) - db(dry), 2)
                rec["alpha_median"] = round(float(np.median(alpha[usable])), 3)
                rec["raw_vs_dry_rms_usable_db"] = round(float(np.sqrt(np.mean(diff[usable] ** 2))), 2)
                rec["raw_vs_dry_max_usable_db"] = round(float(np.abs(diff[usable]).max()), 2)
                rec["raw_vs_dry_bands_over_deadzone"] = int((np.abs(diff[usable]) > 2.5).sum())

        audit["songs"][title] = rec

    os.makedirs(OUT, exist_ok=True)
    json.dump(audit, open(os.path.join(OUT, "stem_audit.json"), "w"), ensure_ascii=False, indent=1)
    json.dump(alpha_rows, open(os.path.join(OUT, "reverb_alpha.json"), "w"), ensure_ascii=False, indent=1)

    print(f"files={len(files)}  songs={len(songs)}\n")
    hdr = f"{'song':30s} {'raw':>6s} {'dry':>6s} {'rev':>6s} {'rev adds':>8s} {'raw-dry RMS':>11s} {'max':>5s} {'>2.5dB':>6s}"
    print(hdr)
    usable_rms = []
    for title, rec in audit["songs"].items():
        m = rec["meta"]
        d = rec.get("reverb_added_db")
        r = rec.get("raw_vs_dry_rms_usable_db")
        if r is not None:
            usable_rms.append(r)
        print(f"{title[:30]:30s} {m['raw']['dur_s']:6.1f} {m['dry']['dur_s']:6.1f} {m['reverb']['dur_s']:6.1f} "
              f"{(f'{d:+.2f}dB' if d is not None else '  --  '):>8s} "
              f"{(f'{r:.2f}' if r is not None else ' -- '):>11s} "
              f"{(f'{rec[chr(114)+chr(97)+chr(119)+chr(95)+chr(118)+chr(115)+chr(95)+chr(100)+chr(114)+chr(121)+chr(95)+chr(109)+chr(97)+chr(120)+chr(95)+chr(117)+chr(115)+chr(97)+chr(98)+chr(108)+chr(101)+chr(95)+chr(100)+chr(98)]:.2f}' if r is not None else ' --'):>5s} "
              f"{(rec['raw_vs_dry_bands_over_deadzone'] if r is not None else '-'):>6}")
    if usable_rms:
        print(f"\nmedian raw-vs-dry in 200 Hz-12 kHz = {np.median(usable_rms):.2f} dB "
              f"(dead zone is 2.5 dB)")
    print("\nPROBLEMS:")
    for p in audit["problems"] or ["none"]:
        print("  -", p)


if __name__ == "__main__":
    main()
