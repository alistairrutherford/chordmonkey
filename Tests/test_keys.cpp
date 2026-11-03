// Key detection tests (ChordChimp's docs/KEY_DETECTION_PLAN.md K6.1): the profiles, a corpus of tunes and
// progressions in every key, ChordMonkey's own chords, modulation, silence, ambiguous input,
// weighting, hysteresis, the scale mapping, and the engine's Analyse mode.
#include "TestUtil.h"
#include "Receiver.h"
#include "core/engine/ChordEngine.h"
#include "core/engine/KeyListener.h"
#include "core/theory/KeyProfiles.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace chordmonkey;
using testutil::check;

namespace
{
    struct Note
    {
        double start = 0.0, length = 0.0;   // seconds
        int pitch = 60, velocity = 100;
    };

    using Notes = std::vector<Note>;

    // A melody from (pitch, beats) pairs at `bpm`, each note held for 90% of its beats; pitch
    // -1 is a rest.
    Notes melody (std::initializer_list<std::pair<int, double>> tune, double bpm = 100.0, double start = 0.0)
    {
        Notes n;
        const double beat = 60.0 / bpm;
        double t = start;
        for (const auto& [pitch, beats] : tune)
        {
            if (pitch >= 0)
                n.push_back ({ t, 0.9 * beats * beat, pitch, 100 });
            t += beats * beat;
        }
        return n;
    }

    // Block chords, one per `beats`, each a list of pitches.
    Notes chords (std::initializer_list<std::pair<std::vector<int>, double>> seq, double bpm = 100.0, double start = 0.0)
    {
        Notes n;
        const double beat = 60.0 / bpm;
        double t = start;
        for (const auto& [pitches, beats] : seq)
        {
            for (const int p : pitches)
                n.push_back ({ t, 0.95 * beats * beat, p, 90 });
            t += beats * beat;
        }
        return n;
    }

    Notes transpose (Notes n, int semitones)
    {
        for (auto& x : n)
            x.pitch += semitones;
        return n;
    }

    Notes append (Notes a, const Notes& b, double offset)
    {
        for (auto x : b)
        {
            x.start += offset;
            a.push_back (x);
        }
        return a;
    }

    double endOf (const Notes& n)
    {
        double e = 0.0;
        for (const auto& x : n)
            e = std::max (e, x.start + x.length);
        return e;
    }

    struct Change
    {
        double time;
        KeyGuess guess;
    };

