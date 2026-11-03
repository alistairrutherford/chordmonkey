// Chord types (PLAN.md §3.2): the absolute chord shapes the workspace and the chord sets use.
// Chordism's table (its Active Chord menu, Chordism's PLAN.md §4.2), with what a Roman numeral
// needs to write and read each type. Plain C++, no JUCE.
//
// Each type lists, for the 3rd … 13th, the interval above the root it needs (in semitones,
// mod 12) or none. The 9th, 11th and 13th sound an octave up, except the 9th of sus2, the 11th
// of sus4 and the 13th of a 6th chord, which sound inside the triad, as the Chord Groups place
// them.
//
// The order is saved in user chord sets by name, never by index, so it may change; append
// anyway, to keep the menus stable.
#pragma once

#include <array>
#include <cstdint>
#include <utility>

namespace chordmonkey
{
    struct ChordType
    {
        const char* name;                 // menu text
        std::array<int8_t, 6> interval;   // 3rd, 5th, 7th, 9th, 11th, 13th: semitones above the root (mod 12), -1 = off
        std::array<int8_t, 6> octave;     // the octave each sounds in, relative to its usual place
        const char* quality;              // the chord name's suffix on a root position
        bool lowerCase;                   // its Roman numeral is lower case (a minor or diminished 3rd)
        bool anyCase;                     // a numeral of either case reads as this type (no 3rd, or the symbol says it all)
        const char* numeralSuffix;        // what follows the numeral: "V7", "iiø7", "IVmaj7"
    };

    constexpr int numChordTypes = 27;

    inline const ChordType& chordType (int i) noexcept
    {
        constexpr int8_t _ = -1;
        static constexpr ChordType table[numChordTypes] = {
            { "Major",      { 4, 7, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "",        false, false, "" },
            { "Minor",      { 3, 7, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "m",       true,  false, "" },
            { "Diminished", { 3, 6, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "dim",     true,  true,  "°" },
            { "Augmented",  { 4, 8, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "aug",     false, true,  "+" },
            { "Sus2",       { _, 7, _, 2, _, _ },    { 0, 0, 0, -1, 0, 0 },  "sus2",    false, true,  "sus2" },
            { "Sus4",       { _, 7, _, _, 5, _ },    { 0, 0, 0, 0, -1, 0 },  "sus4",    false, true,  "sus4" },
            { "Power (5)",  { _, 7, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "5",       false, true,  "5" },
            { "6",          { 4, 7, _, _, _, 9 },    { 0, 0, 0, 0, 0, -1 },  "6",       false, false, "6" },
            { "m6",         { 3, 7, _, _, _, 9 },    { 0, 0, 0, 0, 0, -1 },  "m6",      true,  false, "6" },
            { "7",          { 4, 7, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7",       false, false, "7" },
            { "maj7",       { 4, 7, 11, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "maj7",    false, false, "maj7" },
            { "m7",         { 3, 7, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m7",      true,  false, "7" },
            { "m(maj7)",    { 3, 7, 11, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m(maj7)", true,  false, "maj7" },
            { "m7b5",       { 3, 6, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m7b5",    true,  true,  "ø7" },
            { "dim7",       { 3, 6, 9, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "dim7",    true,  true,  "°7" },
            { "7sus4",      { _, 7, 10, _, 5, _ },   { 0, 0, 0, 0, -1, 0 },  "7sus4",   false, true,  "7sus4" },
            { "7b5",        { 4, 6, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7b5",     false, false, "7b5" },
            { "7#5",        { 4, 8, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7#5",     false, false, "7#5" },
            { "add9",       { 4, 7, _, 2, _, _ },    { 0, 0, 0, 0, 0, 0 },   "add9",    false, false, "add9" },
            { "m(add9)",    { 3, 7, _, 2, _, _ },    { 0, 0, 0, 0, 0, 0 },   "madd9",   true,  false, "add9" },
            { "9",          { 4, 7, 10, 2, _, _ },   { 0, 0, 0, 0, 0, 0 },   "9",       false, false, "9" },
            { "maj9",       { 4, 7, 11, 2, _, _ },   { 0, 0, 0, 0, 0, 0 },   "maj9",    false, false, "maj9" },
            { "m9",         { 3, 7, 10, 2, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m9",      true,  false, "9" },
            { "7b9",        { 4, 7, 10, 1, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7b9",     false, false, "7b9" },
            { "7#9",        { 4, 7, 10, 3, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7#9",     false, false, "7#9" },
            { "m11",        { 3, 7, 10, 2, 5, _ },   { 0, 0, 0, 0, 0, 0 },   "m11",     true,  false, "11" },
            { "13",         { 4, 7, 10, 2, _, 9 },   { 0, 0, 0, 0, 0, 0 },   "13",      false, false, "13" },
        };
        return table[i >= 0 && i < numChordTypes ? i : 0];
    }

    // A chord's tones in semitones above its root, lowest first: the root, then the 3rd … 13th
    // in their places (the 9th, 11th and 13th an octave up unless the type says otherwise).
    struct ChordTones
    {
        std::array<int8_t, 8> tones {};
        int size = 0;
    };

    inline ChordTones chordTypeTones (int type) noexcept
    {
        const ChordType& t = chordType (type);
        ChordTones out;
        out.tones[(size_t) out.size++] = 0;
        for (int k = 0; k < 6; ++k)
        {
            const int iv = t.interval[(size_t) k];
            if (iv < 0)
                continue;
            const int place = k >= 3 ? 12 : 0;      // 9th, 11th, 13th: the octave above
            out.tones[(size_t) out.size++] = (int8_t) (iv + place + 12 * t.octave[(size_t) k]);
        }
        // Insertion sort: at most seven tones.
        for (int i = 1; i < out.size; ++i)
            for (int j = i; j > 0 && out.tones[(size_t) j - 1] > out.tones[(size_t) j]; --j)
                std::swap (out.tones[(size_t) j - 1], out.tones[(size_t) j]);
        return out;
    }

    // The pitch classes of a type (bit n = n semitones above the root).
    inline unsigned chordTypeMask (int type) noexcept
    {
        const ChordTones t = chordTypeTones (type);
        unsigned m = 0;
        for (int i = 0; i < t.size; ++i)
            m |= 1u << (((t.tones[(size_t) i] % 12) + 12) % 12);
        return m;
    }

    // The type whose pitch classes are exactly `mask` (relative to the root), or -1.
    inline int chordTypeForMask (unsigned mask) noexcept
    {
        for (int i = 0; i < numChordTypes; ++i)
            if (chordTypeMask (i) == (mask & 0xfffu))
                return i;
        return -1;
    }
}
