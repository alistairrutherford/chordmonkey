// Engine tests: KeyRouter, VoiceTracker ownership, chord output, Progressions, the test
// pattern under a fake transport, the snapshot exchange, and stuck-note fuzzing (ChordChimp's PLAN.md §8).
#include "TestUtil.h"
#include "Receiver.h"
#include "core/engine/ChordEngine.h"
#include "core/engine/SnapshotExchange.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace chordmonkey;
using testutil::check;
using testutil::Receiver;

namespace
{
    std::vector<int> notesOn (const BlockEvents& out)
    {
        std::vector<int> v;
        for (const auto& e : out)
            if (e.isNoteOn()) v.push_back (e.note());
        std::sort (v.begin(), v.end());
        return v;
    }

    std::vector<int> notesOff (const BlockEvents& out)
    {
        std::vector<int> v;
        for (const auto& e : out)
            if (e.isNoteOff()) v.push_back (e.note());
        std::sort (v.begin(), v.end());
        return v;
    }

    // Runs one block with the given input events.
    struct Rig
    {
        ChordEngine engine;
        EngineSettings settings;
        Transport transport;
        BlockEvents out;
        Receiver rx;

        BlockEvents& run (std::vector<MidiEvent> in, int numSamples = 512)
        {
            std::stable_sort (in.begin(), in.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });
            out.clear();
            engine.process (settings, transport, in.data(), (int) in.size(), numSamples, out);
            rx.feed (out, numSamples);
            if (transport.playing)
            {
                // A host that loops without splitting its blocks.
                const double next = transport.ppq + numSamples / transport.samplesPerBeat();
                const bool wraps = transport.looping && transport.loopEnd > transport.loopStart
                                && transport.ppq < transport.loopEnd && next >= transport.loopEnd;
                transport.ppq = wraps ? next - (transport.loopEnd - transport.loopStart) : next;
            }
            return out;
        }
    };
}

//==============================================================================
static void testRouter()
{
    std::cout << "== key router ==\n";
    bool whites = true, blacks = true;
    for (int note = 0; note < 128; ++note)
    {
        const int w = keys::whiteLetter (note % 12);
        if (w >= 0)
            whites &= KeyRouter::slotOf (note) == w;
        else
            blacks &= KeyRouter::slotOf (note) == numWhiteSlots + keys::blackIndex (note % 12);
    }
    check (whites, "no split: C..B are chord slots 0-6 in every octave, 0-127");
    check (blacks, "C# D# F# G# A# are slots 7-11 (the borrowed chords), in every octave");
    check (KeyRouter::keyForSlot (0) == 48 && KeyRouter::keyForSlot (6) == 59 && KeyRouter::keyForSlot (7) == 49
               && KeyRouter::keyForSlot (11) == 58 && KeyRouter::keyForSlot (12) == -1,
           "the editor's and test pattern's keys: the home octave, 48..59");
}

//==============================================================================
static void testVoiceTracker()
{
    std::cout << "== voice tracker ==\n";
    VoiceTracker vt;
    BlockEvents out;

    vt.start (1, 0, 60, 100, 0, out);
    vt.start (2, 0, 60, 90, 5, out);
    check (out.size() == 1 && vt.ownerCount (0, 60) == 2, "a shared pitch sends one note-on and has two owners");

    vt.release (1, 10, out);
    check (out.size() == 1 && vt.soundingNotes() == 1, "releasing one owner doesn't cut the other's note");

    vt.release (2, 20, out);
    check (out.size() == 2 && out[1].isNoteOff() && out[1].sample == 20 && vt.isEmpty(), "the last owner sends the note-off");

    out.clear();
    vt.start (3, 0, 64, 100, 0, out);
    vt.start (3, 0, 64, 100, 0, out);
    check (out.size() == 1 && vt.ownerCount (0, 64) == 1, "a source owning a pitch twice owns it once");
    vt.release (3, 0, out);
    check (out.size() == 2 && vt.isEmpty(), "and releases it once");

    out.clear();
    vt.start (4, 0, 60, 100, 0, out);
    vt.start (4, 1, 60, 100, 0, out);
    check (out.size() == 2 && vt.soundingNotes() == 2, "the same note on two channels is two voices");
    vt.releaseAll (0, out);
    check (vt.isEmpty() && out.size() == 4, "releaseAll ends everything");

    // Full output buffer: note-ons are refused once half the buffer is used, note-offs always fit.
    EventBuffer<8> small;
    int started = 0;
    for (int n = 0; n < 8; ++n)
        started += vt.start (5, 0, n, 100, 0, small) ? 1 : 0;
    check (started == 4 && vt.notesOwnedBy (5) == 4, "note-ons stop at half the buffer");
    vt.release (5, 0, small);
    check (small.size() == 8 && vt.isEmpty(), "so every started note can still be released");

    out.clear();
    vt.start (VoiceTracker::maxSources, 0, 60, 100, 0, out);
    check (out.size() == 0, "an out-of-range source owns nothing");
}

