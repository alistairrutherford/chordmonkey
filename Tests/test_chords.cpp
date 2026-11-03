// Chord model and chord naming tests (ChordChimp's PLAN.md §3.2, §8).
//
// A reference set of chords (triads to 13ths, altered chords, inversions, drop voicings) is
// built by step edits on a Chord Group's shape (Chordism's Chord Editor operations, applied to
// the slot directly) and must give exactly the expected MIDI notes, name and Roman numeral.
// The expected notes are written out by hand (MIDI 48 = C2, the chords' default C).
//
// Also: names of pitch-class sets (root choice, slash bass), spelling in every key, Roman
// numerals in ChordPrism's style, and the black keys' borrowed chords (Phase 7).
#include "TestUtil.h"
#include "core/theory/Progressions.h"
#include "core/theory/ChordNamer.h"
#include "core/theory/RomanNumeral.h"
#include <algorithm>
#include <sstream>
#include <vector>

using namespace chordmonkey;
using testutil::check;

namespace
{
    std::string list (const std::vector<int>& v)
    {
        std::ostringstream o;
        for (size_t i = 0; i < v.size(); ++i)
            o << (i ? " " : "") << v[i];
        return o.str();
    }

    // One step edit, on a step written as its chord number (1, 3, 5 … 13).
    struct Op
    {
        enum Kind { On, Off, Acc, OctaveOnly, AddOctave } kind;
        int number;
        int value = 0;
    };

    Op on (int n) { return { Op::On, n }; }
    Op off (int n) { return { Op::Off, n }; }
    Op acc (int n, int a) { return { Op::Acc, n, a }; }
    Op only (int n, int octave) { return { Op::OctaveOnly, n, octave }; }   // sound in this octave only
    Op add (int n, int octave) { return { Op::AddOctave, n, octave }; }     // stack another octave

    int stepOf (int number) { return (number - 1) / 2; }

    void apply (ChordSlot& slot, const Op& op)
    {
        Step& step = slot.steps[(size_t) stepOf (op.number)];
        switch (op.kind)
        {
            case Op::On:  step.on = true; break;
            case Op::Off: step.on = false; break;
            case Op::Acc: step.accidental = (int8_t) op.value; break;
            case Op::AddOctave: step.octaveMask = (uint8_t) (step.octaveMask | octaveBit (op.value)); break;
            case Op::OctaveOnly: step.octaveMask = octaveBit (op.value); break;
        }
    }

    struct Reference
    {
        const char* name;               // expected chord name
        int key;
        Scale scale;
        int degree;                     // 0..6; played by that white key (Diatonic progression)
        std::vector<Op> ops;            // on the Triads group's shape
        std::vector<int> notes;         // expected MIDI notes, sorted
        const char* numeral = nullptr;  // expected Roman numeral, where checked
    };

    const Scale Maj = Scale::Major;

