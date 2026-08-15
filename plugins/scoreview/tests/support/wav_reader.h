#pragma once

// Minimal WAV reader for listener fixtures (plans/scoreview-ear.md E0):
// RIFF/WAVE with PCM16 or IEEE-float32 samples, any channel count
// (mono-mixed by averaging). Just enough to load real piano recordings from
// plugins/scoreview/tests/fixtures/audio/ — not a general audio library.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace draxul::tests
{

struct WavData
{
    int sample_rate = 0;
    std::vector<float> samples; // mono, normalized to [-1, 1]
};

inline bool read_wav(const std::string& path, WavData& out)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    std::vector<uint8_t> bytes;
    uint8_t chunk[4096];
    size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + got);
    std::fclose(file);

    const auto u16 = [&bytes](size_t at) -> uint32_t {
        return bytes[at] | (bytes[at + 1] << 8);
    };
    const auto u32 = [&bytes](size_t at) -> uint32_t {
        return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    };

    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
    size_t data_at = 0;
    size_t data_size = 0;
    size_t at = 12;
    while (at + 8 <= bytes.size())
    {
        const size_t size = u32(at + 4);
        const size_t body = at + 8;
        if (body + size > bytes.size())
            return false;
        if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0 && size >= 16)
        {
            format = static_cast<uint16_t>(u16(body));
            channels = static_cast<uint16_t>(u16(body + 2));
            rate = u32(body + 4);
            bits = static_cast<uint16_t>(u16(body + 14));
        }
        else if (std::memcmp(bytes.data() + at, "data", 4) == 0)
        {
            data_at = body;
            data_size = size;
        }
        at = body + size + (size & 1); // chunks are word-aligned
    }
    const bool pcm16 = format == 1 && bits == 16;
    const bool float32 = format == 3 && bits == 32;
    if ((!pcm16 && !float32) || channels == 0 || rate == 0 || data_at == 0)
        return false;

    const size_t bytes_per_sample = bits / 8u;
    const size_t frame_bytes = bytes_per_sample * channels;
    const size_t frames = data_size / frame_bytes;
    out.sample_rate = static_cast<int>(rate);
    out.samples.assign(frames, 0.0f);
    for (size_t frame = 0; frame < frames; ++frame)
    {
        float mixed = 0.0f;
        for (size_t c = 0; c < channels; ++c)
        {
            const size_t sample_at = data_at + frame * frame_bytes + c * bytes_per_sample;
            if (pcm16)
            {
                const auto raw = static_cast<int16_t>(u16(sample_at));
                mixed += static_cast<float>(raw) / 32768.0f;
            }
            else
            {
                float value = 0.0f;
                const uint32_t raw = u32(sample_at);
                std::memcpy(&value, &raw, sizeof(value));
                mixed += value;
            }
        }
        out.samples[frame] = mixed / static_cast<float>(channels);
    }
    return true;
}

} // namespace draxul::tests
