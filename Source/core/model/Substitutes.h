// Chord substitutes (PLAN.md §2 "Chord exploder", Phase 9): a short list of chords that can
// stand in for a workspace chord, for the slot's right-click menu. Plain C++, no JUCE; uses
// std::vector: message thread only.
//
// For a chord of one of the 27 types (ChordTypes.h), in the Key and Scale, in this order:
//
//   a 3rd below, a 3rd above  where the root is a scale note: the scale's chords on the degrees
//                             a third away, triads for a triad and sevenths for a seventh
//                             chord (C major: C → Am, Em; G7 → Em7, Bm7b5): they share two notes
//   parallel                  major ↔ minor on the same root (C → Cm, Cmaj7 → Cm7, Am → A;
//                             6 ↔ m6): the chord borrowed from the parallel key; not for
//                             dominant, diminished or augmented chords
//   sus2, sus4                on the same root, for a major or minor triad; 7sus4 for a 7th
//   add the 9th               add9, m(add9), maj9, 9 or m9 from the triad or seventh
//   add the 7th               a triad's seventh chord: the scale's where the root is a scale
//                             note and that's one of the types, else a 7th (major) or m7 (minor)
//   tritone sub               a dominant chord (or the major V), as a 7th a tritone away
//                             (G7 → Db7)
//
// Each keeps the chord's register: the new root is the old one moved by at most a tritone
// (C → Am puts A below C). Only chords of the 27 types are offered (a scale's chord that isn't
// one, as harmonic minor's Eb G B D, is left out); the chord itself, and a chord already listed,
// too. Other chords (tones that aren't a type) get none.
#pragma once

#include "../theory/ChordTypes.h"
#include "../theory/Scales.h"
#include "Workspace.h"
#include <cstring>
#include <string>
#include <vector>

namespace chordmonkey
{
    struct Substitute
    {
        WorkspaceChord chord;
        const char* reason;               // "a 3rd below", "parallel minor", "tritone sub"…
    };

    namespace substitutes_detail
    {
        constexpr int mod12 (int n) noexcept { return ((n % 12) + 12) % 12; }
        constexpr int wrap (int n) noexcept { return mod12 (n + 6) - 6; }   // -6..+5

        inline int typeNamed (const char* name) noexcept
        {
            for (int i = 0; i < numChordTypes; ++i)
                if (std::strcmp (chordType (i).name, name) == 0)
                    return i;
            return -1;
        }

        // The chord's root as a note above C2's C (its octave: the root tone's, else the
        // lowest tone's).
        inline int rootFromC (const WorkspaceChord& c) noexcept
        {
            int octave = c.size > 0 ? (c.tones[0] >= 0 ? c.tones[0] / 12 : -((11 - c.tones[0]) / 12)) : 0;
            for (int i = 0; i < c.size; ++i)
                if (mod12 (c.tones[(size_t) i]) == 0)
                {
                    const int t = c.tones[(size_t) i];
                    octave = t >= 0 ? t / 12 : -((11 - t) / 12);
                    break;
                }
            return c.root + 12 * octave;
        }

        // The scale's chord on degree `d` (0-based, any integer): `notes` thirds stacked from it,
        // as semitones above its root.
        inline std::vector<int> diatonicTones (Scale scale, int d, int notes)
        {
            std::vector<int> t;
            const int root = scaleOffset (scale, d);
            for (int k = 0; k < notes; ++k)
                t.push_back (scaleOffset (scale, d + 2 * k) - root);
            return t;
        }
    }

