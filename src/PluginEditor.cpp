// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#include "PluginEditor.h"
#include "engine/PitchMap.h"
#include "engine/ModMap.h"

namespace
{
    juce::String versionString()
    {
        return juce::String ("v") + JucePlugin_VersionString + " (build " + US_BUILD_ID + ")";
    }

    const juce::Colour kBg     { 0xff15171b };
    const juce::Colour kPanel  { 0xff1f2228 };
    const juce::Colour kText   { 0xffe6e6e6 };
    const juce::Colour kDim    { 0xff9aa0a8 };
    const juce::Colour kAccent { 0xffe8a33d };
    const juce::Colour kRec    { 0xffd9423a };
    const juce::Colour kGreen  { 0xff4caf6a };
}

//==============================================================================
class UnofficialSpotykachEditor::AboutPanel : public juce::Component
{
public:
    AboutPanel()
    {
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (close);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBg.withAlpha (0.97f));
        auto r = getLocalBounds().reduced (24);

        g.setColour (kAccent);
        g.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
        g.drawText ("Unofficial Spotykach", r.removeFromTop (30), juce::Justification::left);

        g.setColour (kText);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText ("Version " + versionString(), r.removeFromTop (24), juce::Justification::left);
        r.removeFromTop (10);

        g.setColour (kDim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText (
            "This is an UNOFFICIAL port. It is not a Synthux Academy product and is not "
            "affiliated with, endorsed by, or supported by Synthux Academy.\n\n"
            "Based on the open-source Spotykach firmware by Synthux Academy "
            "(github.com/Synthux-Academy/Spotykach, MIT licence, Copyright (c) 2026 Synthux Academy).\n\n"
            "Spotykach was designed by Roey Tsemah (concept, visual and hardware design), "
            "Vlad Lytvynenko (concept, firmware design), Nick Donaldson (electronics design, "
            "hardware drivers, bootloader) and Rosa Schuurmans (FX and LEDs), per the "
            "project's CREDITS.md.\n\n"
            "Includes code from Infrasonic Audio LLC and DaisySP (Electrosmith, MIT). "
            "Built with JUCE. This plug-in is licensed under the GNU AGPLv3; see "
            "LICENSE and THIRD-PARTY-LICENSES.txt that came with the download.\n\n"
            "Milestone 6: both decks with Reel, Slice and Drift modes, Flux and Grit effects, two modulators that you can patch to a destination, a trigger sequencer per deck, routing, A/B crossfade, tempo (internal, host or tap) and launch quantise. MIDI control is not in yet.",
            r.removeFromTop (r.getHeight() - 44), juce::Justification::topLeft, 20);
    }

    void resized() override
    {
        close.setBounds (getLocalBounds().removeFromBottom (44).removeFromRight (120).reduced (8));
    }

private:
    juce::TextButton close { "Close" };
};

//==============================================================================
juce::Font UnofficialSpotykachEditor::font (float size, bool bold) const
{
    auto o = juce::FontOptions (size * scale);
    if (bold) o = o.withStyle ("Bold");
    return juce::Font (o);
}

void UnofficialSpotykachEditor::addLabelled (LabelledSlider& s, const juce::String& paramId, const juce::String& text,
                                             juce::Slider::SliderStyle style)
{
    s.slider.setSliderStyle (style);
    s.slider.setColour (juce::Slider::trackColourId, kAccent);
    addAndMakeVisible (s.slider);
    s.label.setText (text, juce::dontSendNotification);
    s.label.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (s.label);
    s.attachment = std::make_unique<SliderAtt> (proc.apvts, paramId, s.slider);
}

void UnofficialSpotykachEditor::addCombo (LabelledCombo& c, const juce::String& paramId, const juce::String& text)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        c.box.addItemList (choice->choices, 1);
    addAndMakeVisible (c.box);
    c.label.setText (text, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (c.label);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, c.box);
}

