# ChordMonkey: Project Plan

A macOS VST3 MIDI plug-in for Ableton Live that works like a **simplified Scaler 2**: pick a key
and scale, browse its chords and ready-made chord sets, arrange chords into short patterns, play
them from a few keys or let them play in time with Live, give them a performance (rhythm, strum,
arp, bass), and drag the result out as a MIDI clip. ChordMonkey makes no sound of its own. It sends
MIDI to an instrument on another track, as ChordChimp and Chordism do.

**Starting point:** a fork of **ChordChimp** (`../chordchimp`), which already has the chord theory,
the Chord FX engine, key detection, Export drag-out, presets, the install script and the test
setup. ChordMonkey adds the parts that make Scaler what it is: the **workspace** (patterns of
chords), **chord sets**, **Bind**, **Keys-Lock**, **host-synced playback**, **voicings** and
**Detect**.

**Clean room.** As with Chordism and ChordPrism, ChordMonkey copies *behaviour* only. No code,
artwork, sounds, preset or chord-set data, manual text or product name come from Scaler. The
factory chord sets are our own, written as Roman numerals (§3.5). There are no artist or song sets.

---

## 1. Goals

| # | Requirement | Acceptance criterion |
|---|-------------|----------------------|
| G1 | Loads in Live 12 as a VST3 and sends MIDI to an instrument on another track | `pluginval` strictness 10 passes. In Live, an instrument with *MIDI From: ChordMonkey* plays and records the output sample-accurately. |
| G2 | **Scale palette:** the chords of any Key × Scale, as triads up to 9ths, plus borrowed chords | Theory tests against the music21 fixture (inherited), extended to the chord-type choices (§3.1). |
| G3 | **Workspace:** 4 patterns (A–D) of up to 8 chord slots, each with length, inversion and octave | Model tests: edit, reorder, copy, transpose, save/restore round trip. Saved with the Live set. |
| G4 | **Plays in time with Live:** the current pattern loops, synced to the transport | Fake-transport tests: every chord change lands on its beat at 32–2048 samples, 44.1–96 kHz, across loops, locates and tempo changes. Recorded in Live at 0.000 ms. |
| G5 | **Bind and Keys-Lock:** keys trigger slots and switch patterns; the rest of the keyboard is locked to the scale or the chord | Router tests over all 128 notes in every mode (§3.3). |
| G6 | **Performance:** a rhythm style, voicing, bass, humanize, plus ChordChimp's Strum/Arp/Repeat/Velocity | Each style's notes match its table for a known pattern; humanize stays within its bounds and is repeatable with a seed. |
| G7 | **Chord sets:** about 40 factory progressions by genre and mood, plus user sets | Every set loads into a pattern in all 12 keys; the names and numerals display correctly. |
| G8 | **Detect:** capture chords played into ChordMonkey (keyboard or a clip) into a pattern, and guess the key | Chord recognition tests (inversions, extensions, slash chords, sloppy timing); a clip in Live becomes the right pattern. |
| G9 | **Export:** drag a pattern, performance included, into Live as a MIDI clip | The exported file equals the notes recorded live from the same pattern. |
| G10 | Real-time safe, no stuck notes | ChordChimp's rules: no allocation or locks in `processBlock`, all-notes-off on stop, bypass, preset/pattern change and deletion; the stuck-note fuzz passes. |
| G11 | Simple enough to stay under Live's 64-parameter auto-populate limit | ≤ 64 host parameters, so no `Options.txt` change is needed (Chordism needed one). |

**Out of scope:** Windows, AU, AAX, built-in sounds, audio detection, MPE, modulation and
"Live Suggest" modes, artist and song chord sets, the Pad page, Melody mode, Scaler 3's
Arrange/Song pages.

---

## 2. Feature map (Scaler 2 → ChordMonkey)

From the research in §12. **P1** is the core (Phases 0–6), **P2** finishes the feature set
(Phases 7–9), **P3** is optional, later.

