// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Creates the real processor and the real editor, renders the editor to an image, presses the
// buttons and runs audio through it. Exits non-zero on any failure. auval does not do this.
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>    // mkdtemp is declared here on macOS

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
        for (int w : { 960, 1280, 1600, 1044 })
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

        // Open each lower page from its tab (the tabs are lower case)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e4 (proc.createEditor());
            int found = 0;
            for (const char* name : { "mods", "cv", "midi", "files", "setup" })
            {
                for (int i = 0; i < e4->getNumChildComponents(); ++i)
                    if (auto* b = dynamic_cast<juce::TextButton*> (e4->getChildComponent (i)))
                        if (b->getButtonText() == name) { b->onClick(); ++found; }
                auto snap = e4->createComponentSnapshot (e4->getLocalBounds());
                CHECK (snap.isValid());
                if (juce::String (name) != "setup")
                    if (const char* dir = std::getenv ("US_SNAPSHOT_DIR"))
                    {
                        juce::FileOutputStream os (juce::File (dir).getChildFile ("editor_" + juce::String (name).toLowerCase() + ".png"));
                        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (snap, os); }
                    }
            }
            CHECK (found == 5);
        }

        // Closing and reopening the window must restore the last size.
        for (int w : { 1200, 1000 })
        {
            std::unique_ptr<juce::AudioProcessorEditor> e1 (proc.createEditor());
            e1->setSize (w, (int) std::lround (w * (double) UnofficialSpotykachEditor::kBaseHeight / (double) UnofficialSpotykachEditor::kBaseWidth));
            e1.reset();
            std::unique_ptr<juce::AudioProcessorEditor> e2 (proc.createEditor());
            std::printf ("  reopen: wanted %d, got %d\n", w, e2->getWidth());
            CHECK (e2->getWidth() == w);
        }
    }

    {   // MIDI channel routing through the real processor: Deck B on channel 3 triggers Deck B's loop (and only that)
        UnofficialSpotykachProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        auto setParam = [&] (const char* id, float real) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (real)); };
        setParam ("route", 0.0f);          // Double Mono: Deck A left, Deck B right
        setParam ("b_mix", 1.0f); setParam ("mix", 1.0f);
        setParam ("crossfade", 0.5f);
        setParam ("b_midiChannel", 3.0f);
        CHECK (std::abs (proc.apvts.getRawParameterValue ("b_midiChannel")->load() - 3.0f) < 0.01f);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer none;
        double ph = 0;
        auto runTone = [&] (int blocks, juce::MidiBuffer& mb)
        {
            double rmsL = 0, rmsR = 0;
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < 512; ++i) { const float v = 0.4f * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * 220.0 / 48000.0; b.setSample (0, i, v); b.setSample (1, i, v); }
                proc.processBlock (b, k == 0 ? mb : none);
                for (int i = 0; i < 512; ++i) { rmsL += b.getSample (0, i) * b.getSample (0, i); rmsR += b.getSample (1, i) * b.getSample (1, i); }
            }
            return std::pair<double, double> { std::sqrt (rmsL / (blocks * 512.0)), std::sqrt (rmsR / (blocks * 512.0)) };
        };
        auto press = [&] (const char* id) { setParam (id, 1.0f); runTone (1, none); setParam (id, 0.0f); runTone (1, none); };
        press ("b_record"); runTone (100, none); press ("b_record");     // ~1 s on Deck B only (stopped afterwards)
        press ("b_play"); runTone (20, none); press ("b_play");            // start and stop so nothing is playing
        runTone (40, none);
        auto quiet = runTone (20, none);
        juce::MidiBuffer ch3; ch3.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100), 0);
        auto hit = runTone (40, ch3);
        juce::MidiBuffer ch1; ch1.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        std::printf ("midi channel 3: idle rms L %.4f R %.4f; after note on ch 3: L %.4f R %.4f\n", quiet.first, quiet.second, hit.first, hit.second);
        CHECK (hit.second > 0.02);        // Deck B (right) plays
    }
    {   // Loop files through the real processor: save, project state, restore, moved file, missing file, load, unsaved
        char tmpl[] = "/tmp/usp_smoke_XXXXXX";
        const char* made = mkdtemp (tmpl);
        CHECK (made != nullptr);
        const juce::File dir (made != nullptr ? made : "/tmp");
        const juce::File oldHome = UnofficialSpotykachProcessor::defaultHomeFolder();

        auto make = [&] (UnofficialSpotykachProcessor& p)
        {
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            p.setHomeFolder (dir);
        };
        double ph = 0;
        auto block = [&] (UnofficialSpotykachProcessor& p, float amp)
        {
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer none;
            for (int i = 0; i < 512; ++i) { const float v = amp * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * 220.0 / 48000.0; b.setSample (0, i, v); b.setSample (1, i, v); }
            p.processBlock (b, none);
            double e = 0;
            for (int i = 0; i < 512; ++i) e += b.getSample (0, i) * b.getSample (0, i);
            return std::sqrt (e / 512.0);
        };
        auto pump = [&] (UnofficialSpotykachProcessor& p)
        {
            for (int i = 0; i < 3000 && (i < 5 || p.loopFilesBusy()); ++i) { block (p, 0.0f); p.pollLoopFiles(); juce::Thread::sleep (2); }
            for (int i = 0; i < 20; ++i) { block (p, 0.0f); p.pollLoopFiles(); }
        };
        auto setP = [&] (UnofficialSpotykachProcessor& p, const char* id, float real) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (real)); };
        using FS = UnofficialSpotykachProcessor::FileState;

        UnofficialSpotykachProcessor a;
        make (a);
        setP (a, "mix", 1.0f);
        setP (a, "record", 1.0f); block (a, 0.4f); setP (a, "record", 0.0f); block (a, 0.4f);
        for (int i = 0; i < 100; ++i) block (a, 0.4f);
        setP (a, "play", 1.0f); block (a, 0.4f); setP (a, "play", 0.0f); block (a, 0.4f);   // Play ends the recording
        pump (a);
        CHECK (! a.engineStatus().deck[0].empty.load());
        CHECK (a.deckFileInfo (0).state == FS::unsaved);
        const auto frames = a.engineStatus().deck[0].loopFrames.load();

        const auto target = dir.getChildFile ("sub").getChildFile ("saved.wav");
        a.saveLoopFile (0, target);
        pump (a);
        auto info = a.deckFileInfo (0);
        std::printf ("save: state %d, name '%s', note '%s', file %lld bytes\n", (int) info.state, info.name.toRawUTF8(), info.note.toRawUTF8(), (long long) target.getSize());
        CHECK (info.state == FS::linked && info.name == "saved.wav");
        CHECK (target.getSize() > (juce::int64) frames * 8);

        juce::MemoryBlock state;
        a.getStateInformation (state);
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            CHECK (xml != nullptr && xml->getChildByName ("LOOPFILES") != nullptr && xml->getChildByName ("LOOPFILES")->getNumChildElements() == 1);
            CHECK (state.getSize() < 20000);          // the project holds a link, not the audio
        }

        {   // A new instance (a project opened) gets the loop back from the file.
            UnofficialSpotykachProcessor b;
            make (b);
            setP (b, "mix", 1.0f);
            b.setStateInformation (state.getData(), (int) state.getSize());
            pump (b);
            info = b.deckFileInfo (0);
            std::printf ("restore: state %d, '%s', %u frames (saved %u)\n", (int) info.state, info.note.toRawUTF8(), (unsigned) b.engineStatus().deck[0].loopFrames.load(), (unsigned) frames);
            CHECK (info.state == FS::linked);
            CHECK (b.engineStatus().deck[0].loopFrames.load() == frames);
            CHECK (b.deckFileInfo (1).state == FS::empty);
            setP (b, "play", 1.0f); block (b, 0.0f); setP (b, "play", 0.0f);
            double e = 0;
            for (int i = 0; i < 40; ++i) e = block (b, 0.0f);
            std::printf ("  restored loop plays at rms %.3f\n", e);
            CHECK (e > 0.05);
        }

        // Moved and renamed inside the home folder: found by its fingerprint.
        const auto moved = dir.getChildFile ("elsewhere").getChildFile ("renamed.wav");
        moved.getParentDirectory().createDirectory();
        CHECK (target.moveFileTo (moved));
        {
            UnofficialSpotykachProcessor c;
            make (c);
            c.setStateInformation (state.getData(), (int) state.getSize());
            pump (c);
            info = c.deckFileInfo (0);
            std::printf ("moved: state %d, name '%s', note '%s'\n", (int) info.state, info.name.toRawUTF8(), info.note.toRawUTF8());
            CHECK (info.state == FS::linked && info.name == "renamed.wav" && info.note.contains ("new place"));
            juce::MemoryBlock again;
            c.getStateInformation (again);                       // the project now remembers the new place
            UnofficialSpotykachProcessor c2;
            make (c2);
            c2.setStateInformation (again.getData(), (int) again.getSize());
            pump (c2);
            CHECK (c2.deckFileInfo (0).state == FS::linked && ! c2.deckFileInfo (0).note.contains ("new place"));
        }

        // Missing: reported, the link is kept for next time, nothing is loaded.
        CHECK (moved.deleteFile());
        {
            UnofficialSpotykachProcessor m;
            make (m);
            m.setStateInformation (state.getData(), (int) state.getSize());
            pump (m);
            info = m.deckFileInfo (0);
            std::printf ("missing: state %d, '%s'\n", (int) info.state, info.note.toRawUTF8());
            CHECK (info.state == FS::missing && info.isError && m.engineStatus().deck[0].empty.load());
            juce::MemoryBlock again;
            m.getStateInformation (again);
            auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            CHECK (xml != nullptr && xml->getChildByName ("LOOPFILES") != nullptr && xml->getChildByName ("LOOPFILES")->getNumChildElements() == 1);
        }

        // LOAD of an ordinary file (44.1 kHz, mono, 16-bit) into Deck B.
        {
            const auto wav = dir.getChildFile ("plain.wav");
            {   // a plain 16-bit mono 44.1 kHz WAV, written by hand
                std::vector<unsigned char> bytes;
                auto w32 = [&] (unsigned v) { for (int k = 0; k < 4; ++k) bytes.push_back ((unsigned char) (v >> (8 * k))); };
                auto w16 = [&] (unsigned v) { bytes.push_back ((unsigned char) v); bytes.push_back ((unsigned char) (v >> 8)); };
                const unsigned n = 44100;
                for (char c : { 'R', 'I', 'F', 'F' }) bytes.push_back ((unsigned char) c);
                w32 (36 + n * 2);
                for (char c : { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }) bytes.push_back ((unsigned char) c);
                w32 (16); w16 (1); w16 (1); w32 (44100); w32 (44100 * 2); w16 (2); w16 (16);
                for (char c : { 'd', 'a', 't', 'a' }) bytes.push_back ((unsigned char) c);
                w32 (n * 2);
                for (unsigned i = 0; i < n; ++i)
                    w16 ((unsigned) (unsigned short) (short) std::lround (0.3 * 32767.0 * std::sin (2.0 * 3.14159265358979 * 330.0 * i / 44100.0)));
                CHECK (wav.replaceWithData (bytes.data(), bytes.size()));
            }
            a.loadLoopFile (1, wav);
            pump (a);
            info = a.deckFileInfo (1);
            std::printf ("load: state %d, name '%s', note '%s', %u frames\n", (int) info.state, info.name.toRawUTF8(), info.note.toRawUTF8(), (unsigned) a.engineStatus().deck[1].loopFrames.load());
            CHECK (info.state == FS::linked && info.name == "plain.wav" && info.note.contains ("44.1 kHz"));
            CHECK (std::abs ((int) a.engineStatus().deck[1].loopFrames.load() - 48000) <= 1);

            // not a WAV: refused, the deck keeps what it had
            const auto junk = dir.getChildFile ("junk.wav");
            junk.replaceWithText ("this is not audio");
            a.loadLoopFile (1, junk);
            pump (a);
            info = a.deckFileInfo (1);
            std::printf ("junk: state %d, '%s'\n", (int) info.state, info.note.toRawUTF8());
            CHECK (info.isError && info.state == FS::linked && info.name == "plain.wav");
            CHECK (std::abs ((int) a.engineStatus().deck[1].loopFrames.load() - 48000) <= 1);
        }

        // Recording over a linked deck drops the link: it is "not saved" and is no longer written to the project.
        setP (a, "record", 1.0f); block (a, 0.4f); setP (a, "record", 0.0f); block (a, 0.4f);
        for (int i = 0; i < 20; ++i) { block (a, 0.4f); a.pollLoopFiles(); }
        info = a.deckFileInfo (0);
        CHECK (info.state == FS::unsaved);
        {
            juce::MemoryBlock again;
            a.getStateInformation (again);
            auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            CHECK (xml != nullptr && xml->getChildByName ("LOOPFILES") != nullptr && xml->getChildByName ("LOOPFILES")->getNumChildElements() == 1);   // deck B's link only
        }

        a.setHomeFolder (oldHome);
        dir.deleteRecursively();
    }
    std::printf(fails ? "EDITOR SMOKE TEST FAILED\n" : "EDITOR SMOKE TEST PASSED\n");
    return fails ? 1 : 0;
}
