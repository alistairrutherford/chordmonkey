// Styles (PLAN.md §3.4, Phase 4): the rhythm a chord is played in. Each style is a fixed table
// of hits over one bar (in beats from the bar line, for 4/4; hits past a shorter bar don't
// sound), repeated bar after bar on the host's bar grid, like the Step Sequencer. A hit plays
// some of the chord's voices for its length, at a velocity scale:
//
//   voices   All · Low (the lowest note; the bass, with Bass on) · Upper (all but the lowest) ·
//            the n-th note from the bottom (0 = lowest) · the n-th from the top (Top + n);
//            past the chord's size they wrap
//
// Hits never overlap: a chord is one strike at a time. Swing (0-75%) delays a hit on an
// off-beat eighth by swing/3 of a beat (50%: a triplet feel) and one on an off-beat sixteenth
// by swing/6; it keeps its length, up to the next hit.
//
// Block (the first style) has no hits: the chord sounds as long as it's held, and Repeat and
// the Step Sequencer apply. Any other style replaces them. Humanize (humanize()) gives each tone
// of a strike a later start, up to 15 ms, and a velocity offset, up to ±12, scaled by the
// amount, from a hash of the strike's song position: the same take plays the same way at any
// block size. Plain C++, no JUCE, no allocation.
#pragma once

