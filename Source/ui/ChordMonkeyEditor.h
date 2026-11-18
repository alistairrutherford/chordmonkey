// The editor (PLAN.md §5): one page, about 780×784 and resizable.
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
//   Info View    a strip along the bottom, like Live's: point at any control or display to see
//                what it does (as NoteCap's and DrumChimp's; InfoView.h). It replaces tooltips.
//
// Everything here is a host parameter; the UI keeps only its own preferences (keyboard shown,
// size) in the plug-in state. Refreshed at 30 Hz from the processor's published display and
// note states.
#pragma once

#include "InfoView.h"
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

        // The Info View: the key ("arpRate", "ui.export", "card.style"…) of what is at `p` (editor
        // coordinates; empty over nothing described, the strip itself included), and showing one
        // as hovering does (tests call it).
        juce::String infoKeyAt (juce::Point<int> p);
        void showInfoAt (juce::Point<int> p);
        ui::InfoView& infoView() noexcept { return info; }

        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        // Phase 4: 80 px taller (the Style card), Phase 3's Keyboard row had made the pads short;
        // Phase 6: 80 more (the workspace); the Info View 64 more (the strip and its gap).
        static constexpr int defaultWidth = 780, defaultHeight = 784;
        static constexpr int minWidth = 720, minHeight = 744, maxWidth = 1400, maxHeight = 1184;
        static constexpr int maxPadHeight = 300;   // both rows

    private:
        void timerCallback() override;
        juce::Component* infoComponentAt (juce::Point<int> p, juce::String& key);

        ChordMonkeyProcessor& processor;
        ui::LookAndFeel lookAndFeel;
        ui::Bindings bindings;

        ui::TopBar topBar;
        ui::GlobalRow globals;
        ui::KeyboardRow keyboardRow;
        ui::ChordPads chordPads;
        ui::WorkspacePanel workspace;
        ui::EffectsPanel effects;
        ui::OutputKeyboard outputKeys;
        ui::InfoView info;
        bool sizeRestored = false;      // until then, resized() leaves the saved size alone

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordMonkeyEditor)
    };
}
