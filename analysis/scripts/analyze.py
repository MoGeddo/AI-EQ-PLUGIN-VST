"""Corpus analysis.

What this script does, and why in this order
--------------------------------------------
1. descriptive statistics (pitch, dynamics, HPF, sibilance, space)
2. a *register model*: how the spectrum changes when the singer goes up an
   octave.  This is physiology and it must be removed before anything can be
   called "EQ" - it explains most of the brightness spread (r = 0.81).
3. the pitch-corrected *consensus target*: the transferable curve the plugin
   will match against.
4. pitch-corrected brightness ranking: production choice, not anatomy.
5. the *taste axes* of the family (PCA of the per-song deviations).  These are
   the concrete EQ moves that actually distinguish these mixes, and each axis is
   exported as a real RBJ-biquad preset.
6. de-ess / notch signature, reverb send, subgroup consistency.
"""
from __future__ import annotations

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from aeq import voicemodel as vm  # noqa: E402
from aeq import spectral as sp  # noqa: E402
from aeq import plots as pl  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "out")

D = json.load(open(os.path.join(OUT, "corpus.json")))
S, A = D["songs"], D["agg"]
GRID = np.array(A["grid"])
BANDS = np.array(A["band_centers"])
TITLES = A["titles"]
N = len(S)
SHORT = [t.split(" - ")[0][:22] for t in TITLES]
FEMALE = ("شيرين", "أنغام")


def pct(x, q):
    return float(np.nanpercentile(x, q))


def corr(a, b):
    m = np.isfinite(a) & np.isfinite(b)
    return float(np.corrcoef(a[m], b[m])[0, 1]) if m.sum() >= 4 else float("nan")


# --------------------------------------------------------------------------- #
# 1. statistics
# --------------------------------------------------------------------------- #
f0 = np.array([s["med_f0_hz"] for s in S])
tilt = np.array([s["tilt_db_oct"] for s in S])
lufs = np.array([s["dyn"]["lufs"] for s in S])
plr = np.array([s["dyn"]["plr"] for s in S])
lra = np.array([s["dyn"]["lra"] for s in S])
air = np.array([s["air10k_db"] for s in S])
corner = np.array([s["hpf_corner_hz"] for s in S])
sib = np.array([s["sibilance_index_db"] for s in S])
sib90 = np.array([s["sibilance_p90_db"] for s in S])
wetr = np.array([s.get("wet", {}).get("wet_over_dry_db", np.nan) for s in S])
side = np.array([s.get("side_over_mid_db", np.nan) for s in S])
hnr = np.array([s["hnr_median"] for s in S])

print(f"=== corpus: {N} songs, {sum(s['dur_s'] for s in S)/60:.0f} min of solo vocal ===")
print(f"median F0 (stacks)       : {np.median(f0):.0f} Hz  ({f0.min():.0f}-{f0.max():.0f})")
print(f"tilt 100 Hz-8 kHz        : {np.median(tilt):+.2f} dB/oct (p10 {pct(tilt,10):+.2f}, p90 {pct(tilt,90):+.2f})")
print(f"corr(tilt, log F0)       : {corr(tilt, np.log(f0)):+.2f}  <- brightness is mostly the voice")
print(f"PLR / LRA                : {np.median(plr):.1f} dB / {np.median(lra):.1f} LU")
print(f"HPF corner (3 dB)        : {np.median(corner):.0f} Hz (p10 {pct(corner,10):.0f}, p90 {pct(corner,90):.0f})")
print(f"air 10 k vs 3.15 k       : {np.median(air):+.1f} dB")
print(f"sibilance 5-10 k / 1-4 k : median {np.median(sib):+.1f} dB, p90 {np.median(sib90):+.1f} dB")
print(f"wet / dry                : {np.nanmedian(wetr):+.1f} dB ({np.nanmin(wetr):+.1f} .. {np.nanmax(wetr):+.1f})")
print(f"side / mid               : {np.nanmedian(side):+.1f} dB   aperiodicity {np.nanmedian(hnr):.2f}")

