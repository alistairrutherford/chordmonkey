// The MIDI engine: the key router (KeyRouter.h), the chords (Chord Group, Progression, Key,
// Scale, Chord Octave), the effects (ChordFx.h: Stack, Strum, Arp, Repeat, Step Sequencer,
// Velocity Algorithm, Hold, voice leading) on the Clock, the test pattern, and note bookkeeping
// through VoiceTracker.
// Plain C++, no JUCE, no allocation; the plug-in is a thin adapter around it, so every
// behaviour here is unit-testable.
//
//   white key     → the slot's chord: degree from the Progression, shape from the Chord Group,
//                   Key, Scale, Chord Octave, then the effect chain (one performance per key)
//   black key     → its borrowed chord (ChordGroups.h), the same way
//   Key octave    → Ignore: any octave plays the same; Follow: the key's octave from C at 48
//                   moves the chord (ChordFx.h)
//   Enable off    → input passes through unchanged
//   Analyse       → input passes through unchanged and the KeyListener guesses its key
//                   (ChordChimp's docs/KEY_DETECTION_PLAN.md); no chords, no test pattern
//
// Whenever the chords change while chord keys are held (a parameter), each held key whose chord
// changed is released and replayed. An arp takes the new chord at its next step.
//
// A block is processed in the Clock's segments (split at loop wraps). Within a segment, input
// and queued events run in sample order; at one sample, queued note-offs come first, then
// input, then queued note-ons.
#pragma once

#include "../theory/Progressions.h"
#include "ChordFx.h"
#include "Clock.h"
#include "KeyListener.h"
#include "KeyRouter.h"
#include "MidiEvents.h"
#include "TestPattern.h"
#include "Transport.h"
#include "VoiceTracker.h"
#include <array>
#include <atomic>
#include <cmath>

namespace chordmonkey
{
    struct EngineSettings
    {
        bool enabled = true;
        int key = 0;
        Scale scale = Scale::Major;
        ChordGroup group = ChordGroup::Triads;
        Progression progression = Progression::Diatonic;
        int chordOctave = 0;
        bool followKeyOctave = false;     // Key octave: Follow (Phase 8)
        bool testPattern = false;
        bool analysing = false;           // Analyse is Listen or Use guess (key detection)
        int outputChannel = 0;            // 0..15
        ChordFxSettings fx;
    };

    class ChordEngine
    {
    public:
        ChordEngine() { refreshChords (0, nullptr); }

        // Processes one block. `in` must be sorted by sample. Appends to `out` in sample order.
        void process (const EngineSettings& s, const Transport& t, const MidiEvent* in, int numIn, int numSamples,
                      BlockEvents& out) noexcept
        {
            numSamples = numSamples > 0 ? numSamples : 0;
            secondsPerSample = t.sampleRate > 0.0 && std::isfinite (t.sampleRate) ? 1.0 / t.sampleRate : 0.0;
            clock.beginBlock (t, numSamples);
            performer.setSegment (0);

            if (resetRequested.exchange (false))
                releaseAll (0, out);

            if (wasPlaying && ! t.playing)
                releaseAll (0, out);   // transport stop: all notes off (Chordism's PLAN.md §5)
            if (! wasPlaying && t.playing)
                performer.transportStarted();
            wasPlaying = t.playing;

            if (clock.jumpedAtStart())
                performer.transportJump (clock.jumpDelta(), 0, out);

            applySettings (s, out);

            int i = 0;
            for (int seg = 0; seg < clock.numSegments(); ++seg)
            {
                const ClockSegment g = clock.segment (seg);
                const bool last = seg == clock.numSegments() - 1;
                performer.setSegment (seg);
                if (seg > 0)
                    performer.transportJump (clock.wrapDelta (seg), g.start, out);   // a loop wrap inside the block

                Transport segmentTransport = t;
                segmentTransport.ppq = g.ppq;
                injected.clear();
                pattern.generate (s.enabled && s.testPattern && ! s.analysing, segmentTransport, g.end - g.start, injected, g.start);

                int j = 0, cursor = g.start, runs = 0;
                while (true)
                {
                    // The next key event of this segment: input before the test pattern's at one sample.
                    const bool haveIn = i < numIn && (last || clampSample (in[i].sample, numSamples) < g.end);
                    const bool haveInjected = j < injected.size();
                    const bool fromInput = haveIn && (! haveInjected || clampSample (in[i].sample, numSamples) <= injected[j].sample);
                    const MidiEvent* next = fromInput ? &in[i] : (haveInjected ? &injected[j] : nullptr);
                    const int keySample = next != nullptr ? clampSample (next->sample, numSamples) : g.end;

                    int at = 0;
                    uint8_t order = 0;
                    const bool due = runs < maxQueueRunsPerSegment
                                  && performer.peek (cursor, next != nullptr ? keySample + 1 : g.end, at, order);
                    if (due && (next == nullptr || at < keySample || order < ChordPerformer::inputOrder))
                    {
                        ++runs;
                        cursor = at;
                        performer.runNext (out);
                        continue;
                    }
                    if (next == nullptr)
                        break;
                    cursor = keySample > cursor ? keySample : cursor;
                    (fromInput ? i : j)++;
                    handle (*next, numSamples, out);
                }
            }

            seconds += numSamples * secondsPerSample;
            if (current.analysing)
                listener.advance (seconds);
        }

