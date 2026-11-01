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

    // Design size (M10 panel). The window keeps this aspect ratio and is resizable from 75 % to 125 %.
    static constexpr int kBaseWidth = 1280, kBaseHeight = 960;

private:
    class AboutPanel;
    class PanelLook;
    class PagePanel;
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
        juce::Colour accent;                 // amber for Deck A, violet for Deck B
        std::array<juce::TextButton, 3> modeChip;      // Reel / Slice / Drift (the Mode combo box below stays hidden and is the attachment)
        std::array<juce::TextButton, UnofficialSpotykachProcessor::kNumDeckButtons> buttons;
        std::array<Knob, 6> knobs;      // position, size, pitch, envelope, mix, feedback
        LabelledCombo mode, snap;
        Knob offset, envLength, window;
        int lastSnap = -1, lastFluxMode = -1, lastGritMode = -1;
        std::array<Knob, 6> fxKnobs;     // flux time, feedback, mix; grit intensity, mix, character
        juce::ToggleButton fluxOn { "flux" }, gritOn { "grit" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fluxOnAtt, gritOnAtt;
        LabelledCombo fluxMode, gritMode;
        double clearDownAt = 0.0;        // when CLEAR was pressed (hold 1.5 s, as on the hardware); 0 = not held
        bool clearFired = false;
        juce::Rectangle<float> ring;
        juce::Path bandBright, bandDim;  // the concentric broken rings around the pitch knob (built in layoutDeck)
    };
    struct ModUI
    {
        juce::Label title;
        LabelledCombo type, target;
        LabelledSlider cycle, glow;
        juce::ToggleButton sync { "Sync" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
        int lastType = -1;
        bool lastSync = false;
        juce::Rectangle<float> meter;
    };

    void timerCallback() override;
    void setupDeck (int deck);
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& text, juce::Colour accent);
    void addLabelled (LabelledSlider&, const juce::String& paramId, const juce::String& text, juce::Slider::SliderStyle);
    void addCombo (LabelledCombo&, const juce::String& paramId, const juce::String& text);
    void pulse (const juce::String& paramId);
    void setupMod (int mod);
    void showGlobalPage();
    void tap();
    void layoutDeck (int deck, float s);
    void drawRing (juce::Graphics&, int deck);
    juce::Font font (float size, bool bold = false) const;

    std::unique_ptr<PanelLook> laf;     // first member: destroyed last
    UnofficialSpotykachProcessor& proc;
    juce::TooltipWindow tooltipWindow { this, 600 };
    float scale = 1.0f;
    bool sizeRemembering = false;   // only store the size once construction is finished

    juce::Label titleLabel, hintLabel, versionLabel, footerLabel;
    juce::Rectangle<int> pageRect;     // where the lower page opens (below the pads), in window pixels
    juce::Rectangle<int> footerArea;
    juce::Label inputLabel;
    std::array<DeckUI, 2> deckUI;
    LabelledSlider crossfade, panSpeed, panRange, outLevel, tempo, click;
    std::array<juce::Label, 2> abLabel;           // the crossfade ends: [0] = B (top), [1] = A (bottom)
    std::array<ModUI, 2> modUI;
    juce::TextButton setupTab { "setup" }, modsTab { "mods" }, cvTab { "cv" }, midiTab { "midi" }, filesTab { "files" }, tapButton { "tap" };
    int page = -1;                     // -1 closed, 0 setup, 1 mods, 2 cv, 3 midi, 4 files
    std::unique_ptr<PagePanel> pagePanel;
    std::vector<juce::Component*> pageWidgets;   // everything that lives on a page (raised above the panel when it opens)

    // FILES page: loop files per deck, the home folder and the four trims.
    std::array<juce::TextButton, 2> loadButton, saveButton;
    std::array<juce::Label, 2> fileTag, fileStatus;
    juce::TextButton folderButton { "FOLDER..." };
    juce::Label folderLabel;
    std::array<LabelledSlider, 4> trimSliders;   // per deck: input, output
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseLoad (int deck);
    void chooseSave (int deck);
    void chooseFolder();
    void updateFilePage();
    std::array<LabelledSlider, 8> cvSliders;     // per deck: V/Oct, Mix, Position, Size
    LabelledSlider cvCrossfade;
    std::array<LabelledSlider, 2> fitBeats;
    std::array<LabelledCombo, 2> midiChannel;
    std::array<juce::ToggleButton, 2> follow;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 2> followAtt;
    juce::Label midiMonitor, midiHint;
    std::array<juce::Label, 2> cvTag;           // "A" / "B" in front of each deck's CV rows
    juce::String infoText;             // short message shown at the bottom (e.g. Fit tempo refused)
    double infoUntil = 0.0;
    uint32_t seenFitFail = 0;
    std::vector<double> tapTimes;     // message thread only
    LabelledCombo tempoSource, quantise;
    juce::Label routeLabel;
    juce::ComboBox routeBox;           // hidden: it is the parameter attachment; the three dots below drive it
    std::array<juce::ToggleButton, 3> routeBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> routeAttachment;
    juce::TextButton aboutButton { "about" };
    std::unique_ptr<AboutPanel> about;

    float meterLevel = 0.0f;
    juce::Rectangle<float> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnofficialSpotykachEditor)
};
