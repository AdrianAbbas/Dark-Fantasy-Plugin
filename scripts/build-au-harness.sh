#!/usr/bin/env bash
# Build the AudioToolbox host harness (tools/au-harness) as a universal binary.
# Usage: scripts/build-au-harness.sh [output-path]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-$here/build/VeilAUHarness}"
mkdir -p "$(dirname "$out")"
/usr/bin/xcrun clang++ -std=c++20 -fobjc-arc -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 \
    "$here/tools/au-harness/VeilAUHarness.mm" -o "$out" \
    -framework AppKit -framework AudioToolbox -framework AudioUnit -framework AVFoundation \
    -framework CoreAudio -framework CoreAudioKit -framework Foundation
/usr/bin/codesign --force --sign - "$out"
printf 'Built %s (%s)\n' "$out" "$(/usr/bin/lipo -archs "$out")"
