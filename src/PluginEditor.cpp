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
            "Milestone 3: both decks in Reel mode, routing and A/B crossfade. Slice and Drift modes, FX, modulators, tempo sync and MIDI control are not in yet.",
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

    ui.title.setText ("DECK " + tag + "  (REEL)", juce::dontSendNotification);
    ui.title.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (ui.title);
    ui.status.setColour (juce::Label::textColourId, kText);
    ui.status.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ui.status);

    static const char* btnText[UnofficialSpotykachProcessor::kNumDeckButtons] = { "REC", "PLAY", "REV", "TRIG", "REC OTHER" };
    for (int i = 0; i < UnofficialSpotykachProcessor::kNumDeckButtons; ++i)
    {
        auto& b = ui.buttons[(size_t) i];
        b.setButtonText (i == 4 ? juce::String ("REC FROM ") + (d == 0 ? "B" : "A") : juce::String (btnText[i]));
        const auto id = UnofficialSpotykachProcessor::deckId (d, UnofficialSpotykachProcessor::deckButtonBase (i));
        b.onClick = [this, id] { pulse (id); };
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

    // Resizable, fixed aspect ratio. Below 75 % the labels are no longer readable.
    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);

    int w = (int) proc.apvts.state.getProperty ("uiWidth", kBaseWidth);
    w = juce::jlimit (kBaseWidth * 3 / 4, kBaseWidth * 2, w);

    // setSize() calls resized(), so it must come after every child component exists.
    setSize (w, (int) std::lround ((double) w * kBaseHeight / kBaseWidth));

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
    ui.title.setBounds (head.removeFromLeft (head.getWidth() / 3));
    ui.status.setBounds (head);

    r.removeFromTop (juce::roundToInt (4 * s));
    auto btnRow = r.removeFromTop (juce::roundToInt (28 * s));
    const int gap = juce::roundToInt (6 * s);
    const int bw = (btnRow.getWidth() - 4 * gap) / 5;
    for (auto& b : ui.buttons)
    {
        b.setBounds (btnRow.removeFromLeft (bw));
        btnRow.removeFromLeft (gap);
    }

    r.removeFromTop (juce::roundToInt (4 * s));
    ui.loopBar = r.removeFromBottom (juce::roundToInt (8 * s)).toFloat();
    r.removeFromBottom (juce::roundToInt (4 * s));

    const int kw = r.getWidth() / 6;
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

    const int deckH = juce::roundToInt (205 * s);
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
    setLs (crossfade, r.removeFromTop (rowH), 100, 110);
    r.removeFromTop (juce::roundToInt (4 * s));

    auto row2 = r.removeFromTop (rowH);
    routeLabel.setFont (font (13.0f));
    routeLabel.setBounds (row2.removeFromLeft (juce::roundToInt (56 * s)));
    routeBox.setBounds (row2.removeFromLeft (juce::roundToInt (136 * s)));
    row2.removeFromLeft (juce::roundToInt (10 * s));
    const int half = row2.getWidth() / 2;
    setLs (panSpeed, row2.removeFromLeft (half), 62, 52);
    setLs (panRange, row2, 62, 52);
    r.removeFromTop (juce::roundToInt (4 * s));

    setLs (outLevel, r.removeFromTop (rowH), 100, 70);
    r.removeFromTop (juce::roundToInt (8 * s));
    meterArea = r.removeFromTop (juce::roundToInt (10 * s)).toFloat();

    if (about != nullptr)
        about->setBounds (getLocalBounds());

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
        deckUI[d].status.setText (state + "   " + juce::String (s.loopSeconds.load(), 2) + " s", juce::dontSendNotification);
    }

    const bool generative = juce::roundToInt (routeBox.getSelectedId()) == 3;
    panSpeed.slider.setEnabled (generative);
    panRange.slider.setEnabled (generative);
    repaint();
}
