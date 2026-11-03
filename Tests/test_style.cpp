// Voicing, Bass, Style, Swing and Humanize tests (PLAN.md §3.4, §7; Phase 4, G6): voicings and
// bass notes by hand; every style's table (sorted, inside the bar, never overlapping, at every
// swing); the voices a hit takes; and the engine under a fake host (FakeHost.h) at 32-2048-sample
// blocks, 44.1-96 kHz and three tempos: styles on a held key and on the pattern, Bass under a
// style, swing, a style replacing Repeat, Humanize's delays and velocities (the same at every
// block size, never early, at most 15 ms), and a stuck-note fuzz.
//
// Expected notes are written out by hand (MIDI 48 = C2, the chords' default C).
#include "FakeHost.h"
#include "core/model/ChordSets.h"
#include <random>

using namespace fakehost;
using testutil::check;

namespace
{
    using Notes = std::vector<int>;
    const Notes C { 48, 52, 55 }, G { 55, 59, 62 };

    ChordNotes notesOf (Notes v)
    {
        ChordNotes c;
        for (int n : v)
            c.notes[(size_t) c.size++] = (uint8_t) n;
        return c;
    }

    Notes list (const ChordNotes& c) { return Notes (c.begin(), c.end()); }

    int styleIndex (const char* name)
    {
        for (int i = 0; i < numStyles; ++i)
            if (std::string (styleInfo (i).name) == name)
                return i;
        return -1;
    }

    Pattern progression (const std::string& t)
    {
        Pattern p;
        std::string error;
        parseProgression (t, 0, p, error);
        return p;
    }

    void stopAndEnd (Host& h, Expect& e, std::initializer_list<Notes> sounding, int block)
    {
        const int64_t at = h.pos;
        h.stop (block);
        for (const auto& notes : sounding)
            for (int n : notes)
                e.offAt (at, n);
    }
}

static void testVoicings()
{
    std::cout << "== voicings and bass ==\n";
    check (list (applyVoicing (notesOf ({ 55, 48, 52 }), VoicingMode::Close)) == Notes { 48, 52, 55 }, "Close: sorted, as written");
    check (list (applyVoicing (notesOf ({ 48, 52, 55 }), VoicingMode::Open)) == Notes { 40, 48, 55 }, "Open on C: E down an octave (40 48 55)");
    check (list (applyVoicing (notesOf ({ 48, 52, 55 }), VoicingMode::Spread)) == Notes { 48, 64, 67 }, "Spread on C: 48 64 67");
    check (list (applyVoicing (notesOf ({ 48, 52, 55 }), VoicingMode::Wide)) == Notes { 40, 48, 55 }, "Wide on a triad: drop-2 only");
    check (list (applyVoicing (notesOf ({ 48, 52, 55, 59 }), VoicingMode::Open)) == Notes { 43, 48, 52, 59 }, "Open on Cmaj7: drop-2 (43 48 52 59)");
    check (list (applyVoicing (notesOf ({ 48, 52, 55, 59, 62 }), VoicingMode::Wide)) == Notes { 40, 47, 48, 55, 62 },
           "Wide on Cmaj9: drop-2 (B) and drop-4 (E) (40 47 48 55 62)");
    check (list (applyVoicing (notesOf ({ 5, 9, 12 }), VoicingMode::Open)) == Notes { 5, 9, 12 }
               && list (applyVoicing (notesOf ({ 118, 120, 124 }), VoicingMode::Spread)) == Notes { 118, 120, 124 },
           "a note that would leave 0-127 stays");
    check (list (applyVoicing (notesOf ({ 48, 60, 60, 64 }), VoicingMode::Close)) == Notes { 48, 60, 64 }, "a repeated pitch is kept once");

    int b[2] {};
    const int none = bassNotes (BassMode::Off, 48, 0, b);
    const int one = bassNotes (BassMode::Root, 48, 0, b);
    const int c1 = b[0];
    const int two = bassNotes (BassMode::RootFifth, 48, 0, b);
    check (none == 0 && one == 1 && c1 == 36 && two == 2 && b[0] == 36 && b[1] == 43, "Bass on C (lowest 48): Root C1 (36); Root + 5th 36 43");
    bassNotes (BassMode::Root, 57, 9, b);
    const int am = b[0];
    bassNotes (BassMode::Root, 52, 0, b);
    check (am == 45 && b[0] == 36, "Bass on Am (57): A1 (45); on C/E (lowest 52): C1 (36), the root an octave below or more");
    check (bassNotes (BassMode::Root, 10, 0, b) == 0, "no bass below MIDI 0");
}

