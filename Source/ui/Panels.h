// The editor's panels (ChordChimp's PLAN.md §5):
//
//   TopBar         the name · On · the preset browser · Pattern (A-D) · Play · Export (drag out
//                  the last bars played) · Keys (show or hide the output keyboard). Pattern and
//                  Play sit here until the workspace gets its own panel (PLAN.md Phase 6)
//   GlobalRow      Key · Heard · Scale · Chord Group · Progression · Octave · Key octave
//   HeardKey       the Heard box (ChordChimp's docs/KEY_DETECTION_PLAN.md K5): Analyse, then the key heard;
//                  a click on the guess sets Key (and Scale) to it, ✕ stops analysing
//   EffectsPanel   six cards: Strum, Velocity; Arp, Voicing (Stack, Hold, Voice leading);
//                  Repeat and Step seq, each with its on switch in the card's corner, and the
//                  chosen pattern's steps under Step seq
//
// Test pattern (the LiveBridge rig) has no control: Live's device parameters reach it.
#pragma once

#include "Keyboards.h"
#include "PresetBar.h"

namespace chordmonkey::ui
{
    class TopBar : public juce::Component
    {
    public:
        TopBar (ChordMonkeyProcessor&, Bindings&, std::function<void (bool)> showKeyboard);
        void setKeyboardShown (bool shown);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

        PresetBar presets;
        ExportHandle exportHandle;

    private:
        ParamControl enable, pattern, play;
        juce::TextButton keyboardButton { "Keys" };
        juce::Rectangle<int> nameArea;
    };

    // The Analyse control and the key it heard. It follows the Analyse parameter, so a change
    // from the host shows at once.
    class HeardKey : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        HeardKey (ChordMonkeyProcessor&, Bindings&);
        void refresh (const UiModel&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // What a click does: on the box, or on its ✕ (`close`). For tests too.
        void click (bool close);
        const juce::String& getText() const noexcept { return text; }
        juce::Rectangle<int> boxBounds() const noexcept;
        juce::Rectangle<int> closeBounds() const noexcept;   // empty unless analysing
        // Whether `s` fits the box as it is now; the widths the column needs for its shortest
        // and its longest text.
        bool fits (const juce::String& s) const;
        static int minimumWidth();
        static int preferredWidth();

    private:
        void update();
        juce::String chooseText (const juce::String& full, const juce::String& shorter, const juce::String& shortest) const;
        bool analysing() const noexcept { return mode != params::AnalyseMode::Off; }
        bool matchesKey() const noexcept;

        ChordMonkeyProcessor& processor;
        Bindings& bindings;
        params::AnalyseMode mode = params::AnalyseMode::Off;
        KeyGuess guess;
        int key = 0;
        Scale scale = Scale::Major;
        juce::String text;
        bool highlighted = false;     // the guess, confident: accent
        static constexpr int captionHeight = 15, closeWidth = 18;
    };

    class GlobalRow : public juce::Component
    {
    public:
        GlobalRow (ChordMonkeyProcessor&, Bindings&);
        void refresh (const UiModel&);
        void resized() override;

        HeardKey heard;

    private:
        Bindings& bindings;
        ParamControl key, scale, group, progression, octave, keyOctave;
    };

    class EffectsPanel : public juce::Component
    {
    public:
        explicit EffectsPanel (Bindings&);
        void refresh (const UiModel&);
        void paint (juce::Graphics&) override;
        void resized() override;

        // The height the cards need at this width (the editor sizes the chord pads around it).
        static int preferredHeight();

    private:
        struct Card
        {
            juce::String title;
            ParamControl* toggle = nullptr;           // the card's on switch, if it has one
            std::vector<ParamControl*> controls;
            juce::Component* extra = nullptr;         // under the controls (the steps)
            juce::Rectangle<int> bounds;
        };

        ParamControl& add (const juce::String& paramId, const juce::String& label = {});
        void layoutCard (Card&);

        Bindings& bindings;
        juce::OwnedArray<ParamControl> controls;
        std::array<Card, 6> cards;
        StepSeqView steps;

        static constexpr int titleHeight = 22, controlHeight = 40, stepsHeight = 22, gap = 8;
    };
}
