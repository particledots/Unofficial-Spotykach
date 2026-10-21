// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/PitchMap.h"
#include "engine/LoopFile.h"

namespace
{
    using Attrs = juce::AudioParameterFloatAttributes;

    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
}

// One background thread for everything that reads or writes files or converts audio. The audio thread never
// waits for it and it never touches the engine; results come back through postUi() and the 30 Hz timer.
class UnofficialSpotykachProcessor::Worker : public juce::Thread
{
public:
    Worker() : juce::Thread ("Unofficial Spotykach files") { startThread(); }
    ~Worker() override { signalThreadShouldExit(); wake.signal(); stopThread (10000); }

    void add (std::function<void()> job)
    {
        { const juce::ScopedLock sl (lock); jobs.push_back (std::move (job)); }
        wake.signal();
    }
    void run() override
    {
        while (! threadShouldExit())
        {
            std::function<void()> job;
            {
                const juce::ScopedLock sl (lock);
                if (! jobs.empty()) { job = std::move (jobs.front()); jobs.pop_front(); }
            }
            if (job) job(); else wake.wait (500);
        }
    }
private:
    juce::CriticalSection lock;
    std::deque<std::function<void()>> jobs;
    juce::WaitableEvent wake;
};

UnofficialSpotykachProcessor::UnofficialSpotykachProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int d = 0; d < 2; ++d)
    {
        auto& p = decks[d];
        for (auto [ptr, name] : { std::pair { &p.fluxOn, "fluxOn" }, { &p.fluxMode, "fluxMode" }, { &p.fluxTime, "fluxTime" },
                                  { &p.fluxFeedback, "fluxFeedback" }, { &p.fluxMix, "fluxMix" }, { &p.gritOn, "gritOn" },
                                  { &p.gritMode, "gritMode" }, { &p.gritIntensity, "gritIntensity" }, { &p.gritMix, "gritMix" },
                                  { &p.gritCharacter, "gritCharacter" } })
            *ptr = apvts.getRawParameterValue (deckId (d, name));
        p.mode        = apvts.getRawParameterValue (deckId (d, "mode"));
        p.pitchSnap   = apvts.getRawParameterValue (deckId (d, "pitchSnap"));
        p.sliceOffset = apvts.getRawParameterValue (deckId (d, "sliceOffset"));
        p.envLength   = apvts.getRawParameterValue (deckId (d, "envLength"));
        p.window      = apvts.getRawParameterValue (deckId (d, "window"));
        p.position = apvts.getRawParameterValue (deckId (d, "position"));
        p.size     = apvts.getRawParameterValue (deckId (d, "size"));
        p.pitch    = apvts.getRawParameterValue (deckId (d, "pitch"));
        p.envelope = apvts.getRawParameterValue (deckId (d, "envelope"));
        p.mix      = apvts.getRawParameterValue (deckId (d, "mix"));
        p.feedback = apvts.getRawParameterValue (deckId (d, "feedback"));
        for (int i = 0; i < kNumDeckButtons; ++i)
            p.button[i] = apvts.getRawParameterValue (deckId (d, deckButtonBase (i)));
        p.inTrim = apvts.getRawParameterValue (deckId (d, "inTrim"));
        p.outTrim = apvts.getRawParameterValue (deckId (d, "outTrim"));
        p.cvPitch = apvts.getRawParameterValue (deckId (d, "cvPitch"));
        p.cvMix = apvts.getRawParameterValue (deckId (d, "cvMix"));
        p.cvPosition = apvts.getRawParameterValue (deckId (d, "cvPosition"));
        p.cvSize = apvts.getRawParameterValue (deckId (d, "cvSize"));
        p.transportFollow = apvts.getRawParameterValue (deckId (d, "transportFollow"));
        p.midiChannel = apvts.getRawParameterValue (deckId (d, "midiChannel"));
        p.fitBeats = apvts.getRawParameterValue (deckId (d, "fitBeats"));
    }
    pCvCrossfade = apvts.getRawParameterValue ("cvCrossfade");

    // The firmware's CC map (src/core/config.h enum CC, handled in CoreUI::_on_midi_cc). CC 14/15/85/86 are
    // buttons (handled in processBlock); the rest move a parameter. Per deck, on that deck's MIDI channel.
    {
        static const struct { int cc; const char* base; bool mod; } kDeckCc[] = {
            { 20, "position", false }, { 21, "sliceOffset", false }, { 22, "size", false }, { 23, "envelope", false },
            { 24, "pitch", false }, { 25, "mix", false }, { 26, "feedback", false }, { 27, "envLength", false },
            { 28, "window", false }, { 89, "cycle", true }, { 90, "glow", true },
            { 102, "gritOn", false }, { 103, "gritIntensity", false }, { 104, "gritMix", false },
            { 105, "fluxOn", false }, { 106, "fluxTime", false }, { 107, "fluxFeedback", false }, { 108, "fluxMix", false },
            { 109, "gritCharacter", false } };
        auto bind = [this] (int slot, int cc, const juce::String& id)
        {
            ccRaw[slot][cc] = apvts.getRawParameterValue (id);
            ccParam[slot][cc] = apvts.getParameter (id);
            ccIsBool[slot][cc] = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id)) != nullptr;
        };
        for (int d = 0; d < 2; ++d)
            for (const auto& e : kDeckCc)
                bind (d, e.cc, e.mod ? modId (d, e.base) : deckId (d, e.base));
        bind (2, 3, "crossfade");   // CC 3: crossfade, on any channel
    }
    // Home folder for loop files: a per-computer setting (not stored in projects).
    homePath = defaultHomeFolder().getFullPathName();
    {
        const auto f = settingsFile();
        if (f.existsAsFile())
        {
            const auto saved = f.loadFileAsString().trim();
            if (saved.isNotEmpty()) homePath = saved;
        }
    }
    worker = std::make_unique<Worker>();
    startTimerHz (30);
    for (int m = 0; m < 2; ++m)
    {
        mods[m].type   = apvts.getRawParameterValue (modId (m, "type"));
        mods[m].cycle  = apvts.getRawParameterValue (modId (m, "cycle"));
        mods[m].sync   = apvts.getRawParameterValue (modId (m, "sync"));
        mods[m].glow   = apvts.getRawParameterValue (modId (m, "glow"));
        mods[m].target = apvts.getRawParameterValue (modId (m, "target"));
    }
    pCrossfade = apvts.getRawParameterValue ("crossfade");
    pRoute     = apvts.getRawParameterValue ("route");
    pPanSpeed  = apvts.getRawParameterValue ("panSpeed");
    pPanRange  = apvts.getRawParameterValue ("panRange");
    pTempo       = apvts.getRawParameterValue ("tempo");
    pTempoSource = apvts.getRawParameterValue ("tempoSource");
    pQuantise    = apvts.getRawParameterValue ("quantise");
    pClick       = apvts.getRawParameterValue ("click");
    pOutLevel  = apvts.getRawParameterValue ("outLevel");
}

