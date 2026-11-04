// Editor snapshot test (ChordChimp's PLAN.md §8). Runs the real processor and editor without a
// host and checks:
//
//   1. snapshots  the editor in a fixed state (C major, the Pop progression, effects, keys held)
//                 at its default size (780×640), its smallest (720×600, keyboard hidden) and a
//                 large one (1200×800), each compared with Tests/fixtures/snapshot/*.png (at most
//                 0.5% of pixels may differ by more than 24 levels: font rendering moves a little
//                 between macOS versions); clicking a chord pad plays its chord
//   2. reachable  every host parameter but the test rig (Test pattern, Test workspace) is bound
//                 to a control of the editor
//                 (going through the four sequencer patterns, as a user would)
//   3. state      a saved state restores every parameter and the UI's preferences into a new
//                 instance
//   4. presets    the factory presets are Chordism's (Tests/fixtures/chordism_presets.json:
//                 same names and order, every kept setting the same, Bright Melody's arp the one
//                 deliberate change), every factory program loads its values over the defaults,
//                 Init is the defaults, a preset's state round-trips into a new instance without
//                 touching Enable or the test rig, a load ends every note, and a Chordism preset
//                 file loads (its Progression converted, its dropped settings ignored)
//   5. Phase 8    hardening: no allocation in processBlock (a malloc hook counts every one on
//                 the audio thread) with everything on, two instances, bypass, transport and loop
//                 changes and on-screen keys; damaged saved states (garbage bytes, NaN and
//                 out-of-range values, unknown children) restore to sane parameters; zero-length
//                 and oversized blocks, mono output, bypass, a reset and a sample-rate change
//                 while notes sound, all-notes-off and SysEx from the host, an input flood
//                 (key-ups get through), and 64 instances made and destroyed
//   6. Analyse    key detection (ChordChimp's docs/KEY_DETECTION_PLAN.md K6.2): from the editor (Analyse, a
//                 guess, the click that uses it, the cross) and from the host's thread (Listen,
//                 Use guess, seen by the timer); the test rig's Heard key.json; Analyse isn't
//                 saved or in presets; the globals fit at every size; no allocation while
//                 listening; the editor-heard snapshot
//   7. workspace  (PLAN.md Phase 1) the workspace round-trips through the state, stays out of
//                 the parameters, restores empty from an older state, survives a damaged
//                 pattern, and an edit tells the host; the test rig's Workspace.txt loads while
//                 Test workspace is on; a workspace set on the message thread plays (Play,
//                 Pattern) in processBlock with no allocation (Phase 2); Bind keys choose the
//                 pattern (the parameter follows) and the pads ignore Bind (Phase 3)
//   8. chord sets the Songwriting presets fill the workspace, others leave it, user presets
//                 hold it, a ChordChimp preset loads, chord sets load (with suggestions) and save
//                 (Phase 5)
//   9. workspace  the workspace panel's actions, drops from the pads and between slots, the
//                 preview, and the MIDI drag file = the render (Phase 6)
//  10. Detect     (Phase 8) capture from the editor (the button's states, the input passing
//                 through, no allocation, the click that writes the pattern and the key) and
//                 from the host's thread (Capture, Use result seen by the timer); nothing heard
//                 keeps capturing; the cross; not saved, not in presets
//  11. Phase 9    a substitute from the slot menu's list replaces a chord (length, octave and
//                 inversion kept); Keys-Lock's Chord extensions through processBlock
//
//   ChordMonkeySnapshot <fixtures dir> <output dir> [--update | --no-pixels]
//   ChordMonkeySnapshot --bench [seconds]
//   ChordMonkeySnapshot --bench-ui [seconds [idle]]
//
// --update writes new reference images. --no-pixels renders the snapshots without comparing
// them (CI: the references are tied to this Mac's font rendering). The images of this run are
// always written to the output dir, for a look. --bench measures processBlock's CPU time
// (ChordChimp's PLAN.md §6, Phase 4: 8 instances under 1% CPU each; Tests/run_bench.sh); --bench-ui the
// message thread's CPU with one editor open in a window while everything plays.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/ChordMonkeyProcessor.h"
#include "../../Source/ui/ChordMonkeyEditor.h"
#include "../../Source/core/model/ChordSets.h"
#include "../../Source/core/model/ChordSetLibrary.h"
#include "../../Source/core/model/Substitutes.h"
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <random>
#include <set>
#include <thread>
#include <time.h>

using namespace chordmonkey;

// Phase 8: every allocation the watched thread makes (malloc, realloc, new: libmalloc's logger
// hook sees them all). Only a pthread_t compare inside: the hook runs within malloc.
extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<pthread_t> allocWatched { nullptr };
    void* volatile allocSink = nullptr;
    std::atomic<int> allocCount { 0 };

    void allocLogger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && allocWatched.load (std::memory_order_relaxed) == pthread_self())   // 2: an allocation
            allocCount.fetch_add (1, std::memory_order_relaxed);
    }

    // Counts the allocations this thread makes while it lives.
    struct AllocWatch
    {
        AllocWatch() { malloc_logger = allocLogger; allocWatched = pthread_self(); }
        ~AllocWatch() { allocWatched = nullptr; }
    };
}

namespace
{
    int failures = 0;
    bool comparePixels = true;

    // Records whether the processor told the host its non-parameter state changed.
    struct StateWatch : juce::AudioProcessorListener
    {
        bool changed = false;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { changed |= d.nonParameterStateChanged; }
    };


    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    void set (ChordMonkeyProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.state.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    float get (ChordMonkeyProcessor& p, const juce::String& id) { return p.state.getRawParameterValue (id)->load(); }

    // The first component of type T under `c`, depth first.
    template <typename T>
    T* findChild (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (child))
                return t;
            if (auto* t = findChild<T> (*child))
                return t;
        }
        return nullptr;
    }

    // A block of the processor with these MIDI events at sample 0; returns its output.
    juce::MidiBuffer block (ChordMonkeyProcessor& p, std::initializer_list<juce::MidiMessage> events)
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        for (const auto& e : events)
            midi.addEvent (e, 0);
        p.processBlock (audio, midi);
        return midi;
    }

    std::vector<int> noteOns (const juce::MidiBuffer& midi)
    {
        std::vector<int> n;
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                n.push_back (m.getMessage().getNoteNumber());
        std::sort (n.begin(), n.end());
        return n;
    }

    int noteOffs (const juce::MidiBuffer& midi)
    {
        int n = 0;
        for (const auto m : midi)
            n += m.getMessage().isNoteOff() ? 1 : 0;
        return n;
    }

    // Fraction of pixels that differ by more than `tolerance` in any channel (1 if sizes differ).
    double difference (const juce::Image& a, const juce::Image& b, int tolerance)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return 1.0;
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        long differing = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto ca = da.getPixelColour (x, y), cb = db.getPixelColour (x, y);
                const int d = juce::jmax (std::abs (ca.getRed() - cb.getRed()), std::abs (ca.getGreen() - cb.getGreen()),
                                          std::abs (ca.getBlue() - cb.getBlue()));
                differing += d > tolerance ? 1 : 0;
            }
        return (double) differing / ((double) a.getWidth() * a.getHeight());
    }

    void snapshot (ChordMonkeyEditor& editor, const juce::String& name, const juce::File& fixtures, const juce::File& out, bool update)
    {
        editor.refreshNow();
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto write = [&image] (const juce::File& f)
        {
            f.deleteFile();
            juce::FileOutputStream s (f);
            juce::PNGImageFormat().writeImageToStream (image, s);
        };
        write (out.getChildFile (name + ".png"));

        const auto reference = fixtures.getChildFile (name + ".png");
        if (! comparePixels)
        {
            check (image.isValid() && image.getWidth() == editor.getWidth(), name + ": rendered (pixels not compared)");
            return;
        }
        if (update || ! reference.existsAsFile())
        {
            write (reference);
            check (true, name + ": reference written (" + reference.getFullPathName() + ")");
            return;
        }
        const double d = difference (image, juce::ImageFileFormat::loadFrom (reference), 24);
        check (d <= 0.005, name + ": " + juce::String (d * 100.0, 3) + "% of pixels differ from the reference");
    }

    // The fixed state the snapshots show.
    void arrange (ChordMonkeyProcessor& p)
    {
        set (p, params::progression, (float) progressionChoice (Progression::Pop));
        set (p, params::chordGroup, (float) (int) ChordGroup::Sevenths);
        set (p, params::arp, (float) (int) ArpPattern::ConvergeUp);
        set (p, params::velocity, (float) (int) VelocityCurve::House);
        set (p, params::seq, 1.0f);
        set (p, params::seqPattern, 2.0f);
        p.prepareToPlay (48000.0, 512);
        block (p, {});
        // Hold the E key (Pop: vi), two octaves up the D key (Pop: V), and D# (bIII, Phase 7).
        block (p, { juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), juce::MidiMessage::noteOn (1, 74, (juce::uint8) 100),
                    juce::MidiMessage::noteOn (1, 63, (juce::uint8) 100) });
        block (p, {});
        // Phase 6: a pattern in the workspace, its second slot shown sounding.
        Workspace w;
        std::string error;
        parseProgression ("I V vi:2 IV:2 ii7 V7sus4:2 V7:2", 0, w.patterns[0], error);
        w.patterns[0].slots[2].inversion = 1;
        p.setWorkspace (w);
        p.playingPattern = 0;
        p.playingSlot = 1;
    }
}