# --- codec / measurement ceiling ------------------------------------------- #
# These stems are 320 kbps MP3: content above ~15-16 kHz is not the mix, it is the
# encoder's low-pass.  Everything above that frequency is excluded from claims.
HF_LIMIT = 16000.0
lowpass = []
for s_ in S:
    c = sp.smooth_logfreq(np.array(s_["ltas_raw_bins_db"]), 1 / 12)
    ref10 = float(sp.band_levels(np.array(s_["ltas_raw_bins_db"]), GRID, np.array([8000.0]), 1)[0])
    above = (GRID > 8000) & (c > ref10 - 18.0)
    idx = np.where(above)[0]
    lowpass.append(float(GRID[idx[-1]]) if idx.size else float("nan"))
lowpass = np.array(lowpass)
print(f"codec low-pass (18 dB down): median {np.nanmedian(lowpass)/1000:.1f} kHz "
      f"({np.nanmin(lowpass)/1000:.1f}-{np.nanmax(lowpass)/1000:.1f}) -> HF claims limited to 16 kHz")

# --------------------------------------------------------------------------- #
# 2. register model  (physiology)
# --------------------------------------------------------------------------- #
beta, beta_sd, _ = vm.corpus_register_slope(S, BANDS)
f0_ref = float(np.median(f0))

# --------------------------------------------------------------------------- #
# 3. consensus target  (pitch-corrected)
# --------------------------------------------------------------------------- #
raw = np.stack([np.array(s["ltas_bins_db"]) for s in S])
vn = np.stack([vm.voice_normalised_curve(s, GRID, beta, f0_ref, BANDS) for s in S])
consensus = vn.mean(axis=0)
cons_sd = vn.std(axis=0, ddof=1)
cons_se = cons_sd / np.sqrt(N)
band_mat = sp.band_levels(vn, GRID, BANDS)
cons_band = band_mat.mean(axis=0)
cons_band_sd = band_mat.std(axis=0, ddof=1)
ref = int(np.argmin(abs(BANDS - 1000)))
cons_band_n = cons_band - cons_band[ref]

print("\n--- consensus target: 1/3-oct, dB re 1 kHz, pitch-corrected ---")
print(f"{'Hz':>7s} {'target':>7s} {'between-song sd':>16s} {'SEM':>6s}")
for i, f in enumerate(BANDS):
    print(f"{f:7.0f} {cons_band_n[i]:+7.1f} {cons_band_sd[i]:16.1f} {cons_band_sd[i]/np.sqrt(N):6.1f}")

# --------------------------------------------------------------------------- #
# 4. brightness, pitch-corrected
# --------------------------------------------------------------------------- #
tilt_corr = np.array([sp.tilt_db_per_oct(vn[i], GRID) for i in range(N)])
cons_tilt = sp.tilt_db_per_oct(consensus, GRID)
d_tilt = tilt_corr - cons_tilt
print(f"\n--- brightness (dB/oct over 100 Hz-8 kHz), register removed ---")
print(f"consensus = {cons_tilt:+.2f} dB/oct ; spread +-{d_tilt.std(ddof=1):.2f} dB/oct")
print(f"{'song':24s} {'tilt':>7s} {'vs std':>8s}")
for i in np.argsort(d_tilt):
    print(f"{SHORT[i]:24s} {tilt_corr[i]:+7.2f} {d_tilt[i]:+8.2f}")

# --------------------------------------------------------------------------- #
# 5. taste axes (PCA of deviations) + axis presets
# --------------------------------------------------------------------------- #
dev = band_mat - cons_band                     # (N, nband)
MB = BANDS >= 160.0                            # analysable band mask
X = dev[:, MB] - dev[:, MB].mean(axis=0)
U, sv, Vt = np.linalg.svd(X, full_matrices=False)
expl = sv ** 2 / np.sum(sv ** 2)
print("\n--- taste axes (PCA of per-song deviation, 1/3-oct) ---")
print("variance explained: " + ", ".join(f"PC{i+1} {expl[i]*100:.0f}%" for i in range(min(4, len(expl)))))

