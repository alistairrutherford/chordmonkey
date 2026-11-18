#include "InfoView.h"

namespace chordmonkey::ui
{
    namespace
    {
        struct Info { const char* key; const char* title; const char* text; };

        // One entry per control, display and card. Keep each to a line or two: what it does, and
        // what each of its settings means. UTF-8.
        const Info table[] = {
            // ---- The top bar ----------------------------------------------------------------
            { "enable", "On", "On: keys play chords and the pattern can play. Off: notes pass through unchanged." },
            { "ui.prev", "Previous preset", "Loads the preset before this one." },
            { "ui.next", "Next preset", "Loads the preset after this one." },
            { "ui.presetName", "Presets",
              "The preset loaded. Click to browse and search every preset, factory and your own, and to show their folder." },
            { "ui.save", "Save", "Saves these settings as a preset of your own, with a name and a category." },
            { "ui.export", "Export",
              "Drag into a track: the last bars ChordMonkey played (up to 32), as a MIDI file. Ready once a bar has finished." },
            { "ui.keys", "Keys", "Shows or hides the keyboard along the bottom." },

            // ---- The globals ----------------------------------------------------------------
            { "key", "Key", "The key the chords are in." },
            { "analyse", "Heard",
              "Click to listen: play, or let a clip play in, and ChordMonkey guesses the key (no chords meanwhile). Click the guess to use it." },
            { "scale", "Scale", "The scale the chords are built from: which chord each degree gets." },
            { "chordGroup", "Chord Group", "The chords' shape: triads, sevenths and other stacks, built on each degree of the scale." },
            { "progression", "Progression", "Which chord each white key plays: the degrees in order, or a progression's." },
            { "chordOctave", "Octave", "The register the chords play in, up or down 3 octaves." },

            // ---- The keyboard row -----------------------------------------------------------
            { "bindSource", "Bind",
              "All keys: every key plays its chord. Workspace: an octave from From plays the pattern's slots. Palette: that octave plays the pads. Off: no chords from keys." },
            { "bindStart", "From", "The first key of the Bind zone (Workspace and Palette): the zone is the octave from it." },
            { "keysLock", "Keys-Lock",
              "The keys outside the Bind zone: locked to the scale (mapped, only, or its white keys) or to the chord playing. Off: as played." },
            { "keyOctave", "Key octave",
              "Bind All keys. Ignore: every octave plays the same chord. Follow: the octave you play moves the chord (C2 plays it as written)." },

            // ---- The pads -------------------------------------------------------------------
            { "display.pads", "Chord pads",
              "One chord per white key and, above, the black keys' borrowed chords. Click to play; drag one onto the workspace, or right-click, to add it." },

            // ---- The workspace --------------------------------------------------------------
            { "area.workspace", "Workspace",
              "The pattern's slots: click one to hear it, drag to reorder, right-click for its length, inversion, octave and substitutes." },
            { "pattern", "Pattern", "Which of the four patterns (A to D) is shown and plays. A change while playing waits for the next bar." },
            { "play", "Play", "On: the pattern's chords play in time with the song while the host plays." },
            { "detect", "Detect",
              "Play chords in (keys or a clip) and they become this pattern, with the key heard. Click again to use them; the cross cancels." },
            { "ui.detectCancel", "Cancel", "Stops capturing; the pattern stays as it was." },
            { "ui.chordSets", "Chord sets", "Loads a chord set into the pattern (in the Key), or saves the pattern as one." },
            { "ui.patternMenu", "Pattern menu", "Transposes the pattern to the Key, copies it to another pattern, or clears it." },
            { "ui.dragPattern", "Drag MIDI",
              "Drag the pattern into a track as a MIDI clip: one loop, as it plays (style, voicing, bass, effects). Hold Alt for the chords only." },

            // ---- The effects cards ----------------------------------------------------------
            { "card.strum", "Strum", "The chord's notes start one after another instead of together." },
            { "strum", "Strum direction",
              "Off: together. Up: lowest first. Down: highest first. Alternating: up, then down. Random: a new order each time." },
            { "strumRate", "Strum rate", "The time between strummed notes, a note value (at the last tempo while stopped)." },
            { "card.velocity", "Velocity", "Sets each note's velocity from a curve across the bar, in place of how hard you play." },
            { "velocity", "Velocity curve",
              "Off: as played. Up, Down and their halves and quarters: across the bar. Afrobeat, House and the rest: accent patterns." },
            { "velocityPeak", "Velocity peak", "The loudest velocity the curve gives (1 to 127)." },
            { "velocityRange", "Velocity range", "How far the curve dips below the peak. 0%: every note at the peak. 100%: the curve spans 1 to the peak." },
            { "card.arp", "Arp", "Plays the chord's notes one at a time, on the song's grid." },
            { "arp", "Arp pattern",
              "The order: Linear, Converge (outside in), Diverge, Stutter (each twice), Zigzag or Boomerang, up or down. Off: no arp." },
            { "arpRate", "Arp rate", "How long each arp step is: a note value, 1/2 to 1/32 (T: triplets)." },
            { "arpRange", "Arp range", "How many octaves the arp climbs (1 to 3)." },
            { "arpGate", "Arp gate", "How much of each step a note sounds. 100%: legato." },
            { "card.voicing", "Voicing", "How the chord is spread, given a bass, stacked and led from one chord to the next." },
            { "voicing", "Voicing",
              "Close: as built. Open: the second-highest note an octave down. Spread: the lowest stays, the rest up. Wide: the 2nd and 4th highest down." },
            { "bass", "Bass", "Adds the chord's root (Root), or its root and fifth (Root + 5th), an octave below the chord." },
            { "stack", "Stack", "Adds every note again an octave up (+1), two up (+2), or both (+1&2)." },
            { "voiceLeading", "Voice leading", "Smooth: each chord takes the inversion nearest the one before, so the voices move little. Off: as written." },
            { "card.style", "Style", "The rhythm the chords play in, with its swing and feel, and Hold." },
            { "style", "Style", "The rhythm the chords play in, bar by bar. Block: held. Any other style replaces Repeat and Step seq." },
            { "swing", "Swing", "Plays the style's off-beat eighths and sixteenths later (50%: a triplet feel)." },
            { "humanize", "Humanize", "Each note up to 15 ms later and up to 12 softer or louder, the same every time the song plays there." },
            { "hold", "Hold",
              "Latch: a chord keeps playing after you let go, until the next chord. Bar: the same, ending at the latest at the next bar line." },
            { "card.repeat", "Repeat", "Plays the whole chord again and again on the grid while it is held." },
            { "repeat", "Repeat on", "Turns Repeat on or off." },
            { "repeatRate", "Repeat rate", "How often the chord repeats: a note value, 1/2 to 1/32 (T: triplets)." },
            { "repeatGate", "Repeat gate", "How much of each repeat the chord sounds." },
            { "card.seq", "Step seq", "Gates the chord with a 16-step pattern: it sounds only on the lit steps." },
            { "seq", "Step seq on", "Turns the step sequencer on or off." },
            { "seqPattern", "Seq pattern", "Which of the four fixed 16-step patterns gates the chord (shown under the controls)." },
            { "seqSwing", "Seq swing", "Delays every second step. 0%: straight." },
            { "seqGate", "Seq gate", "How much of each step the chord sounds." },
            { "display.steps", "Steps", "The chosen pattern's 16 steps: the chord sounds on the lit ones." },

            // ---- The output keyboard --------------------------------------------------------
            { "display.keyboard", "Output", "The notes ChordMonkey plays (red) and the keys held (outlined). Click to play." },

            // ---- Not on screen --------------------------------------------------------------
            { "testPattern", "Test pattern", "For testing: plays a fixed pattern of chord keys with the host's transport. Set from the host only." },
            { "testWorkspace", "Test workspace", "For testing: loads the test rig's patterns and plays its keys as host MIDI. Set from the host only." },
        };

