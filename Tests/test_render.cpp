// Pattern render tests (PLAN.md §3.6, §7; Phase 6, G9): a pattern's drag-out MIDI is the notes
// the engine plays from it under a host (FakeHost.h), to the tick: chords only, with a style and
// bass, strummed and humanized, swung, at other tempos and meters; the file round-trips through
// the MIDI writer and reader; an empty pattern renders nothing.
#include "FakeHost.h"
#include "core/midiio/PatternRender.h"
#include "core/model/ChordSets.h"

using namespace fakehost;
using testutil::check;

namespace
{
    Pattern progression (const std::string& t, int key = 0)
    {
        Pattern p;
        std::string error;
        parseProgression (t, key, p, error);
        return p;
    }

    // The notes a host records from the pattern over one loop, as a song (pairs on/off).
    MidiSong hosted (const Workspace& ws, int index, EngineSettings s, double bpm, double rate, int block, int num = 4, int den = 4)
    {
        Host h (rate, bpm);
        h.numerator = num;
        h.denominator = den;
        h.engine->setWorkspace (ws);
        s.play = true;
        s.pattern = index;
        h.settings = s;
        const double beats = ws.patterns[(size_t) index].totalHalfBeats() * 0.5;
        h.run (h.at (beats), block);
        h.stop (block);
        MidiSong song;
        std::vector<std::vector<size_t>> open (128);
        const int64_t last = (int64_t) std::llround (beats * 960);
        for (const auto& hit : h.hits)
        {
            const auto tick = (int64_t) std::llround ((double) hit.sample / h.spq() * 960);
            if (hit.on && tick >= last)
                continue;                 // a step on the loop's end belongs to the next loop
            if (hit.on)
            {
                open[(size_t) hit.note].push_back (song.notes.size());
                song.notes.push_back ({ tick, -1, (uint8_t) hit.note, (uint8_t) hit.velocity, 0 });
            }
            else if (! open[(size_t) hit.note].empty())
            {
                song.notes[open[(size_t) hit.note].front()].end = std::min (tick, last);
                open[(size_t) hit.note].erase (open[(size_t) hit.note].begin());
            }
        }
        std::stable_sort (song.notes.begin(), song.notes.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        return song;
    }

    // The same notes within `tolerance` ticks (a sample's rounding at another rate and block).
    bool same (const MidiSong& a, const MidiSong& b, int64_t tolerance, std::string& why)
    {
        if (a.notes.size() != b.notes.size())
        {
            why = std::to_string (a.notes.size()) + " vs " + std::to_string (b.notes.size()) + " notes";
            return false;
        }
        for (size_t i = 0; i < a.notes.size(); ++i)
        {
            const auto& x = a.notes[i];
            const auto& y = b.notes[i];
            if (x.pitch != y.pitch || x.velocity != y.velocity || std::llabs (x.start - y.start) > tolerance || std::llabs (x.end - y.end) > tolerance)
            {
                why = "note " + std::to_string (i) + ": " + std::to_string (x.pitch) + "@" + std::to_string (x.start) + "-" + std::to_string (x.end)
                    + " v" + std::to_string (x.velocity) + " vs " + std::to_string (y.pitch) + "@" + std::to_string (y.start) + "-"
                    + std::to_string (y.end) + " v" + std::to_string (y.velocity);
                return false;
            }
        }
        return true;
    }

    int styleIndex (const char* name)
    {
        for (int i = 0; i < numStyles; ++i)
            if (std::string (styleInfo (i).name) == name)
                return i;
        return 0;
    }
}

static void testChordsOnly()
{
    std::cout << "== chords only ==\n";
    Workspace ws;
    ws.patterns[1] = progression ("I V vi:2 IV:6");
    EngineSettings s;
    s.fx.style.style = styleIndex ("Eighths");     // left out
    s.fx.strum = StrumMode::Up;
    const auto song = renderPattern (ws, 1, s, 120.0, 4, 4, true);
    struct N { int64_t start, end; int pitch; };
    const std::vector<N> want {
        { 0, 3840, 48 }, { 0, 3840, 52 }, { 0, 3840, 55 },
        { 3840, 7680, 55 }, { 3840, 7680, 59 }, { 3840, 7680, 62 },
        { 7680, 9600, 57 }, { 7680, 9600, 60 }, { 7680, 9600, 64 },
        { 9600, 15360, 53 }, { 9600, 15360, 57 }, { 9600, 15360, 60 },
    };
    bool ok = song.notes.size() == want.size();
    for (size_t i = 0; ok && i < want.size(); ++i)
        ok = song.notes[i].start == want[i].start && song.notes[i].end == want[i].end && song.notes[i].pitch == want[i].pitch && song.notes[i].velocity == 100;
    check (ok, "B = I V vi:2 IV:6, chords only: C G Am F held for 4, 4, 2 and 6 beats, ticks exact (" + std::to_string (song.notes.size()) + " notes)");
    check (song.bpm == 120.0 && song.ppq == 960 && song.name == "ChordMonkey", "the song's tempo, resolution and name");
    check (renderPattern (ws, 0, s, 120.0).notes.empty() && renderPattern (ws, 7, s, 120.0).notes.empty(), "an empty pattern, or no such pattern, renders nothing");
}

static void testMatchesTheHost()
{
    std::cout << "== the render is what the host plays ==\n";
    Workspace ws;
    ws.patterns[0] = progression ("I V vi IV");
    ws.patterns[2] = progression ("ii7:2 V7:2 Imaj7:6 VI7:2", 3);
    struct Case { const char* what; int pattern; double bpm; int num, den; EngineSettings s; };
    std::vector<Case> cases;
    {
        EngineSettings s;
        s.fx.style.style = styleIndex ("Bass & chord");
        s.fx.bass = BassMode::RootFifth;
        s.fx.voicing = VoicingMode::Open;
        cases.push_back ({ "Bass & chord, Root + 5th, Open", 0, 120.0, 4, 4, s });
    }
    {
        EngineSettings s;
        s.fx.style.style = styleIndex ("Charleston");
        s.fx.style.swing = 0.5;
        s.fx.style.humanize = 1.0;
        s.fx.strum = StrumMode::Up;
        s.fx.strumRate = 4;
        s.fx.velocity.curve = VelocityCurve::House;
        cases.push_back ({ "Charleston swung, humanized, strummed, House velocity", 2, 97.0, 4, 4, s });
    }
    {
        EngineSettings s;
        s.fx.arp = ArpPattern::LinearUp;
        s.fx.arpRate = 5;
        s.fx.voiceLeading = VoiceLeadingMode::Smooth;
        cases.push_back ({ "Arp 1/16 with Smooth voice leading", 0, 140.0, 4, 4, s });
    }
    {
        EngineSettings s;
        s.fx.style.style = styleIndex ("Quarters");
        s.chordOctave = 1;
        cases.push_back ({ "Quarters in 3/4, Chord Octave +1", 0, 120.0, 3, 4, s });
    }
    for (const auto& c : cases)
    {
        const auto rendered = renderPattern (ws, c.pattern, c.s, c.bpm, c.num, c.den);
        bool all = true;
        std::string why;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 333, 2048 })
            {
                const MidiSong host = hosted (ws, c.pattern, c.s, c.bpm, rate, block, c.num, c.den);
                // A sample at 44.1 kHz is under half a tick at these tempos; allow one tick for rounding.
                if (! same (rendered, host, 1, why))
                {
                    all = false;
                    why = std::to_string ((int) rate) + "/" + std::to_string (block) + ": " + why;
                    break;
                }
            }
        check (all && ! rendered.notes.empty(), std::string (c.what) + ": " + std::to_string (rendered.notes.size())
                                                    + " notes, the same as a host records at 44.1/48/96 kHz and 64/333/2048-sample blocks, to the tick"
                                                    + (all ? "" : " (" + why + ")"));
    }

    const auto song = renderPattern (ws, 2, cases[1].s, 97.0);
    const auto bytes = writeMidiFile (song);
    MidiSong back;
    std::string error;
    const bool read = readMidiFile (bytes.data(), bytes.size(), back, error);
    std::string why;
    check (read && same (song, back, 0, why) && std::abs (back.bpm - 97.0) < 0.01, "the .mid reads back: the same notes and tempo" + (why.empty() ? "" : " (" + why + ")"));
}

int main()
{
    testChordsOnly();
    testMatchesTheHost();
    return testutil::finish ("test_render");
}
