// Workspace model tests (PLAN.md §3.2, §3.5, §7; Phase 1, G3): chord types, absolute chords,
// palette chords copied without loss, inversions and octaves, pattern editing, transposing,
// the saved text, and chord sets as Roman numerals.
//
// Expected notes are written out by hand (MIDI 48 = C2, the chords' default C).
#include "TestUtil.h"
#include "core/model/ChordSets.h"
#include "core/theory/ChordGroups.h"
#include <random>
#include <vector>

using namespace chordmonkey;
using testutil::check;

namespace
{
    std::vector<int> notesOf (const ChordNotes& n)
    {
        return std::vector<int> (n.begin(), n.end());
    }

    std::vector<int> tonesOf (const WorkspaceChord& c)
    {
        return std::vector<int> (c.tones.begin(), c.tones.begin() + c.size);
    }

    std::string list (const std::vector<int>& v)
    {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i)
            s += (i ? " " : "") + std::to_string (v[i]);
        return s;
    }

    WorkspaceChord chord (int rootFromC, std::vector<int> tones)
    {
        return makeWorkspaceChord (rootFromC, tones.data(), (int) tones.size());
    }

    WorkspaceChord numeral (const std::string& token, int key = 0)
    {
        WorkspaceChord c;
        int half = 0;
        std::string error;
        parseNumeral (token, key, c, half, error);
        return c;
    }
}

static void testChordTypes()
{
    std::cout << "== chord types ==\n";
    auto tones = [] (int type) { const auto t = chordTypeTones (type); return std::vector<int> (t.tones.begin(), t.tones.begin() + t.size); };
    auto find = [] (const char* name) { for (int i = 0; i < numChordTypes; ++i) if (std::string (chordType (i).name) == name) return i; return -1; };

    check (tones (find ("Major")) == std::vector<int> { 0, 4, 7 }, "Major: 0 4 7");
    check (tones (find ("Sus2")) == std::vector<int> { 0, 2, 7 }, "Sus2: its 9th inside the triad (0 2 7)");
    check (tones (find ("Sus4")) == std::vector<int> { 0, 5, 7 }, "Sus4: 0 5 7");
    check (tones (find ("6")) == std::vector<int> { 0, 4, 7, 9 }, "6: its 13th inside (0 4 7 9)");
    check (tones (find ("9")) == std::vector<int> { 0, 4, 7, 10, 14 }, "9: the 9th an octave up (0 4 7 10 14)");
    check (tones (find ("m11")) == std::vector<int> { 0, 3, 7, 10, 14, 17 }, "m11: 0 3 7 10 14 17");
    check (tones (find ("13")) == std::vector<int> { 0, 4, 7, 10, 14, 21 }, "13: 0 4 7 10 14 21");

    bool distinct = true;
    for (int i = 0; i < numChordTypes; ++i)
        distinct = distinct && chordTypeForMask (chordTypeMask (i)) == i;
    check (distinct, "every type has its own pitch classes (" + std::to_string (numChordTypes) + " types)");
    check (chordTypeForMask (0b000010010001u) == find ("Major") && chordTypeForMask (0b111u) == -1,
           "a mask finds its type; one no type has finds none");
}