    // Feeds the notes to `l` in time order (note-offs first at one time), advancing every 10 ms
    // until `until`; returns each change of the key shown (tonic or mode), and when it came.
    std::vector<Change> play (KeyListener& l, const Notes& notes, double until)
    {
        struct Ev { double t; bool on; int pitch, velocity; };
        std::vector<Ev> ev;
        for (const auto& n : notes)
        {
            ev.push_back ({ n.start, true, n.pitch, n.velocity });
            ev.push_back ({ n.start + n.length, false, n.pitch, 0 });
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && ! a.on && b.on); });

        std::vector<Change> changes;
        KeyGuess last;
        size_t i = 0;
        for (double t = 0.0; t <= until + 1.0e-9; t += 0.01)
        {
            for (; i < ev.size() && ev[i].t <= t; ++i)
                ev[i].on ? l.noteOn (ev[i].pitch, ev[i].velocity, ev[i].t) : l.noteOff (ev[i].pitch, ev[i].t);
            l.advance (t);
            const KeyGuess g = l.guess();
            if (g.has() && (! last.has() || g.tonic != last.tonic || g.minor != last.minor))
                changes.push_back ({ t, g });
            last = g.has() ? g : last;
        }
        return changes;
    }

    std::string keyText (int tonic, bool minor)
    {
        return tonic < 0 ? std::string ("none") : std::string (guessKeyName (tonic, minor)) + (minor ? " minor" : " major");
    }

    std::string describe (const KeyGuess& g)
    {
        if (! g.has())
            return "no guess";
        return keyText (g.tonic, g.minor) + " " + std::to_string (g.fit) + "%" + (g.state == GuessState::Confident ? "" : "?")
             + " (next " + keyText (g.runnerUpTonic, g.runnerUpMinor) + " " + std::to_string (g.runnerUpFit) + "%)";
    }

    // The corpus, written in C (or A for minor) and transposed by the tests.
    enum class Expect { Major, Minor, Record };

    struct Case
    {
        const char* name;
        Notes notes;
        int tonic;                        // in the written key
        Expect expect;
    };

    // C4 = 60.
    std::vector<Case> corpus()
    {
        std::vector<Case> c;
        c.push_back ({ "Twinkle Twinkle (C major)", melody ({ { 60, 1 }, { 60, 1 }, { 67, 1 }, { 67, 1 }, { 69, 1 }, { 69, 1 }, { 67, 2 },
                                                              { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 62, 1 }, { 60, 2 },
                                                              { 67, 1 }, { 67, 1 }, { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 2 },
                                                              { 67, 1 }, { 67, 1 }, { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 2 },
                                                              { 60, 1 }, { 60, 1 }, { 67, 1 }, { 67, 1 }, { 69, 1 }, { 69, 1 }, { 67, 2 },
                                                              { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 62, 1 }, { 60, 2 } }),
                       0, Expect::Major });
        c.push_back ({ "Ode to Joy (C major)", melody ({ { 64, 1 }, { 64, 1 }, { 65, 1 }, { 67, 1 }, { 67, 1 }, { 65, 1 }, { 64, 1 }, { 62, 1 },
                                                         { 60, 1 }, { 60, 1 }, { 62, 1 }, { 64, 1 }, { 64, 1.5 }, { 62, 0.5 }, { 62, 2 },
                                                         { 64, 1 }, { 64, 1 }, { 65, 1 }, { 67, 1 }, { 67, 1 }, { 65, 1 }, { 64, 1 }, { 62, 1 },
                                                         { 60, 1 }, { 60, 1 }, { 62, 1 }, { 64, 1 }, { 62, 1.5 }, { 60, 0.5 }, { 60, 2 } }),
                       0, Expect::Major });
        c.push_back ({ "Frere Jacques (C major)", melody ({ { 60, 1 }, { 62, 1 }, { 64, 1 }, { 60, 1 }, { 60, 1 }, { 62, 1 }, { 64, 1 }, { 60, 1 },
                                                            { 64, 1 }, { 65, 1 }, { 67, 2 }, { 64, 1 }, { 65, 1 }, { 67, 2 },
                                                            { 67, 0.5 }, { 69, 0.5 }, { 67, 0.5 }, { 65, 0.5 }, { 64, 1 }, { 60, 1 },
                                                            { 67, 0.5 }, { 69, 0.5 }, { 67, 0.5 }, { 65, 0.5 }, { 64, 1 }, { 60, 1 },
                                                            { 60, 1 }, { 55, 1 }, { 60, 2 }, { 60, 1 }, { 55, 1 }, { 60, 2 } }),
                       0, Expect::Major });
        c.push_back ({ "Amazing Grace (C major, opening)", melody ({ { 55, 1 }, { 60, 2 }, { 64, 0.5 }, { 60, 0.5 }, { 64, 2 }, { 62, 1 },
                                                                     { 60, 2 }, { 57, 1 }, { 55, 2 }, { 55, 1 }, { 60, 2 }, { 64, 0.5 },
                                                                     { 60, 0.5 }, { 64, 2 }, { 62, 1 }, { 67, 5 },
                                                                     { 64, 1 }, { 67, 1.5 }, { 64, 0.5 }, { 67, 0.5 }, { 64, 0.5 }, { 60, 2 },
                                                                     { 55, 1 }, { 57, 1.5 }, { 60, 0.5 }, { 60, 0.5 }, { 57, 0.5 }, { 55, 2 },
                                                                     { 55, 1 }, { 60, 2 }, { 64, 0.5 }, { 60, 0.5 }, { 64, 2 }, { 62, 1 }, { 60, 5 } }),
                       0, Expect::Major });
        c.push_back ({ "Greensleeves (A minor, opening)", melody ({ { 57, 1 }, { 60, 2 }, { 62, 1 }, { 64, 1.5 }, { 65, 0.5 }, { 64, 1 },
                                                                    { 62, 2 }, { 59, 1 }, { 55, 1.5 }, { 57, 0.5 }, { 59, 1 },
                                                                    { 60, 2 }, { 57, 1 }, { 57, 1.5 }, { 56, 0.5 }, { 57, 1 },
                                                                    { 59, 2 }, { 56, 1 }, { 52, 2 }, { 57, 1 },
                                                                    { 60, 2 }, { 62, 1 }, { 64, 1.5 }, { 65, 0.5 }, { 64, 1 },
                                                                    { 62, 2 }, { 59, 1 }, { 55, 1.5 }, { 57, 0.5 }, { 59, 1 },
                                                                    { 60, 1.5 }, { 59, 0.5 }, { 57, 1 }, { 56, 1.5 }, { 54, 0.5 }, { 56, 1 }, { 57, 3 } }),
                       9, Expect::Minor });
        c.push_back ({ "Frere Jacques in minor (A minor, as Mahler's)", melody ({ { 57, 1 }, { 59, 1 }, { 60, 1 }, { 57, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 57, 1 },
                                                                                  { 60, 1 }, { 62, 1 }, { 64, 2 }, { 60, 1 }, { 62, 1 }, { 64, 2 },
                                                                                  { 64, 0.5 }, { 65, 0.5 }, { 64, 0.5 }, { 62, 0.5 }, { 60, 1 }, { 57, 1 },
                                                                                  { 64, 0.5 }, { 65, 0.5 }, { 64, 0.5 }, { 62, 0.5 }, { 60, 1 }, { 57, 1 },
                                                                                  { 57, 1 }, { 52, 1 }, { 57, 2 }, { 57, 1 }, { 52, 1 }, { 57, 2 } }),
                       9, Expect::Minor });
        c.push_back ({ "God Rest Ye Merry Gentlemen (A minor, opening)", melody ({ { 57, 1 }, { 57, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 },
                                                                                   { 55, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 62, 1 }, { 64, 3 },
                                                                                   { 57, 1 }, { 57, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 },
                                                                                   { 55, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 62, 1 }, { 64, 3 },
                                                                                   { 64, 1 }, { 65, 1 }, { 64, 1 }, { 62, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 }, { 55, 1 },
                                                                                   { 57, 1 }, { 59, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 }, { 56, 1 }, { 57, 4 } }),
                       9, Expect::Minor });
        c.push_back ({ "Scarborough Fair (D Dorian)", melody ({ { 62, 2 }, { 62, 1 }, { 69, 2 }, { 69, 1 }, { 64, 1.5 }, { 65, 0.5 }, { 64, 1 }, { 62, 3 },
                                                                { 69, 1 }, { 72, 2 }, { 74, 1 }, { 72, 2 }, { 69, 1 }, { 71, 1 }, { 67, 1 }, { 69, 4 },
                                                                { 74, 2 }, { 74, 1 }, { 74, 2 }, { 72, 1 }, { 69, 2 }, { 69, 1 }, { 67, 1 }, { 65, 2 },
                                                                { 64, 1 }, { 60, 2 }, { 62, 2 }, { 69, 1 }, { 67, 2 }, { 65, 1 }, { 64, 2 }, { 62, 1 },
                                                                { 60, 2 }, { 62, 4 } }),
                       2, Expect::Record });
        c.push_back ({ "Drunken Sailor (D Dorian)", melody ({ { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 62, 0.5 },
                                                              { 65, 0.5 }, { 69, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 },
                                                              { 67, 0.5 }, { 60, 0.5 }, { 64, 0.5 }, { 67, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 },
                                                              { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 71, 0.5 }, { 72, 0.5 }, { 74, 0.5 }, { 72, 0.5 }, { 69, 0.5 },
                                                              { 67, 0.5 }, { 64, 0.5 }, { 62, 1 }, { 62, 2 } }),
                       2, Expect::Record });

        // Progressions: block chords, a bar each, the tonic held two bars at the end.
        const std::vector<int> C { 48, 60, 64, 67 }, F { 53, 65, 69, 72 }, G { 55, 62, 67, 71 }, Am { 45, 60, 64, 69 },
                               G7 { 55, 65, 67, 71 }, Dm7 { 50, 60, 65, 69 }, Cmaj7 { 48, 64, 67, 71 }, C7 { 48, 64, 67, 70 },
                               F7 { 53, 63, 65, 69 };
        // In A minor: i, iv, V (raised 7th), bVII, bVI.
        const std::vector<int> aAm { 45, 60, 64, 69 }, aDm { 50, 62, 65, 69 }, aE { 52, 64, 68, 71 }, aG { 43, 62, 67, 71 }, aF { 41, 60, 65, 69 };
        c.push_back ({ "I-V-vi-IV (C)", chords ({ { C, 4 }, { G, 4 }, { Am, 4 }, { F, 4 }, { C, 4 }, { G, 4 }, { Am, 4 }, { F, 4 }, { C, 8 } }), 0, Expect::Major });
        c.push_back ({ "I-vi-IV-V (C)", chords ({ { C, 4 }, { Am, 4 }, { F, 4 }, { G, 4 }, { C, 4 }, { Am, 4 }, { F, 4 }, { G, 4 }, { C, 8 } }), 0, Expect::Major });
        c.push_back ({ "ii-V-I (C)", chords ({ { Dm7, 4 }, { G7, 4 }, { Cmaj7, 8 }, { Dm7, 4 }, { G7, 4 }, { Cmaj7, 8 } }), 0, Expect::Major });
        c.push_back ({ "I-IV-V-I (C)", chords ({ { C, 4 }, { F, 4 }, { G, 4 }, { C, 4 }, { F, 4 }, { G, 4 }, { C, 8 } }), 0, Expect::Major });
        c.push_back ({ "i-iv-V-i (A minor)", chords ({ { aAm, 4 }, { aDm, 4 }, { aE, 4 }, { aAm, 4 }, { aDm, 4 }, { aE, 4 }, { aAm, 8 } }), 9, Expect::Minor });
        c.push_back ({ "i-bVII-bVI-V (A minor)", chords ({ { aAm, 4 }, { aG, 4 }, { aF, 4 }, { aE, 4 }, { aAm, 4 }, { aG, 4 }, { aF, 4 }, { aE, 4 }, { aAm, 8 } }), 9, Expect::Minor });
        c.push_back ({ "12-bar blues in sevenths (C)", chords ({ { C7, 4 }, { C7, 4 }, { C7, 4 }, { C7, 4 }, { F7, 4 }, { F7, 4 }, { C7, 4 }, { C7, 4 },
                                                                 { G7, 4 }, { F7, 4 }, { C7, 4 }, { G7, 4 }, { C7, 8 } }), 0, Expect::Record });
        return c;
    }

    KeyGuess finalGuess (const Notes& n, KeyProfiles set = KeyListener::defaultProfiles)
    {
        KeyListener l (set);
        play (l, n, endOf (n) + 0.5);
        return l.guess();
    }
}