const juce::StringArray& UnofficialSpotykachProcessor::modTargetNames()
{
    static const juce::StringArray names { "None",
        "A Position", "A Size", "A Position + Size", "A Pitch", "A Mix",
        "B Position", "B Size", "B Position + Size", "B Pitch", "B Mix",
        "A/B Crossfade" };
    return names;
}

UnofficialSpotykachProcessor::~UnofficialSpotykachProcessor()
{
    stopTimer();
    worker.reset();          // finishes the job in progress; jobs still queued are dropped
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
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("mode"), tag + "Mode",
            juce::StringArray { "Reel", "Slice", "Drift" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("pitchSnap"), tag + "Pitch Snap",
            juce::StringArray { "Off", "Steps (octaves, 5ths)", "Semitones" }, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("fluxOn"), tag + "Flux On", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("fluxMode"), tag + "Flux Mode",
            juce::StringArray { "Free", "Clocked" }, 0));
        knob ("fluxTime",     "Flux Time",     0.5f, percentText);
        knob ("fluxFeedback", "Flux Feedback", 0.5f, percentText);
        knob ("fluxMix",      "Flux Mix",      0.7f, percentText);
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("gritOn"), tag + "Grit On", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("gritMode"), tag + "Grit Mode",
            juce::StringArray { "Drive", "Reduce", "Filter" }, 0));
        knob ("gritIntensity", "Grit Intensity", 0.2f,  percentText);
        knob ("gritMix",       "Grit Mix",       0.33f, percentText);
        knob ("gritCharacter", "Grit Character", 0.0f,  percentText);
        knob ("sliceOffset", "Slice Offset", 0.0f, percentText);
        knob ("envLength",   "Envelope Length", 1.0f, percentText);
        knob ("window",      "Window", 0.2f, percentText);
        knob ("position", "Position", 0.0f,  percentText);
        knob ("size",     "Size",     1.0f,  percentText);
        knob ("pitch",    "Pitch",    0.5f,  pitchText);
        knob ("envelope", "Envelope", 0.0f,  envText);
        knob ("mix",      "Mix",      0.5f,  percentText);
        knob ("feedback", "Feedback", 0.95f, percentText);

        // Trims (plug-in addition; the hardware has analog trimmers).
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("inTrim"), tag + "Input Trim",
            NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, Attrs().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("outTrim"), tag + "Output Trim",
            NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, Attrs().withLabel ("dB")));

        // CV-style inputs (the firmware's CV jacks), MIDI channel, transport follow, fit tempo.
        auto cv = [&] (const char* base, const char* name, float lo, float hi, std::function<juce::String (float, int)> text)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (id (base), tag + name,
                NormalisableRange<float> (lo, hi, 0.001f), 0.0f, Attrs().withStringFromValueFunction (text)));
        };
        cv ("cvPitch",    "V/Oct CV",      -5.0f, 5.0f, [] (float v, int) { return (v >= 0 ? "+" : "") + juce::String (v, 2) + " oct"; });
        cv ("cvMix",      "Mix CV",        -1.0f, 1.0f, [] (float v, int) { return (v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
        cv ("cvPosition", "Position CV",   -1.0f, 1.0f, [] (float v, int) { return (v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
        cv ("cvSize",     "Size CV",       -1.0f, 1.0f, [] (float v, int) { return (v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
        {
            juce::StringArray ch;
            ch.add ("Off");
            for (int i = 1; i <= 16; ++i) ch.add (juce::String (i));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id ("midiChannel"), tag + "MIDI Channel", ch, d == 0 ? 1 : 2));
        }
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("transportFollow"), tag + "Follow Transport", false));
        layout.add (std::make_unique<juce::AudioParameterInt> (id ("fitBeats"), tag + "Fit Tempo Beats", 1, 16, 4));

        // Buttons. A press is a rising edge (0 -> 1); the editor sends 1 then 0, automation can do the same.
        static const char* names[kNumDeckButtons] = { "Record", "Play", "Reverse", "Trig", "Record From Other Deck",
                                                      "Sequencer Arm/Stop", "Sequencer Clear", "Fit Tempo To Loop" };
        for (int i = 0; i < kNumDeckButtons; ++i)
            layout.add (std::make_unique<juce::AudioParameterBool> (id (deckButtonBase (i)), tag + names[i], false));
    }

    // Modulators. Defaults: firmware cycle 0.3; Glow 0.5 and Target "None" are mine (the firmware default Glow is 0).
    for (int m = 0; m < 2; ++m)
    {
        const juce::String tag = m == 0 ? "Mod A " : "Mod B ";
        auto id = [m] (const char* base) { return ParameterID { modId (m, base), 1 }; };
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("type"), tag + "Type",
            juce::StringArray { "Follow", "Sine", "Saw", "Square", "Random" }, 1));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("cycle"), tag + "Cycle",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, Attrs().withStringFromValueFunction (percentText)));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("sync"), tag + "Sync", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("glow"), tag + "Glow",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, Attrs().withStringFromValueFunction (percentText)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("target"), tag + "Target", modTargetNames(), 0));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "cvCrossfade", 1 }, "Crossfade CV",
        NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
        Attrs().withStringFromValueFunction ([] (float v, int) { return (v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "crossfade", 1 }, "A/B Crossfade",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        Attrs().withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.01f ? juce::String ("A only") : v > 0.99f ? juce::String ("B only") : percentText (v, 0) + " toward B";
        })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "route", 1 }, "Routing",
        juce::StringArray { "Double Mono", "Stereo", "Generative Stereo" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo",
        NormalisableRange<float> (20.0f, 250.0f, 0.1f), 120.0f, Attrs().withLabel ("BPM")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "tempoSource", 1 }, "Tempo Source",
        juce::StringArray { "Internal", "Host" }, 1));
    {
        juce::StringArray q;
        q.add ("1/16");
        for (int i = 1; i <= 16; ++i) q.add (juce::String (i) + "/4");     // quarter notes, as in the firmware list
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "quantise", 1 }, "Launch Quantise", q, 1));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "click", 1 }, "Click Level",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, Attrs().withStringFromValueFunction (percentText)));
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
    hostKnown = false;
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

    const int numSamples = buffer.getNumSamples();
    const int numChans   = juce::jmin (buffer.getNumChannels(), 2);
    if (numChans == 0 || numSamples == 0) return;

    float peak = 0.0f;
    for (int c = 0; c < numChans; ++c)
        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, numSamples));
    if (peak > inputPeak.load (std::memory_order_relaxed))
        inputPeak.store (peak, std::memory_order_relaxed);

    // MIDI-moved parameters the message thread has not yet applied: keep asserting the value.
    for (int sl = 0; sl < 3; ++sl)
        for (int cc = 0; cc < 128; ++cc)
            if (ccRaw[sl][cc] != nullptr && ccSlots[sl][cc].seq.load (std::memory_order_acquire) != ccSlots[sl][cc].applied.load (std::memory_order_acquire))
                ccRaw[sl][cc]->store (ccSlots[sl][cc].latest.load (std::memory_order_relaxed), std::memory_order_relaxed);

    // Button presses = rising edges.
    static constexpr usp::EngineAction kButtonAction[kNumDeckButtons] =
        { usp::kActionRecord, usp::kActionPlay, usp::kActionReverse, usp::kActionTrig, usp::kActionRecordInternal,
          usp::kActionSeqArm, usp::kActionSeqClear, usp::kActionFitTempo };
    uint32_t actions = 0;
    for (int d = 0; d < 2; ++d)
    {
        auto& dp = decks[d];
        for (int i = 0; i < kNumDeckButtons; ++i)
        {
            const bool now = dp.button[i]->load() > 0.5f;
            if (now && ! dp.was[i]) actions |= usp::deckAction (d, kButtonAction[i]);
            dp.was[i] = now;
        }
    }

    usp::HostTransport host;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            host.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())         { host.hasTempo = *bpm >= 20.0 && *bpm <= 400.0; host.bpm = *bpm; }
            if (auto ppq = pos->getPpqPosition()) { host.hasPpq = true; host.ppq = *ppq; }
        }
    }
    // Host transport start/stop plays/stops the decks that have Follow Transport on (the firmware does this with
    // MIDI Start/Stop; whether Logic sends those to a plug-in is unverified, so the host's own state is used as well).
    if (hostKnown && host.playing != hostWasPlaying)
        for (int d = 0; d < 2; ++d)
            if (decks[d].transportFollow->load() > 0.5f)
                actions |= usp::deckAction (d, host.playing ? usp::kActionTransportStart : usp::kActionTransportStop);
    hostWasPlaying = host.playing;
    hostKnown = true;

    float* left  = buffer.getWritePointer (0);
    float* right = numChans > 1 ? buffer.getWritePointer (1) : left;

    auto buildParams = [this]
    {
        usp::EngineParams p;
        for (int d = 0; d < 2; ++d)
        {
            auto& dp = decks[d];
            auto& t = p.deck[d];
            t.position = dp.position->load();
            t.size     = dp.size->load();
            t.pitch    = dp.pitch->load();
            t.envelope = dp.envelope->load();
            t.mix      = dp.mix->load();
            t.feedback = dp.feedback->load();
            t.mode        = juce::jlimit (0, 2, juce::roundToInt (dp.mode->load()));
            t.pitchSnap   = juce::jlimit (0, 2, juce::roundToInt (dp.pitchSnap->load()));
            t.sliceOffset = dp.sliceOffset->load();
            t.envLength   = dp.envLength->load();
            t.window      = dp.window->load();
            t.fluxOn        = dp.fluxOn->load() > 0.5f;
            t.fluxMode      = juce::jlimit (0, 1, juce::roundToInt (dp.fluxMode->load()));
            t.fluxTime      = dp.fluxTime->load();
            t.fluxFeedback  = dp.fluxFeedback->load();
            t.fluxMix       = dp.fluxMix->load();
            t.gritOn        = dp.gritOn->load() > 0.5f;
            t.gritMode      = juce::jlimit (0, 2, juce::roundToInt (dp.gritMode->load()));
            t.gritIntensity = dp.gritIntensity->load();
            t.gritMix       = dp.gritMix->load();
            t.gritCharacter = dp.gritCharacter->load();
            t.inTrim     = juce::Decibels::decibelsToGain (dp.inTrim->load());
            t.outTrim    = juce::Decibels::decibelsToGain (dp.outTrim->load());
            t.cvPitch    = dp.cvPitch->load();
            t.cvMix      = dp.cvMix->load();
            t.cvPosition = dp.cvPosition->load();
            t.cvSize     = dp.cvSize->load();
            t.fitBeats   = juce::jlimit (1, 16, juce::roundToInt (dp.fitBeats->load()));
        }
        for (int m = 0; m < 2; ++m)
        {
            auto& mp = p.mod[m];
            mp.type   = juce::jlimit (0, 4, juce::roundToInt (mods[m].type->load()));
            mp.cycle  = mods[m].cycle->load();
            mp.sync   = mods[m].sync->load() > 0.5f;
            mp.glow   = mods[m].glow->load();
            mp.target = juce::jlimit (0, usp::kModTargetCount - 1, juce::roundToInt (mods[m].target->load()));
        }
        p.tempoBpm  = pTempo->load();
        p.hostSync  = juce::roundToInt (pTempoSource->load()) == 1;
        p.launchQuantise = juce::jlimit (0, 16, juce::roundToInt (pQuantise->load()));
        p.clickLevel = pClick->load();
        p.crossfade = pCrossfade->load();
        p.cvCrossfade = pCvCrossfade->load();
        p.route     = juce::jlimit (0, 2, juce::roundToInt (pRoute->load()));
        p.panSpeed  = pPanSpeed->load();
        p.panRange  = pPanRange->load();
        return p;
    };

    int notes[2] = { -1, -1 };
    int done = 0;
    auto render = [&] (int upTo)
    {
        const int len = upTo - done;
        const auto p = buildParams();
        // The host position is for the start of the block; move it on to the start of this piece.
        auto h = host;
        if (h.hasPpq && h.playing && getSampleRate() > 0.0) h.ppq += (double) done / getSampleRate() * h.bpm / 60.0;
        engine.process (left + done, right + done, left + done, right + done, len, p, actions, h, notes);
        actions = 0;
        notes[0] = notes[1] = -1;
        done = upTo;
    };

    // MIDI: the audio is rendered in pieces so each message takes effect at its own sample position.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        midiEventCount.fetch_add (1, std::memory_order_relaxed);
        const int pos = juce::jlimit (done, numSamples, metadata.samplePosition);
        const int ch = msg.getChannel();                       // 1..16, 0 for system messages
        auto deckOnChannel = [&] (int c) -> int
        {
            if (c < 1) return -1;
            for (int d = 0; d < 2; ++d)
                if (juce::roundToInt (decks[d].midiChannel->load()) == c) return d;   // Deck A wins if both share a channel
            return -1;
        };

        if (msg.isNoteOn())
        {
            midiNoteOnCount.fetch_add (1, std::memory_order_relaxed);
            lastNote.store (msg.getNoteNumber(), std::memory_order_relaxed);
            lastNoteChannel.store (ch, std::memory_order_relaxed);
            const int d = deckOnChannel (ch);
            if (d >= 0)
            {
                if (pos > done || notes[d] >= 0) render (pos);      // a second note for the same deck starts a new piece
                notes[d] = msg.getNoteNumber();
            }
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber(), val = msg.getControllerValue();
            lastCc.store (cc, std::memory_order_relaxed);
            lastCcValue.store (val, std::memory_order_relaxed);
            lastCcChannel.store (ch, std::memory_order_relaxed);
            const int d = deckOnChannel (ch);
            // Buttons (the firmware acts on values above 0)
            const struct { int cc; usp::EngineAction a; } kButtonCc[] = { { 14, usp::kActionRecord }, { 15, usp::kActionRecordInternal },
                                                                         { 85, usp::kActionPlay }, { 86, usp::kActionReverse } };
            for (const auto& b : kButtonCc)
                if (cc == b.cc && val > 0 && d >= 0)
                {
                    if (pos > done) render (pos);
                    actions |= usp::deckAction (d, b.a);
                }
            // Parameters
            const int slot = cc == 3 ? 2 : d;     // CC 3 (crossfade) works on any channel
            if (slot >= 0 && cc < 128 && ccRaw[slot][cc] != nullptr)
            {
                if (pos > done) render (pos);
                const float v = ccIsBool[slot][cc] ? (val > 0 ? 1.0f : 0.0f) : (float) val / 127.0f;
                auto& sl = ccSlots[slot][cc];
                sl.latest.store (v, std::memory_order_relaxed);
                sl.seq.fetch_add (1, std::memory_order_release);
                ccRaw[slot][cc]->store (v, std::memory_order_relaxed);
            }
        }
        else if (msg.isMidiStart() || msg.isMidiContinue() || msg.isMidiStop())
        {
            if (pos > done) render (pos);
            for (int d = 0; d < 2; ++d)
                if (decks[d].transportFollow->load() > 0.5f)
                    actions |= usp::deckAction (d, msg.isMidiStop() ? usp::kActionTransportStop : usp::kActionTransportStart);
        }
    }
    render (numSamples);

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

