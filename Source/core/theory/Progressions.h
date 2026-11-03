// Chord Progressions (ChordChimp's PLAN.md §3.2): which scale degree each white key plays. A progression is
// seven degrees, one per white key (C = slot 0 … B = slot 6). Each is a permutation, so every
// chord of the scale stays on some key.
//
// The enum keeps Chordism's order, Custom included, so Chordism's values still name the same
// progressions. ChordChimp has no Custom (ChordChimp's PLAN.md §3.5): the host parameter's choices are the
// enum without it (progressionChoice / progressionFromChoice), and Custom plays as Diatonic.
#pragma once

#include "ChordGroups.h"
#include <array>

namespace chordmonkey
{
    enum class Progression : uint8_t
    {
        Diatonic,         // 1 2 3 4 5 6 7
        Custom,           // Chordism's custom order: not offered, plays as Diatonic
        Pop,              // 1 5 6 4 …
        Fifties,          // 1 6 4 5 …
        TwoFiveOne,       // 2 5 1 …
        CircleOfFifths,   // 1 4 7 3 6 2 5
        Descending,       // 1 7 6 5 4 3 2
        Canon,            // 1 5 6 3 4 …
        Thirds,           // 1 3 5 7 2 4 6
        Count
    };

    constexpr int numProgressions = (int) Progression::Count;

    using DegreeOrder = std::array<int8_t, numWhiteSlots>;   // white slot → degree 0..6

    struct ProgressionInfo
    {
        const char* name;
        DegreeOrder degrees;
    };

    // Degrees are written 1-based in the names (the case of a Roman numeral depends on the scale).
    inline const ProgressionInfo& progressionInfo (Progression p) noexcept
    {
        static constexpr ProgressionInfo table[numProgressions] = {
            { "Diatonic (1-7)",           { 0, 1, 2, 3, 4, 5, 6 } },
            { "Diatonic (1-7)",           { 0, 1, 2, 3, 4, 5, 6 } },   // Custom
            { "Pop (1-5-6-4)",            { 0, 4, 5, 3, 1, 2, 6 } },
            { "Fifties (1-6-4-5)",        { 0, 5, 3, 4, 1, 2, 6 } },
            { "Two-Five-One (2-5-1)",     { 1, 4, 0, 5, 3, 2, 6 } },
            { "Circle of Fifths",         { 0, 3, 6, 2, 5, 1, 4 } },
            { "Descending (1-7-6-5)",     { 0, 6, 5, 4, 3, 2, 1 } },
            { "Canon (1-5-6-3-4)",        { 0, 4, 5, 2, 3, 1, 6 } },
            { "Thirds (1-3-5-7)",         { 0, 2, 4, 6, 1, 3, 5 } },
        };
        const int i = (int) p;
        return table[i >= 0 && i < numProgressions ? i : 0];
    }

    constexpr DegreeOrder identityOrder() noexcept { return { 0, 1, 2, 3, 4, 5, 6 }; }

    // The degree each slot plays.
    inline DegreeOrder progressionDegrees (Progression p) noexcept { return progressionInfo (p).degrees; }

    // The Progression parameter's choices: every progression but Custom, in the enum's order.
    constexpr int numProgressionChoices = numProgressions - 1;

    constexpr int progressionChoice (Progression p) noexcept
    {
        const int i = (int) p;
        return i <= (int) Progression::Diatonic || i == (int) Progression::Custom || i >= numProgressions ? 0 : i - 1;
    }

    constexpr Progression progressionFromChoice (int choice) noexcept
    {
        return choice <= 0 || choice >= numProgressionChoices ? Progression::Diatonic : (Progression) (choice + 1);
    }

    // The chords: white slot n plays degree order[n] in group `g`'s shape; the black keys'
    // borrowed chords don't depend on the progression.
    inline ChordSet makeChords (ChordGroup g, Scale s, const DegreeOrder& order) noexcept
    {
        ChordSet set = makeGroupChords (g, s);
        for (size_t n = 0; n < order.size(); ++n)
            set[n] = makeGroupSlot (g, s, order[n]);
        return set;
    }
}
