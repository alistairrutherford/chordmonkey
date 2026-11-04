// Keys-Lock (PLAN.md §3.3, Phase 3): the keys above the Bind zone (or every key, with Bind Off)
// play notes locked to the scale or to the chord playing. Plain C++, no JUCE, no allocation.
//
//   Off                 the note as played
//   Scale notes mapped  a note outside the Key's scale moves to the nearest scale note; a tie
//                       goes up (D major: F → F#, C → C#)
//   Scale notes only    a note outside the scale is silent
//   Scale white keys    the white keys play the scale in order from the tonic, each octave's C
//                       its tonic (D major: C=D, D=E, E=F#, F=G, G=A, A=B, B=C#); black keys
//                       are silent
//   Chord notes         a white key plays the nearest note of the chord playing (the last one
//                       a key or the pattern played; before any, the scale's I); a tie goes up;
//                       black keys are silent
//   Chord extensions    as Chord notes, with the chord's 9th, 11th and 13th added (Phase 9):
//                       each the scale's note there (C major, on G: A, C, E), else the scale's
//                       altered one (the 9th: b9, then #9; the 11th: #11; the 13th: b13), else
//                       the natural one; the root is the pitch class with the most of a 3rd,
//                       5th and 7th above it (Bb in C major: C, E, G)
//
// A mapped note outside 0..127 goes the other way, or is silent.
#pragma once

#include "../theory/Scales.h"
#include "KeyRouter.h"
#include <cstdint>

namespace chordmonkey
{
    // The Keys-Lock parameter's choices, in its order (saved by index: append only).
    enum class KeysLockMode : uint8_t { Off, ScaleMapped, ScaleOnly, ScaleWhiteKeys, ChordNotes, ChordExtensions, Count };
    constexpr int numKeysLockModes = (int) KeysLockMode::Count;

    inline const char* keysLockName (KeysLockMode m) noexcept
    {
        static constexpr const char* names[numKeysLockModes] = { "Off", "Scale notes mapped", "Scale notes only",
                                                                 "Scale white keys", "Chord notes", "Chord extensions" };
        const int i = (int) m;
        return names[i >= 0 && i < numKeysLockModes ? i : 0];
    }

    namespace keyslock
    {
        constexpr int mod12 (int n) noexcept { return ((n % 12) + 12) % 12; }

        // The pitch classes of `key`'s `scale` (bit pc).
        inline unsigned scaleMask (int key, Scale scale) noexcept
        {
            unsigned m = 0;
            for (int d = 0; d < 7; ++d)
                m |= 1u << mod12 (key + scaleInfo (scale).semitones[(size_t) d]);
            return m;
        }

        // The nearest note to `note` whose pitch class is in `mask` (a tie goes up), within 0..127;
        // -1 if `mask` is empty.
        inline int nearestIn (int note, unsigned mask) noexcept
        {
            if ((mask & 0xfffu) == 0)
                return -1;
            for (int d = 0; d < 12; ++d)
            {
                const int up = note + d, down = note - d;
                if (up <= 127 && ((mask >> mod12 (up)) & 1u) != 0)
                    return up;
                if (down >= 0 && ((mask >> mod12 (down)) & 1u) != 0)
                    return down;
            }
            return -1;
        }
    }

    namespace keyslock
    {
        // The chord's root: the pitch class with a 3rd (2 points), a perfect 5th (2; a
        // diminished or augmented one 1) and a 7th (1) above it scoring most; ties go to the
        // lowest pitch class. -1 for no notes.
        inline int chordRoot (unsigned mask) noexcept
        {
            mask &= 0xfffu;
            int best = -1, bestScore = -1;
            for (int r = 0; r < 12; ++r)
            {
                if (((mask >> r) & 1u) == 0)
                    continue;
                const unsigned rel = ((mask >> r) | (mask << (12 - r))) & 0xfffu;
                const auto has = [rel] (int i) { return ((rel >> i) & 1u) != 0; };
                const int score = 2 * (int) (has (3) || has (4)) + (has (7) ? 2 : (int) (has (6) || has (8))) + (int) (has (10) || has (11));
                if (score > bestScore)
                {
                    best = r;
                    bestScore = score;
                }
            }
            return best;
        }

        // The chord with its 9th, 11th and 13th (bit pc).
        inline unsigned extendedMask (unsigned chordMask, int key, Scale scale) noexcept
        {
            const int root = chordRoot (chordMask);
            if (root < 0)
                return chordMask;
            const unsigned sm = scaleMask (key, scale);
            unsigned m = chordMask & 0xfffu;
            // The 9th, 11th and 13th, each with its alterations in the order tried.
            constexpr int tries[3][3] = { { 2, 1, 3 }, { 5, 6, 5 }, { 9, 8, 9 } };
            for (const auto& t : tries)
            {
                int pick = t[0];
                for (int i : t)
                    if (((sm >> mod12 (root + i)) & 1u) != 0)
                    {
                        pick = i;
                        break;
                    }
                m |= 1u << mod12 (root + pick);
            }
            return m;
        }
    }

    // The note key `note` plays with Keys-Lock `mode`, or -1 for silence. `chordMask`: the pitch
    // classes of the chord playing (bit pc), for Chord notes.
    inline int keysLockNote (int note, KeysLockMode mode, int key, Scale scale, unsigned chordMask) noexcept
    {
        using namespace keyslock;
        if (note < 0 || note > 127)
            return -1;
        const bool black = keys::whiteLetter (mod12 (note)) < 0;

        switch (mode)
        {
            case KeysLockMode::ScaleMapped:
                return nearestIn (note, scaleMask (key, scale));

            case KeysLockMode::ScaleOnly:
                return ((scaleMask (key, scale) >> mod12 (note)) & 1u) != 0 ? note : -1;

            case KeysLockMode::ScaleWhiteKeys:
            {
                if (black)
                    return -1;
                const int n = note - mod12 (note) + mod12 (key) + scaleOffset (scale, keys::whiteLetter (mod12 (note)));
                return n >= 0 && n <= 127 ? n : (n > 127 ? n - 12 : -1);
            }

            case KeysLockMode::ChordNotes:
                return black ? -1 : nearestIn (note, chordMask);

            case KeysLockMode::ChordExtensions:
                return black ? -1 : nearestIn (note, extendedMask (chordMask, key, scale));

            case KeysLockMode::Off:
            case KeysLockMode::Count:
            default:
                return note;
        }
    }
}
