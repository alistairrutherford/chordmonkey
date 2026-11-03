// Bind and Keys-Lock tests (PLAN.md §3.3, §7; Phase 3, G5): the router over all 128 notes for
// every Bind and zone start, Keys-Lock's five modes over every note, key and scale (against
// independent definitions, plus examples by hand), and the engine under a fake host: Palette
// and Workspace keys, pattern keys, jumps while the pattern plays, A#, Keys-Lock following the
// chord, the pads and the test pattern ignoring Bind, and a stuck-note fuzz.
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
        parseProgression (text, key, p, error);
        return p;
    }

    using Notes = std::vector<int>;
    const Notes C { 48, 52, 55 }, Dm { 50, 53, 57 }, G { 55, 59, 62 }, Am { 57, 60, 64 }, F { 53, 57, 60 };
    const Notes Dm7 { 50, 53, 57, 60 }, G7 { 55, 59, 62, 65 };

    void chord (Expect& e, double from, double to, const Notes& notes)
    {
        for (int n : notes)
            e.note (from, to, n);
    }

    void stopAndEnd (Host& h, Expect& e, std::initializer_list<Notes> sounding, int block)
    {
        const int64_t at = h.pos;
        h.stop (block);
        for (const auto& notes : sounding)
            for (int n : notes)
                e.offAt (at, n);
    }

    // The notes that went on at absolute sample `at` (±1), sorted.
    Notes onsAt (const Host& h, int64_t at)
    {
        Notes n;
        for (const auto& hit : h.hits)
            if (hit.on && std::llabs (hit.sample - at) <= 1)
                n.push_back (hit.note);
        std::sort (n.begin(), n.end());
        return n;
    }

    std::string list (const Notes& v)
    {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i)
            s += (i ? " " : "") + std::to_string (v[i]);
        return s;
    }
}

static void testRouter()
{
    std::cout << "== router ==\n";
    char name[8];
    noteName (60, name);
    std::string c3 = name;
    noteName (36, name);
    std::string c1 = name;
    noteName (0, name);
    std::string low = name;
    noteName (127, name);
    check (c3 == "C3" && c1 == "C1" && low == "C-2" && std::string (name) == "G8", "note names as Live writes them: 60 C3, 36 C1, 0 C-2, 127 G8");

    const int whiteOf[13] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6, 7 };
    const int blackOf[13] = { -1, 0, -1, 1, -1, -1, 2, -1, 3, -1, 4, -1, -1 };
    int wrong = 0, cases = 0;
    for (auto bind : { BindSource::Workspace, BindSource::Palette, BindSource::Off })
        for (int start : { 0, 21, 36, 48, 60, 99, KeyRouter::maxBindStart })
            for (int n = 0; n < 128; ++n)
            {
                ++cases;
                const auto r = KeyRouter::route (n, bind, start);
                const int off = n - start;
                if (bind == BindSource::Off)
                    wrong += r.kind == KeyRouter::Route::Locked ? 0 : 1;
                else if (off < 0)
                    wrong += r.kind == KeyRouter::Route::Pass ? 0 : 1;
                else if (off > 12)
                    wrong += r.kind == KeyRouter::Route::Locked ? 0 : 1;
                else
                    wrong += (r.kind == KeyRouter::Route::Bound && r.white == whiteOf[off] && r.black == blackOf[off]) ? 0 : 1;
            }
    check (wrong == 0, std::to_string (cases) + " routes (3 Binds × 7 zone starts × 128 notes): below the zone passes, the zone's 13 keys are "
                                                "slots C … C' and the five black keys, above it Keys-Lock; Bind Off locks every key ("
                           + std::to_string (wrong) + " wrong)");
    check (KeyRouter::maxBindStart == 115, "the zone fits up to Bind start 115 (G7)");
}

