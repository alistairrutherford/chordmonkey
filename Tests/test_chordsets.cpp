// Factory chord set tests (PLAN.md §3.5, §7; Phase 5, G7): every set reads in all 12 keys (1-8
// chords, whole bars), its text is the canonical form formatProgression writes back, names and
// progressions are unique, the categories are the six, the suggested styles exist, and a few
// sets by hand.
#include "TestUtil.h"
#include "core/engine/Style.h"
#include "core/model/ChordSetLibrary.h"
#include "core/model/ChordSets.h"
#include <set>
#include <string>

using namespace chordmonkey;
using testutil::check;

namespace
{
    std::string names (const Pattern& p, int key, Scale scale)
    {
        std::string s;
        for (int i = 0; i < p.size(); ++i)
            s += (i ? " " : "") + namePatternSlot (p.slots[(size_t) i], key, scale).text();
        return s;
    }
}

static void testLibrary()
{
    std::cout << "== the factory chord sets ==\n";
    const auto& sets = factoryChordSets();
    std::set<std::string> seenNames, seenProgressions;
    int readable = 0, wholeBars = 0, canonical = 0, styles = 0, categories = 0;
    std::string firstBad;
    std::array<int, numChordSetCategories> perCategory {};
    for (const auto& s : sets)
    {
        seenNames.insert (s.name);
        seenProgressions.insert (s.progression);
        for (int c = 0; c < numChordSetCategories; ++c)
            if (std::string (s.category) == chordSetCategory (c))
            {
                ++perCategory[(size_t) c];
                ++categories;
            }
        for (int i = 0; i < numStyles; ++i)
            if (std::string (styleInfo (i).name) == s.style)
            {
                ++styles;
                break;
            }

        int keysOk = 0;
        Pattern p;
        for (int key = 0; key < 12; ++key)
        {
            std::string error;
            if (parseProgression (s.progression, key, p, error) && p.size() >= 1 && p.size() <= numPatternSlots)
                ++keysOk;
            else if (firstBad.empty())
                firstBad = std::string (s.name) + " in " + keyName (key) + ": " + error;
        }
        readable += keysOk == 12 ? 1 : 0;
        wholeBars += p.totalHalfBeats() % 8 == 0 ? 1 : 0;
        canonical += formatProgression (p) == s.progression ? 1 : 0;
        if (formatProgression (p) != s.progression && firstBad.empty())
            firstBad = std::string (s.name) + " writes back as \"" + formatProgression (p) + "\"";
    }
    const int n = (int) sets.size();
    check (readable == n, std::to_string (readable) + " of " + std::to_string (n) + " sets read in all 12 keys, 1-8 chords"
                              + (firstBad.empty() ? "" : " (first problem: " + firstBad + ")"));
    check (canonical == n, "every set's text is the form formatProgression writes back (" + std::to_string (canonical) + ")");
    check (wholeBars == n, "every set lasts a whole number of 4/4 bars (" + std::to_string (wholeBars) + ")");
    check ((int) seenNames.size() == n && (int) seenProgressions.size() == n, "names and progressions are unique");
    check (categories == n && std::all_of (perCategory.begin(), perCategory.end(), [] (int c) { return c >= 5; }),
           "every set is in one of the six categories, each with at least five");
    check (styles == n, "every suggested style is a style (" + std::to_string (styles) + ")");
    check (n >= 40, std::to_string (n) + " factory sets (the plan: about 40)");
    check (findChordSet ("Two-Five-One") != nullptr && std::string (findChordSet ("Two-Five-One")->progression) == "ii7 V7 Imaj7:8"
               && findChordSet ("Two-Five") == nullptr && findChordSet ("") == nullptr,
           "findChordSet by exact name");
}

static void testByHand()
{
    std::cout << "== by hand ==\n";
    Pattern p;
    std::string error;
    parseProgression (findChordSet ("Four Chords")->progression, 7, p, error);
    check (names (p, 7, Scale::Major) == "G D Em C", "Four Chords in G: G D Em C (" + names (p, 7, Scale::Major) + ")");
    parseProgression (findChordSet ("Andalusian Descent")->progression, 9, p, error);
    check (names (p, 9, Scale::HarmonicMinor) == "Am G F E", "Andalusian Descent in A: Am G F E (" + names (p, 9, Scale::HarmonicMinor) + ")");
    parseProgression (findChordSet ("Minor Two-Five")->progression, 2, p, error);
    check (names (p, 2, Scale::HarmonicMinor) == "Em7b5 A7 Dm7" && p.totalHalfBeats() == 32,
           "Minor Two-Five in D: Em7b5 A7 Dm7, four bars (" + names (p, 2, Scale::HarmonicMinor) + ")");
    parseProgression (findChordSet ("Phrygian Mystery")->progression, 4, p, error);
    check (names (p, 4, Scale::Phrygian) == "Em F Em F", "Phrygian Mystery in E: Em F Em F (" + names (p, 4, Scale::Phrygian) + ")");
    parseProgression (findChordSet ("Canon Steps")->progression, 0, p, error);
    check (p.size() == 8 && p.totalHalfBeats() == 32, "Canon Steps: eight chords, two beats each");
}

int main()
{
    testLibrary();
    testByHand();
    return testutil::finish ("test_chordsets");
}
