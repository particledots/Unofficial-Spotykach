// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Creates the real processor and the real editor, renders the editor to an image, presses the
// buttons and runs audio through it. Exits non-zero on any failure. auval does not do this.
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include <cstdio>

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
        ed->setSize (ed->getWidth(), ed->getHeight());
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (img2.isValid());
        std::printf("rate %.0f: loop %.2f s, playing=%d, empty=%d, underruns=%u\n", rate,
                    proc.engineStatus().loopSeconds.load(), (int) proc.engineStatus().playing.load(),
                    (int) proc.engineStatus().empty.load(), (unsigned) proc.engineStatus().underruns.load());
        CHECK (! proc.engineStatus().empty.load());
        ed.reset();
    }
    std::printf(fails ? "EDITOR SMOKE TEST FAILED\n" : "EDITOR SMOKE TEST PASSED\n");
    return fails ? 1 : 0;
}
