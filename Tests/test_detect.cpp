// Detect tests (PLAN.md §3.6, §7; Phase 8, G8): chords played into Capture become a pattern.
// Recognition of a chord over its bass (inversions, extensions, altered and slash chords, in the
// register played); grouping through the engine under a fake host (sloppy onsets and releases,
// rolled chords, a melody over the chords, a chord repeated in a rhythm, rests, a looping clip
// and a stopped transport); lengths on the half-beat grid; long chords and long captures; the
// input passing through untouched; the key guessed meanwhile; each capture starting afresh.
//
// Expected notes are written out by hand (MIDI 48 = C2).
#include "FakeHost.h"
#include "core/model/ChordSets.h"
#include "core/model/Detect.h"
#include <set>

using namespace fakehost;
using testutil::check;

namespace
{
    std::string join (const std::vector<std::string>& v)
    {
        std::string s;
        for (const auto& x : v)
            s += (s.empty() ? "" : " ") + x;
        return s;
    }

    // "Name:beats" for each slot of `p` (beats as written by the pattern text: 4, 2, 0.5…).
    std::string describe (const Pattern& p, int key = 0, Scale scale = Scale::Major)
    {
        std::vector<std::string> out;
        for (int i = 0; i < p.size(); ++i)
        {
            const auto& s = p.slots[(size_t) i];
            const double beats = s.halfBeats * 0.5;
            std::string b = beats == std::floor (beats) ? std::to_string ((int) beats) : std::to_string (beats).substr (0, 3);
            out.push_back (namePatternSlot (s, key, scale).text() + ":" + b);
        }
        return join (out);
    }

    unsigned maskOf (const std::vector<int>& notes)
    {
        unsigned m = 0;
        for (int n : notes)
            m |= 1u << (n % 12);
        return m;
    }

    // Plays `chords` (notes, start beat, length in beats) into Capture and returns the pattern.
    struct Played { std::vector<int> notes; double start, length; double spreadMs = 0.0; double early = 0.0; };

    Pattern capture (const std::vector<Played>& chords, double bpm = 120.0, int block = 256, bool playing = true,
                     std::vector<std::pair<double, int>> melody = {}, KeyGuess* guess = nullptr, HeardChords* heardOut = nullptr)
    {
        Host h (48000.0, bpm);
        h.playing = playing;
        h.settings.capturing = true;
        double end = 0.0;
        for (const auto& c : chords)
        {
            for (size_t i = 0; i < c.notes.size(); ++i)
            {
                const int64_t spread = (int64_t) std::llround (c.spreadMs * 0.001 * 48000.0 * (double) i / (double) std::max<size_t> (1, c.notes.size() - 1));
                h.keyAt (h.at (c.start) + spread, c.notes[i], true, 90);
                h.keyAt (h.at (c.start + c.length - c.early), c.notes[i], false);
            }
            end = std::max (end, c.start + c.length);
        }
        for (const auto& m : melody)
        {
            h.key (m.first, m.second, true, 70);
            h.key (m.first + 0.4, m.second, false);
        }
        h.run (h.at (end + 1.0), block);
        HeardChords heard;
        h.engine->getHeard (heard);
        if (guess != nullptr)
            *guess = h.engine->getKeyGuess();
        if (heardOut != nullptr)
            *heardOut = heard;
        return patternFromHeard (heard, 0, Scale::Major);
    }
}

