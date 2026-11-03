#include "Parameters.h"

namespace chordmonkey::params
{
    namespace
    {
        juce::ParameterID id (const char* name) { return { name, 1 }; }

        juce::StringArray keyNames()
        {
            juce::StringArray a;
            for (int k = 0; k < numKeys; ++k)
                a.add (keyName (k));
            return a;
        }

        juce::StringArray scaleNames()
        {
            juce::StringArray a;
            for (int s = 0; s < numScales; ++s)
                a.add (scaleInfo ((Scale) s).name);
            return a;
        }

        juce::StringArray groupNames()
        {
            juce::StringArray a;
            for (int g = 0; g < numChordGroups; ++g)
                a.add (chordGroupInfo ((ChordGroup) g).name);
            return a;
        }

        juce::ParameterID id (const juce::String& name) { return { name, 1 }; }

        juce::StringArray progressionNames()
        {
            juce::StringArray a;
            for (int c = 0; c < numProgressionChoices; ++c)
                a.add (progressionInfo (progressionFromChoice (c)).name);
            return a;
        }

        bool on (const std::atomic<float>* p) noexcept { return p->load() > 0.5f; }
        int whole (const std::atomic<float>* p) noexcept { return (int) std::lround (p->load()); }
        int choice (const std::atomic<float>* p, int count) noexcept { return juce::jlimit (0, count - 1, whole (p)); }
        double fraction (const std::atomic<float>* p) noexcept { return juce::jlimit (0.0, 1.0, (double) p->load() / 100.0); }

        template <class Name>
        juce::StringArray names (int count, Name name)
        {
            juce::StringArray a;
            for (int i = 0; i < count; ++i)
                a.add (name (i));
            return a;
        }