static void testChords()
{
    std::cout << "== absolute chords ==\n";
    const auto e3 = chord (16, { 0, 4, 7 });
    check (e3.root == 4 && tonesOf (e3) == std::vector<int> { 12, 16, 19 }, "a root above B2 keeps its octave in the tones (E3: root 4, 12 16 19)");
    const auto messy = chord (0, { 7, 0, 4, 7, 12 });
    check (tonesOf (messy) == std::vector<int> { 0, 4, 7, 12 }, "tones are sorted and a repeated tone is kept once");
    std::vector<int> many { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16 };
    check (chord (0, many).size == maxWorkspaceTones, "at most eight tones");
    check (chord (-3, { 0, 3, 7 }).root == 9 && tonesOf (chord (-3, { 0, 3, 7 })) == std::vector<int> { -12, -9, -5 },
           "a root below C2 (A1) keeps its octave too");
    check (chord (0, { 0, 4, 7 }).pitchClassMask() == 0x91u && chord (9, { 0, 3, 7 }).relativeMask() == 0x89u,
           "pitch-class masks: absolute and relative to the root");

    std::cout << "== palette chords copy without loss ==\n";
    int cases = 0, same = 0, rootSame = 0;
    std::string firstBad;
    for (int key = 0; key < 12; ++key)
        for (int s = 0; s < numScales; ++s)
            for (int g = 0; g < numChordGroups; ++g)
            {
                const ChordContext ctx { key, (Scale) s, 0 };
                const ChordSet set = makeGroupChords ((ChordGroup) g, (Scale) s);
                for (int slot = 0; slot < numChordSlots; ++slot)
                {
                    const auto& cs = set[(size_t) slot];
                    auto want = notesOf (chordNotes (cs, ctx));
                    std::sort (want.begin(), want.end());
                    const auto wc = chordFromSlot (cs, ctx);
                    const auto got = notesOf (slotNotes (makePatternSlot (wc)));
                    ++cases;
                    if (got == want)
                        ++same;
                    else if (firstBad.empty())
                        firstBad = std::string (keyName (key)) + " " + scaleInfo ((Scale) s).name + " " + chordGroupInfo ((ChordGroup) g).name
                                 + " slot " + std::to_string (slot) + ": " + list (want) + " vs " + list (got);
                    rootSame += wc.root == slotRootPc (cs, ctx) ? 1 : 0;
                }
            }
    check (same == cases, std::to_string (same) + " of " + std::to_string (cases)
                              + " palette chords (12 keys × scales × groups × 12 slots) play the same notes from the workspace" + (firstBad.empty() ? "" : " (first: " + firstBad + ")"));
    check (rootSame == cases, "and keep the palette chord's root");
}

static void testSlotNotes()
{
    std::cout << "== inversion and octave ==\n";
    const auto c = chord (0, { 0, 4, 7 });
    check (notesOf (slotNotes (makePatternSlot (c))) == std::vector<int> { 48, 52, 55 }, "C: 48 52 55");
    check (notesOf (slotNotes (makePatternSlot (c, 8, 1))) == std::vector<int> { 52, 55, 60 }, "1st inversion: 52 55 60");
    check (notesOf (slotNotes (makePatternSlot (c, 8, 2))) == std::vector<int> { 55, 60, 64 }, "2nd inversion: 55 60 64");
    check (notesOf (slotNotes (makePatternSlot (c, 8, 3))) == std::vector<int> { 55, 60, 64 }, "a triad's 3rd inversion stays at the 2nd");
    const auto c7 = chord (0, { 0, 4, 7, 11 });
    check (notesOf (slotNotes (makePatternSlot (c7, 8, 3))) == std::vector<int> { 59, 60, 64, 67 }, "Cmaj7 3rd inversion: 59 60 64 67");
    check (notesOf (slotNotes (makePatternSlot (c, 8, 0, 1))) == std::vector<int> { 60, 64, 67 }, "octave +1: 60 64 67");
    check (notesOf (slotNotes (makePatternSlot (c, 8, 0, -2), -1)) == std::vector<int> { 12, 16, 19 }, "octave -2 and Chord Octave -1: 12 16 19");
    check (notesOf (slotNotes (makePatternSlot (chord (0, { 0, 4, 7, 60 }), 8, 0, 2), 2)) == std::vector<int> { 96, 100, 103 },
           "notes above 127 are dropped");
    check (makePatternSlot (c, 99, 9, -9).halfBeats == maxSlotHalfBeats && makePatternSlot (c, 99, 9, -9).inversion == maxSlotInversion
               && makePatternSlot (c, 0, -1, 9).octave == maxSlotOctave && makePatternSlot (c, 0).halfBeats == minSlotHalfBeats,
           "lengths, inversions and octaves are clamped");

    std::cout << "== names ==\n";
    check (namePatternSlot (makePatternSlot (c), 0, Scale::Major).text() == "C", "C");
    check (namePatternSlot (makePatternSlot (c, 8, 1), 0, Scale::Major).text() == "C/E", "C 1st inversion: C/E");
    check (namePatternSlot (makePatternSlot (numeral ("vi7")), 0, Scale::Major).text() == "Am7", "vi7 in C: Am7");
    check (namePatternSlot (makePatternSlot (numeral ("viiø7", 2)), 2, Scale::Major).text() == "C#m7b5", "viiø7 in D: C#m7b5");
    check (namePatternSlot (makePatternSlot (numeral ("bVII", 5)), 5, Scale::Major).text() == "Eb", "bVII in F: Eb");
}