namespace
{
    bool equalParams (ChordMonkeyProcessor& a, ChordMonkeyProcessor& b, juce::StringArray& differ, bool presetOnly)
    {
        for (auto* param : a.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                if ((! presetOnly || params::isPresetParameter (withId->paramID)) && get (a, withId->paramID) != get (b, withId->paramID))
                    differ.add (withId->paramID);
        return differ.isEmpty();
    }

    void testPresets (const juce::File& fixtures)
    {
        std::cout << "== presets vs Chordism (Tests/fixtures/chordism_presets.json) ==\n";
        {
            const auto file = fixtures.getParentDirectory().getChildFile ("chordism_presets.json");
            const auto json = juce::JSON::parse (file);
            const auto* chordism = json["presets"].getArray();
            const auto& defaults = json["defaults"];
            const auto& factory = factoryPresets();
            // Chordism's come first, in its order; ChordMonkey's Songwriting presets (Phase 5) follow.
            int songwriting = 0;
            for (const auto& f : factory)
                songwriting += f.category == "Songwriting" ? 1 : 0;
            check (chordism != nullptr && chordism->size() + songwriting == (int) factory.size() && songwriting >= 10
                       && factory[(size_t) chordism->size()].category == "Songwriting",
                   juce::String (chordism != nullptr ? chordism->size() : 0) + " factory presets as in Chordism (" + json["chordism"].toString()
                       + "), then " + juce::String (songwriting) + " Songwriting presets");

            // Deliberate changes (ChordChimp's PLAN.md §3.4): Bright Melody's melody became a chord arp.
            auto changed = [] (const juce::String& name, const juce::String& id)
            {
                return name == "Bright Melody" && (id == params::arp || id == params::arpRate || id == params::arpRange);
            };

            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            int compared = 0, wrong = 0;
            juce::String firstWrong;
            for (int i = 0; chordism != nullptr && i < juce::jmin ((int) factory.size(), chordism->size()); ++i)
            {
                const auto& theirs = (*chordism)[i];
                const auto name = theirs["name"].toString();
                if (name != factory[(size_t) i].name || theirs["category"].toString() != factory[(size_t) i].category)
                {
                    if (wrong++ == 0)
                        firstWrong = "preset " + juce::String (i) + " is " + factory[(size_t) i].name + ", Chordism's " + name;
                    continue;
                }
                p->setCurrentProgram (i);
                for (auto* param : p->getParameters())
                {
                    auto* r = dynamic_cast<juce::RangedAudioParameter*> (param);
                    if (r == nullptr || ! params::isPresetParameter (r->paramID) || changed (name, r->paramID))
                        continue;
                    const auto& set = theirs["values"];
                    const juce::Identifier id (r->paramID);
                    double want = set.hasProperty (id) ? (double) set[id] : (double) defaults[id];
                    if (r->paramID == params::progression)
                        want = progressionChoice ((Progression) (int) want);   // Chordism's value is the enum's
                    ++compared;
                    if (std::abs (get (*p, r->paramID) - want) > 1.0e-3 && wrong++ == 0)
                        firstWrong = name + ": " + r->paramID + " = " + juce::String (get (*p, r->paramID)) + ", Chordism's " + juce::String (want);
                }
            }
            int presetParameters = 0;
            for (auto* param : p->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param); r != nullptr && params::isPresetParameter (r->paramID))
                    ++presetParameters;
            // 24 of Chordism's, and Phase 4's five (Voicing, Bass, Style, Swing, Humanize): Chordism has
            // none of them, so every factory preset leaves them at their defaults (0).
            // ... and Pattern (a preset parameter since Phase 5, default A).
            check (wrong == 0 && presetParameters == 30 && chordism != nullptr && compared == chordism->size() * presetParameters - 3,
                   juce::String (compared) + " preset values (every preset parameter of every preset) are Chordism's"
                       + (firstWrong.isEmpty() ? juce::String() : " (" + firstWrong + ")"));
            p->setCurrentProgram (chordism != nullptr ? chordism->size() - 1 : 0);
            check (p->getPresetName() == "Bright Melody" && (int) get (*p, params::arp) == (int) ArpPattern::LinearUp && (int) get (*p, params::arpRange) == 2,
                   "Bright Melody plays a chord arp (Linear Up, range 2) in place of its melody");
            set (*p, params::keyOctave, 1.0f);
            p->setCurrentProgram (2);
            check ((int) get (*p, params::keyOctave) == 1, "loading a preset leaves Key octave alone (Phase 8: it isn't a preset parameter)");
            set (*p, params::keyOctave, 0.0f);
        }

        std::cout << "== presets ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            auto fresh = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            const auto& factory = factoryPresets();
            check (p->getNumPrograms() == (int) factory.size() && factory.size() >= 21 && p->getProgramName (0) == "Init",
                   juce::String (factory.size() - 1) + " factory presets, plus Init as program 0");
            int wrong = 0;
            juce::String firstWrong;
            for (int i = 1; i < (int) factory.size(); ++i)
            {
                p->setCurrentProgram (i);
                for (const auto& [id, value] : factory[(size_t) i].values)
                    if (std::abs (get (*p, id) - value) > 1.0e-3f)
                    {
                        if (wrong++ == 0)
                            firstWrong = factory[(size_t) i].name + ": " + id + " = " + juce::String (get (*p, id)) + ", want " + juce::String (value);
                    }
                // Everything it doesn't set is the default.
                for (auto* param : p->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                    {
                        bool listed = false;
                        for (const auto& kv : factory[(size_t) i].values)
                            listed |= kv.first == r->paramID;
                        if (! listed && std::abs (r->getValue() - r->getDefaultValue()) > 1.0e-6f && params::isPresetParameter (r->paramID))
                            if (wrong++ == 0)
                                firstWrong = factory[(size_t) i].name + ": " + r->paramID + " isn't its default";
                    }
                if (p->getCurrentProgram() != i || p->getPresetName() != factory[(size_t) i].name)
                    if (wrong++ == 0)
                        firstWrong = factory[(size_t) i].name + ": program " + juce::String (p->getCurrentProgram());
            }
            check (wrong == 0, "every factory program sets its values over the defaults" + (firstWrong.isEmpty() ? juce::String() : " (" + firstWrong + ")"));
            p->setCurrentProgram (0);
            juce::StringArray differ;
            check (equalParams (*p, *fresh, differ, false) && p->getPresetName() == "Init", "Init is the defaults (" + differ.joinIntoString (", ") + ")");

            // A user preset's state into a new instance; Enable and the test rig stay.
            set (*p, params::key, 5.0f);
            set (*p, params::scale, (float) (int) Scale::Dorian);
            set (*p, params::chordGroup, (float) (int) ChordGroup::Ninths);
            set (*p, params::stack, (float) (int) StackMode::PlusOne);
            set (*p, params::seqPattern, 3.0f);
            const auto tree = p->presetState();
            auto other = std::make_unique<ChordMonkeyProcessor>();
            set (*other, params::enable, 0.0f);
            set (*other, params::testPattern, 1.0f);
            other->applyPreset (tree, "User", "Mine");
            differ.clear();
            check (equalParams (*p, *other, differ, true)
                       && get (*other, params::enable) == 0.0f && get (*other, params::testPattern) == 1.0f && other->getPresetName() == "Mine",
                   "a preset's state loads every preset parameter, and leaves Enable and the test pattern ("
                       + differ.joinIntoString (", ") + ")");
            check (! tree.getChildWithProperty ("id", params::enable).isValid() && ! tree.getChildWithProperty ("id", params::testPattern).isValid()
                       && ! tree.getChildWithName ("UI").isValid(),
                   "a preset doesn't hold Enable, the test rig or the editor's preferences");

            // A load ends every note.
            block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            p->setCurrentProgram (3);
            const auto out = block (*p, {});
            check (noteOffs (out) >= 3 && p->getPresetName() == factory[3].name, "loading a preset while a chord sounds ends its notes ("
                                                                                     + juce::String (noteOffs (out)) + " note-offs)");

            // The program and the name are saved with the plug-in.
            juce::MemoryBlock saved;
            p->getStateInformation (saved);
            auto restored = std::make_unique<ChordMonkeyProcessor>();
            restored->setStateInformation (saved.getData(), (int) saved.getSize());
            check (restored->getCurrentProgram() == 3 && restored->getPresetName() == factory[3].name,
                   "the current program and preset name restore with the plug-in's state");
        }

        std::cout << "== Chordism presets ==\n";
        {
            // A Chordism user preset: its own root tag, Smart Scale and shifter settings, edits.
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("chordmonkey-snapshot-chordism.xml");
            file.replaceWithText ("<Chordism name=\"Old\" category=\"User\"><PARAM id=\"key\" value=\"4\"/><PARAM id=\"chordGroup\" value=\"5\"/>"
                                  "<PARAM id=\"chordOctave\" value=\"1\"/><PARAM id=\"progression\" value=\"2\"/><PARAM id=\"mapping\" value=\"2\"/><PARAM id=\"split\" value=\"72\"/>"
                                  "<PARAM id=\"shifter1Kind\" value=\"1\"/><PARAM id=\"pattern2Arp\" value=\"12\"/>"
                                  "<ChordEditor group=\"5\"><Degree index=\"4\"><Step index=\"4\" on=\"1\"/></Degree></ChordEditor></Chordism>");
            const auto tree = PresetManager::readFile (file);
            file.deleteFile();
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            if (tree.isValid())
                p->applyPreset (tree, "User", "Old");
            const auto chord = noteOns (block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) }));
            block (*p, { juce::MidiMessage::noteOff (1, 48) });
            check (tree.isValid() && (int) get (*p, params::key) == 4 && (int) get (*p, params::chordGroup) == 5
                       && (int) get (*p, params::chordOctave) == 1 && chord == std::vector<int> { 64, 68, 71, 75 }
                       && (int) get (*p, params::progression) == progressionChoice (Progression::Pop),
                   "a Chordism preset file loads its kept settings (E, Sevenths, octave +1, Pop as this plug-in's choice); "
                   "the rest are ignored and C plays Emaj7");
        }
    }
}

