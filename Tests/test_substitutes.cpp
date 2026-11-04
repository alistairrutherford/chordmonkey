// Substitute tests (PLAN.md §2, §6 Phase 9): the substitutes of a workspace chord, written out by
// hand for C major and A minor (names as the slot shows them, in order, with the reason), the
// register kept, the chord itself never offered, and every type on every root in every key
// giving chords that read back.
#include "TestUtil.h"
#include "core/model/ChordSets.h"
#include "core/model/Substitutes.h"
#include <vector>

using namespace chordmonkey;
using testutil::check;

namespace
{
    WorkspaceChord chord (const std::string& numeral, int key)
    {
        WorkspaceChord c;
        int half = 0;
        std::string error;
        parseNumeral (numeral, key, c, half, error);
        return c;
    }

    std::string list (const WorkspaceChord& c, int key, Scale scale)
    {
        std::string s;
        for (const auto& sub : substitutesFor (c, key, scale))
        {
            const auto name = namePatternSlot (makePatternSlot (sub.chord), key, scale).text();
            s += (s.empty() ? "" : ", ") + name + " (" + sub.reason + ")";
        }
        return s;
    }

    void expect (const char* numeral, int key, Scale scale, const std::string& want)
    {
        const auto got = list (chord (numeral, key), key, scale);
        check (got == want, std::string (numeral) + " in " + (key == 0 ? "C major" : "A minor") + ": " + got + (got == want ? "" : "\n        want: " + want));
    }
}

static void testByHand()
{
    std::cout << "== C major ==\n";
    expect ("I", 0, Scale::Major, "Am (a 3rd below), Em (a 3rd above), Cm (parallel minor), Csus2 (sus2), Csus4 (sus4), Cadd9 (add the 9th), Cmaj7 (add the 7th)");
    expect ("ii", 0, Scale::Major, "Bdim (a 3rd below), F (a 3rd above), D (parallel major), Dsus2 (sus2), Dsus4 (sus4), Dmadd9 (add the 9th), Dm7 (add the 7th)");
    expect ("IV", 0, Scale::Major, "Dm (a 3rd below), Am (a 3rd above), Fm (parallel minor), Fsus2 (sus2), Fsus4 (sus4), Fadd9 (add the 9th), Fmaj7 (add the 7th)");
    expect ("V", 0, Scale::Major, "Em (a 3rd below), Bdim (a 3rd above), Gm (parallel minor), Gsus2 (sus2), Gsus4 (sus4), Gadd9 (add the 9th), G7 (add the 7th), Db7 (tritone sub)");
    expect ("vi", 0, Scale::Major, "F (a 3rd below), C (a 3rd above), A (parallel major), Asus2 (sus2), Asus4 (sus4), Amadd9 (add the 9th), Am7 (add the 7th)");
    expect ("V7", 0, Scale::Major, "Em7 (a 3rd below), Bm7b5 (a 3rd above), G7sus4 (sus4), G9 (add the 9th), Db7 (tritone sub)");
    expect ("Imaj7", 0, Scale::Major, "Am7 (a 3rd below), Em7 (a 3rd above), Cm7 (parallel minor), Cmaj9 (add the 9th)");
    expect ("ii7", 0, Scale::Major, "Bm7b5 (a 3rd below), Fmaj7 (a 3rd above), Dmaj7 (parallel major), Dm9 (add the 9th)");
    expect ("bVII", 0, Scale::Major, "Bbm (parallel minor), Bbsus2 (sus2), Bbsus4 (sus4), Bbadd9 (add the 9th), Bb7 (add the 7th)");

    std::cout << "== A minor (natural) ==\n";
    expect ("i", 9, Scale::NaturalMinor, "F (a 3rd below), C (a 3rd above), A (parallel major), Asus2 (sus2), Asus4 (sus4), Amadd9 (add the 9th), Am7 (add the 7th)");
    expect ("iv", 9, Scale::NaturalMinor, "Bdim (a 3rd below), F (a 3rd above), D (parallel major), Dsus2 (sus2), Dsus4 (sus4), Dmadd9 (add the 9th), Dm7 (add the 7th)");
    expect ("v", 9, Scale::NaturalMinor, "C (a 3rd below), G (a 3rd above), E (parallel major), Esus2 (sus2), Esus4 (sus4), Emadd9 (add the 9th), Em7 (add the 7th)");
    expect ("V7", 9, Scale::NaturalMinor, "Cmaj7 (a 3rd below), G7 (a 3rd above), E7sus4 (sus4), E9 (add the 9th), Bb7 (tritone sub)");
    expect ("bIII", 9, Scale::NaturalMinor, "Am (a 3rd below), Em (a 3rd above), Cm (parallel minor), Csus2 (sus2), Csus4 (sus4), Cadd9 (add the 9th), Cmaj7 (add the 7th)");
    expect ("bVII", 9, Scale::NaturalMinor, "Em (a 3rd below), Bdim (a 3rd above), Gm (parallel minor), Gsus2 (sus2), Gsus4 (sus4), Gadd9 (add the 9th), G7 (add the 7th)");
}

