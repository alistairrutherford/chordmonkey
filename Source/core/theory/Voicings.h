// Voicing and Bass (PLAN.md §3.4, Phase 4): how a chord's notes are spread before voice leading,
// and a bass note under it. Plain C++, no JUCE, no allocation.
//
//   Close    as the chord is written (inversion and octave applied)
//   Open     drop-2: the second-highest note an octave down
//   Spread   the lowest note stays, every other note an octave up
//   Wide     drop-2 and drop-4: the second- and fourth-highest notes an octave down (a triad:
//            drop-2 only)
//
//   Bass     Off · Root (the chord's bass note, in the octave below the voicing's lowest note:
//            its root, or for an inversion or a slash chord its written bass, E under C/E) ·
//            Root + 5th (and the fifth above it where the bass is the root; a bass that isn't
//            the root plays alone, since its fifth needn't be in the chord)
//
// Notes that would leave 0..127 stay where they were; a repeated pitch is kept once.
#pragma once

#include "ChordModel.h"
#include <algorithm>
#include <cstdint>

namespace chordmonkey
{
    // The parameters' choices, in their order (saved by index: append only).
    enum class VoicingMode : uint8_t { Close, Open, Spread, Wide, Count };
    constexpr int numVoicingModes = (int) VoicingMode::Count;

    inline const char* voicingName (VoicingMode m) noexcept
    {
        static constexpr const char* names[numVoicingModes] = { "Close", "Open (drop-2)", "Spread", "Wide (drop-2+4)" };
        const int i = (int) m;
        return names[i >= 0 && i < numVoicingModes ? i : 0];
    }

    enum class BassMode : uint8_t { Off, Root, RootFifth, Count };
    constexpr int numBassModes = (int) BassMode::Count;

    inline const char* bassModeName (BassMode m) noexcept
    {
        static constexpr const char* names[numBassModes] = { "Off", "Root", "Root + 5th" };
        const int i = (int) m;
        return names[i >= 0 && i < numBassModes ? i : 0];
    }

    namespace voicing_detail
    {
        // Sorts and drops repeats.
        inline void tidy (ChordNotes& c) noexcept
        {
            std::sort (c.notes.begin(), c.notes.begin() + c.size);
            int n = 0;
            for (int i = 0; i < c.size; ++i)
                if (n == 0 || c.notes[(size_t) n - 1] != c.notes[(size_t) i])
                    c.notes[(size_t) n++] = c.notes[(size_t) i];
            c.size = n;
        }

        inline void move (ChordNotes& c, int index, int semis) noexcept
        {
            if (index < 0 || index >= c.size)
                return;
            const int n = c.notes[(size_t) index] + semis;
            if (n >= 0 && n <= 127)
                c.notes[(size_t) index] = (uint8_t) n;
        }
    }

    // `notes` voiced by `mode`, lowest first.
    inline ChordNotes applyVoicing (ChordNotes notes, VoicingMode mode) noexcept
    {
        using namespace voicing_detail;
        tidy (notes);
        const int n = notes.size;
        switch (mode)
        {
            case VoicingMode::Open:
                if (n >= 3)
                    move (notes, n - 2, -12);
                break;
            case VoicingMode::Spread:
                for (int i = 1; i < n; ++i)
                    move (notes, i, 12);
                break;
            case VoicingMode::Wide:
                if (n >= 3)
                    move (notes, n - 2, -12);
                if (n >= 5)
                    move (notes, n - 4, -12);
                break;
            case VoicingMode::Close:
            case VoicingMode::Count:
            default:
                break;
        }
        tidy (notes);
        return notes;
    }

    // The bass notes for a chord whose lowest note is `lowest`, root pitch class `rootPc` and
    // written bass pitch class `bassPc` (-1: the root): `out` gets 0, 1 or 2 notes (the bass,
    // then the root's fifth), each below `lowest`.
    inline int bassNotes (BassMode mode, int lowest, int rootPc, int (&out)[2], int bassPc = -1) noexcept
    {
        if (mode == BassMode::Off || lowest < 0 || rootPc < 0)
            return 0;
        const int pc = bassPc < 0 ? rootPc : bassPc;
        const int top = lowest - 12;
        const int bass = top - (((top - pc) % 12) + 12) % 12;   // the highest at or below top
        int n = 0;
        if (bass >= 0)
        {
            out[n++] = bass;
            if (mode == BassMode::RootFifth && pc % 12 == rootPc % 12 && bass + 7 < lowest)
                out[n++] = bass + 7;
        }
        return n;
    }
}
