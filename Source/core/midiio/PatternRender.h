// A pattern rendered as MIDI (PLAN.md §3.6, Phase 6): the workspace's drag-out. One loop of the
// pattern played offline through a ChordEngine of its own (the same code that plays it in the
// host), with Play on and a fake transport from song position 0, then stopped at the loop's
// end, so the clip is exactly what the host would hear: style, voicing, bass, strum, arp,
// velocity and humanize included. With `chordsOnly`, the rhythm is left out (Style Block; Arp,
// Strum, Repeat, Step seq and Humanize off): each slot's chord held for its length.
//
// Plain C++, no JUCE. Allocates (the engine is large): for the message thread.
#pragma once

#include "../engine/ChordEngine.h"
#include "MidiFile.h"
#include <memory>
#include <vector>

namespace chordmonkey
{
    inline MidiSong renderPattern (const Workspace& ws, int index, EngineSettings s, double bpm, int numerator = 4,
                                   int denominator = 4, bool chordsOnly = false, double sampleRate = 48000.0, int ppq = 960)
    {
        MidiSong song;
        song.ppq = ppq;
        song.bpm = bpm > 0.0 ? bpm : 120.0;
        song.numerator = numerator;
        song.denominator = denominator;
        song.name = "ChordMonkey";
        if (index < 0 || index >= numPatterns || ws.patterns[(size_t) index].empty())
            return song;

        s.enabled = true;
        s.analysing = false;
        s.testPattern = false;
        s.play = true;
        s.pattern = index;
        if (chordsOnly)
        {
            s.fx.style = StyleSettings {};
            s.fx.arp = ArpPattern::Off;
            s.fx.strum = StrumMode::Off;
            s.fx.repeat.on = false;
            s.fx.seq.on = false;
        }

        auto engine = std::make_unique<ChordEngine>();
        engine->setWorkspace (ws);
        const double beats = ws.patterns[(size_t) index].totalHalfBeats() * 0.5;
        const double spb = sampleRate * 60.0 / song.bpm;
        const auto total = (int64_t) std::llround (beats * spb);
        constexpr int block = 512;

        struct Event { double beat; MidiEvent e; };
        std::vector<Event> events;
        auto out = std::make_unique<BlockEvents>();
        int64_t pos = 0;
        for (bool playing = true;; playing = false)
        {
            do
            {
                const int n = playing ? (int) std::min<int64_t> (block, total - pos) : block;
                Transport t { sampleRate, playing, (double) pos / spb, song.bpm, numerator, denominator };
                out->clear();
                engine->process (s, t, nullptr, 0, n, *out);
                for (const auto& e : *out)
                    if (e.isNoteOn() || e.isNoteOff())
                        events.push_back ({ (double) (pos + e.sample) / spb, e });
                pos += n;
            } while (playing && pos < total);
            if (! playing)
                break;
        }

        // Note-ons and note-offs paired per channel and pitch, oldest first; cut at the loop's end.
        const auto tick = [&] (double beat) { return (int64_t) std::llround (beat * ppq); };
        const int64_t last = tick (beats);
        std::vector<std::vector<size_t>> open (16 * 128);
        for (const auto& ev : events)
        {
            const size_t key = (size_t) ev.e.channel() * 128 + (size_t) ev.e.note();
            if (ev.e.isNoteOn())
            {
                open[key].push_back (song.notes.size());
                song.notes.push_back ({ tick (ev.beat), -1, (uint8_t) ev.e.note(), (uint8_t) ev.e.velocity(), (uint8_t) ev.e.channel() });
            }
            else if (! open[key].empty())
            {
                auto& note = song.notes[open[key].front()];
                open[key].erase (open[key].begin());
                note.end = std::min (tick (ev.beat), last);
            }
        }
        std::vector<MidiNote> kept;
        for (auto note : song.notes)
        {
            if (note.start >= last)
                continue;
            if (note.end < 0)
                note.end = last;
            if (note.end <= note.start)
                note.end = note.start + 1;
            kept.push_back (note);
        }
        std::stable_sort (kept.begin(), kept.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        song.notes = std::move (kept);
        return song;
    }
}
