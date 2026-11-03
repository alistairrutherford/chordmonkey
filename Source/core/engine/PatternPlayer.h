// Pattern playback (PLAN.md §3.2, Phase 2): while Play is on and the host plays, the current
// pattern's chords play in time, each for its slot's length, looping. Plain C++, no JUCE, no
// allocation.
//
// The pattern is locked to the song: its loop starts at song position 0, so the slot under
// the playhead is (ppq mod the pattern's length). A take from bar 1 starts on slot 1, and a
// locate or a loop in the host lands where the pattern would be.
//
// The player decides *when* a slot starts and emits triggers; the engine plays each trigger
// through the effect chain (ChordFx.h) as one more performance, so Strum, Arp, Repeat, the Step
// Sequencer, Velocity and voice leading apply to the pattern as to a key. A trigger restrikes:
//
//   - at every slot start (a one-slot pattern restrikes each time round),
//   - at the start of playback, and wherever the playhead jumps (a locate or a loop wrap): the
//     slot under the playhead plays from there,
//   - when the slot under the playhead changes otherwise (an edit moved the slot lines).
//
// Switching pattern (the Pattern parameter) while playing waits for the next bar line, or a
// jump, whichever comes first; while stopped it's immediate. Play off, the transport stopping,
// or an empty pattern ends the chord.
//
// Like TestPattern, a segment that ends at a loop wrap stops short of the loop end in musical
// time: a slot line there belongs after the wrap.
#pragma once

#include "../model/Workspace.h"
#include "Clock.h"
#include "MidiEvents.h"
#include <cmath>

namespace chordmonkey
{
    struct PatternTrigger
    {
        int sample = 0;
        int8_t pattern = 0;               // 0..3
        int8_t slot = -1;                 // 0..7, or -1: stop
    };

    template <int Capacity>
    struct TriggerBuffer
    {
        static constexpr int capacity = Capacity;
        std::array<PatternTrigger, Capacity> items {};
        int count = 0;

        void clear() noexcept { count = 0; }
        int size() const noexcept { return count; }
        const PatternTrigger& operator[] (int i) const noexcept { return items[(size_t) i]; }
        bool push (const PatternTrigger& t) noexcept
        {
            if (count >= Capacity)
                return false;
            items[(size_t) count++] = t;
            return true;
        }
    };

    class PatternPlayer
    {
    public:
        // The slot of `p` under musical time `ppq`, or -1 for an empty pattern.
        static int slotAt (const Pattern& p, double ppq) noexcept
        {
            const int n = p.size();
            const int total = p.totalHalfBeats();
            if (n == 0 || total <= 0)
                return -1;
            const double len = total * 0.5;
            double pos = std::fmod (ppq, len);
            if (pos < 0.0)
                pos += len;
            double start = 0.0;
            for (int i = 0; i < n; ++i)
            {
                start += p.slots[(size_t) i].halfBeats * 0.5;
                if (pos < start - eps)
                    return i;
            }
            return 0;   // within eps of the loop's end: the next time round
        }

        // The first slot line of `p` after `ppq` (+ eps), in musical time.
        static double nextSlotLine (const Pattern& p, double ppq) noexcept
        {
            const int n = p.size();
            const int total = p.totalHalfBeats();
            if (n == 0 || total <= 0)
                return HUGE_VAL;
            const double len = total * 0.5;
            const double origin = std::floor ((ppq + eps) / len) * len;
            double line = origin;
            for (int i = 0; i < n; ++i)
            {
                line += p.slots[(size_t) i].halfBeats * 0.5;
                if (line > ppq + eps)
                    return line;
            }
            return origin + len;
        }