namespace
{
    // A host transport with a loop and a tempo (Phase 8).
    struct LoopPlayHead : juce::AudioPlayHead
    {
        bool playing = true, looping = true;
        double ppq = 0.0, bpm = 140.0, loopStart = 0.0, loopEnd = 2.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setIsLooping (looping);
            info.setLoopPoints (juce::AudioPlayHead::LoopPoints { loopStart, loopEnd });
            info.setPpqPosition (ppq);
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            return info;
        }
        void advance (int samples, double rate)
        {
            if (! playing)
                return;
            ppq += samples * bpm / (60.0 * rate);
            if (looping && ppq >= loopEnd)
                ppq = loopStart + (ppq - loopEnd);
        }
    };

    // Every effect at once, the test rig playing.
    void everythingOn (ChordMonkeyProcessor& p)
    {
        set (p, params::chordGroup, (float) (int) ChordGroup::Thirteenths);
        set (p, params::stack, (float) (int) StackMode::PlusOneAndTwo);
        set (p, params::arp, (float) (int) ArpPattern::BoomerangUp);
        set (p, params::arpRate, (float) (numPatternRates - 1));
        set (p, params::arpRange, 3.0f);
        set (p, params::repeat, 1.0f);
        set (p, params::seq, 1.0f);
        set (p, params::velocity, (float) (int) VelocityCurve::House);
        set (p, params::hold, (float) (int) HoldMode::Bar);
        set (p, params::voiceLeading, 1.0f);
        set (p, params::voicing, (float) (int) VoicingMode::Wide);   // Phase 4
        set (p, params::bass, (float) (int) BassMode::RootFifth);
        set (p, params::style, (float) (numStyles - 3));   // Alberti
        set (p, params::swing, 60.0f);
        set (p, params::humanize, 100.0f);
        set (p, params::strum, (float) (int) StrumMode::Random);
        set (p, params::testPattern, 1.0f);
    }

    // A parameter is a sane value: finite, in 0..1.
    int insaneParameters (ChordMonkeyProcessor& p)
    {
        int bad = 0;
        for (auto* param : p.getParameters())
        {
            const float v = param->getValue();
            bad += std::isfinite (v) && v >= 0.0f && v <= 1.0f ? 0 : 1;
        }
        return bad;
    }

    // Plays and releases chord key C: the processor still works.
    bool playsAChord (ChordMonkeyProcessor& p)
    {
        set (p, params::enable, 1.0f);
        set (p, params::testPattern, 0.0f);
        set (p, params::hold, 0.0f);
        set (p, params::arp, 0.0f);
        set (p, params::strum, 0.0f);
        set (p, params::seq, 0.0f);
        block (p, {});
        const auto on = noteOns (block (p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) }));
        block (p, { juce::MidiMessage::noteOff (1, 48) });
        block (p, {});
        return ! on.empty() && p.soundingNotes.load() == 0;
    }

    void testPhase8()
    {
        std::cout << "== Phase 8: no allocation on the audio thread ==\n";
        {
            const int before = allocCount.load();
            {
                AllocWatch watch;
                allocSink = new int (8);          // stored through a volatile: the compiler can't elide it
                delete static_cast<int*> (allocSink);
                allocSink = std::malloc (1 << 20);
                std::free (allocSink);
            }
            check (allocCount.load() - before >= 2, "the malloc hook sees allocations (" + juce::String (allocCount.load() - before) + " counted)");
        }
        {
            // Two instances with everything on, under a looping transport; between blocks
            // (as the host and the message thread would) parameters change, the transport stops,
            // starts and moves its loop, on-screen keys go down, bypass toggles.
            auto a = std::make_unique<ChordMonkeyProcessor>();
            auto b = std::make_unique<ChordMonkeyProcessor>();
            LoopPlayHead head;
            for (auto* p : { a.get(), b.get() })
            {
                p->setPlayHead (&head);
                p->prepareToPlay (48000.0, 256);
                everythingOn (*p);
            }
            juce::AudioBuffer<float> audio (2, 256);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);    // a host that reserves enough for any block
            std::mt19937 rng (8);
            int allocations = 0, blocks = 0, peak = 0, bypassed = 0;
            for (int i = 0; i < 4000; ++i)
            {
                if (rng() % 50 == 0) set (*a, params::key, (float) (rng() % 12));
                if (rng() % 70 == 0) set (*b, params::arp, (float) (rng() % numArpPatterns));
                if (rng() % 90 == 0) set (*a, params::chordGroup, (float) (rng() % numChordGroups));
                if (rng() % 60 == 0) set (*a, params::style, (float) (rng() % numStyles));   // Phase 4: styles with and without Arp
                if (rng() % 60 == 0) set (*a, params::arp, (float) (rng() % 2 == 0 ? 0 : 1 + rng() % (numArpPatterns - 1)));
                if (rng() % 200 == 0) head.playing = ! head.playing;
                if (rng() % 300 == 0) { head.loopStart = (double) (rng() % 4); head.loopEnd = head.loopStart + 0.5 + (rng() % 4); }
                if (rng() % 40 == 0) a->pushUiNote (48 + (int) (rng() % 30), 100, rng() % 2 == 0);
                if (rng() % 100 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // the timers
                for (auto* p : { a.get(), b.get() })
                {
                    midi.clear();
                    if (rng() % 8 == 0)
                        midi.addEvent (rng() % 2 == 0 ? juce::MidiMessage::noteOn (1, 36 + (int) (rng() % 48), (juce::uint8) 90)
                                                      : juce::MidiMessage::noteOff (1, 36 + (int) (rng() % 48)), (int) (rng() % 256));
                    const bool bypass = p == b.get() && i % 500 > 480;
                    const int before = allocCount.load();
                    {
                        AllocWatch watch;
                        if (bypass)
                            p->processBlockBypassed (audio, midi);
                        else
                            p->processBlock (audio, midi);
                    }
                    allocations += allocCount.load() - before;
                    bypassed += bypass ? 1 : 0;
                    peak = std::max (peak, midi.getNumEvents());
                    ++blocks;
                }
                head.advance (256, 48000.0);
            }
            check (allocations == 0, juce::String (blocks) + " blocks of two instances with everything on (" + juce::String (bypassed)
                                         + " bypassed, up to " + juce::String (peak) + " events out): " + juce::String (allocations)
                                         + " allocations in processBlock");

            // A host buffer the size JUCE's VST3 wrapper reserves (2048 bytes, ~227 events), and
            // stacked thirteenths without the arp (up to 128 notes a block, restruck): the host's
            // buffer grows in the first blocks with more output; after that it holds.
            set (*a, params::arp, 0.0f);
            set (*a, params::seq, 0.0f);
            set (*a, params::testPattern, 0.0f);
            juce::MidiBuffer small;
            small.ensureSize (2048);
            int growth = 0, late = 0, most = 0;
            for (int i = 0; i < 1000; ++i)
            {
                small.clear();
                if (i % 2 == 0)
                    for (int n = 0; n < 60; ++n)
                        small.addEvent (juce::MidiMessage::noteOn (1, 36 + n % 24, (juce::uint8) 90), n);
                const int before = allocCount.load();
                {
                    AllocWatch watch;
                    a->processBlock (audio, small);
                }
                const int made = allocCount.load() - before;
                most = std::max (most, small.getNumEvents());
                growth += made;
                late += i >= 100 ? made : 0;
                head.advance (256, 48000.0);
            }
            check (late == 0, "with JUCE's VST3 host buffer (2048 bytes) and up to " + juce::String (most)
                                  + " events a block: the host's buffer grew " + juce::String (growth) + " time(s) in the first 100 blocks, never after");
            for (auto* p : { a.get(), b.get() })
                p->setPlayHead (nullptr);
        }

        std::cout << "== Phase 8: damaged saved states ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (44100.0, 512);
            std::mt19937 rng (88);
            int insane = 0;
            for (int i = 0; i < 300; ++i)
            {
                juce::MemoryBlock junk ((size_t) (rng() % 4096));
                for (size_t k = 0; k < junk.getSize(); ++k)
                    junk[k] = (char) (rng() & 0xff);
                if (i % 3 == 0 && junk.getSize() > 8)
                    std::memcpy (junk.getData(), "VC2!", 4);   // JUCE's binary-XML header, garbage after
                p->setStateInformation (junk.getData(), (int) junk.getSize());
                insane += insaneParameters (*p);
            }
            p->setStateInformation (nullptr, 0);
            p->setStateInformation ("xx", -5);
            check (insane == 0 && playsAChord (*p), "300 blobs of random bytes (a third with a valid header) are ignored; the instance still plays");

            const char* values[] = { "nan", "inf", "-inf", "1e300", "-1e300", "abc", "", "0x7fffffff", "-0" };
            int worst = 0;
            for (const char* v : values)
            {
                juce::String xml ("<ChordMonkey program=\"99999\" presetName=\"\">");
                for (auto* param : p->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                        xml << "<PARAM id=\"" << r->paramID << "\" value=\"" << v << "\"/>";
                xml << "<PARAM id=\"noSuchParameter\" value=\"1\"/><PARAM id=\"key\" value=\"3\"/>"
                       "<ChordEditor group=\"999\"><Degree index=\"99\"/><Degree index=\"2\"><Step index=\"-1\" on=\"1\"/>"
                       "<Step index=\"3\" on=\"1\" acc=\"50\" oct=\"99999\"/></Degree></ChordEditor>"
                       "<UI keyboard=\"maybe\" width=\"-1\" height=\"1e9\"/></ChordMonkey>";
                juce::MemoryBlock state;
                juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (xml), state);
                p->setStateInformation (state.getData(), (int) state.getSize());
                worst = std::max (worst, insaneParameters (*p));
                std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());   // opens with the UI's garbage
                editor.reset();
                worst += p->getCurrentProgram() < 0 || p->getCurrentProgram() >= p->getNumPrograms() ? 1 : 0;
            }
            check (worst == 0 && playsAChord (*p), "states whose every value is nan, inf, -inf, +/-1e300, text or empty, with unknown IDs, "
                                                   "an unknown child (Chordism's Chord Editor) and a nonsense UI: every parameter stays "
                                                   "finite and in range, the editor opens, the instance plays");

            // A preset with a NaN gives that parameter its default.
            auto tree = p->presetState();
            tree.getChildWithProperty ("id", params::key).setProperty ("value", "nan", nullptr);
            set (*p, params::key, 5.0f);
            p->applyPreset (tree, "User", "Damaged");
            check ((int) get (*p, params::key) == 0, "a preset whose Key is NaN loads Key's default (C)");
        }

        std::cout << "== Phase 8: host edge cases ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> none (2, 0), huge (2, 8192), mono (1, 512);
            auto run = [&p] (juce::AudioBuffer<float>& audio, std::initializer_list<juce::MidiMessage> events, bool bypass = false)
            {
                juce::MidiBuffer midi;
                for (const auto& e : events)
                    midi.addEvent (e, 0);
                if (bypass)
                    p->processBlockBypassed (audio, midi);
                else
                    p->processBlock (audio, midi);
                return midi;
            };

            const auto zero = run (none, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            const auto big = run (huge, { juce::MidiMessage::noteOff (1, 48), juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) });
            const auto monoOut = run (mono, { juce::MidiMessage::noteOff (1, 50) });
            check (noteOns (zero) == std::vector<int> { 48, 52, 55 } && noteOns (big) == std::vector<int> { 50, 53, 57 } && noteOffs (big) == 3
                       && noteOffs (monoOut) == 3 && p->soundingNotes.load() == 0,
                   "a zero-length block plays its key; an 8192-sample block after prepareToPlay (..., 512) and a mono block work");

            run (mono, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            const auto bypassed = run (mono, { juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90) }, true);
            check (noteOffs (bypassed) == 3 && noteOns (bypassed) == std::vector<int> { 64 }, "host bypass while a chord sounds ends it and passes input through");
            run (mono, { juce::MidiMessage::noteOff (1, 64), juce::MidiMessage::noteOff (1, 48) }, true);

            run (mono, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            p->releaseResources();
            const auto afterRelease = run (mono, {});
            p->prepareToPlay (96000.0, 128);
            run (mono, { juce::MidiMessage::noteOff (1, 48), juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100) });
            p->prepareToPlay (44100.0, 2048);
            const auto afterRate = run (mono, {});
            run (mono, { juce::MidiMessage::noteOff (1, 52), juce::MidiMessage::noteOn (1, 53, (juce::uint8) 100) });
            const auto allOff = run (mono, { juce::MidiMessage::allNotesOff (1) });
            int cc = 0;
            for (const auto m : allOff)
                cc += m.getMessage().isAllNotesOff() ? 1 : 0;
            check (noteOffs (afterRelease) == 3 && noteOffs (afterRate) == 3 && noteOffs (allOff) == 3 && cc == 1 && p->soundingNotes.load() == 0,
                   "releaseResources, a sample-rate change and the host's all-notes-off each end a sounding chord (the CC passes on)");
            run (mono, { juce::MidiMessage::noteOff (1, 53) });

            const juce::uint8 sysex[] = { 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00 };
            const auto passed = run (mono, { juce::MidiMessage::createSysExMessage (sysex, (int) sizeof (sysex)) });
            bool sysexIntact = false;
            for (const auto m : passed)
                sysexIntact |= m.getMessage().isSysEx() && m.getMessage().getSysExDataSize() == (int) sizeof (sysex)
                            && std::memcmp (m.getMessage().getSysExData(), sysex, sizeof (sysex)) == 0;
            check (sysexIntact, "SysEx passes through unchanged");

            // A flood: 2900 note-ons, then every key up, in one block.
            {
                juce::MidiBuffer flood;
                for (int i = 0; i < 2900; ++i)
                    flood.addEvent (juce::MidiMessage::noteOn (1, 36 + i % 48, (juce::uint8) 100), i % 400);
                for (int n = 0; n < 128; ++n)
                    flood.addEvent (juce::MidiMessage::noteOff (1, n), 450);
                juce::AudioBuffer<float> audio (2, 512);
                p->processBlock (audio, flood);
                run (mono, {});
                check (p->soundingNotes.load() == 0, "2900 note-ons and 128 key-ups in one block (input holds 2048): the key-ups get in, nothing sounds after");
            }
        }

        std::cout << "== Phase 8: many instances ==\n";
        {
            std::vector<std::unique_ptr<ChordMonkeyProcessor>> all;
            for (int i = 0; i < 64; ++i)
            {
                all.push_back (std::make_unique<ChordMonkeyProcessor>());
                all.back()->prepareToPlay (44100.0, 256);
            }
            int played = 0;
            for (auto& p : all)
                played += noteOns (block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) })).size() == 3 ? 1 : 0;
            std::shuffle (all.begin(), all.end(), std::mt19937 (64));
            while (all.size() > 2)
                all.pop_back();
            int ended = 0;
            for (auto& p : all)
            {
                block (*p, { juce::MidiMessage::noteOff (1, 48) });
                ended += p->soundingNotes.load() == 0 ? 1 : 0;
            }
            check (played == 64 && ended == 2, "64 instances play; destroyed in random order, the last two still work");
        }
    }

    // A tune in A minor (Frere Jacques in minor, as Mahler's), at 100 bpm: (pitch, beats).
    const std::vector<std::pair<int, double>>& minorTune()
    {
        static const std::vector<std::pair<int, double>> t {
            { 57, 1 }, { 59, 1 }, { 60, 1 }, { 57, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 57, 1 },
            { 60, 1 }, { 62, 1 }, { 64, 2 }, { 60, 1 }, { 62, 1 }, { 64, 2 },
            { 64, 0.5 }, { 65, 0.5 }, { 64, 0.5 }, { 62, 0.5 }, { 60, 1 }, { 57, 1 },
            { 64, 0.5 }, { 65, 0.5 }, { 64, 0.5 }, { 62, 0.5 }, { 60, 1 }, { 57, 1 },
            { 57, 1 }, { 52, 1 }, { 57, 2 }, { 57, 1 }, { 52, 1 }, { 57, 2 } };
        return t;
    }

    // Plays the tune, transposed, into the processor block by block (48 kHz, 512 samples), each
    // note held for 90% of its beats; returns the note-ons that came out.
    int playTune (ChordMonkeyProcessor& p, int transpose = 0)
    {
        constexpr double rate = 48000.0, beat = 0.6;
        std::vector<std::pair<int64_t, juce::MidiMessage>> events;
        double t = 0.0;
        for (const auto& [pitch, beats] : minorTune())
        {
            events.push_back ({ (int64_t) (t * rate), juce::MidiMessage::noteOn (1, pitch + transpose, (juce::uint8) 100) });
            events.push_back ({ (int64_t) ((t + 0.9 * beats * beat) * rate), juce::MidiMessage::noteOff (1, pitch + transpose) });
            t += beats * beat;
        }
        std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        int ons = 0;
        size_t i = 0;
        for (int64_t pos = 0; pos < (int64_t) ((t + 3.0) * rate); pos += 512)
        {
            midi.clear();
            for (; i < events.size() && events[i].first < pos + 512; ++i)
                midi.addEvent (events[i].second, (int) (events[i].first - pos));
            p.processBlock (audio, midi);
            for (const auto m : midi)
                ons += m.getMessage().isNoteOn() ? 1 : 0;
        }
        return ons;
    }

    // Records the gestures and value changes a processor reports, in order.
    struct GestureWatch : juce::AudioProcessorListener
    {
        std::vector<std::pair<char, int>> calls;   // ('b' | 'v' | 'e', parameter index)
        void audioProcessorParameterChanged (juce::AudioProcessor*, int i, float) override { calls.push_back ({ 'v', i }); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int i) override { calls.push_back ({ 'b', i }); }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int i) override { calls.push_back ({ 'e', i }); }

        // Every parameter in `ids` began, changed and ended, all the beginnings first and all
        // the ends last.
        bool together (ChordMonkeyProcessor& p, std::initializer_list<const char*> ids) const
        {
            std::set<int> want;
            for (const char* id : ids)
                if (auto* param = dynamic_cast<juce::AudioProcessorParameterWithID*> (p.state.getParameter (id)))
                    want.insert (param->getParameterIndex());
            std::set<int> b, v, e;
            int phase = 0;
            for (const auto& [kind, i] : calls)
            {
                const int k = kind == 'b' ? 0 : (kind == 'v' ? 1 : 2);
                if (k < phase)
                    return false;
                phase = k;
                (k == 0 ? b : (k == 1 ? v : e)).insert (i);
            }
            return b == want && v == want && e == want;
        }
    };

    void fromAnotherThread (ChordMonkeyProcessor& p, const char* id, float plain)
    {
        std::thread host ([&] { set (p, id, plain); });
        host.join();
    }

    void runTimers (int ms = 200) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    void testAnalyse (const juce::File& fixtures, const juce::File& out, bool update)
    {
        std::cout << "== Analyse: from the editor ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorIfNeeded());
            auto* editor = dynamic_cast<ChordMonkeyEditor*> (base.get());
            auto& heard = editor->getGlobals().heard;
            editor->refreshNow();
            const auto idle = heard.getText();
            heard.click (false);
            editor->refreshNow();
            const auto listening = heard.getText();
            const bool listen = p->analyseMode() == params::AnalyseMode::Listen;
            const int through = playTune (*p);
            editor->refreshNow();
            const auto guess = p->readKeyGuess();
            check (idle == "Analyse" && listen && listening.startsWith ("Listening") && through == (int) minorTune().size()
                       && guess.has() && guess.tonic == 9 && guess.minor && heard.getText().startsWith ("A minor")
                       && heard.getText().endsWith ("%") && ! heard.closeBounds().isEmpty(),
                   "Analyse, then an A-minor tune: every note passes through, the box shows \"" + heard.getText() + "\"");
            snapshot (*editor, "editor-heard", fixtures, out, update);

            GestureWatch watch;
            p->addListener (&watch);
            heard.click (false);
            p->removeListener (&watch);
            block (*p, {});
            editor->refreshNow();
            std::set<int> classes;
            for (const int n : noteOns (block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) })))
                classes.insert (n % 12);
            block (*p, { juce::MidiMessage::noteOff (1, 48) });
            check ((int) get (*p, params::key) == 9 && (int) get (*p, params::scale) == (int) Scale::NaturalMinor
                       && p->analyseMode() == params::AnalyseMode::Off && watch.together (*p, { params::key, params::scale, params::analyse })
                       && classes == std::set<int> { 9, 0, 4 },
                   "clicking the guess sets Key = A, Scale = Natural Minor and Analyse = Off, as one set of gestures; C then plays A minor");
            check (heard.getText() == "A minor" && heard.closeBounds().isEmpty(), "back to chords, the box shows the key heard, matching Key (\"" + heard.getText() + "\")");

            heard.click (false);
            editor->refreshNow();
            const bool fresh = ! p->readKeyGuess().has() || p->analyseMode() == params::AnalyseMode::Listen;
            block (*p, {});
            editor->refreshNow();
            heard.click (true);
            editor->refreshNow();
            check (fresh && ! p->readKeyGuess().has() && p->analyseMode() == params::AnalyseMode::Off && (int) get (*p, params::key) == 9
                       && heard.getText() == "Analyse",
                   "Analyse again starts afresh; the cross goes back to chords and leaves Key alone");
            p->editorBeingDeleted (base.get());
            base.reset();
        }

        std::cout << "== Analyse: from the host ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorIfNeeded());
            auto* editor = dynamic_cast<ChordMonkeyEditor*> (base.get());
            fromAnotherThread (*p, params::analyse, (float) params::AnalyseMode::Listen);
            editor->refreshNow();
            const bool follows = editor->getGlobals().heard.getText().startsWith ("Listening");
            playTune (*p);
            fromAnotherThread (*p, params::analyse, (float) params::AnalyseMode::UseGuess);
            const bool waits = (int) get (*p, params::key) == 0;
            runTimers();
            check (follows && waits && (int) get (*p, params::key) == 9 && (int) get (*p, params::scale) == (int) Scale::NaturalMinor
                       && p->analyseMode() == params::AnalyseMode::Off,
                   "the host sets Listen (the box follows), then Use guess: the timer sets Key = A, Scale = Natural Minor, Analyse = Off");

            // From Dorian, a D-minor guess keeps Dorian.
            set (*p, params::scale, (float) (int) Scale::Dorian);
            fromAnotherThread (*p, params::analyse, (float) params::AnalyseMode::Listen);
            block (*p, {});
            playTune (*p, 5);
            fromAnotherThread (*p, params::analyse, (float) params::AnalyseMode::UseGuess);
            runTimers();
            check ((int) get (*p, params::key) == 2 && (int) get (*p, params::scale) == (int) Scale::Dorian && p->analyseMode() == params::AnalyseMode::Off,
                   "a D-minor guess on Dorian sets Key = D and keeps Dorian");
            p->editorBeingDeleted (base.get());
            base.reset();

            auto q = std::make_unique<ChordMonkeyProcessor>();
            q->prepareToPlay (48000.0, 512);
            set (*q, params::key, 4.0f);
            fromAnotherThread (*q, params::analyse, (float) params::AnalyseMode::UseGuess);
            block (*q, {});
            runTimers();
            check ((int) get (*q, params::key) == 4 && q->analyseMode() == params::AnalyseMode::Listen,
                   "Use guess with nothing heard leaves Key alone and listens");
        }

        std::cout << "== Analyse: the test rig's file ==\n";
        {
            const auto file = ChordMonkeyProcessor::heardKeyFile();
            file.deleteFile();
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 512);
            set (*p, params::analyse, (float) params::AnalyseMode::Listen);
            playTune (*p);
            runTimers();
            const bool notWritten = ! file.existsAsFile();
            set (*p, params::testPattern, 1.0f);
            runTimers();
            const auto json = juce::JSON::parse (file);
            check (notWritten && json["analyse"] == "Listen" && json["state"] == "confident" && json["key"] == "A" && json["mode"] == "minor"
                       && (double) json["fit"] > 0.7 && json["runnerUp"].isObject() && (int) json["notes"] == (int) minorTune().size(),
                   "with Test pattern on, the guess is written to " + file.getFullPathName() + ": " + juce::JSON::toString (json, true));
            set (*p, params::analyse, (float) params::AnalyseMode::UseGuess);
            runTimers();
            const auto after = juce::JSON::parse (file);
            check (after["analyse"] == "Off" && (int) get (*p, params::key) == 9, "after Use guess the file says Analyse is Off");
        }

        std::cout << "== Analyse: state and presets ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            set (*p, params::analyse, (float) params::AnalyseMode::Listen);
            juce::MemoryBlock saved;
            p->getStateInformation (saved);
            auto restored = std::make_unique<ChordMonkeyProcessor>();
            restored->setStateInformation (saved.getData(), (int) saved.getSize());
            const bool notSaved = restored->analyseMode() == params::AnalyseMode::Off;
            p->applyPreset (PresetManager::factoryTree (3), "Genre", "Preset 3");
            check (notSaved && p->analyseMode() == params::AnalyseMode::Listen && ! params::isPresetParameter (params::analyse)
                       && ! p->presetState().getChildWithProperty ("id", params::analyse).isValid(),
                   "a state saved in Listen opens with Analyse Off; a preset neither holds Analyse nor changes it");
        }

        std::cout << "== Analyse: the globals fit ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorIfNeeded());
            auto* editor = dynamic_cast<ChordMonkeyEditor*> (base.get());
            auto& globals = editor->getGlobals();
            const auto comboFont = ui::font (13.0f);
            for (const auto size : { juce::Point<int> { ChordMonkeyEditor::minWidth, ChordMonkeyEditor::minHeight },
                                     juce::Point<int> { ChordMonkeyEditor::defaultWidth, ChordMonkeyEditor::defaultHeight },
                                     juce::Point<int> { 1200, 800 }, juce::Point<int> { ChordMonkeyEditor::maxWidth, ChordMonkeyEditor::maxHeight } })
            {
                editor->setSize (size.x, size.y);
                juce::StringArray problems;
                juce::Rectangle<int> last;
                for (auto* child : globals.getChildren())
                {
                    if (child->getBounds().intersects (last) || child->getRight() > globals.getWidth())
                        problems.add ("overlap at " + child->getBounds().toString());
                    last = child->getBounds();
                    // A combo box's text has its width less the arrow and the label's insets (Theme.h).
                    if (auto* box = findChild<juce::ComboBox> (*child))
                        for (int i = 0; i < box->getNumItems(); ++i)
                        {
                            const int room = box->getWidth() - ui::LookAndFeel::comboArrowWidth - 2 * ui::LookAndFeel::comboTextInset;
                            if (const auto t = box->getItemText (i); juce::GlyphArrangement::getStringWidthInt (comboFont, t) > room)
                                problems.add ("\"" + t + "\" (" + juce::String (juce::GlyphArrangement::getStringWidthInt (comboFont, t)) + " px) in "
                                              + juce::String (room) + " px");
                        }
                }
                // The Heard box (while analysing, with its cross): its shortest form always fits, its
                // longest from the default size up.
                set (*p, params::analyse, (float) params::AnalyseMode::Listen);
                editor->refreshNow();
                auto& heard = globals.heard;
                if (! heard.fits ("F#m?") || (size.x >= ChordMonkeyEditor::defaultWidth && ! heard.fits ("Ab major 100%")))
                    problems.add ("Heard at " + juce::String (heard.getWidth()) + " px");
                const juce::String form = heard.fits ("Ab major 100%") ? "Ab major 100%" : (heard.fits ("Abm 100%") ? "Abm 100%" : "Abm");
                set (*p, params::analyse, 0.0f);
                editor->refreshNow();
                check (problems.isEmpty(), juce::String (size.x) + " x " + juce::String (size.y) + ": the globals don't overlap and every choice fits; "
                                               + "the Heard box shows \"" + form + "\"" + (problems.isEmpty() ? juce::String() : ": " + problems.joinIntoString (", ")));
            }
        }

        std::cout << "== Analyse: no allocation while listening ==\n";
        {
            auto p = std::make_unique<ChordMonkeyProcessor>();
            p->prepareToPlay (48000.0, 256);
            set (*p, params::analyse, (float) params::AnalyseMode::Listen);
            juce::AudioBuffer<float> audio (2, 256);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);
            std::mt19937 rng (3);
            int allocations = 0;
            std::vector<int> down;
            for (int i = 0; i < 4000; ++i)
            {
                midi.clear();
                if (i % 4 == 0)
                {
                    const int note = 48 + (int) (rng() % 36);
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (1 + rng() % 127)), (int) (rng() % 256));
                    down.push_back (note);
                }
                if (down.size() > 3)
                {
                    midi.addEvent (juce::MidiMessage::noteOff (1, down.front()), 255);
                    down.erase (down.begin());
                }
                if (i % 1000 == 999)
                    set (*p, params::analyse, (float) (i % 2000 == 999 ? params::AnalyseMode::UseGuess : params::AnalyseMode::Listen));
                const int before = allocCount.load();
                {
                    AllocWatch watch;
                    p->processBlock (audio, midi);
                }
                allocations += allocCount.load() - before;
            }
            check (allocations == 0 && p->readKeyGuess().notes > 0, "4000 blocks in Listen with notes (and Use guess): "
                                                                         + juce::String (allocations) + " allocations in processBlock");
        }
    }

    // processBlock's CPU time: `instances` processors one after another on one thread (as a
    // host's audio thread runs them), for `seconds` of audio at each block size.
    int runBench (double seconds)
    {
        constexpr int instances = 8;
        constexpr double rate = 44100.0;
        std::cout << "== Phase 8: CPU per instance (" << instances << " instances, " << seconds << " s of audio at 44.1 kHz, 140 BPM, 2-beat loop) ==\n"
                  << "   scenario         block   mean % of real time   99.9th percentile / worst block (% of a block's duration)\n";
        const char* names[] = { "idle", "test pattern", "everything on", "listening" };
        double worstMean = 0.0;
        for (int scenario = 0; scenario < 4; ++scenario)
            for (const int blockSize : { 32, 128, 512 })
            {
                std::vector<std::unique_ptr<ChordMonkeyProcessor>> procs;
                LoopPlayHead head;
                for (int i = 0; i < instances; ++i)
                {
                    procs.push_back (std::make_unique<ChordMonkeyProcessor>());
                    auto& p = *procs.back();
                    p.setPlayHead (&head);
                    p.prepareToPlay (rate, blockSize);
                    if (scenario == 1)
                        set (p, params::testPattern, 1.0f);
                    if (scenario == 2)
                        everythingOn (p);
                    if (scenario == 3)
                        set (p, params::analyse, (float) params::AnalyseMode::Listen);   // a tune passing through, analysed
                }
                // Listening: sixteenth notes up and down a scale (at 140 BPM, 16 a bar).
                const int sixteenth = (int) (rate * 60.0 / 140.0 / 4.0);
                int tuneNote = -1;
                juce::AudioBuffer<float> audio (2, blockSize);
                juce::MidiBuffer midi;
                midi.ensureSize (1 << 16);
                const int numBlocks = (int) (seconds * rate / blockSize);
                double total = 0.0, worst = 0.0;
                std::vector<double> times;
                times.reserve ((size_t) numBlocks * instances);
                for (int n = 0; n < numBlocks; ++n)
                {
                    juce::MidiBuffer tune;
                    if (scenario == 3)
                        for (int64_t s = ((int64_t) n * blockSize + sixteenth - 1) / sixteenth * sixteenth; s < (int64_t) (n + 1) * blockSize; s += sixteenth)
                        {
                            const int at = (int) (s - (int64_t) n * blockSize);
                            if (tuneNote >= 0)
                                tune.addEvent (juce::MidiMessage::noteOff (1, tuneNote), at);
                            const int step = (int) ((s / sixteenth) % 14);
                            tuneNote = 60 + scaleOffset (Scale::NaturalMinor, step < 7 ? step : 14 - step);
                            tune.addEvent (juce::MidiMessage::noteOn (1, tuneNote, (juce::uint8) 100), at);
                        }
                    for (auto& p : procs)
                    {
                        midi.clear();
                        midi.addEvents (tune, 0, -1, 0);
                        const auto t0 = std::chrono::steady_clock::now();
                        p->processBlock (audio, midi);
                        const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                        total += dt;
                        worst = std::max (worst, dt);
                        times.push_back (dt);
                    }
                    head.advance (blockSize, rate);
                }
                const double audioSeconds = numBlocks * blockSize / rate;
                const double mean = 100.0 * total / instances / audioSeconds;
                const double peak = 100.0 * worst / (blockSize / rate);
                std::nth_element (times.begin(), times.begin() + (long) (times.size() * 999 / 1000), times.end());
                const double p999 = 100.0 * times[times.size() * 999 / 1000] / (blockSize / rate);
                worstMean = std::max (worstMean, mean);
                std::printf ("   %-15s %6d   %10.4f%%            %8.3f%% / %.3f%%\n", names[scenario], blockSize, mean, p999, peak);
                for (auto& p : procs)
                    p->setPlayHead (nullptr);
            }
        check (worstMean < 1.0, "every scenario and block size: under 1% of one core per instance (worst mean "
                                    + juce::String (worstMean, 4) + "%)");
        return failures == 0 ? 0 : 1;
    }
}