        // All notes off now (bypass, transport stop, all-notes-off, destruction).
        void releaseAll (int sample, BlockEvents& out) noexcept
        {
            performer.killAll (sample, out);
            voices.releaseAll (sample, out);
            held = {};
            listener.allUp (timeAt (sample));
            refreshChords (sample, nullptr);
        }

        // The next process() starts by releasing everything. For places where no MIDI can be
        // sent (releaseResources, prepareToPlay); callable from any thread.
        void requestReset() noexcept { resetRequested.store (true); }

        const VoiceTracker& getVoices() const noexcept { return voices; }
        const ChordSet& getChords() const noexcept { return chords; }
        const ChordContext& getChordContext() const noexcept { return context; }
        int getLastChordSlot() const noexcept { return lastChordSlot; }
        int getPatternKey() const noexcept { return pattern.getHeldKey(); }
        const ChordPerformer& getPerformer() const noexcept { return performer; }
        const Clock& getClock() const noexcept { return clock; }

        // The key heard in Analyse mode (the last analysis's, until the next one starts).
        KeyGuess getKeyGuess() const noexcept { return listener.guess(); }

        // For the editor: input key `note` is down; a performance of key `note` is playing, held
        // or not.
        bool isKeyDown (int note) const noexcept { return note >= 0 && note < 128 && held[(size_t) note].down; }
        bool isPerforming (int note) const noexcept { return performer.isAlive (note); }

    private:
        struct HeldKey
        {
            bool down = false;
            uint8_t velocity = 0;
            uint8_t channel = 0;
            int8_t slot = -1;             // the chord slot (0..11)
            bool passThrough = false;     // pressed while Enable was off
        };

        // Recomputes the chords. With `out`, every performance whose chord changed is replayed at
        // `sample` (ChordPerformer::retune); `changedOut` gets the slots whose chord changed.
        void refreshChords (int sample, BlockEvents* out, std::array<bool, numChordSlots>* changedOut = nullptr) noexcept
        {
            const ChordContext ctx { current.key, current.scale, current.chordOctave };
            const ChordSet next = makeChords (current.group, current.scale, progressionDegrees (current.progression));

            const bool ctxChanged = ctx.key != context.key || ctx.scale != context.scale || ctx.outputOctave != context.outputOctave;
            std::array<bool, numChordSlots> changed {};
            bool any = false;
            for (size_t n = 0; n < changed.size(); ++n)
            {
                changed[n] = ctxChanged || ! (next[n] == chords[n]);
                any |= changed[n];
            }
            chords = next;
            context = ctx;
            if (changedOut != nullptr)
                *changedOut = changed;

            if (out != nullptr && any)
                performer.retune (changed, sample, *out);
        }

