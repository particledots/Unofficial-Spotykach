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

    // The mockup's palette (docs/SPEC.md section 6).
    const juce::Colour kBg     { 0xff17181a };
    const juce::Colour kPanel  { 0xff212225 };
    const juce::Colour kPanel2 { 0xff1b1c1e };
    const juce::Colour kBorder { 0xff323438 };
    const juce::Colour kEdge   { 0xff5a5c60 };
    const juce::Colour kTrack  { 0xff35373b };
    const juce::Colour kText   { 0xffece8df };
    const juce::Colour kDim    { 0xffb4b1a8 };
    const juce::Colour kAccent { 0xffe8a33d };     // Deck A, and general highlights
    const juce::Colour kViolet { 0xffb48ae0 };     // Deck B
    const juce::Colour kRec    { 0xffd9423a };
    const juce::Colour kGreen  { 0xff4caf6a };
}

//==============================================================================
// Flat look after the mockup: arc knobs, outlined buttons, thin sliders.
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
        setColour (juce::Slider::textBoxBackgroundColourId, kPanel2);
        setColour (juce::Slider::textBoxOutlineColourId, kBorder);
        setColour (juce::Slider::textBoxHighlightColourId, kEdge);
        setColour (juce::Label::textColourId, kText);
        setColour (juce::ToggleButton::textColourId, kText);
        setColour (juce::ToggleButton::tickColourId, kAccent);
        setColour (juce::TooltipWindow::backgroundColourId, kPanel);
        setColour (juce::TooltipWindow::textColourId, kText);
        setColour (juce::TooltipWindow::outlineColourId, kEdge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const float thick = juce::jmax (2.5f, d * 0.09f);
        const auto c = b.getCentre();
        const float r = d * 0.5f - thick;
        const float alpha = s.isEnabled() ? 1.0f : 0.35f;
        const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (kTrack.withMultipliedAlpha (alpha));
        g.strokePath (track, stroke);

        const float ang = a0 + pos * (a1 - a0);
        if (pos > 0.002f)
        {
            juce::Path v;
            v.addCentredArc (c.x, c.y, r, r, 0.0f, a0, ang, true);
            g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
            g.strokePath (v, stroke);
        }
        const float tr = thick * 0.8f;
        const float px = c.x + r * std::sin (ang), py = c.y - r * std::cos (ang);
        g.setColour (kText.withMultipliedAlpha (alpha));
        g.fillEllipse (px - tr, py - tr, tr * 2.0f, tr * 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        const float alpha = s.isEnabled() ? 1.0f : 0.35f;
        const float th = 4.0f, cy = (float) y + (float) h * 0.5f;
        g.setColour (kTrack.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, (float) w, th, th * 0.5f);
        g.setColour (s.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, juce::jmax (th, pos - (float) x), th, th * 0.5f);
        const float tr = juce::jmin (6.0f, (float) h * 0.45f);
        g.setColour (kText.withMultipliedAlpha (alpha));
        g.fillEllipse (pos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float a = b.isEnabled() ? 1.0f : 0.4f;
        const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
        const auto fill = on ? onCol : down ? kTrack : hi ? kPanel2.brighter (0.2f) : juce::Colours::transparentBlack;
        g.setColour (fill.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour ((on ? onCol : hi ? kDim : kEdge).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jlimit (9, 14, (int) ((float) h * 0.42f))).withStyle ("Bold"));
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.7f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        const float h = (float) b.getHeight();
        const float box = juce::jmin (16.0f, h - 4.0f);
        const float a = b.isEnabled() ? 1.0f : 0.4f;
        const auto r = juce::Rectangle<float> (2.0f, (h - box) * 0.5f, box, box);
        g.setColour (kPanel2.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (kEdge.withMultipliedAlpha (a));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        if (b.getToggleState())
        {
            g.setColour (b.findColour (juce::ToggleButton::tickColourId).withMultipliedAlpha (a));
            g.fillRoundedRectangle (r.reduced (3.5f), 2.0f);
        }
        g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (a));
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 15.0f, h * 0.55f))));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) (box + 8.0f)), juce::Justification::centredLeft, 1, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const float a = box.isEnabled() ? 1.0f : 0.45f;
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (kPanel2.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kEdge.withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
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
        label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
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
            "The window layout follows the original panel mockup. Loops come back with the project through a link to their WAV file (the project holds no audio).",
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

    ui.accent = d == 0 ? kAccent : kViolet;
    ui.title.setText ("DECK " + tag, juce::dontSendNotification);
    ui.title.setColour (juce::Label::textColourId, ui.accent);
    addAndMakeVisible (ui.title);
    ui.status.setColour (juce::Label::textColourId, kDim);
    ui.status.setJustificationType (juce::Justification::centred);
    ui.status.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (ui.status);

    static const char* btnText[UnofficialSpotykachProcessor::kNumDeckButtons] = { "REC", "PLAY", "REV", "TRIG", "FROM", "SEQ", "CLEAR", "FIT" };
    static const char* btnTip[UnofficialSpotykachProcessor::kNumDeckButtons] = {
        "Record from the plug-in input (press again to stop)", "Play / stop", "Play in reverse / stop",
        "Trigger the loop with the current settings",
        "Record the other deck's output into this deck (press again to stop)",
        "Trigger sequencer: press to arm, then tap TRIG to record a rhythm; press SEQ again to stop recording",
        "Hold for 1.5 seconds to clear the trigger sequence",
        "Set the tempo so this deck's loop lasts the chosen number of beats (Source becomes Internal)" };
    for (int i = 0; i < UnofficialSpotykachProcessor::kNumDeckButtons; ++i)
    {
        auto& b = ui.buttons[(size_t) i];
        b.setButtonText (i == 4 ? juce::String ("FROM ") + (d == 0 ? "B" : "A") : i == 7 ? juce::String ("FIT ") + tag : juce::String (btnText[i]));
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
        if (i == 5) { b.setColour (juce::TextButton::buttonOnColourId, kRec); b.setColour (juce::TextButton::textColourOnId, kText); }
        if (i < UnofficialSpotykachProcessor::kRowButtons) addAndMakeVisible (b);
        else addChildComponent (b);
    }

    static const char* base[6] = { "position", "size", "pitch", "envelope", "mix", "feedback" };
    static const char* text[6] = { "Position", "Size", "Pitch", "Envelope", "Mix", "Feedback" };
    for (size_t i = 0; i < 6; ++i)
    {
        auto& k = ui.knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, ui.accent);
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
    ui.mode.box.setVisible (false);      // the chips below drive it; the combo stays as the parameter attachment
    static const char* chipText[3] = { "REEL", "SLICE", "DRIFT" };
    for (int i = 0; i < 3; ++i)
    {
        auto& c = ui.modeChip[(size_t) i];
        c.setButtonText (chipText[i]);
        c.setColour (juce::TextButton::buttonOnColourId, ui.accent);
        c.setTooltip ("Deck mode");
        c.onClick = [this, d, i] { deckUI[(size_t) d].mode.box.setSelectedItemIndex (i, juce::sendNotificationSync); };
        addAndMakeVisible (c);
    }
    addCombo (ui.snap, P::deckId (d, "pitchSnap"), "Pitch Snap");
    addLabelled (ui.offset,    P::deckId (d, "sliceOffset"), "Slice Offset", juce::Slider::LinearHorizontal);
    addLabelled (ui.envLength, P::deckId (d, "envLength"),   "Env Length",   juce::Slider::LinearHorizontal);
    addLabelled (ui.window,    P::deckId (d, "window"),      "Window",       juce::Slider::LinearHorizontal);
    for (auto* ls : { &ui.offset, &ui.envLength, &ui.window }) ls->slider.setColour (juce::Slider::trackColourId, ui.accent);

    // FX page: Flux (delay) and Grit (drive / reduce / filter). Shown instead of the loop knobs when FX is pressed.
    static const char* fxBase[6] = { "fluxTime", "fluxFeedback", "fluxMix", "gritIntensity", "gritMix", "gritCharacter" };
    static const char* fxText[6] = { "Flux Time", "Flux Feedback", "Flux Mix", "Grit Intensity", "Grit Mix", "Grit Character" };
    for (size_t i = 0; i < 6; ++i)
    {
        auto& k = ui.fxKnobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, ui.accent);
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
        b->setColour (juce::ToggleButton::tickColourId, ui.accent);
        addChildComponent (*b);
    }
    ui.fluxOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::deckId (d, "fluxOn"), ui.fluxOn);
    ui.gritOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, P::deckId (d, "gritOn"), ui.gritOn);
    addCombo (ui.fluxMode, P::deckId (d, "fluxMode"), "Flux Mode");
    addCombo (ui.gritMode, P::deckId (d, "gritMode"), "Grit Mode");
    for (auto* cb : { &ui.fluxMode, &ui.gritMode }) { cb->box.setVisible (false); cb->label.setVisible (false); }

    addAndMakeVisible (ui.pageButton);
    ui.pageButton.setClickingTogglesState (true);
    ui.pageButton.setColour (juce::TextButton::buttonOnColourId, ui.accent);
    ui.pageButton.setTooltip ("Flux and Grit effects for this deck");
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
        return juce::String (st >= 0 ? "+" : "") + juce::String (st, snap == 0 ? 1 : 0) + " st";
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
    ui.mode.box.setVisible (false);
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

    titleLabel.setText ("SPOTYKACH", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);
    unofficialLabel.setText ("UNOFFICIAL", juce::dontSendNotification);
    unofficialLabel.setColour (juce::Label::textColourId, kText);
    unofficialLabel.setColour (juce::Label::outlineColourId, kText);
    unofficialLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (unofficialLabel);
    taglineLabel.setText ("Community port. Not an official Synthux Academy product, not affiliated with Synthux Academy.", juce::dontSendNotification);
    taglineLabel.setColour (juce::Label::textColourId, kDim);
    taglineLabel.setJustificationType (juce::Justification::centred);
    taglineLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (taglineLabel);
    inputLabel.setText ("INPUT", juce::dontSendNotification);
    inputLabel.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (inputLabel);

    versionLabel.setText (versionString(), juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, kDim);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (versionLabel);

    setupDeck (0);
    setupDeck (1);

    addLabelled (crossfade, "crossfade", "A/B CROSSFADE", juce::Slider::LinearHorizontal);
    addLabelled (panSpeed,  "panSpeed",  "Pan Speed",     juce::Slider::LinearHorizontal);
    addLabelled (panRange,  "panRange",  "Pan Range",     juce::Slider::LinearHorizontal);
    addLabelled (outLevel,  "outLevel",  "OUTPUT LEVEL",  juce::Slider::LinearHorizontal);
    outLevel.slider.setTextValueSuffix (" dB");
    addLabelled (tempo, "tempo", "Tempo", juce::Slider::LinearHorizontal);
    addLabelled (click, "click", "CLICK", juce::Slider::LinearHorizontal);
    addCombo (tempoSource, "tempoSource", "CLOCK SOURCE");
    addCombo (quantise, "quantise", "LAUNCH QUANTISE");
    for (auto* l : { &crossfade.label, &panSpeed.label, &panRange.label, &outLevel.label, &tempo.label, &click.label,
                     &tempoSource.label, &quantise.label })
        l->setColour (juce::Label::textColourId, kDim);
    for (auto* ls : { &panSpeed, &panRange }) ls->label.setColour (juce::Label::textColourId, kText);
    setupMod (0);
    setupMod (1);
    addAndMakeVisible (tapButton);
    tapButton.setTooltip ("Tap the tempo (sets Source to Internal)");
    tapButton.onClick = [this] { tap(); };
    for (auto* b : { &setupTab, &modsTab, &cvTab, &midiTab, &filesTab })
    {
        addAndMakeVisible (*b);
    }
    setupTab.onClick = [this] { page = 0; showGlobalPage(); resized(); };
    modsTab.onClick  = [this] { page = 1; showGlobalPage(); resized(); };
    cvTab.onClick    = [this] { page = 2; showGlobalPage(); resized(); };
    midiTab.onClick  = [this] { page = 3; showGlobalPage(); resized(); };
    filesTab.onClick = [this] { page = 4; showGlobalPage(); resized(); };

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

    routeLabel.setText ("ROUTING", juce::dontSendNotification);
    routeLabel.setColour (juce::Label::textColourId, kDim);
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
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 3 / 2, kBaseHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);

    w = juce::jlimit (kBaseWidth * 3 / 4, kBaseWidth * 3 / 2, w);

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

