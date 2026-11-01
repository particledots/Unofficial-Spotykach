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

    // Palette of the M10 panel (the Audrey-style look: near-black, cream knobs, one accent per deck).
    const juce::Colour kBg     { 0xff0b0b0c };
    const juce::Colour kPanel  { 0xff141416 };
    const juce::Colour kPanel2 { 0xff1b1b1d };
    const juce::Colour kBorder { 0xff2e2d2a };
    const juce::Colour kEdge   { 0xff5b574d };
    const juce::Colour kTrack  { 0xff3b3a36 };
    const juce::Colour kText   { 0xfff1e9d6 };
    const juce::Colour kLabel  { 0xffd9d2c0 };
    const juce::Colour kDim    { 0xffa39d8f };
    const juce::Colour kCap    { 0xfff3ecda };
    const juce::Colour kAccent { 0xffe8a33d };     // Deck A, and general highlights
    const juce::Colour kViolet { 0xffb48ae0 };     // Deck B
    const juce::Colour kRec    { 0xffd9423a };
    const juce::Colour kGreen  { 0xff4caf6a };

    // Window positions are given in the 1280 x 960 design size and scaled.
    constexpr float kDeckW = 540.0f, kDeckAX = 20.0f, kDeckBX = 720.0f;
    constexpr float kRingCX = 255.0f, kRingCY = 370.0f, kRingR = 150.0f;
}

//==============================================================================
// Look of the M10 panel: cream knobs on near-black, outlined pill buttons, text tabs.
class UnofficialSpotykachEditor::PanelLook : public juce::LookAndFeel_V4
{
public:
    PanelLook()
    {
        setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonOnColourId, kAccent);
        setColour (juce::TextButton::textColourOffId, kText);
        setColour (juce::TextButton::textColourOnId, kBg);
        setColour (juce::ComboBox::backgroundColourId, kPanel2);
        setColour (juce::ComboBox::outlineColourId, kEdge);
        setColour (juce::ComboBox::textColourId, kText);
        setColour (juce::ComboBox::arrowColourId, kDim);
        setColour (juce::PopupMenu::backgroundColourId, kPanel);
        setColour (juce::PopupMenu::textColourId, kText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, kEdge);
        setColour (juce::PopupMenu::highlightedTextColourId, kText);
        setColour (juce::Slider::textBoxTextColourId, kText);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, kEdge);
        setColour (juce::Label::textColourId, kText);
        setColour (juce::ToggleButton::textColourId, kText);
        setColour (juce::ToggleButton::tickColourId, kAccent);
        setColour (juce::TooltipWindow::backgroundColourId, kPanel);
        setColour (juce::TooltipWindow::textColourId, kText);
        setColour (juce::TooltipWindow::outlineColourId, kEdge);
    }

    // The value text under a knob scales with the knob's text box.
    juce::Font getLabelFont (juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
            return juce::Font (juce::FontOptions (juce::jlimit (10.0f, 24.0f, (float) l.getHeight() * 0.64f)));
        return l.getFont();
    }

    // Knobs: the dial on top, the value text box directly under it. The vertical fader leaves a gap
    // (property "gap", in pixels) between the fader and its value, where the "a" label sits.
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        if (s.getTextBoxPosition() == juce::Slider::TextBoxBelow && (s.isRotary() || s.isVertical()))
        {
            juce::Slider::SliderLayout l;
            auto b = s.getLocalBounds();
            const int gap = (int) s.getProperties().getWithDefault ("gap", 0);
            l.textBoxBounds = b.removeFromBottom (s.getTextBoxHeight());
            l.sliderBounds = b.withTrimmedBottom (gap);
            return l;
        }
        return juce::LookAndFeel_V4::getSliderLayout (s);
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        if (s.isVertical()) return juce::roundToInt (juce::jmin ((float) s.getWidth() * 0.4f, 38.0f) * 0.62f * 0.5f) + 1;
        return juce::LookAndFeel_V4::getSliderThumbRadius (s);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const float u = d / 100.0f;
        const auto c = b.getCentre();
        const float alpha = s.isEnabled() ? 1.0f : 0.38f;
        const float thick = juce::jmax (2.0f, 3.5f * u);
        const float r = 46.0f * u;
        const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (kTrack.withMultipliedAlpha (alpha));
        g.strokePath (track, stroke);

        const float ang = a0 + pos * (a1 - a0);
        if (pos > 0.004f)
        {
            juce::Path v;
            v.addCentredArc (c.x, c.y, r, r, 0.0f, a0, ang, true);
            g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
            g.strokePath (v, stroke);
        }
        g.setColour (kCap.withMultipliedAlpha (alpha));
        g.fillEllipse (c.x - 37.0f * u, c.y - 37.0f * u, 74.0f * u, 74.0f * u);
        const float s0 = std::sin (ang), c0 = std::cos (ang);
        g.setColour (juce::Colour (0xff111111).withMultipliedAlpha (alpha));
        g.drawLine (c.x + 14.0f * u * s0, c.y - 14.0f * u * c0, c.x + 33.0f * u * s0, c.y - 33.0f * u * c0, juce::jmax (2.0f, 4.0f * u));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        const float alpha = s.isEnabled() ? 1.0f : 0.35f;
        if (style == juce::Slider::LinearVertical)
        {
            // The crossfade fader: a thin track and a cream handle with a dark line, like the module's fader.
            const float cx = (float) x + (float) w * 0.5f;
            g.setColour (kTrack.withMultipliedAlpha (alpha));
            g.drawLine (cx, (float) y, cx, (float) (y + h), 3.0f);
            const float hw = juce::jmin ((float) w * 0.4f, 38.0f), hh = hw * 0.62f;
            const auto hand = juce::Rectangle<float> (cx - hw * 0.5f, pos - hh * 0.5f, hw, hh);
            g.setColour (kCap.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (hand, hh * 0.25f);
            g.setColour (juce::Colour (0xff111111));
            g.drawLine (hand.getX() + hw * 0.22f, pos, hand.getRight() - hw * 0.22f, pos, 2.0f);
            return;
        }
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        const float th = 4.0f, cy = (float) y + (float) h * 0.5f;
        g.setColour (kTrack.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, (float) w, th, th * 0.5f);
        g.setColour (s.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, juce::jmax (th, pos - (float) x), th, th * 0.5f);
        const float tr = juce::jmin (6.0f, (float) h * 0.45f);
        g.setColour (kCap.withMultipliedAlpha (alpha));
        g.fillEllipse (pos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("tab"))
        {
            if (on)
            {
                g.setColour (kCap);
                g.fillRect (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getWidth() - 4.0f, 2.0f);
            }
            return;
        }
        const float a = b.isEnabled() ? 1.0f : 0.4f;
        const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
        const auto fill = on ? onCol : down ? kTrack : hi ? kPanel2.brighter (0.2f) : juce::Colours::transparentBlack;
        const float rad = r.getHeight() * 0.5f;
        g.setColour (fill.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, rad);
        g.setColour ((on ? onCol : hi ? kDim : kEdge).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if (b.getProperties().contains ("tab"))
            return juce::Font (juce::FontOptions (juce::jlimit (10.0f, 18.0f, (float) h * 0.55f)));
        return juce::Font (juce::FontOptions ((float) juce::jlimit (9, 14, (int) ((float) h * 0.42f))).withStyle ("Bold"));
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        const bool tab = b.getProperties().contains ("tab");
        auto col = tab ? (b.getToggleState() ? kText : kDim)
                       : b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        g.setColour (col.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.7f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        const float a = b.isEnabled() ? 1.0f : 0.4f;
        const float W = (float) b.getWidth(), H = (float) b.getHeight();
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("switch"))
        {
            // Pill switch with its name underneath (Flux on, Grit on).
            const float ph = juce::jmin (H * 0.5f, 24.0f), pw = ph * 1.8f;
            const auto pill = juce::Rectangle<float> ((W - pw) * 0.5f, 1.0f, pw, ph);
            g.setColour ((on ? kCap : kTrack).withMultipliedAlpha (a));
            g.fillRoundedRectangle (pill, ph * 0.5f);
            const float tr = ph * 0.36f;
            const float tx = on ? pill.getRight() - ph * 0.5f : pill.getX() + ph * 0.5f;
            g.setColour ((on ? juce::Colour (0xff111111) : kCap).withMultipliedAlpha (a));
            g.fillEllipse (tx - tr, pill.getCentreY() - tr, tr * 2.0f, tr * 2.0f);
            g.setColour (kLabel.withMultipliedAlpha (a));
            g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 15.0f, H * 0.3f))));
            g.drawText (b.getButtonText(), juce::Rectangle<float> (0.0f, ph + 3.0f, W, H - ph - 3.0f), juce::Justification::centredTop);
            return;
        }
        if (b.getProperties().contains ("dot"))
        {
            // Routing choice: a dot and its name.
            const float dr = juce::jmin (4.5f, H * 0.25f);
            const float cy = H * 0.5f;
            g.setColour ((on ? kCap : kDim).withMultipliedAlpha (a));
            if (on) g.fillEllipse (2.0f, cy - dr, dr * 2.0f, dr * 2.0f);
            else    g.drawEllipse (2.0f + 0.5f, cy - dr + 0.5f, dr * 2.0f - 1.0f, dr * 2.0f - 1.0f, 1.0f);
            g.setColour ((on ? kText : kDim).withMultipliedAlpha (a));
            g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 15.0f, H * 0.6f))));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) (dr * 2.0f + 10.0f)), juce::Justification::centredLeft, 1, 0.8f);
            return;
        }
        const float box = juce::jmin (16.0f, H - 4.0f);
        const auto r = juce::Rectangle<float> (2.0f, (H - box) * 0.5f, box, box);
        g.setColour (kPanel2.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (kEdge.withMultipliedAlpha (a));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        if (on)
        {
            g.setColour (b.findColour (juce::ToggleButton::tickColourId).withMultipliedAlpha (a));
            g.fillRoundedRectangle (r.reduced (3.5f), 2.0f);
        }
        g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (a));
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 15.0f, H * 0.55f))));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) (box + 8.0f)), juce::Justification::centredLeft, 1, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const float a = box.isEnabled() ? 1.0f : 0.45f;
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (kPanel2.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (kEdge.withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (kDim.withMultipliedAlpha (a));
        g.strokePath (chevron, juce::PathStrokeType (1.5f));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (juce::FontOptions (juce::jlimit (10.0f, 15.0f, (float) box.getHeight() * 0.55f)));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};

//==============================================================================
// The panel behind the lower pages (setup, mods, cv, midi, files). The page's controls sit on top of it.
class UnofficialSpotykachEditor::PagePanel : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (kBorder);
        g.drawRoundedRectangle (r, 14.0f, 1.0f);
        if (drawExtra) drawExtra (g);
    }

    std::function<void (juce::Graphics&)> drawExtra;     // drawn above the background (the mod meters)
};

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
            "LICENSE and THIRD-PARTY-LICENSES.txt that came with the download. Source code: github.com/particledots/Unofficial-Spotykach.\n\n"
            "Both decks with Reel, Slice and Drift modes, Flux and Grit effects, two modulators that you can patch to a destination, a trigger sequencer per deck, MIDI notes and CCs (firmware map), CV-style automation inputs, fit tempo to loop, routing, A/B crossfade, tempo (internal, host or tap), launch quantise, loading and saving WAV files, and input and output trims. "
            "The window is laid out after the module's front panel (a plug-in interpretation, not a copy of Synthux's artwork). Loops come back with the project through a link to their WAV file (the project holds no audio).",
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

