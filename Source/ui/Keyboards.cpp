#include "Keyboards.h"
#include "Theme.h"
#include "../core/theory/ChordNamer.h"
#include "../core/theory/RomanNumeral.h"

namespace chordmonkey::ui
{
    //==========================================================================
    bool PianoLayout::isBlack (int note) noexcept { return keys::whiteLetter (note) < 0; }

    int PianoLayout::whiteCount() const noexcept
    {
        int n = 0;
        for (int k = lo; k <= hi; ++k)
            n += isBlack (k) ? 0 : 1;
        return juce::jmax (1, n);
    }

    juce::Rectangle<float> PianoLayout::keyBounds (int note, float blackHeight) const
    {
        const float ww = area.getWidth() / (float) whiteCount();
        int whitesBefore = 0;
        for (int k = lo; k < note; ++k)
            whitesBefore += isBlack (k) ? 0 : 1;
        if (! isBlack (note))
            return { area.getX() + ww * (float) whitesBefore, area.getY(), ww, area.getHeight() };
        const float bw = ww * 0.62f;
        return { area.getX() + ww * (float) whitesBefore - bw * 0.5f, area.getY(), bw, area.getHeight() * blackHeight };
    }

    int PianoLayout::noteAt (juce::Point<float> p, float blackHeight) const
    {
        for (int k = lo; k <= hi; ++k)
            if (isBlack (k) && keyBounds (k, blackHeight).contains (p))
                return k;
        for (int k = lo; k <= hi; ++k)
            if (! isBlack (k) && keyBounds (k, blackHeight).contains (p))
                return k;
        return -1;
    }

    //==========================================================================
    void PlayingKeys::press (int note, int velocity)
    {
        if (note == held)
            return;
        release();
        if (note < 0 || note > 127)
            return;
        processor.pushUiNote (note, velocity, true);
        held = note;
    }

    void PlayingKeys::release()
    {
        if (held >= 0)
            processor.pushUiNote (held, 0, false);
        held = -1;
    }

    //==========================================================================
    ChordPads::ChordPads (ChordMonkeyProcessor& p) : processor (p), playing (p)
    {
        setTooltip ("The chords: one per white key and, above them, the black keys' borrowed chords (in any octave). Click or drag across to play");
    }

    void ChordPads::refresh (const UiModel& m)
    {
        std::array<bool, numChordSlots> now {};
        for (int n = 0; n < 128; ++n)
            if (const int slot = KeyRouter::slotOf (n); slot >= 0 && (processor.isKeyDown (n) || processor.isPerforming (n)))
                now[(size_t) slot] = true;

        const bool changed = now != active || ! (m.display == model.display);
        model = m;
        active = now;
        if (changed)
            repaint();
    }

    juce::Rectangle<float> ChordPads::padBounds (int slot) const
    {
        // The black keys' row on top, its pads centred over the gaps between the white ones,
        // as on a piano: C# over C|D, D# over D|E, F# over F|G, G# over G|A, A# over A|B.
        constexpr float gap = 6.0f;
        constexpr int whiteBefore[numBlackSlots] = { 0, 1, 3, 4, 5 };
        const float w = ((float) getWidth() - gap * (numWhiteSlots - 1)) / (float) numWhiteSlots;
        const float blackHeight = std::round ((float) getHeight() * 0.36f);
        if (slot >= numWhiteSlots && slot < numChordSlots)
        {
            const float centre = (w + gap) * (float) (whiteBefore[slot - numWhiteSlots] + 1) - gap * 0.5f;
            const float bw = w * 0.7f;
            return { centre - bw * 0.5f, 0.0f, bw, blackHeight };
        }
        return { (w + gap) * (float) slot, blackHeight + gap, w, (float) getHeight() - blackHeight - gap };
    }

    int ChordPads::slotAt (juce::Point<float> p) const
    {
        for (int slot = 0; slot < numChordSlots; ++slot)
            if (padBounds (slot).contains (p))
                return slot;
        return -1;
    }