static void testKeysLock()
{
    std::cout << "== Keys-Lock ==\n";
    auto inScale = [] (int key, Scale s, int note)
    {
        for (int d = 0; d < 7; ++d)
            if ((key + scaleInfo (s).semitones[(size_t) d]) % 12 == note % 12)
                return true;
        return false;
    };

    int wrongOff = 0, wrongOnly = 0, wrongMapped = 0, wrongWhite = 0, total = 0;
    for (int key = 0; key < 12; ++key)
        for (int si = 0; si < numScales; ++si)
        {
            const Scale s = (Scale) si;
            for (int n = 0; n < 128; ++n)
            {
                ++total;
                wrongOff += keysLockNote (n, KeysLockMode::Off, key, s, 0) == n ? 0 : 1;
                wrongOnly += keysLockNote (n, KeysLockMode::ScaleOnly, key, s, 0) == (inScale (key, s, n) ? n : -1) ? 0 : 1;

                // Mapped: in the scale, nearest, a tie up.
                const int m = keysLockNote (n, KeysLockMode::ScaleMapped, key, s, 0);
                int best = -1;
                for (int d = 0; d < 12 && best < 0; ++d)
                {
                    if (n + d <= 127 && inScale (key, s, n + d)) best = n + d;
                    else if (n - d >= 0 && inScale (key, s, n - d)) best = n - d;
                }
                wrongMapped += m == best ? 0 : 1;

                // White keys: black silent; white key of letter w → the scale's degree w, from the tonic at that octave's C.
                const int w = keys::whiteLetter (n % 12);
                const int wk = keysLockNote (n, KeysLockMode::ScaleWhiteKeys, key, s, 0);
                if (w < 0)
                    wrongWhite += wk == -1 ? 0 : 1;
                else
                {
                    const int want = (n / 12) * 12 + key + scaleInfo (s).semitones[(size_t) w];
                    wrongWhite += wk == (want <= 127 ? want : want - 12) ? 0 : 1;
                }
            }
        }
    check (wrongOff == 0 && wrongOnly == 0, "Off plays every note; Scale notes only silences exactly the notes outside the scale ("
                                                + std::to_string (total) + " = 12 keys × " + std::to_string (numScales) + " scales × 128 notes)");
    check (wrongMapped == 0, "Scale notes mapped: the nearest scale note, a tie going up (" + std::to_string (wrongMapped) + " wrong)");
    check (wrongWhite == 0, "Scale white keys: each white key its degree, black keys silent (" + std::to_string (wrongWhite) + " wrong)");

    // By hand, D major.
    auto row = [] (KeysLockMode m, std::initializer_list<int> notes)
    {
        Notes out;
        for (int n : notes)
            out.push_back (keysLockNote (n, m, 2, Scale::Major, 0));
        return out;
    };
    check (row (KeysLockMode::ScaleMapped, { 60, 62, 65, 67, 72 }) == Notes { 61, 62, 66, 67, 73 }, "D major mapped: C→C#, D, F→F#, G, C→C#");
    check (row (KeysLockMode::ScaleOnly, { 60, 61, 65, 66 }) == Notes { -1, 61, -1, 66 }, "D major only: C and F silent");
    check (row (KeysLockMode::ScaleWhiteKeys, { 60, 62, 64, 65, 67, 69, 71, 61 }) == Notes { 62, 64, 66, 67, 69, 71, 73, -1 },
           "D major white keys: C=D D=E E=F# F=G G=A A=B B=C#, C# silent");
    check (keysLockNote (127, KeysLockMode::ScaleWhiteKeys, 11, Scale::Major, 0) == 126 && keysLockNote (0, KeysLockMode::ScaleMapped, 2, Scale::Major, 0) == 1,
           "at the keyboard's ends: G8 in B major (its F#, past 127) an octave down, 126; C-2 in D maps up to C#");

    // Chord notes.
    const unsigned dm = (1u << 2) | (1u << 5) | (1u << 9);
    Notes chordRow;
    for (int n : { 60, 62, 64, 65, 67, 69, 71, 61 })
        chordRow.push_back (keysLockNote (n, KeysLockMode::ChordNotes, 0, Scale::Major, dm));
    check (chordRow == Notes { 62, 62, 65, 65, 69, 69, 69, -1 }, "Chord notes on Dm: each white key the nearest D F A (a tie up), black keys silent: " + list (chordRow));
    int wrongChord = 0;
    std::mt19937 rng (5);
    for (int i = 0; i < 20000; ++i)
    {
        const unsigned mask = (unsigned) (rng() & 0xfffu) | (1u << (rng() % 12));
        const int n = (int) (rng() % 128);
        const int r = keysLockNote (n, KeysLockMode::ChordNotes, 0, Scale::Major, mask);
        if (keys::whiteLetter (n % 12) < 0)
            wrongChord += r == -1 ? 0 : 1;
        else
        {
            int best = -1;
            for (int d = 0; d < 12 && best < 0; ++d)
            {
                if (n + d <= 127 && ((mask >> ((n + d) % 12)) & 1u)) best = n + d;
                else if (n - d >= 0 && ((mask >> ((n - d) % 12)) & 1u)) best = n - d;
            }
            wrongChord += r == best ? 0 : 1;
        }
    }
    check (wrongChord == 0, "20,000 random chords and notes: Chord notes always the nearest chord note (" + std::to_string (wrongChord) + " wrong)");
}