namespace
{
    double threadCpuSeconds()
    {
        timespec t {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
        return (double) t.tv_sec + (double) t.tv_nsec * 1.0e-9;
    }

    // The editor's cost: one instance with everything on (or, `idle`, nothing playing and the
    // transport stopped) and its editor in a window, the audio
    // on its own thread in real time (512-sample blocks at 44.1 kHz), the message thread running
    // the editor's 30 Hz refresh and the painting for `seconds`. Prints each thread's CPU as a
    // share of one core.
    int runUiBench (double seconds, bool idle)
    {
        auto p = std::make_unique<ChordMonkeyProcessor>();
        LoopPlayHead head;
        head.loopEnd = 8.0;
        head.playing = ! idle;
        p->setPlayHead (&head);
        p->prepareToPlay (44100.0, 512);
        if (! idle)
            everythingOn (*p);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());
        editor->setTopLeftPosition (20, 60);
        editor->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        editor->setVisible (true);

        std::atomic<bool> stop { false };
        double audioCpu = 0.0;
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);
            const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration> (std::chrono::duration<double> (512.0 / 44100.0));
            auto next = std::chrono::steady_clock::now();
            const double c0 = threadCpuSeconds();
            while (! stop.load())
            {
                midi.clear();
                p->processBlock (buffer, midi);
                head.advance (512, 44100.0);
                next += period;
                std::this_thread::sleep_until (next);
            }
            audioCpu = threadCpuSeconds() - c0;
        });
        const double m0 = threadCpuSeconds();
        const auto t0 = std::chrono::steady_clock::now();
        juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000.0));
        const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double ui = threadCpuSeconds() - m0;
        stop = true;
        audio.join();
        editor.reset();
        p->setPlayHead (nullptr);
        std::printf ("== Phase 8: one editor open, %s (%.0f s) ==\n   message thread (refresh + paint): %.2f%% of a core\n"
                     "   audio thread: %.3f%% of a core\n", idle ? "transport stopped, nothing playing" : "everything playing",
                     wall, 100.0 * ui / wall, 100.0 * audioCpu / wall);
        return 0;
    }
}

