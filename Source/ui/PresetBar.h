// The top bar's preset browser and Export handle (ChordChimp's PLAN.md §3.4, §5; Export: §11, Phase 6).
//
//   PresetBar      ◀ preset name ▶ · Save. The name opens the browser: a search box over every
//                  preset (factory, then the user's files by category), click to load, and
//                  "Show folder". Save asks for a name and a category and writes
//                  ~/Music/ChordMonkey/Presets/<Category>/<Name>.xml.
//   ExportHandle   drag it into the host: the last ≤ 32 completed bars of what ChordMonkey sent
//                  since playback started, as a .mid (disabled until the first bar has
//                  finished; its tooltip says why).
#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace chordmonkey
{
    class ChordMonkeyProcessor;
}

namespace chordmonkey::ui
{
    class PresetBar : public juce::Component
    {
    public:
        explicit PresetBar (ChordMonkeyProcessor&);
        ~PresetBar() override;

        void refresh();                   // the shown name, from the processor
        void resized() override;
        void paint (juce::Graphics&) override;

        void openBrowser();
        void askToSave();

    private:
        ChordMonkeyProcessor& processor;
        juce::TextButton previous { juce::String::fromUTF8 ("\xe2\x97\x80") }, next { juce::String::fromUTF8 ("\xe2\x96\xb6") };
        juce::TextButton name, save { "Save" };
        std::unique_ptr<juce::AlertWindow> saveWindow;
        juce::String shown;
    };

    class ExportHandle : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        explicit ExportHandle (ChordMonkeyProcessor&);

        void refresh();                   // the bar count, from the recorder
        int bars() const noexcept { return barCount; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordMonkeyProcessor& processor;
        int barCount = -1;
        bool recording = false, dragging = false;
    };
}
