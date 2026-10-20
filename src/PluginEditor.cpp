// Unofficial Spotykach - Milestone 1 shell.
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Licensed under the GNU AGPLv3 (see LICENSE). Third-party notices: THIRD-PARTY-LICENSES.txt
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
            "Milestone 1: this build contains no looper yet. It passes audio through.",
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
UnofficialSpotykachEditor::UnofficialSpotykachEditor (UnofficialSpotykachProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      levelAttachment (p.apvts, "outLevel", levelSlider)
{
    setSize (520, 340);

    titleLabel.setText ("UNOFFICIAL SPOTYKACH", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
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

    // Input meter bar
    auto m = getLocalBounds().reduced (24).removeFromBottom (36).removeFromTop (14).toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (m, 3.0f);
    g.setColour (meterLevel > 0.99f ? juce::Colours::red : kAccent);
    g.fillRoundedRectangle (m.withWidth (m.getWidth() * juce::jlimit (0.0f, 1.0f, meterLevel)), 3.0f);
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("input level", m.translated (0.0f, 16.0f).toNearestInt(), juce::Justification::left);
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
    auto row = r.removeFromTop (30);
    levelLabel.setBounds (row.removeFromLeft (100));
    levelSlider.setBounds (row);

    r.removeFromTop (10);
    statusLabel.setBounds (r.removeFromTop (120));

    about->setBounds (getLocalBounds());
}

void UnofficialSpotykachEditor::timerCallback()
{
    const float peak = proc.inputPeak.exchange (0.0f);
    meterLevel = juce::jmax (peak, meterLevel * 0.85f);

    const int note = proc.lastNote.load();
    statusLabel.setText (
        juce::String ("Host sample rate: ") + juce::String (proc.hostSampleRate.load(), 0) + " Hz\n"
        + "Host block size: " + juce::String (proc.hostBlockSize.load()) + "\n"
        + "MIDI events received: " + juce::String ((int) proc.midiEventCount.load()) + "\n"
        + "MIDI note-ons received: " + juce::String ((int) proc.midiNoteOnCount.load())
        + (note >= 0 ? juce::String ("  (last note ") + juce::String (note) + ")" : juce::String()) + "\n\n"
        + "Milestone 1: audio passes straight through. No looper yet.",
        juce::dontSendNotification);
    repaint();
}