// A button press = parameter 0 -> 1, then back to 0 shortly after. The audio thread acts on the rising edge.
void UnofficialSpotykachEditor::pulse (const juce::String& paramId)
{
    auto* param = proc.apvts.getParameter (paramId);
    if (param == nullptr) return;
    param->beginChangeGesture();
    param->setValueNotifyingHost (1.0f);
    param->endChangeGesture();

    juce::Component::SafePointer<UnofficialSpotykachEditor> safe (this);
    juce::Timer::callAfterDelay (80, [safe, paramId]
    {
        if (safe == nullptr) return;
        if (auto* p = safe->proc.apvts.getParameter (paramId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (0.0f);
            p->endChangeGesture();
        }
    });
}

void UnofficialSpotykachEditor::setupDeck (int d)
{
    auto& ui = deckUI[(size_t) d];
    const juce::String tag = d == 0 ? "A" : "B";

    ui.title.setText ("DECK " + tag, juce::dontSendNotification);
    ui.title.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (ui.title);
    ui.status.setColour (juce::Label::textColourId, kText);
    ui.status.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ui.status);

    static const char* btnText[UnofficialSpotykachProcessor::kNumDeckButtons] = { "REC", "PLAY", "REV", "TRIG", "FROM", "SEQ", "CLEAR" };
    static const char* btnTip[UnofficialSpotykachProcessor::kNumDeckButtons] = {
        "Record from the plug-in input (press again to stop)", "Play / stop", "Play in reverse / stop",
        "Trigger the loop with the current settings",
        "Record the other deck's output into this deck (press again to stop)",
        "Trigger sequencer: press to arm, then tap TRIG to record a rhythm; press SEQ again to stop recording",
        "Hold for 1.5 seconds to clear the trigger sequence" };
    for (int i = 0; i < UnofficialSpotykachProcessor::kNumDeckButtons; ++i)
    {
        auto& b = ui.buttons[(size_t) i];
        b.setButtonText (i == 4 ? juce::String ("FROM ") + (d == 0 ? "B" : "A") : juce::String (btnText[i]));
        b.setTooltip (btnTip[i]);
        const auto id = UnofficialSpotykachProcessor::deckId (d, UnofficialSpotykachProcessor::deckButtonBase (i));
        if (i == 6)
        {
            // CLEAR needs a 1.5 s hold (the firmware's Hold<1500>); the timer fires it.
            b.onStateChange = [this, d]
            {
                auto& u = deckUI[(size_t) d];
                if (u.buttons[6].isDown()) { if (u.clearDownAt == 0.0) { u.clearDownAt = juce::Time::getMillisecondCounterHiRes(); u.clearFired = false; } }
                else u.clearDownAt = 0.0;
            };
        }
        else
            b.onClick = [this, id] { pulse (id); };
        if (i == 5) b.setColour (juce::TextButton::buttonOnColourId, kRec);
        addAndMakeVisible (b);
    }

    static const char* base[6] = { "position", "size", "pitch", "envelope", "mix", "feedback" };
    static const char* text[6] = { "Position", "Size", "Pitch", "Envelope", "Mix", "Feedback" };
    for (size_t i = 0; i < 6; ++i)
    {
        auto& k = ui.knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
        addAndMakeVisible (k.slider);
        k.label.setText (text[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<SliderAtt> (proc.apvts, UnofficialSpotykachProcessor::deckId (d, base[i]), k.slider);
    }

    using P = UnofficialSpotykachProcessor;
    addCombo (ui.mode, P::deckId (d, "mode"), "Mode");
    ui.mode.label.setVisible (false);
    addCombo (ui.snap, P::deckId (d, "pitchSnap"), "Pitch Snap");
    addLabelled (ui.offset,    P::deckId (d, "sliceOffset"), "Slice Offset", juce::Slider::LinearHorizontal);
    addLabelled (ui.envLength, P::deckId (d, "envLength"),   "Env Length",   juce::Slider::LinearHorizontal);
    addLabelled (ui.window,    P::deckId (d, "window"),      "Window",       juce::Slider::LinearHorizontal);

    // FX page: Flux (delay) and Grit (drive / reduce / filter). Shown instead of the loop knobs when FX is pressed.
    static const char* fxBase[6] = { "fluxTime", "fluxFeedback", "fluxMix", "gritIntensity", "gritMix", "gritCharacter" };
    static const char* fxText[6] = { "Flux Time", "Flux Feedback", "Flux Mix", "Grit Intensity", "Grit Mix", "Grit Character" };
    for (size_t i = 0; i < 6; ++i)
    {
        auto& k = ui.fxKnobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, i < 3 ? kGreen : kAccent);
        addChildComponent (k.slider);
        k.label.setText (fxText[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, kText);
        addChildComponent (k.label);
        k.attachment = std::make_unique<SliderAtt> (proc.apvts, P::deckId (d, fxBase[i]), k.slider);
    }
    ui.fxKnobs[0].slider.textFromValueFunction = [this, d] (double v)
    {
        const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId (d, "fluxMode"))->load());
        if (mode == 1)
        {
            static const char* names[8] = { "1/32", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar" };
            return juce::String (names[juce::jlimit (0, 7, juce::roundToInt ((float) v * 7.0f))]);
        }
        return juce::String (10 + juce::roundToInt ((float) v * 1990.0f)) + " ms";
    };
    ui.fxKnobs[3].slider.textFromValueFunction = [this, d] (double v)
    {
        const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId (d, "gritMode"))->load());
        if (mode != 2) return juce::String (juce::roundToInt ((float) v * 100.0f)) + " %";
        if (v < 0.48) return "LP " + juce::String (juce::roundToInt ((float) v * 200.0f)) + " %";
        if (v > 0.52) return "HP " + juce::String (juce::roundToInt (((float) v - 0.5f) * 200.0f)) + " %";
        return juce::String ("open");
    };
    for (auto* b : { &ui.fluxOn, &ui.gritOn })
    {
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, kAccent);
        addChildComponent (*b);
    }
    ui.fluxOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::deckId (d, "fluxOn"), ui.fluxOn);
    ui.gritOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::deckId (d, "gritOn"), ui.gritOn);
    addCombo (ui.fluxMode, P::deckId (d, "fluxMode"), "Flux Mode");
    addCombo (ui.gritMode, P::deckId (d, "gritMode"), "Grit Mode");
    for (auto* cb : { &ui.fluxMode, &ui.gritMode }) { cb->box.setVisible (false); cb->label.setVisible (false); }

    addAndMakeVisible (ui.pageButton);
    ui.pageButton.setClickingTogglesState (true);
    ui.pageButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    ui.pageButton.setColour (juce::TextButton::textColourOnId, kBg);
    ui.pageButton.onClick = [this, d]
    {
        deckUI[(size_t) d].fxPage = deckUI[(size_t) d].pageButton.getToggleState();
        showPage (d);
        resized();
    };

    // With Pitch Snap on, the knob itself jumps to the snapped position while you drag it.
    ui.knobs[2].slider.onValueChange = [this, d]
    {
        const int snap = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId (d, "pitchSnap"))->load());
        if (snap == 0) return;
        auto& sl = deckUI[(size_t) d].knobs[2].slider;
        const float v = (float) sl.getValue(), target = usp::snapKnob (v, snap);
        if (std::abs (target - v) > 1.0e-4f)
            sl.setValue (target, juce::sendNotificationSync);
    };

    // The pitch knob shows what the engine will actually play, including pitch snap.
    ui.knobs[2].slider.textFromValueFunction = [this, d] (double v)
    {
        const int snap = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId (d, "pitchSnap"))->load());
        const float speed = usp::normToSpeed (usp::snapPitch ((float) v, snap));
        if (speed < 0.005f) return juce::String ("stopped");
        const float st = 12.0f * std::log2 (speed);
        return juce::String (speed, 2) + "x " + (st >= 0 ? "+" : "") + juce::String (st, snap == 0 ? 1 : 0) + " st";
    };
}

