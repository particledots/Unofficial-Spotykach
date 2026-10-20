// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "engine/Engine.h"

class UnofficialSpotykachProcessor : public juce::AudioProcessor
{
public:
    UnofficialSpotykachProcessor();
    ~UnofficialSpotykachProcessor() override = default;

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

    const usp::EngineStatus& engineStatus() const { return engine.status(); }

    // Read by the editor on the message thread. Written only by the audio thread.
    std::atomic<float>    inputPeak       { 0.0f };
    std::atomic<uint32_t> midiNoteOnCount { 0 };
    std::atomic<uint32_t> midiEventCount  { 0 };
    std::atomic<int>      lastNote        { -1 };
    std::atomic<double>   hostSampleRate  { 0.0 };
    std::atomic<int>      hostBlockSize   { 0 };

    juce::AudioProcessorValueTreeState apvts;

    // Mapping of Reel-mode pitch (0..1) to playback speed, as in the firmware (generator.cpp mapped_speed()).
    static float pitchToSpeed (float v) { return v < 0.5f ? 2.0f * v : 1.0f + (v - 0.5f) * 6.0f; }

    // Parameter id for a deck's control: Deck A keeps the original ids (so saved sessions keep working),
    // Deck B uses a "b_" prefix.
    static juce::String deckId (int deck, const juce::String& base) { return deck == 0 ? base : "b_" + base; }

    static constexpr int kNumDeckButtons = 5;   // record, play, reverse, trig, recInternal
    static const char* deckButtonBase (int i)
    {
        static const char* names[kNumDeckButtons] = { "record", "play", "reverse", "trig", "recInternal" };
        return names[i];
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    usp::Engine engine;

    struct DeckPtrs
    {
        std::atomic<float>* position = nullptr; std::atomic<float>* size = nullptr; std::atomic<float>* pitch = nullptr;
        std::atomic<float>* envelope = nullptr; std::atomic<float>* mix = nullptr;  std::atomic<float>* feedback = nullptr;
        std::atomic<float>* button[kNumDeckButtons] = {};
        bool was[kNumDeckButtons] = {};            // audio thread only
    };
    DeckPtrs decks[2];
    std::atomic<float>* pCrossfade = nullptr;
    std::atomic<float>* pRoute = nullptr;
    std::atomic<float>* pPanSpeed = nullptr;
    std::atomic<float>* pPanRange = nullptr;
    std::atomic<float>* pOutLevel = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachProcessor)
};
