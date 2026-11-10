#!/bin/sh
# Syntax-check the editor against a real JUCE 8 tree, without a JUCE checkout in
# this repo and without a full CMake configure.
#
# The plugin itself is built on Windows by CI (that is the real compile).  This
# script exists so a mistake in the editor - a JUCE API that changed, a missing
# include, a typo in a header the build would only reach after six minutes - is
# caught in seconds on any machine that has the JUCE source.
#
#   JUCE_DIR=/path/to/JUCE-8.0.4 ./plugin/check_ui_compile.sh
#
# The JUCE tree is not vendored: it is a 250 MB dependency fetched by CMake, and
# keeping a copy in git to satisfy this check would be the wrong trade.
set -e
# The default search: a checkout placed anywhere on this machine, including the two places
# a previous session put JUCE 8.0.4 (a /tmp download does not survive a restart, a
# /opt or ~ copy does).  Unset and absent is UNAVAILABLE, never green.
for candidate in /tmp/JUCE-8.0.4 "$HOME/JUCE-8.0.4" /opt/JUCE-8.0.4; do
    if [ -d "$candidate/modules" ]; then JUCE_DIR="${JUCE_DIR:-$candidate}"; fi
done
JUCE_DIR="${JUCE_DIR:-/tmp/JUCE-8.0.4}"
if [ ! -d "$JUCE_DIR/modules" ]; then
    echo "no JUCE source at $JUCE_DIR - set JUCE_DIR to a JUCE 8 checkout" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
# juce_add_binary_data generates this at CMake configure time; for a syntax check
# a declaration is enough (nothing is linked here).
cat > "$tmp/BinaryData.h" <<'HDR'
#pragma once
#include <cstddef>
namespace BinaryData {
    extern const char* CairoRegular_ttf;      extern const int CairoRegular_ttfSize;
    extern const char* CairoSemiBold_ttf;     extern const int CairoSemiBold_ttfSize;
}
HDR
cat > "$tmp/tu.cpp" <<CPP
#include "$here/Source/PluginEditor.cpp"
CPP
g++ -std=c++17 -fsyntax-only -I"$here/Source" -I"$tmp" -I"$JUCE_DIR/modules" \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_devices=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_utils=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 \
    -DJUCE_PLUGINHOST_VST3=0 -DJUCE_PLUGINHOST_AU=0 -DJUCE_STANDALONE_APPLICATION=0 \
    "$tmp/tu.cpp"
# the text funnel is compiled too: a font that does not load is a build problem
cat > "$tmp/tu2.cpp" <<CPP
#include "$here/Source/ui/ArabicText.cpp"
CPP
g++ -std=c++17 -fsyntax-only -I"$here/Source" -I"$tmp" -I"$JUCE_DIR/modules" \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    "$tmp/tu2.cpp"
# the screenshot harness is compiled too: it is the thing that has to run on the
# CI runner, and a typo in it costs six minutes of Windows build to find
printf '#include "%s/Tests/ui_shot.cpp"\n' "$here" > "$tmp/tu3.cpp"
g++ -std=c++17 -fsyntax-only -I"$here/Source" -I"$tmp" -I"$JUCE_DIR/modules" \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_devices=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_utils=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_MODAL_LOOPS_PERMITTED=1 "$tmp/tu3.cpp"
# The processor is the other translation unit the plugin is made of, and it is the one
# that owns the project state (the reference text lives in it).  The editor includes its
# header, which catches the declarations; this catches the definitions.
printf '#include "%s/Source/PluginProcessor.cpp"\n' "$here" > "$tmp/tu4.cpp"
g++ -std=c++17 -fsyntax-only -I"$here/Source" -I"$tmp" -I"$JUCE_DIR/modules" \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_devices=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_utils=1 -DJUCE_MODULE_AVAILABLE_juce_dsp=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 \
    -DJUCE_PLUGINHOST_VST3=0 -DJUCE_PLUGINHOST_AU=0 -DJUCE_STANDALONE_APPLICATION=0 \
    "$tmp/tu4.cpp"
echo "editor, processor and harness compile against $JUCE_DIR"
