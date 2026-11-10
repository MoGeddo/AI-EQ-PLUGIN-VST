#!/usr/bin/env python3
"""emit_amir_target.py - Amir Mahrous's own curve, as a table the DSP can solve against.

Two sources, one header
-----------------------
`measured` (preferred, and the default when it exists)
    `analysis/out/amir15/amir_measured.json` + `amir_stems.aeqprof` - his 15 dry stems
    measured by **this plugin's own analyser** (`aeq_cli profile`, 104 bands, gated frames,
    mean power per bin).  Target and take are then on one ruler, so nothing has to be
    widened or corrected before they can be subtracted (GATES G72, G77).

`pipeline`
    `analysis/out/amir15/amir15.json` - the older 1/3-octave consensus from the analysis
    pipeline, with a leave-one-out shift of 1.05 dB.  Kept as the fallback for a checkout
    where the stems have never been measured, and emitted with a flag (`sameInstrument =
    false`) so the DSP widens the measurement to the table's bandwidth before comparing.

Why this is not the corpus table
--------------------------------
`model/TargetCurve.h` is the consensus of 13 songs by 11 engineers - a family average, and
the validation against Amir's stems was not significant (p = 0.0876).  This table is the
thing the user asked for: *his* curve.  Restore keeps the family average (a restore pass
must not chase one engineer's taste); Match uses this one when no reference is loaded.

    .venv/bin/python analysis/scripts/emit_amir_target.py --check    # is the header in sync?
    .venv/bin/python analysis/scripts/emit_amir_target.py --write
    .venv/bin/python analysis/scripts/emit_amir_target.py --check --from pipeline
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "analysis" / "out" / "amir15"
MEASURED = OUT / "amir_measured.json"
PIPELINE = OUT / "amir15.json"
HEADER = ROOT / "plugin" / "Source" / "model" / "AmirTarget.h"


def source_for(which: str) -> tuple[str, list[float], list[float], list[float], float]:
    """(instrument, hz, db, sd, loo) for the requested source."""
    if which == "measured" and MEASURED.exists():
        d = json.loads(MEASURED.read_text(encoding="utf-8"))
        return ("measured", [float(v) for v in d["freqs"]], [float(v) for v in d["mean_db"]],
                [float(v) for v in d["sd_db"]], float(d["loo_mean_db"]))
    if which == "pipeline" and PIPELINE.exists():
        c = json.loads(PIPELINE.read_text(encoding="utf-8"))["amir_consensus"]
        return ("pipeline", [float(v) for v in c["bands_hz"]], [float(v) for v in c["mean_db"]],
                [float(v) for v in c["sd_db"]], float(c["loo_max_shift_db"]))
    raise SystemExit(f"no data for source {which!r}")


def default_source() -> str:
    return "measured" if MEASURED.exists() else "pipeline"


def build(which: str) -> str:
    instrument, hz, db, sd, loo = source_for(which)
    n = len(hz)
    assert len(db) == n == len(sd)

    def rows(values, per_line=6, fmt="{:.2f}f"):
        out = []
        for i in range(0, len(values), per_line):
            chunk = ", ".join(fmt.format(v) for v in values[i:i + per_line])
            out.append("        " + chunk + ",")
        return "\n".join(out)

    if instrument == "measured":
        provenance = (
            "// Generated from analysis/out/amir15/amir_measured.json - do not edit by hand.\n"
            "// Amir Mahrous's 15 dry stems, MEASURED BY THIS PLUGIN'S OWN ANALYSER:\n"
            "// 1/12-octave bands, gated frames, mean power per bin (aeq_cli profile, 15/15 stems).\n")
        limit = (
            "// Target and take are on one ruler, so `sameInstrument` is true and Match compares\n"
            "// them without widening either side.  Band levels are relative to 1 kHz.\n")
    else:
        provenance = (
            "// Generated from analysis/out/amir15/amir15.json - do not edit by hand.\n"
            "// Amir Mahrous's 15 dry stems: 1/3-octave band level, averaged, in dB.\n")
        limit = (
            "// MEASURED BY THE ANALYSIS PIPELINE, NOT BY THIS PLUGIN'S ANALYSER (1/3 octave, its\n"
            "// own gating, its own window): `sameInstrument` is false and the solver widens the\n"
            "// take to this table's bandwidth before subtracting.  Re-run\n"
            "// `analysis/scripts/amir_stems_stats.py <folder>` to replace it with the measured one.\n")

    return f"""// {provenance[3:]}\
