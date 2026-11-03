// The editor (PLAN.md §5): one page, about 780×720 and resizable.
//
//   top bar      the name · On · presets · Pattern · Play · Export · Keys
//   globals      Key · Heard (Analyse, the key heard) · Scale · Chord Group · Progression ·
//                Octave
//   keyboard     Bind · From · Keys-Lock · Key octave (Phase 3)
//   chord pads   the seven white-key chords and the five black-key ones above them (they take
//                the space the rest leaves): the palette
//   workspace    the pattern's slots, A-D, Play, chord sets, the pattern's MIDI drag (Phase 6)
//   effects      Strum, Velocity; Arp, Voicing; Style; Repeat, Step seq
//   bottom       the output keyboard (can be hidden), which also plays
//
// Everything here is a host parameter; the UI keeps only its own preferences (keyboard shown,
// size) in the plug-in state. Refreshed at 30 Hz from the processor's published display and
// note states.
#pragma once

#include "Panels.h"
#include "Theme.h"
#include "WorkspacePanel.h"

namespace chordmonkey
{
    class ChordMonkeyProcessor;

    class ChordMonkeyEditor : public juce::AudioProcessorEditor,
                             public juce::DragAndDropContainer,
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
        ui::WorkspacePanel& getWorkspacePanel() noexcept { return workspace; }

        // Phase 4: 80 px taller (the Style card), Phase 3's Keyboard row had made the pads short;
        // Phase 6: 80 more (the workspace).
        static constexpr int defaultWidth = 780, defaultHeight = 720;
        static constexpr int minWidth = 720, minHeight = 680, maxWidth = 1400, maxHeight = 1120;
        static constexpr int maxPadHeight = 300;   // both rows

    private:
        void timerCallback() override;

        ChordMonkeyProcessor& processor;
        ui::LookAndFeel lookAndFeel;
        ui::Bindings bindings;
        juce::TooltipWindow tooltips { this, 600 };

        ui::TopBar topBar;
        ui::GlobalRow globals;
        ui::KeyboardRow keyboardRow;
        ui::ChordPads chordPads;
        ui::WorkspacePanel workspace;
        ui::EffectsPanel effects;
        ui::OutputKeyboard outputKeys;
        bool sizeRestored = false;      // until then, resized() leaves the saved size alone

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordMonkeyEditor)
    };
}