static void testTranspose()
{
    std::cout << "== transposing ==\n";
    check (transposed (chord (0, { 0, 4, 7 }), 7) == chord (7, { 0, 4, 7 }), "C up 7: G (55 59 62)");
    const auto bUp = transposed (chord (11, { 0, 4, 7 }), 1);
    check (bUp.root == 0 && notesOf (slotNotes (makePatternSlot (bUp))) == std::vector<int> { 60, 64, 67 }, "B2 up 1: C3, not C2");
    check (notesOf (slotNotes (makePatternSlot (transposed (chord (0, { 0, 4, 7 }), -5)))) == std::vector<int> { 43, 47, 50 }, "C down 5: G1");

    bool inRange = true;
    auto w = chord (0, { 0, 4, 7, 11 });
    for (int i = 0; i < 40; ++i)
    {
        w = transposed (w, 7);
        inRange = inRange && w.root + w.tones[0] >= -12 && w.root + w.tones[0] <= 23;
    }
    for (int i = 0; i < 40; ++i)
    {
        w = transposed (w, -5);
        inRange = inRange && w.root + w.tones[0] >= -12 && w.root + w.tones[0] <= 23;
    }
    check (inRange && w.relativeMask() == chord (0, { 0, 4, 7, 11 }).relativeMask(), "80 transpositions stay within C1 … B3 and keep the shape");

    Pattern p;
    std::string error;
    parseProgression ("I V vi IV", 0, p, error);
    Pattern g = p;
    g.transposeToKey (7);
    check (g.key == 7 && g.slots[0].chord.root == 7 && g.slots[0].chord.tones[0] == -12, "C to G: down a 4th (the I is G1)");
    Pattern fs = p;
    fs.transposeToKey (6);
    check (fs.key == 6 && fs.slots[0].chord.root == 6 && fs.slots[0].chord.tones[0] == 0, "C to F#: up a tritone");
    g.transposeToKey (0);
    check (g == p, "G back to C (up a 4th): the same pattern");
    check (g.slots[4] == PatternSlot {}, "empty slots stay empty");
}

