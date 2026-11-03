// Listens to the notes played into Detect's Capture (PLAN.md §3.6, Phase 8) and groups them into
// chords on the beat grid: what core/model/Detect.h turns into a pattern. Plain C++, no JUCE, no
// allocation: fixed arrays, called on the audio thread.
//
//   a chord     notes that start within 60 ms of the first (a chord played by hand, or a clip's
//               chord, starts together); a strummed or rolled chord also gathers notes that
//               keep coming (each within 120 ms of the one before) while every note so far is
//               held, up to half a second from the first
//   counted     three or more pitch classes, or a power chord (two, a fifth apart); a melody
//               note or a two-note interval played over a chord is ignored, so it neither makes
//               a chord nor ends one
//   repeated    the same pitch classes over the same bass as the chord before (a chord played
//               in a rhythm, a clip in a style) lengthen that chord instead of adding one
//   time        in beats of listening (the host's tempo, counted while the engine runs), so a
//               looping clip or a stopped transport counts on; a chord's start is its first
//               note's, its end the release of its last note
//
// Up to maxHeard chords are kept (later ones are counted as dropped).
#pragma once

#include <array>
#include <cstdint>

namespace chordmonkey
{
    struct HeardChord
    {
        double start = 0.0;               // beats of listening
        double end = -1.0;                // beats; -1 while a note of it is held
        uint16_t mask = 0;                // pitch classes (bit pc)
        int8_t lowest = -1;               // the lowest note (MIDI): the bass, and the register

        bool operator== (const HeardChord& o) const noexcept
        {
            return start == o.start && end == o.end && mask == o.mask && lowest == o.lowest;
        }
    };

    inline constexpr int maxHeard = 32;

    // What the listener has heard: the chords so far, oldest first.
    struct HeardChords
    {
        std::array<HeardChord, maxHeard> chords {};
        int count = 0;
        int dropped = 0;                  // chords after the first maxHeard
        double now = 0.0;                 // beats of listening when this was taken

        bool operator== (const HeardChords& o) const noexcept
        {
            if (count != o.count || dropped != o.dropped)
                return false;
            for (int i = 0; i < count; ++i)
                if (! (chords[(size_t) i] == o.chords[(size_t) i]))
                    return false;
            return true;                  // `now` alone doesn't make it news
        }
    };

    class ChordListener
    {
    public:
        static constexpr double groupSeconds = 0.06;      // notes this close start one chord
        static constexpr double rollGapSeconds = 0.12;    // a rolled chord's notes follow each other this closely
        static constexpr double rollSeconds = 0.5;        // …and gather this long

        // Forgets everything heard (a new capture); the empty result is news.
        void reset() noexcept
        {
            *this = ChordListener {};
            dirty = true;
        }

        void noteOn (int note, double seconds, double beat) noexcept
        {
            if (note < 0 || note > 127)
                return;
            advanceTo (beat);
            const bool joins = open && (seconds - groupSeconds0 <= groupSeconds
                                        || (heldInGroup == notesInGroup && seconds - lastOnSeconds <= rollGapSeconds
                                            && seconds - groupSeconds0 <= rollSeconds));
            if (! joins)
            {
                closeGroup();
                open = true;
                groupSeconds0 = seconds;
                groupBeat = beat;
                groupMask = 0;
                groupLowest = 127;
                notesInGroup = heldInGroup = 0;
                inGroup = {};
                groupEnd = -1.0;
            }
            if (! held[(size_t) note])
            {
                held[(size_t) note] = true;
                if (! inGroup[(size_t) note])
                {
                    inGroup[(size_t) note] = true;
                    ++notesInGroup;
                }
                ++heldInGroup;
            }
            lastOnSeconds = seconds;
            groupMask = (uint16_t) (groupMask | 1u << (note % 12));
            groupLowest = note < groupLowest ? note : groupLowest;
            dirty = true;
        }

