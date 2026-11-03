// Routes incoming notes (ChordChimp's PLAN.md §3.1). There is no split: the whole keyboard plays chords.
//
//   white C D E F G A B  → chord slots 0–6, in every octave (the octave picks no other
//                          chord; with Key octave Follow it moves the chord, ChordFx.h)
//   black C# D# F# G# A# → chord slots 7–11, the borrowed chords (ChordGroups.h; Phase 7)
#pragma once

#include "../theory/ChordModel.h"

namespace chordmonkey
{
    namespace keys
    {
        // Letter of a white key (C = 0 … B = 6), or -1.
        constexpr int whiteLetter (int pitchClass) noexcept
        {
            constexpr int t[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
            return t[((pitchClass % 12) + 12) % 12];
        }

        // Index of a black key (C# = 0 … A# = 4), or -1.
        constexpr int blackIndex (int pitchClass) noexcept
        {
            constexpr int t[12] = { -1, 0, -1, 1, -1, -1, 2, -1, 3, -1, 4, -1 };
            return t[((pitchClass % 12) + 12) % 12];
        }

        constexpr int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
        constexpr int blackOffsets[5] = { 1, 3, 6, 8, 10 };
    }

    class KeyRouter
    {
    public:
        // The octave the test pattern and the editor's chord keys play in: C at MIDI 48 (Live's
        // C2), the chord zone's octave in Chordism at its default split.
        static constexpr int homeC = 48;

        // The chord slot a note plays: 0..6 for a white key, 7..11 for a black one.
        static constexpr int slotOf (int note) noexcept
        {
            const int w = keys::whiteLetter (note % 12);
            return w >= 0 ? w : numWhiteSlots + keys::blackIndex (note % 12);
        }

        // The key in the home octave that plays `slot`, or -1.
        static constexpr int keyForSlot (int slot) noexcept
        {
            if (slot >= 0 && slot < numWhiteSlots)
                return homeC + keys::whiteOffsets[slot];
            if (slot >= numWhiteSlots && slot < numChordSlots)
                return homeC + keys::blackOffsets[slot - numWhiteSlots];
            return -1;
        }
    };
}