static void testPaletteBind()
{
    std::cout << "== Bind Palette ==\n";
    Host h (48000.0, 120.0);
    h.playing = false;
    h.settings.bind = BindSource::Palette;
    h.settings.followKeyOctave = true;   // ignored: only All keys follows the octave
    const std::pair<int, Notes> keysAndChords[] = {
        { 36, C }, { 38, Dm }, { 43, G }, { 48, { 60, 64, 67 } },       // C, D, G, and C' an octave up
        { 37, { 49, 53, 56 } }, { 39, { 51, 55, 58 } }, { 42, { 54, 57, 60 } }, { 44, { 56, 60, 63 } }, { 46, { 58, 62, 65 } },
    };
    std::string bad;
    double t = 0.0;
    for (const auto& [key, notes] : keysAndChords)
    {
        h.key (t, key, true);
        h.key (t + 0.5, key, false);
        h.run (h.at (t + 1.0), 64);
        const auto got = onsAt (h, h.at (t));
        if (got != notes && bad.empty())
            bad = " (key " + std::to_string (key) + ": " + list (got) + ")";
        t += 1.0;
    }
    check (bad.empty(), "C1-C2 play I ii V and I an octave up; C# D# F# G# A# play bII bIII #iv° bVI bVII; Key octave Follow doesn't move them" + bad);

    h.key (t, 35, true);
    h.key (t + 0.5, 35, false);
    h.key (t + 1.0, 61, true);
    h.key (t + 1.5, 61, false);
    h.run (h.at (t + 2.0), 64);
    check (onsAt (h, h.at (t)) == Notes { 35 } && onsAt (h, h.at (t + 1.0)) == Notes { 61 }, "below the zone a key passes; above it, with Keys-Lock Off, too");
    t += 2.0;
    h.settings.keysLock = KeysLockMode::ScaleOnly;
    h.key (t, 61, true);
    h.key (t + 0.5, 61, false);
    h.key (t + 1.0, 38, true);                   // Dm: Chord notes follows it
    h.run (h.at (t + 1.5), 64);
    h.settings.keysLock = KeysLockMode::ChordNotes;
    h.key (t + 2.0, 60, true);
    h.key (t + 2.5, 60, false);
    h.key (t + 2.5, 38, false);
    h.run (h.at (t + 3.0), 64);
    check (onsAt (h, h.at (t)).empty() && onsAt (h, h.at (t + 2.0)) == Notes { 62 } && h.clean(),
           "Keys-Lock above the zone: C# silent in C major; with Dm held, C3 plays D3; everything ends with its key");
}

