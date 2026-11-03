// Phase 6: Export (ChordChimp's PLAN.md §11; the design is Chordism's PLAN.md §4.6). Ported from Chordism's
// test_midiio.cpp without its MIDI import.
//
//   file        writeMidiFile → readMidiFile keeps every note, the tempo and the time signature;
//               hand-made files (type 0, running status, note-on velocity 0, other messages)
//               read right; files that aren't readable MIDI are refused
//   recorder    Export keeps only completed bars, the last 32, from the take's first bar line;
//               notes still sounding are cut at the last bar line; loops run on like a tape; a
//               new take replaces the old; the file's notes are the engine's output, to the tick
#include "FakeHost.h"
#include "core/midiio/ExportRecorder.h"
#include "core/midiio/MidiFile.h"
#include <map>

using namespace chordmonkey;
using namespace fakehost;
using testutil::check;

namespace
{
    bool sameNotes (const MidiNote& a, const MidiNote& b, int64_t tolerance = 0)
    {
        return a.pitch == b.pitch && a.velocity == b.velocity && a.channel == b.channel
            && std::llabs (a.start - b.start) <= tolerance && std::llabs (a.end - b.end) <= tolerance;
    }

    //==========================================================================
    // Export through the fake host.

    struct Take
    {
        std::unique_ptr<ExportRecorder> recorder = std::make_unique<ExportRecorder>();

        void attach (Host& h)
        {
            h.observe = [this] (const Transport& t, int n, const BlockEvents& out) { recorder->record (t, n, out.begin(), out.size()); };
        }

        MidiSong song (int maxBars = ExportRecorder::maxBars) const
        {
            TakeInfo info;
            std::vector<RecordedEvent> events;
            recorder->snapshot (info, events);
            return exportBars (info, events, maxBars);
        }
    };

    // The host's notes (paired in order per pitch) in beats [from, to), as file notes from `from`
    // (ticks at 960), cut at `to`. Constant tempo; ppq `startPpq` at sample 0.
    std::vector<MidiNote> hostNotes (const Host& h, double from, double to, double startPpq = 0.0)
    {
        std::map<int, std::vector<std::pair<double, int>>> open;
        std::vector<MidiNote> notes;
        const auto tick = [&] (double beat) { return (int64_t) std::llround ((beat - from) * 960.0); };
        for (const auto& hit : h.hits)
        {
            const double beat = startPpq + (double) hit.sample / h.spq();
            if (hit.on)
            {
                open[hit.note].push_back ({ beat, hit.velocity });
                continue;
            }
            auto& o = open[hit.note];
            if (o.empty())
                continue;
            const auto [start, velocity] = o.front();
            o.erase (o.begin());
            if (tick (start) < 0 || tick (start) >= tick (to))
                continue;
            notes.push_back ({ tick (start), std::min (tick (beat), tick (to)), (uint8_t) hit.note, (uint8_t) velocity, 0 });
        }
        for (auto& [note, o] : open)
            for (const auto& [start, velocity] : o)
                if (tick (start) >= 0 && tick (start) < tick (to))
                    notes.push_back ({ tick (start), tick (to), (uint8_t) note, (uint8_t) velocity, 0 });
        std::stable_sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        return notes;
    }

    // "" if the song's notes are `want` (within `tolerance` ticks), else the first difference.
    std::string compareNotes (const std::vector<MidiNote>& got, const std::vector<MidiNote>& want, int64_t tolerance = 1)
    {
        if (got.size() != want.size())
            return " " + std::to_string (got.size()) + " notes, want " + std::to_string (want.size());
        for (size_t i = 0; i < got.size(); ++i)
            if (! sameNotes (got[i], want[i], tolerance))
                return " note " + std::to_string (i) + ": pitch " + std::to_string (got[i].pitch) + " " + std::to_string (got[i].start) + "-"
                     + std::to_string (got[i].end) + ", want " + std::to_string (want[i].pitch) + " " + std::to_string (want[i].start) + "-"
                     + std::to_string (want[i].end);
        return {};
    }