// A knob: arc and cream cap, the value under it (the slider's text box) and a name under that.
void UnofficialSpotykachEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text, juce::Colour accent)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kLabel);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<SliderAtt> (proc.apvts, paramId, k.slider);
}

void UnofficialSpotykachEditor::setupDeck (int d)
{
    using P = UnofficialSpotykachProcessor;
    auto& ui = deckUI[(size_t) d];
    const juce::String tag = d == 0 ? "A" : "B";

    ui.accent = d == 0 ? kAccent : kViolet;
    ui.title.setText (d == 0 ? "deck a" : "deck b", juce::dontSendNotification);
    ui.title.setColour (juce::Label::textColourId, ui.accent);
    addAndMakeVisible (ui.title);
    ui.status.setColour (juce::Label::textColourId, kDim);
    ui.status.setJustificationType (juce::Justification::centred);
    ui.status.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (ui.status);

    static const char* btnText[P::kNumDeckButtons] = { "rec", "play", "rev", "trig", "from", "seq", "clear", "FIT" };
    static const char* btnTip[P::kNumDeckButtons] = {
        "Record from the plug-in input (press again to stop)", "Play / stop", "Play in reverse / stop",
        "Trigger the loop with the current settings",
        "Record the other deck's output into this deck (press again to stop)",
        "Trigger sequencer: press to arm, then tap TRIG to record a rhythm; press SEQ again to stop recording",
        "Hold for 1.5 seconds to clear the trigger sequence",
        "Set the tempo so this deck's loop lasts the chosen number of beats (Source becomes Internal)" };
    for (int i = 0; i < P::kNumDeckButtons; ++i)
    {
        auto& b = ui.buttons[(size_t) i];
        b.setButtonText (i == 4 ? juce::String ("from ") + (d == 0 ? "b" : "a") : i == 7 ? juce::String ("FIT ") + tag : juce::String (btnText[i]));
        b.setTooltip (btnTip[i]);
        const auto id = P::deckId (d, P::deckButtonBase (i));
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
        if (i == 0 || i == 5) { b.setColour (juce::TextButton::buttonOnColourId, kRec); b.setColour (juce::TextButton::textColourOnId, kText); }
        if (i == 1 || i == 2) b.setColour (juce::TextButton::buttonOnColourId, ui.accent);
        if (i < P::kRowButtons) addAndMakeVisible (b);
        else addChildComponent (b);
    }

    static const char* base[6] = { "position", "size", "pitch", "envelope", "mix", "feedback" };
    static const char* text[6] = { "position", "size", "pitch", "envelope", "mix", "feedback" };
    for (size_t i = 0; i < 6; ++i)
        setupKnob (ui.knobs[i], P::deckId (d, base[i]), text[i], ui.accent);

    addCombo (ui.mode, P::deckId (d, "mode"), "Mode");
    ui.mode.label.setVisible (false);
    ui.mode.box.setVisible (false);      // the buttons below drive it; the combo stays as the parameter attachment
    static const char* chipText[3] = { "reel", "slice", "drift" };
    for (int i = 0; i < 3; ++i)
    {
        auto& c = ui.modeChip[(size_t) i];
        c.setButtonText (chipText[i]);
        c.setColour (juce::TextButton::buttonOnColourId, ui.accent);
        c.setTooltip ("Deck mode");
        c.onClick = [this, d, i] { deckUI[(size_t) d].mode.box.setSelectedItemIndex (i, juce::sendNotificationSync); };
        addAndMakeVisible (c);
    }
    addCombo (ui.snap, P::deckId (d, "pitchSnap"), "pitch snap");
    ui.snap.label.setColour (juce::Label::textColourId, kLabel);
    ui.snap.label.setJustificationType (d == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight);
    setupKnob (ui.offset,    P::deckId (d, "sliceOffset"), "slice offset", ui.accent);
    setupKnob (ui.envLength, P::deckId (d, "envLength"),   "env length",   ui.accent);
    setupKnob (ui.window,    P::deckId (d, "window"),      "window",       ui.accent);

    // Flux (delay) and Grit (drive / reduce / filter): always on the panel, as in the mockup.
    static const char* fxBase[6] = { "fluxTime", "fluxFeedback", "fluxMix", "gritIntensity", "gritMix", "gritCharacter" };
    static const char* fxText[6] = { "time", "feedback", "mix", "intensity", "mix", "character" };
    for (size_t i = 0; i < 6; ++i)
        setupKnob (ui.fxKnobs[i], P::deckId (d, fxBase[i]), fxText[i], ui.accent);
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
        b->getProperties().set ("switch", true);
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, ui.accent);
        addAndMakeVisible (*b);
    }
    ui.fluxOn.setTooltip ("Flux on (latching; the firmware's pad is momentary unless Alt is held)");
    ui.gritOn.setTooltip ("Grit on (latching; the firmware's pad is momentary unless Alt is held)");
    ui.fluxOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::deckId (d, "fluxOn"), ui.fluxOn);
    ui.gritOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::deckId (d, "gritOn"), ui.gritOn);
    addCombo (ui.fluxMode, P::deckId (d, "fluxMode"), "Flux Mode");
    addCombo (ui.gritMode, P::deckId (d, "gritMode"), "Grit Mode");
    ui.fluxMode.box.setTooltip ("Flux mode: Free (milliseconds) or Clocked (note lengths at the tempo)");
    ui.gritMode.box.setTooltip ("Grit mode: Drive, Reduce or Filter");
    for (auto* cb : { &ui.fluxMode, &ui.gritMode }) cb->label.setVisible (false);

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
        return juce::String (st >= 0 ? "+" : "") + juce::String (st, snap == 0 ? 1 : 0) + " st";
    };
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
    auto vis = [] (std::initializer_list<juce::Component*> l, bool v) { for (auto* c : l) c->setVisible (v); };
    const bool setup = page == 0, mods = page == 1, cv = page == 2, midi = page == 3, files = page == 4;
    vis ({ &panSpeed.slider, &panSpeed.label, &panRange.slider, &panRange.label,
           &fitBeats[0].slider, &fitBeats[0].label, &fitBeats[1].slider, &fitBeats[1].label,
           &deckUI[0].buttons[7], &deckUI[1].buttons[7] }, setup);
    for (auto& ui : modUI)
        vis ({ &ui.title, &ui.type.box, &ui.target.box, &ui.target.label, &ui.cycle.slider, &ui.cycle.label, &ui.glow.slider,
               &ui.glow.label, &ui.sync }, mods);
    for (auto& c : cvSliders) vis ({ &c.slider, &c.label }, cv);
    vis ({ &cvCrossfade.slider, &cvCrossfade.label, &cvTag[0], &cvTag[1] }, cv);
    for (int d = 0; d < 2; ++d)
        vis ({ &midiChannel[(size_t) d].box, &midiChannel[(size_t) d].label, &follow[(size_t) d] }, midi);
    vis ({ &midiMonitor, &midiHint }, midi);
    for (int d = 0; d < 2; ++d)
        vis ({ &loadButton[(size_t) d], &saveButton[(size_t) d], &fileTag[(size_t) d], &fileStatus[(size_t) d] }, files);
    for (auto& t : trimSliders) vis ({ &t.slider, &t.label }, files);
    vis ({ &folderButton, &folderLabel }, files);
    filesTab.setToggleState (files, juce::dontSendNotification);
    setupTab.setToggleState (setup, juce::dontSendNotification);
    modsTab.setToggleState (mods, juce::dontSendNotification);
    cvTab.setToggleState (cv, juce::dontSendNotification);
    midiTab.setToggleState (midi, juce::dontSendNotification);

    // The page opens as a panel over the lower part of the window; its controls are raised above the panel.
    if (pagePanel != nullptr)
    {
        pagePanel->setVisible (page >= 0);
        if (page >= 0)
        {
            pagePanel->toFront (false);
            for (auto* c : pageWidgets) c->toFront (false);
        }
    }
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
    : AudioProcessorEditor (&p), laf (std::make_unique<PanelLook>()), proc (p)
{
    setLookAndFeel (laf.get());

    titleLabel.setText ("unofficial spotykach", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);
    hintLabel.setText ("MIDI notes ch 1 / 2 | CC 3 14 15 20-28 85 86 89 90 102-109", juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, kDim);
    hintLabel.setJustificationType (juce::Justification::centredRight);
    hintLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (hintLabel);
    footerLabel.setText ("Based on the open-source Spotykach firmware (Synthux Academy, MIT). Not a Synthux Academy product.", juce::dontSendNotification);
    footerLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8c8678));
    footerLabel.setJustificationType (juce::Justification::topRight);
    footerLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (footerLabel);
    inputLabel.setText ("input level", juce::dontSendNotification);
    inputLabel.setColour (juce::Label::textColourId, kDim);
    inputLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (inputLabel);

    versionLabel.setText (versionString(), juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, kDim);
    versionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (versionLabel);

    setupDeck (0);
    setupDeck (1);

    // Global controls. Tempo, click and output are knobs like the deck knobs; the crossfade is a vertical fader.
    auto knobStyle = [] (LabelledSlider& ls)
    {
        ls.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        ls.slider.setColour (juce::Slider::rotarySliderFillColourId, kCap);
        ls.label.setJustificationType (juce::Justification::centred);
        ls.label.setColour (juce::Label::textColourId, kLabel);
    };
    addLabelled (crossfade, "crossfade", "a / b mix", juce::Slider::LinearVertical);
    crossfade.label.setJustificationType (juce::Justification::centred);
    crossfade.label.setColour (juce::Label::textColourId, kLabel);
    addLabelled (panSpeed,  "panSpeed",  "Pan Speed",     juce::Slider::LinearHorizontal);
    addLabelled (panRange,  "panRange",  "Pan Range",     juce::Slider::LinearHorizontal);
    addLabelled (outLevel,  "outLevel",  "output",        juce::Slider::RotaryHorizontalVerticalDrag);
    outLevel.slider.setTextValueSuffix (" dB");
    addLabelled (tempo, "tempo", "tempo", juce::Slider::RotaryHorizontalVerticalDrag);
    addLabelled (click, "click", "click", juce::Slider::RotaryHorizontalVerticalDrag);
    knobStyle (outLevel); knobStyle (tempo); knobStyle (click);
    addCombo (tempoSource, "tempoSource", "clock");
    addCombo (quantise, "quantise", "launch quantise");
    for (auto* l : { &tempoSource.label, &quantise.label })
    {
        l->setColour (juce::Label::textColourId, kDim);
        l->setJustificationType (juce::Justification::centred);
    }
    for (auto* ls : { &panSpeed, &panRange }) ls->label.setColour (juce::Label::textColourId, kText);
    abLabel[0].setText ("b", juce::dontSendNotification);
    abLabel[1].setText ("a", juce::dontSendNotification);
    abLabel[0].setColour (juce::Label::textColourId, kViolet);
    abLabel[1].setColour (juce::Label::textColourId, kAccent);
    for (auto& l : abLabel) { l.setJustificationType (juce::Justification::centred); addAndMakeVisible (l); }
    setupMod (0);
    setupMod (1);
    addAndMakeVisible (tapButton);
    tapButton.setTooltip ("Tap the tempo (sets Source to Internal)");
    tapButton.onClick = [this] { tap(); };
    for (auto* b : { &setupTab, &modsTab, &cvTab, &midiTab, &filesTab })
    {
        b->getProperties().set ("tab", true);
        addAndMakeVisible (*b);
    }
    setupTab.setButtonText ("setup"); modsTab.setButtonText ("mods"); cvTab.setButtonText ("cv");
    midiTab.setButtonText ("midi");   filesTab.setButtonText ("files");
    // A tab opens its page over the lower part of the window; the same tab again closes it.
    setupTab.onClick = [this] { page = page == 0 ? -1 : 0; showGlobalPage(); resized(); };
    modsTab.onClick  = [this] { page = page == 1 ? -1 : 1; showGlobalPage(); resized(); };
    cvTab.onClick    = [this] { page = page == 2 ? -1 : 2; showGlobalPage(); resized(); };
    midiTab.onClick  = [this] { page = page == 3 ? -1 : 3; showGlobalPage(); resized(); };
    filesTab.onClick = [this] { page = page == 4 ? -1 : 4; showGlobalPage(); resized(); };
    setupTab.setTooltip ("Generative pan and fit tempo to loop");
    modsTab.setTooltip ("Modulators");
    cvTab.setTooltip ("CV-style automation inputs");
    midiTab.setTooltip ("MIDI channels, transport and monitor");
    filesTab.setTooltip ("Load and save loops, input and output trims");

    pagePanel = std::make_unique<PagePanel>();
    addChildComponent (*pagePanel);

    // FILES page: load / save a deck's loop as a WAV, the home folder, and the trims.
    for (int d = 0; d < 2; ++d)
    {
        const auto i = (size_t) d;
        loadButton[i].setButtonText ("LOAD...");
        saveButton[i].setButtonText ("SAVE...");
        loadButton[i].setTooltip ("Load a WAV file into this deck (any sample rate; mono or stereo; cut to 42 s; cue points become slice points)");
        saveButton[i].setTooltip ("Save this deck's loop as a WAV file. Nothing is saved automatically.");
        loadButton[i].onClick = [this, d] { chooseLoad (d); };
        saveButton[i].onClick = [this, d] { chooseSave (d); };
        fileTag[i].setText (d == 0 ? "A" : "B", juce::dontSendNotification);
        fileTag[i].setColour (juce::Label::textColourId, d == 0 ? kAccent : kViolet);
        fileStatus[i].setColour (juce::Label::textColourId, kDim);
        fileStatus[i].setMinimumHorizontalScale (1.0f);
        fileStatus[i].setJustificationType (juce::Justification::centredLeft);
        for (auto* c : std::initializer_list<juce::Component*> { &loadButton[i], &saveButton[i], &fileTag[i], &fileStatus[i] })
            addChildComponent (*c);

        addLabelled (trimSliders[i * 2],     UnofficialSpotykachProcessor::deckId (d, "inTrim"),  d == 0 ? "In A" : "In B",   juce::Slider::LinearHorizontal);
        addLabelled (trimSliders[i * 2 + 1], UnofficialSpotykachProcessor::deckId (d, "outTrim"), d == 0 ? "Out A" : "Out B", juce::Slider::LinearHorizontal);
        for (size_t k = 0; k < 2; ++k)
        {
            trimSliders[i * 2 + k].slider.setTextValueSuffix (" dB");
            trimSliders[i * 2 + k].slider.setDoubleClickReturnValue (true, 0.0);
        }
    }
    folderButton.onClick = [this] { chooseFolder(); };
    folderButton.setTooltip ("Where LOAD and SAVE start, and where a moved loop file is searched for");
    folderLabel.setColour (juce::Label::textColourId, kDim);
    folderLabel.setMinimumHorizontalScale (1.0f);
    addChildComponent (folderButton);
    addChildComponent (folderLabel);

    // CV page: the firmware's CV inputs as automatable parameters.
    {
        static const char* cvId[4]   = { "cvPitch", "cvMix", "cvPosition", "cvSize" };
        static const char* cvText[4] = { "V/Oct", "Mix", "Position", "Size" };
        for (int d = 0; d < 2; ++d)
            for (int i = 0; i < 4; ++i)
            {
                auto& c = cvSliders[(size_t) (d * 4 + i)];
                addLabelled (c, UnofficialSpotykachProcessor::deckId (d, cvId[i]), cvText[i], juce::Slider::LinearHorizontal);
                c.slider.setDoubleClickReturnValue (true, 0.0);
            }
        addLabelled (cvCrossfade, "cvCrossfade", "Crossfade CV", juce::Slider::LinearHorizontal);
        cvCrossfade.slider.setDoubleClickReturnValue (true, 0.0);
    }
    for (int d = 0; d < 2; ++d)
    {
        addLabelled (fitBeats[(size_t) d], UnofficialSpotykachProcessor::deckId (d, "fitBeats"), "beats", juce::Slider::LinearHorizontal);
        fitBeats[(size_t) d].slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt ((float) v)) + (juce::roundToInt ((float) v) == 1 ? " beat" : " beats"); };
        fitBeats[(size_t) d].slider.updateText();
        fitBeats[(size_t) d].slider.setTooltip ("Fit tempo to loop: the loop will last this many beats");
        addCombo (midiChannel[(size_t) d], UnofficialSpotykachProcessor::deckId (d, "midiChannel"), d == 0 ? "Deck A channel" : "Deck B channel");
        follow[(size_t) d].setButtonText ("Follow transport");
        follow[(size_t) d].setColour (juce::ToggleButton::textColourId, kText);
        follow[(size_t) d].setColour (juce::ToggleButton::tickColourId, kAccent);
        follow[(size_t) d].setTooltip ("Host play/stop (and MIDI Start/Continue/Stop) plays and stops this deck");
        addAndMakeVisible (follow[(size_t) d]);
        followAtt[(size_t) d] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, UnofficialSpotykachProcessor::deckId (d, "transportFollow"), follow[(size_t) d]);
    }
    for (int d = 0; d < 2; ++d)
    {
        cvTag[(size_t) d].setText (d == 0 ? "A" : "B", juce::dontSendNotification);
        cvTag[(size_t) d].setColour (juce::Label::textColourId, d == 0 ? kAccent : kViolet);
        addChildComponent (cvTag[(size_t) d]);
    }
    midiMonitor.setColour (juce::Label::textColourId, kAccent);
    midiHint.setColour (juce::Label::textColourId, kDim);
    midiHint.setText ("Notes trigger the loop (note 60 = normal pitch). CCs follow the Spotykach firmware's list; see the README.", juce::dontSendNotification);
    addAndMakeVisible (midiMonitor);
    addAndMakeVisible (midiHint);
    seenFitFail = proc.engineStatus().fitFailSeq.load();

    routeLabel.setText ("routing", juce::dontSendNotification);
    routeLabel.setColour (juce::Label::textColourId, kDim);
    routeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (routeLabel);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("route")))
        routeBox.addItemList (choice->choices, 1);
    addChildComponent (routeBox);           // hidden: the parameter attachment; the three dots drive it
    routeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "route", routeBox);
    {
        static const char* routeText[3] = { "dual mono", "stereo", "generative" };
        static const char* routeTip[3] = {
            "Input A feeds Deck A, input B feeds Deck B; outputs A (left) and B (right)",
            "Both inputs feed both decks; outputs are left and right",
            "Generative: the decks are panned randomly (see Pan Speed and Pan Range on the setup tab)" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = routeBtn[(size_t) i];
            b.setButtonText (routeText[i]);
            b.setTooltip (routeTip[i]);
            b.getProperties().set ("dot", true);
            b.onClick = [this, i] { routeBox.setSelectedItemIndex (i, juce::sendNotificationSync); };
            addAndMakeVisible (b);
        }
    }

    aboutButton.onClick = [this]
    {
        about->setVisible (true);
        about->toFront (true);
    };
    addAndMakeVisible (aboutButton);

    about = std::make_unique<AboutPanel>();
    addChildComponent (*about);

    // Everything that lives on a lower page; raised above the page panel when a page opens.
    pageWidgets = { &panSpeed.slider, &panSpeed.label, &panRange.slider, &panRange.label,
                    &fitBeats[0].slider, &fitBeats[0].label, &fitBeats[1].slider, &fitBeats[1].label,
                    &deckUI[0].buttons[7], &deckUI[1].buttons[7],
                    &cvCrossfade.slider, &cvCrossfade.label, &cvTag[0], &cvTag[1],
                    &midiMonitor, &midiHint, &folderButton, &folderLabel };
    for (auto& ui : modUI)
        for (auto* c : std::initializer_list<juce::Component*> { &ui.title, &ui.type.box, &ui.target.box, &ui.target.label,
                                                                   &ui.cycle.slider, &ui.cycle.label, &ui.glow.slider, &ui.glow.label, &ui.sync })
            pageWidgets.push_back (c);
    for (auto& c : cvSliders) { pageWidgets.push_back (&c.slider); pageWidgets.push_back (&c.label); }
    for (size_t d = 0; d < 2; ++d)
        for (auto* c : std::initializer_list<juce::Component*> { &midiChannel[d].box, &midiChannel[d].label, &follow[d],
                                                                   &loadButton[d], &saveButton[d], &fileTag[d], &fileStatus[d] })
            pageWidgets.push_back (c);
    for (auto& t : trimSliders) { pageWidgets.push_back (&t.slider); pageWidgets.push_back (&t.label); }

    // The mod meters belong to the MODS page: they are drawn by the page panel, above its background.
    pagePanel->drawExtra = [this] (juce::Graphics& g)
    {
        if (page != 1) return;
        const auto& st = proc.engineStatus();
        g.addTransform (juce::AffineTransform::translation ((float) -pageRect.getX(), (float) -pageRect.getY()));
        for (size_t m = 0; m < 2; ++m)
        {
            g.setColour (kPanel2);
            g.fillRoundedRectangle (modUI[m].meter, 3.0f * scale);
            g.setColour (m == 0 ? kAccent : kViolet);
            g.fillRoundedRectangle (modUI[m].meter.withWidth (modUI[m].meter.getWidth() * juce::jlimit (0.0f, 1.0f, st.modValue[m].load())), 3.0f * scale);
        }
    };
    showGlobalPage();

    // Read the remembered width FIRST: setResizeLimits() below triggers resized() at size 0, which would
    // otherwise overwrite the stored value before it is read.
    int w = (int) proc.apvts.state.getProperty ("uiWidth", 0);
    if (w <= 0)
    {
        // First time: open at 100 %, or smaller if the screen is not tall enough (a 1440 x 900 laptop gets about 85 %).
        w = kBaseWidth;
        if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const int usableH = disp->userBounds.getHeight();
            if (usableH > 200)
                w = juce::jmin (w, (int) std::lround ((double) usableH * 0.92 * (double) kBaseWidth / (double) kBaseHeight));
        }
    }

    // Resizable, fixed aspect ratio. Below 75 % the labels are no longer readable.
    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 5 / 4, kBaseHeight * 5 / 4);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);

    w = juce::jlimit (kBaseWidth * 3 / 4, kBaseWidth * 5 / 4, w);

    // setSize() calls resized(), so it must come after every child component exists.
    setSize (w, (int) std::lround ((double) w * kBaseHeight / kBaseWidth));
    sizeRemembering = true;

    startTimerHz (30);
    timerCallback();
}

