#!/usr/bin/env python3
"""dep_graph.py - the include graph, and the one rule it is here to enforce.

Graphify's premise is that you look at the arrows before you add a feature, because a
layer that reaches up is a layer that cannot be tested alone.  This plugin has a shape:

    ui  ->  PluginEditor  ->  PluginProcessor  ->  dsp/*  ->  model/*

and every arrow may only point that way.  An `#include "../ui/..."` inside `dsp/` would
mean the audio engine knows what a knob looks like; a `model` file that includes `dsp`
would mean the corpus table can only be used inside the DSP.  Neither has happened, but
nothing stopped it either, and the file that says so (GATES G53) pointed at a script whose
subject was layers, not includes.

    python3 scripts/dep_graph.py            # print the graph and check the direction
    python3 scripts/dep_graph.py --write    # also write docs/dependency_graph.json
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "plugin" / "Source"
OUT = ROOT / "docs" / "dependency_graph.json"

# rank 0 is the floor; an include may only go to a file of equal or lower rank
RANK = {"model": 0, "dsp": 1, "PluginProcessor": 2, "PluginEditor": 3, "ui": 3, "cli": 4}

# One upward edge is allowed, and it is allowed by name: a VST3 processor *must* implement
# createEditor() (JUCE calls it through AudioProcessor), so PluginProcessor.cpp has to know
# that the editor type exists.  It is a factory edge, not a dependency: the processor
# never touches an editor object, and the editor is the only thing that knows the window.
# Everything else has to point down.
ALLOWED_UP = {("PluginProcessor.cpp", "PluginEditor.h")}


def layer_of(path: pathlib.Path) -> str:
    rel = path.relative_to(SRC).as_posix()
    if rel.startswith("model/"):
        return "model"
    if rel.startswith("dsp/"):
        return "dsp"
    if rel.startswith("ui/"):
        return "ui"
    if rel.startswith("cli/"):
        return "cli"
    if rel.startswith("PluginEditor"):
        return "PluginEditor"
    if rel.startswith("PluginProcessor"):
        return "PluginProcessor"
    return "other"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()

    files = sorted(p for p in SRC.rglob("*") if p.suffix in (".h", ".cpp"))
    edges = []
    for f in files:
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r'^\s*#include\s+"([^"]+)"', text, re.M):
            target = (f.parent / m.group(1)).resolve()
            try:
                target.relative_to(SRC)
            except ValueError:
                continue                       # a JUCE header or a test include
            edges.append((f.relative_to(SRC).as_posix(), target.relative_to(SRC).as_posix()))

    upward = []
    for src, dst in edges:
        a = RANK.get(layer_of(SRC / src), 9)
        b = RANK.get(layer_of(SRC / dst), 9)
        if b > a and (src, dst) not in ALLOWED_UP:
            upward.append(f"{src} -> {dst} (rank {a} reaches up to {b})")

    # a cycle inside the graph would be a second, different problem
    graph: dict[str, set[str]] = {}
    for s, d in edges:
        graph.setdefault(s, set()).add(d)
    cycles = []
    state: dict[str, int] = {}

    def visit(n: str, stack: list[str]) -> None:
        if state.get(n) == 1:
            cycles.append(" -> ".join(stack[stack.index(n):] + [n]))
            return
        if state.get(n) == 2:
            return
        state[n] = 1
        for m in graph.get(n, ()):
            visit(m, stack + [n])
        state[n] = 2

    for n in sorted(graph):
        visit(n, [])

    layers: dict[str, int] = {}
    for f in files:
        layers[layer_of(f)] = layers.get(layer_of(f), 0) + 1

    print(f"{len(files)} source files, {len(edges)} internal includes")
    for k in sorted(layers, key=lambda k: RANK.get(k, 9)):
        print(f"  {k:<16} {layers[k]:>2} files")

    if args.write:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(json.dumps({
            "files": [f.relative_to(SRC).as_posix() for f in files],
            "includes": [{"from": s, "to": d} for s, d in edges],
            "layers": layers,
            "upward": upward,
            "cycles": cycles,
        }, indent=1), encoding="utf-8")
        print(f"wrote {OUT.relative_to(ROOT)}")

    if upward:
        print("upward includes:")
        for u in upward:
            print("  - " + u)
    if cycles:
        print("cycles:")
        for c in cycles:
            print("  - " + c)

    if upward or cycles:
        return 1
    print(f"the arrows only point down: {len(edges)} includes, no cycle, no layer reaching up")
    return 0


if __name__ == "__main__":
    sys.exit(main())
