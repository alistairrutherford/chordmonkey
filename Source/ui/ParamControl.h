// Controls bound to host parameters (ChordChimp's PLAN.md §5). A ParamControl builds itself from the
// parameter's type: a choice is a menu, a bool a toggle button, a number a bar slider showing
// the parameter's own text. It can be re-bound to another parameter.
//
// Bindings records every parameter some control has been bound to, so a test can check that
// every feature is reachable from the editor (Tests/Snapshot). StepSeqView shows the chosen
// sequencer pattern's 16 steps.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

namespace chordmonkey::ui
{
    class Bindings
    {
    public:
        explicit Bindings (juce::AudioProcessorValueTreeState& s) : state (s) {}

        juce::AudioProcessorValueTreeState& state;

        void bound (const juce::String& paramId) { ids.insert (paramId); }
        const std::set<juce::String>& boundIds() const noexcept { return ids; }

        juce::RangedAudioParameter* param (const juce::String& id) const { return state.getParameter (id); }
        float value (const juce::String& id) const
        {
            auto* p = state.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        }
        // Sets a parameter from the UI as one host gesture.
        void set (const juce::String& id, float plainValue) const;

    private:
        std::set<juce::String> ids;
    };

    class ParamControl : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        // `label` replaces the parameter's name as the caption; `showLabel` false for a control
        // with its caption elsewhere. A bool's caption is the button text.
        ParamControl (Bindings&, const juce::String& paramId, const juce::String& label = {}, bool showLabel = true);
        ~ParamControl() override;

        void bind (const juce::String& paramId, const juce::String& label = {});
        const juce::String& getParamId() const noexcept { return paramId; }
        void setAccent (juce::Colour c);
        void setCaptionLeft (int width) { captionLeft = width; resized(); }   // caption beside the control

        void resized() override;

    private:
        Bindings& bindings;
        juce::String paramId;
        bool showLabel = true;
        int captionLeft = 0;
        juce::Colour accent;
        juce::Label caption;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    };

    // The 16 steps of the chosen sequencer pattern (read-only: the patterns are fixed).
    class StepSeqView : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        explicit StepSeqView (juce::Colour accent);

        void setSteps (uint16_t mask);    // bit s = step s + 1
        void paint (juce::Graphics&) override;

    private:
        juce::Rectangle<float> stepBounds (int step) const;

        juce::Colour accent;
        juce::Font numberFont;
        uint16_t steps = 0;
    };
}