| Area | Scaler 2 | ChordMonkey | Pri |
|---|---|---|---|
| Scales | Hundreds of scales and modes; browse by name | ChordChimp's 13 scales (the 7 modes, harmonic and melodic minor, …). Add more only if asked | P1 |
| Scale chords | Chords of the scale, with a chord-type switch (triads, 7ths, 9ths, …) | The **palette row**: 7 diatonic chords + 5 borrowed (ChordChimp's black keys), shaped by **Chord type** (ChordChimp's Chord Group) | P1 |
| Chord sets | Genre, mood, artist and song sets (1,000+) | **~40 of our own**, by genre and mood, stored as numerals; user sets saved as files | P1 |
| Workspace | Section C: chords dragged into patterns; several patterns | **4 patterns × 8 slots**; drag from palette or chord set; per slot: length, inversion, octave | P1 |
| Edit | Per chord: length, velocity, inversion, octave, timing | Length, inversion, octave per slot; velocity and timing come from the performance | P1 |
| Playback | Sync to DAW, loop a pattern | **Play** on/off: loops the current pattern in time with the transport; one-shot preview by clicking a slot | P1 |
| Bind | Chords bound to white keys (or all keys, or "true scale") from any start note; keys switch patterns | **Bind** zone from a start note: white keys = slots 1–8, four black keys = patterns A–D (§3.3). Source: Workspace or Palette | P1 |
| Keys-Lock | Scale Notes Mapped, Scale Notes Only, Scale White Keys, Chord Notes, Chord Extensions | All five, above the Bind zone (§3.3) | P1 (Extensions P2) |
| Voicing | Voicing menu, voice grouping, dynamic voicing, voice leading | **Voicing:** Close, Open (drop-2), Spread, Wide; **Voice leading** Off/Smooth (ChordChimp's) | P1 |
| Performance | 500+ "expressions": phrases, rhythms, strums, arps; Bass mode; swing; humanize | **Style** (~16 rhythm tables of our own) + ChordChimp's Strum, Arp, Repeat, Velocity; **Bass** (root an octave down); **Swing**; **Humanize** | P1 |
| Detect | Chords and key from MIDI or audio | **MIDI only:** chords played into a pattern; key guess by ChordChimp's `KeyListener` | P2 |
| Chord exploder / suggestions | Substitutes, variations, compatible chords | Right-click a slot: a short list of **substitutes** (relative, sus2/sus4, add9, tritone sub, borrowed) | P2 |
| MIDI out | Drag chords or the performance out as MIDI; MIDI out to a track | **Export** a pattern (rendered offline, §3.6); live MIDI out via *MIDI From* | P1 / P2 |
| Automation | Parameters automatable, MIDI CC | All parameters are host parameters (APVTS) | P1 |
| Circle of fifths, modulation | Circle-of-fifths browser, modulation suggestions | **Dropped** (maybe P3: "pivot chord to key X") | — |
| Sounds | 30–45 built-in instruments | **Dropped.** Output goes to any Live instrument. A tiny preview synth in the Standalone only is P3 | — |
| Audio detection, MPE, Pad page, Melody mode | | **Dropped** | — |

---

## 3. Functional design

### 3.1 Palette (the scale's chords)

- **Key** (12), **Scale** (ChordChimp's list, same order), **Chord type**: ChordChimp's Chord Group
  list (Triads, Sevenths, Ninths, Sus2, Sus4, …, same IDs and order).
- The palette row shows the 7 diatonic chords and the 5 borrowed chords (bII bIII #iv° bVI bVII)
  with name and numeral (`ChordNamer`, `RomanNumeral`). Clicking one plays it; dragging it drops it
  into a workspace slot.
- **Progression** (ChordChimp's 8 orders of the white keys) is **dropped**: chord sets and the
  workspace replace it.

### 3.2 Workspace

```
Pattern A  [ C    | Am   | F    | G    |      |      |      |      ]  ← 8 slots
            2 bt   2 bt   4 bt   4 bt
Pattern B  [ Dm7  | G7   | Cmaj7| ...                              ]
```

- A **slot** holds an absolute chord: a root pitch class and the chord's **tones** in semitones
  above it (up to 8, placed above C2), **length** (½–8 beats, default 4), **inversion** (root,
  1st, 2nd, 3rd: each moves the lowest note up an octave), **octave** (−2…+2). An empty slot
  ends the pattern. *As built (Phase 1):* tones rather than a chord-type index, so any palette
  chord (every Chord Group, scale and borrowed chord: 31,824 cases tested) copies in with exactly
  its notes; `ChordTypes` (Chordism's table) names the shapes for chord sets and menus. Each
  pattern also keeps the key it was written in, for Transpose to key.
- Chords are stored **absolute**, so changing Key doesn't rewrite a pattern (as in Scaler). A
  **Transpose to key** action moves a pattern to the current Key; loading a chord set writes it
  in the current Key.
- Edits: drag to reorder, Alt-drag to copy, Delete, right-click for length, inversion, octave and
  substitutes (P2). Copy/paste a whole pattern between A–D.
- The workspace is **plug-in state**, not parameters: it's saved with the Live set and in presets.
  It reaches the audio thread through ChordChimp's `SnapshotExchange` (a whole new pattern table,
  swapped lock-free).
- **Current pattern** (A–D) is a host parameter, so Live can automate a pattern change.

### 3.3 Keyboard: Bind and Keys-Lock

```
 below Bind start      Bind zone (one octave from Bind start, default C1 = MIDI 36)    above
 ───────────────  ┌─────────────────────────────────────────────────────┐  ───────────────
 pass through     │ white C D E F G A B C' → slots 1–8                   │  Keys-Lock mode
                  │ black C# D# F# G#      → pattern A B C D             │  (or pass through)
                  │ black A#               → stop / release              │
                  └─────────────────────────────────────────────────────┘
```

- **Bind source:** *Workspace* (the current pattern's slots) or *Palette* (slots 1–7 = the scale
  chords I–VII, slot 8 = I an octave up; black keys play the borrowed chords instead of switching
  patterns). Palette mode is ChordChimp's keyboard, confined to one octave.
- **Bind start** is a parameter (any note, default C1), so a 25-key controller can put it at the
  bottom and play Keys-Lock with the right hand.
- A bound key plays its chord with the performance (§3.4) for as long as it's held. While **Play**
  is on, a bound key instead **jumps** playback to that slot at the next beat (Scaler's "trigger a
  block"), and a pattern key switches pattern at the next bar.
- **Keys-Lock** (above the Bind zone), Scaler's five modes:
  | Mode | Behaviour (D major example) |
  |---|---|
  | Off | Notes pass through |
  | Scale notes mapped | Off-scale keys move to the nearest scale note (F → F#) |
  | Scale notes only | Off-scale keys are silent |
  | Scale white keys | White keys play the scale in order (C=D, D=E, E=F# …); black keys silent |
  | Chord notes | White keys play the **current chord's** tones up the keyboard; black keys silent |
  | Chord extensions (P2) | As Chord notes, but the full stack to the 13th |
  "Current chord" is the last bound or playing chord, so a melody follows the pattern.
- **Enable** off passes everything through, as in ChordChimp.

### 3.4 Performance

The chain, ChordChimp's with three new stages (in **bold**):

```
slot → chord → inversion/octave → **Voicing** → voice leading → Stack → **Bass**
     → [**Style** | Strum | Arp] → Repeat → Velocity → **Humanize** → Hold → out
```

- **Voicing:** Close (as now), Open (drop-2), Spread (root low, the rest an octave up), Wide (drop-2
  and drop-4). Applied before voice leading, so Smooth picks the inversion of the voiced chord.
- **Bass:** Off / Root / Root + 5th; the bass note an octave below the voicing's lowest note, on
  the style's downbeats.
- **Style** (rhythm): Block (hold the slot's length), plus ~15 fixed tables of our own: 1/4 pulse,
  1/8 pulse, off-beat, push (anticipate by an 1/8), Charleston, half-bar, reggae skank, house stab,
  ballad (bass on 1, chord on 2-and), broken chord up/down, … Each is a list of
  `(beat, length, voices, velocity)` over one bar, repeated across the slot's length. ChordChimp's
  Step Sequencer is folded into this (its 4 factory patterns become styles).
- **Strum, Arp, Repeat, Velocity, Hold:** ChordChimp's, unchanged. Strum and Arp are alternatives to
  a style, as Strum and Arp already are to each other.
- **Swing** (0–75%) applies to 1/8 and 1/16 off-beats; **Humanize** (0–100%) adds seeded timing
  (≤ 15 ms) and velocity (≤ ±12) jitter. The seed is fixed per pattern, so a take is repeatable.
  Humanize may move a note **later** only, never earlier than its grid position (no look-ahead).

### 3.5 Chord sets and presets

- **Chord sets** are progressions stored as numerals plus lengths, e.g. `I:4 V:4 vi:4 IV:4`, with a
  suggested scale and style. Loading one writes it into the current pattern in the current Key.
  *As built (Phase 1, `core/model/ChordSets.h`):* numerals count by the major scale's degrees
  with `b`/`#` for the rest (`i bVI bIII bVII`), so a set works in any key and scale; case gives
  the 3rd, the suffix the type (`V7`, `IVmaj7`, `ii7`, `viiø7`, `vii°7`, `IVsus4`, `bVII9`); a
  length is beats after `:` (default 4, halves allowed); a shape no type names keeps its tones,
  `V(0.7.16)`. Every chord writes a numeral that reads back as the same chord.
  About 40 factory sets in 6 categories: Pop, Rock, Jazz & Soul, EDM & House, Cinematic, Moods
  (Happy, Sad, Dreamy, Dark, …). All are common-practice progressions, written by us.
- **User chord sets:** *Save pattern as chord set* writes `~/Music/ChordMonkey/ChordSets/User/<Name>.json`
  (numerals, relative to Key, so they transpose).
- **Presets** (ChordChimp's `PresetManager` and `PresetBar`) hold the parameters **and** the
  workspace: `~/Music/ChordMonkey/Presets/<Category>/<Name>.xml`. Factory presets: Init plus ~12
  combining a chord set, a style and a voicing (e.g. *Neo-soul Sevenths*, *House Stabs*, *Cinematic
  Spread*). They're also the plug-in's programs (program 0 = Init), so the bridge can load them.

### 3.6 Detect and Export

- **Detect** (P2) is a parameter: Off / **Capture** / Use result. In Capture, ChordMonkey plays
  nothing; every note passes through; a `ChordListener` groups notes that start within 60 ms into
  chords, recognises each (Chordism's `ChordImporter` recognition, without the import file), and
  measures its length on the beat grid. ChordChimp's `KeyListener` guesses the key at the same
  time. *Use result* writes the chords (up to 8) into the current pattern and sets Key/Scale to the
  guess, then returns to Off. A capture from a clip in Live: set the ChordMonkey track's *MIDI From*
  to the clip's track, play, Use result.
- **Export** (P1): a drag handle on each pattern. It renders the pattern **offline**, through the
  same engine with a fake transport at the set's tempo, into a `.mid` (ChordChimp's `MidiFile`):
  one loop of the pattern, performance included, or *chords only* with Alt. Rendering offline,
  instead of ChordChimp's "last 32 bars played", means the clip is available before playing.

### 3.7 Parameters (target ≈ 40)

| Group | Parameters |
|---|---|
| Global | `enable`, `key`, `scale`, `chordGroup` (Chord type), `chordOctave` |
| Workspace | `pattern` (A–D), `play` (Off/On), `bindSource`, `bindStart`, `keysLock` |
| Voicing | `voicing`, `voiceLeading`, `stack`, `bass` |
| Performance | `style`, `swing`, `humanize`, `strum`, `strumRate`, `arp`, `arpRate`, `arpRange`, `arpGate`, `repeat`, `repeatRate`, `repeatGate`, `velocity`, `velocityPeak`, `velocityRange`, `hold` |
| Detect | `detect` (Off / Capture / Use result; not saved) |
| Test rig | `testPattern`, `testWorkspace` (Phase 2: loads the rig's `Workspace.txt`, one progression per pattern, so the bridge can give the plug-in patterns), `testSlot` (plays slot n at the next beat, for the bridge) |

ChordChimp's IDs are kept for every parameter carried over, so its presets load (unknown IDs
ignored). `progression`, `seq*` and `keyOctave` are dropped. IDs and choice orders are fixed once
Phase 0 ships: **append only**.

---

## 4. Architecture

Same stack as the siblings: **JUCE 8.0.14, C++17, Projucer + xcodebuild**, JUCE at
`~/Development/JUCE`. VST3 + Standalone. Header-only core in plain C++ (no JUCE) so the core tests
run without a host.

```
                 ┌─ Bind zone ──► SlotTrigger ─┐
 MIDI in ─► KeyRouter                          ├─► ChordEngine ─► Performer ─► VoiceTracker ─► EventQueue ─► MIDI out
 (host/UI)       ├─ Lock zone ──► KeysLock ────┼──────────────────────────────────▲
                 └─ Detect ────► ChordListener + KeyListener ─► (FIFO) ─► message thread ─► Workspace
 Transport ─► Clock ─► PatternPlayer ──────────┘
 Workspace (message thread) ─► SnapshotExchange ─► PatternPlayer / SlotTrigger
```

### 4.1 Modules

| Module | From | Notes |
|---|---|---|
| `core/theory`: `Scales`, `ChordModel`, `ChordGroups`, `VoiceLeading`, `ChordNamer`, `RomanNumeral`, `KeyProfiles` | ChordChimp | As is |
| `core/theory/ChordTypes.h` | Chordism | Brought back (Phase 1): the table, with each type's numeral suffix and tones; no step editing |
| `core/theory/ChordRecognizer.h` | Chordism `ChordImporter` | The recognition part only |
| `core/theory/Voicings.h`, `Substitutes.h` | **New** | Drop-2/spread/wide; substitute lists |
| `core/model/Workspace.h`, `ChordSets.h` | **New** (Phase 1) | Patterns, slots, editing, transposing, the saved text; numerals ↔ chords, slot names (`ChordSets`, plural: `ChordSet` is ChordGroups.h's 12 palette slots) |
| `core/engine/PatternPlayer.h` | **New** (Phase 2) | Slot timeline from beat position; loops; jumps; pattern switch at the bar. Its triggers play through `ChordPerformer` as performance 128 on chord slots 12-19 |
| `core/engine/KeyRouter.h` | ChordChimp, extended (Phase 3) | `BindSource`; `route()`: pass / bound (white 0-7, black 0-4) / locked zones; All keys keeps ChordChimp's `slotOf` |
| `core/engine/KeysLock.h`, `Style.h`, `Humanize.h`, `ChordListener.h` | **New** | |
| `core/engine/*` (Clock, Transport, EventQueue, VoiceTracker, Strum, Arp, Repeat, Stack, Velocity, Hold, SnapshotExchange, KeyListener) | ChordChimp | As is; `StepSequencer` becomes `Style` |
| `core/midiio/MidiFile.h` | ChordChimp | `ExportRecorder` replaced by an offline renderer |
| `plugin/*` | ChordChimp, renamed | Workspace in the state XML; chord-set loader |
| `ui/*` | ChordChimp's `Theme`, `ParamControl`, `PresetBar` | New `PalettePanel`, `WorkspacePanel`, `PerformancePanel`, `ChordSetBrowser` |

**Copy, don't share** (ChordChimp §4.1's reasoning holds): ChordChimp is finished, and a shared
library would tie three projects together.

**Comments in the code** cite this plan as `PLAN.md §…`. Code copied from ChordChimp cites
*ChordChimp's PLAN.md §…* or *ChordChimp's docs/KEY_DETECTION_PLAN.md* (`../chordchimp/`), and
code ChordChimp took from Chordism cites *Chordism's PLAN.md §…*: those plans hold the design
behind it.

### 4.2 Identity

| | ChordMonkey |
|---|---|
| Bundle ID | `com.alistairrutherford.chordmonkey` |
| Manufacturer / plug-in code | `ARut` / `Cmky` |
| Installed as | `~/Library/Audio/Plug-Ins/VST3/ChordMonkey.vst3`, `~/Applications/ChordMonkey.app` |
| User data | `~/Music/ChordMonkey/{Presets,ChordSets}/` |

Fixed in Phase 0; never change them (Live sets refer to them).

---

## 5. UI

One page, about **900 × 520**, resizable; ChordChimp's dark theme with its own accent colour.

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ ChordMonkey [On]  ◀ Pop / Four Chords ▶ [Save]   Key [C] Scale [Major] Type [Triads]│
│ [Detect] Heard: C major 91%                                    [▶ Play]  Oct [0]    │
├──────────────────────────────────── PALETTE ───────────────────────────────────────┤
│  C    Dm    Em    F    G    Am   B°      │  D♭  E♭  F#°  A♭  B♭     [Chord sets ▾]  │
│  I    ii    iii   IV   V    vi   vii°    │  ♭II ♭III #iv° ♭VI ♭VII                   │
├──────────────────────────────────── WORKSPACE ─────────────────────────────────────┤
│ (A) B  C  D   [ C ][ G ][ Am ][ F ][   ][   ][   ][   ]   ⇩ drag MIDI               │
│               4    4    4     4      ▲ playing                                     │
├──────────────────────────────────── PERFORMANCE ───────────────────────────────────┤
│ Style [Off-beat ▾] Swing Humanize │ Voicing [Open ▾] Voice leading Bass [Root ▾]     │
│ Strum/Arp/Repeat cards (ChordChimp) │ Velocity [Curve ▾] Peak Range │ Hold          │
├────────────────────────────────────────────────────────────────────────────────────┤
│ Bind [Workspace ▾] from [C1]   Keys-Lock [Chord notes ▾]   ▁▂ output keyboard ▂▁     │
└────────────────────────────────────────────────────────────────────────────────────┘
```

- Palette chords and workspace slots are drag sources and targets inside the editor; the
  **⇩ drag MIDI** handle drags out to Live (`performExternalDragDropOfFiles`, proven in Chordism).
- The playing slot is highlighted from the audio thread's position (an atomic read on the timer).
- Controls build themselves from parameters (`ParamControl`), so the snapshot test still proves
  every parameter has a control.
- The output keyboard colours the Bind zone, the pattern keys and the Keys-Lock zone.

---

## 6. Milestones

| Phase | Est. | Deliverable | Exit criteria |
|---|---|---|---|
| **0. Fork and identity** | ½ day | `git init`; copy ChordChimp; rename (namespace `chordmonkey`, classes, bundle ID, code `Cmky`, folders); `Tools/install.sh` | Builds; `pluginval` 10; loads in Live next to ChordChimp, behaving like ChordChimp |
| **1. Workspace model** | 1 day | `ChordTypes` back, `Workspace`, `ChordSet` (numerals ↔ absolute), state XML | Model tests (G3); round trip through the plug-in state |
| **2. Pattern playback** | 1½ days | `PatternPlayer`, `play` and `pattern` parameters, `SnapshotExchange` for the workspace | Fake-transport tests (G4); stuck-note fuzz with pattern edits during playback |
| **3. Bind and Keys-Lock** | 1 day | `KeyRouter` zones, `SlotTrigger`, `KeysLock` (4 modes) | Router tests over 128 notes × every mode (G5) |
| **4. Voicing and performance** | 1½ days | `Voicings`, Bass, `Style` tables (Step Seq folded in), Swing, Humanize; parameters (§3.7) | Style tables and voicing tests (G6); 0 allocations in `processBlock` |
| **5. Chord sets and presets** | 1 day | ~40 chord sets; user sets; factory presets; ChordChimp presets load | Every set × 12 keys (G7); preset round trip with workspace |
| **6. UI** | 2–3 days | Palette, workspace (drag/drop, slot menus), performance panel, chord-set browser, Export handle with the offline renderer | Snapshot tests: every parameter has a control, reference images; export file = render (G9) |
| **7. Live check (P1 done)** | ½ day | Bridge tests (§7) and the manual checklist | Patterns record exactly at 0.000 ms; Bind and pattern switches; set save/reopen restores the workspace |
| **8. Detect** | 1 day | `ChordListener`, `detect` parameter, Heard box | Recognition tests (G8); a clip captured in Live by the bridge alone |
| **9. Substitutes, Chord extensions** | 1 day | Slot right-click substitutes; the fifth Keys-Lock mode | Substitute lists by hand for C major / A minor; router tests |
| **10. Hardening and install** | ½ day | Benchmark, CI, README, screenshot | 8 instances < 1% CPU each; `Tools/install.sh --validate` clean |

**Total: about 11–13 working days.** P1 (a usable Scaler-like tool) ends at Phase 7, about 9 days.

---

## 7. Testing

Inherited from ChordChimp, plus the new parts:

- **Core (no JUCE)** `Tests/run_tests.sh`: theory against music21; `test_workspace` (G3),
  `test_player` (G4: chord changes on beat across buffer sizes, sample rates, loops, locates, tempo
  changes, pattern switch at the bar, jump at the beat), `test_router` (G5), `test_style` and
  `test_voicing` (G6), `test_chordsets` (G7), `test_detect` (G8), stuck-note fuzz with random
  keys, edits, pattern switches and preset loads.
- **Snapshot** `Tests/run_snapshot.sh`: every parameter has a control; reference images; state
  and presets with the workspace; the export renderer; malloc hook during playback.
- **Host** `Tests/run_validation.sh`: `pluginval` strictness 10; `Tests/run_bench.sh`.
- **Live in the loop**, through the **Ableton MCP plug-in** (ClaudeLiveBridge, port 9873; Live
  12.4.16 answered on 2026-11-03), and the same commands from pytest via Chordism's
  `LiveBridge/livebridge/client.py` pointed at port 9873. One script per check, in
  `LiveBridge/tests/` (a copy of ChordChimp's approach: no Remote Script of our own):
  1. **Load and route:** `create_midi_track` ×2; `load_plugin` ChordMonkey on *CM Src*, Drift on
     *CM Rec*; `set_input_routing` *CM Rec* ← *CM Src / ChordMonkey*; `list_parameters` shows ≤ 64.
  2. **Pattern playback:** `set_preset` (Program) → a factory preset with a known chord set;
     `set_parameter play=On`; `start_recording` 8 bars at two tempos; `get_clip_notes` on *CM Rec*
     → compare with the core renderer's output for the same preset, every note at 0.000 ms.
  3. **Bind:** generate a `.mid` of bound keys and pattern keys; `load_clip_file` onto *CM Src*;
     record; compare.
  4. **Keys-Lock:** a chromatic-scale clip above the Bind zone in each mode; compare with the router
     table.
  5. **Detect:** `load_clip_file` a known progression on a third track; route it into *CM Src*;
     `detect=Capture`, play, `detect=Use result`, `play=On`, record; the take equals the clip's
     chords.
  6. **No stuck notes:** stop mid-chord, switch pattern, bypass, delete the device; `get_meters`
     at 0 on *CM Rec*.
  7. **Save/reopen:** save the set, restart Live (`scripts/restart_live.sh` from Chordism), the
     workspace and Program restored.

  Known bridge quirk (ChordChimp Phase 5): arrangement recording always starts at beat 0 and a few
  seconds late, so compare clips that start at 0 and keep one take per clip.
- **Manual checklist:** dragging palette → workspace, drag-out into Live at the drop point,
  resizing, a hardware keyboard on Bind and Keys-Lock.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Workspace edits while playing cause clicks or stuck notes | Swap the whole pattern table at a slot boundary; the `VoiceTracker` ends voices of a slot that disappears; covered by the fuzz |
| Bound key "jump" while playing feels late (next beat) | Option *Jump: next beat / next 1/16 / immediately* if it feels wrong in Live |
| The 64-parameter budget | Workspace is state, not parameters; ≈ 40 planned leaves room |
| Chord recognition on real playing (rolled chords, pedal) | 60 ms grouping window, sustain-pedal aware, tested on recorded takes; *Use result* is a separate step, so a bad capture is easy to throw away |
| Scope creep toward full Scaler | §1's out-of-scope list; P3 items only after Phase 10 |
| Clean-room line | Our own chord-set data, style tables, names and artwork; no Scaler assets or text in the repo |

---

## 9. Open questions

1. **Bind layout:** one octave (8 slots + 4 pattern keys) as planned, or Scaler's "all keys" option
   (12 slots per octave)?
2. **Keys-Lock without Bind:** should Keys-Lock cover the whole keyboard when Bind is off? (Plan:
   yes; Bind *Off* is a `bindSource` choice.)
3. **Slots per pattern:** 8 (planned) or 16?
4. **Preview sound:** is a small built-in synth in the Standalone worth having, so the editor
   can be tried without Live?
5. **ChordChimp's Progression and Step Seq:** dropped here (chord sets and styles replace them).
   Keep them instead, for ChordChimp preset compatibility?

---

## 10. Status

| Phase | Status | Detail |
|---|---|---|
| 0. Fork and identity | **Done (2026-11-03)** | Copied from ChordChimp at `e3e349a` (its tracked `Source/`, `Tests/`, `Tools/`, CI and `.jucer`s; not its plan, README, docs or generated Xcode projects). Renamed: namespace `chordmonkey`, `ChordMonkeyProcessor`, `ChordMonkeyEditor`, the snapshot app, bundle ID `com.alistairrutherford.chordmonkey`, plug-in code `Cmky` (so its VST3 class IDs differ from ChordChimp's), presets in `~/Music/ChordMonkey/Presets`, description *"Chords from a scale, arranged into patterns"*. Comments citing ChordChimp's plan now say so (§4.1). `PresetManager::readFile` also reads ChordChimp preset files (root tag `ChordChimp`), as well as Chordism's. Checks: `Tests/run_tests.sh` all passed; `Tests/run_snapshot.sh` all passed (reference images updated for the new name); `Tools/install.sh --validate` built Release (universal), installed `ChordMonkey.vst3` and `ChordMonkey.app` next to ChordChimp and Chordism, and `pluginval` strictness 10 passed. **In Live** (12.4.16, set `~/Music/Ableton/Projects/ChordMonkey Phase 0 Project`, by the Ableton MCP alone): Live lists ChordMonkey and ChordChimp separately (VST3 › Alistair Rutherford); ChordMonkey shows 28 parameters + Program + Device On = 30, as ChordChimp. Each loaded on its own MIDI track feeding a Drift track through *MIDI From: <track> → <plug-in>*. **Test-pattern take (Init):** about 20 beats from each, identical note for note, 63 notes: C-major triads, slot = beat mod 7, every note on the beat and an eighth long. Meters at 0 after the stop. **Bridge notes:** `start_recording`'s `length_beats` didn't stop the take (stopped by hand), and Live counts in first. **Not checked:** the editor in Live, set save/reopen (unchanged code from ChordChimp, checked there in its Phase 5). |
| 1. Workspace model | **Done (2026-11-03)** | `ChordTypes.h` (Chordism's 27 types, plus each one's numeral suffix and case); `core/model/Workspace.h` (`WorkspaceChord` = root + tones, `PatternSlot`, `Pattern` with set/insert/remove/move/clear/transpose/transposeToKey, `Workspace`, `slotNotes` with inversion and octave, `chordFromSlot` from the palette, `patternToText`/`patternFromText`); `core/model/ChordSets.h` (`parseNumeral`, `numeralFor`, `parseProgression`, `formatProgression`, `namePatternSlot`). The processor holds the workspace (`getWorkspace`/`setWorkspace`, behind a lock; not on the audio thread yet) and saves it in its state as `<WORKSPACE><PATTERN index text/>…`; a state without one restores empty; an edit tells the host. Checks: `Tests/test_workspace.cpp` (new in `run_tests.sh`): the type table; all 31,824 palette chords (12 keys × 13 scales × 17 groups × 12 slots) play the same notes from the workspace with the same root; inversions, octaves, clamping; names (C/E, Am7, C#m7b5, Eb); transposing (B2 up 1 is C3; 80 moves stay in C1-B3; C→G→C exact); editing; the saved text, its documented form, damaged text, 2,000 random patterns; 23 numerals by hand, 10 bad tokens refused with a reason, 3,888 typed chords and 2,000 random chords round-trip as numerals; palette rows as numerals (C major triads, E major sevenths, A minor triads, the borrowed chords); progressions. Snapshot app, new section 7: the workspace round-trips through the state, stays out of the parameters, restores empty from an older state, a damaged pattern doesn't spoil the others, an edit (not a repeat) notifies the host. All core and snapshot tests passed; `Tools/install.sh --validate` built, installed and passed `pluginval` 10. Not in Live: nothing there uses the workspace until Phase 2. |
| 2. Pattern playback | **Done (2026-11-03)** | `core/engine/PatternPlayer.h`: the pattern locked to the song (slot under the playhead = ppq mod the pattern's length); a trigger at every slot line, at the start of playback, at every jump (locate or loop wrap) and when the slot under the playhead changes; a Pattern change while playing waits for the next bar line or a jump, while stopped it's immediate; Play off, a stop or an empty pattern end the chord. `ChordPerformer` gained chord slots 12-19 (the playing pattern's notes, `PatternChords`, with inversion, octave and Chord Octave) and performance 128 (`patternKey`, VoiceTracker sources 384-385; the key octave never moves it), so every effect applies to the pattern. `ChordEngine`: `setWorkspace` (audio thread), the triggers merged into the event loop after the host's input and the test pattern's at one sample, an edit or a Chord Octave change retunes a sounding pattern chord, `releaseAll` resets the player. Parameters (appended; 31 now): `play`, `pattern` (A-D), and the test rig's `testWorkspace`; none in presets (presets hold no workspace until Phase 5). The processor publishes each new workspace to the audio thread through a `SnapshotExchange`, and `playingPattern`/`playingSlot` for the editor. Editor: Pattern (A-D) and Play in the top bar, until Phase 6's workspace panel. Checks: `Tests/test_player.cpp` (new in `run_tests.sh`, 26 checks, the timing ones swept over 26 host configurations: 44.1/48/96 kHz, 32-2048-sample blocks, 97-174 BPM): slot lines; I V vi IV and uneven lengths on their beats (within a sample); a one-beat chord restruck every beat; starting mid-slot; a host loop restriking at each wrap (blocks across the wrap and split at it); a locate; switching at the next bar, cancelling, and at a loop wrap; a switch while stopped; Play off and on mid-slot; Enable and Analyse stopping it; an edit retuning the sounding slot; emptying and refilling; Chord Octave; a key over the pattern; Strum and Repeat on the pattern; a fuzz of 60 × 120 random blocks (Play, Pattern, edits, loops, locates, tempo, stops, resets, keys, every effect) with nothing left sounding and the pattern never sounding while it shouldn't. Snapshot app: Workspace.txt loads only with Test workspace on; Play with Pattern B plays it through the real processor, with no allocation in processBlock; reference images updated for the top bar. All core and snapshot tests passed; `Tools/install.sh --validate` passed `pluginval` 10. **Bug found by the tests:** a slot line exactly on a block boundary was skipped (fixed: a line at a segment's start belongs to it). **In Live** (12.4.16 after a restart, set `~/Music/Ableton/Projects/ChordMonkey Phase 2 Project`, by the Ableton MCP and a small Python client on the bridge's port 9873): Live lists Play, Pattern and Test workspace (31 parameters + Program + Device On). ChordMonkey on *CM Src* (input: No Input), Drift on *CM Rec* with *MIDI From: CM Src → ChordMonkey*; Workspace.txt = A `I V vi IV`, B `ii7:2 V7:2`; Test workspace and Play on. **Take 1** (arrangement, 18.7 beats): 15 notes, C G Am F a bar each then C, every note exactly on its beat (0.000 ms). **Take 2**: Pattern set to B (value 1/3) at beat 5.155: A's C and G, then B from the bar line at beat 8 (Dm7, G7, Dm7), 18 notes, all on the beat. **Take 3**: Live's loop at beats 2-6, recorded into a session clip (linear time): C 0-4, G 4-6, C restruck at each wrap (6, 10), G at 8 and 12, 18 notes exactly. Meters at 0 after each stop. **Bridge notes:** loading a plug-in arms its track, and Live's exclusive arm disarms the recording track (re-arm it); plug-in choices are set as 0-1 values (B = 1/3); a take's leftover clip later in the arrangement is what `get_clip_notes` returns by default (use `clip_index`); arrangement recording over a loop makes take lanes, so loop takes go to a session clip; quitting Live by AppleScript discarded the open set's unsaved changes without asking. |
| 3. Bind and Keys-Lock | **Done (2026-11-03)** | **As built, one decision beyond the plan:** `Bind` has a fourth choice, **All keys**, and it's the default: ChordChimp's keyboard (every key plays its palette chord, Key octave applies), so existing sets, presets and tests behave as before; Workspace, Palette and Off are §3.3's. `KeyRouter::route` (zone Bind start … +12, counted as if Bind start were a C; below passes, above Keys-Lock; Off locks every key); `KeysLock.h` (Off, Scale notes mapped (nearest, a tie up), Scale notes only, Scale white keys (each octave's C its tonic), Chord notes (a white key snaps to the nearest note of the chord playing, a tie up; before any chord, the scale's I); black keys silent where the mode says). Engine: Palette keys play I-VII, I an octave up (a performance `octave`) and the borrowed chords; Workspace slot keys play the current pattern's slot while held, or, while the pattern plays, `PatternPlayer::requestJump`: the slot plays from the first beat at or after the key and the pattern goes on from there (a `shift` against the song until a stop or switch; the rest of the segment's triggers regenerated, so a beat inside the same block isn't missed); C# D# F# G# choose patterns A-D (switching at the bar while playing) and ask the plug-in to set Pattern (`takePatternRequest`, set by the timer as a gesture); A# ends the chords and `silenceUntilNextSlot`. Keys-Lock follows the last chord a key or the pattern played. Key octave applies only with All keys. `MidiEvent::origin`: the chord pads and the test pattern play the palette whatever Bind is. A note Keys-Lock maps onto a pitch the chord already sounds isn't struck twice (VoiceTracker's shared pitches). Parameters (appended; 34): `bindSource`, `bindStart` (0-115, shown as Live's note names, default C1 = 36), `keysLock`; none in presets. Editor: a Keyboard row under the globals (Bind, From, Keys-Lock, and Key octave moved from the globals row), dimming what doesn't apply; the black pads' last-played marker became a bar at their right edge (the pads are shorter). **Fixed on the way:** moving to another pattern ended the outgoing pattern chord only after briefly restriking it (a retune meant for a Bind key's held chord). Checks: `Tests/test_bind.cpp` (new in `run_tests.sh`): 2,688 routes (3 Binds × 7 zone starts × 128 notes); every Keys-Lock mode over 12 keys × 13 scales × 128 notes against independent definitions, D major by hand, the keyboard's ends, 20,000 random Chord-notes cases; Palette keys (I ii V, C', the five borrowed chords, Follow ignored, pass below, Keys-Lock above with Dm held); Workspace keys stopped (slots, an empty slot, D# then Dm7 from B, the request once); while playing, swept over 26 host configurations: a jump at the next beat and the pattern going on from it, a key on a beat, A# silencing until the next slot, D# switching at the bar with Chord notes following the pattern; Bind Off with Scale white keys and a pad still playing its chord; the test pattern unaffected; a fuzz of 60 × 120 random blocks with nothing left sounding. Snapshot app: D#1 sets the Pattern parameter and C1 plays B's vi; with Bind Off a pad plays its chord and a key its note; none of the three in presets; Bind start shows C1; reference images updated. All core and snapshot tests passed; `pluginval` 10 passed. **Test rig addition (for the check in Live):** the bridge can load a MIDI file only as a session clip and can't launch one, so while Test workspace is on the plug-in also plays keys from the rig's `Keys.txt` (`beat note length` per line, up to 256, as host MIDI while the host plays; snapshot check: keys at beats 0.5 and 1.5 to the sample). **In Live** (12.4.16 after a restart, set `~/Music/Ableton/Projects/ChordMonkey Phase 3 Project`): Workspace.txt A `I V vi IV`, B `ii7:2 V7:2`. **Take 1** (Bind Workspace, Keys-Lock Chord notes, Play on; keys F1 at 1.3, C4 at 7, D#1 at 11, E4 at 13, A#1 at 14.5): C 0-2, F from the jump at beat 2 to 6, C 6-10, G 10-12, B from the bar at 12 (Dm7 12-14, G7 14 until A# at 14.5, silence, Dm7 from the next slot at 16), and the Keys-Lock notes C4 over C and F4 (E4 snapped) over Dm7: 26 notes, exactly as expected, every one on the grid (0.000 ms); Live's Pattern parameter showed B after the D# key. **Take 2** (Bind Palette, Keys-Lock Scale notes mapped, Play off): C1 C, C2 C an octave up, C#1 Db, C#3 → D3, B0 passed through: 11 notes exactly. Meters at 0 after. |
| 4–10 | Not started | |

---

## 11. Repository layout

```
chordmonkey/
  ChordMonkey.jucer   Builds/   JuceLibraryCode/
  Source/core/{theory,model,engine,midiio}/   Source/plugin/   Source/ui/
  Resources/ChordSets/*.json          factory chord sets
  Tests/   (run_tests.sh, run_snapshot.sh, run_validation.sh, run_bench.sh, fixtures/)
  LiveBridge/tests/                   Live-in-the-loop checks (§7)
  Tools/install.sh   docs/   dist/
```

---

## 12. Research sources (Scaler 2 behaviour)

- Keys-Lock modes: [KEYS LOCK Options in Scaler 2.6](https://forum.scalermusic.com/t/keys-lock-options-in-scaler-2-6/11537)
- Bind ("All" vs "Only White" keys, start note, "True Scale"; green keys switch patterns):
  [Scaler 2 changelog](https://help.pluginboutique.com/hc/en-us/articles/6232885883412-Scaler-2-Update-Information-Changelog),
  [Changing the bound keys](https://forum.scalermusic.com/t/changing-the-bound-keys/13487)
- Detect, chord sets, expressions, edit modes, humanize: [Music Gateway](https://www.musicgateway.com/blog/music-production/scaler-chord-sets-plugin),
  [rekkerd.org](https://rekkerd.org/?p=157384)
- Workflow (scale → voicing → phrases → Keys-Lock; Detect then drag MIDI back):
  [Scaler 2.2 workflow tips](https://forum.scalermusic.com/t/scaler-2-2-workflow-tips-tricks-and-concepts/5708)
- Reviews: [MusicTech](https://musictech.com/reviews/plug-ins/plugin-boutique-scaler-2/),
  [Attack Magazine](https://www.attackmagazine.com/reviews/gear-software/plugin-boutique-scaler-2-your-new-composition-assistant/)

The official Scaler 2 manual wasn't reachable (Plugin Boutique's pages returned 403); details
above marked "Scaler's" come from the forum and reviews and are worth checking against the
manual before Phase 3.