static void testProperties()
{
    std::cout << "== every type, root and key ==\n";
    int chords = 0, offered = 0, bad = 0;
    std::string first;
    for (int key = 0; key < 12; ++key)
        for (int si = 0; si < numScales; ++si)
            for (int type = 0; type < numChordTypes; ++type)
                for (int root = 0; root < 12; ++root)
                {
                    const ChordTones t = chordTypeTones (type);
                    std::array<int, maxWorkspaceTones> tones {};
                    for (int i = 0; i < t.size; ++i)
                        tones[(size_t) i] = t.tones[(size_t) i];
                    const auto c = makeWorkspaceChord (root, tones.data(), t.size);
                    ++chords;
                    const auto subs = substitutesFor (c, key, (Scale) si);
                    offered += (int) subs.size();
                    for (size_t i = 0; i < subs.size(); ++i)
                    {
                        const auto& s = subs[i].chord;
                        // A typed chord, not the chord itself or one listed before, its root
                        // within a tritone of the old one's, and its root tone at C2 + root
                        // moved by that.
                        bool ok = ! s.empty() && chordTypeForMask (s.relativeMask()) >= 0
                               && ! (s.root == c.root && s.relativeMask() == c.relativeMask());
                        for (size_t j = 0; j < i; ++j)
                            ok = ok && ! (subs[j].chord.root == s.root && subs[j].chord.relativeMask() == s.relativeMask());
                        const int move = ((s.root - c.root) % 12 + 18) % 12 - 6;
                        bool rootAt = false;
                        for (int k = 0; k < s.size; ++k)
                            rootAt = rootAt || s.root + s.tones[(size_t) k] == c.root + move;
                        ok = ok && rootAt;
                        if (! ok)
                        {
                            ++bad;
                            if (first.empty())
                                first = std::string (chordType (type).name) + " on " + std::to_string (root) + " in key " + std::to_string (key) + ": " + subs[i].reason;
                        }
                    }
                }
    check (bad == 0, std::to_string (offered) + " substitutes for " + std::to_string (chords) + " chords (27 types × 12 roots × 12 keys × "
                         + std::to_string (numScales) + " scales): typed, new, listed once, the root moved by at most a tritone in the same register"
                         + (first.empty() ? "" : " (first failure: " + first + ")"));

    // Tones that aren't a type: none.
    const int odd[] = { 0, 1, 6 };
    check (substitutesFor (makeWorkspaceChord (0, odd, 3), 0, Scale::Major).empty() && substitutesFor (WorkspaceChord {}, 0, Scale::Major).empty(),
           "a chord that isn't one of the types (C Db F#), or no chord, gets none");
}

int main()
{
    testByHand();
    testProperties();
    return testutil::finish ("test_substitutes");
}