//==============================================================================
static void testChords()
{
    std::cout << "== chord output ==\n";
    Rig rig;

    auto& o1 = rig.run ({ MidiEvent::noteOn (0, 48, 100, 10) });
    check (notesOn (o1) == std::vector<int> { 48, 52, 55 } && o1[0].sample == 10, "C key: C major triad at the key's sample");
    check (o1[0].velocity() == 100, "chord notes take the key's velocity");

    auto& o2 = rig.run ({ MidiEvent::noteOff (0, 48, 3) });
    check (notesOff (o2) == std::vector<int> { 48, 52, 55 } && rig.engine.getVoices().isEmpty(), "key up releases the chord");

    // Every white key in the default octave, C major: I ii iii IV V vi vii°.
    const std::vector<std::vector<int>> expected = { { 48, 52, 55 }, { 50, 53, 57 }, { 52, 55, 59 }, { 53, 57, 60 },
                                                     { 55, 59, 62 }, { 57, 60, 64 }, { 59, 62, 65 } };
    bool allSlots = true;
    for (int w = 0; w < 7; ++w)
    {
        allSlots &= notesOn (rig.run ({ MidiEvent::noteOn (0, 48 + keys::whiteOffsets[w], 90, 0) })) == expected[(size_t) w];
        rig.run ({ MidiEvent::noteOff (0, 48 + keys::whiteOffsets[w], 0) });
    }
    check (allSlots, "C major: the seven white keys play I ii iii IV V vi vii°");

    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 36, 90, 0) })) == expected[0], "the octave doesn't matter");
    rig.run ({ MidiEvent::noteOff (0, 36, 0) });

    // Shared tones: I (C E G) and iii (E G B).
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    auto& both = rig.run ({ MidiEvent::noteOn (0, 52, 90, 0) });
    check (notesOn (both) == std::vector<int> { 59 }, "iii while I is held: only B is new");
    auto& relI = rig.run ({ MidiEvent::noteOff (0, 48, 0) });
    check (notesOff (relI) == std::vector<int> { 48 }, "releasing I keeps E and G for iii");
    auto& reliii = rig.run ({ MidiEvent::noteOff (0, 52, 0) });
    check (notesOff (reliii) == std::vector<int> { 52, 55, 59 } && rig.engine.getVoices().isEmpty(), "releasing iii ends the rest");

    // Key change while held: the chord follows, and the key-up releases the new notes.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    rig.settings.key = 2;
    auto& moved = rig.run ({});
    check (notesOff (moved) == std::vector<int> { 48, 52, 55 } && notesOn (moved) == std::vector<int> { 50, 54, 57 },
           "Key C→D while held: C E G off, D F# A on");
    auto& movedOff = rig.run ({ MidiEvent::noteOff (0, 48, 0) });
    check (notesOff (movedOff) == std::vector<int> { 50, 54, 57 } && rig.engine.getVoices().isEmpty(), "key up releases D F# A");
    rig.settings.key = 0;

    // Group, scale and octave.
    rig.settings.group = ChordGroup::Sevenths;
    rig.settings.scale = Scale::NaturalMinor;
    rig.settings.chordOctave = 1;
    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 55, 90, 0) })) == std::vector<int> { 67, 71, 74, 77 },
           "C natural minor, Sevenths, octave +1: the G key plays G7 (raised 3rd)");
    rig.run ({ MidiEvent::noteOff (0, 55, 0) });
    rig.settings = EngineSettings {};

    // No split: a white key plays its chord in any octave; a black key plays its borrowed chord.
    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 86, 90, 0) })) == expected[1], "D at 86 (above the old split) plays ii");
    rig.run ({ MidiEvent::noteOff (0, 86, 0) });
    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 73, 90, 0) })) == std::vector<int> { 49, 53, 56 },
           "C# at 73 plays bII (Db F Ab), in any octave");
    check (notesOff (rig.run ({ MidiEvent::noteOff (0, 73, 0) })) == std::vector<int> { 49, 53, 56 } && rig.engine.getVoices().isEmpty(),
           "and its key-up ends it");
    rig.settings.progression = Progression::Pop;
    rig.run ({ MidiEvent::noteOn (0, 58, 90, 0) });
    rig.settings.key = 2;
    auto& movedBlack = rig.run ({});
    check (notesOff (movedBlack) == std::vector<int> { 58, 62, 65 } && notesOn (movedBlack) == std::vector<int> { 60, 64, 67 },
           "A# under Pop plays bVII (Bb D F) as without it; Key C→D while held: it becomes C E G");
    rig.run ({ MidiEvent::noteOff (0, 58, 0) });
    check (rig.engine.getVoices().isEmpty(), "and its key-up releases it");
    rig.settings.key = 0;
    rig.settings.progression = Progression::Diatonic;

    // Enable off: input passes through unchanged, on its own channel.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    rig.settings.enabled = false;
    auto& disabled = rig.run ({ MidiEvent::noteOn (3, 50, 77, 7) });
    check (notesOff (disabled) == std::vector<int> { 48, 52, 55 }, "Enable off releases the generated chord");
    check (notesOn (disabled) == std::vector<int> { 50 } && disabled[disabled.size() - 1].channel() == 3
               && disabled[disabled.size() - 1].velocity() == 77,
           "Enable off: a key passes through unchanged (note, channel, velocity)");
    rig.settings.enabled = true;
    auto& reenabled = rig.run ({});
    check (notesOff (reenabled) == std::vector<int> { 50 }, "Enable on again releases the passed-through note");
    rig.run ({ MidiEvent::noteOff (3, 50, 0), MidiEvent::noteOff (0, 48, 0) });
    check (rig.engine.getVoices().isEmpty(), "and the late key-ups send nothing stray");

    // Non-note messages pass through; All Notes Off releases everything and passes on.
    auto& cc = rig.run ({ MidiEvent::controller (0, 1, 64, 3) });
    check (cc.size() == 1 && cc[0].isController (1) && cc[0].sample == 3, "a CC passes through");
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    auto& ano = rig.run ({ MidiEvent::controller (0, 123, 0, 9) });
    check (notesOff (ano) == std::vector<int> { 48, 52, 55 } && ano[ano.size() - 1].isAllNotesOff(),
           "All Notes Off releases the chord and is passed on");

    // Retrigger: the same key twice releases then replays.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    auto& again = rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    check (notesOff (again).size() == 3 && notesOn (again).size() == 3 && again[0].isNoteOff(), "a repeated key-down retriggers (offs first)");

    // requestReset (releaseResources / prepareToPlay): the next block releases everything.
    rig.engine.requestReset();
    check (notesOff (rig.run ({})).size() == 3 && rig.engine.getVoices().isEmpty(), "requestReset: next block releases all");
    rig.run ({ MidiEvent::noteOff (0, 48, 0) });

    check (rig.rx.clean() && rig.rx.on.empty(), "receiver stream clean: " + rig.rx.describe());
}