//==============================================================================
static void testProfiles()
{
    std::cout << "== profiles ==\n";
    bool own = true;
    for (const auto set : { KeyProfiles::KrumhanslKessler, KeyProfiles::Temperley })
    for (int m = 0; m < 2; ++m)
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            PitchWeights w {};
            for (int i = 0; i < 12; ++i)
                w[(size_t) ((i + tonic) % 12)] = keyprofiles::profile (m == 1, set)[i];
            const auto e = estimateKey (w, set);
            own &= e.valid && e.best.tonic == tonic && e.best.minor == (m == 1) && std::abs (e.best.fit - 1.0) < 1.0e-9;
        }
    check (own, "each of the 24 profiles (Krumhansl-Kessler and Temperley), as a histogram, is its own key with fit 1");
    const auto kk = KeyProfiles::KrumhanslKessler;

    PitchWeights flat {};
    const bool none = ! estimateKey (flat, kk).valid;
    flat.fill (3.0);
    check (none && ! estimateKey (flat, kk).valid && keyFit (flat, 0, false, kk) == 0.0, "nothing, or all 12 pitch classes alike, is no estimate");

    PitchWeights nan {};
    nan[0] = std::nan ("");
    check (! estimateKey (nan, kk).valid, "a non-finite weight is no estimate");

    KeyGuess g { 9, true, GuessState::Confident, 82, 0, false, 76, 41 };
    const KeyGuess back = KeyGuess::unpack (g.pack());
    check (back == g && back.tonic == 9 && back.minor && back.state == GuessState::Confident && back.fit == 82
               && back.runnerUpTonic == 0 && ! back.runnerUpMinor && back.runnerUpFit == 76 && back.notes == 41
               && ! KeyGuess::unpack (KeyGuess().pack()).has(),
           "a guess packs into 64 bits and back");
}