void UnofficialSpotykachEditor::drawRing (juce::Graphics& g, int d)
{
    using P = UnofficialSpotykachProcessor;
    const auto& ui = deckUI[(size_t) d];
    const auto& s = proc.engineStatus().deck[(size_t) d];
    const auto c = ui.ring.getCentre();
    const float R = juce::jmin (ui.ring.getWidth(), ui.ring.getHeight()) * 0.5f - 7.0f * scale;
    const float dotR = juce::jmax (1.5f, R * 0.04f);
    constexpr int N = 64;

    const bool empty = s.empty.load(), rec = s.recording.load(), armed = s.armed.load(), playing = s.playing.load();
    const bool rev = s.reverse.load();
    const float head = juce::jlimit (0.0f, 1.0f, s.playhead.load());

    for (int i = 0; i < N; ++i)
    {
        const float frac = (float) i / (float) N;
        const float th = frac * juce::MathConstants<float>::twoPi;
        const float x = c.x + R * std::sin (th), y = c.y - R * std::cos (th);
        juce::Colour col = kTrack;
        if (rec)        col = kRec.withAlpha (0.85f);
        else if (armed) col = kRec.withAlpha (0.35f);
        else if (! empty)
        {
            float behind = rev ? frac - head : head - frac;         // how far the playhead has already passed this dot
            behind -= std::floor (behind);
            const float glow = playing ? juce::jmax (0.0f, 1.0f - behind / 0.22f) : 0.0f;
            col = ui.accent.withAlpha ((playing ? 0.30f : 0.45f) + 0.55f * glow);
        }
        g.setColour (col);
        g.fillEllipse (x - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
    }
    if (! empty && (playing || rec))
    {
        const float th = head * juce::MathConstants<float>::twoPi;
        const float hr = dotR * 2.1f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x + R * std::sin (th) - hr, c.y - R * std::cos (th) - hr, hr * 2.0f, hr * 2.0f);
    }

    static const char* modeName[3] = { "REEL", "SLICE", "DRIFT" };
    const int mode = juce::jlimit (0, 2, juce::roundToInt (proc.apvts.getRawParameterValue (P::deckId (d, "mode"))->load()));
    g.setColour (kText);
    g.setFont (font (15.0f, true));
    g.drawText (modeName[mode], juce::Rectangle<float> (c.x - R * 0.7f, c.y - 20.0f * scale, R * 1.4f, 20.0f * scale).toNearestInt(), juce::Justification::centred);
    g.setColour (kDim);
    g.setFont (font (12.0f));
    g.drawText (empty ? juce::String ("empty") : juce::String (s.loopSeconds.load(), 2) + " s",
                juce::Rectangle<float> (c.x - R * 0.7f, c.y + 2.0f * scale, R * 1.4f, 16.0f * scale).toNearestInt(), juce::Justification::centred);
}

void UnofficialSpotykachEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto& st = proc.engineStatus();

    auto box = [&] (juce::Rectangle<float> r, juce::Colour fill)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, 8.0f * scale);
        g.setColour (kBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f * scale, 1.0f);
    };
    for (size_t d = 0; d < 2; ++d)
    {
        box (deckUI[d].panel, kPanel);
        drawRing (g, (int) d);
    }
    box (centrePanel, kPanel2);
    box (bottomPanel, kPanel2);

    if (page == 1)
        for (size_t m = 0; m < 2; ++m)
        {
            g.setColour (kPanel);
            g.fillRoundedRectangle (modUI[m].meter, 3.0f * scale);
            g.setColour (m == 0 ? kAccent : kViolet);
            g.fillRoundedRectangle (modUI[m].meter.withWidth (modUI[m].meter.getWidth() * juce::jlimit (0.0f, 1.0f, st.modValue[m].load())), 3.0f * scale);
        }

    g.setColour (kBg);
    g.fillRoundedRectangle (meterArea, 3.0f * scale);
    g.setColour (meterLevel > 0.99f ? juce::Colours::red : kAccent);
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * juce::jlimit (0.0f, 1.0f, meterLevel)), 3.0f * scale);
    if (infoText.isNotEmpty())
    {
        g.setColour (kDim);
        g.setFont (font (11.0f));
        g.drawText (infoText, footerArea, juce::Justification::centredLeft);
    }
}

