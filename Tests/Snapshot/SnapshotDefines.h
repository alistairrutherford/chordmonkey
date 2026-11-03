// The plug-in's defines for the snapshot app, which compiles Source/plugin and Source/ui outside
// a plug-in target (forced include, ChordMonkeySnapshot.jucer). Keep in step with ChordMonkey.jucer.
#pragma once
#define JucePlugin_Name "ChordMonkey"
#define JucePlugin_VersionString "0.1.0"

// The test runs the message loop between blocks (runDispatchLoopUntil).
#define JUCE_MODAL_LOOPS_PERMITTED 1