static void testScaleMapping()
{
    std::cout << "== scale mapping ==\n";
    int majors = 0, minors = 0, neither = 0;
    bool ok = true;
    for (int s = 0; s < numScales; ++s)
    {
        const auto scale = (Scale) s;
        const auto f = scaleFamily (scale);
        majors += f == ScaleFamily::Major ? 1 : 0;
        minors += f == ScaleFamily::Minor ? 1 : 0;
        neither += f == ScaleFamily::Neither ? 1 : 0;
        ok &= scaleForGuess (scale, false) == (f == ScaleFamily::Major ? scale : Scale::Major);
        ok &= scaleForGuess (scale, true) == (f == ScaleFamily::Minor ? scale : Scale::NaturalMinor);
    }
    check (majors == 6 && minors == 6 && neither == 1 && scaleFamily (Scale::Locrian) == ScaleFamily::Neither
               && scaleFamily (Scale::PhrygianDominant) == ScaleFamily::Major && scaleFamily (Scale::HungarianMinor) == ScaleFamily::Minor,
           "6 major-family scales, 6 minor-family, Locrian neither");
    check (ok && scaleForGuess (Scale::Dorian, true) == Scale::Dorian && scaleForGuess (Scale::Dorian, false) == Scale::Major
               && scaleForGuess (Scale::Locrian, true) == Scale::NaturalMinor && scaleForGuess (Scale::Mixolydian, false) == Scale::Mixolydian,
           "a guess keeps a same-family Scale (D minor on Dorian stays Dorian), else Major or Natural Minor; Locrian is always replaced");
    check (std::string (guessKeyName (1, false)) == "Db" && std::string (guessKeyName (1, true)) == "C#" && std::string (guessKeyName (8, true)) == "G#"
               && std::string (guessKeyName (6, false)) == "F#",
           "guesses are spelled by key signature (Db major, C# minor, G# minor, F# major)");
}

static void testCorpus()
{
    std::cout << "== corpus ==\n";
    for (const auto& c : corpus())
    {
        bool invariant = true;
        KeyGuess first;
        for (int t = 0; t < 12; ++t)
        {
            const KeyGuess g = finalGuess (transpose (c.notes, t));
            if (t == 0)
                first = g;
            invariant &= g.has() == first.has() && g.fit == first.fit && g.state == first.state && g.minor == first.minor
                      && (! g.has() || g.tonic == (first.tonic + t) % 12);
        }

        // When the guess first shows, and when it first shows the right key confidently.
        KeyListener l;
        const auto changes = play (l, c.notes, endOf (c.notes) + 0.5);
        const bool wantMinor = c.expect == Expect::Minor;
        const bool right = first.has() && first.tonic == c.tonic && first.minor == wantMinor;
        if (c.expect == Expect::Record)
        {
            check (invariant, std::string (c.name) + ": " + describe (first) + " (recorded, not required)");
            continue;
        }
        // When the right key first shows.
        double settled = -1.0;
        for (const auto& ch : changes)
            if (settled < 0.0 && ch.guess.tonic == c.tonic && ch.guess.minor == wantMinor)
                settled = ch.time;
        const double eightBars = 8 * 4 * 60.0 / 100.0;
        check (right && first.state == GuessState::Confident && invariant && settled >= 0.0 && settled <= eightBars,
               std::string (c.name) + ": " + describe (first) + ", shown from " + std::to_string (settled).substr (0, 5) + " s, "
                   + std::to_string (changes.size()) + " change(s); the same in all 12 keys");
    }
}

// ChordMonkey's own chords (source C, as a corpus): a progression's first four white keys
// twice, a bar each, then the tonic's key for two bars, at 100 bpm.
struct OwnResult { int cases = 0, right = 0, knownWrong = 0; std::set<std::string> wrong; };

static OwnResult ownOutput (KeyProfiles set)
{
    OwnResult r;
    const Progression progressions[] = { Progression::Pop, Progression::Fifties, Progression::TwoFiveOne, Progression::Canon };
    for (int key = 0; key < 12; ++key)
        for (const Scale scale : { Scale::Major, Scale::NaturalMinor })
            for (const ChordGroup group : { ChordGroup::Triads, ChordGroup::Sevenths })
                for (const Progression prog : progressions)
                {
                    auto engine = std::make_unique<ChordEngine>();
                    EngineSettings s;
                    s.key = key;
                    s.scale = scale;
                    s.group = group;
                    s.progression = prog;
                    Transport t;
                    t.sampleRate = 48000.0;
                    const auto degrees = progressionDegrees (prog);
                    int tonicSlot = 0;
                    for (int n = 0; n < numWhiteSlots; ++n)
                        tonicSlot = degrees[(size_t) n] == 0 ? n : tonicSlot;
                    // The progression's first four keys, twice, then the tonic's key for two bars.
                    std::vector<int> order { 0, 1, 2, 3, 0, 1, 2, 3, tonicSlot, tonicSlot };

                    KeyListener l (set);
                    const int blockSize = 480, barBlocks = 48000 * 24 / 10 / blockSize;   // a bar at 100 bpm: 2.4 s
                    BlockEvents out;
                    int64_t pos = 0;
                    for (size_t step = 0; step < order.size(); ++step)
                    {
                        const int note = KeyRouter::keyForSlot (order[step]);
                        for (int b = 0; b < barBlocks; ++b)
                        {
                            std::vector<MidiEvent> in;
                            if (b == 0)
                                in.push_back (MidiEvent::noteOn (0, note, 100, 0));
                            if (b == barBlocks - 1)
                                in.push_back (MidiEvent::noteOff (0, note, blockSize - 1));
                            out.clear();
                            engine->process (s, t, in.data(), (int) in.size(), blockSize, out);
                            for (const auto& e : out)
                            {
                                const double at = (double) (pos + e.sample) / 48000.0;
                                if (e.isNoteOn()) l.noteOn (e.note(), e.velocity(), at);
                                else if (e.isNoteOff()) l.noteOff (e.note(), at);
                            }
                            pos += blockSize;
                            l.advance ((double) pos / 48000.0);
                        }
                    }
                    l.advance ((double) pos / 48000.0 + 0.5);
                    const auto g = l.guess();
                    ++r.cases;
                    const bool ok = g.has() && g.tonic == key && g.minor == (scale == Scale::NaturalMinor);
                    r.right += ok ? 1 : 0;
                    // Canon's first four in natural minor with sevenths, i7 v7 VImaj7 IIImaj7, is the
                    // relative major's vi iii IV I: heard as that, in every key.
                    const bool known = scale == Scale::NaturalMinor && group == ChordGroup::Sevenths && prog == Progression::Canon;
                    r.knownWrong += ! ok && known ? 1 : 0;
                    if (! ok && ! known)
                        r.wrong.insert (keyText (key, scale == Scale::NaturalMinor) + " " + chordGroupInfo (group).name + " "
                                      + progressionInfo (prog).name + ": " + describe (g));
                }
    return r;
}

