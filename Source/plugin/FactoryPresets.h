// ChordMonkey's factory presets: Chordism's (ChordChimp's PLAN.md §3.4), without its Smart Scale
// settings, in three categories: Genre, Artist-style (described by style only, never by an
// artist's name) and Vibe; then ChordMonkey's own (PLAN.md §3.5, Phase 5) in Songwriting, each
// with four factory chord sets in patterns A-D and a style, voicing and bass. Each is a list of
// host parameter values on top of the defaults (plain values: a choice's index, a bool's 0/1, a
// percentage 0-100), so the list is checked by the compiler against the engine's enums.
// Program 0 of the plug-in is "Init" (every default), so a new instance is its program 0. The
// programs keep their numbers: new presets only ever go at the end.
//
// A preset with chord sets replaces the workspace when it loads (written in the preset's Key);
// one without (Init, Chordism's) leaves the workspace alone.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <utility>
#include <vector>

namespace chordmonkey
{
    struct FactoryPreset
    {
        juce::String category, name;
        std::vector<std::pair<juce::String, float>> values;   // parameter ID → plain value
        std::array<const char*, 4> chordSets {};               // patterns A-D: factory chord set names (ChordSetLibrary.h), or nullptr
        bool hasWorkspace() const noexcept { return chordSets[0] != nullptr || chordSets[1] != nullptr || chordSets[2] != nullptr || chordSets[3] != nullptr; }
    };

    // "Init" first, then the factory presets in browser order.
    const std::vector<FactoryPreset>& factoryPresets();
}