static void testWorkspaceBind()
{
    std::cout << "== Bind Workspace ==\n";
    {
        Host h (48000.0, 120.0);
        h.playing = false;
        Workspace ws;
        ws.patterns[0] = progression ("I V vi IV");
        ws.patterns[1] = progression ("ii7 V7");
        h.engine->setWorkspace (ws);
        h.settings.bind = BindSource::Workspace;
        const std::pair<int, Notes> steps[] = { { 36, C }, { 38, G }, { 40, Am }, { 41, F }, { 43, {} } };
        std::string bad;
        double t = 0.0;
        for (const auto& [key, notes] : steps)
        {
            h.key (t, key, true);
            h.key (t + 0.5, key, false);
            h.run (h.at (t + 1.0), 128);
            if (onsAt (h, h.at (t)) != notes && bad.empty())
                bad = " (key " + std::to_string (key) + ": " + list (onsAt (h, h.at (t))) + ")";
            t += 1.0;
        }
        check (bad.empty(), "stopped: C1 D1 E1 F1 play pattern A's slots 1-4 while held; G1 (slot 5, empty) nothing" + bad);

        h.key (t, 39, true);                     // D#: pattern B
        h.key (t + 0.25, 39, false);
        h.key (t + 0.5, 36, true);
        h.key (t + 0.75, 36, false);
        h.run (h.at (t + 1.0), 128);
        const int request = h.engine->takePatternRequest();
        check (onsAt (h, h.at (t)).empty() && onsAt (h, h.at (t + 0.5)) == Dm7 && request == 1 && h.engine->takePatternRequest() == -1
                   && h.clean(),
               "D# chooses pattern B (and asks once for the Pattern parameter); C1 then plays its Dm7");
    }

    sweep ("playing: F1 at beat 1.3 jumps to slot 4 (IV) at beat 2; the pattern goes on from there", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        Workspace ws;
        ws.patterns[0] = progression ("I V vi IV");
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.settings.bind = BindSource::Workspace;
        h.key (1.3, 41, true);
        h.key (1.6, 41, false);
        h.run (h.at (13.5), c.block);
        Expect e (h.spq());
        chord (e, 0, 2, C);
        chord (e, 2, 6, F);
        chord (e, 6, 10, C);
        for (int n : G)
            e.on (10, n);
        stopAndEnd (h, e, { G }, c.block);
        return finish (h, e, worst);
    });

    sweep ("playing: a slot key on a beat jumps there and then", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        Workspace ws;
        ws.patterns[0] = progression ("I V vi IV");
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.settings.bind = BindSource::Workspace;
        h.key (2.0, 40, true);                   // E1: slot 3 (vi)
        h.key (2.5, 40, false);
        h.run (h.at (7.5), c.block);
        Expect e (h.spq());
        chord (e, 0, 2, C);
        chord (e, 2, 6, Am);
        for (int n : F)
            e.on (6, n);
        stopAndEnd (h, e, { F }, c.block);
        return finish (h, e, worst);
    });

    sweep ("playing: A# silences the chord until the next slot", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        Workspace ws;
        ws.patterns[0] = progression ("I V");
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.settings.bind = BindSource::Workspace;
        h.key (1.0, 46, true);
        h.key (1.5, 46, false);
        h.run (h.at (5.5), c.block);
        Expect e (h.spq());
        chord (e, 0, 1, C);
        for (int n : G)
            e.on (4, n);
        stopAndEnd (h, e, { G }, c.block);
        return finish (h, e, worst);
    });

    sweep ("playing: D# at beat 5 switches to B at the bar (8); Chord notes follows the pattern's chord", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        Workspace ws;
        ws.patterns[0] = progression ("I V vi IV");
        ws.patterns[1] = progression ("ii7:2 V7:2");
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.settings.bind = BindSource::Workspace;
        h.settings.keysLock = KeysLockMode::ChordNotes;
        h.key (5.0, 39, true);
        h.key (5.25, 39, false);
        // An octave above the pattern's chord, so no note is one the chord sounds already (a
        // shared pitch isn't struck twice).
        h.key (6.0, 72, true);                   // over G: C4 → B3 (71)
        h.key (7.0, 72, false);
        h.key (9.0, 77, true);                   // over Dm7 (D F A C): F4 stays F (77)
        h.key (9.5, 77, false);
        h.key (10.5, 76, true);                  // over G7 (G B D F): E4 → F4, the nearer
        h.key (11.0, 76, false);
        h.run (h.at (11.5), c.block);
        Expect e (h.spq());
        chord (e, 0, 4, C);
        chord (e, 4, 8, G);
        chord (e, 8, 10, Dm7);
        for (int n : G7)
            e.on (10, n);
        e.note (6.0, 7.0, 71);
        e.note (9.0, 9.5, 77);
        e.note (10.5, 11.0, 77);
        stopAndEnd (h, e, { G7 }, c.block);
        return finish (h, e, worst);
    });

    {
        Host h (48000.0, 120.0);
        h.playing = false;
        h.settings.bind = BindSource::Off;
        h.settings.keysLock = KeysLockMode::ScaleWhiteKeys;
        h.settings.key = 2;
        h.key (0.0, 36, true);
        h.key (0.5, 36, false);
        h.keyAt (h.at (1.0), 48, true);
        h.run (h.at (1.5), 64);
        // The chord pads (and the test pattern) play the palette whatever Bind is.
        BlockEvents out;
        MidiEvent pad = MidiEvent::noteOn (0, 50, 100, 0).withOrigin (MidiEvent::fromPalette);
        h.engine->process (h.settings, Transport { 48000.0, false }, &pad, 1, 64, out);
        Notes padNotes;
        for (const auto& ev : out)
            if (ev.isNoteOn())
                padNotes.push_back (ev.note());
        std::sort (padNotes.begin(), padNotes.end());
        check (onsAt (h, 0) == Notes { 38 } && onsAt (h, h.at (1.0)) == Notes { 50 } && padNotes == Notes { 52, 55, 59 },
               "Bind Off: every key is Keys-Lock (white keys in D: C1→D1, C2→D2), but a pad's D plays ii in D (Em: " + list (padNotes) + ")");
    }

    sweep ("the test pattern plays the palette with Bind Workspace and Keys-Lock on", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.testPattern = true;
        h.settings.bind = BindSource::Workspace;
        h.settings.keysLock = KeysLockMode::ScaleOnly;
        h.run (h.at (1.75), c.block);
        Expect e (h.spq());
        chord (e, 0, 0.5, C);
        chord (e, 1, 1.5, Dm);
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });
}