#include "NoteValues.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace chordmonkey
{
    struct StyleHit
    {
        float start, length;              // beats from the bar line
        int8_t voices;                    // StyleVoices, or a note index
        uint8_t velocity;                 // percent of the key's velocity
    };

    namespace StyleVoices
    {
        constexpr int8_t All = -1, Low = -2, Upper = -3, Top = 16;   // Top + n: the n-th from the top
    }

    constexpr int maxStyleHits = 16;

    struct StyleInfo
    {
        const char* name;
        std::array<StyleHit, maxStyleHits> hits;
        int numHits;
    };

    // The Style parameter's choices, in its order (saved by index: append only).
    constexpr int numStyles = 18;

    inline const StyleInfo& styleInfo (int i) noexcept
    {
        using namespace StyleVoices;
        constexpr int8_t T = Top;
        static constexpr StyleInfo table[numStyles] = {
            { "Block", {}, 0 },
            { "Quarters",       { { { 0, 0.9f, All, 100 }, { 1, 0.9f, All, 85 }, { 2, 0.9f, All, 95 }, { 3, 0.9f, All, 85 } } }, 4 },
            { "Eighths",        { { { 0, 0.45f, All, 100 }, { 0.5f, 0.45f, All, 75 }, { 1, 0.45f, All, 90 }, { 1.5f, 0.45f, All, 75 },
                                    { 2, 0.45f, All, 95 }, { 2.5f, 0.45f, All, 75 }, { 3, 0.45f, All, 90 }, { 3.5f, 0.45f, All, 75 } } }, 8 },
            { "Sixteenths",     { { { 0, 0.2f, All, 100 }, { 0.25f, 0.2f, All, 70 }, { 0.5f, 0.2f, All, 85 }, { 0.75f, 0.2f, All, 70 },
                                    { 1, 0.2f, All, 95 }, { 1.25f, 0.2f, All, 70 }, { 1.5f, 0.2f, All, 85 }, { 1.75f, 0.2f, All, 70 },
                                    { 2, 0.2f, All, 100 }, { 2.25f, 0.2f, All, 70 }, { 2.5f, 0.2f, All, 85 }, { 2.75f, 0.2f, All, 70 },
                                    { 3, 0.2f, All, 95 }, { 3.25f, 0.2f, All, 70 }, { 3.5f, 0.2f, All, 85 }, { 3.75f, 0.2f, All, 70 } } }, 16 },
            { "Half notes",     { { { 0, 1.9f, All, 100 }, { 2, 1.9f, All, 90 } } }, 2 },
            { "Off-beats",      { { { 0.5f, 0.4f, All, 95 }, { 1.5f, 0.4f, All, 90 }, { 2.5f, 0.4f, All, 95 }, { 3.5f, 0.4f, All, 90 } } }, 4 },
            { "House stabs",    { { { 0.5f, 0.2f, All, 110 }, { 1.5f, 0.2f, All, 100 }, { 2.5f, 0.2f, All, 110 }, { 3.5f, 0.2f, All, 100 } } }, 4 },
            { "Charleston",     { { { 0, 0.75f, All, 105 }, { 1.5f, 0.5f, All, 95 }, { 2, 0.75f, All, 100 }, { 3.5f, 0.5f, All, 90 } } }, 4 },
            { "Dotted quarters", { { { 0, 1.4f, All, 105 }, { 1.5f, 1.4f, All, 95 }, { 3, 0.9f, All, 95 } } }, 3 },
            { "Tresillo",       { { { 0, 0.7f, All, 105 }, { 0.75f, 0.7f, All, 90 }, { 1.5f, 0.45f, All, 95 },
                                    { 2, 0.7f, All, 100 }, { 2.75f, 0.7f, All, 90 }, { 3.5f, 0.45f, All, 95 } } }, 6 },
            { "Reggae skank",   { { { 1, 0.25f, Upper, 100 }, { 3, 0.25f, Upper, 100 } } }, 2 },
            { "Bass & chord",   { { { 0, 0.9f, Low, 105 }, { 1, 0.9f, Upper, 90 }, { 2, 0.9f, Low, 100 }, { 3, 0.9f, Upper, 90 } } }, 4 },
            { "Ballad",         { { { 0, 1.45f, Low, 105 }, { 1.5f, 2.4f, Upper, 90 } } }, 2 },
            { "Broken up",      { { { 0, 0.45f, 0, 100 }, { 0.5f, 0.45f, 1, 85 }, { 1, 0.45f, 2, 90 }, { 1.5f, 0.45f, 3, 85 },
                                    { 2, 0.45f, 0, 95 }, { 2.5f, 0.45f, 1, 85 }, { 3, 0.45f, 2, 90 }, { 3.5f, 0.45f, 3, 85 } } }, 8 },
            { "Broken down",    { { { 0, 0.45f, T + 0, 100 }, { 0.5f, 0.45f, T + 1, 85 }, { 1, 0.45f, T + 2, 90 }, { 1.5f, 0.45f, T + 3, 85 },
                                    { 2, 0.45f, T + 0, 95 }, { 2.5f, 0.45f, T + 1, 85 }, { 3, 0.45f, T + 2, 90 }, { 3.5f, 0.45f, T + 3, 85 } } }, 8 },
            { "Alberti",        { { { 0, 0.45f, 0, 100 }, { 0.5f, 0.45f, T + 0, 80 }, { 1, 0.45f, 1, 90 }, { 1.5f, 0.45f, T + 0, 80 },
                                    { 2, 0.45f, 0, 95 }, { 2.5f, 0.45f, T + 0, 80 }, { 3, 0.45f, 1, 90 }, { 3.5f, 0.45f, T + 0, 80 } } }, 8 },
            { "Four on the floor", { { { 0, 0.125f, All, 100 }, { 1, 0.125f, All, 100 }, { 2, 0.125f, All, 100 }, { 3, 0.125f, All, 100 } } }, 4 },
            { "Dembow",         { { { 0, 0.125f, All, 100 }, { 0.75f, 0.125f, All, 100 }, { 1.5f, 0.125f, All, 100 },
                                    { 2, 0.125f, All, 100 }, { 2.75f, 0.125f, All, 100 }, { 3.5f, 0.125f, All, 100 } } }, 6 },
        };
        return table[i >= 0 && i < numStyles ? i : 0];
    }

    struct StyleSettings
    {
        int style = 0;                    // 0 = Block
        double swing = 0.0;               // 0..0.75
        double humanize = 0.0;            // 0..1

        bool on() const noexcept { return style > 0 && style < numStyles && styleInfo (style).numHits > 0; }
    };

    // Which of a chord's notes (`size` of them, lowest first) a hit's voices take: bit i = note i.
    inline uint64_t styleVoiceMask (int voices, int size) noexcept
    {
        if (size <= 0)
            return 0;
        const uint64_t all = size >= 64 ? ~0ull : ((1ull << size) - 1);
        if (voices == StyleVoices::All)
            return all;
        if (voices == StyleVoices::Low)
            return 1;
        if (voices == StyleVoices::Upper)
            return size > 1 ? all & ~1ull : all;
        if (voices >= StyleVoices::Top)
            return 1ull << (size - 1 - (voices - StyleVoices::Top) % size);
        return voices >= 0 ? 1ull << (voices % size) : all;
    }

    struct StyleWindow
    {
        bool valid = false;
        double start = 0.0, end = 0.0;    // ppq
        int8_t voices = StyleVoices::All;
        uint8_t velocity = 100;
    };

    // A style's hits on the bar grid a clock reports (bar lines at origin + k · length).
    class StyleGrid
    {
    public:
        StyleGrid (const StyleSettings& s, double barOrigin, double barLength) noexcept
            : info (styleInfo (s.style)), swing (s.swing < 0.0 ? 0.0 : (s.swing > 0.75 ? 0.75 : s.swing)),
              origin (barOrigin), barLen (barLength > 0.0 ? barLength : 4.0) {}

        double barStart (double ppq) const noexcept
        {
            return origin + std::floor ((ppq - origin) / barLen + 1.0e-9) * barLen;
        }

        // Hit `i` of the bar at `bar` (invalid past a short bar). A swung hit keeps its length,
        // up to the next hit's start or the bar line.
        StyleWindow hit (double bar, int i) const noexcept
        {
            StyleWindow w;
            if (i < 0 || i >= info.numHits)
                return w;
            const auto& h = info.hits[(size_t) i];
            const double start = swung (i);
            if (h.start >= barLen - grid::epsilon || start >= barLen - grid::epsilon)
                return w;
            double end = std::fmin (start + h.length, barLen);
            if (i + 1 < info.numHits)
                end = std::fmin (end, swung (i + 1));
            w.valid = true;
            w.start = bar + start;
            w.end = bar + end;
            w.voices = h.voices;
            w.velocity = h.velocity;
            return w;
        }

        // The hit sounding at `ppq`, if any.
        StyleWindow windowAt (double ppq) const noexcept
        {
            const double bar = barStart (ppq);
            for (int i = 0; i < info.numHits; ++i)
            {
                const StyleWindow w = hit (bar, i);
                if (w.valid && w.start <= ppq + grid::epsilon && ppq < w.end - grid::epsilon)
                    return w;
            }
            return {};
        }

        // The first hit that starts after `ppq` (at or after it, if `inclusive`). False if the
        // style has no hit in a bar of this length.
        bool nextStart (double ppq, bool inclusive, double& out) const noexcept
        {
            double bar = barStart (ppq);
            for (int b = 0; b < 3; ++b, bar += barLen)
                for (int i = 0; i < info.numHits; ++i)
                {
                    const StyleWindow w = hit (bar, i);
                    if (w.valid && (inclusive ? w.start >= ppq - grid::epsilon : w.start > ppq + grid::epsilon))
                    {
                        out = w.start;
                        return true;
                    }
                }
            return false;
        }

    private:
        // Hit `i`'s start in its bar, swung.
        double swung (int i) const noexcept
        {
            const double s = info.hits[(size_t) i].start;
            const double frac = s - std::floor (s);
            if (std::abs (frac - 0.5) < 1.0e-6)
                return s + swing / 3.0;
            if (std::abs (frac - 0.25) < 1.0e-6 || std::abs (frac - 0.75) < 1.0e-6)
                return s + swing / 6.0;
            return s;
        }

        const StyleInfo& info;
        double swing, origin, barLen;
    };

    // Humanize: a strike's tone `i` at song position `ppq` (performance `key`): a delay in
    // 0..1 (× 15 ms × amount) and a velocity offset in -1..1 (× 12 × amount).
    struct HumanizeJitter
    {
        double delay = 0.0, velocity = 0.0;
    };

    constexpr double maxHumanizeSeconds = 0.015;
    constexpr int maxHumanizeVelocity = 12;

    inline HumanizeJitter humanize (double ppq, int key, int i) noexcept
    {
        // splitmix64 of the position (1/3840 beat), the performance and the tone.
        uint64_t x = (uint64_t) (int64_t) std::llround (ppq * 3840.0) * 0x9e3779b97f4a7c15ull
                   ^ ((uint64_t) (uint32_t) key << 40) ^ ((uint64_t) (uint32_t) i << 52);
        auto next = [&x]() noexcept
        {
            uint64_t z = (x += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        };
        HumanizeJitter j;
        j.delay = (double) (next() >> 11) * (1.0 / 9007199254740992.0);
        j.velocity = (double) (next() >> 11) * (2.0 / 9007199254740992.0) - 1.0;
        return j;
    }
}
