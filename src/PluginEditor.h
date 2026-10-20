// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Third-party notices: THIRD-PARTY-LICENSES.txt
#pragma once
#include "PluginProcessor.h"
#include <array>

class UnofficialSpotykachEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit UnofficialSpotykachEditor (UnofficialSpotykachProcessor&);
    ~UnofficialSpotykachEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class AboutPanel;
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& paramId, const juce::String& text);
    void pulse (const juce::String& paramId);

    UnofficialSpotykachProcessor& proc;

    juce::Label titleLabel, versionLabel, statusLabel, levelLabel, modeLabel;
    std::array<Knob, 6> knobs;     // position, size, pitch, envelope, mix, feedback
    juce::TextButton recButton { "REC" }, playButton { "PLAY" }, revButton { "REV" };
    juce::TextButton aboutButton { "About" };
    juce::Slider levelSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment levelAttachment;
    std::unique_ptr<AboutPanel> about;

    float meterLevel = 0.0f;
    juce::Rectangle<float> meterArea, loopBarArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachEditor)
};
