// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// WAV reading and writing for the decks' loops. JUCE-free, so the headless tests can use it.
//
// The firmware keeps loops on its SD card as 32-bit float stereo 48 kHz WAV files and takes the
// "cue " chunk of a file as slice points (src/memory/wav.cpp, storage.cpp). This reads any common
// WAV (8/16/24/32-bit integer, 32/64-bit float, mono or stereo, any sample rate), converts it to the
// engine format (stereo, 48 kHz), cuts it to the deck's 42 s and keeps the cue points as slice points.
// It writes the firmware's own format (32-bit float, stereo, 48 kHz) plus a "cue " chunk when the
// loop has slice points.
//
// None of this runs on the audio thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace usp
{
    constexpr double kLoopRate = 48000.0;
    constexpr size_t kMaxLoopFrames = (size_t) 42 * 48000;   // one deck's buffer: 42 s, as in the firmware
    // The firmware allows 256 slice points but keeps their count in a uint8_t (so 256 would read back as 0).
    constexpr size_t kMaxLoopCues = 255;

    // Audio in the engine's format: stereo, 48 kHz.
    struct LoopAudio
    {
        std::vector<float> l, r;
        std::vector<uint32_t> cues;   // slice points in frames, ascending, each below frames()
        size_t frames() const { return l.size(); }
    };

    enum class WavError { none, notWav, unsupported, empty };

    struct WavInfo
    {
        int channels = 0;
        int bits = 0;
        bool isFloat = false;
        double rate = 0.0;
        size_t fileFrames = 0;       // frames in the file
        bool cut = false;            // the file was longer than 42 s and was cut
        size_t cueCount = 0;         // slice points kept
    };

    // Decodes a whole WAV file held in memory. On failure `message` says why, in words for the window.
    WavError decodeWav (const uint8_t* data, size_t size, LoopAudio& out, WavInfo& info, std::string& message);

    // Encodes the loop as the firmware's format (32-bit float, stereo, 48 kHz), with a "cue " chunk when it has slice points.
    void encodeWav (const LoopAudio& audio, std::vector<uint8_t>& bytes);

    // 64-bit FNV-1a hash of a file's bytes: identifies a file independently of its name and place.
    uint64_t fnv1a64 (const uint8_t* data, size_t size);
}
