// The editor (ChordChimp's PLAN.md §5): one page, about 780×560 and resizable.
//
//   top bar      the name · On · presets · Export · Keys
//   globals      Key · Heard (Analyse, the key heard) · Scale · Chord Group · Progression ·
//                Octave · Key octave
//   chord pads   the seven white-key chords and the five black-key ones above them (they take
//                the space the rest leaves)
//   effects      Strum, Velocity; Arp, Voicing; Repeat, Step seq
//   bottom       the output keyboard (can be hidden), which also plays
//
// Everything here is a host parameter; the UI keeps only its own preferences (keyboard shown,
// size) in the plug-in state. Refreshed at 30 Hz from the processor's published display and
// note states.
#pragma once

#include "Panels.h"
#include "Theme.h"

namespace chordmonkey
{
    class ChordMonkeyProcessor;

    class ChordMonkeyEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
    {
    public:
        explicit ChordMonkeyEditor (ChordMonkeyProcessor&);
        ~ChordMonkeyEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // For tests (Tests/Snapshot): read the processor now, without waiting for the timer;
        // the parameters some control is (or was) bound to; the keyboard; the pads; Export.
        void refreshNow() { timerCallback(); }
        const std::set<juce::String>& boundParameters() const noexcept { return bindings.boundIds(); }
        void showKeyboard (bool shown);
        ui::ChordPads& getChordPads() noexcept { return chordPads; }
        const ui::ExportHandle& getExportHandle() const noexcept { return topBar.exportHandle; }
        ui::GlobalRow& getGlobals() noexcept { return globals; }

        static constexpr int defaultWidth = 780, defaultHeight = 560;
        static constexpr int minWidth = 720, minHeight = 520, maxWidth = 1400, maxHeight = 960;
        static constexpr int maxPadHeight = 300;   // both rows

    private:
        void timerCallback() override;

        ChordMonkeyProcessor& processor;
        ui::LookAndFeel lookAndFeel;
        ui::Bindings bindings;
        juce::TooltipWindow tooltips { this, 600 };

        ui::TopBar topBar;
        ui::GlobalRow globals;
        ui::ChordPads chordPads;
        ui::EffectsPanel effects;
        ui::OutputKeyboard outputKeys;
        bool sizeRestored = false;      // until then, resized() leaves the saved size alone

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordMonkeyEditor)
    };
}