//==============================================================================
// The test pattern under a fake transport: a chord on every beat, sample-accurate, an
// eighth note long, slot = beat mod 7, across block edges, tempos and sample rates.
static void testPatternTiming (double sampleRate, double bpm, int blockSize, double startPpq = 0.0)
{
    Rig rig;
    rig.settings.testPattern = true;
    rig.transport = { sampleRate, true, startPpq, bpm, 4, 4 };

    const double spb = rig.transport.samplesPerBeat();
    const int beats = 16;
    const int64_t totalSamples = (int64_t) std::ceil (beats * spb) + blockSize;

    struct Hit { int64_t sample; int note; bool on; };
    std::vector<Hit> hits;
    int64_t pos = 0;
    while (pos < totalSamples)
    {
        auto& out = rig.run ({}, blockSize);
        for (const auto& e : out)
            hits.push_back ({ pos + e.sample, e.note(), e.isNoteOn() });
        pos += blockSize;
    }

    // Group note-ons by beat.
    std::map<int, std::vector<int>> onsByBeat;
    double worst = 0.0;
    int offTiming = 0;
    for (const auto& h : hits)
    {
        const double beat = startPpq + (double) h.sample / spb;
        const int nearest = (int) std::llround (beat * 2.0);   // half-beat grid
        worst = std::max (worst, std::abs (beat * 2.0 - nearest) * spb / 2.0);
        if (h.on)
        {
            if (nearest % 2 != 0) ++offTiming;
            onsByBeat[nearest / 2].push_back (h.note);
        }
        else if (nearest % 2 == 0)
        {
            ++offTiming;
        }
    }

    const ChordSet chords = makeGroupChords (ChordGroup::Triads, Scale::Major);
    int wrongChords = 0, beatsSeen = 0;
    const int firstBeat = (int) std::ceil (startPpq - 1e-9);
    for (auto& [beat, notes] : onsByBeat)
    {
        if (beat < firstBeat || beat >= firstBeat + beats)
            continue;
        ++beatsSeen;
        std::sort (notes.begin(), notes.end());
        const auto want = chordNotes (chords[(size_t) TestPattern::slotForBeat (beat)], {});
        std::vector<int> w (want.begin(), want.end());
        std::sort (w.begin(), w.end());
        wrongChords += notes == w ? 0 : 1;
    }

    std::ostringstream name;
    name << "test pattern at " << sampleRate / 1000.0 << " kHz, " << bpm << " BPM, block " << blockSize
         << (startPpq != 0.0 ? ", from ppq " + std::to_string (startPpq) : "") << ": ";
    check (beatsSeen == beats && wrongChords == 0,
           name.str() + std::to_string (beatsSeen) + " beats, " + std::to_string (wrongChords) + " wrong or doubled chords");
    check (worst <= 1.0 && offTiming == 0,
           name.str() + "every on/off within 1 sample of its beat / eighth (worst " + std::to_string (worst) + ")");

    // Stop: the held pattern chord ends.
    rig.transport.playing = false;
    rig.run ({}, blockSize);
    check (rig.engine.getVoices().isEmpty() && rig.rx.clean() && rig.rx.on.empty(), name.str() + "clean after stop: " + rig.rx.describe());
}

