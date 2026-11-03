#include "WorkspacePanel.h"
#include "Theme.h"
#include "../plugin/ChordMonkeyProcessor.h"
#include "../core/model/ChordSets.h"

namespace chordmonkey::ui
{
    namespace
    {
        const juce::Identifier suggestionsId ("chordSetSuggestions");

        juce::String lengthText (int halfBeats)
        {
            if (halfBeats % 8 == 0)
                return juce::String (halfBeats / 8) + (halfBeats == 8 ? " bar" : " bars");
            if (halfBeats == 1)
                return "1/2 beat";
            return (halfBeats % 2 == 0 ? juce::String (halfBeats / 2) : juce::String (halfBeats / 2.0, 1)) + (halfBeats == 2 ? " beat" : " beats");
        }
    }

    //==========================================================================
    PatternExportHandle::PatternExportHandle (ChordMonkeyProcessor& p, std::function<int()> pattern)
        : processor (p), currentPattern (std::move (pattern))
    {
        setTooltip ("Drag the pattern into Live as a MIDI clip: one loop, as it plays (style, voicing, bass, effects). Hold Alt for the chords only");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void PatternExportHandle::setEnabledFor (bool hasChords)
    {
        if (enabled != hasChords)
        {
            enabled = hasChords;
            repaint();
        }
    }

    void PatternExportHandle::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 2.5f);
        g.setColour (dragging ? colours::chord.withAlpha (0.35f) : colours::raised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (enabled ? colours::outline.brighter (0.2f) : colours::outline);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        g.setColour (enabled ? colours::text : colours::faint);
        g.setFont (font (12.0f, true));
        g.drawText ("Drag MIDI", getLocalBounds(), juce::Justification::centred);
    }

