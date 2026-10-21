// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
#include "LoopFile.h"
#include "StreamResampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace usp
{
    namespace
    {
        uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
        uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

        void wr16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); }
        void wr32 (std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
        void wrId (std::vector<uint8_t>& b, const char* id) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) id[i]); }

        float finiteOrZero (double v)
        {
            return std::isfinite (v) ? (float) v : 0.0f;
        }

        // One sample at byte offset p.
        float readSample (const uint8_t* p, int bits, bool isFloat)
        {
            if (isFloat)
            {
                if (bits == 32) { uint32_t u = rd32 (p); float f; std::memcpy (&f, &u, 4); return finiteOrZero ((double) f); }
                uint64_t u = (uint64_t) rd32 (p) | ((uint64_t) rd32 (p + 4) << 32);
                double d; std::memcpy (&d, &u, 8);
                return finiteOrZero (d);
            }
            switch (bits)
            {
                case 8:  return ((float) p[0] - 128.0f) / 128.0f;                      // 8-bit WAV is unsigned
                case 16: return (float) (int16_t) rd16 (p) / 32768.0f;
                case 24: { int32_t v = (int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)); return (float) ((double) (v >> 8) / 8388608.0); }
                default: return (float) ((double) (int32_t) rd32 (p) / 2147483648.0);
            }
        }

        // Converts to 48 kHz with the same windowed-sinc resampler the audio path uses. The output is aligned
        // with the input (the resampler's own delay is cancelled by priming it with 15 silent frames).
        void resampleTo48k (const std::vector<float>& inL, const std::vector<float>& inR, double rate, size_t wantFrames,
                            std::vector<float>& outL, std::vector<float>& outR)
        {
            constexpr int kBlock = 4096;
            StreamResampler rs;
            rs.prepare (rate, kLoopRate, kBlock);
            rs.reset (StreamResampler::kTaps / 2 - 1);
            outL.assign (wantFrames, 0.0f);
            outR.assign (wantFrames, 0.0f);
            std::vector<float> zero ((size_t) kBlock, 0.0f);
            size_t pushed = 0, produced = 0;
            int guard = 0;
            while (produced < wantFrames && guard++ < 100000)
            {
                if (pushed < inL.size())
                {
                    const int n = (int) std::min ((size_t) kBlock, inL.size() - pushed);
                    rs.push (inL.data() + pushed, inR.data() + pushed, n);
                    pushed += (size_t) n;
                }
                else rs.push (zero.data(), zero.data(), kBlock);            // tail: silence flushes the filter

                for (;;)
                {
                    const int avail = rs.available();
                    if (avail <= 0 || produced >= wantFrames) break;
                    const int take = (int) std::min ((size_t) avail, wantFrames - produced);
                    const int got = rs.pull (outL.data() + produced, outR.data() + produced, take);
                    if (got <= 0) break;
                    produced += (size_t) got;
                }
            }
        }
    }

    uint64_t fnv1a64 (const uint8_t* data, size_t size)
    {
        uint64_t h = 14695981039346656037ull;
        for (size_t i = 0; i < size; ++i) { h ^= data[i]; h *= 1099511628211ull; }
        return h;
    }

    WavError decodeWav (const uint8_t* data, size_t size, LoopAudio& out, WavInfo& info, std::string& message)
    {
        out = LoopAudio();
        info = WavInfo();
        message.clear();
        if (data == nullptr || size < 12 || std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "WAVE", 4) != 0)
        {
            message = "not a WAV file";
            return WavError::notWav;
        }

        bool haveFmt = false, haveData = false;
        int tag = 0, channels = 0, bits = 0;
        uint32_t rate = 0;
        size_t dataOff = 0, dataLen = 0;
        size_t cueOff = 0, cueLen = 0;
        bool haveCue = false;

        size_t pos = 12;
        while (pos + 8 <= size)
        {
            const uint8_t* id = data + pos;
            uint32_t len = rd32 (data + pos + 4);
            const size_t body = pos + 8;
            const size_t avail = size - body;
            if (std::memcmp (id, "fmt ", 4) == 0 && len >= 16 && avail >= 16)
            {
                tag = rd16 (data + body);
                channels = rd16 (data + body + 2);
                rate = rd32 (data + body + 4);
                bits = rd16 (data + body + 14);
                if (tag == 0xFFFE && len >= 40 && avail >= 40) tag = rd16 (data + body + 24);   // WAVE_FORMAT_EXTENSIBLE: sub-format
                haveFmt = true;
            }
            else if (std::memcmp (id, "data", 4) == 0)
            {
                dataOff = body;
                dataLen = (len == 0xFFFFFFFFu || len > avail) ? avail : (size_t) len;   // streamed or cut-off files: take what is there
                haveData = true;
            }
            else if (std::memcmp (id, "cue ", 4) == 0 && len >= 4)
            {
                cueOff = body;
                cueLen = std::min ((size_t) len, avail);
                haveCue = true;
            }
            const size_t step = (size_t) len + (len & 1u);
            if (step > avail) break;
            pos = body + step;
        }

        if (! haveFmt || ! haveData)
        {
            message = "not a usable WAV file (no format or audio data)";
            return WavError::notWav;
        }
        const bool isFloat = tag == 3;
        if (tag != 1 && tag != 3)
        {
            message = "unsupported WAV encoding (only PCM and float are supported)";
            return WavError::unsupported;
        }
        if (channels < 1 || channels > 2)
        {
            message = channels > 2 ? "more than two channels is not supported" : "invalid channel count";
            return WavError::unsupported;
        }
        const bool bitsOk = isFloat ? (bits == 32 || bits == 64) : (bits == 8 || bits == 16 || bits == 24 || bits == 32);
        if (! bitsOk)
        {
            message = "unsupported sample size (" + std::to_string (bits) + " bit)";
            return WavError::unsupported;
        }
        if (rate < 1000 || rate > 768000)
        {
            message = "unsupported sample rate";
            return WavError::unsupported;
        }

        const size_t bytesPerSample = (size_t) bits / 8;
        const size_t frameBytes = bytesPerSample * (size_t) channels;
        const size_t fileFrames = dataLen / frameBytes;
        if (fileFrames == 0)
        {
            message = "the file holds no audio";
            return WavError::empty;
        }

        info.channels = channels;
        info.bits = bits;
        info.isFloat = isFloat;
        info.rate = (double) rate;
        info.fileFrames = fileFrames;

        // Frames to read from the file: enough for 42 s at 48 kHz (a little extra so the filter tail is exact).
        const bool same = std::abs ((double) rate - kLoopRate) < 0.5;
        const double ratio = kLoopRate / (double) rate;     // output frames per input frame
        const size_t outTotal = same ? fileFrames : (size_t) std::floor ((double) fileFrames * ratio + 0.5);
        const size_t outFrames = std::min (outTotal, kMaxLoopFrames);
        info.cut = outTotal > kMaxLoopFrames;
        const size_t needIn = same ? outFrames : std::min (fileFrames, (size_t) std::ceil ((double) outFrames / ratio) + 64);

        std::vector<float> l (needIn), r (needIn);
        const uint8_t* src = data + dataOff;
        for (size_t i = 0; i < needIn; ++i)
        {
            const uint8_t* f = src + i * frameBytes;
            const float a = readSample (f, bits, isFloat);
            l[i] = a;
            r[i] = channels == 2 ? readSample (f + bytesPerSample, bits, isFloat) : a;
        }

        if (same)
        {
            out.l = std::move (l);
            out.r = std::move (r);
        }
        else
        {
            resampleTo48k (l, r, (double) rate, outFrames, out.l, out.r);
        }

        // Cue points: sample offsets in the file's own rate -> frames at 48 kHz.
        if (haveCue)
        {
            const uint32_t n = rd32 (data + cueOff);
            const size_t room = cueLen >= 4 ? (cueLen - 4) / 24 : 0;
            const size_t count = std::min ((size_t) n, room);
            std::vector<uint32_t> pts;
            for (size_t i = 0; i < count; ++i)
            {
                const uint32_t off = rd32 (data + cueOff + 4 + i * 24 + 20);    // "sample offset" field of a cue point
                if (off >= fileFrames) continue;
                const size_t f = same ? (size_t) off : (size_t) std::floor ((double) off * ratio + 0.5);
                if (f < out.l.size()) pts.push_back ((uint32_t) f);
            }
            std::sort (pts.begin(), pts.end());
            pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
            if (pts.size() > kMaxLoopCues) pts.resize (kMaxLoopCues);
            out.cues = std::move (pts);
            info.cueCount = out.cues.size();
        }
        return WavError::none;
    }

    void encodeWav (const LoopAudio& audio, std::vector<uint8_t>& b)
    {
        const size_t frames = audio.l.size();
        const size_t nCues = std::min (audio.cues.size(), kMaxLoopCues);
        const uint32_t dataBytes = (uint32_t) (frames * 8);
        const uint32_t cueBytes = nCues > 0 ? (uint32_t) (4 + nCues * 24) : 0u;
        const uint32_t riffBytes = 4 + (8 + 16) + (8 + 4) + (8 + dataBytes) + (cueBytes > 0 ? 8 + cueBytes : 0u);

        b.clear();
        b.reserve ((size_t) riffBytes + 8);
        wrId (b, "RIFF"); wr32 (b, riffBytes); wrId (b, "WAVE");
        wrId (b, "fmt "); wr32 (b, 16);
        wr16 (b, 3);                     // IEEE float, as the firmware writes
        wr16 (b, 2);                     // stereo
        wr32 (b, 48000);
        wr32 (b, 48000u * 8u);           // bytes per second
        wr16 (b, 8);                     // bytes per frame
        wr16 (b, 32);
        wrId (b, "fact"); wr32 (b, 4); wr32 (b, (uint32_t) frames);   // players expect it for non-PCM formats
        wrId (b, "data"); wr32 (b, dataBytes);
        for (size_t i = 0; i < frames; ++i)
        {
            for (float v : { audio.l[i], audio.r[i] })
            {
                uint32_t u;
                std::memcpy (&u, &v, 4);
                wr32 (b, u);
            }
        }
        if (nCues > 0)
        {
            wrId (b, "cue "); wr32 (b, cueBytes);
            wr32 (b, (uint32_t) nCues);
            for (size_t i = 0; i < nCues; ++i)
            {
                wr32 (b, (uint32_t) (i + 1));       // id
                wr32 (b, audio.cues[i]);            // play order position
                wrId (b, "data");                   // chunk the point refers to
                wr32 (b, 0);                        // chunk start
                wr32 (b, 0);                        // block start
                wr32 (b, audio.cues[i]);            // sample offset
            }
        }
    }
}