        void applySettings (const EngineSettings& s, BlockEvents& out) noexcept
        {
            const EngineSettings old = current;
            current = s;
            performer.setFollowKeyOctave (s.followKeyOctave);

            if (s.enabled != old.enabled || s.analysing != old.analysing)
            {
                releaseAll (0, out);
                if (s.analysing && ! old.analysing)
                    listener.reset();     // each analysis starts afresh
                performer.applySettings (s.fx, s.outputChannel, {}, 0, out);
                return;
            }

            // Held chords follow parameter changes, and the effects follow theirs. A Key octave
            // change moves every held chord.
            std::array<bool, numChordSlots> changed {};
            refreshChords (0, nullptr, &changed);
            if (s.followKeyOctave != old.followKeyOctave)
                changed.fill (true);
            performer.applySettings (s.fx, s.outputChannel, changed, 0, out);
        }

        static int clampSample (int sample, int numSamples) noexcept
        {
            return sample < 0 ? 0 : (sample >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : sample);
        }

        // Seconds since the engine started, at `sample` of this block (the KeyListener's clock).
        double timeAt (int sample) const noexcept { return seconds + sample * secondsPerSample; }

        void handle (const MidiEvent& e, int numSamples, BlockEvents& out) noexcept
        {
            const int sample = clampSample (e.sample, numSamples);

            if (e.isNoteOn())
            {
                const int n = e.note();
                auto& h = held[(size_t) n];
                if (h.down)
                {
                    performer.end (n, sample, out);   // retrigger: a held key ends, even with Hold
                    keyUp (n, sample, out);
                }

                h.down = true;
                h.velocity = (uint8_t) e.velocity();
                h.channel = (uint8_t) e.channel();
                h.passThrough = ! current.enabled || current.analysing;
                h.slot = (int8_t) KeyRouter::slotOf (n);

                if (h.passThrough)
                {
                    voices.start ((VoiceTracker::Source) n, h.channel, n, h.velocity, sample, out);
                    if (current.analysing)
                        listener.noteOn (n, h.velocity, timeAt (sample));
                }
                else if (h.slot >= 0)
                {
                    lastChordSlot = h.slot;
                    performer.press (n, h.slot, h.velocity, sample, out);
                }
            }
            else if (e.isNoteOff())
            {
                const int n = e.note();
                if (held[(size_t) n].down)
                    keyUp (n, sample, out);
            }
            else if (e.isAllNotesOff())
            {
                releaseAll (sample, out);
                out.push ({ sample, { e.data[0], e.data[1], e.data[2] }, e.size });
            }
            else
            {
                out.push ({ sample, { e.data[0], e.data[1], e.data[2] }, e.size });
            }
        }

        void keyUp (int n, int sample, BlockEvents& out) noexcept
        {
            auto& h = held[(size_t) n];
            h.down = false;
            if (h.passThrough)
            {
                voices.release ((VoiceTracker::Source) n, sample, out);
                if (current.analysing)
                    listener.noteOff (n, timeAt (sample));
            }
            else if (h.slot >= 0)
                performer.release (n, sample, out);   // ends, or is held (Hold)
        }

        static constexpr int maxQueueRunsPerSegment = 1 << 16;

        EngineSettings current;
        VoiceTracker voices;
        Clock clock;
        TestPattern pattern;
        ChordSet chords {};
        ChordContext context;
        std::array<HeldKey, 128> held {};
        EventBuffer<256> injected;
        int lastChordSlot = -1;
        bool wasPlaying = false;
        KeyListener listener;
        double seconds = 0.0, secondsPerSample = 1.0 / 44100.0;
        std::atomic<bool> resetRequested { false };
        ChordPerformer performer { voices, clock, chords, context };
    };
}