        const Info* find (const juce::String& key)
        {
            if (key.isEmpty())
                return nullptr;
            for (const auto& i : table)
                if (key == i.key)
                    return &i;
            return nullptr;
        }
    }

    juce::String InfoView::titleFor (const juce::String& key)
    {
        const auto* i = find (key);
        return i != nullptr ? juce::String::fromUTF8 (i->title) : juce::String();
    }

    juce::String InfoView::textFor (const juce::String& key)
    {
        const auto* i = find (key);
        return i != nullptr ? juce::String::fromUTF8 (i->text) : juce::String();
    }

    bool InfoView::has (const juce::String& key) { return find (key) != nullptr; }

    juce::StringArray InfoView::keys()
    {
        juce::StringArray out;
        for (const auto& i : table)
            out.add (i.key);
        return out;
    }

    juce::String InfoView::hint() { return "Point at a control or display to see what it does."; }

    void InfoView::showKey (const juce::String& newKey, const juce::String& live)
    {
        const auto* i = find (newKey);
        const auto newTitle = i != nullptr ? juce::String::fromUTF8 (i->title) : juce::String();
        const auto newText = i == nullptr ? hint() : live.isNotEmpty() ? live : juce::String::fromUTF8 (i->text);
        key = i != nullptr ? newKey : juce::String();
        if (newTitle == title && newText == text)
            return;
        title = newTitle;
        text = newText;
        repaint();
    }

    void InfoView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 6.0f);
        r = r.reduced (14.0f, 8.0f);
        if (title.isNotEmpty())
        {
            g.setColour (colours::text);
            g.setFont (font (13.0f, true));
            g.drawText (title, r.removeFromTop (18.0f), juce::Justification::centredLeft);
        }
        g.setColour (title.isEmpty() ? colours::dim : colours::text.withAlpha (0.85f));
        g.setFont (font (12.5f));
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
}