UnofficialSpotykachEditor::~UnofficialSpotykachEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}


namespace
{
    // A knob in the 1280 x 960 design space: the dial centred on (cx, cy) with diameter d, the value text
    // directly under it (the slider's text box) and the name under that.
    void placeKnob (juce::Slider& sl, juce::Label& lb, float s, float cx, float cy, float d, float boxW, bool big)
    {
        auto px = [s] (float v) { return juce::roundToInt (v * s); };
        const int tb = px (big ? 26.0f : 22.0f);
        const int top = px (cy - d * 0.5f);
        sl.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (boxW), tb);
        sl.setBounds (px (cx - boxW * 0.5f), top, px (boxW), px (d) + tb);
        lb.setFont (juce::Font (juce::FontOptions ((big ? 14.0f : 12.0f) * s)));
        lb.setBounds (px (cx - boxW * 0.5f), top + px (d) + tb - px (2.0f), px (boxW), px (18.0f));
    }

    void arrow (juce::Graphics& g, const std::vector<juce::Point<float>>& pts, float s)
    {
        if (pts.size() < 2) return;
        juce::Path p;
        p.startNewSubPath (pts[0]);
        for (size_t i = 1; i < pts.size(); ++i) p.lineTo (pts[i]);
        g.setColour (juce::Colour (0xff8c8678));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.0f, s), juce::PathStrokeType::curved));
        const auto a = pts[pts.size() - 2], b = pts.back();
        const float ang = std::atan2 (b.y - a.y, b.x - a.x), len = 7.0f * s;
        juce::Path h;
        h.startNewSubPath (b.x - len * std::cos (ang - 0.5f), b.y - len * std::sin (ang - 0.5f));
        h.lineTo (b);
        h.lineTo (b.x - len * std::cos (ang + 0.5f), b.y - len * std::sin (ang + 0.5f));
        g.setColour (juce::Colour (0xffbdb6a5));
        g.strokePath (h, juce::PathStrokeType (juce::jmax (1.0f, s * 1.3f)));
    }
}