        std::unique_ptr<juce::AudioParameterFloat> percent (const juce::String& pid, const juce::String& name, float initial)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                id (pid), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), initial,
                juce::AudioParameterFloatAttributes().withLabel ("%"));
        }
    }

    bool isPresetParameter (const juce::String& paramId)
    {
        for (const char* excluded : { enable, testPattern, keyOctave, analyse, play, pattern, testWorkspace, bindSource, bindStart, keysLock })
            if (paramId == excluded)
                return false;
        return true;
    }

    void fromChordism (juce::ValueTree& tree)
    {
        auto p = tree.getChildWithProperty ("id", progression);
        if (p.isValid() && p.hasProperty ("value"))
        {
            const double v = p.getProperty ("value");
            const int index = std::isfinite (v) ? juce::jlimit (0, numProgressions - 1, (int) std::lround (v)) : 0;
            p.setProperty ("value", progressionChoice ((Progression) index), nullptr);
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterBool> (id (enable), "Enable", true));
        layout.add (std::make_unique<AudioParameterChoice> (id (key), "Key", keyNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (scale), "Scale", scaleNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (chordGroup), "Chord Group", groupNames(), 0));
        layout.add (std::make_unique<AudioParameterInt> (id (chordOctave), "Chord Octave", -3, 3, 0));
        layout.add (std::make_unique<AudioParameterBool> (id (testPattern), "Test pattern", false));

        // The virtual port's three parameters ("port", "portCompensate", "portTrimMs") were
        // removed with the AU builds on 2026-10-20; a saved state's values for them are ignored.

        // Phase 2: Progression. Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (progression), "Progression", progressionNames(), 0));

        // Phase 3: the effects (Source/core/engine/ChordFx.h). Appended, never
        // reordered; the choice lists come from the engine's enums, which only ever append.
        const ChordFxSettings fx;
        layout.add (std::make_unique<AudioParameterChoice> (id (stack), "Stack",
                                                            names (numStackModes, [] (int i) { return stackModeName ((StackMode) i); }), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (strum), "Strum",
                                                            names (numStrumModes, [] (int i) { return strumModeName ((StrumMode) i); }), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (strumRate), "Strum rate", names (numStrumRates, strumRateName), fx.strumRate));
        layout.add (std::make_unique<AudioParameterChoice> (id (arp), "Arp",
                                                            names (numArpPatterns, [] (int i) { return arpPatternName ((ArpPattern) i); }), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (arpRate), "Arp rate", names (numPatternRates, patternRateName), fx.arpRate));
        layout.add (std::make_unique<AudioParameterInt> (id (arpRange), "Arp range", minArpRange, maxArpRange, fx.arpRange));
        layout.add (percent (arpGate, "Arp gate", (float) (fx.arpGate * 100.0)));
        layout.add (std::make_unique<AudioParameterBool> (id (repeat), "Repeat", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (repeatRate), "Repeat rate", names (numPatternRates, patternRateName), fx.repeat.rate));
        layout.add (percent (repeatGate, "Repeat gate", (float) (fx.repeat.gate * 100.0)));
        layout.add (std::make_unique<AudioParameterBool> (id (seq), "Step seq", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (seqPattern), "Seq pattern", StringArray { "1", "2", "3", "4" }, 0));
        layout.add (percent (seqSwing, "Seq swing", 0.0f));
        layout.add (percent (seqGate, "Seq gate", (float) (fx.seq.gate * 100.0)));
        layout.add (std::make_unique<AudioParameterChoice> (id (velocity), "Velocity",
                                                            names (numVelocityCurves, [] (int i) { return velocityCurveName ((VelocityCurve) i); }), 0));
        layout.add (std::make_unique<AudioParameterInt> (id (velocityPeak), "Velocity peak", 1, 127, fx.velocity.peak));
        layout.add (percent (velocityRange, "Velocity range", (float) (fx.velocity.range * 100.0)));
        layout.add (std::make_unique<AudioParameterChoice> (id (hold), "Hold",
                                                            names (numHoldModes, [] (int i) { return holdModeName ((HoldMode) i); }), 0));

        // Phase 7: voice leading (VoiceLeading.h). Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (voiceLeading), "Voice leading",
                                                            names (numVoiceLeadingModes, [] (int i) { return voiceLeadingName ((VoiceLeadingMode) i); }), 0));

        // ChordChimp Phase 8: Key octave. Appended, never reordered; Ignore is the old behaviour.
        layout.add (std::make_unique<AudioParameterChoice> (id (keyOctave), "Key octave", StringArray { "Ignore", "Follow" }, 0));

        // Key detection: Analyse. Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (analyse), "Analyse", StringArray { "Off", "Listen", "Use guess" }, 0));

        // Phase 2: pattern playback. Appended, never reordered.
        layout.add (std::make_unique<AudioParameterBool> (id (play), "Play", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (pattern), "Pattern",
                                                            names (numPatterns, [] (int i) { return juce::String (patternName (i)); }), 0));
        layout.add (std::make_unique<AudioParameterBool> (id (testWorkspace), "Test workspace", false));

        // Phase 3: Bind and Keys-Lock. Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (bindSource), "Bind",
                                                            names (numBindSources, [] (int i) { return bindSourceName ((BindSource) i); }), 0));
        layout.add (std::make_unique<AudioParameterInt> (id (bindStart), "Bind start", 0, KeyRouter::maxBindStart, 36,
                                                         AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                                         {
                                                             char name[8];
                                                             noteName (v, name);
                                                             return juce::String (name);
                                                         })));
        layout.add (std::make_unique<AudioParameterChoice> (id (keysLock), "Keys-Lock",
                                                            names (numKeysLockModes, [] (int i) { return keysLockName ((KeysLockMode) i); }), 0));
        return layout;
    }

    Snapshot::Snapshot (juce::AudioProcessorValueTreeState& state)
        : enable (state.getRawParameterValue (params::enable)),
          keyParam (state.getRawParameterValue (params::key)),
          scale (state.getRawParameterValue (params::scale)),
          group (state.getRawParameterValue (params::chordGroup)),
          chordOctave (state.getRawParameterValue (params::chordOctave)),
          testPattern (state.getRawParameterValue (params::testPattern)),
          progression (state.getRawParameterValue (params::progression)),
          keyOctaveParam (state.getRawParameterValue (params::keyOctave)),
          analyseParam (state.getRawParameterValue (params::analyse)),
          playParam (state.getRawParameterValue (params::play)),
          patternParam (state.getRawParameterValue (params::pattern)),
          testWorkspaceParam (state.getRawParameterValue (params::testWorkspace)),
          bindSourceParam (state.getRawParameterValue (params::bindSource)),
          bindStartParam (state.getRawParameterValue (params::bindStart)),
          keysLockParam (state.getRawParameterValue (params::keysLock)),
          fxStack (state.getRawParameterValue (params::stack)),
          fxStrum (state.getRawParameterValue (params::strum)),
          fxStrumRate (state.getRawParameterValue (params::strumRate)),
          fxArp (state.getRawParameterValue (params::arp)),
          fxArpRate (state.getRawParameterValue (params::arpRate)),
          fxArpRange (state.getRawParameterValue (params::arpRange)),
          fxArpGate (state.getRawParameterValue (params::arpGate)),
          fxRepeat (state.getRawParameterValue (params::repeat)),
          fxRepeatRate (state.getRawParameterValue (params::repeatRate)),
          fxRepeatGate (state.getRawParameterValue (params::repeatGate)),
          fxSeq (state.getRawParameterValue (params::seq)),
          fxSeqPattern (state.getRawParameterValue (params::seqPattern)),
          fxSeqSwing (state.getRawParameterValue (params::seqSwing)),
          fxSeqGate (state.getRawParameterValue (params::seqGate)),
          fxVelocity (state.getRawParameterValue (params::velocity)),
          fxVelocityPeak (state.getRawParameterValue (params::velocityPeak)),
          fxVelocityRange (state.getRawParameterValue (params::velocityRange)),
          fxHold (state.getRawParameterValue (params::hold)),
          fxVoiceLeading (state.getRawParameterValue (params::voiceLeading))
    {
        jassert (enable && keyParam && scale && group && chordOctave && testPattern && progression && keyOctaveParam && analyseParam
                 && playParam && patternParam && testWorkspaceParam && bindSourceParam && bindStartParam && keysLockParam);
        jassert (fxStack && fxStrum && fxStrumRate && fxArp && fxArpRate && fxArpRange && fxArpGate && fxRepeat && fxRepeatRate
                 && fxRepeatGate && fxSeq && fxSeqPattern && fxSeqSwing && fxSeqGate && fxVelocity && fxVelocityPeak
                 && fxVelocityRange && fxHold && fxVoiceLeading);
    }

    ChordFxSettings Snapshot::chordFx() const noexcept
    {
        ChordFxSettings fx;
        fx.stack = (StackMode) choice (fxStack, numStackModes);
        fx.strum = (StrumMode) choice (fxStrum, numStrumModes);
        fx.strumRate = choice (fxStrumRate, numStrumRates);
        fx.arp = (ArpPattern) choice (fxArp, numArpPatterns);
        fx.arpRate = choice (fxArpRate, numPatternRates);
        fx.arpRange = juce::jlimit (minArpRange, maxArpRange, whole (fxArpRange));
        fx.arpGate = fraction (fxArpGate);
        fx.repeat.on = on (fxRepeat);
        fx.repeat.rate = choice (fxRepeatRate, numPatternRates);
        fx.repeat.gate = fraction (fxRepeatGate);
        fx.seq.on = on (fxSeq);
        fx.seq.pattern = choice (fxSeqPattern, numSeqPatterns);
        fx.seq.swing = fraction (fxSeqSwing);
        fx.seq.gate = fraction (fxSeqGate);
        fx.velocity.curve = (VelocityCurve) choice (fxVelocity, numVelocityCurves);
        fx.velocity.peak = juce::jlimit (1, 127, whole (fxVelocityPeak));
        fx.velocity.range = fraction (fxVelocityRange);
        fx.hold = (HoldMode) choice (fxHold, numHoldModes);
        fx.voiceLeading = (VoiceLeadingMode) choice (fxVoiceLeading, numVoiceLeadingModes);
        return fx;
    }

    ChordGroup Snapshot::chordGroup() const noexcept
    {
        return (ChordGroup) juce::jlimit (0, numChordGroups - 1, whole (group));
    }

    Scale Snapshot::currentScale() const noexcept
    {
        return (Scale) juce::jlimit (0, numScales - 1, whole (scale));
    }

    AnalyseMode Snapshot::analyseMode() const noexcept { return (AnalyseMode) choice (analyseParam, 3); }

    bool Snapshot::testWorkspace() const noexcept { return on (testWorkspaceParam); }

    int Snapshot::key() const noexcept { return juce::jlimit (0, numKeys - 1, whole (keyParam)); }

    EngineSettings Snapshot::engineSettings() const noexcept
    {
        EngineSettings s;
        s.enabled = on (enable);
        s.key = key();
        s.scale = currentScale();
        s.group = chordGroup();
        s.chordOctave = juce::jlimit (-3, 3, whole (chordOctave));
        s.testPattern = on (testPattern);

        s.progression = progressionFromChoice (whole (progression));
        s.followKeyOctave = choice (keyOctaveParam, 2) == 1;
        s.analysing = analyseMode() != AnalyseMode::Off;
        s.play = on (playParam);
        s.pattern = choice (patternParam, numPatterns);
        s.bind = (BindSource) choice (bindSourceParam, numBindSources);
        s.bindStart = juce::jlimit (0, KeyRouter::maxBindStart, whole (bindStartParam));
        s.keysLock = (KeysLockMode) choice (keysLockParam, numKeysLockModes);
        s.fx = chordFx();
        return s;
    }
}
