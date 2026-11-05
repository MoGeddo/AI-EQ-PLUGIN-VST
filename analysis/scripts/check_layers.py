#!/usr/bin/env python3
"""check_layers.py - the include graph, and the arrows that are not allowed to point up.

Graphify's idea, applied to something much smaller than a knowledge graph: before
you add a feature, you should be able to *see* the dependency structure, because
the bugs that hurt in a plugin are the ones where two layers reach into each other
and the audio thread starts touching the UI.

So this builds the real graph from the `#include "..."` lines of plugin/Source
(no LLM, no service - 30 lines of parsing), and asserts the three rules that keep
this codebase reviewable:

  1. NO CYCLES.  An include cycle is not a style issue: it means two headers are
     one component that has been written down twice.
  2. NO UPWARD EDGES.  dsp/ and model/ must not include ui/ or the editor.  The DSP
     has to be testable on a machine with no window server, and it is: that is why
     the eight DSP test binaries run in CI without a display.
  3. EVERY HEADER IS REACHABLE.  A header nobody includes is either dead or is
     being included through a path this check cannot see (a CMake include dir), and
     both of those are worth knowing.

It also prints the graph, so the answer to "what does RestoreCore actually depend
on" is one command instead of a grep.

    /tmp/venv/bin/python analysis/scripts/check_layers.py            # check
    /tmp/venv/bin/python analysis/scripts/check_layers.py --graph    # + the edges
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "Source"

# The stack, bottom to top.  model/ is the data the measurements produced (pure,
# no audio, no JUCE); dsp/ processes with it; ui/ draws what dsp computed; the root
# files wire the two together for JUCE.  A dependency may only point *down*.
LAYER = {"model": 0, "dsp": 1, "ui": 2, "": 3}


def layer_of(path: pathlib.Path) -> int:
    rel = path.relative_to(SRC)
    return LAYER.get(rel.parts[0] if len(rel.parts) > 1 else "", 3)


def build() -> dict[pathlib.Path, set[pathlib.Path]]:
    files = {p for p in SRC.rglob("*") if p.suffix in (".h", ".hpp", ".cpp")}
    by_name = {p.name: p for p in files}
    graph: dict[pathlib.Path, set[pathlib.Path]] = {}
    for p in files:
        deps: set[pathlib.Path] = set()
        for line in p.read_text(errors="ignore").splitlines():
            m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
            if not m:
                continue
            target = m.group(1)
            hit = by_name.get(pathlib.Path(target).name)
            if hit is not None and hit != p:
                deps.add(hit)
        graph[p] = deps
    return graph


def cycles(graph) -> list[list[pathlib.Path]]:
    """Every strongly-connected component of size > 1, i.e. every real cycle."""
    index: dict[pathlib.Path, int] = {}
    low: dict[pathlib.Path, int] = {}
    on: set[pathlib.Path] = set()
    stack: list[pathlib.Path] = []
    out: list[list[pathlib.Path]] = []
    counter = [0]

    def strong(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in graph.get(v, ()):
            if w not in index:
                strong(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1:
                out.append(comp)

    for v in list(graph):
        if v not in index:
            strong(v)
    return out


def main() -> int:
    if not SRC.exists():
        print(f"FAIL {SRC} does not exist")
        return 1
    graph = build()
    problems: list[str] = []

    for comp in cycles(graph):
        names = " <-> ".join(sorted(p.name for p in comp))
        problems.append(f"include cycle: {names}")

    upward = []
    for src, deps in graph.items():
        for dep in deps:
            if layer_of(dep) > layer_of(src):
                upward.append((src, dep))
                problems.append(f"{src.relative_to(SRC)} ({layer_of(src)}) includes "
                                f"{dep.relative_to(SRC)} ({layer_of(dep)}) - bottom layer "
                                "reaching up")
    edges = sum(len(d) for d in graph.values())
    reachable = {d for deps in graph.values() for d in deps}

    # entry points are allowed to be un-referenced: the editor and the processor are
    # included by the CMake target, not by another header
    entries = {"PluginEditor.cpp", "PluginProcessor.cpp", "PluginEditor.h", "PluginProcessor.h"}
    # headers written for the next milestone and deliberately not wired yet
    pending = {"MotionSpec.h"}
    orphans = sorted(p.name for p in graph
                     if p.suffix in (".h", ".hpp")
                     and p not in reachable and p.name not in entries and p.name not in pending
                     and not any(p.name in (d.name for d in ds) for ds in graph.values()))
    if orphans:
        problems.append("headers nothing includes: " + ", ".join(orphans))

    if "--graph" in sys.argv:
        for src in sorted(graph, key=str):
            for dep in sorted(graph[src], key=str):
                print(f"  {src.relative_to(SRC)} -> {dep.relative_to(SRC)}")

    if problems:
        for p in problems:
            print("FAIL " + p)
        print(f"{len(problems)} problem(s)")
        return 1

    layers = {}
    for p in graph:
        layers[layer_of(p)] = layers.get(layer_of(p), 0) + 1
    print(f"layers ok: {len(graph)} files, {edges} include edges, 0 cycles, "
          f"0 upward edges, 0 orphans; by layer {dict(sorted(layers.items()))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