namespace
{
    // The workspace in the plug-in's state (PLAN.md §3.2, Phase 1): it round-trips, it isn't
    // in the parameters' tree, an older state restores an empty one, a damaged pattern doesn't
    // spoil the rest, and an edit tells the host the state changed.
    void testWorkspace()
    {
        std::cout << "== workspace in the state ==\n";
        auto p = std::make_unique<ChordMonkeyProcessor>();
        check (p->getWorkspace() == Workspace {}, "a new instance has an empty workspace");

        Workspace w;
        std::string error;
        parseProgression ("I V vi IV", 0, w.patterns[0], error);
        parseProgression ("ii7:2 V7:2 Imaj7:8 bVII(0.7.16)", 9, w.patterns[2], error);
        w.patterns[0].slots[1].inversion = 1;
        w.patterns[2].slots[3].octave = -1;

        StateWatch watch;
        p->addListener (&watch);
        p->setWorkspace (w);
        const bool told = watch.changed;
        watch.changed = false;
        p->setWorkspace (w);
        check (told && ! watch.changed, "an edit tells the host the state changed; setting the same workspace doesn't");
        p->removeListener (&watch);

        set (*p, params::key, 9.0f);
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto restored = std::make_unique<ChordMonkeyProcessor>();
        restored->setStateInformation (saved.getData(), (int) saved.getSize());
        check (restored->getWorkspace() == w && (int) get (*restored, params::key) == 9, "the workspace and the parameters restore into a new instance");
        check (! restored->state.state.getChildWithName ("WORKSPACE").isValid(), "the workspace stays out of the parameters' tree");

        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        const auto ws = xml != nullptr ? xml->getChildByName ("WORKSPACE") : nullptr;
        check (ws != nullptr && ws->getNumChildElements() == numPatterns
                   && ws->getChildElement (0)->getStringAttribute ("text") == "0|0:0.4.7:8:0:0 7:0.4.7:8:1:0 9:0.3.7:8:0:0 5:0.4.7:8:0:0",
               "saved as one PATTERN per pattern with its text");

        // A state from before Phase 1: no WORKSPACE.
        if (xml != nullptr)
        {
            auto old = std::make_unique<juce::XmlElement> (*xml);
            old->removeChildElement (old->getChildByName ("WORKSPACE"), true);
            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*old, oldState);
            auto q = std::make_unique<ChordMonkeyProcessor>();
            q->setWorkspace (w);
            q->setStateInformation (oldState.getData(), (int) oldState.getSize());
            check (q->getWorkspace() == Workspace {} && (int) get (*q, params::key) == 9, "an older state (no workspace) restores an empty one, and its parameters");

            // One pattern damaged.
            auto damaged = std::make_unique<juce::XmlElement> (*xml);
            damaged->getChildByName ("WORKSPACE")->getChildElement (0)->setAttribute ("text", "garbage");
            juce::MemoryBlock damagedState;
            juce::AudioProcessor::copyXmlToBinary (*damaged, damagedState);
            q->setStateInformation (damagedState.getData(), (int) damagedState.getSize());
            const auto got = q->getWorkspace();
            check (got.patterns[0] == Pattern {} && got.patterns[2] == w.patterns[2], "a damaged pattern restores empty; the others are kept");
        }

        std::cout << "== workspace: test rig and playback (Phase 2) ==\n";
        {
            const auto file = ChordMonkeyProcessor::testWorkspaceFile();
            const bool hadFile = file.existsAsFile();
            const auto previous = hadFile ? file.loadFileAsString() : juce::String();
            file.getParentDirectory().createDirectory();
            file.replaceWithText ("# the bridge's patterns\nI V vi IV\n\nii7:2 V7:2 oops\nvi:8\n");

            auto q = std::make_unique<ChordMonkeyProcessor>();
            set (*q, params::key, 2.0f);
            runTimers (150);
            const bool ignoredWhileOff = q->getWorkspace() == Workspace {};
            set (*q, params::testWorkspace, 1.0f);
            runTimers (150);
            Workspace want;
            std::string error;
            parseProgression ("I V vi IV", 2, want.patterns[0], error);
            parseProgression ("vi:8", 2, want.patterns[3], error);
            check (ignoredWhileOff && q->getWorkspace() == want,
                   "Workspace.txt loads only while Test workspace is on: in D, a comment skipped, a blank and a bad line empty");
            check (! params::isPresetParameter (params::testWorkspace) && ! params::isPresetParameter (params::play)
                       && params::isPresetParameter (params::pattern),
                   "Play and Test workspace aren't preset parameters; Pattern is (Phase 5: presets hold the workspace)");

            if (hadFile)
                file.replaceWithText (previous);
            else
                file.deleteFile();

            // Playback: the message thread sets the workspace; the next block plays it.
            q->prepareToPlay (48000.0, 512);
            LoopPlayHead head;
            head.looping = false;
            head.bpm = 120.0;
            q->setPlayHead (&head);
            set (*q, params::testWorkspace, 0.0f);
            Workspace w;
            parseProgression ("I:2 V:2", 0, w.patterns[1], error);
            q->setWorkspace (w);
            set (*q, params::pattern, 1.0f);
            set (*q, params::play, 1.0f);
            std::vector<int> ons;
            const auto before = allocCount.load();
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);    // a host that reserves enough for any block
            for (int b = 0; b < 200; ++b)   // 200 blocks of 512 at 48 kHz, 120 BPM: 4.27 beats
            {
                midi.clear();
                {
                    AllocWatch watch;
                    q->processBlock (buffer, midi);
                }
                head.advance (512, 48000.0);
                for (const auto m : midi)
                    if (m.getMessage().isNoteOn())
                        ons.push_back (m.getMessage().getNoteNumber());
            }
            q->setPlayHead (nullptr);
            check (ons == std::vector<int> { 48, 52, 55, 55, 59, 62, 48, 52, 55 } && q->playingPattern.load() == 1 && q->playingSlot.load() == 0,
                   "Play with Pattern B plays its C and G, two beats each, and C again (" + juce::String ((int) ons.size()) + " note-ons)");
            check (allocCount.load() == before, "no allocation in processBlock while the pattern plays");
        }

        std::cout << "== Bind (Phase 3) ==\n";
        {
            auto q = std::make_unique<ChordMonkeyProcessor>();
            q->prepareToPlay (48000.0, 512);
            Workspace w;
            std::string error;
            parseProgression ("I V", 0, w.patterns[0], error);
            parseProgression ("vi", 0, w.patterns[1], error);
            q->setWorkspace (w);
            set (*q, params::bindSource, (float) BindSource::Workspace);
            juce::AudioBuffer<float> buffer (2, 512);
            auto play = [&] (juce::MidiBuffer midi)
            {
                q->processBlock (buffer, midi);
                std::vector<int> ons;
                for (const auto m : midi)
                    if (m.getMessage().isNoteOn())
                        ons.push_back (m.getMessage().getNoteNumber());
                std::sort (ons.begin(), ons.end());
                return ons;
            };
            juce::MidiBuffer dSharp;
            dSharp.addEvent (juce::MidiMessage::noteOn (1, 39, (juce::uint8) 100), 0);
            play (dSharp);
            runTimers (150);
            juce::MidiBuffer c1;
            c1.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            const auto vi = play (c1);
            check ((int) get (*q, params::pattern) == 1 && vi == std::vector<int> { 57, 60, 64 },
                   "Bind Workspace: D#1 chooses pattern B and the Pattern parameter follows; C1 then plays its vi");

            set (*q, params::bindSource, (float) BindSource::Off);
            juce::MidiBuffer allOff;
            allOff.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            play (allOff);
            q->pushUiNote (50, 100, true, true);
            const auto pad = play ({});
            q->pushUiNote (50, 0, false, true);
            juce::MidiBuffer d1;
            d1.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 0);
            const auto key = play (d1);
            check (pad == std::vector<int> { 50, 53, 57 } && key == std::vector<int> { 38 },
                   "Bind Off: a chord pad still plays its chord (ii); a key plays its note (Keys-Lock Off)");
            check (! params::isPresetParameter (params::bindSource) && ! params::isPresetParameter (params::bindStart)
                       && ! params::isPresetParameter (params::keysLock),
                   "Bind, Bind start and Keys-Lock aren't preset parameters");
            const auto* start = dynamic_cast<juce::AudioParameterInt*> (q->state.getParameter (params::bindStart));
            check (start != nullptr && start->getCurrentValueAsText() == "C1" && start->getRange().getEnd() == 115,
                   "Bind start shows note names (C1) and reaches 115");
        }
        {
            // The test rig's Keys.txt: keys at song positions while the host plays.
            const auto keysFile = ChordMonkeyProcessor::testKeysFile();
            const bool hadKeys = keysFile.existsAsFile();
            const auto previousKeys = hadKeys ? keysFile.loadFileAsString() : juce::String();
            keysFile.replaceWithText ("# beat note length\n0.5 36 0.5\n1 bad\n1.5 38 0.25\n");
            auto q = std::make_unique<ChordMonkeyProcessor>();
            q->prepareToPlay (48000.0, 512);
            set (*q, params::testWorkspace, 1.0f);
            runTimers (150);
            LoopPlayHead head;
            head.looping = false;
            head.bpm = 120.0;
            q->setPlayHead (&head);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);
            std::vector<std::pair<double, int>> ons;
            for (int b = 0; b < 100; ++b)   // 2.1 beats
            {
                midi.clear();
                const double ppq = head.ppq;
                q->processBlock (buffer, midi);
                for (const auto m : midi)
                    if (m.getMessage().isNoteOn())
                        ons.push_back ({ ppq + m.samplePosition / 24000.0, m.getMessage().getNoteNumber() });
                head.advance (512, 48000.0);
            }
            q->setPlayHead (nullptr);
            bool onTime = ons.size() == 6;
            for (const auto& [at, note] : ons)
                onTime = onTime && std::abs (at - (note == 48 || note == 52 || note == 55 ? 0.5 : 1.5)) < 1.0 / 24000.0 + 1.0e-9;
            check (onTime, "Keys.txt (with Test workspace on) plays C and D at beats 0.5 and 1.5, to the sample (" + juce::String ((int) ons.size()) + " note-ons)");
            if (hadKeys)
                keysFile.replaceWithText (previousKeys);
            else
                keysFile.deleteFile();
        }
    }
}

