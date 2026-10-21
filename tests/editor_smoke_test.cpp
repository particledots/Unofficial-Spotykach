// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Creates the real processor and the real editor, renders the editor to an image, presses the
// buttons and runs audio through it. Exits non-zero on any failure. auval does not do this.
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include <cstdio>
#include <cstdlib>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        UnofficialSpotykachProcessor proc;
        proc.setPlayConfigDetails (2, 2, rate, 512);
        proc.prepareToPlay (rate, 512);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        CHECK (ed != nullptr);
        if (ed == nullptr) return 1;
        CHECK (ed->getWidth() > 100 && ed->getHeight() > 100);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());   // runs paint() for the whole tree
        CHECK (img.isValid());

        auto press = [&] (const char* id)
        {
            auto* p = proc.apvts.getParameter (id);
            p->setValueNotifyingHost (1.0f);
        };
        auto release = [&] (const char* id) { proc.apvts.getParameter (id)->setValueNotifyingHost (0.0f); };

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        double phase = 0;
        auto run = [&] (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.4f * (float) std::sin (phase);
                    phase += 2.0 * 3.14159265358979 * 220.0 / rate;
                    buf.setSample (0, i, v); buf.setSample (1, i, v);
                }
                proc.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        CHECK (std::isfinite (buf.getSample (c, i)));
            }
        };

        press ("record");  run (2); release ("record"); run (1);
        run ((int) (rate * 1.0 / 512));
        press ("record");  run (1); release ("record"); run (1);   // stop recording
        press ("play");    run (1); release ("play");
        run ((int) (rate * 0.5 / 512));
        // Deck B buttons and every resize step the host window could produce.
        press ("b_record"); run (1); release ("b_record"); run (1);
        press ("b_trig"); run (1); release ("b_trig"); run (1);
        press ("b_recInternal"); run (1); release ("b_recInternal"); run (1);
        press ("seqArm"); run (1); release ("seqArm"); run (2);
        press ("seqArm"); run (1); release ("seqArm"); run (1);
        press ("b_seqClear"); run (1); release ("b_seqClear"); run (1);
        // MIDI through the real processor: notes, CCs, transport, on the right and the wrong channel
        {
            juce::MidiBuffer mb;
            mb.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 10);
            mb.addEvent (juce::MidiMessage::controllerEvent (1, 22, 64), 100);     // Deck A size
            mb.addEvent (juce::MidiMessage::controllerEvent (2, 22, 127), 120);    // Deck B size
            mb.addEvent (juce::MidiMessage::controllerEvent (5, 22, 0), 130);      // unmatched channel: ignored
            mb.addEvent (juce::MidiMessage::controllerEvent (9, 3, 127), 140);     // crossfade works on any channel
            mb.addEvent (juce::MidiMessage::controllerEvent (1, 105, 127), 150);   // Flux on
            mb.addEvent (juce::MidiMessage::controllerEvent (1, 85, 127), 160);    // play
            mb.addEvent (juce::MidiMessage::midiStart(), 200);
            mb.addEvent (juce::MidiMessage::midiStop(), 300);
            buf.clear();
            proc.processBlock (buf, mb);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) CHECK (std::isfinite (buf.getSample (c, i)));
            CHECK (std::abs (proc.apvts.getRawParameterValue ("size")->load() - 64.0f / 127.0f) < 1.0e-3f);
            CHECK (std::abs (proc.apvts.getRawParameterValue ("b_size")->load() - 1.0f) < 1.0e-3f);
            CHECK (std::abs (proc.apvts.getRawParameterValue ("crossfade")->load() - 1.0f) < 1.0e-3f);
            CHECK (proc.apvts.getRawParameterValue ("fluxOn")->load() > 0.5f);
            CHECK (proc.lastCc.load() == 3 || proc.lastCc.load() == 85 || proc.lastCc.load() == 105);
            // The message thread has not run here; applying the pending values is what the 30 Hz timer does.
            proc.applyPendingMidi();
            CHECK (std::abs (proc.apvts.getParameter ("b_size")->getValue() - 1.0f) < 1.0e-3f);
            CHECK (proc.apvts.getParameter ("fluxOn")->getValue() > 0.5f);
            proc.apvts.getParameter ("crossfade")->setValueNotifyingHost (0.0f);
            proc.apvts.getParameter ("fluxOn")->setValueNotifyingHost (0.0f);
        }

        // modulators on every target
        for (int tg = 0; tg < 12; ++tg)
        {
            if (auto* t = proc.apvts.getParameter ("modA_target")) t->setValueNotifyingHost (t->convertTo0to1 ((float) tg));
            if (auto* t = proc.apvts.getParameter ("modB_target")) t->setValueNotifyingHost (t->convertTo0to1 ((float) (11 - tg)));
            run (2);
        }
        for (int w : { 480, 640, 900, 1280 })
        {
            ed->setSize (w, (int) std::lround (w * (double) UnofficialSpotykachEditor::kBaseHeight / (double) UnofficialSpotykachEditor::kBaseWidth));
            auto snap = ed->createComponentSnapshot (ed->getLocalBounds());
            CHECK (snap.isValid());
            if (const char* dir = std::getenv ("US_SNAPSHOT_DIR"))   // optional: save pictures for inspection
            {
                juce::File f = juce::File (dir).getChildFile ("editor_" + juce::String (w) + ".png");
                juce::FileOutputStream os (f);
                if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (snap, os); }
            }
            CHECK ((int) proc.apvts.state.getProperty ("uiWidth", 0) == w);   // size is remembered
        }
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (img2.isValid());
        std::printf("rate %.0f: loop %.2f s, playing=%d, empty=%d, underruns=%u\n", rate,
                    proc.engineStatus().deck[0].loopSeconds.load(), (int) proc.engineStatus().deck[0].playing.load(),
                    (int) proc.engineStatus().deck[0].empty.load(), (unsigned) proc.engineStatus().underruns.load());
        CHECK (! proc.engineStatus().deck[0].empty.load());
        ed.reset();

        // FX page on both decks, then back
        {
            std::unique_ptr<juce::AudioProcessorEditor> e3 (proc.createEditor());
            int pressed = 0;
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int i = 0; i < e3->getNumChildComponents(); ++i)
                    if (auto* b = dynamic_cast<juce::TextButton*> (e3->getChildComponent (i)))
                        if (b->getButtonText() == "FX") { b->setToggleState (pass == 0, juce::dontSendNotification); b->onClick(); if (pass == 0) ++pressed; }
                auto snap = e3->createComponentSnapshot (e3->getLocalBounds());
                CHECK (snap.isValid());
                if (pass == 0)
                    if (const char* dir = std::getenv ("US_SNAPSHOT_DIR"))
                    {
                        juce::FileOutputStream os (juce::File (dir).getChildFile ("editor_fx.png"));
                        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (snap, os); }
                    }
            }
            CHECK (pressed == 2);
        }

        // MODS page, then back to SETUP
        {
            std::unique_ptr<juce::AudioProcessorEditor> e4 (proc.createEditor());
            int found = 0;
            for (const char* name : { "MODS", "CV", "MIDI", "SETUP" })
            {
                for (int i = 0; i < e4->getNumChildComponents(); ++i)
                    if (auto* b = dynamic_cast<juce::TextButton*> (e4->getChildComponent (i)))
                        if (b->getButtonText() == name) { b->onClick(); ++found; }
                auto snap = e4->createComponentSnapshot (e4->getLocalBounds());
                CHECK (snap.isValid());
                if (juce::String (name) != "SETUP")
                    if (const char* dir = std::getenv ("US_SNAPSHOT_DIR"))
                    {
                        juce::FileOutputStream os (juce::File (dir).getChildFile ("editor_" + juce::String (name).toLowerCase() + ".png"));
                        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (snap, os); }
                    }
            }
            CHECK (found == 4);
        }

        // Closing and reopening the window must restore the last size.
        for (int w : { 900, 500 })
        {
            std::unique_ptr<juce::AudioProcessorEditor> e1 (proc.createEditor());
            e1->setSize (w, (int) std::lround (w * (double) UnofficialSpotykachEditor::kBaseHeight / (double) UnofficialSpotykachEditor::kBaseWidth));
            e1.reset();
            std::unique_ptr<juce::AudioProcessorEditor> e2 (proc.createEditor());
            std::printf ("  reopen: wanted %d, got %d\n", w, e2->getWidth());
            CHECK (e2->getWidth() == w);
        }
    }
    std::printf(fails ? "EDITOR SMOKE TEST FAILED\n" : "EDITOR SMOKE TEST PASSED\n");
    return fails ? 1 : 0;
}