static void testRecognition()
{
    std::cout << "== recognition ==\n";
    struct Case { std::vector<int> notes; const char* name; int inversion; };
    const std::vector<Case> cases {
        { { 48, 52, 55 }, "C", 0 },
        { { 52, 55, 60 }, "C/E", 1 },
        { { 43, 48, 52 }, "C/G", 2 },
        { { 57, 60, 64 }, "Am", 0 },
        { { 47, 50, 53, 55 }, "G7/B", 1 },
        { { 41, 50, 53, 55, 59 }, "G7/F", 3 },
        { { 50, 53, 56, 60 }, "Dm7b5", 0 },
        { { 46, 50, 53, 56, 60 }, "Bb9", 0 },
        { { 48, 52, 58, 62, 69 }, "C13", 0 },
        { { 43, 47, 53, 56 }, "G7b9", 0 },
        { { 48, 52, 55, 59, 62 }, "Cmaj9", 0 },
        { { 45, 52, 55, 60, 62, 67 }, "Am11", 0 },
        { { 50, 55, 57, 60 }, "D7sus4", 0 },
        { { 40, 48, 55, 59 }, "Cmaj7/E", 1 },
        { { 38, 48, 52, 55 }, "D9sus4", 0 },     // "C/D": a triad over a bass outside it reads as the chord it makes
        { { 38, 54, 57, 62 }, "D", 0 },
        { { 42, 50, 57 }, "D/F#", 1 },
        { { 43, 50 }, "G5", 0 },
    };
    int ok = 0;
    std::string first;
    for (const auto& c : cases)
    {
        int lowest = 127;
        for (int n : c.notes) lowest = std::min (lowest, n);
        const PatternSlot s = slotForHeard (maskOf (c.notes), lowest, 0, Scale::Major, 8);
        const ChordNotes notes = slotNotes (s);
        unsigned got = 0;
        for (int i = 0; i < notes.size; ++i)
            got |= 1u << (notes.notes[(size_t) i] % 12);
        const std::string name = namePatternSlot (s, 0, Scale::Major).text();
        const bool good = name == c.name && got == maskOf (c.notes) && notes.size > 0 && notes.notes[0] % 12 == lowest % 12
                       && std::abs ((int) notes.notes[0] - lowest) <= 6 && (c.inversion == 0 || s.inversion == c.inversion);
        if (good)
            ++ok;
        else if (first.empty())
            first = std::string (c.name) + " read as " + name + " (inversion " + std::to_string (s.inversion) + ", bass "
                  + std::to_string (notes.size > 0 ? notes.notes[0] : -1) + ")";
    }
    check (ok == (int) cases.size(), std::to_string (ok) + " of " + std::to_string (cases.size())
                                         + " voicings recognised: name, the same pitch classes, the bass, and the register played"
                                         + (first.empty() ? "" : " (first failure: " + first + ")"));

    // Every chord type, on every root, in every inversion the slot can play: read back as the
    // same pitch classes over the same bass.
    int same = 0, total = 0;
    std::string miss;
    for (int type = 0; type < numChordTypes; ++type)
        for (int root = 0; root < 12; ++root)
        {
            const ChordTones t = chordTypeTones (type);
            std::array<int, maxWorkspaceTones> tones {};
            for (int i = 0; i < t.size; ++i)
                tones[(size_t) i] = t.tones[(size_t) i];
            for (int inv = 0; inv <= std::min (maxSlotInversion, t.size - 1); ++inv)
            {
                const PatternSlot played = makePatternSlot (makeWorkspaceChord (root, tones.data(), t.size), 8, inv, 0);
                const ChordNotes n = slotNotes (played);
                std::vector<int> v (n.begin(), n.begin() + n.size);
                const PatternSlot back = slotForHeard (maskOf (v), v[0], 0, Scale::Major, 8);
                const ChordNotes m = slotNotes (back);
                std::vector<int> w (m.begin(), m.begin() + m.size);
                ++total;
                if (maskOf (w) == maskOf (v) && w[0] % 12 == v[0] % 12 && std::abs (w[0] - v[0]) <= 6)
                    ++same;
                else if (miss.empty())
                    miss = chordType (type).name + std::string (" on ") + std::to_string (root) + " inversion " + std::to_string (inv);
            }
        }
    check (same == total, std::to_string (same) + " of " + std::to_string (total) + " chords (27 types × 12 roots × their inversions) come back as"
                              " the same pitch classes over the same bass, in the register played" + (miss.empty() ? "" : " (first failure: " + miss + ")"));
}