void UnofficialSpotykachEditor::showPage (int d)
{
    auto& ui = deckUI[(size_t) d];
    const bool fx = ui.fxPage;
    for (auto& k : ui.knobs)   { k.slider.setVisible (! fx); k.label.setVisible (! fx); }
    for (auto& k : ui.fxKnobs) { k.slider.setVisible (fx);   k.label.setVisible (fx); }
    ui.snap.box.setVisible (! fx);   ui.snap.label.setVisible (! fx);
    for (auto* ls : { &ui.offset, &ui.envLength, &ui.window }) { ls->slider.setVisible (! fx); ls->label.setVisible (! fx); }
    ui.fluxOn.setVisible (fx);  ui.gritOn.setVisible (fx);
    ui.fluxMode.box.setVisible (fx);  ui.gritMode.box.setVisible (fx);
}

void UnofficialSpotykachEditor::setupMod (int m)
{
    using P = UnofficialSpotykachProcessor;
    auto& ui = modUI[(size_t) m];
    ui.title.setText (m == 0 ? "MOD A" : "MOD B", juce::dontSendNotification);
    ui.title.setColour (juce::Label::textColourId, kAccent);
    addChildComponent (ui.title);
    addCombo (ui.type,   P::modId (m, "type"),   "Type");
    addCombo (ui.target, P::modId (m, "target"), "Target");
    addLabelled (ui.cycle, P::modId (m, "cycle"), "Rate", juce::Slider::LinearHorizontal);
    addLabelled (ui.glow,  P::modId (m, "glow"),  "Glow", juce::Slider::LinearHorizontal);
    ui.glow.slider.setColour (juce::Slider::trackColourId, kGreen);
    ui.type.label.setVisible (false);
    ui.sync.setColour (juce::ToggleButton::textColourId, kText);
    ui.sync.setColour (juce::ToggleButton::tickColourId, kAccent);
    addChildComponent (ui.sync);
    ui.syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::modId (m, "sync"), ui.sync);
    ui.sync.setTooltip ("Lock the LFO rate to the tempo (note lengths instead of Hz)");
    ui.target.box.setTooltip ("Where this modulator's output is sent. Glow is the amount.");
    ui.glow.slider.setTooltip ("Amount (and, for Follow, the input gain)");

    ui.cycle.slider.textFromValueFunction = [this, m] (double v)
    {
        const int type = juce::roundToInt (proc.apvts.getRawParameterValue (UnofficialSpotykachProcessor::modId (m, "type"))->load());
        const bool sync = proc.apvts.getRawParameterValue (UnofficialSpotykachProcessor::modId (m, "sync"))->load() > 0.5f;
        if (type == usp::kModFollow)
        {
            const float ms = usp::modFollowReleaseMs ((float) v);
            return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms / 1000.0f, 1) + " s";
        }
        if (sync) return juce::String (usp::modDivisionName (usp::modDivisionIndex ((float) v)));
        return juce::String (usp::modRateHz ((float) v), 2) + " Hz";
    };
}

