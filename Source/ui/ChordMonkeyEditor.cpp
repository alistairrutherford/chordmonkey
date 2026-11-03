#include "ChordMonkeyEditor.h"
#include "../plugin/ChordMonkeyProcessor.h"

namespace chordmonkey
{
    namespace
    {
        const juce::Identifier keyboardId ("keyboard"), widthId ("width"), heightId ("height");
    }

    ChordMonkeyEditor::ChordMonkeyEditor (ChordMonkeyProcessor& p)
        : AudioProcessorEditor (p), processor (p), bindings (p.state),
          topBar (p, bindings, [this] (bool shown) { showKeyboard (shown); }),
          globals (p, bindings), keyboardRow (bindings), chordPads (p), workspace (p, bindings), effects (bindings), outputKeys (p)
    {
        setLookAndFeel (&lookAndFeel);
        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &globals, &keyboardRow, &chordPads, &workspace, &effects, &outputKeys })
            addAndMakeVisible (c);
        chordPads.onAddToPattern = [this] (int pad) { workspace.addPaletteChord (pad); };

        // Read the saved preferences before anything resizes the editor: setResizeLimits and the
        // first layouts would otherwise store 0×0, then the minimum, over the saved size. Nothing
        // is stored until the editor is up, so opening it doesn't tell the host the state changed.
        auto& ui = processor.uiState;
        const bool keyboardShown = ui.getProperty (keyboardId, true);
        const int width = juce::jlimit (minWidth, maxWidth, (int) ui.getProperty (widthId, defaultWidth));
        const int height = juce::jlimit (minHeight, maxHeight, (int) ui.getProperty (heightId, defaultHeight));

        outputKeys.setVisible (keyboardShown);
        topBar.setKeyboardShown (keyboardShown);
        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (width, height);
        sizeRestored = true;
        timerCallback();
        startTimerHz (30);
    }

    ChordMonkeyEditor::~ChordMonkeyEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void ChordMonkeyEditor::showKeyboard (bool shown)
    {
        outputKeys.setVisible (shown);
        topBar.setKeyboardShown (shown);
        processor.uiState.setProperty (keyboardId, shown, nullptr);
        resized();
    }

    void ChordMonkeyEditor::paint (juce::Graphics& g)
    {
        g.fillAll (ui::colours::background);
    }

    void ChordMonkeyEditor::resized()
    {
        auto r = getLocalBounds();
        topBar.setBounds (r.removeFromTop (44));
        if (outputKeys.isVisible())
            outputKeys.setBounds (r.removeFromBottom (60).reduced (12, 6));

        r.reduce (12, 10);
        globals.setBounds (r.removeFromTop (42));
        r.removeFromTop (4);
        keyboardRow.setBounds (r.removeFromTop (42));
        r.removeFromTop (8);
        effects.setBounds (r.removeFromBottom (ui::EffectsPanel::preferredHeight()));
        r.removeFromBottom (8);
        workspace.setBounds (r.removeFromBottom (ui::WorkspacePanel::preferredHeight()));
        r.removeFromBottom (8);
        // The pads take the space left, up to a height that still looks like pads; any more is
        // shared above and below them.
        chordPads.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), maxPadHeight)));

        if (sizeRestored)
        {
            processor.uiState.setProperty (widthId, getWidth(), nullptr);
            processor.uiState.setProperty (heightId, getHeight(), nullptr);
        }
    }

    void ChordMonkeyEditor::timerCallback()
    {
        topBar.refresh();

        ui::UiModel m;
        m.display = processor.readDisplay();
        m.settings = processor.currentSettings();
        m.analyse = processor.analyseMode();
        m.guess = processor.readKeyGuess();
        m.workspace = processor.getWorkspace();
        m.playingPattern = processor.playingPattern.load();
        m.playingSlot = processor.playingSlot.load();
        m.detect = processor.detectMode();
        m.heardChords = m.detect != params::DetectMode::Off ? processor.heardChords().count : 0;

        globals.refresh (m);
        keyboardRow.refresh (m);
        chordPads.refresh (m);
        workspace.refresh (m);
        outputKeys.refresh (m);
        effects.refresh (m);
    }
}