    std::vector<Reference> referenceSet()
    {
        return {
            // Triads.
            { "C",        0, Maj, 0, {}, { 48, 52, 55 }, "I" },
            { "Dm",       0, Maj, 1, {}, { 50, 53, 57 }, "ii" },
            { "Bdim",     0, Maj, 6, {}, { 59, 62, 65 }, "vii*" },
            { "Caug",     0, Maj, 0, { acc (5, 1) }, { 48, 52, 56 }, "I+" },
            { "Cm",       0, Maj, 0, { acc (3, -1) }, { 48, 51, 55 }, "i" },
            { "Csus2",    0, Maj, 0, { off (3), on (9), only (9, -1) }, { 48, 50, 55 }, "I" },
            { "Csus4",    0, Maj, 0, { off (3), on (11), only (11, -1) }, { 48, 53, 55 }, "I" },
            { "C5",       0, Maj, 0, { off (3) }, { 48, 55 }, "I" },
            { "Db",       0, Maj, 1, { acc (1, -1), acc (5, -1) }, { 49, 53, 56 }, "bII" },   // Neapolitan
            // Sixths.
            { "C6",       0, Maj, 0, { on (13), only (13, -1) }, { 48, 52, 55, 57 } },
            { "Cm6",      0, Maj, 0, { acc (3, -1), on (13), only (13, -1) }, { 48, 51, 55, 57 } },
            { "C6/9",     0, Maj, 0, { on (13), only (13, -1), on (9) }, { 48, 52, 55, 57, 62 } },
            // Sevenths.
            { "Cmaj7",    0, Maj, 0, { on (7) }, { 48, 52, 55, 59 }, "I" },
            { "C7",       0, Maj, 0, { on (7), acc (7, -1) }, { 48, 52, 55, 58 } },
            { "G7",       0, Maj, 4, { on (7) }, { 55, 59, 62, 65 }, "V" },
            { "Dm7",      0, Maj, 1, { on (7) }, { 50, 53, 57, 60 }, "ii" },
            { "Bm7b5",    0, Maj, 6, { on (7) }, { 59, 62, 65, 69 }, "vii*" },
            { "Bdim7",    0, Maj, 6, { on (7), acc (7, -1) }, { 59, 62, 65, 68 }, "vii*" },
            { "Cm(maj7)", 0, Maj, 0, { acc (3, -1), on (7) }, { 48, 51, 55, 59 } },
            { "C7#5",     0, Maj, 0, { acc (5, 1), on (7), acc (7, -1) }, { 48, 52, 56, 58 }, "I+" },
            { "Cmaj7#5",  0, Maj, 0, { acc (5, 1), on (7) }, { 48, 52, 56, 59 } },
            { "C7sus4",   0, Maj, 0, { off (3), on (11), only (11, -1), on (7), acc (7, -1) }, { 48, 53, 55, 58 } },
            { "C7b5",     0, Maj, 0, { acc (5, -1), on (7), acc (7, -1) }, { 48, 52, 54, 58 } },
            // Ninths.
            { "Cmaj9",    0, Maj, 0, { on (7), on (9) }, { 48, 52, 55, 59, 62 } },
            { "G9",       0, Maj, 4, { on (7), on (9) }, { 55, 59, 62, 65, 69 } },
            { "Dm9",      0, Maj, 1, { on (7), on (9) }, { 50, 53, 57, 60, 64 } },
            { "G7b9",     0, Maj, 4, { on (7), on (9), acc (9, -1) }, { 55, 59, 62, 65, 68 } },
            { "G7#9",     0, Maj, 4, { on (7), on (9), acc (9, 1) }, { 55, 59, 62, 65, 70 } },
            { "Cadd9",    0, Maj, 0, { on (9) }, { 48, 52, 55, 62 } },
            { "Cmadd9",   0, Maj, 0, { acc (3, -1), on (9) }, { 48, 51, 55, 62 } },
            { "G9sus4",   0, Maj, 4, { off (3), on (11), only (11, -1), on (7), on (9) }, { 55, 60, 62, 65, 69 } },
            // Elevenths and thirteenths.
            { "Dm11",     0, Maj, 1, { on (7), on (9), on (11) }, { 50, 53, 57, 60, 64, 67 } },
            { "C9#11",    0, Maj, 0, { on (7), acc (7, -1), on (9), on (11), acc (11, 1) }, { 48, 52, 55, 58, 62, 66 } },
            { "Cmaj7#11", 0, Maj, 0, { on (7), on (11), acc (11, 1) }, { 48, 52, 55, 59, 66 } },
            { "Fmaj7#11", 0, Maj, 3, { on (7), on (11) }, { 53, 57, 60, 64, 71 }, "IV" },
            { "G13",      0, Maj, 4, { on (7), on (9), on (13) }, { 55, 59, 62, 65, 69, 76 } },
            { "Cmaj13",   0, Maj, 0, { on (7), on (9), on (13) }, { 48, 52, 55, 59, 62, 69 } },
            { "Dm13",     0, Maj, 1, { on (7), on (9), on (11), on (13) }, { 50, 53, 57, 60, 64, 67, 71 } },
            // Altered dominants.
            { "G7b9b13",  0, Maj, 4, { on (7), on (9), acc (9, -1), on (13), acc (13, -1) }, { 55, 59, 62, 65, 68, 75 } },
            { "G7#5#9",   0, Maj, 4, { acc (5, 1), on (7), on (9), acc (9, 1) }, { 55, 59, 63, 65, 70 }, "V+" },
            { "C13b9",    0, Maj, 0, { on (7), acc (7, -1), on (9), acc (9, -1), on (13) }, { 48, 52, 55, 58, 61, 69 } },
            { "G7b5",     0, Maj, 4, { acc (5, -1), on (7) }, { 55, 59, 61, 65 } },
            // Inversions.
            { "C/E",      0, Maj, 0, { only (1, 1) }, { 52, 55, 60 }, "I" },
            { "C/G",      0, Maj, 0, { only (1, 1), only (3, 1) }, { 55, 60, 64 }, "I" },
            { "Cmaj7/B",  0, Maj, 0, { on (7), only (7, -1) }, { 47, 48, 52, 55 } },
            { "G7/F",     0, Maj, 4, { on (7), only (7, -1) }, { 53, 55, 59, 62 }, "V" },
            { "Dm7/C",    0, Maj, 1, { on (7), only (7, -1) }, { 48, 50, 53, 57 } },
            // Drop voicings and spreads.
            { "Cmaj7/G",  0, Maj, 0, { on (7), only (5, -1) }, { 43, 48, 52, 59 } },               // drop 2
            { "Cmaj7/E",  0, Maj, 0, { on (7), only (3, -1) }, { 40, 48, 55, 59 } },               // drop 3
            { "Cmaj7",    0, Maj, 0, { on (7), only (1, -1), only (5, -1) }, { 36, 43, 52, 59 } }, // drop 2 & 4
            { "C",        0, Maj, 0, { only (1, -1), only (3, 1) }, { 36, 55, 64 } },              // open triad
            { "C",        0, Maj, 0, { add (1, -1), add (1, 1) }, { 36, 48, 52, 55, 60 } },        // stacked roots
            // Other keys and scales (names spelled in the key).
            { "E7b9",     9, Scale::HarmonicMinor, 4, { on (7), on (9) }, { 64, 68, 71, 74, 77 }, "V" },
            { "Gm7",      5, Maj, 1, { on (7) }, { 55, 58, 62, 65 }, "ii" },
            { "Bb",       5, Maj, 3, {}, { 58, 62, 65 }, "IV" },
            { "A7",       2, Maj, 4, { on (7) }, { 57, 61, 64, 67 } },
            { "G7",       0, Scale::NaturalMinor, 4, { on (7) }, { 55, 59, 62, 65 }, "V" },        // default raised 3rd
            { "Db",       1, Maj, 0, {}, { 49, 53, 56 }, "I" },
            { "D#m7",     6, Maj, 5, { on (7) }, { 63, 66, 70, 73 }, "vi" },
            { "Ebm",      3, Scale::NaturalMinor, 0, {}, { 51, 54, 58 }, "i" },
            { "E/G#",     9, Scale::NaturalMinor, 4, { only (1, 1) }, { 68, 71, 76 }, "V" },
            { "Ebmaj7#5", 0, Scale::HarmonicMinor, 2, { on (7) }, { 51, 55, 59, 62 }, "III+" },
        };
    }