void UnofficialSpotykachEditor::showGlobalPage()
{
    const bool mods = modsPage;
    for (auto* c : std::initializer_list<juce::Component*> { &routeLabel, &routeBox, &panSpeed.slider, &panSpeed.label, &panRange.slider,
                                                             &panRange.label, &tempo.slider, &tempo.label, &tempoSource.box, &tempoSource.label,
                                                             &quantise.box, &quantise.label, &click.slider, &click.label, &outLevel.slider,
                                                             &outLevel.label, &tapButton })
        c->setVisible (! mods);
    for (auto& ui : modUI)
        for (auto* c : std::initializer_list<juce::Component*> { &ui.title, &ui.type.box, &ui.target.box, &ui.target.label, &ui.cycle.slider,
                                                                 &ui.cycle.label, &ui.glow.slider, &ui.glow.label, &ui.sync })
            c->setVisible (mods);
    setupTab.setToggleState (! mods, juce::dontSendNotification);
    modsTab.setToggleState (mods, juce::dontSendNotification);
    repaint();
}

// Tap tempo: the average of the last few intervals; a pause of more than 2.5 s starts a new count.
// Tapping switches the tempo source to Internal (a tapped tempo would otherwise be overridden by the host).
void UnofficialSpotykachEditor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! tapTimes.empty() && now - tapTimes.back() > 2.5) tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() > 5) tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() < 2) return;
    const double interval = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
    const float bpm = juce::jlimit (20.0f, 250.0f, (float) (60.0 / interval));
    if (auto* src = proc.apvts.getParameter ("tempoSource")) { src->beginChangeGesture(); src->setValueNotifyingHost (0.0f); src->endChangeGesture(); }
    if (auto* t = proc.apvts.getParameter ("tempo"))
    {
        t->beginChangeGesture();
        t->setValueNotifyingHost (t->convertTo0to1 (bpm));
        t->endChangeGesture();
    }
}

UnofficialSpotykachEditor::UnofficialSpotykachEditor (UnofficialSpotykachProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    titleLabel.setText ("UNOFFICIAL SPOTYKACH", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (versionString(), juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, kDim);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (versionLabel);

    setupDeck (0);
    setupDeck (1);

    addLabelled (crossfade, "crossfade", "A/B Crossfade", juce::Slider::LinearHorizontal);
    addLabelled (panSpeed,  "panSpeed",  "Pan Speed",     juce::Slider::LinearHorizontal);
    addLabelled (panRange,  "panRange",  "Pan Range",     juce::Slider::LinearHorizontal);
    addLabelled (outLevel,  "outLevel",  "Output Level",  juce::Slider::LinearHorizontal);
    outLevel.slider.setTextValueSuffix (" dB");
    addLabelled (tempo, "tempo", "Tempo", juce::Slider::LinearHorizontal);
    tempo.slider.setTextValueSuffix (" BPM");
    addLabelled (click, "click", "Click", juce::Slider::LinearHorizontal);
    addCombo (tempoSource, "tempoSource", "Source");
    addCombo (quantise, "quantise", "Quantise");
    setupMod (0);
    setupMod (1);
    addAndMakeVisible (tapButton);
    tapButton.setTooltip ("Tap the tempo (sets Source to Internal)");
    tapButton.onClick = [this] { tap(); };
    for (auto* b : { &setupTab, &modsTab })
    {
        b->setColour (juce::TextButton::buttonOnColourId, kAccent);
        b->setColour (juce::TextButton::textColourOnId, kBg);
        addAndMakeVisible (*b);
    }
    setupTab.onClick = [this] { modsPage = false; showGlobalPage(); resized(); };
    modsTab.onClick  = [this] { modsPage = true;  showGlobalPage(); resized(); };

    routeLabel.setText ("Routing", juce::dontSendNotification);
    routeLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (routeLabel);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("route")))
        routeBox.addItemList (choice->choices, 1);
    addAndMakeVisible (routeBox);
    routeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "route", routeBox);

    aboutButton.onClick = [this]
    {
        about->setVisible (true);
        about->toFront (true);
    };
    addAndMakeVisible (aboutButton);

    about = std::make_unique<AboutPanel>();
    addChildComponent (*about);
    showGlobalPage();

    // Read the remembered width FIRST: setResizeLimits() below triggers resized() at size 0, which would
    // otherwise overwrite the stored value before it is read.
    int w = (int) proc.apvts.state.getProperty ("uiWidth", kBaseWidth);

    // Resizable, fixed aspect ratio. Below 75 % the labels are no longer readable.
    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);

    w = juce::jlimit (kBaseWidth * 3 / 4, kBaseWidth * 2, w);

    // setSize() calls resized(), so it must come after every child component exists.
    setSize (w, (int) std::lround ((double) w * kBaseHeight / kBaseWidth));
    sizeRemembering = true;

    startTimerHz (15);
    timerCallback();
}

