"""Figures for the corpus report."""
from __future__ import annotations

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from . import spectral as sp

plt.rcParams.update({
    "figure.dpi": 130, "savefig.dpi": 130, "font.size": 9,
    "axes.grid": True, "grid.alpha": 0.25, "axes.spines.top": False,
    "axes.spines.right": False, "figure.facecolor": "white",
    "font.family": "DejaVu Sans",
})


def _ax_freq(ax, fmin=40, fmax=18000):
    ax.set_xscale("log")
    ax.set_xlim(fmin, fmax)
    ticks = [50, 100, 200, 500, 1000, 2000, 5000, 10000]
    ax.set_xticks(ticks)
    ax.set_xticklabels([f"{t//1000}k" if t >= 1000 else str(t) for t in ticks])
    ax.minorticks_off()


def fig_consensus(out, grid, vn, consensus, cons_se, titles):
    fig, ax = plt.subplots(figsize=(8.4, 4.8))
    for i, t in enumerate(titles):
        y = vn[i] - vn[i][np.argmin(abs(grid - 1000))]
        ax.plot(grid, sp.smooth_logfreq(y, 1 / 6), lw=0.6, alpha=0.3, color="#7f8c8d", zorder=1)
    ax.fill_between(grid, sp.smooth_logfreq(consensus, 1/6) - 1.96 * sp.smooth_logfreq(cons_se, 1/6), sp.smooth_logfreq(consensus, 1/6) + 1.96 * sp.smooth_logfreq(cons_se, 1/6),
                    color="#c0392b", alpha=0.25, zorder=2, label="95% CI of the mean")
    ax.plot(grid, sp.smooth_logfreq(consensus, 1 / 6), color="#c0392b", lw=2.2, zorder=3, label="consensus target")
    _ax_freq(ax, 40, 16000)
    ax.set_ylim(-45, 25)
    ax.set_xlabel("Hz")
    ax.set_ylabel("dB re 1 kHz")
    ax.set_title("Consensus long-term spectrum of the 13 mixes (pitch-corrected, 1/6-oct smoothing)\n"
                 "grey = individual songs   red = the transferable target   shaded = 95% CI")
    ax.legend(loc="lower left", fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig1_consensus.png"))
    plt.close(fig)


def fig_register(out, bands, beta, beta_sd):
    fig, ax = plt.subplots(figsize=(8.4, 3.8))
    ax.axhline(0, color="k", lw=0.8)
    ax.errorbar(bands, beta, yerr=beta_sd, fmt="o-", ms=3, lw=1.2, capsize=2, color="#2c3e50")
    _ax_freq(ax, bands[0], bands[-1])
    ax.set_xlabel("Hz")
    ax.set_ylabel("dB per octave of F0")
    ax.set_title("Register model: band level change per octave of sung pitch (mean +- sd across songs)\n"
                 "this is physiology - it dominates 'brightness' and must be removed first")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig2_register.png"))
    plt.close(fig)


def fig_heatmap(out, bands, dev, short):
    fig, ax = plt.subplots(figsize=(7.8, 0.34 * len(short) + 1.9))
    lim = 6.0
    im = ax.imshow(dev, aspect="auto", cmap="RdBu_r", vmin=-lim, vmax=lim)
    ax.set_yticks(range(len(short)))
    ax.set_yticklabels(short, fontsize=7)
    ax.set_xticks(range(len(bands)))
    ax.set_xticklabels([f"{b/1000:g}k" if b >= 1000 else f"{b:g}" for b in bands], rotation=90, fontsize=7)
    ax.grid(False)
    cb = fig.colorbar(im, ax=ax, pad=0.02)
    cb.set_label("dB vs consensus")
    ax.set_title("Deviation from the consensus per song (register-corrected)")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig3_deviation.png"))
    plt.close(fig)


def fig_tilt(out, short, d_tilt):
    fig, ax = plt.subplots(figsize=(7.6, 3.6))
    o = np.argsort(d_tilt)
    ax.barh(range(len(short)), d_tilt[o],
            color=["#2980b9" if v < 0 else "#e67e22" for v in d_tilt[o]])
    ax.set_yticks(range(len(short)))
    ax.set_yticklabels([short[i] for i in o], fontsize=7)
    ax.axvline(0, color="k", lw=0.9)
    ax.set_xlabel("dB/oct vs the consensus (positive = brighter)")
    ax.set_title("Production brightness after removing the register effect")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig4_brightness.png"))
    plt.close(fig)


def fig_axes(out, bands, axes):
    if not axes:
        return
    fig, axs = plt.subplots(1, 2, figsize=(9.6, 3.8))
    for k, ax in enumerate(axs[:len(axes)]):
        a = axes[k]
        ax.axhline(0, color="k", lw=0.8)
        ax.plot(bands, a["loadings"], "o-", ms=3.5, lw=1.4, color="#16a085")
        ax.fill_between(bands, 0, a["loadings"], color="#16a085", alpha=0.18)
        _ax_freq(ax, bands[0], bands[-1])
        ax.set_xlabel("Hz")
        ax.set_ylabel("dB per 1 SD")
        ax.set_title(f"PC{a['pc']} taste axis ({a['explained']*100:.0f}% of variance)")
    fig.suptitle("The axes along which these 13 mixes actually differ", fontsize=10)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig5_axes.png"))
    plt.close(fig)


def fig_confound(out, f0, tilt, plr, wetr):
    fig, axs = plt.subplots(1, 3, figsize=(10.6, 3.3))
    axs[0].scatter(f0, tilt, c="#c0392b")
    axs[0].set_xlabel("median F0 (Hz)")
    axs[0].set_ylabel("tilt 100 Hz-8 kHz (dB/oct)")
    axs[0].set_title("'Brightness' vs register")
    axs[1].scatter(tilt, plr, c="#16a085")
    axs[1].set_xlabel("tilt (dB/oct)")
    axs[1].set_ylabel("PLR (dB)")
    axs[1].set_title("Tilt vs dynamics")
    axs[2].scatter(wetr, plr, c="#8e44ad")
    axs[2].set_xlabel("wet/dry (dB)")
    axs[2].set_ylabel("PLR (dB)")
    axs[2].set_title("Space vs dynamics")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig6_scatter.png"))
    plt.close(fig)


def fig_send(out, bands, send):
    if send is None:
        return
    fig, ax = plt.subplots(figsize=(7.4, 3.4))
    ax.axhline(0, color="k", lw=0.8)
    ax.plot(bands, send, "o-", ms=3, color="#8e44ad")
    _ax_freq(ax, bands[0], bands[-1])
    ax.set_xlabel("Hz")
    ax.set_ylabel("dB re 1 kHz")
    ax.set_title("Reverb send colour: what the space adds relative to the dry vocal")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig7_send.png"))
    plt.close(fig)


def fig_notches(out, prim, prim_d):
    m = np.isfinite(prim_d)
    if not m.any():
        return
    fig, ax = plt.subplots(figsize=(6.6, 3.3))
    ax.scatter(prim[m], prim_d[m], c="#d35400", s=28)
    ax.set_xscale("log")
    ax.set_xlim(2500, 8000)
    ax.set_xlabel("dip frequency (Hz)")
    ax.set_ylabel("depth (dB)")
    ax.set_title("Primary narrow dip per song (clusters near 4.3 kHz)")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig8_notches.png"))
    plt.close(fig)


def main(out, grid, bands, titles, short, vn, consensus, cons_se, dev, beta, beta_sd,
         axes, send, f0, tilt, plr, wetr, prim, prim_d, d_tilt):
    fig_consensus(out, grid, vn, consensus, cons_se, titles)
    fig_register(out, bands, beta, beta_sd)
    fig_heatmap(out, bands, dev, short)
    fig_tilt(out, short, d_tilt)
    fig_axes(out, bands, axes)
    fig_confound(out, f0, tilt, plr, wetr)
    fig_send(out, bands, send)
    fig_notches(out, prim, prim_d)