static void testOwnOutput()
{
    std::cout << "== ChordMonkey's own chords ==\n";
    const auto r = ownOutput (KeyListener::defaultProfiles);
    std::string kinds;
    for (const auto& w : r.wrong)
        kinds += (kinds.empty() ? "" : "; ") + w;
    check (r.cases == 192 && r.wrong.empty() && r.right + r.knownWrong == r.cases,
           std::to_string (r.right) + " of " + std::to_string (r.cases) + " Key x Scale x Chord Group x Progression played by ChordMonkey give the Key; "
               + std::to_string (r.knownWrong) + " are natural minor sevenths under Canon (i v VI III), heard as the relative major"
               + (kinds.empty() ? std::string() : " (also wrong: " + kinds + ")"));
}

// The plan's bake-off: Krumhansl-Kessler against Temperley on the corpus and ChordMonkey's own
// chords. The listener uses the one that gets more right.
static void testBakeOff()
{
    std::cout << "== profiles bake-off ==\n";
    int score[2] = {};
    std::string detail[2];
    for (int i = 0; i < 2; ++i)
    {
        const auto set = i == 0 ? KeyProfiles::KrumhanslKessler : KeyProfiles::Temperley;
        int corpusRight = 0, corpusCases = 0;
        for (const auto& c : corpus())
        {
            if (c.expect == Expect::Record)
                continue;
            const auto g = finalGuess (c.notes, set);
            ++corpusCases;
            corpusRight += g.has() && g.tonic == c.tonic && g.minor == (c.expect == Expect::Minor) ? 1 : 0;
        }
        const auto own = ownOutput (set);
        score[i] = corpusRight + own.right;
        detail[i] = std::to_string (corpusRight) + "/" + std::to_string (corpusCases) + " corpus, " + std::to_string (own.right) + "/192 own chords";
    }
    check (KeyListener::defaultProfiles == KeyProfiles::Temperley && score[1] > score[0],
           "Temperley (" + detail[1] + ") beats Krumhansl-Kessler (" + detail[0] + "), so the listener uses Temperley's profiles");
}

static void testModulation()
{
    std::cout << "== modulation ==\n";
    const auto cases = corpus();
    const auto& majorTune = cases[2].notes;   // Frere Jacques
    const auto& minorTune = cases[5].notes;   // Frere Jacques in A minor
    const auto part = [] (const Notes& tune, double length)
    {
        Notes n;
        const double e = endOf (tune) + 0.6;
        for (double o = 0.0; o < length; o += e)
            for (auto x : tune)
                if (x.start + o < length)
                {
                    x.start += o;
                    x.length = std::min (x.length, length - x.start);
                    n.push_back (x);
                }
        return n;
    };

    // 24 s in one key, then 24 s in the other. While the evidence is a mix of both, one key
    // between them may show.
    constexpr double partLength = 24.0, bound = 20.0;   // close keys cross over after about a half-life
    struct Mod { const char* name; Notes a, b; int fromTonic, toTonic; bool minor; };
    const Mod mods[] = {
        { "C major -> G major", part (majorTune, partLength), transpose (part (majorTune, partLength), 7), 0, 7, false },
        { "A minor -> C minor", part (minorTune, partLength), transpose (part (minorTune, partLength), 3), 9, 0, true },
    };
    for (const auto& m : mods)
    {
        KeyListener l;
        const auto changes = play (l, append (m.a, m.b, partLength), 2.0 * partLength + 0.5);
        const bool first = ! changes.empty() && changes.front().guess.tonic == m.fromTonic && changes.front().guess.minor == m.minor
                        && (changes.size() < 2 || changes[1].time > partLength);
        const auto& last = changes.back().guess;
        const bool arrived = last.tonic == m.toTonic && last.minor == m.minor && changes.size() >= 2 && changes.size() <= 3;
        const double after = changes.size() >= 2 ? changes.back().time - partLength : -1.0;
        std::string keys;
        for (const auto& c : changes)
            keys += (keys.empty() ? "" : ", ") + keyText (c.guess.tonic, c.guess.minor);
        check (first && arrived && after > 0.0 && after <= bound,
               std::string (m.name) + ": " + keys + "; the new key " + std::to_string (after).substr (0, 5)
                   + " s after the modulation (at most " + std::to_string ((int) bound) + " s), then " + describe (l.guess()));
    }
}