        // This segment's triggers, in sample order. `on`: Play is on and the engine may play
        // (enabled, not analysing). `requested`: the Pattern parameter. For a segment of a block
        // (Clock.h): `seg` its index, `numSamples` its length, `offset` its first sample.
        template <class Out>
        void generate (bool on, const Workspace& ws, int requested, const Clock& clock, int seg,
                       int numSamples, Out& out, int offset = 0) noexcept
        {
            requested = requested < 0 ? 0 : (requested >= numPatterns ? numPatterns - 1 : requested);
            const ClockSegment& g = clock.segment (seg);
            const double spq = clock.samplesPerQuarter();
            const double ppq0 = g.ppq;
            double endPpq = ppq0 + numSamples / spq;
            if (g.endPpq < endPpq)
                endPpq = g.endPpq;

            if (! on || ! clock.isPlaying())
            {
                stop (offset, out);
                playing = requested;
                switchAt = HUGE_VAL;
                haveLastEnd = false;
                return;
            }

            const bool jumped = haveLastEnd && std::abs (ppq0 - lastEnd) * spq > 0.5;
            if (requested != playing && current < 0)
                playing = requested;                          // nothing sounds yet: switch now
            if (requested == playing)
                switchAt = HUGE_VAL;
            else if (jumped)
            {
                playing = requested;                          // a jump counts as a bar line
                switchAt = HUGE_VAL;
            }
            else if (switchAt == HUGE_VAL)
            {
                const double bar = clock.barStart (ppq0);
                switchAt = std::abs (bar - ppq0) * spq < 0.5 ? ppq0 : clock.nextBarLine (ppq0);
            }

            auto emit = [&] (double at, int slot) noexcept
            {
                int s = (int) std::llround ((at - ppq0) * spq);
                s = offset + (s < 0 ? 0 : (s >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : s));
                if (slot < 0)
                {
                    stop (s, out);
                    return;
                }
                if (out.push ({ s, (int8_t) playing, (int8_t) slot }))
                    current = slot;
            };

            // At the segment's start. A slot line on it belongs here: the segment before stopped
            // short of it.
            {
                if (switchAt <= ppq0 + eps)
                {
                    playing = requested;
                    switchAt = HUGE_VAL;
                    restart = true;
                }
                const Pattern& p = ws.patterns[(size_t) playing];
                const int want = slotAt (p, ppq0);
                const bool onLine = nextSlotLine (p, ppq0 - 2.0 * eps) <= ppq0 + eps;
                if (want != current || jumped || restart || onLine)
                    emit (ppq0, want);
                restart = false;
            }

            // Slot lines and the switch inside it.
            double t = ppq0;
            for (int guard = 0; guard < 4096; ++guard)
            {
                const double line = nextSlotLine (ws.patterns[(size_t) playing], t);
                const double next = switchAt < line ? switchAt : line;
                if (next >= endPpq - eps || next == HUGE_VAL)
                    break;
                if (next == switchAt)
                {
                    playing = requested;
                    switchAt = HUGE_VAL;
                }
                emit (next, slotAt (ws.patterns[(size_t) playing], next));
                t = next;
            }

            lastEnd = ppq0 + numSamples / spq;
            if (g.endPpq < lastEnd)
                lastEnd = g.endPpq;
            haveLastEnd = true;
        }

        // Everything ended outside the player (all notes off, a reset): the slot under the
        // playhead plays again from the next segment.
        void reset() noexcept
        {
            current = -1;
            restart = true;
        }

        int playingPattern() const noexcept { return playing; }
        int currentSlot() const noexcept { return current; }
        bool switchPending() const noexcept { return switchAt != HUGE_VAL; }

    private:
        static constexpr double eps = 1.0e-7;

        template <class Out>
        void stop (int sample, Out& out) noexcept
        {
            if (current >= 0 && out.push ({ sample, (int8_t) playing, (int8_t) -1 }))
                current = -1;
        }

        int playing = 0;                  // the pattern playing (0..3)
        int current = -1;                 // its slot sounding, or -1
        bool restart = true;
        double switchAt = HUGE_VAL;       // when a waiting switch to `requested` happens (ppq)
        double lastEnd = 0.0;
        bool haveLastEnd = false;
    };
}