void UnofficialSpotykachEditor::drawRing (juce::Graphics& g, int d)
{
    const auto& ui = deckUI[(size_t) d];
    const auto& st = proc.engineStatus().deck[(size_t) d];
    const auto c = ui.ring.getCentre();
    const float R = ui.ring.getWidth() * 0.5f;          // the outer playhead ring
    const bool empty = st.empty.load(), rec = st.recording.load(), armed = st.armed.load(), playing = st.playing.load();
    const bool rev = st.reverse.load();
    const float head = juce::jlimit (0.0f, 1.0f, st.playhead.load());
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // the concentric broken rings
    g.setColour (juce::Colour (0xff8c8678));
    g.strokePath (ui.bandDim, juce::PathStrokeType (juce::jmax (1.0f, scale)));
    g.setColour (juce::Colour (0xffd9d2c0));
    g.strokePath (ui.bandBright, juce::PathStrokeType (juce::jmax (1.0f, 1.4f * scale)));

    // the outer ring: loop state and playhead
    const float thick = juce::jmax (2.0f, 3.0f * scale);
    juce::Path full;
    full.addEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour (kTrack);
    g.strokePath (full, juce::PathStrokeType (thick * 0.6f));
    if (rec || armed || ! empty)
    {
        g.setColour (rec ? kRec.withAlpha (0.9f) : armed ? kRec.withAlpha (0.4f) : ui.accent.withAlpha (playing ? 0.30f : 0.45f));
        g.strokePath (full, juce::PathStrokeType (thick * 0.6f));
    }
    if (! empty && playing && ! rec)
    {
        const float th = head * twoPi, len = 0.22f * twoPi;
        juce::Path trail;
        if (rev) trail.addCentredArc (c.x, c.y, R, R, 0.0f, th, th + len, true);
        else     trail.addCentredArc (c.x, c.y, R, R, 0.0f, th - len, th, true);
        g.setColour (ui.accent);
        g.strokePath (trail, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (! empty && (playing || rec))
    {
        const float th = head * twoPi, hr = juce::jmax (3.0f, 5.0f * scale);
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x + R * std::sin (th) - hr, c.y - R * std::cos (th) - hr, hr * 2.0f, hr * 2.0f);
    }

    g.setColour (kDim);
    g.setFont (font (12.0f));
    g.drawText (empty ? juce::String ("empty") : juce::String (st.loopSeconds.load(), 2) + " s",
                juce::Rectangle<float> (c.x - 60.0f * scale, c.y - 92.0f * scale, 120.0f * scale, 16.0f * scale).toNearestInt(),
                juce::Justification::centred);
}

void UnofficialSpotykachEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const float s = scale;
    for (int d = 0; d < 2; ++d)
    {
        drawRing (g, d);
        // thin arrows: loop controls act on the ring, the ring feeds the mix
        const float dx = d == 0 ? kDeckAX : kDeckBX, mir = d == 1;
        auto P = [&] (float lx, float y) { return juce::Point<float> ((dx + (mir ? kDeckW - lx : lx)) * s, y * s); };
        arrow (g, { P (88, 222), P (112, 222), P (124, 300) }, s);
        arrow (g, { P (88, 440), P (114, 440) }, s);
        arrow (g, { P (426, 452), P (386, 452) }, s);
        arrow (g, { P (335, 250), P (380, 195), P (430, 160) }, s);
    }

    g.setColour (kPanel2);
    g.fillRoundedRectangle (meterArea, 3.0f * s);
    g.setColour (meterLevel > 0.99f ? juce::Colours::red : kCap);
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * juce::jlimit (0.0f, 1.0f, meterLevel)), 3.0f * s);
    if (infoText.isNotEmpty())
    {
        g.setColour (kDim);
        g.setFont (font (11.0f));
        g.drawFittedText (infoText, footerArea, juce::Justification::centredLeft, 2);
    }
}