    //==========================================================================
    void testMidiFile()
    {
        std::cout << "== MIDI files ==\n";
        MidiSong a;
        a.ppq = 960;
        a.bpm = 97.5;
        a.numerator = 3;
        a.denominator = 4;
        a.name = "test";
        a.notes = { { 0, 480, 60, 100, 0 }, { 0, 480, 64, 90, 0 }, { 480, 1920, 67, 80, 1 }, { 1920, 1921, 127, 1, 15 }, { 5000, 9000, 0, 127, 3 } };
        const auto bytes = writeMidiFile (a);
        MidiSong b;
        std::string error;
        const bool read = readMidiFile (bytes.data(), bytes.size(), b, error);
        bool same = read && b.ppq == 960 && std::abs (b.bpm - 97.5) < 0.01 && b.numerator == 3 && b.denominator == 4 && b.name == "test"
                 && b.notes.size() == a.notes.size();
        for (size_t i = 0; same && i < a.notes.size(); ++i)
            same = sameNotes (a.notes[i], b.notes[i]);
        check (same, "write → read keeps every note (pitch, velocity, channel, ticks), 960 PPQ, the tempo and 3/4");
        check (bytes.size() > 22 && std::string ((const char*) bytes.data(), 4) == "MThd" && bytes[9] == 1 && bytes[11] == 2
                   && bytes[12] == 0x03 && bytes[13] == 0xc0,
               "the file is type 1 with two tracks at 960 ticks per quarter");

        // A hand-made type 0 file at 480 PPQ: running status, note-on velocity 0 as note-off,
        // a program change (one data byte), a controller and a SysEx in between.
        const std::vector<uint8_t> type0 = {
            'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0,
            'M', 'T', 'r', 'k', 0, 0, 0, 38,
            0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,      // 120 BPM
            0x00, 0x90, 0x3c, 0x64,                        // C4 on
            0x00, 0x40, 0x50,                              // E4 on (running status)
            0x83, 0x60, 0x3c, 0x00,                        // 480 ticks later: C4 off (velocity 0)
            0x00, 0x40, 0x00,                              // E4 off
            0x00, 0xc0, 0x05,                              // program change
            0x00, 0xb0, 0x07, 0x64,                        // volume
            0x00, 0xf0, 0x03, 0x01, 0x02, 0xf7,            // SysEx
            0x00, 0xff, 0x2f, 0x00
        };
        MidiSong c;
        const bool ok0 = readMidiFile (type0.data(), type0.size(), c, error);
        check (ok0 && c.ppq == 480 && std::abs (c.bpm - 120.0) < 1.0e-6 && c.notes.size() == 2 && c.notes[0].pitch == 60 && c.notes[0].end == 480
                   && c.notes[1].pitch == 64 && c.notes[1].velocity == 0x50 && c.notes[1].end == 480,
               "a type 0 file with running status, velocity-0 note-offs and other messages reads right" + (ok0 ? std::string() : ": " + error));

        // The same pitch struck twice before either ends: the first on ends first.
        MidiSong overlap;
        overlap.notes = { { 0, 960, 60, 100, 0 }, { 240, 480, 60, 90, 0 } };
        const auto ob = writeMidiFile (overlap);
        MidiSong od;
        readMidiFile (ob.data(), ob.size(), od, error);
        check (od.notes.size() == 2 && od.notes[0].end == 480 && od.notes[1].end == 960,
               "overlapping notes on one pitch pair in order (first on, first off)");

        std::vector<uint8_t> smpte (type0);
        smpte[12] = 0xe7;
        smpte[13] = 0x28;
        MidiSong x;
        check (! readMidiFile (smpte.data(), smpte.size(), x, error), "an SMPTE-timed file is refused (" + error + ")");
        const std::vector<uint8_t> riff = { 'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E', 0, 0 };
        check (! readMidiFile (riff.data(), riff.size(), x, error), "a file that isn't MIDI is refused (" + error + ")");
        std::vector<uint8_t> truncated (type0.begin(), type0.begin() + 30);
        check (! readMidiFile (truncated.data(), truncated.size(), x, error), "a truncated file is refused (" + error + ")");
    }

    //==========================================================================
    EngineSettings testPatternSettings()
    {
        EngineSettings s;
        s.testPattern = true;
        return s;
    }

    void testRecorder()
    {
        std::cout << "== Export recorder ==\n";

        // Nothing to export until the first bar has finished.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (3.9), 512);
            const int before = take.recorder->info().completedBars();
            const bool emptyBefore = take.song().notes.empty();
            h.run (h.at (4.05), 512);
            check (before == 0 && emptyBefore && take.recorder->info().completedBars() == 1 && take.song().notes.size() == 12,
                   "the handle has nothing until the first bar has finished, then one bar (4 chords × 3 notes)");
        }