static void testGrouping()
{
    std::cout << "== grouping through the engine ==\n";
    const std::vector<int> C { 48, 52, 55 }, G { 47, 50, 55 }, Am { 45, 48, 52 }, F { 45, 48, 53 };
    {
        const auto p = capture ({ { C, 0, 4 }, { G, 4, 4 }, { Am, 8, 4 }, { F, 12, 4 } });
        check (describe (p) == "C:4 G/B:4 Am:4 F/A:4", "I V vi IV played exactly: " + describe (p));
    }
    {
        // By hand: onsets spread over 40 ms, some late by up to a 16th's worth of slop, releases
        // early; a melody on top that neither makes nor ends a chord.
        const auto p = capture ({ { C, 0.0, 4, 40, 0.3 }, { G, 4.06, 3.9, 35, 0.2 }, { Am, 7.97, 4, 25, 0.4 }, { F, 12.05, 4, 40, 0.1 } },
                               120.0, 333, true, { { 1.0, 72 }, { 2.0, 74 }, { 5.5, 71 }, { 9.0, 76 }, { 13.5, 77 } });
        check (describe (p) == "C:4 G/B:4 Am:4 F/A:4", "played by hand (spread onsets, early releases, a melody on top): " + describe (p));
    }
    {
        // A rolled chord: 300 ms bottom to top, each note held.
        const auto p = capture ({ { { 36, 43, 52, 55, 60 }, 0, 4, 300 }, { { 41, 48, 57, 60, 65 }, 4, 4, 280 } });
        check (describe (p) == "C:4 F:4", "rolled chords (300 ms, every note held) are one chord each: " + describe (p));
    }
    {
        // A chord in a rhythm (Charleston), as from a clip in a style: one slot per chord.
        std::vector<Played> hits;
        const std::vector<std::vector<int>> chords { { 50, 53, 56, 60 }, { 43, 47, 50, 53 }, { 48, 51, 55, 58 } };
        const double lengths[] = { 4, 4, 8 };
        double at = 0;
        for (int c = 0; c < 3; ++c)
        {
            for (double bar = 0; bar < lengths[c]; bar += 4)
                for (auto hit : { std::pair<double, double> { 0, 0.75 }, { 1.5, 0.5 }, { 2, 0.75 }, { 3.5, 0.5 } })
                    hits.push_back ({ chords[(size_t) c], at + bar + hit.first, hit.second });
            at += lengths[c];
        }
        const auto p = capture (hits);
        check (describe (p) == "Dm7b5:4 G7:4 Cm7:8", "a chord repeated in a rhythm lengthens its slot (Minor Two-Five in Charleston): " + describe (p));
    }
    {
        // Rests: a chord lasts to the next onset; the last to its release, on the half-beat grid.
        const auto p = capture ({ { C, 0, 1.5 }, { F, 3, 0.9 }, { G, 4, 1.4 } });
        check (describe (p) == "C:3 F/A:1 G/B:2", "rests count in the chord before; the last lasts to its release, up to a whole beat: " + describe (p));
    }
    {
        // A capture that starts mid-bar starts the pattern at its first chord.
        const auto p = capture ({ { C, 2.5, 2 }, { G, 4.5, 2 } });
        check (describe (p) == "C:2 G/B:2", "the pattern starts at the first chord heard: " + describe (p));
    }
    {
        // A chord of 12 beats takes two slots (8 + 4); nine chords fill the pattern's eight.
        const auto p = capture ({ { C, 0, 12 }, { G, 12, 4 } });
        check (describe (p) == "C:8 C:4 G/B:4", "a chord longer than 8 beats takes more slots: " + describe (p));
        std::vector<Played> many;
        const std::vector<std::vector<int>> cycle { C, G, Am, F };
        for (int i = 0; i < 10; ++i)
            many.push_back ({ cycle[(size_t) i % 4], i * 2.0, 2 });
        const auto q = capture (many);
        check (q.size() == numPatternSlots && describe (q) == "C:2 G/B:2 Am:2 F/A:2 C:2 G/B:2 Am:2 F/A:2", "ten chords: the first eight fill the pattern: " + describe (q));
    }
    {
        // Other tempos and block sizes, and a stopped transport (the keyboard, no playback).
        bool all = true;
        std::string what;
        for (double bpm : { 72.0, 97.0, 174.0 })
            for (int block : { 32, 512, 2048 })
                for (bool playing : { true, false })
                {
                    const auto p = capture ({ { C, 0, 2 }, { G, 2, 2 }, { Am, 4, 4 }, { F, 8, 1 } }, bpm, block, playing);
                    if (describe (p) != "C:2 G/B:2 Am:4 F/A:1")
                    {
                        all = false;
                        what = std::to_string ((int) bpm) + " BPM/" + std::to_string (block) + (playing ? "" : " stopped") + ": " + describe (p);
                    }
                }
        check (all, "the same at 72, 97 and 174 BPM, 32-2048-sample blocks, playing or stopped" + (what.empty() ? "" : " (" + what + ")"));
    }
}

