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
// Bind keys (Phase 3, KeyRouter.h): a slot key while playing jumps to that slot at the next
// beat, and the pattern goes on from there, shifted against the song until the transport stops
// or the pattern switches; A# silences the chord until the next slot line.
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
        void truncate (int n) noexcept { count = n < count ? (n < 0 ? 0 : n) : count; }
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
        // (enabled, not analysing). `requested`: the pattern asked for (the Pattern parameter, or
        // a Bind key). For a segment of a block (Clock.h): `seg` its index, `numSamples` its
        // length, `offset` its first sample.
        //
        // With `resumeSample` ≥ 0 the segment's start was handled already: this continues from
        // that sample of the block (after a jump request, the triggers after it are generated
        // again).
        template <class Out>
        void generate (bool on, const Workspace& ws, int requested, const Clock& clock, int seg,
                       int numSamples, Out& out, int offset = 0, int resumeSample = -1) noexcept
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
                switchAt = jumpAt = HUGE_VAL;
                jumpSlot = -1;
                shift = 0.0;
                silenced = false;
                haveLastEnd = false;
                return;
            }

            const bool resuming = resumeSample >= 0;
            const double from = resuming ? ppq0 + (resumeSample - offset) / spq : ppq0;
            const bool jumped = ! resuming && haveLastEnd && std::abs (ppq0 - lastEnd) * spq > 0.5;

            if (requested != playing && current < 0 && ! silenced)
                playing = requested;                          // nothing sounds yet: switch now
            if (requested == playing)
                switchAt = HUGE_VAL;
            else if (jumped)
                switchAt = ppq0;                              // a jump counts as a bar line
            else if (switchAt == HUGE_VAL)
            {
                const double bar = clock.barStart (from);
                switchAt = std::abs (bar - from) * spq < 0.5 ? from : clock.nextBarLine (from);
            }
            if (jumpSlot >= 0 && jumpAt == HUGE_VAL)
                jumpAt = jumped ? ppq0 : beatAtOrAfter (from, spq);

            auto emit = [&] (double at, int slot) noexcept
            {
                int s = (int) std::llround ((at - ppq0) * spq);
                s = offset + (s < 0 ? 0 : (s >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : s));
                silenced = false;
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
            if (! resuming)
            {
                bool force = restart || jumped;
                if (switchAt <= ppq0 + eps)
                    force |= applySwitch (requested);
                if (jumpAt <= ppq0 + eps)
                    force |= applyJump (ws, ppq0);
                const Pattern& p = ws.patterns[(size_t) playing];
                const int want = slotAt (p, ppq0 + shift);
                const bool onLine = nextSlotLine (p, ppq0 + shift - 2.0 * eps) - shift <= ppq0 + eps;
                if (force || onLine || (want != current && ! silenced))
                    emit (ppq0, want);
                restart = false;
            }

            // Slot lines, the switch and the jump inside it.
            double t = from;
            for (int guard = 0; guard < 4096; ++guard)
            {
                const double line = nextSlotLine (ws.patterns[(size_t) playing], t + shift) - shift;
                double next = line < switchAt ? line : switchAt;
                next = jumpAt < next ? jumpAt : next;
                if (next >= endPpq - eps || next == HUGE_VAL)
                    break;
                if (next == switchAt)
                    applySwitch (requested);
                if (next == jumpAt)
                    applyJump (ws, next);
                emit (next, slotAt (ws.patterns[(size_t) playing], next + shift));
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
            silenced = false;
        }

        // A Bind key (Phase 3): the playing pattern's slot `slot` plays from the first beat at or
        // after now, and the pattern carries on from there (shifted against the song until the
        // next stop or switch). The engine regenerates the rest of the segment.
        void requestJump (int slot) noexcept
        {
            jumpSlot = slot;
            jumpAt = HUGE_VAL;
        }

        // The A# Bind key (Phase 3): the engine ended the chord; nothing plays until the next
        // slot line (or a jump, a switch, a locate).
        void silenceUntilNextSlot() noexcept
        {
            current = -1;
            silenced = true;
        }

        int playingPattern() const noexcept { return playing; }
        int currentSlot() const noexcept { return current; }
        bool switchPending() const noexcept { return switchAt != HUGE_VAL; }
        bool jumpPending() const noexcept { return jumpSlot >= 0; }
        double shiftBeats() const noexcept { return shift; }

    private:
        static constexpr double eps = 1.0e-7;

        // The first beat line at or after `ppq` (within half a sample: now).
        static double beatAtOrAfter (double ppq, double spq) noexcept
        {
            return std::ceil (ppq - 0.5 / spq);
        }

        // Where slot `slot` starts in `p`'s loop (beats).
        static double slotStart (const Pattern& p, int slot) noexcept
        {
            double t = 0.0;
            for (int i = 0; i < slot && i < numPatternSlots; ++i)
                t += p.slots[(size_t) i].halfBeats * 0.5;
            return t;
        }

        bool applySwitch (int requested) noexcept
        {
            playing = requested;
            switchAt = HUGE_VAL;
            shift = 0.0;                  // a new pattern is locked to the song again
            return true;
        }

        // The jump happens at `at`: the pattern is shifted so that its slot starts there.
        bool applyJump (const Workspace& ws, double at) noexcept
        {
            const Pattern& p = ws.patterns[(size_t) playing];
            const int slot = jumpSlot;
            jumpSlot = -1;
            jumpAt = HUGE_VAL;
            const double len = p.totalHalfBeats() * 0.5;
            if (slot < 0 || slot >= p.size() || len <= 0.0)
                return false;
            shift = std::fmod (slotStart (p, slot) - at, len);
            if (shift < 0.0)
                shift += len;
            return true;
        }

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
        int jumpSlot = -1;                // a Bind key's slot, waiting for its beat
        double jumpAt = HUGE_VAL;         // that beat (ppq), once known
        double shift = 0.0;               // the pattern's position = ppq + shift (beats)
        bool silenced = false;            // A#: nothing until the next line
        double lastEnd = 0.0;
        bool haveLastEnd = false;
    };
}