static void testSilenceAndAmbiguity()
{
    std::cout << "== silence and ambiguous input ==\n";
    {
        KeyListener l;
        l.advance (0.0);
        l.advance (30.0);
        check (! l.guess().has(), "nothing heard: no guess");

        KeyListener fresh;
        auto& m = fresh;
        const auto tune = corpus()[1].notes;
        play (m, tune, endOf (tune) + KeyListener::tailSeconds + 0.5);
        const auto before = m.guess();
        const double listened = m.listeningTime();
        m.advance (endOf (tune) + 60.0);
        check (before.has() && m.guess() == before && m.listeningTime() == listened,
               "a tune, then 60 s of silence: the guess is unchanged (listening time stops 2 s after the last note)");
    }

    // noneOnly: no guess; else no guess or a tentative one, or one of `either`.
    struct Amb { const char* name; Notes notes; bool noneOnly; std::vector<std::pair<int, bool>> either = {}; };
    Notes repeated, fifth, triad, chromatic, wholeTone, diatonic;
    for (int i = 0; i < 40; ++i)
    {
        repeated.push_back ({ i * 0.5, 0.4, 60, 100 });
        fifth.push_back ({ i * 0.5, 0.4, i % 2 == 0 ? 60 : 67, 100 });
        chromatic.push_back ({ i * 0.25, 0.2, 60 + i % 12, 100 });
        wholeTone.push_back ({ i * 0.25, 0.2, 60 + 2 * (i % 6), 100 });
    }
    for (const int p : { 60, 64, 67 })
        triad.push_back ({ 0.0, 8.0, p, 100 });
    for (int bar = 0; bar < 14; ++bar)
        for (const int step : { 0, 2, 4 })
        {
            const int degree = bar % 7;
            const int idx = degree + step;
            diatonic.push_back ({ bar * 1.2, 1.1, 60 + scaleOffset (Scale::Major, idx), 100 });
        }
    const Amb ambs[] = {
        { "one note repeated", repeated, true },
        { "a fifth (C G)", fifth, true },
        { "a C major triad held", triad, false },
        { "all 12 pitch classes evenly", chromatic, true },
        { "a whole-tone scale", wholeTone, false },
        { "the 7 diatonic triads of C, evenly", diatonic, false, { { 0, false }, { 9, true } } },
        { "Am F C G (i-bVI-bIII-bVII, or vi-IV-I-V in C)",
          chords ({ { { 45, 60, 64, 69 }, 4 }, { { 41, 60, 65, 69 }, 4 }, { { 48, 60, 64, 67 }, 4 }, { { 43, 62, 67, 71 }, 4 },
                    { { 45, 60, 64, 69 }, 4 }, { { 41, 60, 65, 69 }, 4 }, { { 48, 60, 64, 67 }, 4 }, { { 43, 62, 67, 71 }, 4 }, { { 45, 60, 64, 69 }, 8 } }),
          false, { { 0, false }, { 9, true } } },
    };
    for (const auto& a : ambs)
    {
        const auto g = finalGuess (a.notes);
        bool either = false;
        for (const auto& [tonic, minor] : a.either)
            either |= g.has() && g.tonic == tonic && g.minor == minor;
        const bool ok = a.noneOnly ? ! g.has() : (! g.has() || g.state == GuessState::Tentative || either);
        check (ok, std::string (a.name) + ": " + describe (g)
                       + (a.noneOnly ? " (no guess expected)" : a.either.empty() ? " (no guess or tentative expected)" : " (C major or A minor, or tentative)"));
    }
}

static void testWeighting()
{
    std::cout << "== weighting ==\n";
    const auto tune = corpus()[0].notes;
    Notes ghost = tune;
    for (size_t i = 1; i < ghost.size(); i += 2)
        ghost[i].velocity = 20;
    const auto a = finalGuess (tune), b = finalGuess (ghost);
    check (a.has() && b.has() && a.tonic == b.tonic && a.minor == b.minor, "every other note at velocity 20: the same guess (" + describe (b) + ")");

    Notes held = tune;
    held.push_back ({ 0.0, endOf (tune), 67, 100 });
    const auto c = finalGuess (held);
    check (c.has() && c.tonic == a.tonic && c.minor == a.minor, "with G held throughout: the same guess (" + describe (c) + ")");

    // One note held 30 s counts as its onset and 2 s of holding, decayed.
    KeyListener l;
    l.noteOn (60, 127, 0.0);
    l.advance (30.0);
    const double k = std::pow (2.0, -1.0 / KeyListener::halfLife);
    double expect = KeyListener::onsetSeconds * std::pow (k, 30.0);
    expect += (std::pow (k, 28.0) - std::pow (k, 30.0)) / -std::log (k);   // the integral of k^(30-s) over 0..2
    check (std::abs (l.getWeights()[0] - expect) < 0.01 * expect, "a note held 30 s counts 2 s of holding ("
                                                                 + std::to_string (l.getWeights()[0]) + " vs " + std::to_string (expect) + ")");
}

static void testHysteresis()
{
    std::cout << "== hysteresis ==\n";
    // Random notes of the C major scale, evenly, 4 a second for 60 s: C major and A minor (and
    // their neighbours) stay close all along.
    std::mt19937 rng (7);
    Notes n;
    for (int i = 0; i < 240; ++i)
        n.push_back ({ i * 0.25, 0.22, 60 + scaleOffset (Scale::Major, (int) (rng() % 7)), 100 });
    KeyListener l;
    const auto changes = play (l, n, 60.5);
    std::string keys;
    for (const auto& c : changes)
        keys += (keys.empty() ? "" : ", ") + keyText (c.guess.tonic, c.guess.minor);
    check (changes.size() <= 2, "60 s of random C major scale notes: " + std::to_string (changes.size()) + " key(s) shown (" + keys + ")");
}

