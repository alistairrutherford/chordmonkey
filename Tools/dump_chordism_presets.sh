#!/usr/bin/env bash
# Writes Tests/fixtures/chordism_presets.json: Chordism's factory presets and parameter defaults,
# the reference for ChordMonkey's preset test (ChordChimp's PLAN.md §8). Builds Tools/ChordismPresetDump (a
# console app compiling Chordism's own FactoryPresets.cpp and Parameters.cpp) in dist/presetdump,
# leaving both checkouts alone. Run it again only if Chordism's presets are meant to change the
# reference.
#
#   Tools/dump_chordism_presets.sh [--chordism DIR] [--juce DIR]
#
#   --chordism DIR   the Chordism checkout (default: ../chordism next to this one)
#   --juce DIR       the JUCE 8.0.14 checkout (default: $JUCE_DIR, else ~/Development/JUCE)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHORDISM="$REPO/../chordism"
JUCE="${JUCE_DIR:-$HOME/Development/JUCE}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --chordism) CHORDISM="$2"; shift ;;
        --juce) JUCE="$2"; shift ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

[[ -f "$CHORDISM/Source/plugin/FactoryPresets.cpp" ]] || { echo "no Chordism checkout at $CHORDISM" >&2; exit 1; }
CHORDISM="$(cd "$CHORDISM" && pwd)"
JUCE="$(cd "$JUCE" && pwd)"
PROJUCER="$JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer"
[[ -x "$PROJUCER" ]] || { echo "no Projucer at $PROJUCER (Tools/install.sh builds it)" >&2; exit 1; }

BUILD="$REPO/dist/presetdump"
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp "$REPO/Tools/ChordismPresetDump/Main.cpp" "$BUILD/"
sed -e "s#CHORDISM_DIR#$CHORDISM#g" -e "s#JUCE_MODULES#$JUCE/modules#g" \
    "$REPO/Tools/ChordismPresetDump/ChordismPresetDump.jucer" > "$BUILD/ChordismPresetDump.jucer"
"$PROJUCER" --resave "$BUILD/ChordismPresetDump.jucer" > "$BUILD/projucer.log" 2>&1 \
    || { cat "$BUILD/projucer.log" >&2; exit 1; }
xcodebuild -project "$BUILD/Builds/MacOSX/ChordismPresetDump.xcodeproj" -configuration Release build > "$BUILD/xcodebuild.log" 2>&1 \
    || { grep -E "error:" "$BUILD/xcodebuild.log" | sort -u | head -20 >&2; exit 1; }

commit="$(git -C "$CHORDISM" rev-parse --short HEAD 2> /dev/null || echo unknown)"
if [[ -n "$(git -C "$CHORDISM" status --porcelain -- Source 2> /dev/null)" ]]; then
    commit="$commit (with uncommitted changes)"
fi
"$BUILD/Builds/MacOSX/build/Release/ChordismPresetDump" "$REPO/Tests/fixtures/chordism_presets.json" "$commit"
