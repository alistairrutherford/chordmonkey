#include "Panels.h"
#include "Theme.h"

namespace chordmonkey::ui
{
    //==========================================================================
    TopBar::TopBar (ChordMonkeyProcessor& p, Bindings& b, std::function<void (bool)> showKeyboard)
        : presets (p), exportHandle (p), enable (b, params::enable, "On"),
          pattern (b, params::pattern, {}, false), play (b, params::play, "Play")
    {
        enable.setTooltip ("On: chords from every key. Off: notes pass through unchanged");
        pattern.setTooltip ("Pattern: which of the workspace's four patterns plays (a change while playing waits for the next bar)");
        play.setTooltip ("Play: the pattern's chords play in time while the host plays");
        addAndMakeVisible (enable);
        addAndMakeVisible (pattern);
        addAndMakeVisible (play);
        addAndMakeVisible (presets);
        addAndMakeVisible (exportHandle);
        keyboardButton.setClickingTogglesState (true);
        keyboardButton.setTooltip ("Show or hide the keyboard at the bottom");
        keyboardButton.onClick = [this, showKeyboard] { showKeyboard (keyboardButton.getToggleState()); };
        addAndMakeVisible (keyboardButton);
    }

    void TopBar::setKeyboardShown (bool shown) { keyboardButton.setToggleState (shown, juce::dontSendNotification); }

    void TopBar::refresh()
    {
        presets.refresh();
        exportHandle.refresh();
    }

