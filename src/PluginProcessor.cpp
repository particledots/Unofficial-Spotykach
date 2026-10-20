// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Attrs = juce::AudioParameterFloatAttributes;

    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
}

UnofficialSpotykachProcessor::UnofficialSpotykachProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pPosition = apvts.getRawParameterValue ("position");
    pSize     = apvts.getRawParameterValue ("size");
    pPitch    = apvts.getRawParameterValue ("pitch");
    pEnvelope = apvts.getRawParameterValue ("envelope");
    pMix      = apvts.getRawParameterValue ("mix");
    pFeedback = apvts.getRawParameterValue ("feedback");
    pRecord   = apvts.getRawParameterValue ("record");
    pPlay     = apvts.getRawParameterValue ("play");
    pReverse  = apvts.getRawParameterValue ("reverse");
    pOutLevel = apvts.getRawParameterValue ("outLevel");
}

juce::AudioProcessorValueTreeState::ParameterLayout UnofficialSpotykachProcessor::createLayout()
{
    using juce::NormalisableRange;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Defaults are the firmware's (CoreUI::_init_values in src/ui/core.ui.cpp).
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "position", 1 }, "Position",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, Attrs().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "size", 1 }, "Size",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, Attrs().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "pitch", 1 }, "Pitch",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        Attrs().withStringFromValueFunction ([] (float v, int)
        {
            const float speed = UnofficialSpotykachProcessor::pitchToSpeed (v);
            if (speed < 0.005f) return juce::String ("stopped");
            const float st = 12.0f * std::log2 (speed);
            return juce::String (speed, 2) + "x  " + (st >= 0 ? "+" : "") + juce::String (st, 1) + " st";
        })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "envelope", 1 }, "Envelope",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        Attrs().withStringFromValueFunction ([] (float v, int) { return v < 0.02f ? juce::String ("off") : percentText (v, 0); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, Attrs().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.95f, Attrs().withStringFromValueFunction (percentText)));

    // Buttons. A press is a rising edge (0 -> 1); the editor sends 1 then 0, automation can do the same.
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "record", 1 },  "Record",  false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "play", 1 },    "Play",    false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "reverse", 1 }, "Reverse", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "outLevel", 1 }, "Output Level",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, Attrs().withLabel ("dB")));
    return layout;
}

void UnofficialSpotykachProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    hostBlockSize.store (samplesPerBlock);
    engine.prepare (sampleRate, samplesPerBlock);       // allocates; never called from the audio callback
    setLatencySamples (engine.latencySamples());
    outGain.reset (sampleRate, 0.02);                    // 20 ms ramp
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));
    wasRecord = pRecord->load() > 0.5f;
    wasPlay = pPlay->load() > 0.5f;
    wasReverse = pReverse->load() > 0.5f;
}

bool UnofficialSpotykachProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void UnofficialSpotykachProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI bookkeeping only for now (the firmware's MIDI map arrives in a later milestone).
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        midiEventCount.fetch_add (1, std::memory_order_relaxed);
        if (msg.isNoteOn())
        {
            midiNoteOnCount.fetch_add (1, std::memory_order_relaxed);
            lastNote.store (msg.getNoteNumber(), std::memory_order_relaxed);
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int numChans   = juce::jmin (buffer.getNumChannels(), 2);
    if (numChans == 0 || numSamples == 0) return;

    float peak = 0.0f;
    for (int c = 0; c < numChans; ++c)
        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, numSamples));
    if (peak > inputPeak.load (std::memory_order_relaxed))
        inputPeak.store (peak, std::memory_order_relaxed);

    // Button presses = rising edges.
    const bool rec = pRecord->load() > 0.5f, ply = pPlay->load() > 0.5f, rev = pReverse->load() > 0.5f;
    uint32_t actions = 0;
    if (rec && ! wasRecord) actions |= usp::kActionRecord;
    if (ply && ! wasPlay)   actions |= usp::kActionPlay;
    if (rev && ! wasReverse) actions |= usp::kActionReverse;
    wasRecord = rec; wasPlay = ply; wasReverse = rev;

    usp::EngineParams p;
    p.position = pPosition->load();
    p.size     = pSize->load();
    p.pitch    = pPitch->load();
    p.envelope = pEnvelope->load();
    p.mix      = pMix->load();
    p.feedback = pFeedback->load();

    float* left  = buffer.getWritePointer (0);
    float* right = numChans > 1 ? buffer.getWritePointer (1) : left;
    engine.process (left, right, left, right, numSamples, p, actions);

    // Output level, smoothed. At 0 dB the audio is untouched.
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutLevel->load()));
    if (outGain.isSmoothing() || outGain.getTargetValue() != 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < numChans; ++c)
                buffer.getWritePointer (c)[i] *= g;
        }
    }
}

juce::AudioProcessorEditor* UnofficialSpotykachProcessor::createEditor()
{
    return new UnofficialSpotykachEditor (*this);
}

void UnofficialSpotykachProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void UnofficialSpotykachProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UnofficialSpotykachProcessor();
}
