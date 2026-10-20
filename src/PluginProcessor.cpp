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
    for (int d = 0; d < 2; ++d)
    {
        auto& p = decks[d];
        p.position = apvts.getRawParameterValue (deckId (d, "position"));
        p.size     = apvts.getRawParameterValue (deckId (d, "size"));
        p.pitch    = apvts.getRawParameterValue (deckId (d, "pitch"));
        p.envelope = apvts.getRawParameterValue (deckId (d, "envelope"));
        p.mix      = apvts.getRawParameterValue (deckId (d, "mix"));
        p.feedback = apvts.getRawParameterValue (deckId (d, "feedback"));
        for (int i = 0; i < kNumDeckButtons; ++i)
            p.button[i] = apvts.getRawParameterValue (deckId (d, deckButtonBase (i)));
    }
    pCrossfade = apvts.getRawParameterValue ("crossfade");
    pRoute     = apvts.getRawParameterValue ("route");
    pPanSpeed  = apvts.getRawParameterValue ("panSpeed");
    pPanRange  = apvts.getRawParameterValue ("panRange");
    pOutLevel  = apvts.getRawParameterValue ("outLevel");
}

juce::AudioProcessorValueTreeState::ParameterLayout UnofficialSpotykachProcessor::createLayout()
{
    using juce::NormalisableRange;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Defaults are the firmware's (CoreUI::_init_values in src/ui/core.ui.cpp).
    auto pitchText = [] (float v, int)
    {
        const float speed = UnofficialSpotykachProcessor::pitchToSpeed (v);
        if (speed < 0.005f) return juce::String ("stopped");
        const float st = 12.0f * std::log2 (speed);
        return juce::String (speed, 2) + "x  " + (st >= 0 ? "+" : "") + juce::String (st, 1) + " st";
    };
    auto envText = [] (float v, int) { return v < 0.02f ? juce::String ("off") : percentText (v, 0); };

    for (int d = 0; d < 2; ++d)
    {
        const juce::String tag = d == 0 ? "A " : "B ";
        auto id = [d] (const char* base) { return ParameterID { deckId (d, base), 1 }; };
        auto knob = [&] (const char* base, const char* name, float def, std::function<juce::String (float, int)> text)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (id (base), tag + name,
                NormalisableRange<float> (0.0f, 1.0f, 0.001f), def, Attrs().withStringFromValueFunction (text)));
        };
        knob ("position", "Position", 0.0f,  percentText);
        knob ("size",     "Size",     1.0f,  percentText);
        knob ("pitch",    "Pitch",    0.5f,  pitchText);
        knob ("envelope", "Envelope", 0.0f,  envText);
        knob ("mix",      "Mix",      0.5f,  percentText);
        knob ("feedback", "Feedback", 0.95f, percentText);

        // Buttons. A press is a rising edge (0 -> 1); the editor sends 1 then 0, automation can do the same.
        static const char* names[kNumDeckButtons] = { "Record", "Play", "Reverse", "Trig", "Record From Other Deck" };
        for (int i = 0; i < kNumDeckButtons; ++i)
            layout.add (std::make_unique<juce::AudioParameterBool> (id (deckButtonBase (i)), tag + names[i], false));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "crossfade", 1 }, "A/B Crossfade",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        Attrs().withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.01f ? juce::String ("A only") : v > 0.99f ? juce::String ("B only") : percentText (v, 0) + " toward B";
        })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "route", 1 }, "Routing",
        juce::StringArray { "Double Mono", "Stereo", "Generative Stereo" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "panSpeed", 1 }, "Pan Speed",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, Attrs().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "panRange", 1 }, "Pan Range",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f, Attrs().withStringFromValueFunction (percentText)));

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
    for (auto& d : decks)
        for (int i = 0; i < kNumDeckButtons; ++i)
            d.was[i] = d.button[i]->load() > 0.5f;
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
    static constexpr usp::EngineAction kButtonAction[kNumDeckButtons] =
        { usp::kActionRecord, usp::kActionPlay, usp::kActionReverse, usp::kActionTrig, usp::kActionRecordInternal };
    uint32_t actions = 0;
    usp::EngineParams p;
    for (int d = 0; d < 2; ++d)
    {
        auto& dp = decks[d];
        for (int i = 0; i < kNumDeckButtons; ++i)
        {
            const bool now = dp.button[i]->load() > 0.5f;
            if (now && ! dp.was[i]) actions |= usp::deckAction (d, kButtonAction[i]);
            dp.was[i] = now;
        }
        auto& t = p.deck[d];
        t.position = dp.position->load();
        t.size     = dp.size->load();
        t.pitch    = dp.pitch->load();
        t.envelope = dp.envelope->load();
        t.mix      = dp.mix->load();
        t.feedback = dp.feedback->load();
    }
    p.crossfade = pCrossfade->load();
    p.route     = juce::jlimit (0, 2, juce::roundToInt (pRoute->load()));
    p.panSpeed  = pPanSpeed->load();
    p.panRange  = pPanRange->load();

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
