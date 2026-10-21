// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <deque>
#include <functional>
#include "engine/Engine.h"
#include "engine/LoopLibrary.h"

class UnofficialSpotykachProcessor : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    UnofficialSpotykachProcessor();
    ~UnofficialSpotykachProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Last tempo/clock status published by the engine, for the window.
    const usp::EngineStatus& engineStatus() const { return engine.status(); }

    // Read by the editor on the message thread. Written only by the audio thread.
    std::atomic<float>    inputPeak       { 0.0f };
    std::atomic<uint32_t> midiNoteOnCount { 0 };
    std::atomic<uint32_t> midiEventCount  { 0 };
    std::atomic<int>      lastNote        { -1 };
    std::atomic<int>      lastNoteChannel { 0 };
    std::atomic<int>      lastCc          { -1 };
    std::atomic<int>      lastCcValue     { 0 };
    std::atomic<int>      lastCcChannel   { 0 };
    std::atomic<double>   hostSampleRate  { 0.0 };
    std::atomic<int>      hostBlockSize   { 0 };

    // Message thread: moves parameters changed by MIDI (and the Fit tempo result) into the real parameters.
    // Called by the 30 Hz timer; public so the headless test can call it without a message loop.
    void applyPendingMidi();

    // ---- Loop files (message thread). A deck's loop can be loaded from a WAV file or saved to one. The project
    // keeps only a link to the file, never the audio (decisions D36 and D53). Nothing is saved automatically.
    enum class FileState { empty, unsaved, linked, loading, saving, missing };
    struct FileInfo
    {
        FileState state = FileState::empty;
        juce::String name;      // file name, when there is a link
        juce::String note;      // one line for the window
        bool isError = false;   // the note reports something that went wrong
    };
    FileInfo deckFileInfo (int deck) const;
    void loadLoopFile (int deck, const juce::File& file);
    void saveLoopFile (int deck, const juce::File& file);
    juce::File homeFolder() const;
    void setHomeFolder (const juce::File& folder);
    static juce::File defaultHomeFolder();
    void pollLoopFiles();             // 30 Hz timer work; public so the headless test can call it
    bool loopFilesBusy() const;       // a load or save is still running

    juce::AudioProcessorValueTreeState apvts;

    // Mapping of Reel-mode pitch (0..1) to playback speed, as in the firmware (generator.cpp mapped_speed()).
    static float pitchToSpeed (float v) { return v < 0.5f ? 2.0f * v : 1.0f + (v - 0.5f) * 6.0f; }

    // Parameter id for a deck's control: Deck A keeps the original ids (so saved sessions keep working),
    // Deck B uses a "b_" prefix.
    static juce::String deckId (int deck, const juce::String& base) { return deck == 0 ? base : "b_" + base; }

    static constexpr int kNumDeckButtons = 8;   // record, play, reverse, trig, recInternal, seqArm, seqClear, fit
    static constexpr int kRowButtons = 7;       // the first seven sit in the deck's button row; Fit tempo is on the setup page
    static const char* deckButtonBase (int i)
    {
        static const char* names[kNumDeckButtons] = { "record", "play", "reverse", "trig", "recInternal", "seqArm", "seqClear", "fit" };
        return names[i];
    }

    // Modulator parameter id: "modA_cycle", "modB_glow", ...
    static juce::String modId (int mod, const juce::String& base) { return juce::String (mod == 0 ? "modA_" : "modB_") + base; }
    static const juce::StringArray& modTargetNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;      // message thread: MIDI-moved parameters -> host, Fit tempo result -> Tempo

    usp::Engine engine;

    // Loop files: everything below is touched on the message thread only (the worker thread gets copies).
    struct DeckFile
    {
        FileState state = FileState::empty;
        usp::LoopLink link;               // kept while the project should remember the file (linked or missing)
        juce::String name, note;
        bool isError = false;
        uint32_t linkedSeq = 0;           // the deck's contentSeq when the file and the loop were last the same
        uint32_t gen = 0;                 // operation number; results of older operations are ignored
        uint32_t startedMs = 0;
        // an operation in flight
        std::shared_ptr<usp::LoopTransfer> queuedLoad;     // decoded audio waiting to be handed to the engine
        bool loadSubmitted = false, saveSubmitted = false;
        uint32_t loadGen = 0, saveGen = 0;                 // operation numbers of the transfers handed to the engine
        uint32_t saveSnapshotSeq = 0;
        usp::LoopLink pendLink;
        juce::String pendName, pendNote;
        juce::String saveTarget;
        FileState backupState = FileState::empty;
        usp::LoopLink backupLink; juce::String backupName, backupNote; uint32_t backupSeq = 0;
    };
    class Worker;
    std::unique_ptr<Worker> worker;
    mutable juce::CriticalSection fileLock;
    DeckFile files[2];
    juce::String homePath;
    std::deque<std::function<void()>> uiQueue;      // results from the worker thread, run by the timer
    juce::CriticalSection uiLock;
    void postUi (std::function<void()>);
    void beginOp (int deck, FileState state, const juce::String& note);
    void failOp (int deck, uint32_t gen, const juce::String& message);
    void startRestore (int deck, const usp::LoopLink& link);
    void queueDecoded (int deck, uint32_t gen, std::vector<uint8_t>&& bytes, const usp::LoopLink& link, const juce::String& prefixNote, bool isRestore);   // worker thread
    void markMissing (int deck, uint32_t gen, const usp::LoopLink& link, const juce::String& message);
    juce::File settingsFile() const;

    struct DeckPtrs
    {
        std::atomic<float>* mode = nullptr; std::atomic<float>* pitchSnap = nullptr;
        std::atomic<float>* fluxOn = nullptr; std::atomic<float>* fluxMode = nullptr; std::atomic<float>* fluxTime = nullptr;
        std::atomic<float>* fluxFeedback = nullptr; std::atomic<float>* fluxMix = nullptr;
        std::atomic<float>* gritOn = nullptr; std::atomic<float>* gritMode = nullptr; std::atomic<float>* gritIntensity = nullptr;
        std::atomic<float>* gritMix = nullptr; std::atomic<float>* gritCharacter = nullptr;
        std::atomic<float>* sliceOffset = nullptr; std::atomic<float>* envLength = nullptr; std::atomic<float>* window = nullptr;
        std::atomic<float>* position = nullptr; std::atomic<float>* size = nullptr; std::atomic<float>* pitch = nullptr;
        std::atomic<float>* envelope = nullptr; std::atomic<float>* mix = nullptr;  std::atomic<float>* feedback = nullptr;
        std::atomic<float>* inTrim = nullptr; std::atomic<float>* outTrim = nullptr;
        std::atomic<float>* cvPitch = nullptr; std::atomic<float>* cvMix = nullptr;
        std::atomic<float>* cvPosition = nullptr; std::atomic<float>* cvSize = nullptr;
        std::atomic<float>* transportFollow = nullptr; std::atomic<float>* midiChannel = nullptr; std::atomic<float>* fitBeats = nullptr;
        std::atomic<float>* button[kNumDeckButtons] = {};
        bool was[kNumDeckButtons] = {};            // audio thread only
    };
    DeckPtrs decks[2];
    struct ModPtrs { std::atomic<float>* type = nullptr; std::atomic<float>* cycle = nullptr; std::atomic<float>* sync = nullptr;
                     std::atomic<float>* glow = nullptr; std::atomic<float>* target = nullptr; };
    ModPtrs mods[2];
    std::atomic<float>* pCrossfade = nullptr;
    std::atomic<float>* pRoute = nullptr;
    std::atomic<float>* pPanSpeed = nullptr;
    std::atomic<float>* pPanRange = nullptr;
    std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pTempoSource = nullptr;
    std::atomic<float>* pQuantise = nullptr;
    std::atomic<float>* pClick = nullptr;
    std::atomic<float>* pOutLevel = nullptr;
    std::atomic<float>* pCvCrossfade = nullptr;

    // MIDI CC -> parameter. The audio thread writes the value straight into the parameter's processing value (so it
    // is heard at once) and marks the slot dirty; the message thread (timerCallback) then moves the real parameter
    // so the window, the host and the saved state follow. Until it has, the audio thread re-asserts the value each
    // block. Slot [0] = Deck A, [1] = Deck B, [2] = global (crossfade).
    struct MidiSlot
    {
        std::atomic<float> latest { 0.0f };
        std::atomic<uint32_t> seq { 0 }, applied { 0 };
    };
    MidiSlot ccSlots[3][128];
    std::atomic<float>* ccRaw[3][128] = {};
    juce::RangedAudioParameter* ccParam[3][128] = {};
    bool ccIsBool[3][128] = {};
    uint32_t lastFitSeq = 0;           // message thread
    bool hostWasPlaying = false;       // audio thread
    bool hostKnown = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachProcessor)
};
