#!/usr/bin/env bash
#
# Screenshot of the REAL editor on the real processor, fed live hits (macOS, dev tool).
#
#   scripts/shot.sh <out.png> [preset index] [javascript to run in the page after 3 s]
#
# Needs build/BounceShot (cmake --build build --target BounceShot). The window is kept on top:
# an occluded WKWebView stops running requestAnimationFrame and would capture stale.

set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tools
[ build/tools/winid -nt scripts/winid.swift ] 2>/dev/null || swiftc -O scripts/winid.swift -o build/tools/winid
build/BounceShot_artefacts/Release/BounceShot 8 "${2:--1}" "${3:-}" > build/tools/shot.log 2>&1 &
sleep 5
screencapture -x -o -l "$(build/tools/winid)" "$1"
wait
grep -E "script:|bounds" build/tools/shot.log || true