void UnofficialSpotykachProcessor::timerCallback()
{
    applyPendingMidi();
    pollLoopFiles();
}

// Message thread, 30 Hz.
void UnofficialSpotykachProcessor::applyPendingMidi()
{
    for (int sl = 0; sl < 3; ++sl)
        for (int cc = 0; cc < 128; ++cc)
        {
            auto* par = ccParam[sl][cc];
            if (par == nullptr) continue;
            auto& s = ccSlots[sl][cc];
            const uint32_t seq = s.seq.load (std::memory_order_acquire);
            if (seq == s.applied.load (std::memory_order_relaxed)) continue;
            par->setValueNotifyingHost (par->convertTo0to1 (s.latest.load (std::memory_order_relaxed)));
            s.applied.store (seq, std::memory_order_release);
        }

    // Fit tempo succeeded in the engine: make the Tempo parameter match and switch to the internal clock.
    const uint32_t fit = engine.status().fitSeq.load();
    if (fit != lastFitSeq)
    {
        lastFitSeq = fit;
        if (auto* src = apvts.getParameter ("tempoSource")) src->setValueNotifyingHost (0.0f);
        if (auto* t = apvts.getParameter ("tempo")) t->setValueNotifyingHost (t->convertTo0to1 (engine.status().fitBpm.load()));
    }
}

