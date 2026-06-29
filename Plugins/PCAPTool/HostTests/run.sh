#!/usr/bin/env bash
# Build + run the host-side VCam logic tests with plain clang (no Unreal Engine required).
# Compiles the REAL FVCamInputLayer / FPCAPVCamProcessor translation units against the
# UE-math stubs in ./stubs. From anywhere:  Plugins/PCAPTool/HostTests/run.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../Source/PCAPTool"
OUT="$HERE/run_tests"

CXX="${CXX:-clang++}"

"$CXX" -std=c++17 -Wall -Wextra -Wno-unused-parameter -O0 -g \
    -I "$HERE/stubs" \
    -I "$SRC/Public" \
    "$HERE/stubs/stub_globals.cpp" \
    "$SRC/Private/VCamInputLayer.cpp" \
    "$SRC/Private/VCamProcessor.cpp" \
    "$HERE/test_main.cpp" \
    -o "$OUT"

echo "── running host tests ──"
"$OUT"
