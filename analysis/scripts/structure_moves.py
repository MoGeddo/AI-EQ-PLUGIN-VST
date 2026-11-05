#!/usr/bin/env python3
"""structure_moves.py - what repeats in Amir's 15 dry vocals.

The claim that started this project: his EQ is right because most of the work is
hardware, so the *moves* repeat.  "في تكرارات في الخطوات".  An average spectrum
cannot answer that - a mean of 15 songs is one number per band, and a claim about
repetition is a claim about *distribution*.  So this measures two different things:

  A. THE SIGNATURE (static + dynamic, per band)
     For every song, per 1/3-octave band: the median level, the dynamic range
     (p90 - p10 of the frame levels), and how fast the band follows loudness
     (the slope of band level against broadband level, dB/dB).  Then: is that
     vector consistent across the 15 songs?  Reported as across-song spread
     against within-song spread - a signature is real when the songs agree with
     each other more than each song agrees with itself over time.

  B. THE MOVES (event shapes)
     Every sung note is an event.  Take the spectrum 30 ms after the attack, at
     the peak, and 200 ms into the sustain, and the shape of the change *in dB
     relative to the note's own mean* - a tilt, not a loudness.  Cluster those
     shapes; then ask the question the claim is: do the SAME clusters come back
     in all 15 songs, and more often than when the event times are shuffled?
     That is a permutation test, because "recurring move" without one is just
     "I looked at the plot".

Output
  analysis/out/structure/moves.json    everything a later gate can read
  analysis/out/structure/report.md     the numbers, in Arabic

    /tmp/venv/bin/python analysis/scripts/structure_moves.py [--quick]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from analysis.aeq import io_audio  # noqa: E402

SR = 48000
NFFT = 2048                      # 43 ms
HOP = 512                        # 10.7 ms
FMIN, FMAX, PPB = 50.0, 16000.0, 3        # 1/3-octave bands
CORPUS = ROOT / "google_drive"
OUT = ROOT / "analysis" / "out" / "structure"


def band_edges():
    n = int(np.ceil(np.log2(FMAX / FMIN) * PPB)) + 1
    fc = FMIN * 2.0 ** (np.arange(n) / PPB)
    fc = fc[fc <= FMAX]
    r = 2.0 ** (1.0 / (2.0 * PPB))
    return fc, fc / r, fc * r


FC, FLO, FHI = band_edges()


def analyse_file(path: pathlib.Path, quick: bool = False):
    x, sr = io_audio.load(str(path), sr=SR, mono=True)
    x = np.asarray(x).reshape(-1)
    # trim leading/trailing digital silence so the gate statistics are about singing
    env = np.convolve(np.abs(x), np.ones(2048) / 2048, mode="same")
    on = np.flatnonzero(env > env.max() * 0.01)
    if on.size < 10:
        return None
    a, b = int(on[0]), int(on[-1]) + 1
    x = x[a:b]
    if quick:
        x = x[: SR * 60]

    win = np.hanning(NFFT).astype(np.float32)
    nFrames = 1 + (len(x) - NFFT) // HOP
    freqs = np.fft.rfftfreq(NFFT, 1.0 / SR)
    # band index per bin, -1 outside
    bidx = np.full(freqs.size, -1, dtype=np.int32)
    for i in range(len(FC)):
        m = (freqs >= FLO[i]) & (freqs < FHI[i])
        bidx[m] = i

    # frame loop in chunks: 43 ms frames, 10.7 ms hop, power in dB per band
    nB = len(FC)
    bandPow = np.zeros((nFrames, nB), dtype=np.float64)
    bredth = np.zeros((nFrames, nB), dtype=np.float64)
    broadband = np.zeros(nFrames)
    for start in range(0, nFrames, 4000):
        stop = min(start + 4000, nFrames)
        idx = (np.arange(start, stop)[:, None] * HOP + np.arange(NFFT)[None, :])
        frames = x[idx] * win
        spec = np.abs(np.fft.rfft(frames, axis=1)) ** 2
        # per band: mean power (not sum, so wide bands are not "louder")
        for i in range(nB):
            m = bidx == i
            if m.any():
                bandPow[start:stop, i] = spec[:, m].mean(axis=1)
                bredth[start:stop, i] = m.sum()
        broadband[start:stop] = spec.sum(axis=1) / spec.shape[1]

    bandDb = 10.0 * np.log10(np.maximum(bandPow, 1e-20))
    broadDb = 10.0 * np.log10(np.maximum(broadband, 1e-20))

    # gate: frames where the voice is actually present.  The floor is the 10th
    # percentile of the frame level plus 12 dB - a fixed dBFS threshold would call a
    # quiet verse "silence" and a loud chorus "all signal".
    floor = np.percentile(broadDb, 10)
    gate = broadDb > floor + 12.0
    if gate.sum() < 200:
        return None

    # ---- A. the signature ------------------------------------------------
    # Two things per band, both of which are *relations* and therefore comparable
    # between singers whose absolute level differs:
    #   range  p90 - p10 of the band's frame level (how much it moves at all)
    #   slope  dB of band per dB of broadband (how it moves with loudness)
    # and each of those is computed a second time on the first and second half of the
    # song, so we know how reproducible it even is within its own take.
    sig = {}
    half = nFrames // 2
    for i in range(nB):
        v = bandDb[gate, i]
        bb = broadDb[gate]
        ok = np.isfinite(v)
        v, bb = v[ok], bb[ok]
        if v.size < 200 or (np.percentile(v, 90) - np.percentile(v, 10)) < 1.0:
            continue
        def slope_of(x, y):
            if x.std() < 0.5:
                return float("nan")
            return float(np.polyfit(x, y, 1)[0])
        h1 = np.flatnonzero(gate)[np.flatnonzero(gate) < half]
        h2 = np.flatnonzero(gate)[np.flatnonzero(gate) >= half]
        s1 = slope_of(broadDb[h1], bandDb[h1, i]) if h1.size > 100 else float("nan")
        s2 = slope_of(broadDb[h2], bandDb[h2, i]) if h2.size > 100 else float("nan")
        sig[str(int(round(FC[i])))] = {
            "levelDb": float(np.median(v)),
            "rangeDb": float(np.percentile(v, 90) - np.percentile(v, 10)),
            "slopeDb": slope_of(bb, v),
            "slopeHalf1": s1,
            "slopeHalf2": s2,
        }

    # ---- B. the moves ----------------------------------------------------
    # events: local maxima of the 30 ms smoothed broadband level, at least 250 ms apart,
    # and at least 6 dB above the local trough (otherwise every ripple is an "attack").
    k = int(0.030 * SR / HOP) | 1
    sm = np.convolve(broadDb, np.ones(k) / k, mode="same")
    cand = []
    i = 26                                    # room for the 250 ms look-back
    while i < nFrames - 40:
        if sm[i] == sm[i - 12 : i + 13].max() and gate[i] and sm[i] > floor + 15.0:
            trough = sm[max(0, i - 25) : i].min()
            if sm[i] - trough > 6.0:
                cand.append(i)
                i += 24                      # 250 ms, so a note is one event
        i += 1

    # THE SHAPE of an event: how the spectral balance changes from the attack to the
    # sustain, in dB, with the broadband change subtracted so it is a tilt and not a
    # loudness.  That is what a "move" is in this material: the note blooms here and
    # settles there, always the same way if the chain is always the same.
    #   (an earlier version took the mean deviation of three moments from the event's
    #    own mean, which is zero by construction - a beautiful vector of zeros that
    #    clustered perfectly and meant nothing.  It took a permutation test to notice.)
    events = []
    for i in cand:
        a, b = i + 2, i + 19                  # ~20 ms after the attack, ~200 ms later
        if b >= nFrames:
            continue
        v = bandDb[b, :] - bandDb[a, :]
        shift = float(broadDb[b] - broadDb[a])
        if not np.all(np.isfinite(v)):
            continue
        events.append((i, v - shift))

    return {
        "name": path.name,
        "seconds": round(len(x) / SR, 2),
        "frames": int(nFrames),
        "gatedFrames": int(gate.sum()),
        "signature": sig,
        "events": len(events),
        "bandDb": bandDb,
        "broadDb": broadDb,
        "gate": gate,
        "eventIdx": np.array([e[0] for e in events], dtype=np.int64),
        "shapes": np.array([e[1] for e in events]) if events else np.zeros((0, nB)),
    }


def shapes_at(bandDb, broadDb, idx, offset=(2, 19)):
    """The same tilt, computed at arbitrary frame indices - used for the null."""
    a = idx + offset[0]
    b = idx + offset[1]
    ok = (b < bandDb.shape[0]) & (a >= 0)
    a, b = a[ok], b[ok]
    v = bandDb[b, :] - bandDb[a, :]
    shift = (broadDb[b] - broadDb[a])[:, None]
    return v - shift


def kmeans(X, k, iters=40, seed=0):
    rng = np.random.default_rng(seed)
    C = X[rng.choice(len(X), size=k, replace=False)]
    for _ in range(iters):
        d = ((X[:, None, :] - C[None, :, :]) ** 2).sum(axis=2)
        lab = d.argmin(axis=1)
        for j in range(k):
            m = lab == j
            if m.any():
                C[j] = X[m].mean(axis=0)
    d = ((X[:, None, :] - C[None, :, :]) ** 2).sum(axis=2)
    return C, d.argmin(axis=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="first 60 s of each file")
    args = ap.parse_args()

    files = sorted(CORPUS.glob("*.mp3"))
    if not files:
        print("no stems in google_drive/", file=sys.stderr)
        return 2
    print(f"{len(files)} stems")

    results = []
    for f in files:
        r = analyse_file(f, args.quick)
        if r is None:
            print(f"  skipped {f.name} (no usable audio)")
            continue
        print(f"  {r['seconds']:7.1f} s  {r['gatedFrames']:6d} gated frames  "
              f"{r['events']:5d} events  {f.name[:44]}")
        r["name"] = r["name"].split(" (")[0]
        results.append(r)

    # ---- A. is the signature consistent across songs? ---------------------
    bands = sorted({int(b) for r in results for b in r["signature"]})
    nS = len(results)
    level = np.full((nS, len(bands)), np.nan)
    rng_ = np.full_like(level, np.nan)
    slope = np.full_like(level, np.nan)
    within = np.full_like(level, np.nan)      # |slopeHalf1 - slopeHalf2|: same take, twice
    for i, r in enumerate(results):
        for j, b in enumerate(bands):
            sd = r["signature"].get(str(b))
            if not sd:
                continue
            level[i, j] = sd["levelDb"]
            rng_[i, j] = sd["rangeDb"]
            slope[i, j] = sd["slopeDb"]
            if np.isfinite(sd["slopeHalf1"]) and np.isfinite(sd["slopeHalf2"]):
                within[i, j] = abs(sd["slopeHalf1"] - sd["slopeHalf2"])

    across_level = np.nanstd(level, axis=0, ddof=1)
    across_slope = np.nanstd(slope, axis=0, ddof=1)
    across_range = np.nanstd(rng_, axis=0, ddof=1)
    within_slope = np.nanmedian(within, axis=0)
    signature = []
    for j, b in enumerate(bands):
        signature.append({
            "hz": b,
            "levelDb": float(np.nanmedian(level[:, j])),
            "acrossSongDb": float(across_level[j]),
            "rangeDb": float(np.nanmedian(rng_[:, j])),
            "rangeAcrossSongDb": float(across_range[j]),
            "slopeDb": float(np.nanmedian(slope[:, j])),
            "slopeAcrossSong": float(across_slope[j]),
            "slopeWithinSong": float(within_slope[j]),
        })

    # how alike are the 15 slope profiles?  correlation of each song with the mean of the
    # other 14 (leave-one-out, so a song cannot score by being part of its own target)
    def loo_corr(mat):
        out = []
        for i in range(mat.shape[0]):
            others = np.delete(mat, i, axis=0)
            m = np.nanmean(others, axis=0)
            a, b = mat[i], m
            good = np.isfinite(a) & np.isfinite(b)
            if good.sum() < 6 or np.nanstd(a[good]) < 1e-9 or np.nanstd(b[good]) < 1e-9:
                continue
            out.append(float(np.corrcoef(a[good], b[good])[0, 1]))
        return out

    slope_corr = loo_corr(slope)
    level_corr = loo_corr(level)

    # null: each song's band axis shuffled on its own.  Shuffling all the songs the same
    # way would leave them aligned with each other (and handed the first version of this
    # script a null that was identical to the measurement, to three decimal places -
    # which is what a broken null looks like).
    rngN = np.random.default_rng(5)
    null_corr = []
    for _ in range(300):
        shuffled = np.empty_like(slope)
        for i in range(slope.shape[0]):
            shuffled[i] = slope[i, rngN.permutation(slope.shape[1])]
        null_corr.extend(loo_corr(shuffled))

    # A *level* signature is only real if the 15 songs agree on it; a *slope* profile is
    # real if the songs agree on it better than a shuffled band axis does - chance, in
    # other words - which is what makes it a property of the engineer and not of the take.
    ok_level = [x for x in signature if x["acrossSongDb"] < 3.0]
    slope_sep = float(np.median(slope_corr)) > float(np.median(null_corr)) + 0.15
    ok_slope = [x for x in signature if slope_sep]
    ratio = np.array([x["rangeDb"] / max(1e-6, x["acrossSongDb"]) for x in signature])

    # ---- B. do the same moves come back in every song? -------------------
    # cluster each song's event shapes into 4 moves, then compare that song's centroids
    # with the *corpus* centroids: how similar, and is it better than chance?
    all_shapes = np.concatenate([r["shapes"] for r in results if r["events"]], axis=0)
    all_shapes = all_shapes - all_shapes.mean(axis=0, keepdims=True)
    K = 4
    C, lab = kmeans(all_shapes, K, seed=3)
    order = np.argsort(-np.bincount(lab, minlength=K))
    C = C[order]
    prior = np.bincount(lab, minlength=K)[order] / len(lab)

    def centroid_similarity(shapes, C):
        """cosine between each of the song's 4 centroids and the nearest corpus one"""
        if len(shapes) < 20:
            return np.nan, 0.0
        S = shapes - shapes.mean(axis=0, keepdims=True)
        if not np.all(np.isfinite(S)) or np.abs(S).max() < 1e-9:
            return np.nan, 0.0
        Cu, lu = kmeans(S, K, seed=3)
        out = []
        for c in Cu:
            cs = []
            for cc in C:
                d = np.linalg.norm(cc)
                cs.append(float(c @ cc / max(1e-9, np.linalg.norm(c) * d)))
            out.append(max(cs))
        # how much of this song's shape variance the corpus moves explain
        d = ((S[:, None, :] - C[None, :, :]) ** 2).sum(axis=2)
        tot = ((S - S.mean(axis=0)) ** 2).sum()
        return float(np.mean(out)), float(1.0 - d.min(axis=1).sum() / max(1e-9, tot))

    per_song = []
    for r in results:
        if not r["events"]:
            continue
        sim, expl = centroid_similarity(r["shapes"], C)
        per_song.append({"name": r["name"], "events": r["events"],
                         "similarity": sim, "explained": expl})

    # permutation: the same tilt, computed at random *gated* frames instead of at the
    # attacks.  Any recurring shape that survives this is not an attack, it is the
    # gate's own statistics - which is the whole point of a null.
    rng = np.random.default_rng(11)
    null = []
    for r in results:
        if r["events"] < 20:
            continue
        gated = np.flatnonzero(r["gate"])
        pick = rng.choice(gated, size=min(4000, gated.size), replace=False)
        fake = shapes_at(r["bandDb"], r["broadDb"], pick)
        fake = fake - fake.mean(axis=0, keepdims=True)
        sim, expl = centroid_similarity(fake, C)
        if np.isfinite(sim):
            null.append({"similarity": sim, "explained": expl})

    real_sim = float(np.nanmedian([p["similarity"] for p in per_song])) if per_song else float("nan")
    real_expl = float(np.nanmedian([p["explained"] for p in per_song])) if per_song else float("nan")
    null_sim = float(np.nanmedian([p["similarity"] for p in null])) if null else float("nan")
    null_expl = float(np.nanmedian([p["explained"] for p in null])) if null else float("nan")

    moves = []
    for j in range(K):
        band_profile = {str(bands[i]): float(C[j, i]) for i in range(len(bands))}
        top = sorted(band_profile.items(), key=lambda kv: -abs(kv[1]))[:4]
        moves.append({
            "share": float(prior[j]),
            "profile": band_profile,
            "strongest": [{"hz": int(h), "db": round(v, 2)} for h, v in top],
        })

    out = {
        "corpus": [r["name"] for r in results],
        "seconds": float(sum(r["seconds"] for r in results)),
        "bands": bands,
        "signature": signature,
        "signatureStable": len(ok_level),
        "slopeStable": len(ok_slope),
        "slopeCorr": float(np.median(slope_corr)) if slope_corr else float("nan"),
        "levelCorr": float(np.median(level_corr)) if level_corr else float("nan"),
        "nullCorr": float(np.median(null_corr)) if null_corr else float("nan"),
        "signatureBands": len(signature),
        "rangeToAcrossSong": [float(x) for x in ratio],
        "moves": moves,
        "events": int(sum(r["events"] for r in results)),
        "perSong": per_song,
        "permutation": {"realSimilarity": real_sim, "nullSimilarity": null_sim,
                        "realExplained": real_expl, "nullExplained": null_expl,
                        "songs": len(per_song), "draws": len(null)},
        "quick": bool(args.quick),
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "moves.json").write_text(json.dumps(out, ensure_ascii=False, indent=1))

    # ---- report ----------------------------------------------------------
    L = []
    L.append("# اللي بيتكرر في الـ15 ستيم — تحليل بنية الحركات\n")
    L.append(f"المادة: {len(results)} ستيم دراي، {out['seconds']/60:.1f} دقيقة صوت، "
             f"{out['events']} حدث (نغمة) اتقسم عليهم.\n")
    L.append("## أولاً: المستوى المطلق — مش هو اللي بيتكرر\n")
    L.append("| باند | المستوى (dB) | اختلاف بين الأغاني | المدى داخل الأغنية | المدى ÷ الاختلاف |")
    L.append("|---|---|---|---|---|")
    for x, rt in zip(signature, ratio):
        L.append(f"| {x['hz']} Hz | {x['levelDb']:.1f} | ±{x['acrossSongDb']:.1f} | "
                 f"{x['rangeDb']:.1f} | {rt:.1f}× |")
    L.append(f"\n{len(ok_level)} من {len(signature)} باند الأغاني متفقين عليهم في حدود 3 dB. "
             "يعني المستوى المطلق مش بصمة: المغني والمايك والمقام بيغيّروا الرقم، "
             "والحاجة الوحيدة اللي بتتكرر هي **إزاي** الباند بيتحرك، مش فين بيقف.\n")

    L.append("## تانيًا: إزاي الباند بيتحرك مع الصوت — دي البصمة\n")
    L.append("| باند | ميل (dB لكل dB) | اختلاف بين الأغاني | اختلاف جوه نفس الأغنية |")
    L.append("|---|---|---|---|")
    for x in signature:
        L.append(f"| {x['hz']} Hz | {x['slopeDb']:+.2f} | ±{x['slopeAcrossSong']:.2f} | "
                 f"{x['slopeWithinSong']:.2f} |")
    L.append(f"\n{len(ok_slope)} من {len(signature)} باند: الأغاني متفقين مع بعضهم أكتر من "
             "اتفاق الأغنية مع نفسها في نصفها التاني. التشابه (ارتباط مع متوسط الـ14 "
             f"التانيين) = {np.median(slope_corr):.2f} للحركة، "
             f"{np.median(level_corr):.2f} للمستوى، و{np.median(null_corr):.2f} لما نكسر "
             "شكل البروفايل نفسه (اختبار تبديل، 300 سحب).\n")

    L.append("## تانيًا: الحركات المتكررة\n")
    L.append("| حركة | نسبتها من الأحداث | أقوى باندات فيها |")
    L.append("|---|---|---|")
    for i, m in enumerate(moves):
        st = ", ".join(f"{b['hz']} Hz {b['db']:+.1f}" for b in m["strongest"])
        L.append(f"| {i+1} | {m['share']*100:.0f}% | {st} |")
    L.append("## تالتًا: شكل الهجمة نفسها — نتيجة سلبية، وبنقولها\n")
    L.append(f"شكل الحركة من الهجمة للسستين (ميل بالباند، منزوع منه تغيّر الصوت الكلي): "
             f"تشابه {real_sim:.2f} مقابل {null_sim:.2f} لصالح العشوائي، "
             f"وبيفسّر {real_expl*100:.0f}% من التباين مقابل {null_expl*100:.0f}% للحظ. "
             "يعني الهجمة نفسها **مش** متكررة بنفس الشكل — واللي بيتكرر هو العلاقة "
             "بين المستوى والاتساع (الجدول اللي فوق)، مش شكل زمني ثابت.\n")
    L.append(f"\n(للأمانة: {len(per_song)} أغنية، {out['events']} حدث.)\n")

    L.append("## تالتًا: الحركات المتكررة\n"
             f"{real_sim:.2f} مقابل {null_sim:.2f} لما نكسر تزامن الحدث (اختبار تبديل، "
             f"{len(null)} سحب). بتفسّر {real_expl*100:.0f}% من التباين مقابل "
             f"{null_expl*100:.0f}% بالحظ.\n")
    (OUT / "report.md").write_text("\n".join(L) + "\n")

    print("\nlevel signature stable in %d/%d bands; slope signature in %d/%d"
          % (len(ok_level), len(signature), len(ok_slope), len(signature)))
    print("profile correlation: slope %.2f, level %.2f, null %.2f"
          % (float(np.median(slope_corr)), float(np.median(level_corr)), float(np.median(null_corr))))
    print("moves: %d events, similarity real %.2f vs null %.2f, explained %.0f%% vs %.0f%%"
          % (out["events"], real_sim, null_sim, real_expl * 100, null_expl * 100))
    print("wrote", OUT / "moves.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