    void PatternExportHandle::mouseDrag (const juce::MouseEvent& e)
    {
        if (! enabled || dragging || e.getDistanceFromDragStart() < 4)
            return;
        juce::String error;
        const auto file = processor.writePatternExport (currentPattern(), e.mods.isAltDown(), error);
        if (! file.existsAsFile())
        {
            setTooltip (error);
            return;
        }
        dragging = true;
        repaint();
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<PatternExportHandle> (this)]
        {
            if (safe != nullptr)
            {
                safe->dragging = false;
                safe->repaint();
            }
        });
    }

    void PatternExportHandle::mouseUp (const juce::MouseEvent&)
    {
        dragging = false;
        repaint();
    }

    //==========================================================================
    WorkspacePanel::WorkspacePanel (ChordMonkeyProcessor& p, Bindings& b)
        : processor (p), bindings (b), play (b, params::play, "Play"),
          exportHandle (p, [this] { return currentPattern(); })
    {
        setTooltip ("The pattern: drag a chord pad here (or right-click a pad) to add it; click a slot to hear it, "
                    "drag to reorder, right-click for its length, inversion and octave");
        bindings.bound (params::pattern);
        for (int i = 0; i < numPatterns; ++i)
        {
            auto& t = tabs[(size_t) i];
            t.setButtonText (patternName (i));
            t.setClickingTogglesState (false);
            t.setTooltip ("Pattern " + juce::String (patternName (i)) + " (a change while playing waits for the next bar)");
            t.onClick = [this, i] { bindings.set (params::pattern, (float) i); };
            addAndMakeVisible (t);
        }
        play.setTooltip ("Play: the pattern's chords play in time while Live plays");
        chordSetsButton.setTooltip ("Load a chord set into the pattern (in the Key), or save the pattern as one");
        chordSetsButton.onClick = [this] { showChordSetMenu(); };
        patternButton.setTooltip ("Transpose the pattern to the Key, copy it to another pattern, clear it");
        patternButton.onClick = [this] { showPatternMenu(); };
        bindings.bound (params::detect);
        detectButton.onClick = [this] { detectClicked(); };
        detectButton.setClickingTogglesState (false);
        cancelDetectButton.setTooltip ("Stop capturing; the pattern stays as it was");
        cancelDetectButton.onClick = [this] { cancelDetect(); };
        addChildComponent (cancelDetectButton);
        for (auto* c : std::initializer_list<juce::Component*> { &play, &detectButton, &chordSetsButton, &patternButton, &exportHandle })
            addAndMakeVisible (c);
        updateDetect();
    }

    int WorkspacePanel::preferredHeight() { return titleHeight + bodyHeight + 6; }

    int WorkspacePanel::currentPattern() const
    {
        return juce::jlimit (0, numPatterns - 1, (int) std::lround (bindings.value (params::pattern)));
    }

    void WorkspacePanel::refresh (const UiModel& m)
    {
        const int cur = currentPattern();
        const bool changed = ! (m.workspace == model.workspace) || m.playingPattern != model.playingPattern || m.playingSlot != model.playingSlot
                          || m.settings.key != model.settings.key || m.settings.scale != model.settings.scale || cur != shownPattern;
        model = m;
        shownPattern = cur;
        for (int i = 0; i < numPatterns; ++i)
            tabs[(size_t) i].setToggleState (i == cur, juce::dontSendNotification);
        exportHandle.setEnabledFor (! m.workspace.patterns[(size_t) cur].empty());
        updateDetect();
        if (changed)
            repaint();
    }

    //==========================================================================
    // Detect (Phase 8).

    void WorkspacePanel::updateDetect()
    {
        const bool capturing = model.detect != params::DetectMode::Off;
        const int n = juce::jmin (model.heardChords, numPatternSlots);
        const juce::String text = ! capturing ? juce::String ("Detect")
                                : model.heardChords == 0 ? juce::String::fromUTF8 ("Listening\xe2\x80\xa6")
                                : "Use " + juce::String (n) + (n == 1 ? " chord" : " chords");
        if (detectButton.getButtonText() != text)
            detectButton.setButtonText (text);
        detectButton.setToggleState (capturing, juce::dontSendNotification);
        detectButton.setTooltip (! capturing ? "Detect: play chords into ChordMonkey (the keyboard, or a clip on the track feeding it) and they become "
                                               "pattern " + juce::String (patternName (currentPattern())) + ", with the key heard"
                                 : model.heardChords == 0 ? "Capturing: play chords, or let a clip play into this track. The cross stops"
                                 : "Write the " + juce::String (n) + (n == 1 ? " chord" : " chords") + " heard into pattern "
                                   + juce::String (patternName (currentPattern())) + " (replacing it), and set Key and Scale to the key heard");
        if (cancelDetectButton.isVisible() != capturing)
        {
            cancelDetectButton.setVisible (capturing);
            resized();
        }
    }

    void WorkspacePanel::detectClicked()
    {
        if (model.detect == params::DetectMode::Off && processor.detectMode() == params::DetectMode::Off)
            bindings.set (params::detect, (float) params::DetectMode::Capture);
        else
            processor.applyDetect();
        model.detect = processor.detectMode();
        updateDetect();
    }

    void WorkspacePanel::cancelDetect()
    {
        processor.cancelDetect();
        model.detect = params::DetectMode::Off;
        updateDetect();
    }

    //==========================================================================
    // Actions.

    void WorkspacePanel::edit (const std::function<void (Pattern&)>& change)
    {
        auto w = processor.getWorkspace();
        change (w.patterns[(size_t) currentPattern()]);
        processor.setWorkspace (w);
        model.workspace = w;
        repaint();
    }

    void WorkspacePanel::addPaletteChord (int paletteSlot, int targetSlot)
    {
        if (paletteSlot < 0 || paletteSlot >= numChordSlots)
            return;
        // The palette as the engine makes it from the parameters (not the audio thread's
        // display, which is empty until the first block).
        const auto s = processor.currentSettings();
        const ChordSet palette = makeChords (s.group, s.scale, progressionDegrees (s.progression));
        const auto chord = chordFromSlot (palette[(size_t) paletteSlot], { s.key, s.scale, s.chordOctave });
        const int key = s.key;
        edit ([&] (Pattern& p)
        {
            const int n = p.size();
            if (n == 0)
                p.key = (int8_t) key;     // a new pattern is in the Key
            if (targetSlot >= 0 && targetSlot < n)
                p.slots[(size_t) targetSlot].chord = chord;
            else
                p.set (n, makePatternSlot (chord));
        });
    }

    void WorkspacePanel::moveSlot (int from, int to)
    {
        edit ([&] (Pattern& p) { p.move (from, juce::jmin (to, p.size() - 1)); });
    }

    void WorkspacePanel::setSlotLength (int slot, int halfBeats)
    {
        edit ([&] (Pattern& p)
        {
            if (slot >= 0 && slot < p.size())
                p.slots[(size_t) slot].halfBeats = (uint8_t) juce::jlimit (minSlotHalfBeats, maxSlotHalfBeats, halfBeats);
        });
    }

    void WorkspacePanel::setSlotInversion (int slot, int inversion)
    {
        edit ([&] (Pattern& p)
        {
            if (slot >= 0 && slot < p.size())
                p.slots[(size_t) slot].inversion = (int8_t) juce::jlimit (0, maxSlotInversion, inversion);
        });
    }

    void WorkspacePanel::setSlotOctave (int slot, int octave)
    {
        edit ([&] (Pattern& p)
        {
            if (slot >= 0 && slot < p.size())
                p.slots[(size_t) slot].octave = (int8_t) juce::jlimit (minSlotOctave, maxSlotOctave, octave);
        });
    }

    void WorkspacePanel::duplicateSlot (int slot)
    {
        edit ([&] (Pattern& p)
        {
            if (slot >= 0 && slot < p.size())
                p.insert (slot + 1, p.slots[(size_t) slot]);
        });
    }

    void WorkspacePanel::deleteSlot (int slot)
    {
        edit ([&] (Pattern& p) { p.remove (slot); });
    }

    void WorkspacePanel::transposeToKey()
    {
        const int key = processor.currentSettings().key;
        edit ([&] (Pattern& p) { p.transposeToKey (key); });
    }

    void WorkspacePanel::copyPatternTo (int other)
    {
        if (other < 0 || other >= numPatterns || other == currentPattern())
            return;
        auto w = processor.getWorkspace();
        w.patterns[(size_t) other] = w.patterns[(size_t) currentPattern()];
        processor.setWorkspace (w);
        model.workspace = w;
        repaint();
    }

    void WorkspacePanel::clearPattern()
    {
        const int key = processor.currentSettings().key;
        edit ([&] (Pattern& p)
        {
            p.clear();
            p.key = (int8_t) key;
        });
    }

    bool WorkspacePanel::loadChordSet (int entry)
    {
        const bool ok = processor.chordSets.load (entry, currentPattern(), usesSuggestions());
        model.workspace = processor.getWorkspace();
        repaint();
        return ok;
    }

    juce::Result WorkspacePanel::saveChordSet (const juce::String& name)
    {
        return processor.chordSets.save (name, currentPattern());
    }

    bool WorkspacePanel::usesSuggestions() const { return processor.uiState.getProperty (suggestionsId, true); }
    void WorkspacePanel::setUsesSuggestions (bool use) { processor.uiState.setProperty (suggestionsId, use, nullptr); }

    //==========================================================================
    // Menus.

    void WorkspacePanel::showSlotMenu (int slot)
    {
        const auto& p = model.workspace.patterns[(size_t) currentPattern()];
        if (slot < 0 || slot >= p.size())
            return;
        const auto& s = p.slots[(size_t) slot];
        juce::PopupMenu length, inversion, octave;
        for (int hb : { 1, 2, 4, 6, 8, 12, 16 })
            length.addItem (lengthText (hb), true, s.halfBeats == hb, [this, slot, hb] { setSlotLength (slot, hb); });
        const char* inversions[] = { "Root position", "1st inversion", "2nd inversion", "3rd inversion" };
        for (int i = 0; i <= maxSlotInversion; ++i)
            inversion.addItem (inversions[i], true, s.inversion == i, [this, slot, i] { setSlotInversion (slot, i); });
        for (int o = minSlotOctave; o <= maxSlotOctave; ++o)
            octave.addItem (o > 0 ? "+" + juce::String (o) : juce::String (o), true, s.octave == o, [this, slot, o] { setSlotOctave (slot, o); });

        juce::PopupMenu m;
        m.addSectionHeader (namePatternSlot (s, model.settings.key, model.settings.scale).text());
        m.addSubMenu ("Length", length);
        m.addSubMenu ("Inversion", inversion);
        m.addSubMenu ("Octave", octave);
        m.addSeparator();
        m.addItem ("Duplicate", ! p.full(), false, [this, slot] { duplicateSlot (slot); });
        m.addItem ("Delete", [this, slot] { deleteSlot (slot); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotBounds (slot).toNearestInt())));
    }

    void WorkspacePanel::showPatternMenu()
    {
        const int cur = currentPattern();
        const auto& p = model.workspace.patterns[(size_t) cur];
        juce::PopupMenu copy;
        for (int i = 0; i < numPatterns; ++i)
            if (i != cur)
                copy.addItem ("Pattern " + juce::String (patternName (i)), [this, i] { copyPatternTo (i); });

        juce::PopupMenu m;
        const juce::String key = juce::String (keyName (model.settings.key)).upToFirstOccurrenceOf ("/", false, false);
        m.addItem ("Transpose to " + key, ! p.empty() && p.key != model.settings.key, false, [this] { transposeToKey(); });
        m.addSubMenu ("Copy to", copy, ! p.empty());
        m.addItem ("Clear", ! p.empty(), false, [this] { clearPattern(); });
        m.addSeparator();
        m.addItem ("Save as chord set...", ! p.empty(), false, [this] { askChordSetName(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternButton));
    }

    void WorkspacePanel::showChordSetMenu()
    {
        auto& sets = processor.chordSets;
        sets.rescan();
        const auto& all = sets.getEntries();
        juce::StringArray categories;
        for (const auto& e : all)
            categories.addIfNotAlreadyThere (e.category);

        juce::PopupMenu m;
        m.addSectionHeader ("Into pattern " + juce::String (patternName (currentPattern())) + ", in "
                            + juce::String (keyName (model.settings.key)).upToFirstOccurrenceOf ("/", false, false));
        for (const auto& category : categories)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < all.size(); ++i)
                if (all[i].category == category)
                    sub.addItem (all[i].name + "   " + all[i].progression, [this, i] { loadChordSet (i); });
            m.addSubMenu (category, sub);
        }
        m.addSeparator();
        m.addItem ("Also set its Scale and Style", true, usesSuggestions(), [this] { setUsesSuggestions (! usesSuggestions()); });
        m.addItem ("Save the pattern as a chord set...", ! model.workspace.patterns[(size_t) currentPattern()].empty(), false,
                   [this] { askChordSetName(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chordSetsButton));
    }

    void WorkspacePanel::askChordSetName()
    {
        auto* w = new juce::AlertWindow ("Save chord set", "A name for pattern " + juce::String (patternName (currentPattern())) + ":",
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", "My chord set");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<WorkspacePanel> (this), w] (int result)
        {
            if (result == 1 && safe != nullptr)
                if (const auto r = safe->saveChordSet (w->getTextEditorContents ("name")); r.failed())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", r.getErrorMessage());
        }), true);
    }

    //==========================================================================
    // Layout and paint.

    juce::Rectangle<float> WorkspacePanel::slotBounds (int slot) const
    {
        const float w = slotsArea.getWidth() / (float) numPatternSlots;
        return juce::Rectangle<float> (slotsArea.getX() + slot * w, (float) slotsArea.getY(), w, (float) slotsArea.getHeight()).reduced (2.0f, 0.0f);
    }

    int WorkspacePanel::slotAt (juce::Point<float> p) const
    {
        if (! slotsArea.toFloat().contains (p))
            return -1;
        return juce::jlimit (0, numPatternSlots - 1, (int) ((p.x - slotsArea.getX()) / (slotsArea.getWidth() / (float) numPatternSlots)));
    }

    void WorkspacePanel::resized()
    {
        auto r = getLocalBounds().reduced (6, 0);
        auto head = r.removeFromTop (titleHeight);
        exportHandle.setBounds (head.removeFromRight (72).reduced (0, 1));
        head.removeFromRight (6);
        patternButton.setBounds (head.removeFromRight (72).reduced (0, 1));
        head.removeFromRight (6);
        chordSetsButton.setBounds (head.removeFromRight (92).reduced (0, 1));
        head.removeFromRight (6);
        detectButton.setBounds (head.removeFromRight (104).reduced (0, 1));
        if (cancelDetectButton.isVisible())
            cancelDetectButton.setBounds (head.removeFromRight (22).reduced (0, 1));

        auto body = r.removeFromTop (bodyHeight).reduced (0, 6);
        for (auto& t : tabs)
            t.setBounds (body.removeFromLeft (26).reduced (1, 6));
        body.removeFromLeft (4);
        play.setBounds (body.removeFromLeft (56).withSizeKeepingCentre (56, 28));
        body.removeFromLeft (8);
        slotsArea = body;
    }

    void WorkspacePanel::paint (juce::Graphics& g)
    {
        g.setColour (colours::raised.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (colours::dim);
        g.setFont (font (11.5f, true));
        g.drawText ("WORKSPACE", getLocalBounds().reduced (10, 0).removeFromTop (titleHeight), juce::Justification::centredLeft);

        const int cur = currentPattern();
        const auto& p = model.workspace.patterns[(size_t) cur];
        const int n = p.size();
        for (int i = 0; i < numPatternSlots; ++i)
        {
            const auto r = slotBounds (i);
            if (i >= n)
            {
                g.setColour (i == dropHint ? colours::chord : colours::outline);
                const float dash[] = { 4.0f, 3.0f };
                juce::Path outline;
                outline.addRoundedRectangle (r.reduced (0.5f), 6.0f);
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
                g.fillPath (dashed);
                if (i == n)
                {
                    g.setColour (colours::faint);
                    g.setFont (font (11.0f));
                    g.drawFittedText (n == 0 ? "drag a chord here" : "+", r.toNearestInt().reduced (4), juce::Justification::centred, 2);
                }
                continue;
            }
            const auto& s = p.slots[(size_t) i];
            const bool lit = model.playingPattern == cur && model.playingSlot == i;
            g.setColour (lit ? colours::chord : colours::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (i == dropHint ? colours::chord : (lit ? colours::chord.brighter (0.3f) : colours::outline));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, i == dropHint ? 2.0f : 1.0f);

            auto text = r.reduced (5.0f, 3.0f);
            g.setColour (lit ? colours::background : colours::text);
            g.setFont (font (juce::jlimit (12.0f, 16.0f, r.getHeight() * 0.36f), true));
            g.drawFittedText (namePatternSlot (s, model.settings.key, model.settings.scale).text(), text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(),
                              juce::Justification::centredBottom, 1, 0.7f);
            const auto numeral = juce::String (numeralName (s.chord, model.settings.key));
            g.setColour (lit ? colours::background.withAlpha (0.75f) : colours::dim);
            g.setFont (font (10.5f));
            g.drawFittedText ((numeral.containsChar ('(') ? juce::String() : numeral + juce::String::fromUTF8 (" \xc2\xb7 ")) + lengthText (s.halfBeats),
                              text.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
        }
    }

    //==========================================================================
    // Mouse and drops.

    void WorkspacePanel::stopPreview()
    {
        if (previewing >= 0)
            processor.pushSlotPreview (previewing, false);
        previewing = -1;
    }

    void WorkspacePanel::mouseDown (const juce::MouseEvent& e)
    {
        const int slot = slotAt (e.position);
        const bool used = slot >= 0 && slot < model.workspace.patterns[(size_t) currentPattern()].size();
        if (e.mods.isPopupMenu())
        {
            if (used)
                showSlotMenu (slot);
            return;
        }
        pressed = used ? slot : -1;
        if (used)
        {
            previewing = slot;
            processor.pushSlotPreview (slot, true);
        }
    }

    void WorkspacePanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (pressed < 0 || e.getDistanceFromDragStart() < 6)
            return;
        stopPreview();
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            container->startDragging ("slot:" + juce::String (pressed), this);
        pressed = -1;
    }

    void WorkspacePanel::mouseUp (const juce::MouseEvent&)
    {
        stopPreview();
        pressed = -1;
    }

    bool WorkspacePanel::isInterestedInDragSource (const SourceDetails& d)
    {
        const auto desc = d.description.toString();
        return desc.startsWith ("palette:") || desc.startsWith ("slot:");
    }

    void WorkspacePanel::itemDragMove (const SourceDetails& d)
    {
        const int n = model.workspace.patterns[(size_t) currentPattern()].size();
        const int slot = slotAt (d.localPosition.toFloat());
        const int hint = slot < 0 ? -1 : juce::jmin (slot, d.description.toString().startsWith ("slot:") ? n - 1 : n);
        if (hint != dropHint)
        {
            dropHint = hint;
            repaint();
        }
    }

    void WorkspacePanel::itemDragExit (const SourceDetails&)
    {
        dropHint = -1;
        repaint();
    }

    void WorkspacePanel::itemDropped (const SourceDetails& d)
    {
        dropHint = -1;
        const auto desc = d.description.toString();
        const int slot = slotAt (d.localPosition.toFloat());
        const int value = desc.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (desc.startsWith ("palette:"))
            addPaletteChord (value, slot);
        else if (desc.startsWith ("slot:") && slot >= 0)
            moveSlot (value, slot);
        repaint();
    }
}