static void testPatternLoop()
{
    Rig rig;
    rig.settings.testPattern = true;
    rig.transport = { 48000.0, true, 0.0, 120.0, 4, 4 };
    const int block = 480;   // 1/50 beat at 120 BPM
    // Play into beat 3, past the key-down, then jump back to 0 before the key-up.
    while (rig.transport.ppq < 3.2)
        rig.run ({}, block);
    check (rig.engine.getVoices().soundingNotes() == 3, "pattern chord sounding at ppq 3.2");
    rig.transport.ppq = 0.0;
    auto& wrap = rig.run ({}, block);
    check (notesOff (wrap).size() == 3 && notesOn (wrap) == std::vector<int> { 48, 52, 55 },
           "loop wrap: the held chord is released and beat 0 plays again");

    rig.settings.testPattern = false;
    rig.run ({}, block);
    rig.run ({}, block);
    check (rig.engine.getVoices().isEmpty() && rig.rx.clean(), "test pattern off: released, " + rig.rx.describe());
}

//==============================================================================
static void testProgressions()
{
    std::cout << "== progressions ==\n";
    Rig rig;
    rig.settings.progression = Progression::Pop;   // 1 5 6 4 2 3 7
    const std::vector<std::vector<int>> pop = { { 48, 52, 55 }, { 55, 59, 62 }, { 57, 60, 64 }, { 53, 57, 60 },
                                                { 50, 53, 57 }, { 52, 55, 59 }, { 59, 62, 65 } };
    bool all = true;
    for (int w = 0; w < 7; ++w)
    {
        all &= notesOn (rig.run ({ MidiEvent::noteOn (0, 48 + keys::whiteOffsets[w], 90, 0) })) == pop[(size_t) w];
        rig.run ({ MidiEvent::noteOff (0, 48 + keys::whiteOffsets[w], 0) });
    }
    check (all, "Pop (1-5-6-4): C D E F G A B play I V vi IV ii iii vii*");

    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 50, 90, 0) })) == std::vector<int> { 55, 59, 62 }, "Pop: the D key plays V");

    // Progression change while held: the key replays its new degree; key-up releases it.
    rig.settings.progression = Progression::Diatonic;
    auto& moved = rig.run ({});
    check (notesOff (moved) == std::vector<int> { 55, 59, 62 } && notesOn (moved) == std::vector<int> { 50, 53, 57 },
           "progression change while held: V -> ii");
    rig.settings.progression = Progression::Custom;   // Chordism's Custom: plays as Diatonic
    check (rig.run ({}).size() == 0, "Chordism's Custom plays as Diatonic: nothing restrikes");
    check (notesOff (rig.run ({ MidiEvent::noteOff (0, 50, 0) })) == std::vector<int> { 50, 53, 57 }
               && rig.engine.getVoices().isEmpty() && rig.rx.clean(),
           "and its key-up releases exactly what it plays");
}