    void TopBar::paint (juce::Graphics& g)
    {
        g.setColour (colours::panel);
        g.fillRect (getLocalBounds());
        g.setColour (colours::outline);
        g.fillRect (getLocalBounds().removeFromBottom (1));
        g.setColour (colours::chord);
        g.setFont (font (17.0f, true));
        g.drawText ("ChordMonkey", nameArea, juce::Justification::centredLeft);
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (12, 6);
        nameArea = r.removeFromLeft (112);
        enable.setBounds (r.removeFromLeft (44).withSizeKeepingCentre (44, 28));
        r.removeFromLeft (12);
        keyboardButton.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 28));
        r.removeFromRight (8);
        exportHandle.setBounds (r.removeFromRight (104).withSizeKeepingCentre (104, 28));
        r.removeFromRight (12);
        play.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 28));
        r.removeFromRight (6);
        pattern.setBounds (r.removeFromRight (56).withSizeKeepingCentre (56, 28));
        r.removeFromRight (12);
        presets.setBounds (r.removeFromLeft (juce::jmin (r.getWidth(), 360)).withTrimmedBottom (2));
    }

    //==========================================================================
    HeardKey::HeardKey (ChordMonkeyProcessor& p, Bindings& b) : processor (p), bindings (b)
    {
        bindings.bound (params::analyse);
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        update();
    }

    void HeardKey::refresh (const UiModel& m)
    {
        if (m.analyse == mode && m.guess == guess && m.settings.key == key && m.settings.scale == scale)
            return;
        mode = m.analyse;
        guess = m.guess;
        key = m.settings.key;
        scale = m.settings.scale;
        update();
    }

    bool HeardKey::matchesKey() const noexcept
    {
        return guess.has() && guess.tonic == key && scaleFamily (scale) == (guess.minor ? ScaleFamily::Minor : ScaleFamily::Major);
    }

    juce::Rectangle<int> HeardKey::boxBounds() const noexcept
    {
        auto r = getLocalBounds();
        r.removeFromTop (juce::jmin (captionHeight, r.getHeight() / 2));
        return r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 26));
    }

    juce::Rectangle<int> HeardKey::closeBounds() const noexcept
    {
        return analysing() ? boxBounds().removeFromRight (closeWidth) : juce::Rectangle<int>();
    }

    bool HeardKey::fits (const juce::String& s) const
    {
        const int room = boxBounds().getWidth() - 12 - (analysing() ? closeWidth : 0) - (! analysing() && matchesKey() ? 14 : 0);
        return juce::GlyphArrangement::getStringWidthInt (font (13.0f), s) <= room;
    }

    // The text's margins and the cross, around the shortest guess ("F#m?") and the longest
    // ("Ab major 100%").
    int HeardKey::minimumWidth() { return juce::GlyphArrangement::getStringWidthInt (font (13.0f), "F#m?") + 12 + closeWidth + 1; }
    int HeardKey::preferredWidth() { return juce::GlyphArrangement::getStringWidthInt (font (13.0f), "Ab major 100%") + 12 + closeWidth + 1; }

    juce::String HeardKey::chooseText (const juce::String& full, const juce::String& shorter, const juce::String& shortest) const
    {
        return fits (full) ? full : (fits (shorter) ? shorter : shortest);
    }

    void HeardKey::update()
    {
        const juce::String name = guess.has() ? juce::String (guessKeyName (guess.tonic, guess.minor)) : juce::String();
        const juce::String longName = name + (guess.minor ? " minor" : " major"), chordName = name + (guess.minor ? "m" : "");
        const juce::String percent = juce::String (guess.fit) + "%";
        highlighted = false;
        if (! analysing())
        {
            text = matchesKey() ? chooseText (longName, chordName, chordName) : juce::String ("Analyse");
            setTooltip (matchesKey() ? "Key is already " + longName + ", as heard. Click to analyse again"
                                     : "Analyse: stop playing chords and listen to the notes coming in, to guess their key");
        }
        else if (! guess.has())
        {
            text = juce::String::fromUTF8 ("Listening\xe2\x80\xa6");
            setTooltip ("Listening: play, or let a clip play into this track. The cross goes back to chords");
        }
        else
        {
            const bool confident = guess.state == GuessState::Confident;
            text = confident ? chooseText (longName + " " + percent, chordName + " " + percent, chordName)
                             : chooseText (longName + "?", chordName + "?", chordName + "?");
            highlighted = confident;
            const Scale target = scaleForGuess (scale, guess.minor);
            juce::String tip = "Use " + longName + ": set Key to " + name;
            if (target != scale)
                tip << " and Scale to " << scaleInfo (target).name;
            tip << ", and play chords again. " << percent << " is how well the notes fit " << longName << "'s profile";
            if (guess.runnerUpTonic >= 0)
                tip << "; next: " << guessKeyName (guess.runnerUpTonic, guess.runnerUpMinor) << (guess.runnerUpMinor ? " minor " : " major ")
                    << guess.runnerUpFit << "%";
            setTooltip (tip);
        }
        repaint();
    }

    void HeardKey::resized() { update(); }   // the text's form depends on the width

    void HeardKey::paint (juce::Graphics& g)
    {
        // The caption, as a ParamControl's (a Label's text, 5 px in), with a dot while listening.
        const auto captionFont = font (12.0f);
        g.setFont (captionFont);
        g.setColour (colours::dim);
        const auto caption = getLocalBounds().removeFromTop (juce::jmin (captionHeight, getHeight() / 2)).withTrimmedLeft (5);
        g.drawText ("Heard", caption, juce::Justification::centredLeft);
        if (analysing())
        {
            g.setColour (colours::sounding);
            const float x = (float) caption.getX() + juce::GlyphArrangement::getStringWidth (captionFont, "Heard") + 5.0f;
            g.fillEllipse (x, (float) caption.getCentreY() - 2.5f, 5.0f, 5.0f);
        }

        const auto box = boxBounds().toFloat().reduced (0.5f);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (highlighted ? colours::chord : colours::outline);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        auto area = boxBounds().reduced (6, 0);
        if (analysing())
        {
            const auto x = closeBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f);
            g.setColour (colours::dim);
            g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.5f);
            g.drawLine ({ x.getBottomLeft(), x.getTopRight() }, 1.5f);
            area.removeFromRight (closeWidth - 4);
        }
        else if (matchesKey())
        {
            // A tick before the key.
            const auto t = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (10.0f, 8.0f);
            juce::Path tick;
            tick.startNewSubPath (t.getX(), t.getCentreY());
            tick.lineTo (t.getX() + 3.5f, t.getBottom());
            tick.lineTo (t.getRight(), t.getY());
            g.setColour (colours::chord.withAlpha (0.8f));
            g.strokePath (tick, juce::PathStrokeType (1.6f));
        }

        const bool dim = analysing() ? ! highlighted : matchesKey();
        g.setColour (highlighted ? colours::chord : (dim ? colours::dim : colours::text));
        g.setFont (font (13.0f, highlighted));
        g.drawText (text, area, juce::Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::chord.withAlpha (0.5f));
            g.drawRoundedRectangle (box.reduced (1.5f), 3.0f, 1.0f);
        }
    }

    void HeardKey::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && boxBounds().contains (e.getPosition()))
            click (closeBounds().contains (e.getPosition()));
    }

    bool HeardKey::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
        {
            click (false);
            return true;
        }
        if (k == juce::KeyPress::escapeKey && analysing())
        {
            click (true);
            return true;
        }
        return false;
    }

    void HeardKey::click (bool close)
    {
        if (close)
        {
            if (analysing())
                bindings.set (params::analyse, (float) params::AnalyseMode::Off);
            return;
        }
        if (! analysing())
            bindings.set (params::analyse, (float) params::AnalyseMode::Listen);
        else if (guess.has())
            processor.applyGuess();
    }

    //==========================================================================
    GlobalRow::GlobalRow (ChordMonkeyProcessor& p, Bindings& b)
        : heard (p, b), bindings (b), key (b, params::key), scale (b, params::scale), group (b, params::chordGroup), progression (b, params::progression),
          octave (b, params::chordOctave, "Octave"), keyOctave (b, params::keyOctave, "Key octave")
    {
        progression.setTooltip ("Which chord each white key plays");
        octave.setTooltip ("Chord Octave: the register the chords play in");
        keyOctave.setTooltip ("Key octave. Ignore: every octave plays the same chord. Follow: the octave you play moves the chord (C2 plays it as written)");
        for (auto* c : std::initializer_list<juce::Component*> { &key, &heard, &scale, &group, &progression, &octave, &keyOctave })
            addAndMakeVisible (c);
    }

    void GlobalRow::refresh (const UiModel& m) { heard.refresh (m); }

    void GlobalRow::resized()
    {
        // Sized to the content (ChordChimp's docs/KEY_DETECTION_PLAN.md K5.1): each menu gets the width of its
        // longest choice, Octave a little slider; then Heard grows to fit its longest guess, and
        // what's left is shared by weight. Each column has 4 px either side.
        constexpr int pad = 8, comboExtra = LookAndFeel::comboArrowWidth + 2 * LookAndFeel::comboTextInset;
        const auto comboFont = font (13.0f);
        const auto widest = [&] (const char* id)
        {
            int w = 0;
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (bindings.param (id)))
                for (const auto& t : c->choices)
                    w = juce::jmax (w, juce::GlyphArrangement::getStringWidthInt (comboFont, t));
            return w + comboExtra + pad;
        };
        struct Col { juce::Component* c; int min, weight; };
        Col cols[] = { { &key, widest (params::key), 1 },
                       { &heard, HeardKey::minimumWidth() + pad, 0 },
                       { &scale, widest (params::scale), 2 },
                       { &group, widest (params::chordGroup), 2 },
                       { &progression, widest (params::progression), 2 },
                       { &octave, 52 + pad, 1 },
                       { &keyOctave, widest (params::keyOctave), 1 } };
        int spare = getWidth(), weights = 0;
        for (const auto& c : cols)
        {
            spare -= c.min;
            weights += c.weight;
        }
        int widths[7] {};
        if (spare < 0)
        {
            // Narrower than the content (not at the editor's sizes): everything shrinks alike.
            for (size_t i = 0; i < 7; ++i)
                widths[i] = cols[i].min * getWidth() / (getWidth() - spare);
        }
        else
        {
            const int heardMore = juce::jmin (spare, HeardKey::preferredWidth() + pad - cols[1].min);
            spare -= heardMore;
            for (size_t i = 0; i < 7; ++i)
                widths[i] = cols[i].min + (i == 1 ? heardMore : spare * cols[i].weight / weights);
        }
        auto r = getLocalBounds();
        for (size_t i = 0; i < 7; ++i)
            cols[i].c->setBounds (r.removeFromLeft (widths[i]).reduced (pad / 2, 0));
    }

    //==========================================================================
    EffectsPanel::EffectsPanel (Bindings& b) : bindings (b), steps (colours::chord)
    {
        auto& strum = cards[0];
        strum.title = "Strum";
        strum.controls = { &add (params::strum, "Direction"), &add (params::strumRate, "Rate") };

        auto& velocity = cards[1];
        velocity.title = "Velocity";
        velocity.controls = { &add (params::velocity, "Curve"), &add (params::velocityPeak, "Peak"), &add (params::velocityRange, "Range") };

        auto& arp = cards[2];
        arp.title = "Arp";
        arp.controls = { &add (params::arp, "Pattern"), &add (params::arpRate, "Rate"), &add (params::arpRange, "Range"),
                         &add (params::arpGate, "Gate") };

        auto& voicing = cards[3];
        voicing.title = "Voicing";
        voicing.controls = { &add (params::stack), &add (params::hold), &add (params::voiceLeading, "Voice leading") };
        voicing.controls.back()->setTooltip ("Smooth: each chord takes the inversion nearest the one before");

        auto& repeat = cards[4];
        repeat.title = "Repeat";
        repeat.toggle = &add (params::repeat, "On");
        repeat.controls = { &add (params::repeatRate, "Rate"), &add (params::repeatGate, "Gate") };

        auto& seq = cards[5];
        seq.title = "Step seq";
        seq.toggle = &add (params::seq, "On");
        seq.controls = { &add (params::seqPattern, "Pattern"), &add (params::seqSwing, "Swing"), &add (params::seqGate, "Gate") };
        seq.extra = &steps;
        addAndMakeVisible (steps);
    }

    ParamControl& EffectsPanel::add (const juce::String& paramId, const juce::String& label)
    {
        auto* c = controls.add (new ParamControl (bindings, paramId, label));
        addAndMakeVisible (c);
        return *c;
    }

    void EffectsPanel::refresh (const UiModel& m) { steps.setSteps (m.settings.fx.seq.steps()); }

    int EffectsPanel::preferredHeight()
    {
        const int card = titleHeight + controlHeight + 8;
        return 2 * card + (card + stepsHeight + 4) + 2 * gap;
    }

    void EffectsPanel::paint (juce::Graphics& g)
    {
        for (const auto& c : cards)
        {
            g.setColour (colours::raised.withAlpha (0.55f));
            g.fillRoundedRectangle (c.bounds.toFloat(), 6.0f);
            g.setColour (colours::dim);
            g.setFont (font (11.5f, true));
            g.drawText (c.title.toUpperCase(), c.bounds.reduced (10, 0).removeFromTop (titleHeight), juce::Justification::centredLeft);
        }
    }

    void EffectsPanel::layoutCard (Card& c)
    {
        auto r = c.bounds.reduced (6, 0);
        auto head = r.removeFromTop (titleHeight);
        if (c.toggle != nullptr)
            c.toggle->setBounds (head.removeFromRight (48).reduced (0, 2));
        auto row = r.removeFromTop (controlHeight);
        const int w = row.getWidth() / juce::jmax (1, (int) c.controls.size());
        for (auto* control : c.controls)
            control->setBounds (row.removeFromLeft (w).reduced (3, 0));
        if (c.extra != nullptr)
            c.extra->setBounds (r.withTrimmedTop (4).removeFromTop (stepsHeight).reduced (3, 0));
    }

    void EffectsPanel::resized()
    {
        auto r = getLocalBounds();
        const int card = titleHeight + controlHeight + 8;
        const int heights[] = { card, card, card + stepsHeight + 4 };
        for (int row = 0; row < 3; ++row)
        {
            auto line = r.removeFromTop (heights[row]);
            r.removeFromTop (gap);
            cards[(size_t) (2 * row)].bounds = line.removeFromLeft ((line.getWidth() - gap) / 2);
            line.removeFromLeft (gap);
            cards[(size_t) (2 * row + 1)].bounds = line;
        }
        for (auto& c : cards)
            layoutCard (c);
    }
}