static void testFuzz()
{
    std::cout << "== stuck-note fuzz ==\n";
    std::mt19937 rng (303);
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    const char* progs[] = { "I V vi IV", "ii7:2 V7:2 Imaj7", "I:0.5 IV:0.5 V:1", "vi:8", "I" };

    int dirty = 0, played = 0;
    std::string first;
    for (int r = 0; r < 60; ++r)
    {
        Host h (48000.0, 60.0 + pick (0, 140));
        Workspace ws;
        for (auto& p : ws.patterns)
            if (chance (0.8))
                p = progression (progs[pick (0, (int) std::size (progs) - 1)], pick (0, 11));
        h.engine->setWorkspace (ws);
        h.looping = chance (0.3);
        h.loopStart = pick (0, 4);
        h.loopEnd = h.loopStart + pick (1, 8) * 0.5;
        for (int step = 0; step < 120; ++step)
        {
            const int block = chance (0.3) ? pick (1, 4096) : 1 << pick (5, 11);
            if (chance (0.08)) h.settings.bind = (BindSource) pick (0, numBindSources - 1);
            if (chance (0.05)) h.settings.bindStart = pick (0, KeyRouter::maxBindStart);
            if (chance (0.08)) h.settings.keysLock = (KeysLockMode) pick (0, numKeysLockModes - 1);
            if (chance (0.08)) h.settings.play = ! h.settings.play;
            if (chance (0.05)) h.settings.pattern = pick (0, 3);
            if (chance (0.05)) h.settings.key = pick (0, 11);
            if (chance (0.05)) h.settings.scale = (Scale) pick (0, numScales - 1);
            if (chance (0.04)) h.settings.enabled = ! h.settings.enabled;
            if (chance (0.04)) h.settings.fx.hold = (HoldMode) pick (0, numHoldModes - 1);
            if (chance (0.04)) h.settings.fx.arp = chance (0.5) ? ArpPattern::Off : (ArpPattern) pick (1, numArpPatterns - 1);
            if (chance (0.04)) h.settings.followKeyOctave = ! h.settings.followKeyOctave;
            if (chance (0.03)) h.playing = ! h.playing;
            if (chance (0.03)) h.ppq = pick (0, 64) * 0.25;
            if (chance (0.05))
            {
                ws.patterns[(size_t) pick (0, 3)] = chance (0.3) ? Pattern {} : progression (progs[pick (0, (int) std::size (progs) - 1)]);
                h.engine->setWorkspace (ws);
            }
            for (int k = pick (0, 3); k > 0; --k)
            {
                const int note = h.settings.bindStart + pick (-6, 24);
                if (note < 0 || note > 127)
                    continue;
                h.keyAt (h.pos + pick (0, block - 1), note, true, pick (1, 127));
                h.keyAt (h.pos + pick (0, 3 * block), note, false);
            }
            h.run (h.pos + block, block);
            (void) h.engine->takePatternRequest();
        }
        h.playing = true;
        h.run (h.pos + 3 * 4096 + 1, 512);
        h.stop (256);
        played += h.rx.noteOns;
        if (! h.clean() && dirty++ == 0)
            first = " (first: run " + std::to_string (r) + ": " + h.rx.describe() + ")";
    }
    check (dirty == 0 && played > 1000, "60 runs of 120 random blocks (Bind, zone start, Keys-Lock, Play, patterns, keys in and around the zone, "
                                        "Hold, Arp, loops, locates, stops): " + std::to_string (played) + " note-ons, nothing left sounding" + first);
}

int main()
{
    testRouter();
    testKeysLock();
    testPaletteBind();
    testWorkspaceBind();
    testFuzz();
    return testutil::finish ("test_bind");
}