juce::AudioProcessorEditor* UnofficialSpotykachProcessor::createEditor()
{
    return new UnofficialSpotykachEditor (*this);
}

void UnofficialSpotykachProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("LOOPFILES"), nullptr);
    juce::ValueTree files_ ("LOOPFILES");
    {
        const juce::ScopedLock sl (fileLock);
        for (int d = 0; d < 2; ++d)
        {
            const auto& l = files[d].link;
            if (! l.valid()) continue;
            juce::ValueTree n ("LOOP");
            n.setProperty ("deck", d, nullptr);
            n.setProperty ("path", juce::String (l.path), nullptr);
            n.setProperty ("rel", juce::String (l.rel), nullptr);
            n.setProperty ("size", (juce::int64) l.size, nullptr);
            n.setProperty ("fp", juce::String::toHexString ((juce::int64) l.fingerprint), nullptr);
            files_.addChild (n, -1, nullptr);
        }
    }
    state.addChild (files_, -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void UnofficialSpotykachProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // Loop links. A state without a LOOPFILES element (an older version) leaves the decks alone.
    const auto lf = apvts.state.getChildWithName ("LOOPFILES");
    if (! lf.isValid()) return;
    bool restored[2] = { false, false };
    for (int i = 0; i < lf.getNumChildren(); ++i)
    {
        const auto n = lf.getChild (i);
        const int d = (int) n.getProperty ("deck", -1);
        if (d < 0 || d > 1 || restored[d]) continue;
        usp::LoopLink link;
        link.path = n.getProperty ("path").toString().toStdString();
        link.rel = n.getProperty ("rel").toString().toStdString();
        link.size = (uint64_t) (juce::int64) n.getProperty ("size", 0);
        link.fingerprint = (uint64_t) n.getProperty ("fp").toString().getHexValue64();
        if (! link.valid()) continue;
        restored[d] = true;
        startRestore (d, link);
    }
    // A deck that holds audio but has no link in this state keeps the audio; it is simply not saved anywhere.
    const juce::ScopedLock sl (fileLock);
    for (int d = 0; d < 2; ++d)
        if (! restored[d] && files[d].state != FileState::loading && files[d].state != FileState::empty)
        {
            files[d].state = FileState::unsaved;
            files[d].link = {};
            files[d].name = {};
            files[d].note = "Loop is not saved to a file";
            files[d].isError = false;
        }
}