namespace
{
    // Chord sets and presets with the workspace (PLAN.md §3.5, Phase 5).
    void testChordSetsAndPresets()
    {
        std::cout << "== chord sets and presets (Phase 5) ==\n";
        auto p = std::make_unique<ChordMonkeyProcessor>();
        p->prepareToPlay (48000.0, 512);
        const auto& factory = factoryPresets();

        // Every Songwriting preset fills patterns A-D with its chord sets in its Key, Pattern A.
        int good = 0, songwriting = 0;
        juce::String firstBad;
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            if (factory[(size_t) i].category != "Songwriting")
                continue;
            ++songwriting;
            set (*p, params::pattern, 2.0f);
            p->setCurrentProgram (i);
            const auto w = p->getWorkspace();
            const int key = (int) get (*p, params::key);
            bool ok = (int) get (*p, params::pattern) == 0;
            for (size_t k = 0; k < 4; ++k)
            {
                Pattern want;
                std::string error;
                const auto* set = findChordSet (factory[(size_t) i].chordSets[k]);
                ok = ok && set != nullptr && parseProgression (set->progression, key, want, error) && w.patterns[k] == want;
            }
            good += ok ? 1 : 0;
            if (! ok && firstBad.isEmpty())
                firstBad = factory[(size_t) i].name;
        }
        check (good == songwriting && songwriting >= 10, juce::String (good) + " of " + juce::String (songwriting)
                                                             + " Songwriting presets fill A-D with their chord sets in their Key and choose A"
                                                             + (firstBad.isEmpty() ? juce::String() : " (first wrong: " + firstBad + ")"));

        const auto filled = p->getWorkspace();
        p->setCurrentProgram (0);
        const bool initKeeps = p->getWorkspace() == filled;
        p->setCurrentProgram (1);
        check (initKeeps && p->getWorkspace() == filled, "Init and Chordism's presets (no workspace) leave the workspace alone");

        // A user preset holds the workspace.
        const juce::String category ("ZZ ChordMonkey Test");
        const auto folder = PresetManager::userFolder().getChildFile (category);
        p->presets.save ("With Workspace", category);
        const auto file = folder.getChildFile ("With Workspace.xml");
        const auto saved = PresetManager::readFile (file);
        p->setWorkspace (Workspace {});
        p->presets.rescan();
        int index = -1;
        for (int i = 0; i < p->presets.getEntries().size(); ++i)
            if (p->presets.getEntries()[i].file == file)
                index = i;
        p->presets.load (index);
        check (saved.getChildWithName ("WORKSPACE").isValid() && index >= 0 && p->getWorkspace() == filled,
               "a saved preset holds the workspace, and loading it brings it back");

        // A ChordChimp preset file loads.
        const auto chimp = folder.getChildFile ("From ChordChimp.xml");
        chimp.replaceWithText ("<ChordChimp name=\"Old\" category=\"User\"><PARAM id=\"key\" value=\"5\"/><PARAM id=\"chordGroup\" value=\"5\"/></ChordChimp>");
        const auto chimpTree = PresetManager::readFile (chimp);
        if (chimpTree.isValid())
            p->applyPreset (chimpTree, "User", "Old");
        check (chimpTree.isValid() && (int) get (*p, params::key) == 5 && (int) get (*p, params::chordGroup) == 5 && p->getWorkspace() == filled,
               "a ChordChimp preset file loads its settings (F, Sevenths) and leaves the workspace");
        folder.deleteRecursively();

        // Chord sets: load with suggestions, save, read back.
        set (*p, params::key, 2.0f);
        set (*p, params::scale, (float) (int) Scale::Dorian);
        auto& sets = p->chordSets;
        sets.rescan();
        int twoFive = -1;
        for (int i = 0; i < sets.getEntries().size(); ++i)
            if (sets.getEntries()[i].name == "Two-Five-One" && sets.getEntries()[i].factory >= 0)
                twoFive = i;
        const bool loaded = sets.load (twoFive, 2, true);
        const auto w = p->getWorkspace();
        juce::StringArray names;
        for (int i = 0; i < w.patterns[2].size(); ++i)
            names.add (namePatternSlot (w.patterns[2].slots[(size_t) i], 2, Scale::Major).text());
        check (sets.getEntries().size() >= numFactoryChordSets && loaded && names.joinIntoString (" ") == "Em7 A7 Dmaj7"
                   && (int) get (*p, params::scale) == (int) Scale::Major && (int) get (*p, params::style) == 7,
               "Two-Five-One into C in D: Em7 A7 Dmaj7, and its suggestions set Scale (Major) and Style (Charleston): " + names.joinIntoString (" "));
        const auto before = p->getWorkspace();
        check (! p->loadChordSet ("I V nonsense", 0) && ! sets.load (-1, 0, false) && p->getWorkspace() == before,
               "a progression that doesn't read, or no such set, changes nothing");

        // Every factory set loads through the manager, its text intact (UTF-8: "iiø7", "ii°").
        {
            const auto keep = p->getWorkspace();
            juce::StringArray failed;
            int factorySets = 0;
            for (int i = 0; i < sets.getEntries().size(); ++i)
            {
                const auto& entry = sets.getEntries()[i];
                if (entry.factory < 0)
                    continue;
                ++factorySets;
                const auto& lib = factoryChordSets()[(size_t) entry.factory];
                if (entry.progression.toStdString() != lib.progression || entry.name.toStdString() != lib.name || ! sets.load (i, 0, false)
                    || p->getWorkspace().patterns[0].empty())
                    failed.add (entry.name + " (" + entry.progression + ")");
            }
            check (factorySets == numFactoryChordSets && failed.isEmpty(),
                   "all " + juce::String (factorySets) + " factory chord sets load from the menu's list, text intact"
                       + (failed.isEmpty() ? juce::String() : ": failed " + failed.joinIntoString (", ")));
            p->setWorkspace (keep);
        }

        const auto setFolder = ChordSetManager::userFolder().getChildFile (category);
        auto cleared = p->getWorkspace();
        cleared.patterns[3] = Pattern {};
        p->setWorkspace (cleared);
        const auto savedSet = sets.save ("My Two-Five", 2, category);
        ChordSetManager::Entry e;
        const bool read = ChordSetManager::readFile (setFolder.getChildFile ("My Two-Five.json"), e);
        int listed = 0;
        for (const auto& entry : sets.getEntries())
            listed += entry.name == "My Two-Five" && entry.category == category ? 1 : 0;
        check (savedSet.wasOk() && read && e.progression == "ii7 V7 Imaj7:8" && e.scale == "Major" && e.style == "Charleston" && listed == 1
                   && ! sets.save ("Empty", 3, category).wasOk(),
               "a pattern saves as a chord set (numerals in its key, the Scale and Style) and is listed; an empty pattern doesn't save");
        setFolder.deleteRecursively();

        // Pop Writer plays: Four Chords in Eighths with the bass.
        auto q = std::make_unique<ChordMonkeyProcessor>();
        q->prepareToPlay (48000.0, 512);
        int popWriter = -1;
        for (int i = 0; i < (int) factory.size(); ++i)
            if (factory[(size_t) i].name == "Pop Writer")
                popWriter = i;
        q->setCurrentProgram (popWriter);
        set (*q, params::play, 1.0f);
        LoopPlayHead head;
        head.looping = false;
        head.bpm = 120.0;
        q->setPlayHead (&head);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::vector<int> first;
        q->processBlock (buffer, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                first.push_back (m.getMessage().getNoteNumber());
        std::sort (first.begin(), first.end());
        q->setPlayHead (nullptr);
        check (first == std::vector<int> { 36, 48, 52, 55 }, "Pop Writer with Play: its first strike is C with the bass C1 (Four Chords, Bass Root)");
    }
}