        // 6.5 bars, then stop: six bars, every note as the engine sent it.
        const auto all = configs();
        for (const auto& c : all)
        {
            Host h (c.rate, c.bpm);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (26.0), c.block);
            h.stop (c.block);
            const auto song = take.song();
            const std::string why = compareNotes (song.notes, hostNotes (h, 0.0, 24.0));
            if (! why.empty() || song.notes.size() != 72 || song.ppq != 960 || std::abs (song.bpm - c.bpm) > 1.0e-9)
            {
                check (false, "6.5 bars, stopped: the file holds bars 1-6 exactly (" + describe (c) + "):" + why + " "
                                  + std::to_string (song.notes.size()) + " notes at " + std::to_string (song.bpm) + " BPM");
                break;
            }
            if (&c == &all.back())
                check (true, "6.5 bars, stopped: the file holds bars 1-6, every note to the tick, in all configurations");
        }

        // 40 bars: the last 32.
        {
            Host h (44100.0, 120.0);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (161.0), 512);
            const auto song = take.song();
            check (compareNotes (song.notes, hostNotes (h, 32.0, 160.0)).empty() && song.notes.front().start == 0,
                   "40 bars: the file holds the last 32 (bars 9-40), starting at its first bar line");
        }

        // A chord held across the end is cut at the last bar line; one started before the first
        // exported bar is left out.
        {
            Host h (48000.0, 120.0);
            Take take;
            take.attach (h);
            h.key (2.0, 48, true);        // C (I) from beat 2…
            h.key (9.5, 48, false);
            h.key (5.0, 50, true);        // …and D (ii) from beat 5
            h.key (5.5, 50, false);
            h.run (h.at (9.75), 256);
            const auto two = take.song();
            bool cut = two.notes.size() == 6;
            for (const auto& n : two.notes)
                cut &= n.start == 2 * 960 ? n.end == 8 * 960 : (n.start == 5 * 960 && n.end == 5 * 960 + 480);
            const auto one = take.song (1);
            check (cut && one.notes.size() == 3 && one.notes[0].start == 960,
                   "a chord held past the last bar line is cut there; with one bar, the chord begun before it is left out");
        }

        // A one-bar loop played three times: three bars of the same notes.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            h.looping = true;
            h.loopStart = 0.0;
            h.loopEnd = 4.0;
            Take take;
            take.attach (h);
            h.run (h.at (12.0) + 10, 333);
            h.stop();
            const auto song = take.song();
            bool same = song.notes.size() == 36;
            for (size_t i = 0; same && i < 12; ++i)
                for (int pass = 1; pass < 3; ++pass)
                {
                    MidiNote moved = song.notes[i];
                    moved.start += pass * 4 * 960;
                    moved.end += pass * 4 * 960;
                    same &= sameNotes (song.notes[i + 12 * (size_t) pass], moved, 1);
                }
            check (same, "a loop runs on like a tape: three passes of a one-bar loop are three equal bars");
        }

        // Playback started on beat 3: the take starts at the bar line before it.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            h.ppq = 6.0;
            Take take;
            take.attach (h);
            h.run (h.at (6.2), 512);
            const auto info = take.recorder->info();
            const auto song = take.song();
            check (info.startBeat == 2.0 && info.completedBars() == 2 && compareNotes (song.notes, hostNotes (h, 4.0, 12.0, 6.0)).empty()
                       && song.notes.front().start == 2 * 960,
                   "a take started on beat 3 of bar 2 begins at bar 2's line: its first chord is at beat 3 of the file");
        }

        // A new take replaces the old one.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (8.5), 512);
            h.stop (512);
            const auto first = take.recorder->info().take;
            h.playing = true;
            h.ppq = 0.0;
            const int64_t start = h.pos;
            h.settings.key = 2;           // D major: other notes
            h.run (start + h.at (4.5), 512);
            const auto song = take.song();
            bool fromSecond = take.recorder->info().take == first + 1 && song.notes.size() == 12;
            for (const auto& n : song.notes)
                fromSecond &= n.pitch % 12 == 2 || n.pitch % 12 == 6 || n.pitch % 12 == 9 || n.pitch % 12 == 4 || n.pitch % 12 == 7
                           || n.pitch % 12 == 11 || n.pitch % 12 == 1;
            check (fromSecond && song.notes[0].pitch == 50, "playing again starts a new take: only its notes are exported");
        }
    }
}

int main()
{
    testMidiFile();
    testRecorder();
    return testutil::finish ("test_export");
}