axes = []
for k in range(2):
    load = Vt[k] * sv[k] / np.sqrt(N)          # dB per 1 SD of the axis
    load_full = np.zeros(len(BANDS)); load_full[MB] = load; load = load_full
    scores = U[:, k] * sv[k] / np.sqrt(N)
    order = np.argsort(scores)
    print(f"\nPC{k+1} ({expl[k]*100:.0f} %): darkest/low  <-  -> bright/high")
    for i in order[:3]:
        print(f"    {SHORT[i]:22s} {scores[i]:+.2f}")
    print("    ...")
    for i in order[-3:]:
        print(f"    {SHORT[i]:22s} {scores[i]:+.2f}")
    top = [j for j in np.argsort(-np.abs(load)) if BANDS[j] >= 160][:6]
    print("    strongest bands: " + ", ".join(f"{BANDS[j]:.0f}Hz {load[j]:+.1f}dB" for j in top))
    # concrete preset for +1 SD of this axis
    curve = np.interp(np.log2(GRID), np.log2(BANDS[MB]), load[MB])
    curve = sp.smooth_logfreq(curve, 1 / 12)
    pr = vm.fit_parametric(GRID, curve, n_peaks=3, fmin=160.0, fmax=14000.0)
    q = vm.fit_quality(GRID, curve, pr, fmin=160.0, fmax=14000.0)
    axes.append({"pc": k + 1, "explained": float(expl[k]), "loadings": load.tolist(),
                 "scores": dict(zip(TITLES, scores.tolist())), "preset": pr,
                 "fit": q})
    print(f"    +1 SD preset (RMS {q['rms_db']:.2f} dB): " +
          "; ".join(f"{b['type']} {b['f0']:.0f}Hz {b['gain_db']:+.1f}dB" for b in pr))

# --------------------------------------------------------------------------- #
# 6. notch / de-ess signature + subgroup consistency
# --------------------------------------------------------------------------- #
notches = [(s["title"], n["freq"], n["depth_db"]) for s in S for n in s.get("notch_list", [])]
clusters = []
for title, f, d in sorted(notches, key=lambda x: x[1]):
    for c in clusters:
        if abs(np.log2(f / c["f"])) < 1 / 6:
            c["items"].append((title, f, d))
            c["f"] = float(np.median([i[1] for i in c["items"]]))
            break
    else:
        clusters.append({"f": f, "items": [(title, f, d)]})
clusters.sort(key=lambda c: -len(c["items"]))
prim = np.array([s["notch_list"][0]["freq"] if s["notch_list"] else np.nan for s in S])
prim_d = np.array([s["notch_list"][0]["depth_db"] if s["notch_list"] else np.nan for s in S])
print("\n--- narrow spectral dips ---")
for c in clusters[:8]:
    if len(c["items"]) >= 3:
        print(f"  {c['f']:7.0f} Hz  in {len(c['items']):2d}/{N} songs, median depth "
              f"{np.median([i[2] for i in c['items']]):.1f} dB")
print(f"  primary dip frequency vs pitch: r = {corr(np.log(prim), np.log(f0)):+.2f} "
      f"(~0 => fixed in frequency, not formant scaling)")

is_f = np.array([any(k in t for k in FEMALE) for t in TITLES])
sub = {}
for name, mask in (("male", ~is_f), ("female", is_f)):
    if mask.sum() >= 2:
        m_ = dev[mask].mean(axis=0)
        sub[name] = {"dev": m_.tolist(), "n": int(mask.sum())}
        print(f"{name:6s} n={mask.sum():2d} mean deviation at 250/400/1.6k/2.5k/8k: " +
              "  ".join(f"{v:+.1f}" for f, v in zip(BANDS, m_) if f in (250, 400, 1600, 2500, 8000)))
if len(sub) == 2:
    mm = (BANDS >= 200) & (BANDS <= 8000)
    a1, a2 = np.array(sub["male"]["dev"])[mm], np.array(sub["female"]["dev"])[mm]
    print(f"male vs female deviation pattern (200-8000 Hz): r = {np.corrcoef(a1, a2)[0,1]:+.2f}, "
          f"mean |diff| = {np.mean(np.abs(a1 - a2)):.1f} dB  "
          f"<- INCONCLUSIVE: only {int(is_f.sum())} female songs; reported, not relied on")

# --------------------------------------------------------------------------- #
# 7. space
# --------------------------------------------------------------------------- #
wets = [s["wet"] for s in S if s.get("wet")]
send = None
if wets:
    col = np.stack([np.array(w["colour_db"]) for w in wets])
    dry_b = np.stack([np.array(s["ltas_db"]) for s in S if s.get("wet")])
    send = col.mean(axis=0) - dry_b.mean(axis=0)
    send = send - send[ref]
    rts = np.array([w.get("rt60", np.nan) for w in wets], dtype=float)
    print(f"\n--- space ---\nwet/dry {np.nanmedian(wetr):+.1f} dB; RT60 resolvable in "
          f"{int(np.isfinite(rts).sum())}/{len(rts)} songs - too few, low confidence")
    print("send colour (wet - dry, dB re 1 k): " +
          "  ".join(f"{f:.0f}:{v:+.1f}" for f, v in zip(BANDS, send)
                   if f in (100, 200, 400, 800, 1600, 3150, 6300, 10000, 16000)))