UnofficialSpotykachEditor::~UnofficialSpotykachEditor()
{
    stopTimer();
}

void UnofficialSpotykachEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto& st = proc.engineStatus();

    for (size_t d = 0; d < 2; ++d)
    {
        auto& ui = deckUI[d];
        g.setColour (kPanel);
        g.fillRoundedRectangle (ui.panel, 8.0f * scale);

        g.setColour (kBg);
        g.fillRoundedRectangle (ui.loopBar, 3.0f * scale);
        const auto& s = st.deck[d];
        if (! s.empty.load())
        {
            g.setColour (s.recording.load() ? kRec : kGreen);
            const float x = ui.loopBar.getX() + ui.loopBar.getWidth() * juce::jlimit (0.0f, 1.0f, s.playhead.load());
            g.fillRect (juce::Rectangle<float> (x - 1.5f * scale, ui.loopBar.getY(), 3.0f * scale, ui.loopBar.getHeight()));
        }
    }

    if (modsPage)
        for (size_t m = 0; m < 2; ++m)
        {
            g.setColour (kPanel);
            g.fillRoundedRectangle (modUI[m].meter, 3.0f * scale);
            g.setColour (kGreen);
            g.fillRoundedRectangle (modUI[m].meter.withWidth (modUI[m].meter.getWidth() * juce::jlimit (0.0f, 1.0f, st.modValue[m].load())), 3.0f * scale);
        }

    g.setColour (kBg);
    g.fillRoundedRectangle (meterArea, 3.0f * scale);
    g.setColour (meterLevel > 0.99f ? juce::Colours::red : kAccent);
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * juce::jlimit (0.0f, 1.0f, meterLevel)), 3.0f * scale);
    g.setColour (kDim);
    g.setFont (font (11.0f));
    g.drawText ("input level", meterArea.translated (0.0f, 16.0f * scale).toNearestInt(), juce::Justification::left);
}