static void testStyleTables()
{
    std::cout << "== style tables ==\n";
    check (styleIndex ("Block") == 0 && ! StyleSettings {}.on() && numStyles == 18, "18 styles; Block (the first) has no hits");
    int bad = 0;
    std::string first;
    for (int s = 1; s < numStyles; ++s)
        for (double swing : { 0.0, 0.25, 0.5, 0.75 })
        {
            StyleSettings set;
            set.style = s;
            set.swing = swing;
            const StyleGrid g (set, 0.0, 4.0);
            double last = -1.0, lastEnd = 0.0;
            for (int i = 0; i < styleInfo (s).numHits; ++i)
            {
                const auto w = g.hit (0.0, i);
                const auto& h = styleInfo (s).hits[(size_t) i];
                const bool ok = w.valid && w.start > last && w.start >= lastEnd - 1.0e-9 && w.end > w.start && w.end <= 4.0 + 1.0e-9
                             && h.velocity >= 1 && h.velocity <= 127 && w.start >= h.start - 1.0e-9 && w.start <= h.start + 0.25 + 1.0e-9;
                if (! ok && bad++ == 0)
                    first = std::string (styleInfo (s).name) + " hit " + std::to_string (i) + " at swing " + std::to_string (swing);
                last = w.start;
                lastEnd = w.end;
                // Every point of a hit finds it; the next start after its start is the next hit.
                const auto at = g.windowAt (w.start + 0.5 * (w.end - w.start));
                double next = 0.0;
                if (! at.valid || at.start != w.start || ! g.nextStart (w.start, true, next) || std::abs (next - w.start) > 1.0e-9)
                    if (bad++ == 0)
                        first = std::string (styleInfo (s).name) + " hit " + std::to_string (i) + ": windowAt/nextStart";
            }
        }
    check (bad == 0, "every style at swing 0/25/50/75%: hits in order, never overlapping, inside the bar, swung by at most a sixteenth"
                         + (first.empty() ? std::string() : " (first: " + first + ")"));

    StyleSettings off;
    off.style = styleIndex ("Off-beats");
    off.swing = 0.5;
    const auto w = StyleGrid (off, 0.0, 4.0).hit (0.0, 0);
    StyleSettings six;
    six.style = styleIndex ("Sixteenths");
    six.swing = 0.75;
    const auto s1 = StyleGrid (six, 0.0, 4.0).hit (0.0, 1);
    check (std::abs (w.start - (0.5 + 1.0 / 6.0)) < 1.0e-9 && std::abs ((w.end - w.start) - 0.4) < 1.0e-6 && std::abs (s1.start - 0.375) < 1.0e-9
               && std::abs (s1.end - 0.575) < 1.0e-6,
           "swing 50%: the off-beat eighth at 2/3 of the beat, keeping its length; 75% on a sixteenth: 0.375 to 0.575");
    StyleSettings waltz;
    waltz.style = styleIndex ("Quarters");
    check (! StyleGrid (waltz, 0.0, 3.0).hit (0.0, 3).valid && StyleGrid (waltz, 0.0, 3.0).hit (3.0, 0).valid, "in 3/4, a hit on beat 4 doesn't sound; the bar repeats every 3 beats");

    check (styleVoiceMask (StyleVoices::All, 3) == 7 && styleVoiceMask (StyleVoices::Low, 4) == 1 && styleVoiceMask (StyleVoices::Upper, 4) == 14
               && styleVoiceMask (StyleVoices::Upper, 1) == 1 && styleVoiceMask (StyleVoices::Top, 3) == 4 && styleVoiceMask (StyleVoices::Top + 1, 3) == 2
               && styleVoiceMask (3, 3) == 1 && styleVoiceMask (4, 3) == 2,
           "voices: All, Low, Upper (a single note: itself), Top, the 2nd from the top, the n-th wrapping");
}