// ---- Loop files ---------------------------------------------------------------------------------------------
juce::File UnofficialSpotykachProcessor::defaultHomeFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Unofficial Spotykach");
}

juce::File UnofficialSpotykachProcessor::settingsFile() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("Unofficial Spotykach").getChildFile ("home-folder.txt");
}

juce::File UnofficialSpotykachProcessor::homeFolder() const
{
    const juce::ScopedLock sl (fileLock);
    return juce::File (homePath);
}

void UnofficialSpotykachProcessor::setHomeFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (fileLock);
        homePath = folder.getFullPathName();
    }
    const auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (folder.getFullPathName());
}

void UnofficialSpotykachProcessor::postUi (std::function<void()> f)
{
    const juce::ScopedLock sl (uiLock);
    uiQueue.push_back (std::move (f));
}

UnofficialSpotykachProcessor::FileInfo UnofficialSpotykachProcessor::deckFileInfo (int d) const
{
    const juce::ScopedLock sl (fileLock);
    const auto& f = files[d];
    FileInfo i;
    i.state = f.state;
    i.name = f.name;
    i.note = f.note;
    i.isError = f.isError;
    if ((f.state == FileState::loading || f.state == FileState::saving) && juce::Time::getMillisecondCounter() - f.startedMs > 2500)
        i.note += " (waiting for the host to run audio - press play in Logic)";
    return i;
}