void UnofficialSpotykachEditor::layoutDeck (int d, float s)
{
    auto px = [s] (float v) { return juce::roundToInt (v * s); };
    auto& ui = deckUI[(size_t) d];
    const bool mir = d == 1;
    const float dx = mir ? kDeckBX : kDeckAX;
    auto X = [&] (float lx) { return dx + (mir ? kDeckW - lx : lx); };
    auto boxLeft = [&] (float lx, float w) { return dx + (mir ? kDeckW - lx - w : lx); };
    const auto just = mir ? juce::Justification::centredRight : juce::Justification::centredLeft;

    ui.title.setFont (juce::Font (juce::FontOptions (22.0f * s)));
    ui.title.setJustificationType (just);
    ui.title.setBounds (px (boxLeft (0, 160)), px (78), px (160), px (30));
    for (int i = 0; i < 3; ++i)
        ui.modeChip[(size_t) i].setBounds (px (boxLeft ((float) i * 60, 56)), px (114), px (56), px (26));
    ui.status.setFont (font (12.0f));
    ui.status.setJustificationType (just);
    ui.status.setBounds (px (boxLeft (0, 220)), px (148), px (220), px (16));

    // the ring and its broken bands
    const float cx = X (kRingCX) * s;                 // pixels
    ui.ring = juce::Rectangle<float> (cx - (kRingR + 8.0f) * s, (kRingCY - kRingR - 8.0f) * s, (kRingR + 8.0f) * 2.0f * s, (kRingR + 8.0f) * 2.0f * s);
    ui.bandBright.clear();
    ui.bandDim.clear();
    constexpr float pi = juce::MathConstants<float>::pi;
    for (int i = 0; i < 8; ++i)
    {
        const float r = kRingR - (float) i * 5.0f, circ = 2.0f * pi * r;
        float pos = -(float) (i * 23), total = 0.0f;
        for (int k = 0; k <= 14 && total < circ - 2.0f; ++k)
        {
            const float on = 20.0f + 90.0f * std::abs (std::sin ((float) i * 1.7f + (float) k * 2.3f));
            const float gap = 2.0f + 14.0f * std::abs (std::cos ((float) i * 0.9f + (float) k * 1.9f));
            const float len = juce::jmin (on, circ - total);
            auto& path = i < 2 ? ui.bandBright : ui.bandDim;
            path.addCentredArc (cx, kRingCY * s, r * s, r * s, 0.0f, pos / r, (pos + len) / r, true);
            pos += on + gap;
            total += on + gap;
        }
    }

    // knobs around the ring: position left, mix top right, size lower left, envelope lower right, pitch in the middle
    placeKnob (ui.knobs[2].slider, ui.knobs[2].label, s, X (kRingCX), kRingCY - 12.0f, 100.0f, 124.0f, true);
    placeKnob (ui.knobs[0].slider, ui.knobs[0].label, s, X (45), 235.0f, 64.0f, 124.0f, false);
    placeKnob (ui.knobs[4].slider, ui.knobs[4].label, s, X (470), 150.0f, 64.0f, 124.0f, false);
    placeKnob (ui.knobs[5].slider, ui.knobs[5].label, s, X (490), 300.0f, 56.0f, 124.0f, false);
    placeKnob (ui.knobs[1].slider, ui.knobs[1].label, s, X (45), 435.0f, 64.0f, 124.0f, false);
    placeKnob (ui.knobs[3].slider, ui.knobs[3].label, s, X (470), 450.0f, 64.0f, 124.0f, false);

    // pads (the FIT button, buttons[7], lives on the setup page)
    for (int i = 0; i < 7; ++i)
        ui.buttons[(size_t) i].setBounds (px (boxLeft (20.0f + (float) i * 75.0f, 68)), px (550), px (68), px (34));

    // mode extras: pitch snap, slice offset, env length, window
    ui.snap.box.setBounds (px (boxLeft (0, 96)), px (630), px (96), px (24));
    ui.snap.label.setFont (font (12.0f));
    ui.snap.label.setJustificationType (just);
    ui.snap.label.setBounds (px (boxLeft (0, 96)), px (658), px (96), px (16));
    placeKnob (ui.offset.slider,    ui.offset.label,    s, X (150), 640.0f, 44.0f, 92.0f, false);
    placeKnob (ui.envLength.slider, ui.envLength.label, s, X (245), 640.0f, 44.0f, 92.0f, false);
    placeKnob (ui.window.slider,    ui.window.label,    s, X (340), 640.0f, 44.0f, 92.0f, false);

    // Flux and Grit
    const float fxY = 780.0f;
    ui.fluxOn.setBounds (px (X (35) - 24.0f), px (fxY - 16.0f), px (48), px (44));
    ui.gritOn.setBounds (px (X (305) - 24.0f), px (fxY - 16.0f), px (48), px (44));
    static const float fxX[6] = { 100, 165, 230, 375, 440, 505 };
    for (size_t i = 0; i < 6; ++i)
        placeKnob (ui.fxKnobs[i].slider, ui.fxKnobs[i].label, s, X (fxX[i]), fxY, 44.0f, 64.0f, false);
    ui.fluxMode.box.setBounds (px (boxLeft (105, 120)), px (862), px (120), px (24));
    ui.gritMode.box.setBounds (px (boxLeft (380, 120)), px (862), px (120), px (24));
}