    std::vector<int> sortedNotes (const ChordSlot& slot, const ChordContext& ctx)
    {
        const ChordNotes n = chordNotes (slot, ctx);
        std::vector<int> notes (n.begin(), n.end());
        std::sort (notes.begin(), notes.end());
        return notes;
    }
}

//==============================================================================
static void testReferenceSet()
{
    std::cout << "== reference chords, built by step edits ==\n";
    int built = 0;

    for (const auto& ref : referenceSet())
    {
        ChordSlot slot = makeGroupSlot (ChordGroup::Triads, ref.scale, ref.degree);
        for (const auto& op : ref.ops)
            apply (slot, op);

        const ChordContext ctx { ref.key, ref.scale, 0 };
        const auto got = sortedNotes (slot, ctx);
        const std::string slotName = nameSlot (slot, ctx).text();

        unsigned mask = 0;
        for (int n : got)
            mask |= 1u << (n % 12);
        const std::string setName = got.empty() ? "" : nameChord (mask, got.front() % 12, makeSpelling (ref.key, ref.scale)).text();

        std::ostringstream what;
        what << ref.name << " (" << keyName (ref.key) << " " << scaleInfo (ref.scale).name << ", degree " << ref.degree + 1
             << "): notes [" << list (got) << "]";
        bool ok = got == ref.notes && slotName == ref.name && setName == ref.name;
        if (got != ref.notes)
            what << ", want [" << list (ref.notes) << "]";
        if (slotName != ref.name || setName != ref.name)
            what << ", named " << slotName << " / " << setName;
        if (ref.numeral != nullptr)
        {
            const auto rn = romanNumeral (slot, ref.scale);
            what << ", " << rn;
            ok &= rn == ref.numeral;
            if (rn != ref.numeral)
                what << " (want " << ref.numeral << ")";
        }
        check (ok, what.str());
        built += ok ? 1 : 0;
    }
    std::cout << "        " << built << " reference chords built\n";
}

