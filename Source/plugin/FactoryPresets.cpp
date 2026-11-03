#include "FactoryPresets.h"
#include "Parameters.h"

namespace chordmonkey
{
    namespace
    {
        using V = std::vector<std::pair<juce::String, float>>;

        float f (Scale s) { return (float) (int) s; }
        float f (ChordGroup g) { return (float) (int) g; }
        float f (Progression p) { return (float) progressionChoice (p); }   // the parameter's choice
        float f (StackMode m) { return (float) (int) m; }
        float f (StrumMode m) { return (float) (int) m; }
        float f (ArpPattern p) { return (float) (int) p; }
        float f (VelocityCurve c) { return (float) (int) c; }
        float f (HoldMode m) { return (float) (int) m; }
        float f (VoicingMode m) { return (float) (int) m; }
        float f (BassMode m) { return (float) (int) m; }
        float style (const char* name)
        {
            for (int i = 0; i < numStyles; ++i)
                if (juce::String (styleInfo (i).name) == name)
                    return (float) i;
            jassertfalse;
            return 0.0f;
        }
        // Strum and pattern rate indices (NoteValues.h).
        constexpr float strum1_64 = 2.0f, strum1_32 = 4.0f, strum1_16T = 5.0f;
        constexpr float rate1_8 = 3.0f, rate1_8T = 4.0f, rate1_16 = 5.0f, rate1_16T = 6.0f;