//==============================================================================
//==============================================================================
// Phase 8: Key octave (ChordChimp's PLAN.md §3.1).
static void testKeyOctave()
{
    std::cout << "== key octave ==\n";
    Rig rig;
    auto press = [&] (int note) { return notesOn (rig.run ({ MidiEvent::noteOn (0, note, 90, 0) })); };
    auto lift = [&] (int note) { return notesOff (rig.run ({ MidiEvent::noteOff (0, note, 0) })); };

    check (press (60) == std::vector<int> { 48, 52, 55 } && lift (60) == std::vector<int> { 48, 52, 55 },
           "Ignore (the default): C at 60 plays C E G at 48, as every C does");

    rig.settings.followKeyOctave = true;
    const auto c1 = press (36);
    lift (36);
    const auto c2 = press (48);
    lift (48);
    const auto c3 = press (60);
    lift (60);
    check (c1 == std::vector<int> { 36, 40, 43 } && c2 == std::vector<int> { 48, 52, 55 } && c3 == std::vector<int> { 60, 64, 67 },
           "Follow: C at 36, 48, 60 plays C E G an octave down, as written, an octave up");
    rig.settings.chordOctave = 1;
    check (press (73) == std::vector<int> { 85, 89, 92 } && lift (73) == std::vector<int> { 85, 89, 92 },
           "Follow with Chord Octave +1: C# at 73 (two octaves up) plays Db F Ab three octaves above 49");
    rig.settings.chordOctave = 0;
    check (press (127) == std::vector<int> { 127 } && lift (127) == std::vector<int> { 127 } && rig.engine.getVoices().isEmpty(),
           "Follow at the top: G at 127 keeps only the tone that fits (127) and its key-up ends it");

    // Switching while a chord is held moves it.
    press (60);
    rig.settings.followKeyOctave = false;
    auto& moved = rig.run ({});
    check (notesOff (moved) == std::vector<int> { 60, 64, 67 } && notesOn (moved) == std::vector<int> { 48, 52, 55 },
           "Follow → Ignore while C at 60 is held: C E G moves down to 48");
    lift (60);
    check (rig.engine.getVoices().isEmpty(), "and its key-up ends it");

    // Smooth voice leading leads from the chord as written, then the octave moves the voicing.
    auto ledG = [] (bool follow, int gKey) {
        Rig r;
        r.settings.fx.voiceLeading = VoiceLeadingMode::Smooth;
        r.settings.followKeyOctave = follow;
        r.run ({ MidiEvent::noteOn (0, follow ? 60 : 48, 90, 0) });
        r.run ({ MidiEvent::noteOff (0, follow ? 60 : 48, 0) });
        return notesOn (r.run ({ MidiEvent::noteOn (0, gKey, 90, 0) }));
    };
    auto plain = ledG (false, 55);
    for (auto& n : plain)
        n += 12;
    check (! plain.empty() && ledG (true, 67) == plain,
           "Follow with Smooth: C then G an octave up is the led C-to-G voicing, an octave up");
}