void UnofficialSpotykachEditor::resized()
{
    if (getWidth() <= 0) return;
    scale = (float) getWidth() / (float) kBaseWidth;
    const float s = scale;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };

    // top bar
    titleLabel.setFont (juce::Font (juce::FontOptions (26.0f * s)));
    titleLabel.setBounds (px (30), px (22), px (420), px (34));
    versionLabel.setFont (font (11.0f));
    versionLabel.setBounds (px (30), px (54), px (320), px (14));
    hintLabel.setFont (font (12.0f));
    hintLabel.setBounds (px (560), px (31), px (610), px (16));
    aboutButton.setBounds (px (1180), px (26), px (70), px (26));

    layoutDeck (0, s);
    layoutDeck (1, s);

    // centre column
    auto small = [&] (juce::Label& l, float size) { l.setFont (font (size)); };
    small (tempoSource.label, 12.0f);
    tempoSource.label.setBounds (px (560), px (96), px (160), px (16));
    tempoSource.box.setBounds (px (590), px (116), px (100), px (26));
    placeKnob (tempo.slider, tempo.label, s, 640.0f, 200.0f, 76.0f, 124.0f, true);
    tapButton.setBounds (px (610), px (308), px (60), px (26));
    small (quantise.label, 12.0f);
    quantise.label.setBounds (px (560), px (342), px (160), px (16));
    quantise.box.setBounds (px (580), px (360), px (120), px (26));
    small (routeLabel, 12.0f);
    routeLabel.setBounds (px (560), px (394), px (160), px (14));
    for (int i = 0; i < 3; ++i)
        routeBtn[(size_t) i].setBounds (px (590), px (412 + i * 21), px (116), px (20));
    small (abLabel[0], 12.0f);
    small (abLabel[1], 12.0f);
    abLabel[0].setBounds (px (560), px (484), px (160), px (16));
    abLabel[1].setBounds (px (560), px (636), px (160), px (16));
    crossfade.slider.getProperties().set ("gap", px (24));
    crossfade.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (124), px (22));
    crossfade.slider.setBounds (px (578), px (504), px (124), px (124) + px (24) + px (22));
    small (crossfade.label, 12.0f);
    crossfade.label.setBounds (px (578), px (676), px (124), px (16));
    placeKnob (click.slider, click.label, s, 640.0f, 728.0f, 44.0f, 124.0f, false);
    meterArea = juce::Rectangle<float> (600.0f * s, 794.0f * s, 80.0f * s, 6.0f * s);
    small (inputLabel, 10.0f);
    inputLabel.setBounds (px (580), px (802), px (120), px (12));
    placeKnob (outLevel.slider, outLevel.label, s, 640.0f, 840.0f, 44.0f, 124.0f, false);

    // bottom: tabs and footer
    {
        int x = px (30);
        const int widths[5] = { 52, 52, 36, 46, 46 };
        juce::TextButton* tabs[5] = { &setupTab, &modsTab, &cvTab, &midiTab, &filesTab };
        for (int i = 0; i < 5; ++i)
        {
            tabs[i]->setBounds (x, px (914), px ((float) widths[i]), px (24));
            x += px ((float) widths[i] + 14.0f);
        }
    }
    footerLabel.setFont (font (11.0f));
    footerLabel.setBounds (px (770), px (914), px (480), px (32));
    footerArea = juce::Rectangle<int> (px (350), px (914), px (400), px (32));

    // the lower page: a panel over the bottom part of the window
    pageRect = juce::Rectangle<int> (px (20), px (704), px (1240), px (202));
    if (pagePanel != nullptr) pagePanel->setBounds (pageRect);
    const int rowH = px (26);
    auto pageArea = pageRect.reduced (px (24), px (16));
    const int colGap = px (20);
    auto setLs = [&] (LabelledSlider& ls, juce::Rectangle<int> row, int labelW, int textW)
    {
        ls.label.setFont (font (12.0f));
        ls.label.setBounds (row.removeFromLeft (px ((float) labelW)));
        ls.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px ((float) textW), px (20));
        ls.slider.setBounds (row);
    };
    auto halves = [&] (juce::Rectangle<int> row, juce::Rectangle<int>& left, juce::Rectangle<int>& right)
    {
        left = row.removeFromLeft ((row.getWidth() - colGap) / 2);
        row.removeFromLeft (colGap);
        right = row;
    };
    {   // SETUP page: generative pan and fit tempo
        auto p = pageArea;
        juce::Rectangle<int> l, rt;
        halves (p.removeFromTop (rowH), l, rt);
        setLs (panSpeed, l, 90, 56);
        setLs (panRange, rt, 90, 56);
        p.removeFromTop (px (6));
        halves (p.removeFromTop (rowH), l, rt);
        for (size_t d = 0; d < 2; ++d)
        {
            auto cell = d == 0 ? l : rt;
            deckUI[d].buttons[7].setBounds (cell.removeFromLeft (px (70)).reduced (0, px (1)));
            cell.removeFromLeft (px (8));
            fitBeats[d].label.setBounds ({});
            fitBeats[d].slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px (66), px (20));
            fitBeats[d].slider.setBounds (cell);
        }
    }
    {   // CV page: Deck A left, Deck B right, crossfade below
        auto p = pageArea;
        for (int k = 0; k < 2; ++k)
        {
            juce::Rectangle<int> l, rt;
            halves (p.removeFromTop (rowH), l, rt);
            for (int d = 0; d < 2; ++d)
            {
                auto cell = d == 0 ? l : rt;
                auto tag = cell.removeFromLeft (px (22));
                cvTag[(size_t) d].setFont (font (13.0f, true));
                if (k == 0) cvTag[(size_t) d].setBounds (tag);
                const int w = (cell.getWidth() - px (12)) / 2;
                setLs (cvSliders[(size_t) (d * 4 + k * 2)], cell.removeFromLeft (w), 52, 56);
                cell.removeFromLeft (px (12));
                setLs (cvSliders[(size_t) (d * 4 + k * 2 + 1)], cell, 52, 56);
            }
            p.removeFromTop (px (6));
        }
        setLs (cvCrossfade, p.removeFromTop (rowH).withWidth (px (480)), 100, 66);
    }
    {   // FILES page: a file line per deck side by side, then the trims, then the folder
        auto p = pageArea;
        juce::Rectangle<int> l, rt;
        halves (p.removeFromTop (px (30)), l, rt);
        for (int d = 0; d < 2; ++d)
        {
            const auto i = (size_t) d;
            auto row = d == 0 ? l : rt;
            fileTag[i].setFont (font (13.0f, true));
            fileTag[i].setBounds (row.removeFromLeft (px (22)));
            loadButton[i].setBounds (row.removeFromLeft (px (70)).reduced (0, px (2)));
            row.removeFromLeft (px (6));
            saveButton[i].setBounds (row.removeFromLeft (px (70)).reduced (0, px (2)));
            row.removeFromLeft (px (10));
            fileStatus[i].setFont (font (11.0f));
            fileStatus[i].setBounds (row);
        }
        p.removeFromTop (px (4));
        for (int k = 0; k < 2; ++k)       // left column Deck A, right column Deck B; first row inputs, second row outputs
        {
            halves (p.removeFromTop (rowH), l, rt);
            setLs (trimSliders[(size_t) k], l, 54, 72);
            setLs (trimSliders[(size_t) (2 + k)], rt, 54, 72);
            p.removeFromTop (px (4));
        }
        auto row = p.removeFromTop (rowH);
        folderButton.setBounds (row.removeFromLeft (px (86)).reduced (0, px (1)));
        row.removeFromLeft (px (8));
        folderLabel.setFont (font (11.0f));
        folderLabel.setBounds (row);
    }
    {   // MIDI page
        auto p = pageArea;
        juce::Rectangle<int> l, rt;
        halves (p.removeFromTop (rowH), l, rt);
        for (int d = 0; d < 2; ++d)
        {
            auto cell = d == 0 ? l : rt;
            midiChannel[(size_t) d].label.setFont (font (12.0f));
            midiChannel[(size_t) d].label.setBounds (cell.removeFromLeft (px (110)));
            midiChannel[(size_t) d].box.setBounds (cell.removeFromLeft (px (70)).reduced (0, px (1)));
            cell.removeFromLeft (px (14));
            follow[(size_t) d].setBounds (cell.removeFromLeft (px (170)));
        }
        p.removeFromTop (px (6));
        midiMonitor.setFont (font (12.0f));
        midiMonitor.setBounds (p.removeFromTop (rowH));
        midiHint.setFont (font (11.0f));
        midiHint.setBounds (p.removeFromTop (px (36)));
    }
    {   // MODS page: Mod A left, Mod B right
        auto p = pageArea;
        juce::Rectangle<int> cols[2];
        halves (p, cols[0], cols[1]);
        for (size_t m = 0; m < 2; ++m)
        {
            auto q = cols[m];
            auto& ui = modUI[m];
            auto row1 = q.removeFromTop (rowH);
            ui.title.setFont (font (13.0f, true));
            ui.title.setBounds (row1.removeFromLeft (px (54)));
            ui.type.box.setBounds (row1.removeFromLeft (px (88)).reduced (0, px (1)));
            row1.removeFromLeft (px (10));
            ui.sync.setBounds (row1.removeFromRight (px (62)));
            setLs (ui.cycle, row1, 44, 78);
            q.removeFromTop (px (6));

            auto row2 = q.removeFromTop (rowH);
            ui.target.label.setFont (font (12.0f));
            ui.target.label.setBounds (row2.removeFromLeft (px (54)));
            ui.target.box.setBounds (row2.removeFromLeft (px (120)).reduced (0, px (1)));
            row2.removeFromLeft (px (10));
            ui.meter = row2.removeFromRight (px (58)).reduced (0, px (9)).toFloat();
            row2.removeFromRight (px (8));
            setLs (ui.glow, row2, 40, 52);
        }
    }


    if (pagePanel != nullptr && page >= 0)
    {
        pagePanel->toFront (false);
        for (auto* c : pageWidgets) c->toFront (false);
    }
    if (about != nullptr)
        about->setBounds (getLocalBounds());

    if (sizeRemembering && getWidth() > 0)
        proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);   // remembered with the session
}

