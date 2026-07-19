#include "wav_reader.h"

#include "fbsampler/detail/rt_check.h"

#include <cstdio>
#include <cstring>

namespace fbsampler::detail {

namespace {

void addError(std::vector<Diagnostic>* diagnostics, const std::string& path,
              std::string code, std::string message)
{
    if (!diagnostics)
        return;
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = std::move(code);
    d.message = std::move(message);
    d.location.file = path;
    diagnostics->push_back(std::move(d));
}

std::uint32_t readU32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readU16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

bool readWavFile(const std::string& path, DecodedWav& out,
                 std::vector<Diagnostic>* diagnostics)
{
    if (rtcheck::inRtSection())
        rtcheck::reportViolation("file I/O on audio thread (readWavFile)");

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        addError(diagnostics, path, "pool.sample_open_failed",
                 "cannot open sample file: " + path);
        return false;
    }

    std::vector<unsigned char> bytes;
    unsigned char chunk[4096];
    std::size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);

    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0
        || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        addError(diagnostics, path, "pool.sample_not_wav",
                 "not a RIFF/WAVE file: " + path);
        return false;
    }

    std::uint16_t format = 0;
    std::uint16_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    const unsigned char* data = nullptr;
    std::uint32_t dataSize = 0;

    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char* hdr = bytes.data() + pos;
        std::uint32_t chunkSize = readU32(hdr + 4);
        const unsigned char* body = hdr + 8;
        // Compare against the remaining byte count rather than adding
        // pos + 8 + chunkSize, which could overflow size_t on 32-bit builds
        // for a corrupt/adversarial chunk-size field.
        if (chunkSize > bytes.size() - pos - 8)
            break;
        if (std::memcmp(hdr, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = readU16(body);
            numChannels = readU16(body + 2);
            sampleRate = readU32(body + 4);
            bitsPerSample = readU16(body + 14);
            if (format == 0xFFFE && chunkSize >= 40) // WAVE_FORMAT_EXTENSIBLE
                format = readU16(body + 24);         // first 2 bytes of SubFormat GUID
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            data = body;
            dataSize = chunkSize;
        }
        pos += 8 + chunkSize + (chunkSize & 1); // chunks are word-aligned
    }

    const bool isPcm = format == 1 && (bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32);
    const bool isFloat = format == 3 && bitsPerSample == 32;
    if (!data || numChannels == 0 || sampleRate == 0 || (!isPcm && !isFloat)) {
        addError(diagnostics, path, "pool.sample_format_unsupported",
                 "unsupported WAVE format (need PCM 16/24/32 or float32): " + path);
        return false;
    }

    const std::uint32_t bytesPerSample = bitsPerSample / 8u;
    const std::uint32_t frameBytes = bytesPerSample * numChannels;
    const std::uint64_t numFrames = frameBytes ? dataSize / frameBytes : 0;

    out.numChannels = numChannels;
    out.numFrames = numFrames;
    out.sampleRate = static_cast<double>(sampleRate);
    out.channels.assign(numChannels, std::vector<float>(static_cast<std::size_t>(numFrames)));

    for (std::uint64_t frame = 0; frame < numFrames; ++frame) {
        const unsigned char* fp = data + frame * frameBytes;
        for (std::uint32_t ch = 0; ch < numChannels; ++ch) {
            const unsigned char* sp = fp + ch * bytesPerSample;
            float v = 0.0f;
            if (isFloat) {
                std::uint32_t u = readU32(sp);
                std::memcpy(&v, &u, 4);
            } else if (bitsPerSample == 16) {
                auto s = static_cast<std::int16_t>(readU16(sp));
                v = static_cast<float>(s) / 32768.0f;
            } else if (bitsPerSample == 24) {
                std::int32_t s = static_cast<std::int32_t>(sp[0] | (sp[1] << 8) | (sp[2] << 16));
                if (s & 0x800000)
                    s |= ~0xFFFFFF;
                v = static_cast<float>(s) / 8388608.0f;
            } else { // PCM 32
                auto s = static_cast<std::int32_t>(readU32(sp));
                v = static_cast<float>(s) / 2147483648.0f;
            }
            out.channels[ch][static_cast<std::size_t>(frame)] = v;
        }
    }

    return true;
}

} // namespace fbsampler::detail