static void testSnapshotExchange()
{
    std::cout << "== snapshot exchange ==\n";
    SnapshotExchange<int> x (7);
    check (x.read() == 7 && ! x.hasNew(), "starts with the initial value");
    x.publish (1);
    x.publish (2);
    check (x.hasNew() && x.read() == 2 && ! x.hasNew() && x.read() == 2, "the reader gets the latest; reading again keeps it");

    // Two threads: the writer publishes {n, n, …}; the reader must never see a torn snapshot
    // or go backwards.
    struct Blob { std::array<uint32_t, 64> v {}; };
    SnapshotExchange<Blob> ex;
    std::atomic<bool> done { false };
    std::thread writer ([&] {
        Blob b;
        for (uint32_t n = 1; n <= 200000; ++n)
        {
            b.v.fill (n);
            ex.publish (b);
        }
        done = true;
    });
    int torn = 0, backwards = 0, reads = 0;
    uint32_t last = 0;
    while (! done.load() || ex.hasNew())
    {
        const Blob& b = ex.read();
        for (auto e : b.v)
            torn += e != b.v[0] ? 1 : 0;
        backwards += b.v[0] < last ? 1 : 0;
        last = b.v[0];
        ++reads;
    }
    writer.join();
    check (torn == 0 && backwards == 0 && ex.read().v[0] == 200000,
           "200000 publishes against " + std::to_string (reads) + " reads: none torn, none out of order, the last one seen");
}