// Leave-one-out shift across the {n if False else 15} stems: {loo:.2f} dB (the noise floor of his own material).
{limit}\
#pragma once
#include <array>
#include <cmath>

namespace aeq {{

struct AmirTarget {{
    static constexpr int kPoints = {n};
    // Are these numbers and the engine's own measurement the same instrument?  When they
    // are, the solver must not smooth the take to match a table that was never smoothed.
    static constexpr bool sameInstrument = {str(instrument == "measured").lower()};
    static constexpr const char* name = "{'AMIR 15' if instrument == 'measured' else 'AMIR 15 (pipeline)'}";

    static const std::array<float, kPoints>& hz() {{
        static const std::array<float, kPoints> v {{
{rows(hz, 6, "{:.2f}f")}
        }};
        return v;
    }}
    static const std::array<float, kPoints>& db() {{
        static const std::array<float, kPoints> v {{
{rows(db)}
        }};
        return v;
    }}
    // the spread of his own 15 stems, band by band: the dead zone of a match is the
    // natural variation of the material, and this is that variation, not a guess
    static const std::array<float, kPoints>& sd() {{
        static const std::array<float, kPoints> v {{
{rows(sd)}
        }};
        return v;
    }}

    static double at(double f) {{
        const auto& x = hz();
        const auto& y = db();
        if (f <= x.front()) return y.front();
        if (f >= x.back())  return y.back();
        for (int i = 1; i < kPoints; ++i)
            if (x[i] >= f) {{
                const double t = std::log2(f / x[i - 1]) / std::log2(x[i] / x[i - 1]);
                return y[i - 1] * (1.0 - t) + y[i] * t;
            }}
        return y.back();
    }}

    // Same shape, divided by its own 1 kHz - the frame the solver compares in.
    static double shapeAt(double f) {{ return at(f) - at(1000.0); }}

    static double spreadAt(double f) {{
        const auto& x = hz();
        const auto& s = sd();
        if (f <= x.front()) return s.front();
        if (f >= x.back())  return s.back();
        for (int i = 1; i < kPoints; ++i)
            if (x[i] >= f) {{
                const double t = std::log2(f / x[i - 1]) / std::log2(x[i] / x[i - 1]);
                return s[i - 1] * (1.0 - t) + s[i] * t;
            }}
        return s.back();
    }}
}};

}} // namespace aeq
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--from", dest="which", default=None, choices=("measured", "pipeline"))
    args = ap.parse_args()

    which = args.which or default_source()
    want = build(which)
    have = HEADER.read_text(encoding="utf-8") if HEADER.exists() else ""

    if args.write:
        HEADER.parent.mkdir(parents=True, exist_ok=True)
        HEADER.write_text(want, encoding="utf-8")
        print(f"wrote {HEADER.relative_to(ROOT)} ({len(want)} bytes) from {which}")
        return 0

    if have != want:
        print("AmirTarget.h does not match the analysis it claims to come from")
        print("  the table was hand-edited, or the analysis changed and the DSP did not")
        print("  run: .venv/bin/python analysis/scripts/emit_amir_target.py --write")
        return 1

    instrument, hz, db, sd, loo = source_for(which)
    i1k = min(range(len(hz)), key=lambda i: abs(hz[i] - 1000.0))
    i250 = min(range(len(hz)), key=lambda i: abs(hz[i] - 250.0))
    print(f"amir target in sync: {len(hz)} bands from {instrument}, LOO {loo:.2f} dB, "
          f"1 kHz re 250 Hz {db[i1k] - db[i250]:+.2f} dB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