void UnofficialSpotykachEditor::layoutDeck (int d, juce::Rectangle<int> area, float s)
{
    auto px = [s] (float v) { return juce::roundToInt (v * s); };
    auto& ui = deckUI[(size_t) d];
    ui.panel = area.toFloat();
    auto r = area.reduced (px (12), px (10));

    // Head: name on the left, the three mode buttons on the right.
    auto head = r.removeFromTop (px (26));
    ui.title.setFont (font (15.0f, true));
    ui.title.setBounds (head.removeFromLeft (px (100)));
    for (int i = 2; i >= 0; --i)
    {
        ui.modeChip[(size_t) i].setBounds (head.removeFromRight (px (58)));
        head.removeFromRight (px (4));
    }
    r.removeFromTop (px (8));

    // Ring and pads (mirrored for Deck B, as on the hardware's reverse layout).
    auto mid = r.removeFromTop (px (150));
    auto ring = d == 0 ? mid.removeFromLeft (px (150)) : mid.removeFromRight (px (150));
    ui.ring = ring.toFloat();
    if (d == 0) mid.removeFromLeft (px (12)); else mid.removeFromRight (px (12));
    auto padArea = mid.removeFromTop (px (104));
    {
        const int g = px (6);
        const int cw = (padArea.getWidth() - 3 * g) / 4, ch = (padArea.getHeight() - g) / 2;
        juce::TextButton* pads[8] = { &ui.buttons[0], &ui.buttons[1], &ui.buttons[2], &ui.buttons[3],
                                      &ui.buttons[4], &ui.buttons[5], &ui.buttons[6], &ui.pageButton };
        for (int i = 0; i < 8; ++i)
            pads[i]->setBounds (padArea.getX() + (i % 4) * (cw + g), padArea.getY() + (i / 4) * (ch + g), cw, ch);
    }
    ui.status.setFont (font (11.0f));
    ui.status.setBounds (mid.withTrimmedTop (px (4)));

    // Knobs.
    r.removeFromTop (px (6));
    auto knobRow = r.removeFromTop (px (112));
    const int kw = knobRow.getWidth() / 6;
    for (size_t i = 0; i < 6; ++i)
    {
        auto cell = juce::Rectangle<int> (knobRow.getX() + (int) i * kw, knobRow.getY(), kw, knobRow.getHeight());
        for (auto* kn : { &ui.knobs[i], &ui.fxKnobs[i] })
        {
            auto c = cell;
            kn->label.setFont (font (11.0f));
            kn->label.setBounds (c.removeFromTop (px (14)));
            kn->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kw - px (4), px (17));
            kn->slider.setBounds (c);
        }
    }

    // Two rows below: pitch snap and slice offset / env length and window, or the Flux and Grit switches.
    r.removeFromTop (px (6));
    auto rowA = r.removeFromTop (px (22));
    r.removeFromTop (px (4));
    auto rowB = r.removeFromTop (px (22));
    {
        ui.fluxOn.setBounds (rowA.removeFromLeft (px (74)));
        ui.fluxMode.box.setBounds (rowA.removeFromLeft (px (170)));
        ui.gritOn.setBounds (rowB.removeFromLeft (px (74)));
        ui.gritMode.box.setBounds (rowB.removeFromLeft (px (170)));
    }
    {
        auto a = area.reduced (px (12), px (10));
        auto rowA2 = juce::Rectangle<int> (a.getX(), rowA.getY(), a.getWidth(), rowA.getHeight());
        auto rowB2 = juce::Rectangle<int> (a.getX(), rowB.getY(), a.getWidth(), rowB.getHeight());
        ui.snap.label.setFont (font (11.0f));
        ui.snap.label.setBounds (rowA2.removeFromLeft (px (62)));
        ui.snap.box.setBounds (rowA2.removeFromLeft (px (104)));
        rowA2.removeFromLeft (px (12));
        auto place = [&] (LabelledSlider& ls, juce::Rectangle<int> cell, int labelW)
        {
            ls.label.setFont (font (11.0f));
            ls.label.setBounds (cell.removeFromLeft (px ((float) labelW)));
            ls.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px (48), px (18));
            ls.slider.setBounds (cell);
        };
        place (ui.offset, rowA2, 64);
        const int half = rowB2.getWidth() / 2;
        place (ui.envLength, rowB2.removeFromLeft (half - px (6)), 64);
        rowB2.removeFromLeft (px (12));
        place (ui.window, rowB2, 50);
    }
}