static void testStylesInTheEngine()
{
    std::cout << "== styles in the engine ==\n";
    sweep ("Quarters on a held C: four strikes a bar, 0.9 beats each, at 100/85/95/85%", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.style.style = styleIndex ("Quarters");
        h.key (0.0, 48, true);
        h.key (4.5, 48, false);
        h.run (h.at (5.0), c.block);
        Expect e (h.spq());
        const int vel[4] = { 100, 85, 95, 85 };
        for (int b = 0; b < 4; ++b)
            for (int n : C)
                e.note (b, b + 0.9, n, vel[b]);
        for (int n : C)
            e.note (4.0, 4.5, n, 100);
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });

    sweep ("Bass & chord with Bass Root: C1 on 1 and 3, the chord on 2 and 4", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.style.style = styleIndex ("Bass & chord");
        h.settings.fx.bass = BassMode::Root;
        h.key (0.0, 48, true);
        h.key (3.95, 48, false);
        h.run (h.at (4.5), c.block);
        Expect e (h.spq());
        e.note (0, 0.9, 36, 105);
        e.note (2, 2.9, 36, 100);
        for (int n : C)
        {
            e.note (1, 1.9, n, 90);
            e.note (3, 3.9, n, 90);
        }
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });

    sweep ("Off-beats with swing 50%: each hit 2/3 into its beat", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.style.style = styleIndex ("Off-beats");
        h.settings.fx.style.swing = 0.5;
        h.key (0.0, 48, true);
        h.key (2.0, 48, false);
        h.run (h.at (2.5), c.block);
        Expect e (h.spq());
        for (int n : C)
        {
            e.note (2.0 / 3.0, 2.0 / 3.0 + 0.4, n, 95);
            e.note (1 + 2.0 / 3.0, 2.0, n, 90);   // the key-up ends it
        }
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });

    sweep ("the pattern in Half notes with Voicing Open: I then V, each struck twice", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        Workspace ws;
        ws.patterns[0] = progression ("I V");
        h.engine->setWorkspace (ws);
        h.settings.play = true;
        h.settings.fx.style.style = styleIndex ("Half notes");
        h.settings.fx.voicing = VoicingMode::Open;
        h.run (h.at (7.95), c.block);
        Expect e (h.spq());
        const Notes cOpen { 40, 48, 55 }, gOpen { 47, 55, 62 };
        for (int n : cOpen)
        {
            e.note (0, 1.9, n, 100);
            e.note (2, 3.9, n, 90);
        }
        for (int n : gOpen)
        {
            e.note (4, 5.9, n, 100);
            e.note (6, 7.9, n, 90);
        }
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });

    sweep ("a style replaces Repeat: Repeat 1/8 on, Style Half notes plays only the halves", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.repeat.on = true;
        h.settings.fx.repeat.rate = 3;   // 1/8
        h.settings.fx.style.style = styleIndex ("Half notes");
        h.key (0.0, 48, true);
        h.key (3.95, 48, false);
        h.run (h.at (4.5), c.block);
        Expect e (h.spq());
        for (int n : C)
        {
            e.note (0, 1.9, n, 100);
            e.note (2, 3.9, n, 90);
        }
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });

    sweep ("Humanize 100% on Quarters: each tone up to 15 ms late and up to ±12 velocity, as hashed from the song position", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.style.style = styleIndex ("Quarters");
        h.settings.fx.style.humanize = 1.0;
        h.key (0.0, 48, true);
        h.key (3.95, 48, false);
        h.run (h.at (4.5), c.block);
        Expect e (h.spq());
        const int vel[4] = { 100, 85, 95, 85 };
        for (int b = 0; b < 4; ++b)
            for (int i = 0; i < 3; ++i)
            {
                const auto j = humanize ((double) b, 48, i);
                const int64_t at = e.s (b) + (int64_t) std::llround (j.delay * maxHumanizeSeconds * c.rate);
                const int v = std::clamp (vel[b] + (int) std::lround (maxHumanizeVelocity * j.velocity), 1, 127);
                e.onAt (at, C[(size_t) i], v);
                e.off (b + 0.9, C[(size_t) i]);
            }
        stopAndEnd (h, e, {}, c.block);
        return finish (h, e, worst);
    });

    {
        // Humanize's spread, over many strikes: late only, at most 15 ms, velocities spread both ways.
        double most = 0.0, least = 1.0, vlo = 0.0, vhi = 0.0;
        for (int b = 0; b < 2000; ++b)
            for (int i = 0; i < 4; ++i)
            {
                const auto j = humanize (b * 0.25, 128, i);
                most = std::max (most, j.delay);
                least = std::min (least, j.delay);
                vlo = std::min (vlo, j.velocity);
                vhi = std::max (vhi, j.velocity);
            }
        check (least >= 0.0 && most < 1.0 && most > 0.95 && least < 0.05 && vlo < -0.95 && vhi > 0.95,
               "Humanize's hash over 8,000 tones: delays fill 0-15 ms (never early), velocities -12 … +12");
    }
}