void UnofficialSpotykachEditor::layoutDeck (int d, juce::Rectangle<int> area, float s)
{
    auto& ui = deckUI[(size_t) d];
    ui.panel = area.toFloat();
    auto r = area.reduced (juce::roundToInt (10 * s), juce::roundToInt (8 * s));

    auto head = r.removeFromTop (juce::roundToInt (22 * s));
    ui.title.setFont (font (15.0f, true));
    ui.status.setFont (font (12.0f));
    ui.title.setBounds (head.removeFromLeft (head.getWidth() / 4));
    ui.mode.box.setBounds (head.removeFromLeft (juce::roundToInt (96 * s)));
    head.removeFromLeft (juce::roundToInt (6 * s));
    ui.pageButton.setBounds (head.removeFromLeft (juce::roundToInt (44 * s)));
    head.removeFromLeft (juce::roundToInt (8 * s));
    ui.status.setBounds (head);

    r.removeFromTop (juce::roundToInt (4 * s));
    auto btnRow = r.removeFromTop (juce::roundToInt (28 * s));
    const int gap = juce::roundToInt (6 * s);
    const int bw = (btnRow.getWidth() - 6 * gap) / 7;
    for (auto& b : ui.buttons)
    {
        b.setBounds (btnRow.removeFromLeft (bw));
        btnRow.removeFromLeft (gap);
    }

    r.removeFromTop (juce::roundToInt (4 * s));
    auto row2 = r.removeFromBottom (juce::roundToInt (24 * s));     // snap, offset, env length, window
    r.removeFromBottom (juce::roundToInt (4 * s));
    {
        auto fxRow = row2;
        const int halfFx = fxRow.getWidth() / 2;
        auto flux = fxRow.removeFromLeft (halfFx).reduced (juce::roundToInt (4 * s), 0);
        auto grit = fxRow.reduced (juce::roundToInt (4 * s), 0);
        ui.fluxOn.setBounds (flux.removeFromLeft (juce::roundToInt (70 * s)));
        ui.fluxMode.box.setBounds (flux.reduced (0, juce::roundToInt (1 * s)));
        ui.gritOn.setBounds (grit.removeFromLeft (juce::roundToInt (70 * s)));
        ui.gritMode.box.setBounds (grit.reduced (0, juce::roundToInt (1 * s)));
    }
    {
        ui.snap.label.setFont (font (12.0f));
        ui.snap.label.setBounds (row2.removeFromLeft (juce::roundToInt (62 * s)));
        ui.snap.box.setBounds (row2.removeFromLeft (juce::roundToInt (118 * s)));
        row2.removeFromLeft (juce::roundToInt (8 * s));
        const int third = row2.getWidth() / 3;
        auto place = [&] (LabelledSlider& ls, juce::Rectangle<int> cell, int labelW)
        {
            ls.label.setFont (font (11.0f));
            ls.label.setBounds (cell.removeFromLeft (juce::roundToInt ((float) labelW * s)));
            ls.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, juce::roundToInt (38 * s), juce::roundToInt (18 * s));
            ls.slider.setBounds (cell);
        };
        place (ui.offset,    row2.removeFromLeft (third), 54);
        place (ui.envLength, row2.removeFromLeft (third), 54);
        place (ui.window,    row2, 46);
    }
    ui.loopBar = r.removeFromBottom (juce::roundToInt (8 * s)).toFloat();
    r.removeFromBottom (juce::roundToInt (4 * s));

    const int kw = r.getWidth() / 6;
    const auto knobArea = r;
    for (size_t i = 0; i < 6; ++i)
    {
        auto cell = juce::Rectangle<int> (knobArea.getX() + (int) i * kw, knobArea.getY(), kw, knobArea.getHeight());
        auto& k = ui.fxKnobs[i];
        k.label.setFont (font (12.0f));
        k.label.setBounds (cell.removeFromTop (juce::roundToInt (16 * s)));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::roundToInt (92 * s), juce::roundToInt (18 * s));
        k.slider.setBounds (cell);
    }
    for (auto& k : ui.knobs)
    {
        auto cell = r.removeFromLeft (kw);
        k.label.setFont (font (12.0f));
        k.label.setBounds (cell.removeFromTop (juce::roundToInt (16 * s)));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::roundToInt (92 * s), juce::roundToInt (18 * s));
        k.slider.setBounds (cell);
    }
}