bool UnofficialSpotykachProcessor::loopFilesBusy() const
{
    const juce::ScopedLock sl (fileLock);
    return files[0].state == FileState::loading || files[0].state == FileState::saving
        || files[1].state == FileState::loading || files[1].state == FileState::saving;
}

// Starts an operation on a deck: remembers how the deck's file state was (to go back to it on failure).
void UnofficialSpotykachProcessor::beginOp (int d, FileState state, const juce::String& note)
{
    auto& f = files[d];
    f.backupState = f.state; f.backupLink = f.link; f.backupName = f.name; f.backupNote = f.note; f.backupSeq = f.linkedSeq;
    f.state = state;
    f.note = note;
    f.isError = false;
    f.queuedLoad.reset();
    ++f.gen;
    f.startedMs = juce::Time::getMillisecondCounter();
}

void UnofficialSpotykachProcessor::failOp (int d, uint32_t gen, const juce::String& message)
{
    const juce::ScopedLock sl (fileLock);
    auto& f = files[d];
    if (f.gen != gen) return;
    f.state = f.backupState; f.link = f.backupLink; f.name = f.backupName; f.linkedSeq = f.backupSeq;
    f.queuedLoad.reset();
    f.note = message;
    f.isError = true;
}

void UnofficialSpotykachProcessor::markMissing (int d, uint32_t gen, const usp::LoopLink& link, const juce::String& message)
{
    const juce::ScopedLock sl (fileLock);
    auto& f = files[d];
    if (f.gen != gen) return;
    f.state = FileState::missing;
    f.link = link;                      // kept: the project remembers it and will try again next time
    f.name = juce::String (usp::fileNameOf (link.path));
    f.note = message;
    f.isError = true;
    f.linkedSeq = engine.status().deck[d].contentSeq.load();
    f.queuedLoad.reset();
}