    inline std::vector<Substitute> substitutesFor (const WorkspaceChord& c, int key, Scale scale)
    {
        using namespace substitutes_detail;
        std::vector<Substitute> out;
        if (c.empty())
            return out;
        const unsigned rel = c.relativeMask();
        const int type = chordTypeForMask (rel);
        if (type < 0)
            return out;

        const auto has = [rel] (int i) { return ((rel >> i) & 1u) != 0; };
        const bool major = has (4) && ! has (3);
        const bool minor = has (3) && ! has (4);
        const bool fifth = has (7);
        const bool seventh = has (10) || has (11) || (has (9) && has (6) && minor);
        const bool dominant = has (4) && has (10);
        const bool triad = c.size == 3;
        const std::string name = chordType (type).name;

        const int base = rootFromC (c);
        int degree = -1;
        for (int d = 0; d < 7; ++d)
            if (mod12 (key + scaleInfo (scale).semitones[(size_t) d]) == c.root)
                degree = d;

        // Adds the chord (false if it isn't a type); the chord itself or a repeat is skipped.
        const auto add = [&] (int rootPc, const std::vector<int>& tones, const char* reason)
        {
            if (tones.empty())
                return false;
            const WorkspaceChord s = makeWorkspaceChord (base + wrap (rootPc - c.root), tones.data(), (int) tones.size());
            if (chordTypeForMask (s.relativeMask()) < 0)
                return false;
            const auto same = [&s] (const WorkspaceChord& o) { return o.root == s.root && o.relativeMask() == s.relativeMask(); };
            if (same (c))
                return true;
            for (const auto& x : out)
                if (same (x.chord))
                    return true;
            out.push_back ({ s, reason });
            return true;
        };
        const auto addType = [&] (int rootPc, const char* typeName, const char* reason)
        {
            const int t = typeNamed (typeName);
            if (t < 0)
                return;
            const ChordTones ct = chordTypeTones (t);
            add (rootPc, std::vector<int> (ct.tones.begin(), ct.tones.begin() + ct.size), reason);
        };

        // A 3rd below and above: the scale's chords sharing two notes.
        if (degree >= 0 && (major || minor) && (triad || seventh))
            for (int step : { -2, 2 })
            {
                const int d = degree + step;
                add (mod12 (key + scaleOffset (scale, d)), diatonicTones (scale, d, seventh ? 4 : 3), step < 0 ? "a 3rd below" : "a 3rd above");
            }

        // The parallel chord (borrowed).
        if (! dominant && fifth)
        {
            if (name == "Major") addType (c.root, "Minor", "parallel minor");
            else if (name == "maj7") addType (c.root, "m7", "parallel minor");
            else if (name == "6") addType (c.root, "m6", "parallel minor");
            else if (name == "add9") addType (c.root, "m(add9)", "parallel minor");
            else if (name == "Minor") addType (c.root, "Major", "parallel major");
            else if (name == "m7") addType (c.root, "maj7", "parallel major");
            else if (name == "m6") addType (c.root, "6", "parallel major");
            else if (name == "m(add9)") addType (c.root, "add9", "parallel major");
        }

        // Suspended.
        if (name == "Major" || name == "Minor")
        {
            addType (c.root, "Sus2", "sus2");
            addType (c.root, "Sus4", "sus4");
        }
        else if (name == "7")
            addType (c.root, "7sus4", "sus4");

        // The 9th.
        if (name == "Major") addType (c.root, "add9", "add the 9th");
        else if (name == "Minor") addType (c.root, "m(add9)", "add the 9th");
        else if (name == "maj7") addType (c.root, "maj9", "add the 9th");
        else if (name == "7") addType (c.root, "9", "add the 9th");
        else if (name == "m7") addType (c.root, "m9", "add the 9th");

        // The 7th.
        if (triad && (major || minor || name == "Diminished"))
        {
            if (degree < 0 || ! add (c.root, diatonicTones (scale, degree, 4), "add the 7th"))
                addType (c.root, major ? "7" : (minor ? "m7" : "m7b5"), "add the 7th");
        }

        // The tritone substitute of a dominant.
        if (dominant || (name == "Major" && mod12 (c.root - key) == 7))
            addType (mod12 (c.root + 6), "7", "tritone sub");
        return out;
    }
}
