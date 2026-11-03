#include "ChordMonkeyProcessor.h"
#include "../ui/ChordMonkeyEditor.h"
#include "../core/model/ChordSets.h"
#include <cmath>

namespace chordmonkey
{
    namespace
    {
        // Phase 8: a saved value that isn't a finite number (a damaged set or preset file) is
        // dropped rather than making the parameter NaN: a restore keeps the parameter's value,
        // a preset gives it its default.
        void dropNonFiniteValues (juce::ValueTree& tree)
        {
            for (auto child : tree)
                if (child.hasType ("PARAM") && child.hasProperty ("value") && ! std::isfinite ((double) child.getProperty ("value")))
                    child.removeProperty ("value", nullptr);
        }

        // Analyse isn't saved (ChordChimp's docs/KEY_DETECTION_PLAN.md K5.4): a set saved mid-analysis opens
        // playing chords.
        void dropAnalyse (juce::ValueTree& tree)
        {
            tree.removeChild (tree.getChildWithProperty ("id", params::analyse), nullptr);
        }

        const char* analyseName (params::AnalyseMode m)
        {
            return m == params::AnalyseMode::Listen ? "Listen" : (m == params::AnalyseMode::UseGuess ? "Use guess" : "Off");
        }
    }

    juce::AudioProcessor::BusesProperties ChordMonkeyProcessor::makeBuses()
    {
        // An instrument with a silent stereo output: hosts route MIDI-only instruments more
        // reliably when there's an audio bus.
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    ChordMonkeyProcessor::ChordMonkeyProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "ChordMonkey", params::createLayout()),
          snapshot (state)
    {
        generated.ensureSize (output.capacity * 4);
        uiState.addListener (this);
        startTimerHz (20);
    }

    ChordMonkeyProcessor::~ChordMonkeyProcessor()
    {
        stopTimer();
        uiState.removeListener (this);
    }

