// Unofficial Spotykach - Milestone 1 shell.
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Licensed under the GNU AGPLv3 (see LICENSE). Third-party notices: THIRD-PARTY-LICENSES.txt
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

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

    // Read by the editor on the message thread. Written only by the audio thread.
    std::atomic<float>    inputPeak       { 0.0f };   // peak of the input since the editor last reset it
    std::atomic<uint32_t> midiNoteOnCount { 0 };
    std::atomic<uint32_t> midiEventCount  { 0 };
    std::atomic<int>      lastNote        { -1 };
    std::atomic<double>   hostSampleRate  { 0.0 };
    std::atomic<int>      hostBlockSize   { 0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* outLevelDb = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachProcessor)
};
