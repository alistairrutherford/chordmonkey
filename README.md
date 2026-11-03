# ChordMonkey

A MIDI chord workstation plug-in (VST3) for Ableton Live on macOS, after the manner of a simplified
Scaler 2: a scale's chords, chord sets, patterns of chords that play in time with Live, Bind and
Keys-Lock, and drag-out to MIDI clips. It's a fork of [ChordChimp](../chordchimp). The plan and
status are in [PLAN.md](PLAN.md).

**Status:** Phases 0-7 done: the fork (checked in Live 12.4.16 next to ChordChimp), the
workspace model (patterns of chords, chord sets as Roman numerals, saved with the set) and
pattern playback (**Play** and the pattern tabs **A–D**: the pattern plays in time with Live,
through the effects) and Bind and Keys-Lock (the Keyboard row: a zone of keys plays the
pattern's slots or the palette and switches patterns, and the keys above it are locked to the
scale or the chord), and voicing and performance (Voicing, Bass, 17 rhythm Styles, Swing and
Humanize), and chord sets and presets (41 factory chord sets; presets that hold the
workspace, including 10 Songwriting presets: pick one in the preset bar or Live's Program and
press Play), and the editor's workspace (drag chord pads into the pattern's slots, reorder and
edit them, load chord sets, and drag the pattern into Live as a MIDI clip). With Bind
on **All keys** (the default) the keys play exactly like ChordChimp: every white key its chord,
the black keys borrowed chords, with ChordChimp's effects, presets, Analyse and Export. All of it was
checked by hand in Live (Phase 7): a dragged-out clip is note for note what ChordMonkey plays, and
a saved set reopens with its patterns. The rest of the Scaler-style features come in Phases 8–9.

## Install

Needs Xcode (open it once after installing) and JUCE 8.0.14:

```sh
git clone --branch 8.0.14 https://github.com/juce-framework/JUCE.git ~/Development/JUCE
Tools/install.sh              # build, sign ad hoc, install for this user
```

`Tools/install.sh` builds the VST3 and the Standalone (Release, universal) in `dist/build`, signs
both ad hoc and installs `~/Library/Audio/Plug-Ins/VST3/ChordMonkey.vst3` and
`~/Applications/ChordMonkey.app` for the current user. `--juce DIR` for JUCE elsewhere,
`--validate` to run pluginval on the installed plug-in, `--uninstall` to remove both. Then rescan
plug-ins in Live.

ChordMonkey has its own bundle ID (`com.alistairrutherford.chordmonkey`), plug-in code (`Cmky`) and
preset folder (`~/Music/ChordMonkey/Presets`), so it installs and loads next to ChordChimp and
Chordism. It reads ChordChimp and Chordism preset files.

## Build (development)

```sh
PROJUCER=~/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer
"$PROJUCER" --resave ChordMonkey.jucer
xcodebuild -project Builds/MacOSX/ChordMonkey.xcodeproj -target "ChordMonkey - All" -configuration Release build
```

## Test

```sh
Tests/run_tests.sh          # core tests (no JUCE)
Tests/run_snapshot.sh       # editor snapshots, parameters, state, presets
Tests/run_validation.sh     # pluginval strictness 10 on the installed VST3
Tests/run_bench.sh          # CPU per instance
```

Live-in-the-loop checks use the Ableton MCP plug-in (ClaudeLiveBridge, port 9873); see PLAN.md §7.

## In Live

ChordMonkey (VST3) on a MIDI track, armed or monitoring *In*, with your keyboard as its MIDI From;
an instrument on another track with *MIDI From: ChordMonkey track → ChordMonkey*.