# --------------------------------------------------------------------------- #
# save
# --------------------------------------------------------------------------- #
out = {
    "n_songs": N, "titles": TITLES, "grid": GRID.tolist(), "bands": BANDS.tolist(),
    "f0_ref": f0_ref,
    "register_beta": beta.tolist(), "register_beta_sd": beta_sd.tolist(),
    "consensus_curve_db": consensus.tolist(),
    "consensus_ci95_db": (1.96 * cons_se).tolist(),
    "consensus_band_db": cons_band_n.tolist(),
    "consensus_band_sd_db": cons_band_sd.tolist(),
    "consensus_tilt_db_oct": float(cons_tilt),
    "per_song_curve": {t: vn[i].tolist() for i, t in enumerate(TITLES)},
    "per_song_deviation": {t: dev[i].tolist() for i, t in enumerate(TITLES)},
    "per_song_tilt_corrected": dict(zip(TITLES, tilt_corr.tolist())),
    "per_song_tilt_vs_standard": dict(zip(TITLES, d_tilt.tolist())),
    "axes": axes,
    "notch_clusters": [{"freq": c["f"], "n_songs": len(c["items"]),
                        "depth_db": float(np.median([i[2] for i in c["items"]]))} for c in clusters],
    "send_colour_db": None if send is None else send.tolist(),
    "subgroup": sub,
    "limits": ("A finished vocal stem cannot be inverse-filtered: engineer EQ, mic, "
               "proximity, compression and the singer's own tract response are "
               "inseparable without the raw take. Everything here is either "
               "pitch-corrected consensus, or a relative deviation from that consensus."),
    "stats": {
        "median_f0": float(np.median(f0)), "f0_min": float(f0.min()), "f0_max": float(f0.max()),
        "median_tilt": float(np.median(tilt)), "tilt_p10": pct(tilt, 10), "tilt_p90": pct(tilt, 90),
        "tilt_f0_corr": corr(tilt, np.log(f0)),
        "median_lufs": float(np.median(lufs)), "median_plr": float(np.median(plr)),
        "median_lra": float(np.median(lra)), "median_hpf": float(np.median(corner)),
        "median_air10k": float(np.median(air)), "median_sib": float(np.median(sib)),
        "median_sib_p90": float(np.median(sib90)), "median_wet": float(np.nanmedian(wetr)),
        "codec_lowpass_hz": float(np.nanmedian(lowpass)),
        "median_side_mid": float(np.nanmedian(side)), "median_hnr": float(np.nanmedian(hnr)),
        "prim_notch_f0_corr": corr(np.log(prim), np.log(f0)),
    },
}
json.dump(out, open(os.path.join(OUT, "analysis.json"), "w"), indent=1)
with open(os.path.join(OUT, "target_curve.csv"), "w") as f:
    f.write("# consensus vocal target, pitch-corrected, normalised at 1 kHz\n")
    f.write("# valid 100 Hz - 14 kHz: above 14 kHz the MP3 stems are already low-passed\n")
    f.write("freq_hz,target_db_re_1k,target_1_6oct_smoothed\n")
    sm = sp.smooth_logfreq(consensus, 1 / 6)
    for i in range(0, GRID.size):
        if 100.0 <= GRID[i] <= 14000.0:
            f.write(f"{GRID[i]:.1f},{consensus[i]:.3f},{sm[i]:.3f}\n")
for k, ax in enumerate(axes):
    with open(os.path.join(OUT, f"axis_pc{k+1}_positive.csv"), "w") as f:
        f.write("type,freq_hz,q_or_s,gain_db\n")
        for b in ax["preset"]:
            f.write(f"{b['type']},{b['f0']:.1f},{b.get('q', b.get('s', 1.0)):.3f},{b['gain_db']:+.2f}\n")

# --------------------------------------------------------------------------- #
pl.main(OUT, GRID, BANDS, TITLES, SHORT, vn, consensus, cons_se, dev, beta, beta_sd,
        axes, send, f0, tilt, plr, wetr, prim, prim_d, d_tilt)
print("\nwrote analysis.json, target_curve.csv, axis_pc*.csv + figures")
