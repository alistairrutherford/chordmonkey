// Pattern playback tests (PLAN.md §3.2, §7; Phase 2, G4): the PatternPlayer's slot lines, and the
// engine playing the workspace in time under a fake transport (FakeHost.h) at 32-2048-sample
// blocks, 44.1-96 kHz and three tempos: every chord on its beat (within a sample), loops,
// locates, a pattern switch at the next bar, Play on and off, edits and Chord Octave while a
// chord sounds, the effects on the pattern, and a stuck-note fuzz with all of it at once.
//
// Expected notes are written out by hand (MIDI 48 = C2, the chords' default C).
#include "FakeHost.h"
#include "core/model/ChordSets.h"
#include <random>

using namespace fakehost;
using testutil::check;

namespace
{
    Pattern progression (const std::string& text, int key = 0)
    {
        Pattern p;
        std::string error;
        if (! parseProgression (text, key, p, error))
            std::cout << "  (bad progression \"" << text << "\": " << error << ")\n";
        return p;
    }

    using Notes = std::vector<int>;
    const Notes C { 48, 52, 55 }, G { 55, 59, 62 }, Am { 57, 60, 64 }, F { 53, 57, 60 };
    const Notes Dm7 { 50, 53, 57, 60 }, G7 { 55, 59, 62, 65 };

    void chord (Expect& e, double from, double to, const Notes& notes)
    {
        for (int n : notes)
            e.note (from, to, n);
    }

    // A host playing `patterns` (A, B, …) with Play on.
    std::unique_ptr<Host> playing (const Config& c, std::initializer_list<const char*> patterns)
    {
        auto h = std::make_unique<Host> (c.rate, c.bpm);
        Workspace ws;
        int i = 0;
        for (const char* text : patterns)
            ws.patterns[(size_t) i++] = progression (text);
        h->engine->setWorkspace (ws);
        h->settings.play = true;
        return h;
    }

    // Stops the host and expects every note still sounding to end there.
    void stopAndEnd (Host& h, Expect& e, std::initializer_list<Notes> sounding, int block)
    {
        const int64_t at = h.pos;
        h.stop (block);
        for (const auto& notes : sounding)
            for (int n : notes)
                e.offAt (at, n);
    }
}

static void testSlotLines()
{
    std::cout << "== slot lines ==\n";
    const Pattern p = progression ("I:2 V:1 vi:0.5 IV:4.5");   // 8 beats
    check (PatternPlayer::slotAt (p, 0.0) == 0 && PatternPlayer::slotAt (p, 1.99) == 0 && PatternPlayer::slotAt (p, 2.0) == 1
               && PatternPlayer::slotAt (p, 3.0) == 2 && PatternPlayer::slotAt (p, 3.5) == 3 && PatternPlayer::slotAt (p, 7.99) == 3,
           "slotAt: lines at 2, 3, 3.5 beats");
    check (PatternPlayer::slotAt (p, 8.0) == 0 && PatternPlayer::slotAt (p, 10.5) == 1 && PatternPlayer::slotAt (p, -0.5) == 3
               && PatternPlayer::slotAt (p, -6.0) == 1,
           "the pattern loops every 8 beats, before 0 too");
    check (PatternPlayer::slotAt (p, 2.0 - 1.0e-9) == 1, "a hair before a line counts as on it");
    check (PatternPlayer::nextSlotLine (p, 0.0) == 2.0 && PatternPlayer::nextSlotLine (p, 2.0) == 3.0 && PatternPlayer::nextSlotLine (p, 3.6) == 8.0
               && PatternPlayer::nextSlotLine (p, 7.9) == 8.0 && PatternPlayer::nextSlotLine (p, 8.0) == 10.0 && PatternPlayer::nextSlotLine (p, -1.0) == 0.0,
           "nextSlotLine: the next line strictly after");
    check (PatternPlayer::slotAt (Pattern {}, 3.0) == -1 && PatternPlayer::nextSlotLine (Pattern {}, 3.0) == HUGE_VAL, "an empty pattern has no slots or lines");
}