static void testLoopingClip()
{
    std::cout << "== a looping clip ==\n";
    // The clip loops (the host's ppq wraps at 8 beats) and plays twice: the chords repeat,
    // counted on; the first pass's chords are the pattern's first.
    Host h (48000.0, 120.0);
    h.looping = true;
    h.loopStart = 0.0;
    h.loopEnd = 8.0;
    h.settings.capturing = true;
    const std::vector<std::pair<double, std::vector<int>>> clip { { 0, { 48, 52, 55 } }, { 2, { 43, 47, 50 } }, { 4, { 45, 48, 52 } }, { 6, { 41, 45, 48 } } };
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : clip)
            for (int n : c.second)
            {
                h.key (pass * 8 + c.first, n, true);
                h.key (pass * 8 + c.first + 1.9, n, false);
            }
    h.run (h.at (16.5), 512);
    HeardChords heard;
    h.engine->getHeard (heard);
    const auto p = patternFromHeard (heard, 0, Scale::Major);
    check (heard.count == 8 && describe (p) == "C:2 G:2 Am:2 F:2 C:2 G:2 Am:2 F:2", "two passes of a looping 2-bar clip: 8 chords, " + describe (p));
}

static void testPassThroughAndKey()
{
    std::cout << "== the input passes through; the key is guessed ==\n";
    Host h (48000.0, 120.0);
    h.settings.capturing = true;
    h.settings.play = true;                  // Play is held off while capturing
    Workspace ws;
    std::string error;
    parseProgression ("I V vi IV", 0, ws.patterns[0], error);
    h.engine->setWorkspace (ws);
    const std::vector<std::vector<int>> chords { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };   // Am F C G
    std::multiset<std::pair<int64_t, int>> in, out;
    for (int bar = 0; bar < 8; ++bar)
        for (int n : chords[(size_t) bar % 4])
        {
            h.key (bar * 4.0, n, true);
            h.key (bar * 4.0 + 3.5, n, false);
            in.insert ({ h.at (bar * 4.0), n });
        }
    h.run (h.at (33), 256);
    for (const auto& hit : h.hits)
        if (hit.on)
            out.insert ({ hit.sample, hit.note });
    check (in == out, "every note passes through as played, and nothing else plays (not the pattern): "
                          + std::to_string (out.size()) + " notes for " + std::to_string (in.size()));
    const KeyGuess g = h.engine->getKeyGuess();
    HeardChords heard;
    h.engine->getHeard (heard);
    const auto p = patternFromHeard (heard, g.tonic, g.minor ? Scale::NaturalMinor : Scale::Major);
    check (g.has() && (g.tonic == 9 || g.tonic == 0) && describe (p, g.tonic, g.minor ? Scale::NaturalMinor : Scale::Major).rfind ("Am:4 F:4 C:4 G:4", 0) == 0,
           "vi IV I V: the key guessed meanwhile (" + std::string (g.has() ? guessKeyName (g.tonic, g.minor) : "none") + (g.minor ? " minor" : " major")
               + ") and the chords: " + describe (p, g.tonic, g.minor ? Scale::NaturalMinor : Scale::Major));

    // Capture off and on again: a new capture starts empty.
    h.settings.capturing = false;
    h.run (h.at (34), 256);
    h.settings.capturing = true;
    h.run (h.at (35), 256);
    HeardChords fresh;
    h.engine->getHeard (fresh);
    check (fresh.count == 0, "each capture starts afresh");

    // The plug-in's request (a result used, a capture cancelled) empties it without Capture
    // going off in between.
    for (int n : { 48, 52, 55 })
    {
        h.key (35.5, n, true);
        h.key (37, n, false);
    }
    h.run (h.at (38), 256);
    HeardChords one, emptied;
    h.engine->getHeard (one);
    h.engine->requestHeardReset();
    h.run (h.at (38.5), 256);
    h.engine->getHeard (emptied);
    check (one.count == 1 && emptied.count == 0 && h.engine->takeHeardChanged(), "a reset request empties the capture, and that's news to publish");
}

int main()
{
    testRecognition();
    testGrouping();
    testLoopingClip();
    testPassThroughAndKey();
    return testutil::finish ("test_detect");
}
