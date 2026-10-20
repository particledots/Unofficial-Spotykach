// Unofficial Spotykach - Milestone 1 shell.
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
// Licensed under the GNU AGPLv3 (see LICENSE). Third-party notices: THIRD-PARTY-LICENSES.txt
#pragma once
#include "PluginProcessor.h"

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
    void timerCallback() override;

    UnofficialSpotykachProcessor& proc;

    juce::Label  titleLabel, versionLabel, statusLabel, levelLabel;
    juce::Slider levelSlider;
    juce::TextButton aboutButton { "About" };
    juce::AudioProcessorValueTreeState::SliderAttachment levelAttachment;
    std::unique_ptr<AboutPanel> about;

    float meterLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachEditor)
};