static void testPlayback()
{
    std::cout << "== playback ==\n";
    sweep ("I V vi IV, a bar each, from the top: every chord on its bar line, ends at the stop", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV" });
        h->run (h->at (19.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 4, 8, G);
        chord (e, 8, 12, Am);
        chord (e, 12, 16, F);
        for (int n : C)
            e.on (16, n);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("I:2 V:1 vi:0.5 IV:4.5: uneven lengths, twice round", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I:2 V:1 vi:0.5 IV:4.5" });
        h->run (h->at (15.75), c.block);
        Expect e (h->spq());
        for (double base : { 0.0, 8.0 })
        {
            chord (e, base, base + 2, C);
            chord (e, base + 2, base + 3, G);
            chord (e, base + 3, base + 3.5, Am);
        }
        chord (e, 3.5, 8, F);
        for (int n : F)
            e.on (11.5, n);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("one chord, one beat: restruck every beat", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I:1" });
        h->run (h->at (3.5), c.block);
        Expect e (h->spq());
        for (int b = 0; b < 3; ++b)
            chord (e, b, b + 1, C);
        for (int n : C)
            e.on (3, n);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("starting mid-slot (ppq 6): the slot under the playhead plays at once", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV" });
        h->ppq = 6.0;
        h->run (h->at (6.5), c.block);   // to ppq 12.5
        Expect e ([&] (double beats) { return (beats - 6.0) * h->spq(); });
        chord (e, 6, 8, G);
        chord (e, 8, 12, Am);
        for (int n : F)
            e.on (12, n);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });
}

static void testLoopsAndJumps()
{
    std::cout << "== loops and locates ==\n";
    for (auto style : { LoopStyle::Wrap, LoopStyle::Split })
        sweep (std::string ("a host loop of beats 2-6 over I V vi IV (") + (style == LoopStyle::Wrap ? "blocks across the wrap" : "split at the loop end")
                   + "): the chord at the loop start restrikes on every wrap",
               [style] (const Config& c, int64_t& worst)
        {
            auto h = playing (c, { "I V vi IV" });
            h->looping = true;
            h->loopStart = 2.0;
            h->loopEnd = 6.0;
            h->style = style;
            h->run (h->at (13.5), c.block);   // song time: 0-6, then 2-6, then 2-5.5
            Expect e (h->spq());
            chord (e, 0, 4, C);
            chord (e, 4, 6, G);
            chord (e, 6, 8, C);      // wrap to 2
            chord (e, 8, 10, G);
            chord (e, 10, 12, C);    // wrap to 2
            for (int n : G)
                e.on (12, n);
            stopAndEnd (*h, e, { G }, c.block);
            return finish (*h, e, worst);
        });

    sweep ("a locate from beat 6 to 13: IV restrikes at the jump, then I at 16", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV" });
        h->run (h->at (6.0), c.block);
        const int64_t jump = h->pos;
        h->ppq = 13.0;
        h->run (jump + h->at (5.0), c.block);   // to ppq 18
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (jump, n);
        }
        const double j = (double) jump / h->spq();
        for (int n : F)
        {
            e.onAt (jump, n);
            e.off (j + 3.0, n);
        }
        for (int n : C)
            e.on (j + 3.0, n);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });
}

static void testSwitching()
{
    std::cout << "== switching pattern ==\n";
    sweep ("A to B at beat 5: B starts on the next bar line (8), where its loop is", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV", "ii7:2 V7:2" });
        h->run (h->at (5.0), c.block);
        h->settings.pattern = 1;
        h->run (h->at (13.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 4, 8, G);
        chord (e, 8, 10, Dm7);
        chord (e, 10, 12, G7);
        for (int n : Dm7)
            e.on (12, n);
        stopAndEnd (*h, e, { Dm7 }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("A to B and back before the bar: no switch", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV", "ii7:2 V7:2" });
        h->run (h->at (5.0), c.block);
        h->settings.pattern = 1;
        h->run (h->at (6.0), c.block);
        h->settings.pattern = 0;
        h->run (h->at (10.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 4, 8, G);
        for (int n : Am)
            e.on (8, n);
        stopAndEnd (*h, e, { Am }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("a switch waiting for the bar happens at a loop wrap instead (a 2-beat loop never reaches it)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV", "ii7:2 V7:2" });
        h->looping = true;
        h->loopStart = 0.0;
        h->loopEnd = 2.0;
        h->run (h->at (1.0), c.block);
        h->settings.pattern = 1;
        h->run (h->at (4.25), c.block);  // wraps at song time 2 and 4: B from its start
        Expect e (h->spq());
        chord (e, 0, 2, C);
        chord (e, 2, 4, Dm7);
        for (int n : Dm7)
            e.on (4, n);
        stopAndEnd (*h, e, { Dm7 }, c.block);
        return finish (*h, e, worst);
    });

    {
        Host h (48000.0, 120.0);
        Workspace ws;
        ws.patterns[0] = progression ("I V vi IV");
        ws.patterns[2] = progression ("vi");
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.settings.pattern = 2;
        h.playing = false;
        h.run (h.at (1.0), 256);
        h.playing = true;
        h.run (h.pos + h.at (1.0), 256);
        check (h.engine->getPlayingPattern() == 2 && h.engine->getPlayingSlot() == 0 && h.hits.size() == 3 && h.hits[0].note == 57,
               "a switch while stopped is immediate: playback starts with C's vi");
    }
}

static void testOnOffAndEdits()
{
    std::cout << "== Play, Enable and edits ==\n";
    sweep ("Play off mid-slot ends the chord; on again plays the slot under the playhead", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV" });
        h->run (h->at (5.0), c.block);
        const int64_t off = h->pos;
        h->settings.play = false;
        h->run (h->at (9.0), c.block);
        const int64_t on = h->pos;
        h->settings.play = true;
        h->run (h->at (12.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (off, n);
        }
        for (int n : Am)
        {
            e.onAt (on, n);
            e.off (12, n);
        }
        for (int n : F)
            e.on (12, n);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Enable off and Analyse stop the pattern; the test pattern can play with it", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I:8" });
        h->run (h->at (2.0), c.block);
        const int64_t off = h->pos;
        h->settings.enabled = false;
        h->run (h->at (3.0), c.block);
        h->settings.enabled = true;
        h->settings.analysing = true;
        h->run (h->at (4.0), c.block);
        Expect e (h->spq());
        for (int n : C)
        {
            e.on (0, n);
            e.offAt (off, n);
        }
        stopAndEnd (*h, e, {}, c.block);
        return finish (*h, e, worst);
    });

    sweep ("an edit to the sounding slot retunes it at the next block; an edit elsewhere changes nothing yet", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV" });
        h->run (h->at (5.0), c.block);
        const int64_t edit = h->pos;
        Workspace ws = h->engine->getWorkspace();
        ws.patterns[0].slots[1].chord = progression ("V7").slots[0].chord;   // G → G7 while G sounds
        ws.patterns[0].slots[3].octave = 1;                                   // F an octave up, later
        h->engine->setWorkspace (ws);
        h->run (h->at (14.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (edit, n);
        }
        for (int n : G7)
        {
            e.onAt (edit, n);
            e.off (8, n);
        }
        chord (e, 8, 12, Am);
        for (int n : F)
            e.on (12, n + 12);
        stopAndEnd (*h, e, { { 65, 69, 72 } }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("emptying the pattern while it plays ends the chord; refilling it plays again", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V vi IV" });
        h->run (h->at (2.0), c.block);
        const int64_t emptied = h->pos;
        h->engine->setWorkspace (Workspace {});
        h->run (h->at (6.0), c.block);
        const int64_t refilled = h->pos;
        Workspace ws;
        ws.patterns[0] = progression ("vi:8");
        h->engine->setWorkspace (ws);
        h->run (h->at (7.75), c.block);
        Expect e (h->spq());
        for (int n : C)
        {
            e.on (0, n);
            e.offAt (emptied, n);
        }
        for (int n : Am)
            e.onAt (refilled, n);
        stopAndEnd (*h, e, { Am }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Chord Octave +1 while a pattern chord sounds moves it", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I:8" });
        h->run (h->at (3.0), c.block);
        const int64_t moved = h->pos;
        h->settings.chordOctave = 1;
        h->run (h->at (6.0), c.block);
        Expect e (h->spq());
        for (int n : C)
        {
            e.on (0, n);
            e.offAt (moved, n);
            e.onAt (moved, n + 12);
        }
        stopAndEnd (*h, e, { { 60, 64, 67 } }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("a key played over the pattern sounds with it; the pattern goes on", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V" });
        h->key (1.0, 69, true);    // A: vi in C (Am, 57 60 64)
        h->key (2.0, 69, false);
        h->run (h->at (7.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 1, 2, Am);
        for (int n : G)
            e.on (4, n);
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });
}

static void testEffects()
{
    std::cout << "== effects on the pattern ==\n";
    sweep ("Strum Up 1/32 strums each pattern chord", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I V" });
        h->settings.fx.strum = StrumMode::Up;
        h->settings.fx.strumRate = 4;   // 1/32
        h->run (h->at (7.5), c.block);
        Expect e (h->spq());
        for (int i = 0; i < 3; ++i)
        {
            e.note (0.0 + 0.125 * i, 4.0, C[(size_t) i]);
            e.on (4.0 + 0.125 * i, G[(size_t) i]);
        }
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Repeat 1/4 (gate 50%) restrikes the pattern chord every beat", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, { "I:2 V:2" });
        h->settings.fx.repeat.on = true;
        h->settings.fx.repeat.rate = 1;   // 1/4
        h->settings.fx.repeat.gate = 0.5;
        h->run (h->at (3.75), c.block);
        Expect e (h->spq());
        for (int b = 0; b < 4; ++b)
            chord (e, b, b + 0.5, b < 2 ? C : G);
        stopAndEnd (*h, e, {}, c.block);
        return finish (*h, e, worst);
    });
}

static void testFuzz()
{
    std::cout << "== stuck-note fuzz ==\n";
    std::mt19937 rng (2026);
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    const char* progs[] = { "I V vi IV", "ii7:2 V7:2 Imaj7", "I:0.5 IV:0.5 V:1", "i bVI bIII bVII", "vi:8", "I", "V(0.7.16):1.5 ii9:2.5" };

    int runs = 0, dirty = 0, played = 0, patternWhileStopped = 0;
    std::string first;
    for (int r = 0; r < 60; ++r)
    {
        Host h (std::array<double, 3> { 44100.0, 48000.0, 96000.0 }[(size_t) pick (0, 2)], 60.0 + pick (0, 140));
        Workspace ws;
        for (auto& p : ws.patterns)
            if (chance (0.8))
                p = progression (progs[pick (0, (int) std::size (progs) - 1)], pick (0, 11));
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.looping = chance (0.4);
        h.loopStart = pick (0, 4);
        h.loopEnd = h.loopStart + pick (1, 8) * 0.5;
        h.style = chance (0.5) ? LoopStyle::Wrap : LoopStyle::Split;

        for (int step = 0; step < 120; ++step)
        {
            const int block = chance (0.3) ? pick (1, 4096) : 1 << pick (5, 11);
            if (chance (0.1)) h.settings.play = ! h.settings.play;
            if (chance (0.1)) h.settings.pattern = pick (0, 3);
            if (chance (0.05)) h.settings.enabled = ! h.settings.enabled;
            if (chance (0.03)) h.settings.analysing = ! h.settings.analysing;
            if (chance (0.05)) h.settings.chordOctave = pick (-3, 3);
            if (chance (0.05)) h.settings.fx.arp = chance (0.5) ? ArpPattern::Off : (ArpPattern) pick (1, numArpPatterns - 1);
            if (chance (0.05)) h.settings.fx.strum = (StrumMode) pick (0, numStrumModes - 1);
            if (chance (0.05)) h.settings.fx.repeat.on = ! h.settings.fx.repeat.on;
            if (chance (0.05)) h.settings.fx.seq.on = ! h.settings.fx.seq.on;
            if (chance (0.05)) h.settings.fx.hold = (HoldMode) pick (0, numHoldModes - 1);
            if (chance (0.05)) h.settings.fx.voiceLeading = (VoiceLeadingMode) pick (0, numVoiceLeadingModes - 1);
            if (chance (0.03)) h.settings.testPattern = ! h.settings.testPattern;
            if (chance (0.08))
            {
                auto& p = ws.patterns[(size_t) pick (0, 3)];
                if (chance (0.3))
                    p = Pattern {};
                else if (! p.empty() && chance (0.5))
                    p.slots[(size_t) pick (0, p.size() - 1)].halfBeats = (uint8_t) pick (1, 16);
                else
                    p = progression (progs[pick (0, (int) std::size (progs) - 1)], pick (0, 11));
                h.engine->setWorkspace (ws);
            }
            if (chance (0.04)) h.ppq = pick (0, 64) * 0.25;                // a locate
            if (chance (0.03)) h.bpm = 40 + pick (0, 200);
            if (chance (0.04)) h.playing = ! h.playing;
            if (chance (0.02)) h.engine->requestReset();
            if (chance (0.1))
            {
                const int note = pick (36, 84);
                h.keyAt (h.pos + pick (0, block - 1), note, true, pick (1, 127));
                h.keyAt (h.pos + pick (0, 3 * block), note, false);
            }
            h.run (h.pos + block, block);
            if ((! h.playing || ! h.settings.play || ! h.settings.enabled || h.settings.analysing)
                && h.engine->getPerformer().isAlive (ChordPerformer::patternKey))
                ++patternWhileStopped;
        }
        // Deliver the key-ups still waiting, then stop from playing: everything must end.
        h.playing = true;
        h.run (h.pos + 3 * 4096 + 1, 512);
        h.stop (256);
        ++runs;
        played += h.rx.noteOns;
        if (! h.clean() && dirty++ == 0)
            first = " (first: run " + std::to_string (r) + ": " + h.rx.describe() + ")";
    }
    check (patternWhileStopped == 0, "the pattern never sounds while stopped, Play is off, Enable is off or analysing ("
                                          + std::to_string (patternWhileStopped) + " blocks)");
    check (dirty == 0 && played > 1000, std::to_string (runs) + " runs of 120 random blocks (Play, Pattern, edits, loops, locates, tempo, stops, resets, keys, every effect): "
                                            + std::to_string (played) + " note-ons, nothing left sounding" + first);
}

int main()
{
    testSlotLines();
    testPlayback();
    testLoopsAndJumps();
    testSwitching();
    testOnOffAndEdits();
    testEffects();
    testFuzz();
    return testutil::finish ("test_player");
}