static void testFuzz()
{
    std::cout << "== fuzz ==\n";
    std::mt19937 rng (11);
    KeyListener l;
    double t = 0.0;
    bool sane = true;
    for (int i = 0; i < 200000; ++i)
    {
        t += (rng() % 1000) / 20000.0;
        const int note = (int) (rng() % 140) - 6;
        switch (rng() % 10)
        {
            case 0: case 1: case 2: case 3: l.noteOn (note, (int) (rng() % 140) - 6, t); break;
            case 4: case 5: case 6: l.noteOff (note, t); break;
            case 7: l.allUp (t); break;
            case 8: l.advance (t - 1.0); break;   // backwards: ignored
            default: if (rng() % 500 == 0) l.reset(); else l.advance (t); break;
        }
        for (const double w : l.getWeights())
            sane &= std::isfinite (w) && w >= 0.0;
        const auto g = l.guess();
        sane &= g.fit >= 0 && g.fit <= 100 && g.tonic >= -1 && g.tonic <= 11 && (int) g.state <= 2;
    }
    check (sane, "200,000 random notes, velocities, key-ups, resets and times (10 minutes and more): weights finite and >= 0, guesses in range");
}

//==============================================================================
// The engine's Analyse mode.
namespace
{
    struct Rig
    {
        ChordEngine engine;
        EngineSettings settings;
        Transport transport { 48000.0, false, 0.0, 120.0, 4, 4 };
        BlockEvents out;
        testutil::Receiver rx;

        BlockEvents& run (std::vector<MidiEvent> in, int numSamples = 480)
        {
            std::stable_sort (in.begin(), in.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });
            out.clear();
            engine.process (settings, transport, in.data(), (int) in.size(), numSamples, out);
            rx.feed (out, numSamples);
            return out;
        }
    };

    int notesOnCount (const BlockEvents& out)
    {
        int n = 0;
        for (const auto& e : out)
            n += e.isNoteOn() ? 1 : 0;
        return n;
    }

    // Plays `notes` (seconds) through the engine at `rate` in blocks of `blockSize`; returns
    // the time each change of the shown key was first seen at a block's end.
    std::vector<Change> playEngine (Rig& rig, const Notes& notes, double until, double rate, int blockSize)
    {
        struct Ev { int64_t sample; bool on; int pitch, velocity; };
        std::vector<Ev> ev;
        for (const auto& n : notes)
        {
            ev.push_back ({ (int64_t) std::llround (n.start * rate), true, n.pitch, n.velocity });
            ev.push_back ({ (int64_t) std::llround ((n.start + n.length) * rate), false, n.pitch, 0 });
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.sample < b.sample || (a.sample == b.sample && ! a.on && b.on); });
        rig.transport.sampleRate = rate;
        std::vector<Change> changes;
        KeyGuess last;
        size_t i = 0;
        for (int64_t pos = 0; (double) pos / rate < until; pos += blockSize)
        {
            std::vector<MidiEvent> in;
            for (; i < ev.size() && ev[i].sample < pos + blockSize; ++i)
                in.push_back (ev[i].on ? MidiEvent::noteOn (0, ev[i].pitch, ev[i].velocity, (int) (ev[i].sample - pos))
                                       : MidiEvent::noteOff (0, ev[i].pitch, (int) (ev[i].sample - pos)));
            rig.run (in, blockSize);
            const auto g = rig.engine.getKeyGuess();
            if (g.has() && (! last.has() || g.tonic != last.tonic || g.minor != last.minor))
                changes.push_back ({ (double) (pos + blockSize) / rate, g });
            last = g.has() ? g : last;
        }
        return changes;
    }
}

