// Routes incoming notes (PLAN.md §3.3). Bind chooses what the keyboard does:
//
//   All keys   (the default; ChordChimp's keyboard, ChordChimp's PLAN.md §3.1) every octave:
//                white C D E F G A B  → chord slots 0–6 (with Key octave Follow the octave
//                                       moves the chord, ChordFx.h)
//                black C# D# F# G# A# → chord slots 7–11, the borrowed chords (ChordGroups.h)
//   Workspace  the Bind zone, Bind start … Bind start + 12 (default C1-C2, MIDI 36-48), counted
//   Palette    from Bind start as if it were a C:
//                white C D E F G A B C' → slots 1–8: Workspace, the current pattern's slots;
//                                         Palette, the chords I–VII and I an octave up
//                black C# D# F# G#      → Workspace: patterns A–D; Palette: bII bIII #iv° bVI
//                black A#               → Workspace: silence the chords until the next slot;
//                                         Palette: bVII
//              below the zone: notes pass through; above it: Keys-Lock (KeysLock.h)
//   Off        no chords from keys: Keys-Lock over the whole keyboard
//
// Notes from the editor's chord pads and the test pattern (MidiEvent::fromPalette) always play
// the palette chord of their key, as with All keys.
#pragma once

#include "../theory/ChordModel.h"

namespace chordmonkey
{
    // The Bind parameter's choices, in its order (saved by index: append only).
    enum class BindSource : uint8_t { AllKeys, Workspace, Palette, Off, Count };
    constexpr int numBindSources = (int) BindSource::Count;

    inline const char* bindSourceName (BindSource b) noexcept
    {
        static constexpr const char* names[numBindSources] = { "All keys", "Workspace", "Palette", "Off" };
        const int i = (int) b;
        return names[i >= 0 && i < numBindSources ? i : 0];
    }

    // A note's name as Live writes it (C3 = MIDI 60).
    inline void noteName (int note, char (&out)[8]) noexcept
    {
        static constexpr const char* letters[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        note = note < 0 ? 0 : (note > 127 ? 127 : note);
        const char* l = letters[note % 12];
        const int octave = note / 12 - 2;
        int i = 0;
        while (*l != 0)
            out[i++] = *l++;
        if (octave < 0)
            out[i++] = '-';
        out[i++] = (char) ('0' + (octave < 0 ? -octave : octave));
        out[i] = 0;
    }

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

        // Where a key goes with Bind (Workspace, Palette or Off).
        struct Route
        {
            enum Kind : uint8_t { Pass, Bound, Locked } kind = Pass;
            int8_t white = -1;            // Bound: 0..7 for C D E F G A B C', else -1
            int8_t black = -1;            // Bound: 0..4 for C# D# F# G# A#, else -1
        };

        static constexpr int zoneSize = 13;   // C … C'
        static constexpr int maxBindStart = 127 - (zoneSize - 1);

        static constexpr Route route (int note, BindSource bind, int bindStart) noexcept
        {
            Route r;
            if (bind == BindSource::Off)
            {
                r.kind = Route::Locked;
                return r;
            }
            const int offset = note - bindStart;
            if (offset < 0)
                return r;                         // Pass
            if (offset >= zoneSize)
            {
                r.kind = Route::Locked;
                return r;
            }
            r.kind = Route::Bound;
            if (offset == 12)
                r.white = 7;
            else
            {
                r.white = (int8_t) keys::whiteLetter (offset);
                r.black = (int8_t) keys::blackIndex (offset);
            }
            return r;
        }
    };
}