//==============================================================================
static void testProgressionOrders()
{
    std::cout << "== progressions ==\n";
    const auto pop = makeChords (ChordGroup::Sevenths, Scale::Major, progressionDegrees (Progression::Pop));
    check (pop[1].degree == 4 && pop[0].degree == 0 && pop[1] == makeGroupSlot (ChordGroup::Sevenths, Scale::Major, 4),
           "Pop progression: the D key plays V in the group's shape");

    check (progressionDegrees (Progression::Custom) == identityOrder(), "Chordism's Custom plays as Diatonic");

    bool permutations = true;
    for (int p = 0; p < numProgressions; ++p)
    {
        auto d = progressionInfo ((Progression) p).degrees;
        std::sort (d.begin(), d.end());
        permutations &= d == identityOrder();
    }
    check (permutations, "every progression keeps all seven chords on some key");

    // The parameter's choices: the enum without Custom, both ways.
    bool roundTrip = numProgressionChoices == numProgressions - 1;
    for (int c = 0; c < numProgressionChoices; ++c)
        roundTrip &= progressionChoice (progressionFromChoice (c)) == c && progressionFromChoice (c) != Progression::Custom;
    check (roundTrip && progressionFromChoice (1) == Progression::Pop && progressionChoice (Progression::Thirds) == 7
               && progressionChoice (Progression::Custom) == 0 && progressionFromChoice (99) == Progression::Diatonic,
           "Progression choices: Diatonic, Pop … Thirds (Custom skipped; out of range is Diatonic)");
}