static void testEngine()
{
    std::cout << "== engine: Analyse mode ==\n";
    const auto cases = corpus();
    const auto& tune = cases[4].notes;    // Greensleeves, A minor

    {
        Rig rig;
        playEngine (rig, tune, endOf (tune) + 0.5, 48000.0, 480);
        check (! rig.engine.getKeyGuess().has() && rig.rx.noteOns > 0, "Analyse off: the keys play chords and nothing is analysed");
    }
    {
        Rig rig;
        rig.settings.enabled = false;
        playEngine (rig, tune, endOf (tune) + 0.5, 48000.0, 480);
        check (! rig.engine.getKeyGuess().has() && rig.rx.noteOns == (int) tune.size(), "Enable off with Analyse off: notes pass through, nothing is analysed");
    }
    {
        Rig rig;
        rig.settings.analysing = true;
        playEngine (rig, tune, endOf (tune) + 0.5, 48000.0, 480);
        const auto g = rig.engine.getKeyGuess();
        check (g.has() && g.tonic == 9 && g.minor && rig.rx.noteOns == (int) tune.size() && rig.rx.on.empty() && rig.rx.clean(),
               "Analyse: every note passes through unchanged and the guess is A minor (" + describe (g) + ")");

        // Back to chords: the guess stays until the next analysis; entering Listen again clears it.
        rig.settings.analysing = false;
        rig.run ({});
        const auto kept = rig.engine.getKeyGuess();
        const auto chord = notesOnCount (rig.run ({ MidiEvent::noteOn (0, 48, 100, 0) }));
        rig.run ({ MidiEvent::noteOff (0, 48, 0) });
        rig.settings.analysing = true;
        rig.run ({});
        check (kept == g && chord == 3 && ! rig.engine.getKeyGuess().has(), "leaving Analyse keeps the guess and chords play; entering it again starts afresh");
    }
    {
        // Entering Analyse while a chord is held ends it; leaving it while notes pass through ends them.
        Rig rig;
        rig.run ({ MidiEvent::noteOn (0, 48, 100, 0) });
        const int sounding = (int) rig.rx.on.size();
        rig.settings.analysing = true;
        rig.run ({});
        const bool chordEnded = rig.rx.on.empty();
        rig.run ({ MidiEvent::noteOn (0, 60, 100, 0), MidiEvent::noteOn (0, 64, 100, 10) });
        const bool through = rig.rx.on.size() == 2;
        rig.settings.analysing = false;
        rig.run ({});
        const bool ended = rig.rx.on.empty();
        rig.run ({ MidiEvent::noteOff (0, 60, 0), MidiEvent::noteOff (0, 64, 0) });   // their key-ups, after leaving
        check (sounding == 3 && chordEnded && through && ended && rig.rx.clean() && rig.engine.getVoices().isEmpty(),
               "entering Analyse ends a held chord; leaving it ends the notes passing through; their later key-ups do nothing");
    }
    {
        // The test pattern is silent while analysing; all-notes-off and a transport stop end
        // listened notes too.
        Rig rig;
        rig.settings.analysing = true;
        rig.settings.testPattern = true;
        rig.transport.playing = true;
        int patternNotes = 0;
        for (int b = 0; b < 400; ++b)
        {
            patternNotes += notesOnCount (rig.run ({}));
            rig.transport.ppq += 480.0 / rig.transport.samplesPerBeat();
        }
        rig.run ({ MidiEvent::noteOn (0, 62, 100, 0) });
        rig.run ({ MidiEvent::controller (0, 123, 0, 0) });
        const bool allOff = rig.rx.on.empty();
        rig.run ({ MidiEvent::noteOn (0, 62, 100, 0) });
        rig.transport.playing = false;
        rig.run ({});
        check (patternNotes == 0 && allOff && rig.rx.on.empty() && rig.rx.clean(),
               "in Analyse the test pattern is silent; all-notes-off and a transport stop end the notes passing through");
    }

    // The time base: the same guess at 44.1, 48 and 96 kHz in blocks of 32-2048, and the change
    // of a modulation at the same time (within an estimate and a block).
    {
        const auto& frere = cases[2].notes;
        Notes mod = append (frere, transpose (frere, 7), endOf (frere) + 0.6);
        const double until = endOf (mod) + 0.5;
        Rig reference;
        reference.settings.analysing = true;
        const auto ref = playEngine (reference, mod, until, 48000.0, 480);
        bool same = ref.size() == 2;
        std::string worst;
        double worstDelta = 0.0;
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int blockSize : { 32, 64, 333, 512, 2048 })
            {
                Rig rig;
                rig.settings.analysing = true;
                const auto c = playEngine (rig, mod, until, rate, blockSize);
                same &= c.size() == ref.size() && rig.engine.getKeyGuess().tonic == reference.engine.getKeyGuess().tonic;
                for (size_t i = 0; i < std::min (c.size(), ref.size()); ++i)
                {
                    same &= c[i].guess.tonic == ref[i].guess.tonic && c[i].guess.minor == ref[i].guess.minor;
                    const double d = std::abs (c[i].time - ref[i].time);
                    if (d > worstDelta)
                    {
                        worstDelta = d;
                        worst = std::to_string ((int) rate) + " Hz, " + std::to_string (blockSize);
                    }
                }
            }
        check (same && worstDelta <= KeyListener::estimateEvery + 2048.0 / 44100.0 + 0.01,
               "the same keys at 44.1/48/96 kHz and blocks of 32-2048; times within "
                   + std::to_string (worstDelta * 1000.0).substr (0, 5) + " ms (worst at " + worst + ")");
    }

    // The on-screen keys reach the engine as input, so a fuzz of notes, Analyse switches and
    // Enable flips leaves nothing sounding.
    {
        std::mt19937 rng (5);
        Rig rig;
        std::vector<int> down;
        for (int b = 0; b < 20000; ++b)
        {
            std::vector<MidiEvent> in;
            if (rng() % 3 == 0)
            {
                const int note = 36 + (int) (rng() % 48);
                in.push_back (MidiEvent::noteOn (0, note, 1 + (int) (rng() % 127), (int) (rng() % 480)));
                down.push_back (note);
            }
            if (! down.empty() && rng() % 3 == 0)
            {
                const size_t k = rng() % down.size();
                in.push_back (MidiEvent::noteOff (0, down[k], (int) (rng() % 480)));
                down.erase (down.begin() + (long) k);
            }
            if (rng() % 60 == 0) rig.settings.analysing = ! rig.settings.analysing;
            if (rig.settings.analysing && rng() % 2000 == 0) rig.settings.enabled = ! rig.settings.enabled;
            if (rng() % 500 == 0) rig.settings.testPattern = ! rig.settings.testPattern;
            if (rng() % 300 == 0) rig.transport.playing = ! rig.transport.playing;
            rig.run (in);
            if ((int) rig.rx.on.size() != rig.engine.getVoices().soundingNotes() || ! rig.rx.clean())
            {
                check (false, "Analyse fuzz, block " + std::to_string (b) + ": " + rig.rx.describe());
                return;
            }
        }
        std::vector<MidiEvent> ups;
        for (const int n : down)
            ups.push_back (MidiEvent::noteOff (0, n, 0));
        rig.run (ups);
        rig.settings.testPattern = false;
        rig.transport.playing = false;
        rig.run ({});
        check (rig.rx.on.empty() && rig.engine.getVoices().isEmpty() && rig.rx.clean(),
               "20,000 blocks of notes with Analyse, Enable, Test pattern and the transport switching: nothing left sounding");
    }
}

int main()
{
    testProfiles();
    testScaleMapping();
    testCorpus();
    testOwnOutput();
    testBakeOff();
    testModulation();
    testSilenceAndAmbiguity();
    testWeighting();
    testHysteresis();
    testFuzz();
    testEngine();
    return testutil::finish ("test_keys");
}
