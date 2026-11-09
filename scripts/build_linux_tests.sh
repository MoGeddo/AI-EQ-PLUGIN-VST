#!/bin/sh
# build_linux_tests.sh - every C++ gate, built where there is no Visual Studio.
#
# The ledger's CHECK lines name `build/aeq_tests`, `build/aeq_commit` and so on, which is
# what the Windows workflow produces.  On a Linux box (and in CI other than the release
# job) those files do not exist, so while the gates were being "verified" by hand they
# were only ever verified on the machine that built them - and 36 of the 59 could not be
# checked at all (scripts/gate_check.py says so out loud now).
#
# This script builds the same binaries with the same sources and flags, so a gate runs
# here and prints what its EXPECT expects.  Nothing about the product changes: it only
# makes the claims executable somewhere other than one laptop.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build

CXX=${CXX:-g++}
FLAGS="-std=c++17 -O2 -Iplugin/Source"

# name -> source
build() {
    printf '  %-14s ' "$1"
    if $CXX $FLAGS "$2" -o "build/$1" 2>"build/$1.log"; then
        echo ok
    else
        echo FAILED
        head -5 "build/$1.log"
        return 1
    fi
}

build aeq_tests                  plugin/Tests/test_core.cpp
build aeq_commit                 plugin/Tests/test_commit.cpp
build aeq_tracked                plugin/Tests/test_tracked.cpp
build aeq_layers                 plugin/Tests/test_layers.cpp
build aeq_auto                   plugin/Tests/test_auto.cpp
build aeq_bounds                 plugin/Tests/test_bounds.cpp
build aeq_smooth                 plugin/Tests/test_smooth.cpp
build aeq_threads                plugin/Tests/test_threads.cpp
build aeq_stabilizer             plugin/Tests/test_stabilizer.cpp
build aeq_stabilizer_chain       plugin/Tests/test_integration_stabilizer.cpp
build aeq_profile                plugin/Tests/test_profile.cpp
build aeq_live                   plugin/Tests/test_live.cpp
build aeq_damage                 plugin/Tests/test_damage.cpp
build aeq_cli                    plugin/cli/main.cpp

echo "built $(ls build/aeq_* 2>/dev/null | wc -l) binaries in build/"
echo "next: python3 scripts/gate_check.py"
