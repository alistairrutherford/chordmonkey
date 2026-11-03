// The workspace (PLAN.md §3.2): four patterns (A-D) of up to eight chord slots. Plain C++, no
// JUCE. The types are trivially copyable, so the audio thread can take a copy through a
// SnapshotExchange (Phase 2); the editing and text functions are for the message thread.
//
// A slot holds an absolute chord: a root pitch class and the chord's tones in semitones above
// that root, placed above chordBaseNote (C2):
//
//   note(i) = chordBaseNote + root + tones[i] + 12·octave        (before the inversion)
//
// Tones may start an octave up (a palette chord whose root sits above B2 keeps its pitch), so
// a chord copied from the palette plays exactly the notes the palette plays. Each step of the
// inversion moves the lowest note up an octave.
//
// Patterns are compact: slots 0 … size()-1 are used and the rest are empty. Each pattern
// remembers the key it was written in, for Transpose to key.
//
// Saved as text (patternToText), one line per pattern:
//
//   key|root:tone.tone.tone:halfBeats:inversion:octave root:… …
//   0|0:0.4.7:8:0:0 7:0.4.7:8:0:0 9:0.3.7:8:0:0 5:0.4.7:8:0:0       (C G Am F, a bar each)
#pragma once

#include "../theory/ChordModel.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace chordmonkey
{
    constexpr int numPatterns = 4;               // A-D
    constexpr int numPatternSlots = 8;
    constexpr int maxWorkspaceTones = 8;
    constexpr int minSlotHalfBeats = 1, maxSlotHalfBeats = 16, defaultSlotHalfBeats = 8;   // ½ … 8 beats, default a bar of 4/4
    constexpr int maxSlotInversion = 3;
    constexpr int minSlotOctave = -2, maxSlotOctave = 2;

    namespace workspace_detail
    {
        constexpr int mod12 (int n) noexcept { return ((n % 12) + 12) % 12; }
    }

    struct WorkspaceChord
    {
        int8_t root = 0;                                     // pitch class, C = 0
        std::array<int8_t, maxWorkspaceTones> tones {};      // semitones above C2 + root, lowest first
        int8_t size = 0;                                     // 0 = no chord

        bool empty() const noexcept { return size == 0; }

        // Pitch classes relative to the root (bit n = n semitones above it).
        unsigned relativeMask() const noexcept
        {
            unsigned m = 0;
            for (int i = 0; i < size; ++i)
                m |= 1u << workspace_detail::mod12 (tones[(size_t) i]);
            return m;
        }

        // Pitch classes (bit pc).
        unsigned pitchClassMask() const noexcept
        {
            unsigned m = 0;
            for (int i = 0; i < size; ++i)
                m |= 1u << workspace_detail::mod12 (root + tones[(size_t) i]);
            return m;
        }

        bool operator== (const WorkspaceChord& o) const noexcept
        {
            if (root != o.root || size != o.size)
                return false;
            for (int i = 0; i < size; ++i)
                if (tones[(size_t) i] != o.tones[(size_t) i])
                    return false;
            return true;
        }
        bool operator!= (const WorkspaceChord& o) const noexcept { return ! (*this == o); }
    };

    // A chord from a root `rootFromC` semitones above C2 (any integer) and tones relative to
    // that root. The root becomes its pitch class and the octaves go into the tones, so the
    // pitches stay the same. Tones are sorted; duplicates and tones past the eighth are dropped.
    inline WorkspaceChord makeWorkspaceChord (int rootFromC, const int* tones, int count) noexcept
    {
        WorkspaceChord c;
        const int root = workspace_detail::mod12 (rootFromC);
        const int carry = rootFromC - root;
        c.root = (int8_t) root;

        std::array<int, 64> sorted {};
        int n = 0;
        for (int i = 0; i < count && n < (int) sorted.size(); ++i)
            sorted[(size_t) n++] = std::clamp (tones[i] + carry, -60, 72);
        std::sort (sorted.begin(), sorted.begin() + n);
        for (int i = 0; i < n && c.size < maxWorkspaceTones; ++i)
            if (c.size == 0 || c.tones[(size_t) c.size - 1] != sorted[(size_t) i])
                c.tones[(size_t) c.size++] = (int8_t) sorted[(size_t) i];
        return c;
    }

    // The chord a palette slot plays (outputOctave ignored), as an absolute chord with the same
    // notes. The root is the slot's root step at its own octave, even where the Chord Group moves
    // it (1st Inversion has its root an octave up).
    inline WorkspaceChord chordFromSlot (const ChordSlot& slot, const ChordContext& ctx) noexcept
    {
        ChordContext c = ctx;
        c.outputOctave = 0;
        const ChordNotes notes = chordNotes (slot, c);
        const int rootFromC = workspace_detail::mod12 (c.key) + scaleOffset (c.scale, slot.degree) + slot.steps[0].accidental;
        const int rootNote = chordBaseNote + rootFromC;

        std::array<int, maxChordNotes> rel {};
        for (int i = 0; i < notes.size; ++i)
            rel[(size_t) i] = notes.notes[(size_t) i] - rootNote;
        return makeWorkspaceChord (rootFromC, rel.data(), notes.size);
    }

    // `c` moved by `semis`, the same shape. If its lowest note would leave C1 … B3 it moves by
    // octaves back inside, so repeated transposing doesn't drift out of range.
    inline WorkspaceChord transposed (const WorkspaceChord& c, int semis) noexcept
    {
        if (c.empty())
            return c;
        int rootFromC = c.root + semis;
        const int lowest = rootFromC + c.tones[0];
        if (lowest < -12)
            rootFromC += 12 * ((-12 - lowest + 11) / 12);
        else if (lowest > 23)
            rootFromC -= 12 * ((lowest - 23 + 11) / 12);

        std::array<int, maxWorkspaceTones> t {};
        for (int i = 0; i < c.size; ++i)
            t[(size_t) i] = c.tones[(size_t) i];
        return makeWorkspaceChord (rootFromC, t.data(), c.size);
    }

    struct PatternSlot
    {
        WorkspaceChord chord;
        uint8_t halfBeats = (uint8_t) defaultSlotHalfBeats;
        int8_t inversion = 0;             // 0 = root position … 3
        int8_t octave = 0;                // -2 … +2

        bool used() const noexcept { return ! chord.empty(); }

        bool operator== (const PatternSlot& o) const noexcept
        {
            return chord == o.chord && halfBeats == o.halfBeats && inversion == o.inversion && octave == o.octave;
        }
        bool operator!= (const PatternSlot& o) const noexcept { return ! (*this == o); }
    };

    inline PatternSlot makePatternSlot (const WorkspaceChord& chord, int halfBeats = defaultSlotHalfBeats,
                                        int inversion = 0, int octave = 0) noexcept
    {
        PatternSlot s;
        s.chord = chord;
        s.halfBeats = (uint8_t) std::clamp (halfBeats, minSlotHalfBeats, maxSlotHalfBeats);
        s.inversion = (int8_t) std::clamp (inversion, 0, maxSlotInversion);
        s.octave = (int8_t) std::clamp (octave, minSlotOctave, maxSlotOctave);
        return s;
    }

    // The notes a slot plays, lowest first, with its inversion and octave and `outputOctave`
    // (the Chord Octave parameter). An inversion past the chord's last tone stops there (a
    // triad has root position and two inversions). Notes outside 0 … 127 are dropped.
    inline ChordNotes slotNotes (const PatternSlot& s, int outputOctave = 0) noexcept
    {
        ChordNotes out;
        const auto& c = s.chord;
        if (c.empty())
            return out;

        std::array<int, maxWorkspaceTones> n {};
        for (int i = 0; i < c.size; ++i)
            n[(size_t) i] = chordBaseNote + c.root + c.tones[(size_t) i] + 12 * (s.octave + outputOctave);

        const int inv = std::min ((int) s.inversion, c.size - 1);
        for (int i = 0; i < inv; ++i)
            *std::min_element (n.begin(), n.begin() + c.size) += 12;   // the lowest note up an octave
        std::sort (n.begin(), n.begin() + c.size);
        for (int i = 0; i < c.size; ++i)
            if (n[(size_t) i] >= 0 && n[(size_t) i] <= 127 && (out.size == 0 || out.notes[(size_t) out.size - 1] != n[(size_t) i]))
                out.notes[(size_t) out.size++] = (uint8_t) n[(size_t) i];
        return out;
    }

    struct Pattern
    {
        std::array<PatternSlot, numPatternSlots> slots {};
        int8_t key = 0;                   // the key it was written in, C = 0

        int size() const noexcept
        {
            int n = 0;
            while (n < numPatternSlots && slots[(size_t) n].used())
                ++n;
            return n;
        }

        bool empty() const noexcept { return size() == 0; }
        bool full() const noexcept { return size() == numPatternSlots; }

        int totalHalfBeats() const noexcept
        {
            int t = 0;
            for (int i = 0, n = size(); i < n; ++i)
                t += slots[(size_t) i].halfBeats;
            return t;
        }

        // Replaces slot `index`, or appends when `index` is size(). False if out of range, or
        // if `slot` has no chord (use remove).
        bool set (int index, const PatternSlot& slot) noexcept
        {
            if (! slot.used() || index < 0 || index > size() || index >= numPatternSlots)
                return false;
            slots[(size_t) index] = slot;
            return true;
        }

        // Inserts before `index` (0 … size()). False if the pattern is full.
        bool insert (int index, const PatternSlot& slot) noexcept
        {
            const int n = size();
            if (! slot.used() || index < 0 || index > n || n == numPatternSlots)
                return false;
            for (int i = n; i > index; --i)
                slots[(size_t) i] = slots[(size_t) i - 1];
            slots[(size_t) index] = slot;
            return true;
        }

        bool remove (int index) noexcept
        {
            const int n = size();
            if (index < 0 || index >= n)
                return false;
            for (int i = index; i < n - 1; ++i)
                slots[(size_t) i] = slots[(size_t) i + 1];
            slots[(size_t) n - 1] = PatternSlot {};
            return true;
        }

        // Moves slot `from` so that it ends up at `to` (both 0 … size()-1).
        bool move (int from, int to) noexcept
        {
            const int n = size();
            if (from < 0 || from >= n || to < 0 || to >= n)
                return false;
            const PatternSlot s = slots[(size_t) from];
            remove (from);
            return insert (to, s);
        }

        void clear() noexcept { *this = Pattern {}; }

        void transpose (int semis) noexcept
        {
            for (auto& s : slots)
                s.chord = transposed (s.chord, semis);
            key = (int8_t) workspace_detail::mod12 (key + semis);
        }

        // Moves the pattern to `newKey` by the smaller way round (down 5 … up 6 semitones).
        void transposeToKey (int newKey) noexcept
        {
            transpose (workspace_detail::mod12 (newKey - key + 5) - 5);
        }

        bool operator== (const Pattern& o) const noexcept { return key == o.key && slots == o.slots; }
        bool operator!= (const Pattern& o) const noexcept { return ! (*this == o); }
    };

    struct Workspace
    {
        std::array<Pattern, numPatterns> patterns {};

        bool operator== (const Workspace& o) const noexcept { return patterns == o.patterns; }
        bool operator!= (const Workspace& o) const noexcept { return ! (*this == o); }
    };

    inline const char* patternName (int index) noexcept
    {
        static constexpr const char* names[numPatterns] = { "A", "B", "C", "D" };
        return names[std::clamp (index, 0, numPatterns - 1)];
    }

    //==========================================================================
    // Text (the plug-in state). Message thread only.

    inline std::string patternToText (const Pattern& p)
    {
        std::ostringstream o;
        o << (int) p.key << '|';
        for (int i = 0, n = p.size(); i < n; ++i)
        {
            const auto& s = p.slots[(size_t) i];
            o << (i ? " " : "") << (int) s.chord.root << ':';
            for (int t = 0; t < s.chord.size; ++t)
                o << (t ? "." : "") << (int) s.chord.tones[(size_t) t];
            o << ':' << (int) s.halfBeats << ':' << (int) s.inversion << ':' << (int) s.octave;
        }
        return o.str();
    }

    namespace workspace_detail
    {
        inline bool toInt (const std::string& s, int& out)
        {
            if (s.empty() || s.size() > 6)
                return false;
            size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
            if (i == s.size())
                return false;
            int v = 0;
            for (; i < s.size(); ++i)
            {
                if (s[i] < '0' || s[i] > '9')
                    return false;
                v = v * 10 + (s[i] - '0');
            }
            out = s[0] == '-' ? -v : v;
            return true;
        }

        inline std::vector<std::string> split (const std::string& s, char sep)
        {
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : s)
            {
                if (ch == sep)
                {
                    parts.push_back (cur);
                    cur.clear();
                }
                else
                    cur += ch;
            }
            parts.push_back (cur);
            return parts;
        }
    }

    // Reads patternToText's form. Slots that don't parse are skipped and values out of range
    // are clamped, so a damaged state still loads what it can. False (and `out` empty) if the
    // text has no key.
    inline bool patternFromText (const std::string& text, Pattern& out)
    {
        using namespace workspace_detail;
        out = Pattern {};
        const auto bar = text.find ('|');
        int key = 0;
        if (bar == std::string::npos || ! toInt (text.substr (0, bar), key))
            return false;
        out.key = (int8_t) mod12 (key);

        int n = 0;
        for (const auto& token : split (text.substr (bar + 1), ' '))
        {
            const auto f = split (token, ':');
            int root = 0, half = 0, inv = 0, oct = 0;
            if (f.size() != 5 || ! toInt (f[0], root) || ! toInt (f[2], half) || ! toInt (f[3], inv) || ! toInt (f[4], oct))
                continue;
            std::array<int, maxWorkspaceTones> tones {};
            int count = 0;
            bool ok = true;
            for (const auto& t : split (f[1], '.'))
                if (count < maxWorkspaceTones)
                    ok = ok && toInt (t, tones[(size_t) count++]);
            if (! ok || count == 0 || n == numPatternSlots)
                continue;
            out.slots[(size_t) n++] = makePatternSlot (makeWorkspaceChord (mod12 (root), tones.data(), count), half, inv, oct);
        }
        return true;
    }
}
