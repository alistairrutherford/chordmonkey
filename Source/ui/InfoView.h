// The Info View (as NoteCap's, DrumChimp's, ChordChimp's and Live's own): a strip along the
// bottom of the editor that says what the thing under the mouse does and what its settings
// mean, in a bold title and a line or two of text; elsewhere a short hint. It replaces tooltips.
//
// Every description is in one table (InfoView.cpp), keyed by what is pointed at: a parameter's
// ID for its control (ParamControl), "ui.…" for the buttons that aren't parameters, "display.…"
// for the pads, the keyboard, Heard and the steps, "card.…" for the effects cards, "area.…" for
// the workspace. The editor finds the key under the mouse (ChordMonkeyEditor::infoKeyAt) and
// shows it.
#pragma once

#include "Theme.h"

namespace chordmonkey::ui
{
    class InfoView : public juce::Component
    {
    public:
        static constexpr int stripHeight = 54;

        // The entry for a key (empty if there is none).
        static juce::String titleFor (const juce::String& key);
        static juce::String textFor (const juce::String& key);
        static bool has (const juce::String& key);
        static juce::StringArray keys();      // every key in the table (for tests)
        static juce::String hint();

        // Shows the key's entry (with `live` in place of its text when there is any: what the
        // thing says about itself now), or the hint for an empty or unknown key.
        void showKey (const juce::String& key, const juce::String& live = {});
        const juce::String& shownKey() const noexcept { return key; }
        const juce::String& shownTitle() const noexcept { return title; }
        const juce::String& shownText() const noexcept { return text; }

        void paint (juce::Graphics&) override;

    private:
        juce::String key, title, text { hint() };
    };
}
