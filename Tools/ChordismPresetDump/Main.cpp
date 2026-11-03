// Writes Chordism's factory presets (category, name and the values each sets) and the default of
// every Chordism parameter as JSON, so ChordMonkey's preset test can check its presets against
// Chordism's without building Chordism (ChordChimp's PLAN.md §8; Tools/dump_chordism_presets.sh).
//
//   ChordismPresetDump <output.json> [chordism commit]
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/FactoryPresets.h"
#include "plugin/Parameters.h"
#include <iostream>

namespace
{
    // Holds Chordism's parameters, to read their defaults.
    struct Holder : juce::AudioProcessor
    {
        Holder() : state (*this, nullptr, "Chordism", chordism::params::createLayout()) {}

        const juce::String getName() const override { return "Chordism"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        juce::AudioProcessorValueTreeState state;
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::cerr << "usage: ChordismPresetDump <output.json> [chordism commit]\n";
        return 2;
    }

    Holder holder;
    auto* defaults = new juce::DynamicObject();
    for (auto* p : holder.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            defaults->setProperty (r->paramID, r->convertFrom0to1 (r->getDefaultValue()));

    juce::Array<juce::var> presets;
    for (const auto& preset : chordism::factoryPresets())
    {
        auto* values = new juce::DynamicObject();
        for (const auto& [id, value] : preset.values)
            values->setProperty (id, value);
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("category", preset.category);
        entry->setProperty ("name", preset.name);
        entry->setProperty ("values", juce::var (values));
        presets.add (juce::var (entry));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("source", "Chordism's Source/plugin/FactoryPresets.cpp and Parameters.cpp, written by Tools/dump_chordism_presets.sh");
    root->setProperty ("chordism", argc > 2 ? juce::String (argv[2]) : juce::String());
    root->setProperty ("defaults", juce::var (defaults));
    root->setProperty ("presets", presets);

    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    if (! out.replaceWithText (juce::JSON::toString (juce::var (root)) + "\n"))
    {
        std::cerr << "couldn't write " << out.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "wrote " << presets.size() << " presets to " << out.getFullPathName() << "\n";
    return 0;
}