namespace
{
    // The workspace panel (PLAN.md §5, Phase 6): its actions, a drop, the preview, the drag-out.
    void testDetect()
    {
        std::cout << "== Detect (Phase 8) ==\n";
        auto p = std::make_unique<ChordMonkeyProcessor>();
        p->prepareToPlay (48000.0, 512);
        auto editor = std::unique_ptr<ChordMonkeyEditor> (dynamic_cast<ChordMonkeyEditor*> (p->createEditor()));
        auto& panel = editor->getWorkspacePanel();
        LoopPlayHead head;
        head.looping = false;
        head.bpm = 120.0;
        p->setPlayHead (&head);

        // Plays `chords` (4 beats each, held 3.5) from the playhead on, as host MIDI; returns the
        // note-ons that came out, and counts allocations.
        int allocations = 0;
        auto play = [&] (const std::vector<std::vector<int>>& chords)
        {
            std::vector<int> ons;
            const double spb = 48000.0 * 60.0 / head.bpm;
            const double from = head.ppq;
            const int blocks = (int) std::ceil (chords.size() * 4.0 * spb / 512.0) + 4;
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);
            for (int b = 0; b < blocks; ++b)
            {
                midi.clear();
                const double start = head.ppq, end = head.ppq + 512 / spb;
                for (size_t c = 0; c < chords.size(); ++c)
                    for (int note : chords[c])
                    {
                        const double on = from + (double) c * 4.0, off = on + 3.5;
                        if (on >= start && on < end)
                            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 90), (int) ((on - start) * spb));
                        if (off >= start && off < end)
                            midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) ((off - start) * spb));
                    }
                const auto before = allocCount.load();
                {
                    AllocWatch watch;
                    p->processBlock (audio, midi);
                }
                allocations += (int) (allocCount.load() - before);
                head.advance (512, 48000.0);
                for (const auto m : midi)
                    if (m.getMessage().isNoteOn())
                        ons.push_back (m.getMessage().getNoteNumber());
            }
            return ons;
        };
        auto names = [&]
        {
            const auto w = p->getWorkspace();
            const auto s = p->currentSettings();
            juce::StringArray out;
            for (int i = 0; i < w.patterns[0].size(); ++i)
                out.add (juce::String (namePatternSlot (w.patterns[0].slots[(size_t) i], s.key, s.scale).text()) + ":"
                         + juce::String (w.patterns[0].slots[(size_t) i].halfBeats / 2));
            return out.joinIntoString (" ");
        };
        auto buttonText = [&]
        {
            editor->refreshNow();
            return panel.detectText();
        };

        // From the editor: Detect, play, the button counts the chords, a click writes them.
        editor->refreshNow();
        const auto idle = buttonText();
        panel.detectClicked();
        const bool capturing = p->detectMode() == params::DetectMode::Capture;
        const auto listening = buttonText();
        const std::vector<std::vector<int>> amFCG { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
        std::vector<std::vector<int>> twice (amFCG);
        twice.insert (twice.end(), amFCG.begin(), amFCG.end());
        const auto ons = play (twice);
        std::vector<int> played;
        for (const auto& c : twice)
            played.insert (played.end(), c.begin(), c.end());
        const auto counted = buttonText();
        check (idle == "Detect" && capturing && listening == juce::String::fromUTF8 ("Listening\xe2\x80\xa6") && ons == played && allocations == 0
                   && counted == "Use 8 chords",
               "Detect captures: the button reads " + idle + ", " + listening + ", then " + counted + "; the "
                   + juce::String ((int) ons.size()) + " notes played pass through, nothing else; no allocation");
        panel.detectClicked();
        editor->refreshNow();
        const auto s = p->currentSettings();
        const KeyGuess g = p->readKeyGuess();
        check (p->detectMode() == params::DetectMode::Off && g.has() && s.key == g.tonic
                   && names() == "Am:4 F:4 C:4 G:4 Am:4 F:4 C:4 G:4" && buttonText() == "Detect",
               "the click writes them into pattern A (" + names() + ") in the key heard (" + juce::String (guessKeyName (g.tonic, g.minor))
                   + (g.minor ? " minor" : " major") + ") and Detect is off");

        // From the host's thread: Capture, play, Use result (seen by the timer).
        fromAnotherThread (*p, params::detect, (float) params::DetectMode::Capture);
        runTimers();
        play ({ { 50, 53, 57, 60 }, { 43, 47, 50, 53 } });
        fromAnotherThread (*p, params::detect, (float) params::DetectMode::UseResult);
        runTimers();
        check (p->detectMode() == params::DetectMode::Off && names() == "Dm7:4 G7:4",
               "from the host: Capture, then Use result writes the new capture (" + names() + ")");

        // Use result with nothing heard keeps capturing; the cross leaves the pattern as it was.
        const auto before = p->getWorkspace();
        fromAnotherThread (*p, params::detect, (float) params::DetectMode::Capture);
        runTimers();
        play ({});
        fromAnotherThread (*p, params::detect, (float) params::DetectMode::UseResult);
        runTimers();
        const bool kept = p->detectMode() == params::DetectMode::Capture;
        editor->refreshNow();
        panel.cancelDetect();
        check (kept && p->detectMode() == params::DetectMode::Off && p->getWorkspace() == before,
               "Use result with nothing heard keeps capturing; the cross stops and the pattern stays");

        // Not saved, not in presets.
        set (*p, params::detect, (float) params::DetectMode::Capture);
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto restored = std::make_unique<ChordMonkeyProcessor>();
        restored->setStateInformation (state.getData(), (int) state.getSize());
        check (restored->detectMode() == params::DetectMode::Off && ! params::isPresetParameter (params::detect)
                   && ! p->presetState().getChildWithProperty ("id", params::detect).isValid(),
               "Detect isn't saved with the state or held in presets");
        set (*p, params::detect, 0.0f);
        p->setPlayHead (nullptr);
        editor.reset();
    }

    void testPhase9()
    {
        std::cout << "== substitutes and Chord extensions (Phase 9) ==\n";
        auto p = std::make_unique<ChordMonkeyProcessor>();
        p->prepareToPlay (48000.0, 512);
        auto editor = std::unique_ptr<ChordMonkeyEditor> (dynamic_cast<ChordMonkeyEditor*> (p->createEditor()));
        auto& panel = editor->getWorkspacePanel();
        Workspace w;
        std::string error;
        parseProgression ("I:2 V7", 0, w.patterns[0], error);
        w.patterns[0].slots[0].inversion = 2;
        w.patterns[0].slots[0].octave = 1;
        p->setWorkspace (w);
        editor->refreshNow();
        const auto subsOfC = substitutesFor (w.patterns[0].slots[0].chord, 0, Scale::Major);
        const auto subsOfG7 = substitutesFor (w.patterns[0].slots[1].chord, 0, Scale::Major);
        panel.setSlotChord (0, subsOfC[0].chord);         // Am
        panel.setSlotChord (1, subsOfG7.back().chord);    // Db7
        const auto after = p->getWorkspace().patterns[0];
        const auto name = [&] (int i) { return namePatternSlot (after.slots[(size_t) i], 0, Scale::Major).text(); };
        check (subsOfC.size() == 7 && name (0) == "Am/E" && after.slots[0].halfBeats == 4 && after.slots[0].octave == 1 && after.slots[0].inversion == 2
                   && name (1) == "Db7",
               "a substitute replaces the slot's chord, keeping its length, octave and inversion: C/G -> " + juce::String (name (0))
                   + ", G7 -> " + juce::String (name (1)));

        // Chord extensions: the sixth Keys-Lock choice; with Bind Palette and Dm held, C3 plays B2.
        auto* lock = dynamic_cast<juce::AudioParameterChoice*> (p->state.getParameter (params::keysLock));
        set (*p, params::bindSource, (float) BindSource::Palette);
        set (*p, params::keysLock, (float) KeysLockMode::ChordExtensions);
        block (*p, { juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100) });
        const auto out = block (*p, { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
        check (lock != nullptr && lock->choices.size() == numKeysLockModes && lock->choices[5] == "Chord extensions"
                   && noteOns (out) == std::vector<int> { 59 },
               "Keys-Lock's sixth choice is Chord extensions; with Dm held, C3 plays its 13th, B2 (" + juce::String ((int) noteOns (out).size()) + " note)");
        block (*p, { juce::MidiMessage::noteOff (1, 60), juce::MidiMessage::noteOff (1, 38) });
        editor.reset();
    }

    void testWorkspacePanel()
    {
        std::cout << "== workspace panel (Phase 6) ==\n";
        auto p = std::make_unique<ChordMonkeyProcessor>();
        p->prepareToPlay (48000.0, 512);
        auto editor = std::unique_ptr<ChordMonkeyEditor> (dynamic_cast<ChordMonkeyEditor*> (p->createEditor()));
        auto& panel = editor->getWorkspacePanel();
        auto names = [&] (int pattern = 0)
        {
            editor->refreshNow();
            const auto w = p->getWorkspace();
            const auto s = p->currentSettings();
            juce::StringArray out;
            for (int i = 0; i < w.patterns[(size_t) pattern].size(); ++i)
                out.add (namePatternSlot (w.patterns[(size_t) pattern].slots[(size_t) i], s.key, s.scale).text());
            return out.joinIntoString (" ");
        };
        editor->refreshNow();

        panel.addPaletteChord (0);
        panel.addPaletteChord (4);
        panel.addPaletteChord (5, 0);
        check (names() == "Am G", "pads C and G added, then A's chord dropped on the first slot: Am G (" + names() + ")");

        // A drop from the pads, as the drag-and-drop container delivers it.
        const auto slot1 = panel.slotBounds (1).getCentre().toInt();
        panel.itemDropped ({ "palette:3", &editor->getChordPads(), slot1 });
        const auto empty = panel.slotBounds (5).getCentre().toInt();
        panel.itemDropped ({ "palette:1", &editor->getChordPads(), empty });
        check (names() == "Am F Dm", "dropping F's pad on slot 2 replaces it; Dm past the end appends (" + names() + ")");
        panel.itemDropped ({ "slot:2", &panel, panel.slotBounds (0).getCentre().toInt() });
        check (names() == "Dm Am F", "a slot dragged to the first place moves it (" + names() + ")");

        panel.setSlotLength (0, 4);
        panel.setSlotInversion (1, 1);
        panel.setSlotOctave (2, 1);
        panel.duplicateSlot (2);
        panel.deleteSlot (0);
        const auto w = p->getWorkspace();
        check (names() == "Am/C F F" && w.patterns[0].slots[0].halfBeats == 8 && w.patterns[0].slots[1].octave == 1 && w.patterns[0].slots[2].octave == 1,
               "length, inversion (Am/C), octave, duplicate and delete (" + names() + ")");

        set (*p, params::key, 2.0f);
        panel.transposeToKey();
        panel.copyPatternTo (2);
        check (names() == "Bm/D G G" && names (2) == names() && p->getWorkspace().patterns[0].key == 2,
               "Transpose to D moves it up a tone (Bm/D G G); Copy to C copies it");
        panel.clearPattern();
        check (p->getWorkspace().patterns[0].empty() && p->getWorkspace().patterns[0].key == 2 && ! p->getWorkspace().patterns[2].empty(),
               "Clear empties A only");

        int fourChords = -1;
        p->chordSets.rescan();
        for (int i = 0; i < p->chordSets.getEntries().size(); ++i)
            if (p->chordSets.getEntries()[i].name == "Four Chords" && p->chordSets.getEntries()[i].factory >= 0)
                fourChords = i;
        panel.setUsesSuggestions (false);
        set (*p, params::style, 0.0f);
        panel.loadChordSet (fourChords);
        check (names() == "D A Bm G" && (int) get (*p, params::style) == 0, "Chord sets: Four Chords into A in D, its style not taken (" + names() + ")");
        panel.setUsesSuggestions (true);
        panel.loadChordSet (fourChords);
        check ((int) get (*p, params::style) == 2, "with the preference on, the set's Style (Eighths) is taken too");
        const auto file = ChordSetManager::userFolder().getChildFile ("User").getChildFile ("ZZ Snapshot Set.json");
        const bool existed = file.existsAsFile();
        const auto saved = panel.saveChordSet ("ZZ Snapshot Set");
        ChordSetManager::Entry e;
        check (saved.wasOk() && ChordSetManager::readFile (file, e) && e.progression == "I V vi IV", "Save as chord set writes the pattern's numerals");
        if (! existed)
            file.deleteFile();

        set (*p, params::pattern, 1.0f);
        editor->refreshNow();
        check (panel.currentPattern() == 1 && names (1).isEmpty(), "the tabs follow the Pattern parameter (B, empty)");
        set (*p, params::pattern, 0.0f);
        editor->refreshNow();

        // The preview: slot 2 (Bm) sounds while held.
        p->pushSlotPreview (2, true);
        std::vector<int> ons;
        for (const auto meta : block (*p, {}))
            if (meta.getMessage().isNoteOn())
                ons.push_back (meta.getMessage().getNoteNumber());
        std::sort (ons.begin(), ons.end());
        p->pushSlotPreview (2, false);
        int offs = 0;
        for (const auto meta : block (*p, {}))
            offs += meta.getMessage().isNoteOff() ? 1 : 0;
        check (ons == std::vector<int> { 59, 62, 66 } && offs == 3, "clicking a slot plays its chord (Bm: 59 62 66) until released");

        // The drag-out file is the render.
        juce::String error;
        const auto mid = p->writePatternExport (0, false, error);
        juce::MemoryBlock data;
        MidiSong song;
        std::string why;
        const bool read = mid.loadFileAsData (data) && readMidiFile ((const uint8_t*) data.getData(), data.getSize(), song, why);
        const auto want = renderPattern (p->getWorkspace(), 0, p->currentSettings(), 120.0);
        bool same = read && song.notes.size() == want.notes.size();
        for (size_t i = 0; same && i < want.notes.size(); ++i)
            same = song.notes[i].start == want.notes[i].start && song.notes[i].end == want.notes[i].end && song.notes[i].pitch == want.notes[i].pitch;
        const auto chordsOnly = p->writePatternExport (0, true, error);
        juce::MemoryBlock data2;
        MidiSong song2;
        const bool read2 = chordsOnly.loadFileAsData (data2) && readMidiFile ((const uint8_t*) data2.getData(), data2.getSize(), song2, why);
        juce::String emptyError;
        const auto none = p->writePatternExport (3, false, emptyError);
        check (same && song.notes.size() == 4 * 8 * 3 && read2 && song2.notes.size() == 12 && mid.getFileName() == "ChordMonkey A D 4 bars.mid"
                   && ! none.exists() && emptyError.isNotEmpty(),
               "Drag MIDI writes the render (" + juce::String ((int) song.notes.size()) + " notes in Eighths; chords only: "
                   + juce::String ((int) song2.notes.size()) + ") as " + mid.getFileName() + "; an empty pattern gives none");
        mid.deleteFile();
        chordsOnly.deleteFile();
        editor.reset();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc >= 2 && juce::String (argv[1]) == "--bench")
        return runBench (argc >= 3 ? juce::String (argv[2]).getDoubleValue() : 20.0);
    if (argc >= 2 && juce::String (argv[1]) == "--bench-ui")
        return runUiBench (argc >= 3 ? juce::String (argv[2]).getDoubleValue() : 15.0, argc >= 4 && juce::String (argv[3]) == "idle");
    if (argc < 3)
    {
        std::cerr << "usage: ChordMonkeySnapshot <fixtures dir> <output dir> [--update | --no-pixels]\n"
                     "       ChordMonkeySnapshot --bench [seconds]\n";
        return 2;
    }
    const juce::File fixtures (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const bool update = argc > 3 && juce::String (argv[3]) == "--update";
    comparePixels = ! (argc > 3 && juce::String (argv[3]) == "--no-pixels");
    fixtures.createDirectory();
    out.createDirectory();

    std::cout << "== snapshots ==\n";
    auto processor = std::make_unique<ChordMonkeyProcessor>();
    arrange (*processor);
    {
        std::unique_ptr<juce::AudioProcessorEditor> base (processor->createEditorIfNeeded());
        auto* editor = dynamic_cast<ChordMonkeyEditor*> (base.get());
        check (editor != nullptr, "the editor opens");
        if (editor == nullptr)
            return 1;
        editor->setSize (ChordMonkeyEditor::defaultWidth, ChordMonkeyEditor::defaultHeight);
        editor->showKeyboard (true);
        snapshot (*editor, "editor", fixtures, out, update);
        editor->setSize (ChordMonkeyEditor::minWidth, ChordMonkeyEditor::minHeight);   // the smallest size, keyboard hidden
        editor->showKeyboard (false);
        snapshot (*editor, "editor-small", fixtures, out, update);
        editor->showKeyboard (true);
        editor->setSize (1200, 800);
        snapshot (*editor, "editor-large", fixtures, out, update);
        editor->setSize (ChordMonkeyEditor::defaultWidth, ChordMonkeyEditor::defaultHeight);

        std::cout << "== chord pads ==\n";
        {
            // Release the arranged keys, then press pad G (Pop puts degree 2 there: Dm7 in Sevenths).
            block (*processor, { juce::MidiMessage::noteOff (1, 52), juce::MidiMessage::noteOff (1, 74), juce::MidiMessage::noteOff (1, 63) });
            set (*processor, params::arp, 0.0f);
            set (*processor, params::seq, 0.0f);
            block (*processor, {});
            auto& pads = editor->getChordPads();
            const auto at = pads.padBounds (4).getCentre();
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (source, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pads, &pads, now, at, now, 1, false);
            pads.mouseDown (e);
            const auto on = noteOns (block (*processor, {}));
            pads.mouseUp (e);
            const auto off = block (*processor, {});
            check (pads.slotAt (at) == 4 && on == std::vector<int> { 50, 53, 57, 60 } && noteOffs (off) == 4,
                   "clicking pad G plays its chord (Pop: ii, Dm7) and letting go ends it");

            // The black keys' row (Phase 7): pad D# plays bIII, Ebmaj7, whatever the Progression.
            const auto black = pads.padBounds (numWhiteSlots + 1);
            const auto atBlack = black.getCentre();
            const juce::MouseEvent eb (source, atBlack, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pads, &pads, now, atBlack, now, 1, false);
            pads.mouseDown (eb);
            const auto onBlack = noteOns (block (*processor, {}));
            pads.mouseUp (eb);
            const auto offBlack = block (*processor, {});
            check (pads.slotAt (atBlack) == numWhiteSlots + 1 && black.getBottom() < pads.padBounds (1).getY()
                       && black.getX() < pads.padBounds (1).getRight() && black.getRight() > pads.padBounds (2).getX()
                       && onBlack == std::vector<int> { 51, 55, 58, 62 } && noteOffs (offBlack) == 4,
                   "pad D#, above the gap between D and E, plays bIII (Ebmaj7) and letting go ends it");
            set (*processor, params::arp, (float) (int) ArpPattern::ConvergeUp);
            set (*processor, params::seq, 1.0f);
        }

        std::cout << "== preferences ==\n";
        {
            // The editor's preferences aren't parameters: hiding the keyboard tells the host the
            // plug-in's state changed (so Live saves it).
            StateWatch watch;
            processor->addListener (&watch);
            editor->showKeyboard (false);
            processor->removeListener (&watch);
            check (watch.changed, "hiding the keyboard tells the host the plug-in's state changed");
            editor->showKeyboard (true);
        }

        std::cout << "== every parameter reachable ==\n";
        for (int pattern = 0; pattern < numSeqPatterns; ++pattern)
        {
            set (*processor, params::seqPattern, (float) pattern);
            editor->refreshNow();
        }
        juce::StringArray missing;
        int total = 0;
        for (auto* param : processor->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            {
                ++total;
                if (editor->boundParameters().count (withId->paramID) == 0 && withId->paramID != params::testPattern
                    && withId->paramID != params::testWorkspace)
                    missing.add (withId->paramID);
            }
        check (total == params::numParameters && missing.isEmpty(), juce::String (total) + " parameters, " + juce::String (missing.size()) + " without a control (Test pattern is Live's)"
                                      + (missing.isEmpty() ? juce::String() : ": " + missing.joinIntoString (", ")));
    }

    std::cout << "== state ==\n";
    processor->uiState.setProperty ("keyboard", false, nullptr);
    juce::MemoryBlock saved;
    processor->getStateInformation (saved);
    auto restored = std::make_unique<ChordMonkeyProcessor>();
    StateWatch restoreWatch;
    restored->addListener (&restoreWatch);
    restored->setStateInformation (saved.getData(), (int) saved.getSize());
    for (int i = 0; i < 10; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    restored->removeListener (&restoreWatch);
    check (! restoreWatch.changed, "a restore doesn't report a change (the set isn't marked as edited when it opens)");
    int differing = 0;
    for (auto* param : processor->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            differing += get (*processor, withId->paramID) != get (*restored, withId->paramID) ? 1 : 0;
    check (differing == 0, "every parameter restores (" + juce::String (differing) + " differ)");
    check (! (bool) restored->uiState.getProperty ("keyboard", true), "the editor's preferences restore");

    std::cout << "== editor size ==\n";
    {
        // A new editor opens at the size the last one was left at, also after a save and restore,
        // and opening one doesn't tell the host the state changed (ChordChimp's PLAN.md §11, Editor in Live).
        // Closes an editor as a host does: JUCE wants editorBeingDeleted() before the delete.
        const auto close = [] (juce::AudioProcessor& p, std::unique_ptr<juce::AudioProcessorEditor>& e)
        {
            p.editorBeingDeleted (e.get());
            e.reset();
        };

        auto sized = std::make_unique<ChordMonkeyProcessor>();
        StateWatch watch;
        sized->addListener (&watch);
        std::unique_ptr<juce::AudioProcessorEditor> first (sized->createEditorIfNeeded());
        sized->removeListener (&watch);
        check (first->getWidth() == ChordMonkeyEditor::defaultWidth && first->getHeight() == ChordMonkeyEditor::defaultHeight && ! watch.changed,
               "a new instance's editor opens at the default size (780 x 640) and doesn't report a change");
        first->setSize (919, 689);
        close (*sized, first);

        std::unique_ptr<juce::AudioProcessorEditor> second (sized->createEditorIfNeeded());
        check (second->getWidth() == 919 && second->getHeight() == 689, "a reopened editor has the size the last one was left at (919 x 689)");
        close (*sized, second);

        juce::MemoryBlock state;
        sized->getStateInformation (state);
        auto reopened = std::make_unique<ChordMonkeyProcessor>();
        reopened->setStateInformation (state.getData(), (int) state.getSize());
        StateWatch reopenWatch;
        reopened->addListener (&reopenWatch);
        std::unique_ptr<juce::AudioProcessorEditor> third (reopened->createEditorIfNeeded());
        reopened->removeListener (&reopenWatch);
        check (third->getWidth() == 919 && third->getHeight() == 689 && ! reopenWatch.changed,
               "after a save and restore the editor opens at the saved size and doesn't report a change");
        third->setSize (ChordMonkeyEditor::minWidth, ChordMonkeyEditor::minHeight);
        close (*reopened, third);
        std::unique_ptr<juce::AudioProcessorEditor> fourth (reopened->createEditorIfNeeded());
        check (fourth->getWidth() == ChordMonkeyEditor::minWidth && fourth->getHeight() == ChordMonkeyEditor::minHeight,
               "an editor left at the smallest size reopens at it");
        close (*reopened, fourth);
    }

    std::cout << "== Export ==\n";
    {
        // Phase 6 (ChordChimp's PLAN.md §11): the handle exports the completed bars ChordMonkey played.
        auto p = std::make_unique<ChordMonkeyProcessor>();
        LoopPlayHead head;
        head.playing = false;
        head.looping = false;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        p->prepareToPlay (48000.0, 512);
        set (*p, params::testPattern, 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorIfNeeded());
        auto* editor = dynamic_cast<ChordMonkeyEditor*> (base.get());
        editor->refreshNow();
        juce::String error;
        const bool emptyAtFirst = ! p->writeExport (error).existsAsFile() && editor->getExportHandle().bars() == 0;
        head.playing = true;
        while (head.ppq < 9.0)
        {
            block (*p, {});
            head.advance (512, 48000.0);
        }
        editor->refreshNow();
        const auto file = p->writeExport (error);
        MidiSong song;
        std::string readError;
        juce::MemoryBlock data;
        file.loadFileAsData (data);
        const bool read = readMidiFile ((const uint8_t*) data.getData(), data.getSize(), song, readError);
        check (emptyAtFirst && editor->getExportHandle().bars() == 2 && file.existsAsFile() && read && song.notes.size() == 24
                   && song.ppq == 960 && song.notes.front().start == 0 && song.notes.back().start == 7 * 960
                   && file.getFileName() == "ChordMonkey C Major 2 bars.mid",
               "after 2.25 bars playing, the handle shows 2 bars and exports them (8 chords) as " + file.getFileName());

        // Stopping with the Test pattern on also writes the take for the checks in Live.
        const auto testTake = ChordMonkeyProcessor::testTakeFile();
        const auto before = juce::Time::getCurrentTime() - juce::RelativeTime::seconds (2.0);   // file times are to the second
        head.playing = false;
        block (*p, {});
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (testTake.existsAsFile() && testTake.getLastModificationTime() > before, "stopping with the Test pattern on writes " + testTake.getFullPathName());
        p->editorBeingDeleted (base.get());
        base.reset();
        p->setPlayHead (nullptr);
    }

    testPresets (fixtures);
    testPhase8();
    testAnalyse (fixtures, out, update);
    testWorkspace();
    testChordSetsAndPresets();
    testWorkspacePanel();
    testDetect();
    testPhase9();

    std::cout << (failures == 0 ? "\nsnapshot: all passed\n" : "\n*** snapshot: " + std::to_string (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