    void ChordMonkeyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedSampleRate.store (sampleRate);
        generated.ensureSize (output.capacity * 4);
        engine.requestReset();
    }

    void ChordMonkeyProcessor::releaseResources()
    {
        engine.requestReset();
    }

    // Phase 8: JUCE's VST3 wrapper calls this when the host stops processing (setProcessing
    // (false)). No MIDI can go out then, so every note ends on the first block after it resumes:
    // in Live, a device switched off leaves its chord sounding until it is on again.
    void ChordMonkeyProcessor::reset()
    {
        engine.requestReset();
    }

    bool ChordMonkeyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return layouts.getMainInputChannelSet().isDisabled()
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    Transport ChordMonkeyProcessor::readTransport()
    {
        Transport t = lastTransport;
        t.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : preparedSampleRate.load();
        t.playing = false;
        t.looping = false;
        t.hasBarStart = false;

        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                t.playing = pos->getIsPlaying();
                if (auto b = pos->getBpm(); b.hasValue() && std::isfinite (*b) && *b > 0.0)
                    t.bpm = *b;
                if (auto p = pos->getPpqPosition(); p.hasValue() && std::isfinite (*p))
                    t.ppq = *p;
                if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
                {
                    t.numerator = ts->numerator;
                    t.denominator = ts->denominator;
                }
                // Loop points and the bar position, for the effects' clock (Source/core/engine/Clock.h).
                if (auto loop = pos->getLoopPoints(); loop.hasValue() && std::isfinite (loop->ppqStart) && std::isfinite (loop->ppqEnd))
                {
                    t.looping = pos->getIsLooping() && loop->ppqEnd > loop->ppqStart;
                    t.loopStart = loop->ppqStart;
                    t.loopEnd = loop->ppqEnd;
                }
                if (auto bar = pos->getPpqPositionOfLastBarStart(); bar.hasValue() && std::isfinite (*bar))
                {
                    t.hasBarStart = true;
                    t.barStartPpq = *bar;
                }
            }
        }
        return t;
    }

    void ChordMonkeyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, false);
    }

    void ChordMonkeyProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, true);
    }

    void ChordMonkeyProcessor::run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        const int numSamples = buffer.getNumSamples();

        const Transport transport = readTransport();

        EngineSettings settings = snapshot.engineSettings();
        if (bypassed)
        {
            // Host bypass behaves like Enable off: generated notes end, input passes through.
            settings.enabled = false;
            settings.testPattern = false;
        }

        // Short messages go through the engine; anything longer (SysEx) passes straight on. Keys
        // played on the editor come first, at the block's first sample.
        input.clear();
        generated.clear();
        {
            const auto scope = uiFifo.read (uiFifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) input.push (uiNotes[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) input.push (uiNotes[(size_t) (scope.startIndex2 + i)]);
        }
        for (const auto meta : midi)
        {
            if (meta.numBytes >= 1 && meta.numBytes <= 3)
            {
                MidiEvent e;
                e.sample = meta.samplePosition;
                e.size = (uint8_t) meta.numBytes;
                for (int i = 0; i < meta.numBytes; ++i)
                    e.data[i] = meta.data[i];
                // A host flooding more than the buffer holds loses note-ons first: the last
                // slots are kept for key-ups, so no key is left down.
                if (input.space() > inputReserve || e.isNoteOff() || e.isAllNotesOff())
                    input.push (e);
            }
            else
            {
                generated.addEvent (meta.data, meta.numBytes, meta.samplePosition);
            }
        }

        if (snapshot.testWorkspace() && ! bypassed)
            addTestKeys (transport, numSamples);

        if (workspaceForAudio.hasNew())
            engine.setWorkspace (workspaceForAudio.read());

        output.clear();
        engine.process (settings, transport, input.begin(), input.size(), numSamples, output);
        playingPattern.store (engine.getPlayingPattern(), std::memory_order_relaxed);
        if (transport.bpm > 0.0 && std::isfinite (transport.bpm))
            hostBpm.store (transport.bpm, std::memory_order_relaxed);
        hostNumerator.store (transport.numerator, std::memory_order_relaxed);
        hostDenominator.store (transport.denominator, std::memory_order_relaxed);
        playingSlot.store (engine.getPlayingSlot(), std::memory_order_relaxed);
        recorder.record (transport, numSamples, output.begin(), output.size());

        for (const auto& e : output)
            generated.addEvent (e.data, e.size, e.sample);

        // Copy rather than swap: swapping would hand `generated` the host's (possibly small)
        // buffer, and the next block would allocate.
        midi.clear();
        midi.addEvents (generated, 0, -1, 0);

        lastTransport = transport;
        if (transport.playing)
            lastTransport.ppq = transport.ppq + numSamples / transport.samplesPerBeat();

        publishNoteStates();
        lastChordSlot.store (engine.getLastChordSlot(), std::memory_order_relaxed);
        keyGuess.store (engine.getKeyGuess().pack(), std::memory_order_relaxed);
        soundingNotes.store (engine.getVoices().soundingNotes(), std::memory_order_relaxed);
        transportPlaying.store (transport.playing, std::memory_order_relaxed);

        ChordDisplay now { engine.getChords(), engine.getChordContext(), engine.getLastChordSlot() };
        if (! (now == lastDisplay))
        {
            lastDisplay = now;
            display.publish (now);
        }
    }

    void ChordMonkeyProcessor::publishNoteStates() noexcept
    {
        std::array<uint64_t, 2> s {}, k {}, p {};
        for (int n = 0; n < 128; ++n)
        {
            const uint64_t bit = (uint64_t) 1 << (n & 63);
            if (engine.getVoices().isSounding (n)) s[(size_t) (n >> 6)] |= bit;
            if (engine.isKeyDown (n))              k[(size_t) (n >> 6)] |= bit;
            if (engine.isPerforming (n))           p[(size_t) (n >> 6)] |= bit;
        }
        for (size_t i = 0; i < 2; ++i)
        {
            sounding[i].store (s[i], std::memory_order_relaxed);
            keysDown[i].store (k[i], std::memory_order_relaxed);
            performing[i].store (p[i], std::memory_order_relaxed);
        }
    }

    void ChordMonkeyProcessor::pushUiNote (int note, int velocity, bool on, bool palette) noexcept
    {
        if (note < 0 || note > 127)
            return;
        const auto scope = uiFifo.write (1);
        const MidiEvent e = (on ? MidiEvent::noteOn (0, note, juce::jlimit (1, 127, velocity), 0) : MidiEvent::noteOff (0, note, 0))
                                .withOrigin (palette ? MidiEvent::fromPalette : MidiEvent::fromKeys);
        if (scope.blockSize1 > 0) uiNotes[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) uiNotes[(size_t) scope.startIndex2] = e;
    }

    //==========================================================================
    void ChordMonkeyProcessor::timerCallback()
    {
        // A program the host chose off the message thread.
        if (const int program = pendingProgram.exchange (-1); program >= 0)
            loadProgram (program);

        // The host (or the bridge) asked for the guess.
        if (snapshot.analyseMode() == params::AnalyseMode::UseGuess)
            applyGuess();

        // A Bind pattern key chose a pattern: the Pattern parameter follows, for the host and
        // the editor (the engine already plays it).
        if (const int k = engine.takePatternRequest(); k >= 0)
            if (auto* p = state.getParameter (params::pattern))
                if (const float v = p->convertTo0to1 ((float) k); std::abs (p->getValue() - v) > 1.0e-6f)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (v);
                    p->endChangeGesture();
                }

        // The test rig's workspace, from the bridge.
        if (snapshot.testWorkspace())
            readTestWorkspace();
        else
            testWorkspaceRead = {};

        // The test rig's takes and guesses, for checks in Live.
        if (snapshot.engineSettings().testPattern)
        {
            writeHeardKey();
            const auto info = recorder.info();
            if (info.take != 0 && ! info.recording && info.take != testTakeWritten && info.completedBars() > 0)
            {
                testTakeWritten = info.take;
                juce::String error;
                writeTake (testTakeFile(), error);
            }
        }
    }

    //==========================================================================
    // Key detection.

    void ChordMonkeyProcessor::applyGuess()
    {
        const KeyGuess g = readKeyGuess();
        const auto s = snapshot.engineSettings();

        // The parameters that change, set together: every gesture begins, then every value,
        // then every gesture ends, so a host can make it one undo step.
        std::array<std::pair<juce::RangedAudioParameter*, float>, 3> changes {};
        size_t n = 0;
        const auto change = [&] (const char* id, float plain)
        {
            if (auto* p = state.getParameter (id))
                if (const float v = p->convertTo0to1 (plain); std::abs (p->getValue() - v) > 1.0e-6f)
                    changes[n++] = { p, v };
        };
        if (g.has())
        {
            change (params::key, (float) g.tonic);
            change (params::scale, (float) (int) scaleForGuess (s.scale, g.minor));
            change (params::analyse, (float) params::AnalyseMode::Off);
        }
        else
        {
            change (params::analyse, (float) params::AnalyseMode::Listen);   // nothing to use yet: keep listening
        }
        for (size_t i = 0; i < n; ++i) changes[i].first->beginChangeGesture();
        for (size_t i = 0; i < n; ++i) changes[i].first->setValueNotifyingHost (changes[i].second);
        for (size_t i = 0; i < n; ++i) changes[i].first->endChangeGesture();
    }

    juce::File ChordMonkeyProcessor::heardKeyFile()
    {
        return testTakeFile().getSiblingFile ("Heard key.json");
    }

    juce::File ChordMonkeyProcessor::testWorkspaceFile()
    {
        return testTakeFile().getSiblingFile ("Workspace.txt");
    }

    juce::File ChordMonkeyProcessor::testKeysFile()
    {
        return testTakeFile().getSiblingFile ("Keys.txt");
    }

    // Audio thread: the test rig's keys that start or end in this block, into `input`.
    void ChordMonkeyProcessor::addTestKeys (const Transport& t, int numSamples) noexcept
    {
        const TestKeys& k = testKeysForAudio.read();
        if (! t.playing || k.count == 0 || numSamples <= 0)
            return;
        const double spb = t.samplesPerBeat();
        const double from = t.ppq, to = t.ppq + numSamples / spb;
        bool added = false;
        for (int i = 0; i < k.count; ++i)
        {
            const auto& key = k.keys[(size_t) i];
            for (const bool on : { true, false })
            {
                const double at = on ? key.from : key.to;
                if (at >= from && at < to && input.space() > inputReserve)
                {
                    const int sample = juce::jlimit (0, numSamples - 1, (int) std::llround ((at - from) * spb));
                    input.push (on ? MidiEvent::noteOn (0, key.note, 100, sample) : MidiEvent::noteOff (0, key.note, sample));
                    added = true;
                }
            }
        }
        if (added)
            input.sortBySample();
    }

    void ChordMonkeyProcessor::readTestWorkspace()
    {
        const auto file = testWorkspaceFile();
        const auto modified = file.getLastModificationTime();
        if (const auto keysFile = testKeysFile(); keysFile.existsAsFile() && keysFile.getLastModificationTime() != testKeysRead)
        {
            testKeysRead = keysFile.getLastModificationTime();
            TestKeys k;
            juce::StringArray lines;
            keysFile.readLines (lines);
            for (const auto& line : lines)
            {
                const auto t = juce::StringArray::fromTokens (line, " \t", "");
                if (line.trimStart().startsWithChar ('#') || t.size() < 3 || k.count == (int) k.keys.size())
                    continue;
                const double from = t[0].getDoubleValue(), length = t[2].getDoubleValue();
                const int note = t[1].getIntValue();
                if (note >= 0 && note <= 127 && length > 0.0 && std::isfinite (from) && std::isfinite (length))
                    k.keys[(size_t) k.count++] = { from, from + length, (uint8_t) note };
            }
            testKeysForAudio.publish (k);
        }

        if (! file.existsAsFile() || modified == testWorkspaceRead)
            return;
        testWorkspaceRead = modified;

        Workspace w;
        int n = 0;
        juce::StringArray lines;
        file.readLines (lines);
        for (const auto& line : lines)
        {
            if (line.trimStart().startsWithChar ('#'))
                continue;
            if (n == numPatterns)
                break;
            std::string error;
            Pattern p;
            if (parseProgression (line.toStdString(), snapshot.key(), p, error))
                w.patterns[(size_t) n] = p;
            ++n;
        }
        setWorkspace (w);
    }

    void ChordMonkeyProcessor::writeHeardKey()
    {
        const KeyGuess g = readKeyGuess();
        const auto mode = snapshot.analyseMode();
        if (g.pack() == heardKeyWritten && (int) mode == heardModeWritten)
            return;
        heardKeyWritten = g.pack();
        heardModeWritten = (int) mode;

        const auto keyObject = [] (int tonic, bool minor, int fit)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("key", guessKeyName (tonic, minor));
            o->setProperty ("mode", minor ? "minor" : "major");
            o->setProperty ("fit", fit / 100.0);
            return o;
        };
        auto* root = g.has() ? keyObject (g.tonic, g.minor, g.fit) : new juce::DynamicObject();
        juce::var json (root);
        root->setProperty ("analyse", analyseName (mode));
        root->setProperty ("state", g.state == GuessState::Confident ? "confident" : (g.state == GuessState::Tentative ? "tentative" : "none"));
        if (g.has() && g.runnerUpTonic >= 0)
            root->setProperty ("runnerUp", juce::var (keyObject (g.runnerUpTonic, g.runnerUpMinor, g.runnerUpFit)));
        root->setProperty ("notes", g.notes);

        const auto file = heardKeyFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (json));
    }

    void ChordMonkeyProcessor::notifyStateChanged()
    {
        if (! restoringState)
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }

    void ChordMonkeyProcessor::pushSlotPreview (int slot, bool on) noexcept
    {
        if (slot < 0 || slot >= numPatternSlots)
            return;
        const int note = MidiEvent::workspaceKey + slot;
        const auto scope = uiFifo.write (1);
        const MidiEvent e = (on ? MidiEvent::noteOn (0, note, 100, 0) : MidiEvent::noteOff (0, note, 0)).withOrigin (MidiEvent::fromWorkspace);
        if (scope.blockSize1 > 0) uiNotes[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) uiNotes[(size_t) scope.startIndex2] = e;
    }

    //==========================================================================
    // Programs and presets.

    int ChordMonkeyProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

    const juce::String ChordMonkeyProcessor::getProgramName (int index)
    {
        const auto& all = factoryPresets();
        return index >= 0 && index < (int) all.size() ? all[(size_t) index].name : juce::String();
    }

    void ChordMonkeyProcessor::setCurrentProgram (int index)
    {
        if (index < 0 || index >= getNumPrograms())
            return;
        currentProgram = index;
        if (juce::MessageManager::existsAndIsCurrentThread())
            loadProgram (index);
        else
            pendingProgram = index;       // the timer loads it
    }

    void ChordMonkeyProcessor::loadProgram (int index)
    {
        const auto& p = factoryPresets()[(size_t) juce::jlimit (0, getNumPrograms() - 1, index)];
        applyPreset (PresetManager::factoryTree (index), p.category, p.name);
        currentProgram = index;
    }

    void ChordMonkeyProcessor::applyPreset (const juce::ValueTree& preset, const juce::String& category, const juce::String& name)
    {
        auto tree = preset.createCopy();
        dropNonFiniteValues (tree);
        for (auto* p : getParameters())
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (param == nullptr || ! params::isPresetParameter (param->paramID))
                continue;
            const auto child = tree.getChildWithProperty ("id", param->paramID);
            const float value = child.hasProperty ("value") ? param->convertTo0to1 ((float) (double) child.getProperty ("value"))
                                                            : param->getDefaultValue();
            if (std::abs (param->getValue() - value) > 1.0e-6f)
                param->setValueNotifyingHost (value);
        }
        if (const auto ws = tree.getChildWithName ("WORKSPACE"); ws.isValid())
            setWorkspace (workspaceFromTree (ws));
        engine.requestReset();            // all notes off on a preset change (Chordism §5)
        setPresetIdentity (category, name);
    }

    juce::ValueTree ChordMonkeyProcessor::presetState()
    {
        juce::ValueTree tree ("ChordMonkey");
        for (const auto& child : state.copyState())
            if (child.hasType ("PARAM") && params::isPresetParameter (child.getProperty ("id").toString()))
                tree.appendChild (child.createCopy(), nullptr);
        tree.appendChild (workspaceToTree (getWorkspace()), nullptr);
        tree.setProperty ("version", JucePlugin_VersionString, nullptr);
        return tree;
    }

    void ChordMonkeyProcessor::setPresetIdentity (const juce::String& category, const juce::String& name)
    {
        presetCategory = category;
        presetName = name;
        notifyStateChanged();
    }

    //==========================================================================
    // Export.

    juce::File ChordMonkeyProcessor::testTakeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("ChordMonkey").getChildFile ("Exports").getChildFile ("Test take.mid");
    }

    bool ChordMonkeyProcessor::writeTake (const juce::File& file, juce::String& error) const
    {
        TakeInfo info;
        std::vector<RecordedEvent> events;
        bool copied = false;
        for (int attempt = 0; attempt < 4 && ! copied; ++attempt)
            copied = recorder.snapshot (info, events);
        if (! copied || info.completedBars() <= 0)
        {
            error = "Nothing to export yet: play at least one whole bar.";
            return false;
        }
        const auto song = exportBars (info, events);
        const auto bytes = writeMidiFile (song);
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    juce::File ChordMonkeyProcessor::writeExport (juce::String& error)
    {
        const auto info = recorder.info();
        const int bars = juce::jmin (info.completedBars(), ExportRecorder::maxBars);
        const auto s = snapshot.engineSettings();
        const juce::String name = "ChordMonkey " + juce::String (keyName (s.key)).upToFirstOccurrenceOf ("/", false, false) + " "
                                + scaleInfo (s.scale).name + " " + juce::String (bars) + (bars == 1 ? " bar" : " bars");
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChordMonkey Export")
                              .getChildFile (juce::File::createLegalFileName (name) + ".mid");
        return writeTake (file, error) ? file : juce::File();
    }

    juce::File ChordMonkeyProcessor::writePatternExport (int pattern, bool chordsOnly, juce::String& error)
    {
        const auto w = getWorkspace();
        if (pattern < 0 || pattern >= numPatterns || w.patterns[(size_t) pattern].empty())
        {
            error = "Nothing to drag: the pattern is empty.";
            return {};
        }
        const auto s = snapshot.engineSettings();
        const auto song = renderPattern (w, pattern, s, hostBpm.load(), hostNumerator.load(), hostDenominator.load(), chordsOnly);
        const auto bytes = writeMidiFile (song);
        const int bars = (int) std::ceil (w.patterns[(size_t) pattern].totalHalfBeats() * 0.5 / 4.0 - 1.0e-9);
        const juce::String name = "ChordMonkey " + juce::String (patternName (pattern)) + " " + juce::String (keyName (s.key)).upToFirstOccurrenceOf ("/", false, false)
                                + (chordsOnly ? " chords" : "") + " " + juce::String (bars) + (bars == 1 ? " bar" : " bars");
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChordMonkey Export")
                              .getChildFile (juce::File::createLegalFileName (name) + ".mid");
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return {};
        }
        return file;
    }

    juce::AudioProcessorEditor* ChordMonkeyProcessor::createEditor()
    {
        return new ChordMonkeyEditor (*this);
    }

    //==========================================================================
    // The workspace (PLAN.md §3.2), saved as one PATTERN child per pattern with its text
    // (patternToText):
    //
    //   <WORKSPACE version="1"><PATTERN index="0" text="0|0:0.4.7:8:0:0 …"/> … </WORKSPACE>

    Workspace ChordMonkeyProcessor::getWorkspace() const
    {
        const juce::ScopedLock sl (workspaceLock);
        return workspace;
    }

    void ChordMonkeyProcessor::setWorkspace (const Workspace& w)
    {
        {
            const juce::ScopedLock sl (workspaceLock);
            if (workspace == w)
                return;
            workspace = w;
            workspaceForAudio.publish (workspace);
        }
        notifyStateChanged();
    }

    bool ChordMonkeyProcessor::loadChordSet (const juce::String& progression, int pattern)
    {
        if (pattern < 0 || pattern >= numPatterns)
            return false;
        Pattern p;
        std::string error;
        if (! parseProgression (progression.toStdString(), snapshot.key(), p, error))
            return false;
        auto w = getWorkspace();
        w.patterns[(size_t) pattern] = p;
        setWorkspace (w);
        return true;
    }

    juce::ValueTree ChordMonkeyProcessor::workspaceToTree (const Workspace& w)
    {
        juce::ValueTree tree ("WORKSPACE");
        tree.setProperty ("version", 1, nullptr);
        for (int i = 0; i < numPatterns; ++i)
        {
            juce::ValueTree p ("PATTERN");
            p.setProperty ("index", i, nullptr);
            p.setProperty ("text", juce::String (patternToText (w.patterns[(size_t) i])), nullptr);
            tree.appendChild (p, nullptr);
        }
        return tree;
    }

    // Patterns missing or unreadable come back empty.
    Workspace ChordMonkeyProcessor::workspaceFromTree (const juce::ValueTree& tree)
    {
        Workspace w;
        for (const auto& p : tree)
        {
            const int i = (int) p.getProperty ("index", -1);
            if (p.hasType ("PATTERN") && i >= 0 && i < numPatterns)
                patternFromText (p.getProperty ("text").toString().toStdString(), w.patterns[(size_t) i]);
        }
        return w;
    }

    void ChordMonkeyProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = state.copyState();
        dropAnalyse (tree);
        tree.appendChild (uiState.createCopy(), nullptr);
        tree.appendChild (workspaceToTree (getWorkspace()), nullptr);
        tree.setProperty ("program", currentProgram.load(), nullptr);
        tree.setProperty ("presetName", presetName, nullptr);
        tree.setProperty ("presetCategory", presetCategory, nullptr);
        tree.setProperty ("version", JucePlugin_VersionString, nullptr);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void ChordMonkeyProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
        presetName = tree.getProperty ("presetName", presetName).toString();
        presetCategory = tree.getProperty ("presetCategory", presetCategory).toString();
        const auto ui = tree.getChildWithName (uiState.getType());
        restoringState = true;
        if (ui.isValid())
        {
            uiState.copyPropertiesAndChildrenFrom (ui, nullptr);
            tree.removeChild (ui, nullptr);
        }
        restoringState = false;

        // A state saved before Phase 1 has no workspace: it restores empty.
        const auto ws = tree.getChildWithName ("WORKSPACE");
        {
            const juce::ScopedLock sl (workspaceLock);
            workspace = workspaceFromTree (ws);
            workspaceForAudio.publish (workspace);
        }
        tree.removeChild (ws, nullptr);
        dropNonFiniteValues (tree);
        dropAnalyse (tree);

        pendingProgram = -1;              // a restore replaces a program change still waiting
        state.replaceState (tree);

        // APVTS leaves a parameter alone when its value rounds to the saved one (a bool a host
        // set to 0.3 is already "off"), so the host's raw value would survive the restore:
        // set each saved one exactly. (APVTS adds a child without a value for a parameter the
        // state doesn't have; that one keeps its value.)
        for (auto* p : getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto child = tree.getChildWithProperty ("id", param->paramID); child.hasProperty ("value"))
                    if (const float v = param->convertTo0to1 ((float) (double) child.getProperty ("value")); std::abs (param->getValue() - v) > 1.0e-6f)
                        param->setValueNotifyingHost (v);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chordmonkey::ChordMonkeyProcessor();
}
