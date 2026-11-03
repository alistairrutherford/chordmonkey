// Chord sets as Roman numerals (PLAN.md §3.5), and the names of workspace chords. Plain C++, no
// JUCE; std::string, so for the message thread only.
//
// A progression is a line of numerals, each with an optional length in beats (default 4):
//
//   I V vi IV                      four chords, a bar each
//   ii7:2 V7:2 Imaj7:8             a two-five-one
//   i bVI bIII bVII                a minor-key loop
//
// A numeral counts from the tonic by the major scale's degrees (I II III IV V VI VII = 0 2 4 5
// 7 9 11 semitones), with b or # for the others, so the same chord set works in any key and
// scale. Upper case is a major 3rd, lower case a minor one; the suffix is the chord type's
// (ChordTypes.h): V7, IVmaj7, ii7, viiø7, vii°7, IVsus4, bVII9, iii°, III+. A chord that no type
// describes keeps its tones, written after the numeral: V(0.4.7.10.13) (semitones above the root).
//
// Read in a key, a numeral's root is placed key + offset semitones above C2, so the chords sit
// where the palette's do (V in A major is E3, as the white key G plays it).
#pragma once

#include "Workspace.h"
#include "../theory/ChordNamer.h"
#include "../theory/ChordTypes.h"
#include <cctype>

namespace chordmonkey
{
    namespace chordsets_detail
    {
        using workspace_detail::mod12;

        inline const char* romanForOffset (int offset) noexcept
        {
            static constexpr const char* names[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };
            return names[mod12 (offset)];
        }

        // Longest first, so "IV" isn't read as "I".
        struct Roman { const char* text; int offset; };
        constexpr Roman romans[] = { { "VII", 11 }, { "III", 4 }, { "IV", 5 }, { "VI", 9 }, { "II", 2 }, { "V", 7 }, { "I", 0 } };

        inline std::string lower (std::string s)
        {
            for (auto& ch : s)
                ch = (char) std::tolower ((unsigned char) ch);
            return s;
        }

        // Spellings people write for the same suffix.
        inline std::string canonicalSuffix (const std::string& s)
        {
            if (s == "o" || s == "dim")                     return "°";
            if (s == "o7" || s == "dim7")                   return "°7";
            if (s == "ø" || s == "m7b5")                    return "ø7";
            if (s == "aug")                                 return "+";
            if (s == "M7" || s == "Maj7" || s == "Δ")       return "maj7";
            return s;
        }

        // Semitones above the root of a type's tones, as ints.
        inline int typeTones (int type, std::array<int, maxWorkspaceTones>& out) noexcept
        {
            const ChordTones t = chordTypeTones (type);
            for (int i = 0; i < t.size; ++i)
                out[(size_t) i] = t.tones[(size_t) i];
            return t.size;
        }

        // Where key + offset puts the root: above C2, up to B3.
        constexpr int rootFromC (int key, int offset) noexcept { return mod12 (key) + mod12 (offset); }
    }

    // Reads one token (a numeral and an optional ":beats") in `key`. On failure returns false
    // and says why in `error`.
    inline bool parseNumeral (const std::string& token, int key, WorkspaceChord& chord, int& halfBeats, std::string& error)
    {
        using namespace chordsets_detail;
        std::string s = token;
        halfBeats = defaultSlotHalfBeats;

        // Length.
        if (const auto colon = s.find (':'); colon != std::string::npos)
        {
            const std::string beats = s.substr (colon + 1);
            s = s.substr (0, colon);
            int whole = 0;
            bool half = false;
            std::string w = beats;
            if (w.size() > 2 && w.compare (w.size() - 2, 2, ".5") == 0)
            {
                half = true;
                w = w.substr (0, w.size() - 2);
            }
            if (! workspace_detail::toInt (w, whole) || whole < 0)
            {
                error = "\"" + token + "\": the length after ':' must be beats, such as 4, 2 or 1.5";
                return false;
            }
            halfBeats = whole * 2 + (half ? 1 : 0);
            if (halfBeats < minSlotHalfBeats || halfBeats > maxSlotHalfBeats)
            {
                error = "\"" + token + "\": a chord lasts ½ to 8 beats";
                return false;
            }
        }

        // Accidental.
        int offset = 0;
        size_t i = 0;
        if (i < s.size() && (s[i] == 'b' || s[i] == '#'))
            offset = s[i++] == 'b' ? -1 : 1;

        // Numeral.
        bool found = false, isLower = false;
        for (const auto& r : romans)
        {
            const std::string up = r.text, lo = lower (up);
            if (s.compare (i, up.size(), up) == 0 || s.compare (i, lo.size(), lo) == 0)
            {
                isLower = s.compare (i, lo.size(), lo) == 0;
                offset += r.offset;
                i += up.size();
                found = true;
                break;
            }
        }
        if (! found)
        {
            error = "\"" + token + "\": expected a Roman numeral (I … VII, or i … vii)";
            return false;
        }

        std::string suffix = s.substr (i);
        std::array<int, maxWorkspaceTones> tones {};
        int count = 0;

        if (const auto paren = suffix.find ('('); paren != std::string::npos)
        {
            // Explicit tones.
            if (paren != 0 || suffix.back() != ')')
            {
                error = "\"" + token + "\": write tones as V(0.4.7), with nothing between the numeral and '('";
                return false;
            }
            for (const auto& t : workspace_detail::split (suffix.substr (1, suffix.size() - 2), '.'))
                if (count == maxWorkspaceTones || ! workspace_detail::toInt (t, tones[(size_t) count++]))
                {
                    error = "\"" + token + "\": tones are up to eight whole numbers of semitones, separated by '.'";
                    return false;
                }
        }
        else
        {
            suffix = canonicalSuffix (suffix);
            int type = -1;
            for (int t = 0; t < numChordTypes && type < 0; ++t)
            {
                const auto& ct = chordType (t);
                if (suffix == ct.numeralSuffix && (ct.anyCase || ct.lowerCase == isLower))
                    type = t;
            }
            if (type < 0)
            {
                error = "\"" + token + "\": unknown chord suffix \"" + suffix + "\"";
                return false;
            }
            count = typeTones (type, tones);
        }

        chord = makeWorkspaceChord (rootFromC (key, offset), tones.data(), count);
        return true;
    }