static void testFuzz()
{
    std::cout << "== stuck-note fuzz ==\n";
    std::mt19937 rng (404);
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    int dirty = 0, played = 0;
    std::string first;
    for (int r = 0; r < 60; ++r)
    {
        Host h (std::array<double, 3> { 44100.0, 48000.0, 96000.0 }[(size_t) pick (0, 2)], 60.0 + pick (0, 140));
        Workspace ws;
        ws.patterns[0] = progression ("I V vi IV");
        ws.patterns[1] = progression ("ii7:2 V7:2 Imaj7");
        h.engine->setWorkspace (ws);
        h.looping = chance (0.3);
        h.loopStart = pick (0, 4);
        h.loopEnd = h.loopStart + pick (1, 8) * 0.5;
        for (int step = 0; step < 120; ++step)
        {
            const int block = chance (0.3) ? pick (1, 4096) : 1 << pick (5, 11);
            if (chance (0.1)) h.settings.fx.style.style = pick (0, numStyles - 1);
            if (chance (0.05)) h.settings.fx.style.swing = pick (0, 75) / 100.0;
            if (chance (0.05)) h.settings.fx.style.humanize = pick (0, 100) / 100.0;
            if (chance (0.05)) h.settings.fx.voicing = (VoicingMode) pick (0, numVoicingModes - 1);
            if (chance (0.05)) h.settings.fx.bass = (BassMode) pick (0, numBassModes - 1);
            if (chance (0.04)) h.settings.fx.strum = (StrumMode) pick (0, numStrumModes - 1);
            if (chance (0.04)) h.settings.fx.arp = chance (0.6) ? ArpPattern::Off : (ArpPattern) pick (1, numArpPatterns - 1);
            if (chance (0.04)) h.settings.fx.repeat.on = ! h.settings.fx.repeat.on;
            if (chance (0.04)) h.settings.fx.seq.on = ! h.settings.fx.seq.on;
            if (chance (0.04)) h.settings.fx.hold = (HoldMode) pick (0, numHoldModes - 1);
            if (chance (0.04)) h.settings.fx.stack = (StackMode) pick (0, numStackModes - 1);
            if (chance (0.04)) h.settings.fx.voiceLeading = (VoiceLeadingMode) pick (0, numVoiceLeadingModes - 1);
            if (chance (0.08)) h.settings.play = ! h.settings.play;
            if (chance (0.05)) h.settings.pattern = pick (0, 1);
            if (chance (0.03)) h.playing = ! h.playing;
            if (chance (0.03)) h.ppq = pick (0, 64) * 0.25;
            if (chance (0.03)) h.bpm = 40 + pick (0, 200);
            for (int k = pick (0, 2); k > 0; --k)
            {
                const int note = pick (36, 84);
                h.keyAt (h.pos + pick (0, block - 1), note, true, pick (1, 127));
                h.keyAt (h.pos + pick (0, 3 * block), note, false);
            }
            h.run (h.pos + block, block);
        }
        h.playing = true;
        h.run (h.pos + 3 * 4096 + 1, 512);
        h.stop (256);
        played += h.rx.noteOns;
        if (! h.clean() && dirty++ == 0)
            first = " (first: run " + std::to_string (r) + ": " + h.rx.describe() + ")";
    }
    check (dirty == 0 && played > 2000, "60 runs of 120 random blocks (Style, Swing, Humanize, Voicing, Bass with every effect, the pattern, "
                                        "keys, loops, locates, tempo, stops): " + std::to_string (played) + " note-ons, nothing left sounding" + first);
}

int main()
{
    testVoicings();
    testStyleTables();
    testStylesInTheEngine();
    testFuzz();
    return testutil::finish ("test_style");
}
