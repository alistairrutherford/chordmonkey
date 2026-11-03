// The plug-in: a thin JUCE adapter around ChordEngine (Source/core/engine).
//
// Two builds from ChordMonkey.jucer (ChordChimp's PLAN.md §4.3): the VST3 instrument and the Standalone app.
// Generated MIDI goes to the host's MIDI output (in Live: another track's "MIDI From"). The AU
// builds and the CoreMIDI virtual port were removed on 2026-10-20.
//
// Presets (PresetManager) load on the message thread, and the factory presets are also the
// plug-in's programs (program 0 = Init), which a host may change from another thread: that load
// waits for the timer.
//
// An ExportRecorder keeps what ChordMonkey sends while the host plays, for the Export drag-out
// (ChordChimp's PLAN.md §11, Phase 6).
//
// Key detection (ChordChimp's docs/KEY_DETECTION_PLAN.md): in Analyse mode the engine guesses the input's
// key; the guess is published as one packed atomic (keyGuess). Using it (applyGuess) sets Key
// and Scale on the message thread: from the editor's click, or from the timer when the host has
// set Analyse to Use guess. Analyse isn't saved with the state.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/ChordEngine.h"
#include "../core/engine/SnapshotExchange.h"
#include "../core/midiio/ExportRecorder.h"
#include "../core/model/Workspace.h"
#include "Parameters.h"
#include "PresetManager.h"
#include <atomic>

namespace chordmonkey
{
    // What the chord keys play now, for the editor (published by the audio thread).
    struct ChordDisplay
    {
        ChordSet chords {};
        ChordContext context;
        int lastSlot = -1;

        bool operator== (const ChordDisplay& o) const noexcept
        {
            return chords == o.chords && context.key == o.context.key && context.scale == o.context.scale
                && context.outputOctave == o.context.outputOctave && lastSlot == o.lastSlot;
        }
    };

    class ChordMonkeyProcessor : public juce::AudioProcessor,
                              private juce::Timer,
                              private juce::ValueTree::Listener
    {
    public:
        ChordMonkeyProcessor();
        ~ChordMonkeyProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        void reset() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;
        using AudioProcessor::processBlockBypassed;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        // The factory presets (Init first).
        int getNumPrograms() override;
        int getCurrentProgram() override { return currentProgram.load(); }
        void setCurrentProgram (int) override;
        const juce::String getProgramName (int) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState state;

        // Message thread, one reader (the editor).
        const ChordDisplay& readDisplay() noexcept { return display.read(); }

        // The engine settings the parameters give now (any thread; lock-free reads).
        EngineSettings currentSettings() const noexcept { return snapshot.engineSettings(); }

        // On-screen keys (Phase 5): the editor's keyboards play through the engine like host
        // MIDI input. Message thread only; lock-free, read by the next audio block.
        void pushUiNote (int note, int velocity, bool on) noexcept;

        // Key and note states for the editor (written by the audio thread each block): an
        // output note sounds; an input key is down; a key's performance plays (held or latched).
        bool isNoteSounding (int note) const noexcept { return testBit (sounding, note); }
        bool isKeyDown (int note) const noexcept { return testBit (keysDown, note); }
        bool isPerforming (int note) const noexcept { return testBit (performing, note); }

        // Editor preferences saved with the plug-in (page shown, keyboard shown). Message thread.
        juce::ValueTree uiState { "UI" };

        // The workspace (PLAN.md §3.2): four patterns of chords. Not parameters; saved with the
        // state. Any thread except the audio thread (a lock; Phase 2 hands the audio thread a
        // copy). A change the host should save marks the state changed.
        Workspace getWorkspace() const;
        void setWorkspace (const Workspace&);

        //==========================================================================
        // Presets (message thread). applyPreset sets every preset parameter (the preset's value,
        // or the default if it has none), and ends every note.
        PresetManager presets { *this };
        void applyPreset (const juce::ValueTree& preset, const juce::String& category, const juce::String& name);
        juce::ValueTree presetState();
        const juce::String& getPresetName() const noexcept { return presetName; }
        const juce::String& getPresetCategory() const noexcept { return presetCategory; }
        void setPresetIdentity (const juce::String& category, const juce::String& name);

        // Export (message thread): the last ≤ 32 completed bars, written to a temporary .mid
        // for a drag. An invalid File (and `error`) if there's nothing to export yet.
        TakeInfo exportInfo() const noexcept { return recorder.info(); }
        juce::File writeExport (juce::String& error);

        // While the Test pattern is on, each finished take is also written here (for checks in
        // Live through the bridge).
        static juce::File testTakeFile();

        // Key detection. The guess (written by the audio thread each block; any thread reads it).
        // applyGuess (message thread): with a guess, Key = its tonic, Scale = scaleForGuess and
        // Analyse = Off, as one set of gestures; without one, Analyse = Listen.
        KeyGuess readKeyGuess() const noexcept { return KeyGuess::unpack (keyGuess.load (std::memory_order_relaxed)); }
        params::AnalyseMode analyseMode() const noexcept { return snapshot.analyseMode(); }
        void applyGuess();

        // While the Test pattern is on, the guess is also written here when it changes (for the
        // bridge's checks in Live).
        static juce::File heardKeyFile();

        // Status for the editor and tests (written by the audio thread).
        std::atomic<int> lastChordSlot { -1 };
        std::atomic<int> soundingNotes { 0 };
        std::atomic<bool> transportPlaying { false };
        std::atomic<uint64_t> keyGuess { KeyGuess().pack() };

    private:
        using NoteBits = std::array<std::atomic<uint64_t>, 2>;
        static bool testBit (const NoteBits& b, int note) noexcept
        {
            return note >= 0 && note < 128 && ((b[(size_t) (note >> 6)].load (std::memory_order_relaxed) >> (note & 63)) & 1u) != 0;
        }
        void publishNoteStates() noexcept;

        // The UI's preferences and the preset's name aren't parameters: Live only saves them in
        // a set if the plug-in says its state changed (and not for a restore).
        void notifyStateChanged();
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { notifyStateChanged(); }
        std::atomic<bool> restoringState { false };

        static BusesProperties makeBuses();
        void timerCallback() override;
        void run (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
        Transport readTransport();

        params::Snapshot snapshot;
        ChordEngine engine;

        // Preallocated in the constructor / prepareToPlay; never resized in processBlock.
        EventBuffer<2048> input;
        static constexpr int inputReserve = 256;   // slots only key-ups may take (Phase 8)
        std::array<MidiEvent, 256> uiNotes {};
        juce::AbstractFifo uiFifo { 256 };
        NoteBits sounding {}, keysDown {}, performing {};
        BlockEvents output;
        juce::MidiBuffer generated;

        std::atomic<double> preparedSampleRate { 44100.0 };
        Transport lastTransport;
        SnapshotExchange<ChordDisplay> display;
        ChordDisplay lastDisplay;

        ExportRecorder recorder;
        bool writeTake (const juce::File& file, juce::String& error) const;
        uint32_t testTakeWritten = 0;
        uint64_t heardKeyWritten = 0;
        int heardModeWritten = -1;
        void writeHeardKey();
        juce::String presetName { "Init" }, presetCategory { "Init" };
        mutable juce::CriticalSection workspaceLock;
        Workspace workspace;
        static juce::ValueTree workspaceToTree (const Workspace&);
        static Workspace workspaceFromTree (const juce::ValueTree&);
        std::atomic<int> currentProgram { 0 }, pendingProgram { -1 };
        void loadProgram (int index);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordMonkeyProcessor)
    };
}
