// The workspace panel (PLAN.md §5, Phase 6): the current pattern's eight slots, as a card under
// the chord pads.
//
//   title row   WORKSPACE · Chord sets ▾ (load a factory or user set into the pattern; save the
//               pattern as one) · Pattern ▾ (transpose to the Key, copy to another pattern,
//               clear, save as a chord set) · Drag MIDI (drag the pattern into the host as a clip:
//               one loop, performance included; with Alt, chords only)
//   body        A B C D (the Pattern parameter) · Play · the slots: chord name, numeral in the Key
//               and length; the one sounding lit
//
// Getting chords in: drag a chord pad onto a slot (replaces its chord) or past the last one
// (appends), or right-click a pad. Slots: click to hear one; drag to reorder; right-click for
// length, inversion, octave, duplicate and delete. Every change goes through the processor's
// setWorkspace (message thread); the actions are public so the snapshot test can drive them.
#pragma once

#include "Keyboards.h"
#include "ParamControl.h"

namespace chordmonkey::ui
{
    // Drags the current pattern out as a .mid (ChordMonkeyProcessor::writePatternExport).
    class PatternExportHandle : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        PatternExportHandle (ChordMonkeyProcessor&, std::function<int()> pattern);
        void setEnabledFor (bool hasChords);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordMonkeyProcessor& processor;
        std::function<int()> currentPattern;
        bool enabled = false, dragging = false;
    };

    class WorkspacePanel : public juce::Component,
                           public juce::DragAndDropTarget,
                           public juce::SettableTooltipClient
    {
    public:
        WorkspacePanel (ChordMonkeyProcessor&, Bindings&);

        void refresh (const UiModel&);
        static int preferredHeight();

        // The pattern shown (the Pattern parameter).
        int currentPattern() const;

        // Actions (each one setWorkspace).
        void addPaletteChord (int paletteSlot, int targetSlot = -1);   // -1 or past the end: append; else replace that slot's chord
        void moveSlot (int from, int to);
        void setSlotLength (int slot, int halfBeats);
        void setSlotInversion (int slot, int inversion);
        void setSlotOctave (int slot, int octave);
        void duplicateSlot (int slot);
        void deleteSlot (int slot);
        void transposeToKey();
        void copyPatternTo (int other);
        void clearPattern();
        bool loadChordSet (int entry);                  // ChordSetManager entry, into the pattern
        juce::Result saveChordSet (const juce::String& name);
        bool usesSuggestions() const;                   // load Scale and Style with a chord set (an editor preference)
        void setUsesSuggestions (bool);

        // Slot geometry (for the mouse and the test).
        juce::Rectangle<float> slotBounds (int slot) const;
        int slotAt (juce::Point<float>) const;          // -1 outside the slots

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

        static constexpr int titleHeight = 22, bodyHeight = 52;

    private:
        void edit (const std::function<void (Pattern&)>& change);
        void showSlotMenu (int slot);
        void showPatternMenu();
        void showChordSetMenu();
        void askChordSetName();
        void stopPreview();

        ChordMonkeyProcessor& processor;
        Bindings& bindings;
        UiModel model;
        std::array<juce::TextButton, numPatterns> tabs;
        ParamControl play;
        juce::TextButton chordSetsButton { "Chord sets" }, patternButton { "Pattern" };
        PatternExportHandle exportHandle;
        juce::Rectangle<int> slotsArea;
        int previewing = -1, pressed = -1, dropHint = -1, shownPattern = -1;
    };
}
