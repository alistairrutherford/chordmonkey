// Detect's result (PLAN.md §3.6, Phase 8): the chords a ChordListener heard, as a pattern. Plain
// C++, no JUCE; uses std::string (ChordNamer): message thread only.
//
//   recognition  ChordNamer reads the pitch classes over the bass (inversions, extensions,
//                altered and slash chords: every pitch class tried as the root, the cheapest
//                reading wins, the bass on ties)
//   the chord    one of the 27 chord types in root position where the pitch classes are one;
//                otherwise the tones as played, folded into an octave above the root. A bass
//                that's a chord tone is the slot's inversion (up to the 3rd); one that isn't
//                (or a higher inversion) goes under the chord. The slot's octave keeps the
//                register played (the bass within a few semitones)
//   time         onsets on the half-beat grid from the first chord's; a slot lasts to the next
//                chord's onset, the last to its release (or `now` while held) rounded up to a
//                whole beat (a chord let go early still fills its beat); two chords on
//                one grid point keep the later; a chord longer than 8 beats takes more slots;
//                at most eight slots
#pragma once

#include "../engine/ChordListener.h"
#include "../theory/ChordNamer.h"
#include "../theory/ChordTypes.h"
#include "Workspace.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace chordmonkey
{
    // The workspace chord and inversion for pitch classes `mask` over the lowest note `lowest`.
    inline PatternSlot slotForHeard (unsigned mask, int lowest, int key, Scale scale, int halfBeats)
    {
        mask &= 0xfffu;
        const int bassPc = ((lowest % 12) + 12) % 12;
        const ChordName name = nameChord (mask | 1u << bassPc, bassPc, makeSpelling (key, scale));
        const int root = name.root < 0 ? bassPc : name.root;
        const unsigned rel = ((mask >> root) | (mask << (12 - root))) & 0xfffu;
        const int bassRel = ((bassPc - root) % 12 + 12) % 12;

        std::array<int, maxWorkspaceTones> tones {};
        int count = 0;
        if (const int type = chordTypeForMask (rel | 1u); type >= 0 && (rel & 1u) != 0)
        {
            const ChordTones t = chordTypeTones (type);
            for (int i = 0; i < t.size && count < maxWorkspaceTones; ++i)
                tones[(size_t) count++] = t.tones[(size_t) i];
        }
        else
        {
            tones[(size_t) count++] = 0;      // the root, even where only the bass implies it
            for (int pc = 1; pc < 12 && count < maxWorkspaceTones; ++pc)
                if (((rel >> pc) & 1u) != 0)
                    tones[(size_t) count++] = pc;
        }

        int inversion = 0;
        if (bassRel != 0)
        {
            int at = -1;
            for (int i = 0; i < count; ++i)
                if (tones[(size_t) i] % 12 == bassRel)
                    at = i;
            if (at >= 1 && at <= maxSlotInversion)
                inversion = at;
            else
            {
                // A bass the inversions can't reach: under the chord, an octave down.
                if (at >= 0)
                {
                    for (int i = at; i + 1 < count; ++i)
                        tones[(size_t) i] = tones[(size_t) i + 1];
                    --count;
                }
                if (count < maxWorkspaceTones)
                    tones[(size_t) count++] = bassRel - 12;
            }
        }

        PatternSlot s = makePatternSlot (makeWorkspaceChord (root, tones.data(), count), halfBeats, inversion, 0);
        // The register played: the bass where it was, within a few semitones.
        const ChordNotes at0 = slotNotes (s);
        if (at0.size > 0)
        {
            const int shift = (int) std::lround ((lowest - (int) at0.notes[0]) / 12.0);
            s.octave = (int8_t) std::clamp (shift, minSlotOctave, maxSlotOctave);
        }
        return s;
    }

    // The heard chords as a pattern in `key` (empty if none counted).
    inline Pattern patternFromHeard (const HeardChords& heard, int key, Scale scale)
    {
        Pattern p;
        p.key = (int8_t) (((key % 12) + 12) % 12);
        if (heard.count <= 0)
            return p;

        const double origin = heard.chords[0].start;
        const auto grid = [origin] (double beat) { return (long) std::lround ((beat - origin) * 2.0); };

        // Onsets on the grid; a later chord on the same point replaces the earlier.
        struct Item { long at; const HeardChord* chord; };
        std::vector<Item> items;
        for (int i = 0; i < heard.count && i < maxHeard; ++i)
        {
            const auto& c = heard.chords[(size_t) i];
            const long at = grid (c.start);
            if (! items.empty() && items.back().at >= at)
                items.back().chord = &c;
            else
                items.push_back ({ at, &c });
        }

        int n = 0;
        for (size_t i = 0; i < items.size() && n < numPatternSlots; ++i)
        {
            const auto& c = *items[i].chord;
            long end = i + 1 < items.size() ? items[i + 1].at : grid (c.end >= 0.0 ? c.end : heard.now);
            if (i + 1 == items.size())
                end = items[i].at + ((end - items[i].at + 1) / 2) * 2;   // up to a whole beat
            long length = std::max (1L, end - items[i].at);
            while (length > 0 && n < numPatternSlots)
            {
                const int piece = (int) std::min<long> (length, maxSlotHalfBeats);
                p.slots[(size_t) n++] = slotForHeard (c.mask, c.lowest, key, scale, piece);
                length -= piece;
            }
        }
        return p;
    }
}
