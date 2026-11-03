// Factory Chord Groups (Chordism's PLAN.md §4.2): families of chord shapes that fill in every
// slot's steps at once. Choosing a group resets the Chord Editor's edits, as ChordPrism does.
//
// The order is the order of the host parameter's choices (saved by index): only append.
#pragma once

#include "ChordModel.h"
#include <array>

namespace chordmonkey
{
    enum class ChordGroup : uint8_t
    {
        Triads,
        Power,
        Sus2,
        Sus4,
        Sixths,
        Sevenths,
        Add9,
        Ninths,
        Elevenths,
        Thirteenths,
        FirstInversion,
        SecondInversion,
        OpenTriads,
        Drop2Sevenths,
        Shell,
        NeoSoul,
        Quartal,
        Count
    };

    constexpr int numChordGroups = (int) ChordGroup::Count;

    // One step of a group shape: which chord step (0 = root, 1 = 3rd, … 6 = 13th) and the
    // octaves it sounds in.
    struct GroupTone
    {
        int8_t step;
        uint8_t octaveMask;
    };

    struct ChordGroupInfo
    {
        const char* name;
        std::array<GroupTone, numChordSteps> tones;
        int numTones;
    };

    inline const ChordGroupInfo& chordGroupInfo (ChordGroup g) noexcept
    {
        constexpr uint8_t o0 = octaveBit (0), up = octaveBit (1), down = octaveBit (-1);
        constexpr int8_t R = 0, T3 = 1, T5 = 2, T7 = 3, T9 = 4, T11 = 5, T13 = 6;

        static constexpr ChordGroupInfo table[numChordGroups] = {
            { "Triads",          { { { R, o0 }, { T3, o0 }, { T5, o0 } } }, 3 },
            { "Power (1-5)",     { { { R, (uint8_t) (o0 | up) }, { T5, o0 } } }, 2 },
            { "Sus2",            { { { R, o0 }, { T9, down }, { T5, o0 } } }, 3 },
            { "Sus4",            { { { R, o0 }, { T11, down }, { T5, o0 } } }, 3 },
            { "6ths",            { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T13, down } } }, 4 },
            { "Sevenths",        { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 } } }, 4 },
            { "Add9",            { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T9, o0 } } }, 4 },
            { "Ninths",          { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 }, { T9, o0 } } }, 5 },
            { "Elevenths",       { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 }, { T9, o0 }, { T11, o0 } } }, 6 },
            // The 11th clashes with the 3rd in a 13th chord, so it's left out, as is usual.
            { "Thirteenths",     { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 }, { T9, o0 }, { T13, o0 } } }, 6 },
            { "1st Inversion",   { { { R, up }, { T3, o0 }, { T5, o0 } } }, 3 },
            { "2nd Inversion",   { { { R, up }, { T3, up }, { T5, o0 } } }, 3 },
            { "Open Triads",     { { { R, o0 }, { T5, o0 }, { T3, up } } }, 3 },
            { "Drop-2 Sevenths", { { { R, o0 }, { T3, o0 }, { T5, down }, { T7, o0 } } }, 4 },
            { "Shell (1-3-7)",   { { { R, o0 }, { T3, o0 }, { T7, o0 } } }, 3 },
            { "Neo-Soul",        { { { R, o0 }, { T3, o0 }, { T7, o0 }, { T9, o0 } } }, 4 },
            // Stacked diatonic 4ths: root, 4th, 7th, 10th.
            { "Quartal",         { { { R, o0 }, { T11, down }, { T7, o0 }, { T3, up } } }, 4 },
        };
        const int i = (int) g;
        return table[i >= 0 && i < numChordGroups ? i : 0];
    }

    // The slot a group gives a degree in a scale: the group's steps and octaves, with the
    // scale's default accidentals.
    inline ChordSlot makeGroupSlot (ChordGroup g, Scale s, int degree) noexcept
    {
        ChordSlot slot;
        slot.degree = degree;
        const auto& info = chordGroupInfo (g);
        for (int i = 0; i < info.numTones; ++i)
        {
            const auto& tone = info.tones[(size_t) i];
            auto& step = slot.steps[(size_t) tone.step];
            step.on = true;
            step.octaveMask = tone.octaveMask;
            step.accidental = (int8_t) defaultAccidental (s, degree, tone.step);
        }
        return slot;
    }

    // The black keys' chords (ChordChimp's PLAN.md §3.1, Phase 7): each plays the chord on its semitone above
    // the tonic, borrowed from the parallel mode that has that chord (modal mixture), in the
    // group's shape. In C, with Triads (Sevenths):
    //
    //   C#  bII   Db (Dbmaj7)      Phrygian, degree 2
    //   D#  bIII  Eb (Ebmaj7)      natural minor, degree 3
    //   F#  #iv°  F#dim (F#m7b5)   Lydian, degree 4
    //   G#  bVI   Ab (Abmaj7)      natural minor, degree 6
    //   A#  bVII  Bb (Bb7)         natural minor, degree 7
    //
    // The slot is the Key's scale's degree, with each step's accidental moving it to the mode's
    // note, so names and numerals follow the Key's scale: in a natural minor key D# is "III",
    // the same chord as E's.
    struct BorrowedChord
    {
        Scale mode;
        int8_t degree;                    // 0..6
    };

    inline const BorrowedChord& borrowedChord (int black) noexcept
    {
        static constexpr BorrowedChord table[numBlackSlots] = {
            { Scale::Phrygian, 1 }, { Scale::NaturalMinor, 2 }, { Scale::Lydian, 3 },
            { Scale::NaturalMinor, 5 }, { Scale::NaturalMinor, 6 },
        };
        return table[black >= 0 && black < numBlackSlots ? black : 0];
    }

    // Black key `black` (0 = C# … 4 = A#) in group `g`, as a slot of scale `s`.
    inline ChordSlot makeBorrowedSlot (ChordGroup g, Scale s, int black) noexcept
    {
        const auto& b = borrowedChord (black);
        ChordSlot slot = makeGroupSlot (g, b.mode, b.degree);
        for (int k = 0; k < numChordSteps; ++k)
        {
            auto& step = slot.steps[(size_t) k];
            if (step.on)
                step.accidental = (int8_t) (step.accidental + scaleOffset (b.mode, b.degree + 2 * k) - scaleOffset (s, b.degree + 2 * k));
        }
        return slot;
    }

    // The twelve slots: white key n (C = 0) plays degree n (the Progression reorders them, in
    // Progressions.h), then the five black keys' borrowed chords.
    using ChordSet = std::array<ChordSlot, numChordSlots>;

    inline ChordSet makeGroupChords (ChordGroup g, Scale s) noexcept
    {
        ChordSet set;
        for (int d = 0; d < numWhiteSlots; ++d)
            set[(size_t) d] = makeGroupSlot (g, s, d);
        for (int b = 0; b < numBlackSlots; ++b)
            set[(size_t) (numWhiteSlots + b)] = makeBorrowedSlot (g, s, b);
        return set;
    }
}