void UnofficialSpotykachProcessor::loadLoopFile (int d, const juce::File& file)
{
    if (d < 0 || d > 1) return;
    uint32_t gen;
    std::string homeStd;
    {
        const juce::ScopedLock sl (fileLock);
        beginOp (d, FileState::loading, "Reading " + file.getFileName());
        files[d].pendName = file.getFileName();
        gen = files[d].gen;
        homeStd = homePath.toStdString();
    }
    const std::string path = file.getFullPathName().toStdString();
    worker->add ([this, d, gen, path, homeStd]
    {
        std::vector<uint8_t> bytes;
        if (! usp::readFileBytes (path, bytes))
        {
            postUi ([this, d, gen] { failOp (d, gen, "Could not read the file"); });
            return;
        }
        const auto link = usp::makeLink (path, homeStd, bytes);
        queueDecoded (d, gen, std::move (bytes), link, {}, false);
    });
}

void UnofficialSpotykachProcessor::startRestore (int d, const usp::LoopLink& link)
{
    uint32_t gen;
    std::string homeStd;
    {
        const juce::ScopedLock sl (fileLock);
        beginOp (d, FileState::loading, "Looking for " + juce::String (usp::fileNameOf (link.path)));
        files[d].link = link;                     // stays in the project while we look
        files[d].name = juce::String (usp::fileNameOf (link.path));
        gen = files[d].gen;
        homeStd = homePath.toStdString();
    }
    worker->add ([this, d, gen, link, homeStd]
    {
        auto res = usp::resolveLink (link, homeStd);
        if (res.state == usp::LinkState::missing)
        {
            postUi ([this, d, gen, link, homeStd]
            {
                markMissing (d, gen, link, "Missing: " + juce::String (usp::fileNameOf (link.path)) + " (looked in " + juce::String (homeStd) + ")");
            });
            return;
        }
        juce::String note;
        if (res.state == usp::LinkState::moved) note = "Found again at a new place. ";
        if (res.state == usp::LinkState::changed) note = "The file has changed since the project was saved; loaded as it is now. ";
        const auto now = usp::makeLink (res.path, homeStd, res.bytes);
        queueDecoded (d, gen, std::move (res.bytes), now, note, true);
    });
}

// Worker thread: decodes the file and hands the audio to the message thread, which gives it to the engine.
void UnofficialSpotykachProcessor::queueDecoded (int d, uint32_t gen, std::vector<uint8_t>&& bytes, const usp::LoopLink& link,
                                                 const juce::String& prefixNote, bool isRestore)
{
    auto audio = std::make_shared<usp::LoopTransfer>();
    usp::WavInfo info;
    std::string message;
    const auto err = usp::decodeWav (bytes.data(), bytes.size(), audio->audio, info, message);
    bytes.clear();
    bytes.shrink_to_fit();
    const juce::String name (usp::fileNameOf (link.path));
    if (err != usp::WavError::none)
    {
        const juce::String text = "Not loaded (" + name + "): " + juce::String (message);
        postUi ([this, d, gen, link, text, isRestore]
        {
            if (isRestore) markMissing (d, gen, link, text);
            else failOp (d, gen, text);
        });
        return;
    }

    juce::String note = prefixNote;
    note << juce::String ((double) audio->audio.frames() / usp::kLoopRate, 1) << " s";
    if (std::abs (info.rate - usp::kLoopRate) > 0.5)
        note << ", converted from " << juce::String (info.rate / 1000.0, info.rate == std::floor (info.rate / 1000.0) * 1000.0 ? 0 : 1) << " kHz";
    if (info.channels == 1) note << ", mono copied to both sides";
    if (info.cut) note << ", cut to the first 42 s";
    if (info.cueCount > 0) note << ", " << (int) info.cueCount << " slice points";

    postUi ([this, d, gen, audio, link, name, note]
    {
        const juce::ScopedLock sl (fileLock);
        auto& f = files[d];
        if (f.gen != gen) return;
        f.queuedLoad = audio;
        f.pendLink = link;
        f.pendName = name;
        f.pendNote = note;
        f.loadSubmitted = false;
        f.note = "Loading " + name + " into the deck";
    });
}

void UnofficialSpotykachProcessor::saveLoopFile (int d, const juce::File& file)
{
    if (d < 0 || d > 1) return;
    const juce::ScopedLock sl (fileLock);
    const auto& st = engine.status().deck[d];
    auto& f = files[d];
    if (st.empty.load() || st.loopFrames.load() == 0)
    {
        f.note = "Nothing to save: this deck has no loop";
        f.isError = true;
        return;
    }
    if (st.recording.load() || st.armed.load())
    {
        f.note = "Stop recording before saving";
        f.isError = true;
        return;
    }
    beginOp (d, FileState::saving, "Saving " + file.getFileName());
    f.saveTarget = file.getFullPathName();

    auto t = std::make_unique<usp::LoopTransfer>();
    const uint32_t n = st.loopFrames.load();
    t->audio.l.assign (n, 0.0f);
    t->audio.r.assign (n, 0.0f);
    t->cues.assign (usp::kMaxLoopCues, 0u);
    if (! engine.submitSave (d, t))
    {
        f.state = f.backupState; f.link = f.backupLink; f.name = f.backupName; f.linkedSeq = f.backupSeq;
        f.note = "Busy: try again in a moment";
        f.isError = true;
        return;
    }
    f.saveSubmitted = true;
    f.saveGen = f.gen;
}

