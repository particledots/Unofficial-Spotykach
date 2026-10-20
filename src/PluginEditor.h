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

    static constexpr int kBaseWidth = 640, kBaseHeight = 712;

private:
    class AboutPanel;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> attachment;
    };
    struct LabelledSlider
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> attachment;
    };
    struct LabelledCombo
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
    struct DeckUI
    {
        juce::Label title, status;
        std::array<juce::TextButton, UnofficialSpotykachProcessor::kNumDeckButtons> buttons;
        std::array<Knob, 6> knobs;      // position, size, pitch, envelope, mix, feedback
        LabelledCombo mode, snap;
        LabelledSlider offset, envLength, window;
        int lastSnap = -1;
        juce::Rectangle<float> panel, loopBar;
    };

    void timerCallback() override;
    void setupDeck (int deck);
    void addLabelled (LabelledSlider&, const juce::String& paramId, const juce::String& text, juce::Slider::SliderStyle);
    void addCombo (LabelledCombo&, const juce::String& paramId, const juce::String& text);
    void pulse (const juce::String& paramId);
    void layoutDeck (int deck, juce::Rectangle<int> area, float s);
    juce::Font font (float size, bool bold = false) const;

    UnofficialSpotykachProcessor& proc;
    float scale = 1.0f;
    bool sizeRemembering = false;   // only store the size once construction is finished

    juce::Label titleLabel, versionLabel;
    std::array<DeckUI, 2> deckUI;
    LabelledSlider crossfade, panSpeed, panRange, outLevel, tempo, click;
    LabelledCombo tempoSource, quantise;
    juce::Label routeLabel;
    juce::ComboBox routeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> routeAttachment;
    juce::TextButton aboutButton { "About" };
    std::unique_ptr<AboutPanel> about;

    float meterLevel = 0.0f;
    juce::Rectangle<float> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachEditor)
};