static void testPatternEditing()
{
    std::cout << "== pattern editing ==\n";
    const auto C = makePatternSlot (chord (0, { 0, 4, 7 }));
    const auto F = makePatternSlot (chord (5, { 0, 4, 7 }), 4);
    const auto G = makePatternSlot (chord (7, { 0, 4, 7 }), 2);
    const auto Am = makePatternSlot (chord (9, { 0, 3, 7 }));
    auto roots = [] (const Pattern& p) { std::vector<int> r; for (int i = 0; i < p.size(); ++i) r.push_back (p.slots[(size_t) i].chord.root); return r; };

    Pattern p;
    check (p.empty() && p.size() == 0 && p.totalHalfBeats() == 0, "a new pattern is empty");
    check (! p.set (1, C) && p.set (0, C) && p.set (1, G) && p.size() == 2, "set appends at size(), not beyond");
    check (! p.set (2, PatternSlot {}), "set won't put an empty slot in (that's remove)");
    check (p.insert (1, Am) && roots (p) == std::vector<int> { 0, 9, 7 }, "insert in the middle: C Am G");
    check (p.insert (3, F) && roots (p) == std::vector<int> { 0, 9, 7, 5 } && p.totalHalfBeats() == 8 + 8 + 2 + 4, "insert at the end; lengths add up");
    check (p.move (0, 3) && roots (p) == std::vector<int> { 9, 7, 5, 0 }, "move first to last: Am G F C");
    check (p.move (2, 0) && roots (p) == std::vector<int> { 5, 9, 7, 0 }, "move third to first: F Am G C");
    check (! p.move (0, 4) && ! p.move (-1, 0), "moves outside the pattern fail");
    check (p.remove (1) && roots (p) == std::vector<int> { 5, 7, 0 } && p.slots[3] == PatternSlot {}, "remove closes the gap");
    check (! p.remove (3), "removing an empty slot fails");
    while (! p.full())
        p.insert (0, C);
    check (p.size() == numPatternSlots && ! p.insert (0, G) && ! p.set (numPatternSlots, G), "a full pattern takes no more");
    check (p.set (2, G) && p.slots[2] == G, "but a slot can be replaced");
    p.clear();
    check (p == Pattern {}, "clear");
}

static void testText()
{
    std::cout << "== saved text ==\n";
    Pattern p;
    std::string error;
    parseProgression ("I V vi IV:2", 0, p, error);
    p.slots[1].inversion = 2;
    p.slots[2].octave = -1;
    check (patternToText (p) == "0|0:0.4.7:8:0:0 7:0.4.7:8:2:0 9:0.3.7:8:0:-1 5:0.4.7:4:0:0", "the documented form: " + patternToText (p));

    Pattern back;
    check (patternFromText (patternToText (p), back) && back == p, "round trip");
    check (patternFromText ("5|", back) && back.empty() && back.key == 5, "an empty pattern keeps its key");
    check (! patternFromText ("", back) && ! patternFromText ("x|0:0.4.7:8:0:0", back), "text without a key is refused");
    check (patternFromText ("2|0:0.4.7:8:0:0 junk 7:0.4:99:9:-9 9::8:0:0 4:0.3.7:8:0", back) && back.size() == 2
               && back.slots[1].halfBeats == maxSlotHalfBeats && back.slots[1].inversion == maxSlotInversion && back.slots[1].octave == minSlotOctave,
           "damaged slots are skipped and values clamped (" + std::to_string (back.size()) + " slots)");
    check (patternFromText ("0|" + std::string (100, ' ') + "0:0:8:0:0", back) && back.size() == 1, "extra spaces are harmless");

    std::mt19937 rng (7);
    int same = 0;
    const int runs = 2000;
    for (int r = 0; r < runs; ++r)
    {
        Pattern q;
        q.key = (int8_t) (rng() % 12);
        const int n = (int) (rng() % (numPatternSlots + 1));
        for (int i = 0; i < n; ++i)
        {
            std::vector<int> tones;
            const int count = 1 + (int) (rng() % maxWorkspaceTones);
            for (int t = 0; t < count; ++t)
                tones.push_back ((int) (rng() % 61) - 24);
            q.slots[(size_t) i] = makePatternSlot (chord ((int) (rng() % 36) - 12, tones), 1 + (int) (rng() % 16),
                                                   (int) (rng() % 4), (int) (rng() % 5) - 2);
        }
        Pattern b;
        same += patternFromText (patternToText (q), b) && b == q ? 1 : 0;
    }
    check (same == runs, std::to_string (same) + " of " + std::to_string (runs) + " random patterns round trip");

    Workspace ws;
    ws.patterns[2] = p;
    Workspace copy = ws;
    check (copy == ws && copy != Workspace {} && std::string (patternName (2)) == "C", "workspaces compare; pattern 2 is C");
}