void UnofficialSpotykachEditor::resized()
{
    scale = (float) getWidth() / (float) kBaseWidth;
    const float s = scale;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };
    auto r = getLocalBounds().reduced (px (14), px (12));

    // Top bar.
    auto top = r.removeFromTop (px (34));
    aboutButton.setBounds (top.removeFromRight (px (70)).reduced (0, px (3)));
    top.removeFromRight (px (8));
    versionLabel.setFont (font (12.0f));
    versionLabel.setBounds (top.removeFromRight (px (170)));
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f * s).withStyle ("Bold").withKerningFactor (0.12f)));
    titleLabel.setBounds (top.removeFromLeft (px (190)));
    unofficialLabel.setFont (juce::Font (juce::FontOptions (11.0f * s).withStyle ("Bold").withKerningFactor (0.1f)));
    unofficialLabel.setBounds (top.removeFromLeft (px (92)).reduced (0, px (6)));
    top.removeFromLeft (px (10));
    taglineLabel.setFont (font (11.0f));
    taglineLabel.setBounds (top);
    r.removeFromTop (px (8));

    // Deck A | centre | Deck B.
    auto decks = r.removeFromTop (px (376));
    const int deckW = px (428), gap = px (10);
    layoutDeck (0, decks.removeFromLeft (deckW), s);
    decks.removeFromLeft (gap);
    auto deckBArea = decks.removeFromRight (deckW);
    decks.removeFromRight (gap);
    layoutDeck (1, deckBArea, s);
    auto centre = decks;
    centrePanel = centre.toFloat();
    {
        auto c = centre.reduced (px (10), px (10));
        auto small = [&] (juce::Label& l) { l.setFont (font (10.5f)); };
        auto labelRow = [&] (juce::Label& l, int h) { small (l); l.setBounds (c.removeFromTop (px ((float) h))); };
        auto comboBlock = [&] (juce::Label& l, juce::ComboBox& b)
        {
            labelRow (l, 14);
            b.setBounds (c.removeFromTop (px (24)));
            c.removeFromTop (px (8));
        };
        auto sliderBlock = [&] (LabelledSlider& ls, int textW)
        {
            labelRow (ls.label, 14);
            ls.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px ((float) textW), px (20));
            ls.slider.setBounds (c.removeFromTop (px (22)));
            c.removeFromTop (px (8));
        };
        comboBlock (routeLabel, routeBox);
        sliderBlock (crossfade, 50);
        comboBlock (tempoSource.label, tempoSource.box);
        {   // tempo: label with TAP on the same row
            auto row = c.removeFromTop (px (18));
            small (tempo.label);
            tapButton.setBounds (row.removeFromRight (px (42)));
            tempo.label.setBounds (row);
            tempo.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px (50), px (20));
            tempo.slider.setBounds (c.removeFromTop (px (22)));
            c.removeFromTop (px (8));
        }
        comboBlock (quantise.label, quantise.box);
        sliderBlock (click, 40);
        sliderBlock (outLevel, 56);
        inputLabel.setFont (font (10.5f));
        inputLabel.setBounds (c.removeFromTop (px (14)));
        meterArea = c.removeFromTop (px (8)).toFloat();
    }
    r.removeFromTop (px (8));

    // Bottom panel: tabs and the page.
    const int rowH = px (26);
    auto setLs = [&] (LabelledSlider& ls, juce::Rectangle<int> row, int labelW, int textW)
    {
        ls.label.setFont (font (12.0f));
        ls.label.setBounds (row.removeFromLeft (px ((float) labelW)));
        ls.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px ((float) textW), px (20));
        ls.slider.setBounds (row);
    };
    auto panel = r.removeFromTop (px (10 + 26 + 6 + 120 + 8));
    bottomPanel = panel.toFloat();
    footerArea = r.removeFromTop (px (16));
    auto inner = panel.reduced (px (12), px (10));
    {
        auto tabs = inner.removeFromTop (px (26));
        for (auto* t : { &setupTab, &modsTab, &cvTab, &midiTab, &filesTab })
        {
            t->setBounds (tabs.removeFromLeft (px (72)));
            tabs.removeFromLeft (px (4));
        }
    }
    inner.removeFromTop (px (6));
    auto pageArea = inner;
    const int colGap = px (20);
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

    if (about != nullptr)
        about->setBounds (getLocalBounds());

    if (sizeRemembering && getWidth() > 0)
        proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);   // remembered with the session
}

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
    tempo.label.setText (! hostSource ? "Tempo" : (st.hostLocked.load() ? "Locked" : "Host"), juce::dontSendNotification);

    const bool generative = juce::roundToInt (routeBox.getSelectedId()) == 3;
    panSpeed.slider.setEnabled (generative);
    panRange.slider.setEnabled (generative);
    repaint();
}
