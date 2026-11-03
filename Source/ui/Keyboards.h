// The editor's playable surfaces (ChordChimp's PLAN.md §5). Each plays through the engine like host MIDI
// input (ChordMonkeyProcessor::pushUiNote), so clicking is the same as pressing a key.
//
//   ChordPads       seven pads, one per white-key letter (C … B), and above them five smaller
//                   ones over the gaps, one per black key (C# … A#: the borrowed chords, Phase
//                   7): the chord's Roman numeral and name, the key letter in a corner, lit
//                   while that chord plays, with a stripe on the last one played. Click (or drag
//                   across) to play.
//   OutputKeyboard  88 keys: the notes ChordMonkey sends (red) and the keys held (outlined).
//                   Hideable.
#pragma once

#include "../plugin/ChordMonkeyProcessor.h"
#include "ParamControl.h"

namespace chordmonkey::ui
{
    // What the editor shows, refreshed on its timer.
    struct UiModel
    {
        ChordDisplay display;             // the chords, their context, last slot
        EngineSettings settings;          // the parameters now
        params::AnalyseMode analyse = params::AnalyseMode::Off;
        KeyGuess guess;                   // the key heard in Analyse mode
        Workspace workspace;              // Phase 6: the patterns
        int playingPattern = 0, playingSlot = -1;
        params::DetectMode detect = params::DetectMode::Off;   // Phase 8
        int heardChords = 0;              // chords heard in this capture
    };

    // Piano geometry for notes lo..hi (both white keys) across a width.
    struct PianoLayout
    {
        int lo = 60, hi = 84;
        juce::Rectangle<float> area;

        static bool isBlack (int note) noexcept;
        int whiteCount() const noexcept;
        juce::Rectangle<float> keyBounds (int note, float blackHeight = 0.62f) const;
        int noteAt (juce::Point<float> p, float blackHeight = 0.62f) const;   // -1 if none
    };

    // Plays a key while the mouse holds it (and a drag moves along the keys).
    class PlayingKeys
    {
    public:
        // `palette`: the notes play their key's palette chord whatever Bind is (the chord pads).
        explicit PlayingKeys (ChordMonkeyProcessor& p, bool palette = false) : processor (p), toPalette (palette) {}
        ~PlayingKeys() { release(); }
        void press (int note, int velocity);
        void release();

    private:
        ChordMonkeyProcessor& processor;
        bool toPalette = false;
        int held = -1;
    };

    class ChordPads : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        explicit ChordPads (ChordMonkeyProcessor&);
        void refresh (const UiModel&);

        juce::Rectangle<float> padBounds (int slot) const;
        int slotAt (juce::Point<float>) const;   // -1 if none

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        // Right-click's "Add to the pattern" (Phase 6): the pad's slot (0..11).
        std::function<void (int)> onAddToPattern;

    private:
        ChordMonkeyProcessor& processor;
        PlayingKeys playing;
        UiModel model;
        std::array<bool, numChordSlots> active {};
        int lastPad = -1;                 // the pad pressed or played last (what a drag out takes)
    };

    class OutputKeyboard : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        explicit OutputKeyboard (ChordMonkeyProcessor&);
        void refresh (const UiModel&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordMonkeyProcessor& processor;
        PlayingKeys playing;
        PianoLayout layout;
        std::array<bool, 128> sounding {}, down {};
        juce::Font labelFont;             // made once: a new Font on every paint costs a typeface lookup
    };
}