void UnofficialSpotykachEditor::resized()
{
    scale = (float) getWidth() / (float) kBaseWidth;
    const float s = scale;
    auto r = getLocalBounds().reduced (juce::roundToInt (16 * s));

    auto top = r.removeFromTop (juce::roundToInt (30 * s));
    aboutButton.setBounds (top.removeFromRight (juce::roundToInt (70 * s)));
    top.removeFromRight (juce::roundToInt (8 * s));
    versionLabel.setFont (font (13.0f));
    versionLabel.setBounds (top.removeFromRight (juce::roundToInt (190 * s)));
    titleLabel.setFont (font (20.0f, true));
    titleLabel.setBounds (top);
    r.removeFromTop (juce::roundToInt (8 * s));

    const int deckH = juce::roundToInt (236 * s);
    layoutDeck (0, r.removeFromTop (deckH), s);
    r.removeFromTop (juce::roundToInt (8 * s));
    layoutDeck (1, r.removeFromTop (deckH), s);
    r.removeFromTop (juce::roundToInt (10 * s));

    const int rowH = juce::roundToInt (26 * s);
    auto setLs = [&] (LabelledSlider& ls, juce::Rectangle<int> row, int labelW, int textW)
    {
        ls.label.setFont (font (13.0f));
        ls.label.setBounds (row.removeFromLeft (juce::roundToInt ((float) labelW * s)));
        ls.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, juce::roundToInt ((float) textW * s), juce::roundToInt (20 * s));
        ls.slider.setBounds (row);
    };
    {
        auto rowX = r.removeFromTop (rowH);
        auto tabs = rowX.removeFromRight (juce::roundToInt (150 * s));
        setupTab.setBounds (tabs.removeFromLeft (juce::roundToInt (72 * s)));
        tabs.removeFromLeft (juce::roundToInt (6 * s));
        modsTab.setBounds (tabs);
        rowX.removeFromRight (juce::roundToInt (10 * s));
        setLs (crossfade, rowX, 100, 110);
    }
    r.removeFromTop (juce::roundToInt (6 * s));

    // The page area below the crossfade row holds either the setup rows or the two modulators.
    auto page = r.removeFromTop (juce::roundToInt (122 * s));
    {
        auto p = page;
        auto rowR = p.removeFromTop (rowH);
        routeLabel.setFont (font (13.0f));
        routeLabel.setBounds (rowR.removeFromLeft (juce::roundToInt (56 * s)));
        routeBox.setBounds (rowR.removeFromLeft (juce::roundToInt (136 * s)));
        rowR.removeFromLeft (juce::roundToInt (10 * s));
        const int halfR = rowR.getWidth() / 2;
        setLs (panSpeed, rowR.removeFromLeft (halfR), 62, 52);
        setLs (panRange, rowR, 62, 52);
        p.removeFromTop (juce::roundToInt (4 * s));

        auto rowT = p.removeFromTop (rowH);
        setLs (tempo, rowT.removeFromLeft (rowT.getWidth() * 40 / 100), 56, 74);
        rowT.removeFromLeft (juce::roundToInt (6 * s));
        tapButton.setBounds (rowT.removeFromLeft (juce::roundToInt (46 * s)).reduced (0, juce::roundToInt (1 * s)));
        rowT.removeFromLeft (juce::roundToInt (10 * s));
        for (auto* cb : { &tempoSource, &quantise })
        {
            auto cell = rowT.removeFromLeft (cb == &tempoSource ? rowT.getWidth() * 42 / 100 : rowT.getWidth());
            cb->label.setFont (font (13.0f));
            cb->label.setBounds (cell.removeFromLeft (juce::roundToInt ((cb == &tempoSource ? 44.0f : 62.0f) * s)));
            cb->box.setBounds (cell.reduced (0, juce::roundToInt (1 * s)));
            rowT.removeFromLeft (juce::roundToInt (8 * s));
        }
        p.removeFromTop (juce::roundToInt (4 * s));

        auto rowO = p.removeFromTop (rowH);
        setLs (click, rowO.removeFromLeft (rowO.getWidth() * 42 / 100), 44, 52);
        rowO.removeFromLeft (juce::roundToInt (10 * s));
        setLs (outLevel, rowO, 86, 64);
    }
    {
        auto p = page;
        for (size_t m = 0; m < 2; ++m)
        {
            auto& ui = modUI[m];
            auto row1 = p.removeFromTop (rowH);
            ui.title.setFont (font (13.0f, true));
            ui.title.setBounds (row1.removeFromLeft (juce::roundToInt (54 * s)));
            ui.type.box.setBounds (row1.removeFromLeft (juce::roundToInt (84 * s)).reduced (0, juce::roundToInt (1 * s)));
            row1.removeFromLeft (juce::roundToInt (10 * s));
            ui.sync.setBounds (row1.removeFromRight (juce::roundToInt (60 * s)));
            setLs (ui.cycle, row1, 40, 78);
            p.removeFromTop (juce::roundToInt (2 * s));

            auto row2 = p.removeFromTop (rowH);
            ui.target.label.setFont (font (13.0f));
            ui.target.label.setBounds (row2.removeFromLeft (juce::roundToInt (54 * s)));
            ui.target.box.setBounds (row2.removeFromLeft (juce::roundToInt (140 * s)).reduced (0, juce::roundToInt (1 * s)));
            row2.removeFromLeft (juce::roundToInt (10 * s));
            ui.meter = row2.removeFromRight (juce::roundToInt (58 * s)).reduced (0, juce::roundToInt (9 * s)).toFloat();
            row2.removeFromRight (juce::roundToInt (8 * s));
            setLs (ui.glow, row2, 40, 52);
            p.removeFromTop (juce::roundToInt (10 * s));
        }
    }
    r.removeFromTop (juce::roundToInt (6 * s));
    meterArea = r.removeFromTop (juce::roundToInt (10 * s)).toFloat();

    if (about != nullptr)
        about->setBounds (getLocalBounds());

    if (sizeRemembering && getWidth() > 0)
        proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);   // remembered with the session
}

