#!/usr/bin/env bash
#
# Builds BOUNCE on macOS (universal arm64 + x86_64, minimum macOS 10.13), runs the test suite,
# validates with pluginval / auval when available, and zips the plug-ins into dist/.
#
#   scripts/build-macos.sh            Release build + tests + validation + zip
#   SKIP_VALIDATE=1 scripts/build-macos.sh
#
# Needs: CMake >= 3.22, Xcode command-line tools, JUCE 9.0.0 at ~/JUCE (or -DBOUNCE_JUCE_DIR).

set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)" \
      --target Bounce_VST3 Bounce_AU Bounce_Standalone BounceTests

echo "== tests"
build/BounceTests_artefacts/Release/BounceTests | tail -1

ART=build/Bounce_artefacts/Release
if [ -z "${SKIP_VALIDATE:-}" ]; then
    PV=""
    for c in /Applications/pluginval.app/Contents/MacOS/pluginval \
             "$HOME/Applications/pluginval.app/Contents/MacOS/pluginval" \
             "$(command -v pluginval || true)"; do
        [ -n "$c" ] && [ -x "$c" ] && PV="$c" && break
    done
    if [ -n "$PV" ]; then
        for p in "$ART/VST3/BOUNCE.vst3" "$ART/AU/BOUNCE.component"; do
            printf "== pluginval strictness 10: %s  " "$(basename "$p")"
            "$PV" --strictness-level 10 --validate "$p" 2>&1 | grep -E "^SUCCESS|^FAILED" | tail -1
        done
    else
        echo "== pluginval not found - skipped (https://github.com/Tracktion/pluginval/releases)"
    fi
    echo "== auval"
    auval -v aufx Bnce Naam 2>&1 | grep -E "VALIDATION" || true
fi

echo "== minimum macOS"
otool -arch x86_64 -l "$ART/VST3/BOUNCE.vst3/Contents/MacOS/BOUNCE" | grep -A3 LC_VERSION_MIN_MACOSX | grep version || true

VERSION=$(grep -E "^project\(Bounce VERSION" CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
mkdir -p dist
STAGE=$(mktemp -d)
cp -R "$ART/VST3/BOUNCE.vst3" "$ART/AU/BOUNCE.component" "$ART/Standalone/BOUNCE.app" "$STAGE/"
(cd "$STAGE" && zip -qry "$OLDPWD/dist/BOUNCE-$VERSION-macOS.zip" .)
rm -rf "$STAGE"
echo "== dist/BOUNCE-$VERSION-macOS.zip"
