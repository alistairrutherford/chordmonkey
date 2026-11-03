// Test pattern: while the transport runs, presses one white key on every beat (C, D, E, F, G,
// A, B, C… in the home octave, so beat b plays chord slot b mod 7) and releases it an eighth
// note later. The key presses go through the same path as the user's, so a recording of the
// output checks routing, chord generation and timing (LiveBridge, ChordChimp's PLAN.md §8).
//
// Plain C++. Stateless apart from duplicate suppression at block edges and the held key.
#pragma once

#include "KeyRouter.h"
#include "MidiEvents.h"
#include "Transport.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace chordmonkey
{
    class TestPattern
    {
    public:
        static constexpr int velocity = 100;

        static int slotForBeat (int64_t beat) noexcept { return (int) (((beat % 7) + 7) % 7); }

        // Adds this block's key presses to `out` (in sample order). `on` is the parameter. For
        // one segment of a block (Clock.h), `t` holds the segment's start and `offset` its first
        // sample: events are stamped from it.
        template <class Out>
        void generate (bool on, const Transport& t, int numSamples, Out& out, int offset = 0) noexcept
        {
            const double spb = t.samplesPerBeat();
            // A segment that ends at a loop wrap stops short of the loop end in musical time (a
            // key there belongs after the wrap, Clock.h), and a segment starting just after the
            // wrap (by part of a sample) still has the key on the loop start.
            double endPpq = t.ppq + numSamples / spb;
            if (t.looping && t.loopEnd > t.loopStart && t.ppq < t.loopEnd && endPpq > t.loopEnd)
                endPpq = t.loopEnd;
            const double fromPpq = t.ppq - 1.0 / spb;
            const bool jumpedBack = t.ppq < lastEndPpq - 1.0e-6;

            if (heldKey >= 0 && (! on || ! t.playing || jumpedBack))
                releaseHeld (offset, out);

            if (! on || ! t.playing)
            {
                lastTick = std::numeric_limits<int64_t>::min();
                lastEndPpq = t.ppq;
                return;
            }

            if (jumpedBack)
                lastTick = std::numeric_limits<int64_t>::min();

            // A grid of 1/16 beats: tick 8h is half beat h (even = key down on the beat, odd = key
            // up an eighth later).
            for (auto k = (int64_t) std::ceil (fromPpq * ticksPerBeat - 8.0e-7); (double) k < endPpq * ticksPerBeat - 8.0e-7; ++k)
            {
                if (k <= lastTick)
                    continue;
                lastTick = k;
                if (k % 8 != 0)
                    continue;

                int sample = (int) std::llround (((double) k / ticksPerBeat - t.ppq) * spb);
                sample = offset + (sample < 0 ? 0 : (sample >= numSamples ? numSamples - 1 : sample));

                const int64_t h = k / 8;
                if (heldKey >= 0)
                    releaseHeld (sample, out);

                if ((h & 1) == 0)
                {
                    const int key = KeyRouter::keyForSlot (slotForBeat (h / 2));
                    if (key >= 0 && out.push (MidiEvent::noteOn (0, key, velocity, sample).withOrigin (MidiEvent::fromPalette)))
                        heldKey = key;
                }
            }
            lastEndPpq = endPpq;
        }

        // Key-up for the held key, e.g. when the engine is reset.
        template <class Out>
        void releaseHeld (int sample, Out& out) noexcept
        {
            if (heldKey >= 0 && out.push (MidiEvent::noteOff (0, heldKey, sample).withOrigin (MidiEvent::fromPalette)))
                heldKey = -1;
        }

        void reset() noexcept
        {
            heldKey = -1;
            lastTick = std::numeric_limits<int64_t>::min();
            lastEndPpq = 0.0;
        }

        int getHeldKey() const noexcept { return heldKey; }

    private:
        int heldKey = -1;
        static constexpr double ticksPerBeat = 16.0;
        int64_t lastTick = std::numeric_limits<int64_t>::min();
        double lastEndPpq = 0.0;
    };
}