void UnofficialSpotykachEditor::timerCallback()
{
    const float peak = proc.inputPeak.exchange (0.0f);
    meterLevel = juce::jmax (peak, meterLevel * 0.85f);

    const auto& st = proc.engineStatus();
    for (size_t d = 0; d < 2; ++d)
    {
        const auto& s = st.deck[d];
        juce::String state;
        if (s.recording.load())      state = "RECORDING";
        else if (s.armed.load())     state = "ARMED";
        else if (s.playing.load())   state = s.reverse.load() ? "PLAYING (rev)" : "PLAYING";
        else if (s.empty.load())     state = "EMPTY";
        else                         state = "STOPPED";
        const char* seq = s.seqRecording.load() ? "   SEQ REC" : s.seqArmed.load() ? "   SEQ ARMED" : s.seqHasData.load() ? "   SEQ" : "";
        deckUI[d].status.setText (state + "   " + juce::String (s.loopSeconds.load(), 2) + " s" + seq, juce::dontSendNotification);

        auto& ui = deckUI[d];
        ui.buttons[5].setToggleState (s.seqArmed.load() || s.seqRecording.load(), juce::dontSendNotification);
        if (ui.clearDownAt > 0.0 && ! ui.clearFired && juce::Time::getMillisecondCounterHiRes() - ui.clearDownAt >= 1500.0)
        {
            ui.clearFired = true;
            pulse (UnofficialSpotykachProcessor::deckId ((int) d, "seqClear"));
        }
        ui.buttons[6].setButtonText (ui.clearFired ? "CLEARED" : ui.clearDownAt > 0.0 ? "HOLD..." : "CLEAR");
    }

    for (size_t m = 0; m < 2; ++m)
    {
        auto& ui = modUI[m];
        const int type = juce::roundToInt (proc.apvts.getRawParameterValue (UnofficialSpotykachProcessor::modId ((int) m, "type"))->load());
        const bool sync = proc.apvts.getRawParameterValue (UnofficialSpotykachProcessor::modId ((int) m, "sync"))->load() > 0.5f;
        if (type != ui.lastType || sync != ui.lastSync)
        {
            ui.lastType = type;
            ui.lastSync = sync;
            ui.cycle.slider.updateText();
            ui.cycle.label.setText (type == usp::kModFollow ? "Release" : "Rate", juce::dontSendNotification);
        }
        ui.sync.setEnabled (type != usp::kModFollow);
    }

    for (size_t d = 0; d < 2; ++d)
    {
        auto& ui = deckUI[d];
        using P = UnofficialSpotykachProcessor;
        const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId ((int) d, "mode"))->load());
        const int snap = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId ((int) d, "pitchSnap"))->load());
        if (snap != ui.lastSnap)
        {
            // Keep what you hear unchanged when the snap mode changes: turning snap off leaves the knob at
            // the pitch that was playing; turning it on moves the knob to the nearest snapped position.
            auto& sl = ui.knobs[2].slider;
            const float v = (float) sl.getValue();
            if (ui.lastSnap > 0 && snap == 0)       sl.setValue (usp::snapPitch (v, ui.lastSnap), juce::sendNotificationSync);
            else if (ui.lastSnap >= 0 && snap != 0) sl.setValue (usp::snapKnob (v, snap), juce::sendNotificationSync);
            ui.lastSnap = snap;
            sl.updateText();
        }
        const int fm = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId ((int) d, "fluxMode"))->load());
        const int gm = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId ((int) d, "gritMode"))->load());
        if (fm != ui.lastFluxMode) { ui.lastFluxMode = fm; ui.fxKnobs[0].slider.updateText(); }
        if (gm != ui.lastGritMode) { ui.lastGritMode = gm; ui.fxKnobs[3].slider.updateText(); }
        ui.fxKnobs[5].slider.setEnabled (gm == 2);                  // Grit Character is the filter resonance
        ui.offset.slider.setEnabled (mode == 1);                    // Slice only
        ui.envLength.slider.setEnabled (mode == 2);                 // Drift only
        ui.window.slider.setEnabled (mode == 2);
        const float env = proc.apvts.getRawParameterValue (P::deckId ((int) d, "envelope"))->load();
        if (mode == 2 && env < 0.05f)
            ui.status.setText ("Drift is silent while Envelope is off", juce::dontSendNotification);
    }

    const bool hostSource = juce::roundToInt (proc.apvts.getRawParameterValue ("tempoSource")->load()) == 1;
    tempo.slider.setEnabled (! hostSource);
    tempo.label.setText (! hostSource ? "Tempo" : (st.hostLocked.load() ? "Locked" : "Host"), juce::dontSendNotification);

    const bool generative = juce::roundToInt (routeBox.getSelectedId()) == 3;
    panSpeed.slider.setEnabled (generative);
    panRange.slider.setEnabled (generative);
    repaint();
}
