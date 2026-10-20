// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#include "PluginEditor.h"

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
            "Milestone 2: Deck A, Reel mode only. Deck B, Slice mode, FX and MIDI control are not in yet.",
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
void UnofficialSpotykachEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 20);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
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

UnofficialSpotykachEditor::UnofficialSpotykachEditor (UnofficialSpotykachProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      levelAttachment (p.apvts, "outLevel", levelSlider)
{
    titleLabel.setText ("UNOFFICIAL SPOTYKACH  -  DECK A (REEL)", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Bold")));
    titleLabel.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (versionString(), juce::dontSendNotification);
    versionLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    versionLabel.setColour (juce::Label::textColourId, kDim);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (versionLabel);

    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    statusLabel.setColour (juce::Label::textColourId, kText);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    addKnob (knobs[0], "position", "Position");
    addKnob (knobs[1], "size",     "Size");
    addKnob (knobs[2], "pitch",    "Pitch");
    addKnob (knobs[3], "envelope", "Envelope");
    addKnob (knobs[4], "mix",      "Mix");
    addKnob (knobs[5], "feedback", "Feedback");

    recButton.onClick  = [this] { pulse ("record"); };
    playButton.onClick = [this] { pulse ("play"); };
    revButton.onClick  = [this] { pulse ("reverse"); };
    for (auto* b : { &recButton, &playButton, &revButton })
        addAndMakeVisible (*b);

    levelLabel.setText ("Output Level", juce::dontSendNotification);
    levelLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (levelLabel);
    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    levelSlider.setTextValueSuffix (" dB");
    addAndMakeVisible (levelSlider);

    aboutButton.onClick = [this]
    {
        about->setVisible (true);
        about->toFront (true);
    };
    addAndMakeVisible (aboutButton);

    about = std::make_unique<AboutPanel>();
    addChildComponent (*about);

    // setSize() calls resized(), so it must come after every child component exists.
    setSize (640, 470);

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
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (12.0f).withTrimmedTop (44.0f), 8.0f);

    const auto& st = proc.engineStatus();

    // Loop position bar
    g.setColour (kBg);
    g.fillRoundedRectangle (loopBarArea, 3.0f);
    if (! st.empty.load())
    {
        g.setColour (st.recording.load() ? kRec : kGreen);
        const float x = loopBarArea.getX() + loopBarArea.getWidth() * juce::jlimit (0.0f, 1.0f, st.playhead.load());
        g.fillRect (juce::Rectangle<float> (x - 1.5f, loopBarArea.getY(), 3.0f, loopBarArea.getHeight()));
    }
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("loop position", loopBarArea.translated (0.0f, 16.0f).toNearestInt(), juce::Justification::left);

    // Input meter
    g.setColour (kBg);
    g.fillRoundedRectangle (meterArea, 3.0f);
    g.setColour (meterLevel > 0.99f ? juce::Colours::red : kAccent);
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * juce::jlimit (0.0f, 1.0f, meterLevel)), 3.0f);
    g.setColour (kDim);
    g.drawText ("input level", meterArea.translated (0.0f, 16.0f).toNearestInt(), juce::Justification::left);
}

void UnofficialSpotykachEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (30);
    aboutButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (8);
    versionLabel.setBounds (top.removeFromRight (190));
    titleLabel.setBounds (top);

    r.removeFromTop (22);
    auto btnRow = r.removeFromTop (36);
    const int bw = 90;
    recButton.setBounds (btnRow.removeFromLeft (bw));
    btnRow.removeFromLeft (8);
    playButton.setBounds (btnRow.removeFromLeft (bw));
    btnRow.removeFromLeft (8);
    revButton.setBounds (btnRow.removeFromLeft (bw));
    btnRow.removeFromLeft (16);
    statusLabel.setBounds (btnRow);

    r.removeFromTop (10);
    auto knobRow = r.removeFromTop (150);
    const int kw = knobRow.getWidth() / 6;
    for (auto& k : knobs)
    {
        auto cell = knobRow.removeFromLeft (kw);
        k.label.setBounds (cell.removeFromTop (20));
        k.slider.setBounds (cell);
    }

    r.removeFromTop (14);
    auto lvl = r.removeFromTop (30);
    levelLabel.setBounds (lvl.removeFromLeft (100));
    levelSlider.setBounds (lvl);

    r.removeFromTop (14);
    loopBarArea = r.removeFromTop (14).toFloat();
    r.removeFromTop (26);
    meterArea = r.removeFromTop (14).toFloat();

    if (about != nullptr)
        about->setBounds (getLocalBounds());
}

void UnofficialSpotykachEditor::timerCallback()
{
    const float peak = proc.inputPeak.exchange (0.0f);
    meterLevel = juce::jmax (peak, meterLevel * 0.85f);

    const auto& st = proc.engineStatus();
    juce::String state;
    if (st.recording.load())      state = "RECORDING";
    else if (st.armed.load())     state = "ARMED (waiting for signal)";
    else if (st.playing.load())   state = st.reverse.load() ? "PLAYING (reverse)" : "PLAYING";
    else if (st.empty.load())     state = "EMPTY";
    else                          state = "STOPPED";

    statusLabel.setText (state + "\nLoop: " + juce::String (st.loopSeconds.load(), 2) + " s   "
                         + "Host: " + juce::String (proc.hostSampleRate.load(), 0) + " Hz / "
                         + juce::String (proc.hostBlockSize.load()) + " smp",
                         juce::dontSendNotification);
    repaint();
}