// ---- FILES page ----------------------------------------------------------------------------------------------
// ---- FILES page ----------------------------------------------------------------------------------------------
void UnofficialSpotykachEditor::chooseLoad (int d)
{
    auto start = proc.homeFolder();
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> (juce::String ("Load a WAV file into Deck ") + (d == 0 ? "A" : "B"), start, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, d] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File()) proc.loadLoopFile (d, f);
                          });
}

void UnofficialSpotykachEditor::chooseSave (int d)
{
    auto start = proc.homeFolder();
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> (juce::String ("Save Deck ") + (d == 0 ? "A" : "B") + " as a WAV file",
                                                   start.getChildFile (d == 0 ? "Deck A loop.wav" : "Deck B loop.wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, d] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
                              proc.saveLoopFile (d, f);
                          });
}

void UnofficialSpotykachEditor::chooseFolder()
{
    auto start = proc.homeFolder();
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choose the home folder for loop files", start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File()) proc.setHomeFolder (f);
                          });
}

void UnofficialSpotykachEditor::updateFilePage()
{
    using FS = UnofficialSpotykachProcessor::FileState;
    const auto& st = proc.engineStatus();
    for (size_t d = 0; d < 2; ++d)
    {
        const auto i = proc.deckFileInfo ((int) d);
        juce::String text;
        switch (i.state)
        {
            case FS::empty:   text = "Empty. LOAD a WAV file, or record a loop."; break;
            case FS::unsaved: text = "Not saved to a file. SAVE it to keep it with the project."; break;
            case FS::linked:  text = i.name + (i.note.isNotEmpty() ? "   -   " + i.note : juce::String()); break;
            default:          text = i.note; break;
        }
        if (i.isError && i.state != FS::missing)
            text = i.note + (i.name.isNotEmpty() ? "   (still: " + i.name + ")" : juce::String());
        fileStatus[d].setText (text, juce::dontSendNotification);
        fileStatus[d].setColour (juce::Label::textColourId, i.isError ? juce::Colour (0xffe0674f) : kDim);

        const auto& s = st.deck[d];
        const bool busy = s.recording.load() || s.armed.load();
        loadButton[d].setEnabled (! busy);
        saveButton[d].setEnabled (! busy && ! s.empty.load());
    }
    folderLabel.setText (proc.homeFolder().getFullPathName(), juce::dontSendNotification);
}
void UnofficialSpotykachEditor::timerCallback()
{
    const float peak = proc.inputPeak.exchange (0.0f);
    meterLevel = juce::jmax (peak, meterLevel * 0.85f);
    updateFilePage();

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
        deckUI[d].status.setText ((state + seq).toLowerCase(), juce::dontSendNotification);

        auto& ui = deckUI[d];
        ui.buttons[5].setToggleState (s.seqArmed.load() || s.seqRecording.load(), juce::dontSendNotification);
        if (ui.clearDownAt > 0.0 && ! ui.clearFired && juce::Time::getMillisecondCounterHiRes() - ui.clearDownAt >= 1500.0)
        {
            ui.clearFired = true;
            pulse (UnofficialSpotykachProcessor::deckId ((int) d, "seqClear"));
        }
        ui.buttons[6].setButtonText (ui.clearFired ? "CLEARED" : ui.clearDownAt > 0.0 ? "HOLD..." : "CLEAR");
    }

    // Fit tempo refused by the engine (no loop, or the tempo would fall outside 20 to 250 BPM)
    {
        const uint32_t f = st.fitFailSeq.load();
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (f != seenFitFail)
        {
            seenFitFail = f;
            infoText = "Fit tempo refused: it needs a recorded loop and a tempo between 20 and 250 BPM";
            infoUntil = now + 5000.0;
        }
        else if (infoText.isNotEmpty() && now > infoUntil) infoText = {};
    }
    if (page == 3)
    {
        juce::String t = "Last note: ";
        const int n = proc.lastNote.load(), c = proc.lastCc.load();
        t += n < 0 ? juce::String ("none") : juce::String (n) + " (ch " + juce::String (proc.lastNoteChannel.load()) + ")";
        t += "     Last CC: ";
        t += c < 0 ? juce::String ("none") : juce::String (c) + " = " + juce::String (proc.lastCcValue.load()) + " (ch " + juce::String (proc.lastCcChannel.load()) + ")";
        t += "     messages: " + juce::String ((int) proc.midiEventCount.load());
        midiMonitor.setText (t, juce::dontSendNotification);
    }
    for (size_t d = 0; d < 2; ++d)
    {
        using P = UnofficialSpotykachProcessor;
        const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId ((int) d, "mode"))->load());
        deckUI[d].buttons[7].setEnabled (mode == 1 && ! st.deck[d].empty.load());      // as on the hardware: Slice mode, with a loop
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
        for (int i = 0; i < 3; ++i) ui.modeChip[(size_t) i].setToggleState (mode == i, juce::dontSendNotification);
        ui.offset.slider.setEnabled (mode == 1);                    // Slice only
        ui.envLength.slider.setEnabled (mode == 2);                 // Drift only
        ui.window.slider.setEnabled (mode == 2);
        const float env = proc.apvts.getRawParameterValue (P::deckId ((int) d, "envelope"))->load();
        if (mode == 2 && env < 0.05f)
            ui.status.setText ("Drift is silent while Envelope is off", juce::dontSendNotification);
    }

    const bool hostSource = juce::roundToInt (proc.apvts.getRawParameterValue ("tempoSource")->load()) == 1;
    tempo.slider.setEnabled (! hostSource);
    tempo.label.setText (! hostSource ? "tempo" : (st.hostLocked.load() ? "locked" : "host"), juce::dontSendNotification);

    const bool generative = juce::roundToInt (routeBox.getSelectedId()) == 3;
    panSpeed.slider.setEnabled (generative);
    panRange.slider.setEnabled (generative);
    for (size_t i = 0; i < 3; ++i) routeBtn[i].setToggleState (routeBox.getSelectedItemIndex() == (int) i, juce::dontSendNotification);
    repaint();
}
