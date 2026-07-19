#include "wav_io.h"

#include <cstdio>
#include <cstring>

namespace fbsampler::testutil {

namespace {

void putU32(std::vector<unsigned char>& out, std::uint32_t v)
{
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
}

void putU16(std::vector<unsigned char>& out, std::uint16_t v)
{
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

} // namespace

bool writeFloatWav(const std::string& path,
                   const std::vector<std::vector<float>>& channels,
                   double sampleRate)
{
    if (channels.empty())
        return false;
    const auto numChannels = static_cast<std::uint16_t>(channels.size());
    const auto numFrames = static_cast<std::uint32_t>(channels[0].size());
    const std::uint32_t dataSize = numFrames * numChannels * 4u;

    std::vector<unsigned char> bytes;
    bytes.reserve(46 + dataSize);
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    putU32(bytes, 4 + 8 + 16 + 8 + dataSize);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E'});
    bytes.insert(bytes.end(), {'f', 'm', 't', ' '});
    putU32(bytes, 16);
    putU16(bytes, 3); // IEEE float
    putU16(bytes, numChannels);
    putU32(bytes, static_cast<std::uint32_t>(sampleRate));
    putU32(bytes, static_cast<std::uint32_t>(sampleRate) * numChannels * 4u);
    putU16(bytes, static_cast<std::uint16_t>(numChannels * 4u));
    putU16(bytes, 32);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    putU32(bytes, dataSize);

    for (std::uint32_t frame = 0; frame < numFrames; ++frame) {
        for (std::uint16_t ch = 0; ch < numChannels; ++ch) {
            std::uint32_t u = 0;
            std::memcpy(&u, &channels[ch][frame], 4);
            putU32(bytes, u);
        }
    }

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

bool writePcm16Wav(const std::string& path,
                   const std::vector<std::vector<float>>& channels,
                   double sampleRate)
{
    if (channels.empty())
        return false;
    const auto numChannels = static_cast<std::uint16_t>(channels.size());
    const auto numFrames = static_cast<std::uint32_t>(channels[0].size());
    const std::uint32_t dataSize = numFrames * numChannels * 2u;

    std::vector<unsigned char> bytes;
    bytes.reserve(44 + dataSize);
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    putU32(bytes, 4 + 8 + 16 + 8 + dataSize);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E'});
    bytes.insert(bytes.end(), {'f', 'm', 't', ' '});
    putU32(bytes, 16);
    putU16(bytes, 1); // PCM
    putU16(bytes, numChannels);
    putU32(bytes, static_cast<std::uint32_t>(sampleRate));
    putU32(bytes, static_cast<std::uint32_t>(sampleRate) * numChannels * 2u);
    putU16(bytes, static_cast<std::uint16_t>(numChannels * 2u));
    putU16(bytes, 16);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    putU32(bytes, dataSize);

    for (std::uint32_t frame = 0; frame < numFrames; ++frame) {
        for (std::uint16_t ch = 0; ch < numChannels; ++ch) {
            float v = channels[ch][frame];
            if (v > 1.0f)
                v = 1.0f;
            if (v < -1.0f)
                v = -1.0f;
            const auto s = static_cast<std::int16_t>(v < 0 ? v * 32768.0f - 0.5f
                                                           : v * 32767.0f + 0.5f);
            putU16(bytes, static_cast<std::uint16_t>(s));
        }
    }

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

} // namespace fbsampler::testutil
