#!/bin/bash
# Fast iteration loop: rebuild the Standalone target and launch it directly,
# skipping Logic Pro X entirely. Run this after any source change.
set -e
export PATH="/Applications/CMake.app/Contents/bin:$PATH"
cd "$(dirname "$0")"

cmake --build build --target JustifiedLevelControl_Standalone -j"$(sysctl -n hw.ncpu)"

APP="build/JustifiedLevelControl_artefacts/Release/Standalone/Justified Level Control.app"
open "$APP"