//==============================================================================
// Stuck-note fuzzing: random keys, settings, transport moves and block sizes over thousands
// of blocks. The output must never double a note-on or send a stray note-off, and once every
// key is up and the transport stops, nothing may sound and VoiceTracker must be empty.
// Phase 3: also every chord effect (Stack, Strum, Arp, Repeat, Step Sequencer, Velocity, Hold),
// loops that wrap inside blocks, and time-signature changes; the scheduler's queue must stay
// within its bound (4 events per performance).
// `fx`: a run that keeps a timed effect on and the transport looping most of the time.
static void testFuzz (uint32_t seed, int blocks, bool fxRun = false)
{
    std::mt19937 rng (seed);
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };

    Rig rig;
    rig.transport = { 44100.0, false, 0.0, 120.0, 4, 4 };
    std::set<std::pair<int, int>> keysDown;   // (channel, note) the "player" holds
    int maxSounding = 0;
    int fxBlocks = 0, wrapBlocks = 0;         // blocks with a timed effect playing, with a loop wrap inside

    for (int b = 0; b < blocks; ++b)
    {
        const int numSamples = chance (0.1) ? pick (1, 2048) : (1 << pick (5, 11));
        std::vector<MidiEvent> in;
        const int numEvents = chance (0.5) ? pick (0, 12) : 0;
        // In sample order, so the player's idea of which keys are down matches the engine's.
        std::vector<int> samples;
        for (int i = 0; i < numEvents; ++i)
            samples.push_back (pick (0, numSamples - 1));
        std::sort (samples.begin(), samples.end());
        for (const int sample : samples)
        {
            const double r = std::uniform_real_distribution<double> (0.0, 1.0) (rng);
            if (r < 0.45)
            {
                const int ch = chance (0.9) ? 0 : pick (1, 15);
                const int note = chance (0.8) ? pick (36, 84) : pick (0, 127);
                in.push_back (MidiEvent::noteOn (ch, note, pick (1, 127), sample));
                keysDown.insert ({ ch, note });
            }
            else if (r < 0.9 && ! keysDown.empty())
            {
                auto it = keysDown.begin();
                std::advance (it, pick (0, (int) keysDown.size() - 1));
                in.push_back (chance (0.5) ? MidiEvent::noteOff (it->first, it->second, sample)
                                           : MidiEvent::noteOn (it->first, it->second, 0, sample));   // velocity-0 note-off
                keysDown.erase (it);
            }
            else if (r < 0.93)
            {
                in.push_back (MidiEvent::noteOff (0, pick (0, 127), sample));   // stray key-up
            }
            else if (r < 0.95)
            {
                in.push_back (MidiEvent::controller (0, 123, 0, sample));
            }
            else
            {
                in.push_back (MidiEvent::controller (0, pick (0, 119), pick (0, 127), sample));
            }
        }

        if (chance (0.05)) rig.settings.key = pick (0, 11);
        if (chance (0.03)) rig.settings.scale = (Scale) pick (0, numScales - 1);
        if (chance (0.03)) rig.settings.group = (ChordGroup) pick (0, numChordGroups - 1);
        if (chance (0.03)) rig.settings.chordOctave = pick (-3, 3);
        if (chance (0.02)) rig.settings.followKeyOctave = chance (0.5);
        if (chance (0.02)) rig.settings.enabled = ! rig.settings.enabled;
        if (chance (0.03)) rig.settings.testPattern = ! rig.settings.testPattern;
        if (chance (0.02)) rig.transport.playing = ! rig.transport.playing;
        if (chance (0.01)) rig.transport.ppq = std::uniform_real_distribution<double> (-1.0, 64.0) (rng);   // jump / loop wrap
        if (chance (0.01)) rig.transport.bpm = std::uniform_real_distribution<double> (40.0, 300.0) (rng);
        if (chance (0.005)) rig.transport.sampleRate = chance (0.5) ? 48000.0 : 96000.0;
        if (chance (0.003)) rig.engine.requestReset();

        // Progressions while notes are held.
        if (chance (0.02)) rig.settings.progression = (Progression) pick (0, numProgressions - 1);
        // Phase 3: chord effects, holds, loops and time signatures while notes are held.
        auto& fx = rig.settings.fx;
        auto unit = [&] { return std::uniform_real_distribution<double> (0.0, 1.0) (rng); };
        if (chance (0.03)) fx.stack = (StackMode) pick (0, numStackModes - 1);
        if (chance (0.03)) { fx.strum = (StrumMode) pick (0, numStrumModes - 1); fx.strumRate = pick (0, numStrumRates - 1); }
        if (chance (0.03)) { fx.arp = chance (0.4) ? ArpPattern::Off : (ArpPattern) pick (1, numArpPatterns - 1); fx.arpRange = pick (1, 3); }
        if (chance (0.03)) { fx.arpRate = pick (0, numPatternRates - 1); fx.arpGate = unit(); }
        if (chance (0.03)) fx.repeat = { chance (0.5), pick (0, numPatternRates - 1), unit() };
        if (chance (0.02)) fx.seq.on = ! fx.seq.on;
        if (chance (0.03))
        {
            fx.seq.pattern = pick (0, numSeqPatterns - 1);
            fx.seq.swing = unit();
            fx.seq.gate = unit();
        }
        if (chance (0.02)) fx.velocity = { (VelocityCurve) pick (0, numVelocityCurves - 1), pick (1, 127), unit() };
        if (chance (0.02)) fx.hold = (HoldMode) pick (0, numHoldModes - 1);
        if (chance (fxRun ? 0.03 : 0.01))
        {
            rig.transport.looping = chance (fxRun ? 0.9 : 0.6);
            rig.transport.loopStart = std::floor (unit() * 8.0);
            rig.transport.loopEnd = rig.transport.loopStart + (chance (0.2) ? 0.02 + unit() * 0.2 : 0.5 + std::floor (unit() * 8.0));
            rig.transport.ppq = rig.transport.loopStart + unit() * (rig.transport.loopEnd - rig.transport.loopStart);
        }
        if (fxRun)
        {
            if (fx.arp == ArpPattern::Off && fx.strum == StrumMode::Off && ! fx.repeat.on && ! fx.seq.on)
                fx.repeat.on = true;
            if (! rig.transport.playing && chance (0.1))
                rig.transport.playing = true;
            if (fx.hold == HoldMode::Off && chance (0.1))
                fx.hold = chance (0.5) ? HoldMode::Latch : HoldMode::Bar;   // keeps chords playing
        }
        // Phase 7: voice leading while notes are held.
        if (chance (0.02)) fx.voiceLeading = chance (0.6) ? VoiceLeadingMode::Smooth : VoiceLeadingMode::Off;
        // Key detection: Analyse mode on and off while notes are held.
        if (chance (0.01)) rig.settings.analysing = ! rig.settings.analysing;

        if (chance (0.005))
        {
            const int sig = pick (0, 3);
            rig.transport.numerator = sig == 0 ? 4 : sig == 1 ? 3 : sig == 2 ? 7 : 5;
            rig.transport.denominator = sig == 2 ? 8 : 4;
        }

        rig.run (in, numSamples);
        const auto& perf = rig.engine.getPerformer();
        fxBlocks += perf.aliveCount() > 0 && (fx.arp != ArpPattern::Off || fx.strum != StrumMode::Off || fx.repeat.on || fx.seq.on) ? 1 : 0;
        wrapBlocks += rig.engine.getClock().numSegments() > 1 ? 1 : 0;
        if (perf.queued() > 4 * perf.aliveCount())
        {
            check (false, "fuzz seed " + std::to_string (seed) + ", block " + std::to_string (b) + ": " + std::to_string (perf.queued())
                              + " events queued for " + std::to_string (perf.aliveCount()) + " chord performances");
            return;
        }
        maxSounding = std::max (maxSounding, (int) rig.rx.on.size());

        if ((int) rig.rx.on.size() != rig.engine.getVoices().soundingNotes() || ! rig.rx.clean())
        {
            check (false, "fuzz seed " + std::to_string (seed) + ", block " + std::to_string (b)
                              + ": receiver and VoiceTracker disagree: " + rig.rx.describe() + " vs tracker "
                              + std::to_string (rig.engine.getVoices().soundingNotes()));
            return;
        }
    }

    // Let go of everything and stop.
    std::vector<MidiEvent> ups;
    for (auto [ch, note] : keysDown)
        ups.push_back (MidiEvent::noteOff (ch, note, 0));
    rig.run (ups, 64);
    rig.settings.fx.hold = HoldMode::Off;   // a hold made while stopped survives a stopped transport
    rig.run ({}, 64);
    rig.transport.playing = false;
    rig.run ({}, 64);
    rig.settings.testPattern = false;
    rig.run ({}, 64);

    check (rig.rx.clean() && rig.rx.on.empty() && rig.engine.getVoices().isEmpty() && rig.rx.noteOns == rig.rx.noteOffs,
           "fuzz seed " + std::to_string (seed) + ", " + std::to_string (blocks) + " blocks (" + std::to_string (fxBlocks)
               + " with timed effects, "
               + std::to_string (wrapBlocks) + " loop wraps; peak " + std::to_string (maxSounding)
               + " notes): " + rig.rx.describe());
}

int main()
{
    testRouter();
    testVoiceTracker();
    testChords();

    std::cout << "== test pattern ==\n";
    testPatternTiming (48000.0, 120.0, 256);
    testPatternTiming (48000.0, 120.0, 333);
    testPatternTiming (44100.0, 97.0, 512);
    testPatternTiming (96000.0, 174.0, 64);
    testPatternTiming (44100.0, 120.0, 2048);
    testPatternTiming (44100.0, 120.0, 32, 2.75);
    testPatternLoop();

    testProgressions();
    testKeyOctave();

    testSnapshotExchange();

    std::cout << "== stuck-note fuzz ==\n";
    for (uint32_t seed = 1; seed <= 8; ++seed)
        testFuzz (seed, 5000);
    for (uint32_t seed = 101; seed <= 108; ++seed)
        testFuzz (seed, 5000, true);

    return testutil::finish ("test_engine");
}