    void ChordPads::paint (juce::Graphics& g)
    {
        const auto& d = model.display;
        for (int slot = 0; slot < numChordSlots; ++slot)
        {
            const auto r = padBounds (slot);
            const bool lit = active[(size_t) slot];
            const bool black = slot >= numWhiteSlots;
            g.setColour (lit ? colours::chord : (black ? colours::panel : colours::raised));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (lit ? colours::chord.brighter (0.3f) : colours::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

            static constexpr const char* letters[numChordSlots] = { "C", "D", "E", "F", "G", "A", "B", "C#", "D#", "F#", "G#", "A#" };
            const auto ink = lit ? colours::background : colours::text;
            g.setColour (lit ? colours::background.withAlpha (0.6f) : colours::faint);
            g.setFont (font (black ? 10.0f : 11.0f, true));
            g.drawText (letters[slot], r.reduced (black ? 7.0f : 9.0f, black ? 5.0f : 7.0f).removeFromTop (14.0f),
                        juce::Justification::topLeft);

            // Black pads are shorter: their numeral and name sit lower, under the key letter, and
            // above the last-played stripe.
            const auto& chord = d.chords[(size_t) slot];
            auto text = black ? r.reduced (4.0f, 6.0f).withTrimmedTop (r.getHeight() * 0.14f).withTrimmedBottom (5.0f) : r.reduced (6.0f, 10.0f);
            const float h = text.getHeight();
            g.setColour (ink);
            g.setFont (font (black ? juce::jlimit (13.0f, 22.0f, h * 0.34f) : juce::jlimit (16.0f, 28.0f, h * 0.24f), true));
            g.drawFittedText (juce::String (romanNumeral (chord, d.context.scale)),
                              text.removeFromTop (h * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1);
            g.setColour (lit ? ink : colours::dim);
            g.setFont (font (black ? juce::jlimit (10.5f, 14.0f, h * 0.2f) : juce::jlimit (12.0f, 16.0f, h * 0.14f)));
            g.drawFittedText (juce::String (nameSlot (chord, d.context).text()), text.withTrimmedTop (black ? 2.0f : 4.0f).toNearestInt(),
                              juce::Justification::centredTop, 1, 0.7f);

            if (slot == d.lastSlot)
            {
                g.setColour (lit ? colours::background : colours::chord);
                g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.25f, 0.0f).removeFromBottom (black ? 3.0f : 4.0f).translated (0.0f, black ? -3.0f : -6.0f), 2.0f);
            }
        }
    }

    void ChordPads::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu())
            mouseDrag (e);
    }

    void ChordPads::mouseDrag (const juce::MouseEvent& e)
    {
        if (const int slot = slotAt (e.position); slot >= 0)
            playing.press (KeyRouter::keyForSlot (slot), 100);
    }

    void ChordPads::mouseUp (const juce::MouseEvent&) { playing.release(); }

    //==========================================================================
    OutputKeyboard::OutputKeyboard (ChordMonkeyProcessor& p) : processor (p), playing (p)
    {
        layout.lo = 21;
        layout.hi = 108;
        labelFont = font (9.0f);
        setTooltip ("Output: the notes ChordMonkey plays (red) and the keys held (outlined). Click to play.");
    }

    void OutputKeyboard::refresh (const UiModel&)
    {
        std::array<bool, 128> s {}, k {};
        for (int n = 0; n < 128; ++n)
        {
            s[(size_t) n] = processor.isNoteSounding (n);
            k[(size_t) n] = processor.isKeyDown (n);
        }
        // Phase 8: only the keys that changed (while chords play, the whole keyboard every frame
        // was most of the editor's CPU).
        layout.area = getLocalBounds().toFloat();
        for (int n = layout.lo; n <= layout.hi; ++n)
            if (s[(size_t) n] != sounding[(size_t) n] || k[(size_t) n] != down[(size_t) n])
                repaint (layout.keyBounds (n).getSmallestIntegerContainer().expanded (1));
        sounding = s;
        down = k;
    }

    void OutputKeyboard::paint (juce::Graphics& g)
    {
        layout.area = getLocalBounds().toFloat();
        const auto clip = g.getClipBounds().toFloat();
        for (int pass = 0; pass < 2; ++pass)
            for (int n = layout.lo; n <= layout.hi; ++n)
            {
                const bool black = PianoLayout::isBlack (n);
                if (black != (pass == 1))
                    continue;
                auto r = layout.keyBounds (n);
                if (! r.intersects (clip))
                    continue;
                if (black)
                {
                    g.setColour (sounding[(size_t) n] ? colours::sounding : colours::blackKey);
                    g.fillRect (r);
                }
                else
                {
                    g.setColour (sounding[(size_t) n] ? colours::sounding : colours::whiteKey);
                    g.fillRect (r.reduced (0.5f, 0.0f));
                    if (n % 12 == 0)
                    {
                        g.setColour (colours::whiteKeyText.withAlpha (0.5f));
                        g.setFont (labelFont);
                        g.drawText ("C" + juce::String (n / 12 - 2), r.removeFromBottom (12.0f), juce::Justification::centred);
                    }
                }
                if (down[(size_t) n])
                {
                    g.setColour (colours::chord);
                    g.drawRect (layout.keyBounds (n).reduced (1.0f), 2.0f);
                }
            }
    }

    void OutputKeyboard::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }

    void OutputKeyboard::mouseDrag (const juce::MouseEvent& e)
    {
        layout.area = getLocalBounds().toFloat();
        const int n = layout.noteAt (e.position);
        const int velocity = juce::jlimit (1, 127, 40 + (int) (87.0f * e.position.y / (float) juce::jmax (1, getHeight())));
        if (n >= 0)
            playing.press (n, velocity);
    }

    void OutputKeyboard::mouseUp (const juce::MouseEvent&) { playing.release(); }
}