        void noteOff (int note, double beat) noexcept
        {
            if (note < 0 || note > 127 || ! held[(size_t) note])
                return;
            advanceTo (beat);
            held[(size_t) note] = false;
            if (open && inGroup[(size_t) note] && --heldInGroup == 0)
            {
                groupEnd = beat;
                dirty = true;
            }
            if (inTail[(size_t) note])
            {
                inTail[(size_t) note] = false;
                if (--heldInTail == 0 && count > 0)
                {
                    chords[(size_t) count - 1].end = beat;   // the last chord, closed while held, ends
                    dirty = true;
                }
            }
        }

        // Every note released (transport stop, bypass, a reset).
        void allUp (double beat) noexcept
        {
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n])
                    noteOff (n, beat);
        }

        // The listening clock, for `now` (each block's end).
        void advanceTo (double beat) noexcept { nowBeat = beat > nowBeat ? beat : nowBeat; }

        // The chords heard so far, the one still forming included if it counts.
        void heard (HeardChords& out) const noexcept
        {
            out.count = count;
            out.dropped = dropped;
            out.now = nowBeat;
            for (int i = 0; i < count; ++i)
                out.chords[(size_t) i] = chords[(size_t) i];
            if (open)
                add (out.chords, out.count, out.dropped, current());
        }

        // A change since the last call (the plug-in publishes then).
        bool takeChanged() noexcept
        {
            const bool d = dirty;
            dirty = false;
            return d;
        }

    private:
        static int pcCount (unsigned m) noexcept
        {
            int n = 0;
            for (; m != 0; m &= m - 1)
                ++n;
            return n;
        }

        static bool counts (unsigned m) noexcept
        {
            const int n = pcCount (m);
            if (n >= 3)
                return true;
            if (n != 2)
                return false;
            for (int pc = 0; pc < 12; ++pc)       // a power chord: two pitch classes a fifth apart
                if (((m >> pc) & 1u) != 0 && ((m >> ((pc + 7) % 12)) & 1u) != 0)
                    return true;
            return false;
        }

        HeardChord current() const noexcept
        {
            HeardChord c;
            c.start = groupBeat;
            c.end = heldInGroup > 0 ? -1.0 : groupEnd;
            c.mask = groupMask;
            c.lowest = (int8_t) groupLowest;
            return c;
        }

        // Appends `c` if it counts (true); the same chord again lengthens the last instead.
        static bool add (std::array<HeardChord, maxHeard>& list, int& n, int& lost, const HeardChord& c) noexcept
        {
            if (! counts (c.mask))
                return false;
            if (n > 0)
            {
                auto& last = list[(size_t) n - 1];
                if (last.mask == c.mask && last.lowest % 12 == c.lowest % 12)
                {
                    last.end = c.end;
                    return true;
                }
            }
            if (n == maxHeard)
            {
                ++lost;
                return false;
            }
            list[(size_t) n++] = c;
            return true;
        }

        // The group becomes a chord (or lengthens the last); its notes still held end it later.
        void closeGroup() noexcept
        {
            if (open && add (chords, count, dropped, current()))
            {
                inTail = {};
                heldInTail = 0;
                for (int n = 0; n < 128; ++n)
                    if (inGroup[(size_t) n] && held[(size_t) n])
                    {
                        inTail[(size_t) n] = true;
                        ++heldInTail;
                    }
            }
            open = false;
        }

        std::array<HeardChord, maxHeard> chords {};
        int count = 0, dropped = 0;
        std::array<bool, 128> held {}, inGroup {}, inTail {};
        bool open = false, dirty = false;
        double groupSeconds0 = 0.0, lastOnSeconds = 0.0, groupBeat = 0.0, groupEnd = -1.0, nowBeat = 0.0;
        uint16_t groupMask = 0;
        int groupLowest = 127, notesInGroup = 0, heldInGroup = 0, heldInTail = 0;
    };
}