static void testNumerals()
{
    std::cout << "== numerals ==\n";
    struct Case { const char* token; int key; int root; std::vector<int> tones; int half; };
    const Case cases[] = {
        { "I",          0, 0,  { 0, 4, 7 },             8 },
        { "V",          0, 7,  { 0, 4, 7 },             8 },
        { "V",          9, 4,  { 12, 16, 19 },          8 },     // E3 in A, as the palette's G key
        { "vi",         0, 9,  { 0, 3, 7 },             8 },
        { "ii7",        0, 2,  { 0, 3, 7, 10 },         8 },
        { "V7:2",       0, 7,  { 0, 4, 7, 10 },         4 },
        { "IVmaj7:1.5", 0, 5,  { 0, 4, 7, 11 },         3 },
        { "imaj7",      9, 9,  { 0, 3, 7, 11 },         8 },
        { "viiø7",      0, 11, { 0, 3, 6, 10 },         8 },
        { "viim7b5",    0, 11, { 0, 3, 6, 10 },         8 },
        { "vii°7",      0, 11, { 0, 3, 6, 9 },          8 },
        { "viio",       0, 11, { 0, 3, 6 },             8 },
        { "#iv°",       0, 6,  { 0, 3, 6 },             8 },
        { "bVII",       0, 10, { 0, 4, 7 },             8 },
        { "bIII+",      0, 3,  { 0, 4, 8 },             8 },
        { "IVsus2",     0, 5,  { 0, 2, 7 },             8 },
        { "Vsus4:0.5",  0, 7,  { 0, 5, 7 },             1 },
        { "V7sus4",     0, 7,  { 0, 5, 7, 10 },         8 },
        { "bVI9",       0, 8,  { 0, 4, 7, 10, 14 },     8 },
        { "ii11",       0, 2,  { 0, 3, 7, 10, 14, 17 }, 8 },
        { "I6:8",       0, 0,  { 0, 4, 7, 9 },          16 },
        { "V(0.4.7.10.13)", 0, 7, { 0, 4, 7, 10, 13 },  8 },
        { "I(-12.0.4.7)",   0, 0, { -12, 0, 4, 7 },     8 },
    };
    for (const auto& c : cases)
    {
        WorkspaceChord ch;
        int half = 0;
        std::string error;
        const bool ok = parseNumeral (c.token, c.key, ch, half, error);
        check (ok && ch.root == c.root && tonesOf (ch) == c.tones && half == c.half,
               std::string (c.token) + " in " + keyName (c.key) + ": root " + std::to_string (ch.root) + ", " + list (tonesOf (ch)) + ", " + std::to_string (half) + " half-beats" + (ok ? "" : " (" + error + ")"));
    }

    const char* bad[] = { "X", "", "Vfoo", "I:9", "I:0", "I:x", "V(0.4", "V7(0.4.7)", "Ii", "V(0.1.2.3.4.5.6.7.8)" };
    int refused = 0;
    std::string firstAccepted;
    for (const char* b : bad)
    {
        WorkspaceChord ch;
        int half = 0;
        std::string error;
        if (! parseNumeral (b, 0, ch, half, error) && ! error.empty())
            ++refused;
        else if (firstAccepted.empty())
            firstAccepted = b;
    }
    check (refused == (int) std::size (bad), "bad tokens are refused with a reason" + (firstAccepted.empty() ? std::string() : " (accepted: \"" + firstAccepted + "\")"));

    std::cout << "== numerals round trip ==\n";
    int same = 0, total = 0;
    std::string first;
    for (int key = 0; key < 12; ++key)
        for (int offset = 0; offset < 12; ++offset)
            for (int type = 0; type < numChordTypes; ++type)
            {
                std::array<int, maxWorkspaceTones> t {};
                const ChordTones ct = chordTypeTones (type);
                for (int i = 0; i < ct.size; ++i)
                    t[(size_t) i] = ct.tones[(size_t) i];
                const auto c = makeWorkspaceChord (key + offset, t.data(), ct.size);
                const auto text = numeralFor (c, key);
                const bool explicitTones = text.find ('(') != std::string::npos;
                ++total;
                if (numeral (text, key) == c && ! explicitTones)
                    ++same;
                else if (first.empty())
                    first = text;
            }
    check (same == total, std::to_string (same) + " of " + std::to_string (total) + " typed chords (12 keys × 12 roots × every type) write a numeral that reads back"
                              + (first.empty() ? "" : " (first failure: " + first + ")"));

    std::mt19937 rng (11);
    int sameRandom = 0;
    for (int r = 0; r < 2000; ++r)
    {
        std::vector<int> tones;
        const int count = 1 + (int) (rng() % maxWorkspaceTones);
        for (int i = 0; i < count; ++i)
            tones.push_back ((int) (rng() % 49) - 12);
        const int key = (int) (rng() % 12);
        const auto c = chord ((int) (rng() % 24), tones);
        sameRandom += numeral (numeralFor (c, key), key) == c ? 1 : 0;
    }
    check (sameRandom == 2000, std::to_string (sameRandom) + " of 2000 random chords round trip (any shape, any octave)");

    std::cout << "== palette chords as numerals ==\n";
    auto row = [] (ChordGroup g, Scale s, int key, int from, int to)
    {
        const ChordSet set = makeGroupChords (g, s);
        std::string out;
        for (int i = from; i < to; ++i)
            out += (i > from ? " " : "") + numeralFor (chordFromSlot (set[(size_t) i], { key, s, 0 }), key);
        return out;
    };
    check (row (ChordGroup::Triads, Scale::Major, 0, 0, 7) == "I ii iii IV V vi vii°", "C major triads: " + row (ChordGroup::Triads, Scale::Major, 0, 0, 7));
    check (row (ChordGroup::Sevenths, Scale::Major, 4, 0, 7) == "Imaj7 ii7 iii7 IVmaj7 V7 vi7 viiø7", "E major sevenths: " + row (ChordGroup::Sevenths, Scale::Major, 4, 0, 7));
    check (row (ChordGroup::Triads, Scale::NaturalMinor, 9, 0, 7) == "i ii° bIII iv V bVI bVII", "A minor triads: " + row (ChordGroup::Triads, Scale::NaturalMinor, 9, 0, 7));
    check (row (ChordGroup::Triads, Scale::Major, 0, numWhiteSlots, numChordSlots) == "bII bIII #iv° bVI bVII", "the black keys' borrowed chords: " + row (ChordGroup::Triads, Scale::Major, 0, numWhiteSlots, numChordSlots));

    std::cout << "== progressions ==\n";
    const char* progs[] = { "I V vi IV", "ii7:2 V7:2 Imaj7:8", "i bVI bIII bVII", "I:0.5 IV:1.5 V(0.7.16) vi" };
    for (const char* text : progs)
    {
        Pattern p;
        std::string error;
        const bool ok = parseProgression (text, 3, p, error);
        check (ok && formatProgression (p) == text && p.key == 3, std::string ("\"") + text + "\" in Eb reads and writes back" + (ok ? "" : " (" + error + ")"));
    }
    Pattern p;
    std::string error;
    check (! parseProgression ("I I I I I I I I I", 0, p, error) && ! error.empty(), "nine chords are refused: " + error);
    check (! parseProgression ("   ", 0, p, error), "no chords are refused");
    check (! parseProgression ("I V oops IV", 0, p, error) && error.find ("oops") != std::string::npos, "the bad token is named: " + error);
    parseProgression ("I V vi IV", 0, p, error);
    check (p.size() == 4 && p.totalHalfBeats() == 32 && p.slots[2].chord == chord (9, { 0, 3, 7 }), "I V vi IV in C: four bars, Am third");
}

int main()
{
    testChordTypes();
    testChords();
    testSlotNotes();
    testTranspose();
    testPatternEditing();
    testText();
    testNumerals();
    return testutil::finish ("test_workspace");
}