//==============================================================================
static void testNaming()
{
    std::cout << "== chord names ==\n";
    const auto c = makeSpelling (0, Scale::Major);
    auto name = [&c] (std::vector<int> notes, const Spelling* sp = nullptr) {
        unsigned m = 0;
        for (int n : notes) m |= 1u << (n % 12);
        return nameChord (m, *std::min_element (notes.begin(), notes.end()) % 12, sp ? *sp : c).text();
    };

    check (name ({ 48, 52, 55, 57 }) == "C6" && name ({ 45, 48, 52, 55 }) == "Am7", "C E G A: C6 over C, Am7 over A");
    check (name ({ 48, 51, 55, 57 }) == "Cm6" && name ({ 45, 48, 51, 55 }) == "Am7b5", "C Eb G A: Cm6 over C, Am7b5 over A");
    check (name ({ 48, 51, 54, 57 }) == "Cdim7" && name ({ 51, 54, 57, 60 }) == "Ebdim7", "diminished 7ths take the bass as root");
    check (name ({ 48, 52, 56 }) == "Caug" && name ({ 52, 56, 60 }) == "Eaug", "augmented triads take the bass as root");
    check (name ({ 48, 50, 55 }) == "Csus2" && name ({ 43, 48, 50 }) == "Gsus4", "C D G: Csus2; G C D: Gsus4");
    check (name ({ 48 }) == "C" && name ({ 48, 60 }) == "C", "a single pitch class is named by itself");
    check (nameChord (0, 0, c).text().empty() && nameNotes ({}, c).empty(), "no notes, no name");
    check (name ({ 40, 48, 55 }) == "C/E" && name ({ 43, 48, 52 }) == "C/G", "slash bass for inversions");
    check (name ({ 38, 48, 52, 55 }) == "D9sus4", "C E G over D reads as D9sus4 (= C/D)");
    check (name ({ 48, 53, 58, 63 }) == "Cm11", "stacked 4ths C F Bb Eb: Cm11");

    // Spelling in the key.
    const auto d = makeSpelling (2, Scale::Major), f = makeSpelling (5, Scale::Major), am = makeSpelling (9, Scale::NaturalMinor);
    check (name ({ 54, 57, 61 }, &d) == "F#m" && name ({ 61, 64, 67 }, &d) == "C#dim", "D major: F#m, C#dim");
    check (name ({ 58, 62, 65 }, &f) == "Bb" && name ({ 50, 58, 65 }, &f) == "Bb/D", "F major: Bb, Bb/D");
    check (name ({ 56, 59, 64 }, &am) == "E/G#", "A minor: E/G# (the raised 7th is G#, not Ab)");

    auto scaleNames = [] (int key, Scale s) {
        const auto sp = makeSpelling (key, s);
        std::string out;
        for (int i = 0; i < 7; ++i)
            out += (i ? " " : "") + sp.name (key + scaleInfo (s).semitones[(size_t) i]);
        return out;
    };
    check (scaleNames (1, Scale::Major) == "Db Eb F Gb Ab Bb C", "key 1 major is Db major: " + scaleNames (1, Scale::Major));
    check (scaleNames (6, Scale::Major) == "F# G# A# B C# D# E#", "key 6 major is F# major (tie: sharps)");
    check (scaleNames (3, Scale::NaturalMinor) == "Eb F Gb Ab Bb Cb Db", "key 3 minor is Eb minor (tie: flats)");
    check (scaleNames (8, Scale::NaturalMinor) == "G# A# B C# D# E F#", "key 8 minor is G# minor");
    check (scaleNames (1, Scale::HarmonicMinor) == "C# D# E F# G# A B#", "C# harmonic minor has B#");
    check (scaleNames (5, Scale::Dorian) == "F G Ab Bb C D Eb", "F Dorian");

    // Every scale is spelled with seven different letters in every key.
    bool letters = true;
    for (int k = 0; k < numKeys; ++k)
        for (int s = 0; s < numScales; ++s)
        {
            const auto sp = makeSpelling (k, (Scale) s);
            int seen = 0;
            for (int i = 0; i < 7; ++i)
                seen |= 1 << sp.letter[(size_t) ((k + scaleInfo ((Scale) s).semitones[(size_t) i]) % 12)];
            letters &= seen == 0x7f;
        }
    check (letters, "every key x scale is spelled with the seven letters once each");

    // Every factory chord gets a name with its own root in some reading, and never an empty one.
    bool named = true;
    int rootIsSlot = 0, total = 0;
    for (int k = 0; k < numKeys; ++k)
        for (int s = 0; s < numScales; ++s)
            for (int g = 0; g < numChordGroups; ++g)
                for (int dgr = 0; dgr < 7; ++dgr)
                {
                    const ChordContext ctx { k, (Scale) s, 0 };
                    const auto slot = makeGroupSlot ((ChordGroup) g, (Scale) s, dgr);
                    const auto n = nameSlot (slot, ctx);
                    named &= ! n.empty() && ! n.rootName.empty();
                    rootIsSlot += n.root == slotRootPc (slot, ctx) ? 1 : 0;
                    ++total;
                }
    check (named, "all " + std::to_string (total) + " factory chords have a name");
    std::cout << "        " << rootIsSlot << " of " << total << " factory chords are named on their slot's root\n";
    check (rootIsSlot * 10 >= total * 8, "at least 80% of factory chords are named on their slot's root (the rest read better on another root, e.g. 6ths iii = Cmaj7/E)");
}

//==============================================================================
static void testRomanNumerals()
{
    std::cout << "== Roman numerals ==\n";
    auto row = [] (Scale s, ChordGroup g = ChordGroup::Triads) {
        std::string out;
        for (int d = 0; d < 7; ++d)
            out += (d ? " " : "") + romanNumeral (makeGroupSlot (g, s, d), s);
        return out;
    };
    check (row (Scale::Major) == "I ii iii IV V vi vii*", "major: " + row (Scale::Major));
    check (row (Scale::NaturalMinor) == "i ii* III iv V VI VII", "natural minor (raised V): " + row (Scale::NaturalMinor));
    check (row (Scale::HarmonicMinor) == "i ii* III+ iv V VI vii*", "harmonic minor: " + row (Scale::HarmonicMinor));
    check (row (Scale::PhrygianDominant) == "I II iii* iv v* VI+ vii", "Phrygian dominant: " + row (Scale::PhrygianDominant));
    check (row (Scale::Major, ChordGroup::FirstInversion) == "I ii iii IV V vi vii*", "an inversion keeps its numeral");
    check (row (Scale::Major, ChordGroup::Power) == "I II III IV V VI VII", "no 3rd: upper case");
    check (row (Scale::Major, ChordGroup::Sevenths) == "I ii iii IV V vi vii*", "sevenths: the triad decides");
}

