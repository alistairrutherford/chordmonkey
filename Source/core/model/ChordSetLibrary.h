// The factory chord sets (PLAN.md §3.5, Phase 5): common-practice progressions written for
// ChordMonkey, as Roman numerals (ChordSets.h), so each works in any key. Six categories: Pop,
// Rock, Jazz & Soul, EDM & House, Cinematic, Moods. Each set has a suggested scale (the one its
// numerals come from, for the palette and Keys-Lock) and a suggested style (Style.h, by name).
// No set copies a song, an artist's catalogue or another product's library; the names describe
// the harmony or the mood.
//
// Loading a set writes it into a pattern in the current Key (ChordMonkeyProcessor). Plain C++.
#pragma once

#include "../theory/Scales.h"
#include <array>

namespace chordmonkey
{
    struct ChordSetInfo
    {
        const char* category;
        const char* name;
        const char* progression;          // ChordSets.h's form: "I V vi IV", "ii7:2 V7:2 Imaj7:8"
        Scale scale;                      // suggested
        const char* style;                // suggested (a Style.h name)
    };

    constexpr int numChordSetCategories = 6;

    inline const char* chordSetCategory (int i) noexcept
    {
        static constexpr const char* names[numChordSetCategories] = { "Pop", "Rock", "Jazz & Soul", "EDM & House", "Cinematic", "Moods" };
        return names[i >= 0 && i < numChordSetCategories ? i : 0];
    }

    constexpr int numFactoryChordSets = 41;

    inline const std::array<ChordSetInfo, numFactoryChordSets>& factoryChordSets() noexcept
    {
        using S = Scale;
        static constexpr std::array<ChordSetInfo, numFactoryChordSets> sets { {
            // Pop
            { "Pop", "Four Chords",          "I V vi IV",                 S::Major,           "Eighths" },
            { "Pop", "Sensitive Pop",        "vi IV I V",                 S::Major,           "Quarters" },
            { "Pop", "Fifties",              "I vi IV V",                 S::Major,           "Charleston" },
            { "Pop", "Pop Lift",             "IV I V vi",                 S::Major,           "Eighths" },
            { "Pop", "Canon Steps",          "I:2 V:2 vi:2 iii:2 IV:2 I:2 IV:2 V:2", S::Major, "Quarters" },
            { "Pop", "Plagal Pop",           "I IV I V",                  S::Major,           "Off-beats" },
            { "Pop", "Doo-Wop Turnaround",   "I:2 vi:2 ii:2 V:2",         S::Major,           "Charleston" },
            // Rock
            { "Rock", "Mixolydian Rock",     "I bVII IV I",               S::Mixolydian,      "Eighths" },
            { "Rock", "Power Descent",       "i bVII bVI bVII",           S::NaturalMinor,    "Eighths" },
            { "Rock", "Twelve-Bar Shuffle",  "I7 IV7 I7 V7:2 IV7:2",      S::Mixolydian,      "Bass & chord" },
            { "Rock", "Classic Rock",        "I IV V IV",                 S::Major,           "Quarters" },
            { "Rock", "Andalusian Descent",  "i bVII bVI V",              S::HarmonicMinor,   "Tresillo" },
            { "Rock", "Minor Riff",          "i bIII bVII IV",            S::Dorian,          "Eighths" },
            // Jazz & Soul
            { "Jazz & Soul", "Two-Five-One",     "ii7 V7 Imaj7:8",              S::Major,         "Charleston" },
            { "Jazz & Soul", "Minor Two-Five",   "iiø7 V7 i7:8",                S::HarmonicMinor, "Charleston" },
            { "Jazz & Soul", "Turnaround",       "Imaj7:2 VI7:2 ii7:2 V7:2",    S::Major,         "Quarters" },
            { "Jazz & Soul", "Neo-Soul Loop",    "IVmaj7 iii7 vi7 ii9",         S::Major,         "Dotted quarters" },
            { "Jazz & Soul", "Gospel Walk",      "I iii IV iv",                 S::Major,         "Ballad" },
            { "Jazz & Soul", "Dorian Vamp",      "i9:8 IV9:8",                  S::Dorian,        "Dotted quarters" },
            { "Jazz & Soul", "Backdoor",         "IVmaj7 bVII7 Imaj7:8",        S::Major,         "Half notes" },
            // EDM & House
            { "EDM & House", "Deep House Minor", "i7:8 iv7:8",                  S::Dorian,        "House stabs" },
            { "EDM & House", "Anthem Minor",     "i bVI bIII bVII",             S::NaturalMinor,  "Eighths" },
            { "EDM & House", "Future Bass Lift", "IVmaj7 V iii7 vi7",           S::Major,         "Sixteenths" },
            { "EDM & House", "Euro Minor",       "i bIII bVII bVI",             S::NaturalMinor,  "Off-beats" },
            { "EDM & House", "Two-Chord Groove", "i:8 bVII:8",                  S::NaturalMinor,  "Four on the floor" },
            { "EDM & House", "Garage Sevenths",  "IVmaj7 iii7 ii7 Imaj7",       S::Major,         "House stabs" },
            { "EDM & House", "Dembow Minor",     "i bIII iv bVII",              S::NaturalMinor,  "Dembow" },
            // Cinematic
            { "Cinematic", "Epic Minor",         "i bVI bVII i",                S::NaturalMinor,  "Half notes" },
            { "Cinematic", "Heroic Rise",        "I bVI bVII I",                S::Major,         "Half notes" },
            { "Cinematic", "Phrygian Mystery",   "i bII i bII",                 S::Phrygian,      "Quarters" },
            { "Cinematic", "Lydian Wonder",      "I II I II",                   S::Lydian,        "Broken up" },
            { "Cinematic", "Chromatic Mediants", "I bVI I III",                 S::Major,         "Half notes" },
            { "Cinematic", "Suspense",           "i ii° V i",                   S::HarmonicMinor, "Quarters" },
            { "Cinematic", "Trailer Pulse",      "i:8 bVI:8",                   S::NaturalMinor,  "Sixteenths" },
            // Moods
            { "Moods", "Happy",                  "I V IV V",                    S::Major,         "Eighths" },
            { "Moods", "Sad",                    "i iv bVII bIII",              S::NaturalMinor,  "Ballad" },
            { "Moods", "Dreamy",                 "Imaj9:8 IVmaj9:8",            S::Lydian,        "Broken up" },
            { "Moods", "Dark",                   "i bII bVI V",                 S::HarmonicMinor, "Half notes" },
            { "Moods", "Hopeful",                "IV V iii vi",                 S::Major,         "Quarters" },
            { "Moods", "Nostalgic",              "I III7 vi IV",                S::Major,         "Alberti" },
            { "Moods", "Tense",                  "i bVI ii° V7",                S::HarmonicMinor, "Off-beats" },
        } };
        return sets;
    }

    // The factory set named `name`, or nullptr.
    inline const ChordSetInfo* findChordSet (const char* name) noexcept
    {
        for (const auto& s : factoryChordSets())
        {
            const char* a = s.name;
            const char* b = name;
            while (*a != 0 && *a == *b)
            {
                ++a;
                ++b;
            }
            if (*a == 0 && *b == 0)
                return &s;
        }
        return nullptr;
    }
}
