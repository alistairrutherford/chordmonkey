// Chord sets (PLAN.md §3.5, Phase 5): the factory chord sets (ChordSetLibrary.h) and the user's,
// JSON files in ~/Music/ChordMonkey/ChordSets/<Category>/<Name>.json:
//
//   { "name": "Verse", "category": "User", "progression": "vi IV I V", "scale": "Major", "style": "Eighths" }
//
// A set is numerals relative to the Key, so it loads into a pattern in any key; loading can also
// apply its suggested Scale and Style. Saving writes a pattern as numerals in the pattern's own
// key, with the current Scale and Style as the suggestions. The list has the factory sets first
// (by category), then the files by category and name. Message thread only.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace chordmonkey
{
    class ChordMonkeyProcessor;

    class ChordSetManager
    {
    public:
        struct Entry
        {
            juce::String category, name, progression;
            juce::String scale, style;    // suggestions, by name ("" for none)
            int factory = -1;             // index into factoryChordSets(), or -1 for a file
            juce::File file;
        };

        explicit ChordSetManager (ChordMonkeyProcessor& p) : processor (p) {}

        static juce::File userFolder();
        static juce::String fileExtension() { return ".json"; }

        void rescan();
        const juce::Array<Entry>& getEntries();

        // Writes entry `index` into pattern `pattern` in the current Key; with `useSuggestions`,
        // also sets Scale and Style to the set's (as one gesture each).
        bool load (int index, int pattern, bool useSuggestions);

        // Saves pattern `pattern` as a chord set file (a new one, or over the one with that name).
        juce::Result save (const juce::String& name, int pattern, const juce::String& category = "User");

        // A chord set file, or false.
        static bool readFile (const juce::File& file, Entry& out);

    private:
        ChordMonkeyProcessor& processor;
        juce::Array<Entry> entries;
        bool scanned = false;
    };
}