//==============================================================================
// Phase 7: the black keys' borrowed chords (ChordGroups.h, ChordChimp's PLAN.md §3.1).
static void testBorrowed()
{
    std::cout << "== borrowed chords (black keys) ==\n";
    auto sorted = [] (const ChordSlot& slot, const ChordContext& ctx) {
        const auto n = chordNotes (slot, ctx);
        std::vector<int> v (n.begin(), n.end());
        std::sort (v.begin(), v.end());
        return v;
    };
    // "numeral name, …" for the five black keys.
    auto row = [] (ChordGroup g, int key, Scale s) {
        const ChordSet set = makeGroupChords (g, s);
        std::string out;
        for (int b = 0; b < numBlackSlots; ++b)
        {
            const auto& slot = set[(size_t) (numWhiteSlots + b)];
            out += (b ? ", " : "") + romanNumeral (slot, s) + " " + nameSlot (slot, { key, s, 0 }).text();
        }
        return out;
    };

    const ChordSet c = makeGroupChords (ChordGroup::Triads, Scale::Major);
    const std::vector<std::vector<int>> want = { { 49, 53, 56 }, { 51, 55, 58 }, { 54, 57, 60 }, { 56, 60, 63 }, { 58, 62, 65 } };
    bool notesRight = true;
    for (int b = 0; b < numBlackSlots; ++b)
        notesRight &= sorted (c[(size_t) (numWhiteSlots + b)], {}) == want[(size_t) b];
    check (notesRight, "C major triads: C# D# F# G# A# play Db F Ab, Eb G Bb, F# A C, Ab C Eb, Bb D F");
    check (row (ChordGroup::Triads, 0, Scale::Major) == "bII Db, bIII Eb, #iv* F#dim, bVI Ab, bVII Bb",
           "C major triads: " + row (ChordGroup::Triads, 0, Scale::Major));
    check (row (ChordGroup::Sevenths, 0, Scale::Major) == "bII Dbmaj7, bIII Ebmaj7, #iv* F#m7b5, bVI Abmaj7, bVII Bb7",
           "C major sevenths: " + row (ChordGroup::Sevenths, 0, Scale::Major));
    check (row (ChordGroup::Triads, 9, Scale::NaturalMinor) == "bII Bb, III C, #iv* D#dim, VI F, VII G",
           "A natural minor (bIII, bVI, bVII are the key's own): " + row (ChordGroup::Triads, 9, Scale::NaturalMinor));

    // In every key, scale and group, a black key plays its mode's chord, in the group's shape.
    int wrong = 0, total = 0;
    for (int key = 0; key < numKeys; ++key)
        for (int sc = 0; sc < numScales; ++sc)
            for (int g = 0; g < numChordGroups; ++g)
            {
                const ChordSet set = makeChords ((ChordGroup) g, (Scale) sc, progressionDegrees (Progression::Pop));
                for (int b = 0; b < numBlackSlots; ++b)
                {
                    const auto& info = borrowedChord (b);
                    const auto own = sorted (makeGroupSlot ((ChordGroup) g, info.mode, info.degree), { key, info.mode, 0 });
                    const auto& slot = set[(size_t) (numWhiteSlots + b)];
                    bool small = true;
                    for (const auto& st : slot.steps)
                        small &= st.accidental >= -2 && st.accidental <= 2;
                    wrong += sorted (slot, { key, (Scale) sc, 0 }) == own && small ? 0 : 1;
                    ++total;
                }
            }
    check (wrong == 0, std::to_string (total - wrong) + "/" + std::to_string (total)
                           + " black-key chords (12 keys x 13 scales x 17 groups x 5, Pop progression) are their mode's chord, accidentals within 2");
}

int main()
{
    testProgressionOrders();
    testReferenceSet();
    testNaming();
    testRomanNumerals();
    testBorrowed();
    return testutil::finish ("test_chords");
}