        std::vector<FactoryPreset> make()
        {
            using namespace params;
            std::vector<FactoryPreset> p;
            p.push_back ({ "Init", "Init", {} });

            // Genre.
            p.push_back ({ "Genre", "Pop Anthem", {
                { key, 0 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Pop) },
                { stack, f (StackMode::PlusOne) }, { strum, f (StrumMode::Up) }, { strumRate, strum1_64 } } });
            p.push_back ({ "Genre", "Neo-Soul Keys", V {
                { key, 3 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::NeoSoul) }, { progression, f (Progression::TwoFiveOne) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_32 }, { velocity, f (VelocityCurve::Breakbeat) }, { velocityPeak, 110 },
                { velocityRange, 30 } } });
            p.push_back ({ "Genre", "House Stabs", {
                { key, 9 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Fifties) },
                { seq, 1 }, { seqPattern, 1 }, { seqGate, 40 }, { velocity, f (VelocityCurve::House) }, { velocityRange, 40 } } });
            p.push_back ({ "Genre", "Lo-Fi Sevenths", V {
                { key, 5 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Fifties) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_32 }, { hold, f (HoldMode::Bar) } } });
            p.push_back ({ "Genre", "Trap Minor Arps", {
                { key, 1 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Descending) },
                { params::arp, f (ArpPattern::LinearUp) }, { arpRate, rate1_16T }, { arpRange, 2 }, { arpGate, 60 },
                { velocity, f (VelocityCurve::Dancehall) }, { velocityRange, 35 } } });
            p.push_back ({ "Genre", "Reggaeton Dembow", {
                { key, 7 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Canon) },
                { seq, 1 }, { seqPattern, 3 }, { seqGate, 60 }, { velocity, f (VelocityCurve::Moombahton) }, { velocityRange, 30 } } });
            p.push_back ({ "Genre", "Synthwave Drive", V {
                { key, 9 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Fifties) },
                { params::arp, f (ArpPattern::ConvergeUp) }, { arpRate, rate1_16 }, { arpRange, 2 }, { arpGate, 70 } } });
            p.push_back ({ "Genre", "Jazz Two-Five", {
                { key, 10 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::TwoFiveOne) },
                { strum, f (StrumMode::Down) }, { strumRate, strum1_64 } } });
            p.push_back ({ "Genre", "Gospel Thirteenths", {
                { key, 8 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Thirteenths) }, { progression, f (Progression::CircleOfFifths) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_64 }, { velocity, f (VelocityCurve::Up) }, { velocityRange, 25 } } });
            p.push_back ({ "Genre", "Bossa Ninths", {
                { key, 2 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::TwoFiveOne) },
                { seq, 1 }, { seqPattern, 2 }, { seqSwing, 20 }, { seqGate, 70 } } });

            // Artist-style: by style, never by name.
            p.push_back ({ "Artist-style", "Cinematic Swells", {
                { key, 2 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::OpenTriads) }, { progression, f (Progression::Descending) },
                { stack, f (StackMode::PlusOne) }, { hold, f (HoldMode::Latch) }, { velocity, f (VelocityCurve::UpDown) }, { velocityRange, 50 } } });
            p.push_back ({ "Artist-style", "Ambient Pads", V {
                { key, 4 }, { scale, f (Scale::Lydian) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Thirds) },
                { stack, f (StackMode::PlusOneAndTwo) }, { hold, f (HoldMode::Latch) } } });
            p.push_back ({ "Artist-style", "Funk Rhythm Guitar", {
                { key, 4 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::Diatonic) },
                { seq, 1 }, { seqPattern, 3 }, { seqGate, 30 }, { velocity, f (VelocityCurve::Breakbeat) }, { velocityRange, 45 } } });
            p.push_back ({ "Artist-style", "Classic Rock Power", {
                { key, 4 }, { scale, f (Scale::Mixolydian) }, { chordGroup, f (ChordGroup::Power) }, { progression, f (Progression::Pop) },
                { stack, f (StackMode::PlusOne) }, { repeat, 1 }, { repeatRate, rate1_8 }, { repeatGate, 80 } } });
            p.push_back ({ "Artist-style", "Baroque Canon", {
                { key, 2 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Canon) },
                { params::arp, f (ArpPattern::BoomerangUp) }, { arpRate, rate1_16 }, { arpRange, 2 }, { arpGate, 80 } } });

            // Vibe.
            p.push_back ({ "Vibe", "Dreamy", V {
                { key, 5 }, { scale, f (Scale::Lydian) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Thirds) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_16T } } });
            p.push_back ({ "Vibe", "Dark", {
                { key, 0 }, { scale, f (Scale::PhrygianDominant) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Descending) },
                { chordOctave, -1 }, { stack, f (StackMode::PlusOne) }, { velocity, f (VelocityCurve::Down) }, { velocityRange, 30 } } });
            p.push_back ({ "Vibe", "Uplifting", {
                { key, 7 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Sus4) }, { progression, f (Progression::Fifties) },
                { params::arp, f (ArpPattern::LinearUp) }, { arpRate, rate1_8T }, { arpRange, 2 } } });
            p.push_back ({ "Vibe", "Melancholy", V {
                { key, 11 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Canon) },
                { strum, f (StrumMode::Down) }, { strumRate, strum1_32 } } });
            p.push_back ({ "Vibe", "Tense", {
                { key, 10 }, { scale, f (Scale::HarmonicMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::CircleOfFifths) },
                { repeat, 1 }, { repeatRate, rate1_16 }, { repeatGate, 35 }, { velocity, f (VelocityCurve::Up) }, { velocityRange, 60 } } });
            p.push_back ({ "Vibe", "Bright Melody", V {
                { key, 0 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Diatonic) },
                { params::arp, f (ArpPattern::LinearUp) }, { arpRate, rate1_8 }, { arpRange, 2 } } });

            // Songwriting (Phase 5): chord sets in patterns A-D, with a style. Press Play.
            const auto song = [&p] (const char* name, V values, std::array<const char*, 4> sets)
            {
                p.push_back ({ "Songwriting", name, std::move (values), sets });
            };
            song ("Pop Writer", { { key, 0 }, { scale, f (Scale::Major) }, { params::style, style ("Eighths") }, { params::bass, f (BassMode::Root) },
                                  { velocity, f (VelocityCurve::House) }, { velocityRange, 25 } },
                  { "Four Chords", "Sensitive Pop", "Fifties", "Pop Lift" });
            song ("Neo-Soul Session", { { key, 3 }, { scale, f (Scale::Major) }, { params::style, style ("Dotted quarters") },
                                        { params::voicing, f (VoicingMode::Open) }, { strum, f (StrumMode::Up) }, { strumRate, strum1_64 },
                                        { params::humanize, 40 } },
                  { "Neo-Soul Loop", "Two-Five-One", "Backdoor", "Gospel Walk" });
            song ("House Session", { { key, 9 }, { scale, f (Scale::Dorian) }, { params::style, style ("House stabs") },
                                     { params::voicing, f (VoicingMode::Open) }, { velocity, f (VelocityCurve::House) }, { velocityRange, 40 } },
                  { "Deep House Minor", "Garage Sevenths", "Two-Chord Groove", "Euro Minor" });
            song ("Cinematic Session", { { key, 2 }, { scale, f (Scale::NaturalMinor) }, { params::style, style ("Half notes") },
                                         { params::voicing, f (VoicingMode::Spread) }, { params::bass, f (BassMode::RootFifth) },
                                         { velocity, f (VelocityCurve::UpDown) }, { velocityRange, 30 } },
                  { "Epic Minor", "Heroic Rise", "Phrygian Mystery", "Trailer Pulse" });
            song ("Rock Session", { { key, 4 }, { scale, f (Scale::Mixolydian) }, { params::style, style ("Eighths") }, { params::bass, f (BassMode::Root) },
                                    { stack, f (StackMode::PlusOne) } },
                  { "Mixolydian Rock", "Classic Rock", "Power Descent", "Twelve-Bar Shuffle" });
            song ("Jazz Session", { { key, 10 }, { scale, f (Scale::Major) }, { params::style, style ("Charleston") },
                                    { params::voicing, f (VoicingMode::Wide) }, { params::swing, 50 }, { params::humanize, 30 } },
                  { "Two-Five-One", "Minor Two-Five", "Turnaround", "Backdoor" });
            song ("Ballad Session", { { key, 7 }, { scale, f (Scale::Major) }, { params::style, style ("Ballad") }, { params::bass, f (BassMode::Root) },
                                      { params::voicing, f (VoicingMode::Spread) }, { params::humanize, 50 } },
                  { "Hopeful", "Sad", "Nostalgic", "Happy" });
            song ("Lo-Fi Session", { { key, 5 }, { scale, f (Scale::Lydian) }, { params::style, style ("Broken up") }, { params::swing, 40 },
                                     { params::humanize, 60 }, { velocity, f (VelocityCurve::Breakbeat) }, { velocityRange, 30 } },
                  { "Dreamy", "Neo-Soul Loop", "Garage Sevenths", "Lydian Wonder" });
            song ("Reggae Session", { { key, 7 }, { scale, f (Scale::Major) }, { params::style, style ("Reggae skank") }, { params::bass, f (BassMode::Root) } },
                  { "Classic Rock", "Plagal Pop", "Four Chords", "Happy" });
            song ("Latin Session", { { key, 9 }, { scale, f (Scale::HarmonicMinor) }, { params::style, style ("Tresillo") },
                                     { params::voicing, f (VoicingMode::Open) }, { params::humanize, 30 } },
                  { "Andalusian Descent", "Dembow Minor", "Minor Riff", "Suspense" });
            return p;
        }
    }

    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> presets = make();
        return presets;
    }
}