void UnofficialSpotykachProcessor::pollLoopFiles()
{
    // Results from the worker thread.
    std::deque<std::function<void()>> todo;
    {
        const juce::ScopedLock sl (uiLock);
        todo.swap (uiQueue);
    }
    for (auto& job : todo) job();

    for (int d = 0; d < 2; ++d)
    {
        const juce::ScopedLock sl (fileLock);
        auto& f = files[d];
        const auto& st = engine.status().deck[d];

        // Decoded audio waiting for the engine.
        if (f.queuedLoad && ! f.loadSubmitted)
        {
            auto t = std::make_unique<usp::LoopTransfer>();
            t->audio = std::move (f.queuedLoad->audio);
            if (engine.submitLoad (d, t))
            {
                f.loadSubmitted = true;
                f.loadGen = f.gen;
                f.queuedLoad.reset();
            }
            else f.queuedLoad->audio = std::move (t->audio);      // the previous transfer is still with the engine: try again later
        }

        if (auto r = engine.collectLoad (d))
        {
            if (f.loadGen == f.gen)
            {
                if (r->result == usp::LoopTransfer::done)
                {
                    f.state = FileState::linked;
                    f.link = f.pendLink;
                    f.name = f.pendName;
                    f.note = f.pendNote;
                    f.isError = false;
                    f.linkedSeq = r->contentSeq;
                }
                else
                {
                    f.state = f.backupState; f.link = f.backupLink; f.name = f.backupName; f.linkedSeq = f.backupSeq;
                    f.note = r->result == usp::LoopTransfer::refused ? "Not loaded: stop recording on this deck first"
                                                                       : "Not loaded: recording started while the file was loading";
                    f.isError = true;
                }
            }
        }

        if (auto r = engine.collectSave (d))
        {
            if (f.saveGen == f.gen && f.state == FileState::saving)
            {
                if (r->result != usp::LoopTransfer::done)
                {
                    f.state = f.backupState; f.link = f.backupLink; f.name = f.backupName; f.linkedSeq = f.backupSeq;
                    f.note = r->result == usp::LoopTransfer::refused ? "Not saved: the loop changed or the deck is recording"
                                                                       : "Not saved: the loop changed while it was being copied";
                    f.isError = true;
                }
                else
                {
                    std::shared_ptr<usp::LoopTransfer> shared (r.release());
                    const uint32_t gen = f.gen, snapshotSeq = shared->contentSeq;
                    const std::string target = f.saveTarget.toStdString(), homeStd = homePath.toStdString();
                    worker->add ([this, d, gen, shared, snapshotSeq, target, homeStd]
                    {
                        usp::LoopAudio a = std::move (shared->audio);
                        a.cues.assign (shared->cues.begin(), shared->cues.begin() + (long) shared->cueCount);
                        std::vector<uint8_t> bytes;
                        usp::encodeWav (a, bytes);
                        const bool ok = usp::writeFileBytes (target, bytes);
                        usp::LoopLink link;
                        if (ok) link = usp::makeLink (target, homeStd, bytes);
                        postUi ([this, d, gen, ok, link, snapshotSeq, target]
                        {
                            if (! ok) { failOp (d, gen, "Could not write " + juce::File (target).getFileName() + " (is the folder writable?)"); return; }
                            const juce::ScopedLock sl2 (fileLock);
                            auto& g = files[d];
                            if (g.gen != gen) return;
                            g.link = link;
                            g.name = juce::String (usp::fileNameOf (link.path));
                            g.isError = false;
                            if (engine.status().deck[d].contentSeq.load() == snapshotSeq)
                            {
                                g.state = FileState::linked;
                                g.linkedSeq = snapshotSeq;
                                g.note = "Saved";
                            }
                            else
                            {
                                g.state = FileState::unsaved;
                                g.link = {};
                                g.note = "Saved " + g.name + ", but the loop changed meanwhile, so it is not linked";
                            }
                        });
                    });
                }
            }
            f.saveSubmitted = false;
        }

        // The deck's audio changed after it was loaded or saved (a recording, an overdub): the file no longer matches.
        const uint32_t seq = st.contentSeq.load();
        if ((f.state == FileState::linked || f.state == FileState::missing) && seq != f.linkedSeq)
        {
            f.state = FileState::unsaved;
            f.link = {};
            f.name = {};
            f.note = "Loop is not saved to a file";
            f.isError = false;
        }
        else if (f.state == FileState::empty && ! st.empty.load())
        {
            f.state = FileState::unsaved;
            f.note = "Loop is not saved to a file";
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UnofficialSpotykachProcessor();
}