    // The numeral for `chord` in `key`, as parseNumeral reads it back (the same chord, in the
    // same octave).
    inline std::string numeralFor (const WorkspaceChord& chord, int key)
    {
        using namespace chordsets_detail;
        if (chord.empty())
            return {};
        const int offset = mod12 (chord.root - key);
        const int carry = rootFromC (key, offset) - chord.root;

        std::array<int, maxWorkspaceTones> rel {};
        for (int i = 0; i < chord.size; ++i)
            rel[(size_t) i] = chord.tones[(size_t) i] - carry;

        const unsigned mask = chord.relativeMask();
        const int type = chordTypeForMask (mask);
        std::array<int, maxWorkspaceTones> want {};
        const int wantCount = type >= 0 ? typeTones (type, want) : 0;
        const bool asType = type >= 0 && wantCount == chord.size && std::equal (want.begin(), want.begin() + wantCount, rel.begin());

        const bool minorThird = (mask & (1u << 3)) != 0 && (mask & (1u << 4)) == 0;
        const bool isLower = asType ? chordType (type).lowerCase : minorThird;
        std::string s = romanForOffset (offset);
        if (isLower)
            for (auto& ch : s)
                if (ch == 'I' || ch == 'V')
                    ch = (char) std::tolower ((unsigned char) ch);

        if (asType)
            return s + chordType (type).numeralSuffix;

        s += '(';
        for (int i = 0; i < chord.size; ++i)
            s += (i ? "." : "") + std::to_string (rel[(size_t) i]);
        return s + ')';
    }

    // The numeral that names `chord` in `key`, whatever octave it sits in (for display): C in D
    // is "bVII", where numeralFor, keeping the octave, writes "bVII(-12.-8.-5)" for a C an
    // octave below the numeral's own.
    inline std::string numeralName (const WorkspaceChord& chord, int key)
    {
        using namespace chordsets_detail;
        if (chord.empty())
            return {};
        const int carry = rootFromC (key, mod12 (chord.root - key)) - chord.root;
        WorkspaceChord c = chord;
        for (int i = 0; i < c.size; ++i)
            c.tones[(size_t) i] = (int8_t) (c.tones[(size_t) i] + carry);
        return numeralFor (c, key);
    }

    // Reads a progression into `out` (key `key`, up to eight chords).
    inline bool parseProgression (const std::string& text, int key, Pattern& out, std::string& error)
    {
        Pattern p;
        p.key = (int8_t) workspace_detail::mod12 (key);
        int n = 0;
        std::istringstream in (text);
        std::string token;
        while (in >> token)
        {
            if (n == numPatternSlots)
            {
                error = "a pattern holds at most " + std::to_string (numPatternSlots) + " chords";
                return false;
            }
            WorkspaceChord chord;
            int half = 0;
            if (! parseNumeral (token, key, chord, half, error))
                return false;
            p.slots[(size_t) n++] = makePatternSlot (chord, half);
        }
        if (n == 0)
        {
            error = "no chords";
            return false;
        }
        out = p;
        return true;
    }

    // A pattern as a progression in its own key (lengths of 4 beats left out). Inversions and
    // octaves aren't part of a chord set.
    inline std::string formatProgression (const Pattern& p)
    {
        std::string s;
        for (int i = 0, n = p.size(); i < n; ++i)
        {
            const auto& slot = p.slots[(size_t) i];
            s += (i ? " " : "") + numeralFor (slot.chord, p.key);
            if (slot.halfBeats != defaultSlotHalfBeats)
                s += ":" + std::to_string (slot.halfBeats / 2) + (slot.halfBeats % 2 ? ".5" : "");
        }
        return s;
    }

    // A slot's name as the workspace shows it, spelled in `key` and `scale`, with a slash bass
    // when its inversion puts another note at the bottom ("C/E").
    inline ChordName namePatternSlot (const PatternSlot& slot, int key, Scale scale)
    {
        return nameNotes (slotNotes (slot), makeSpelling (key, scale), slot.chord.root);
    }
}
