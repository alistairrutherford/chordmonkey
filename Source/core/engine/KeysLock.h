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
//
// A mapped note outside 0..127 goes the other way, or is silent.
#pragma once

#include "../theory/Scales.h"
#include "KeyRouter.h"
#include <cstdint>

namespace chordmonkey
{
    // The Keys-Lock parameter's choices, in its order (saved by index: append only).
    enum class KeysLockMode : uint8_t { Off, ScaleMapped, ScaleOnly, ScaleWhiteKeys, ChordNotes, Count };
    constexpr int numKeysLockModes = (int) KeysLockMode::Count;

    inline const char* keysLockName (KeysLockMode m) noexcept
    {
        static constexpr const char* names[numKeysLockModes] = { "Off", "Scale notes mapped", "Scale notes only",
                                                                 "Scale white keys", "Chord notes" };
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

            case KeysLockMode::Off:
            case KeysLockMode::Count:
            default:
                return note;
        }
    }
}
