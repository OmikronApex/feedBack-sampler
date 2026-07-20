// Pool-side SoundFont embedded-sample reader (Stories 2.1/2.3, AD-2):
// resolves an "sf2://<path>#<sampleIndex>" URI by walking the container's
// RIFF structure to the shdr record and decoding the referenced sdta range
// to float32 at acquire() time — 16-bit PCM for SF2, Ogg Vorbis (stb_vorbis)
// for SF3 (sampleType & 0x10). Decode happens on the control/loader thread
// only, upstream of everything RT; after acquire() an SF3-sourced sample is
// indistinguishable from an SF2-sourced one (AD-2: "upstream of the pool
// SF3 == SF2").
//
// This intentionally re-parses the container (structure once in the
// frontend, referenced ranges once here) — acceptable per the story dev
// notes, and it keeps the AD-2 ownership rule clean.

#include "sf2_sample_reader.h"

#include "fbsampler/detail/rt_check.h"

// stb_vorbis (public domain / MIT dual, v1.22 — NFR-6 licensing matrix).
// DECLARATIONS ONLY here: sfizz's vendored st_audiofile already compiles the
// same v1.22 implementation with external linkage (verified — defining it
// again in this TU is an ODR/LNK2005 clash), so we link its definitions.
// The vendored header pins the declaration set to v1.22 independently of
// sfizz's include layout.
#define STB_VORBIS_HEADER_ONLY 1
#include "stb_vorbis.h"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace fbsampler::detail {

namespace {

constexpr const char kScheme[] = "sf2://";
constexpr std::size_t kSchemeLen = 6;

void addDiag(std::vector<Diagnostic>* diagnostics, Severity severity, const std::string& file,
             std::string code, std::string message)
{
    if (!diagnostics)
        return;
    Diagnostic d;
    d.severity = severity;
    d.code = std::move(code);
    d.message = std::move(message);
    d.location.file = file;
    diagnostics->push_back(std::move(d));
}

void addError(std::vector<Diagnostic>* diagnostics, const std::string& file,
              std::string code, std::string message)
{
    addDiag(diagnostics, Severity::Error, file, std::move(code), std::move(message));
}

std::uint16_t readU16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t readU32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool splitUri(const std::string& uri, std::string& path, std::uint32_t& index)
{
    if (uri.rfind(kScheme, 0) != 0)
        return false;
    const std::size_t hash = uri.rfind('#');
    if (hash == std::string::npos || hash <= kSchemeLen)
        return false;
    path = uri.substr(kSchemeLen, hash - kSchemeLen);
    const char* first = uri.data() + hash + 1;
    const char* last = uri.data() + uri.size();
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc {} || ptr != last)
        return false;
    index = value;
    return true;
}

bool decodeImpl(const std::uint8_t* bytes, std::size_t size, std::uint32_t sampleIndex,
                const std::string& path, DecodedWav& out,
                std::vector<Diagnostic>* diagnostics)
{
    if (bytes == nullptr || size < 12 || std::memcmp(bytes, "RIFF", 4) != 0
        || std::memcmp(bytes + 8, "sfbk", 4) != 0) {
        addError(diagnostics, path, "pool.sf2_not_soundfont",
                 "not a RIFF sfbk (SoundFont) file: " + path);
        return false;
    }

    // Locate sdta/smpl (sample data) and pdta/shdr (sample headers).
    const std::uint8_t* smpl = nullptr;
    std::uint32_t smplBytes = 0;
    const std::uint8_t* shdr = nullptr;
    std::uint32_t shdrBytes = 0;

    const std::size_t limit = size;
    std::size_t pos = 12;
    while (pos + 8 <= limit) {
        const std::uint8_t* hdr = bytes + pos;
        const std::uint32_t chunkSize = readU32(hdr + 4);
        if (chunkSize > limit - pos - 8)
            break;
        if (std::memcmp(hdr, "LIST", 4) == 0 && chunkSize >= 4) {
            const std::uint8_t* body = hdr + 8;
            const std::uint32_t bodySize = chunkSize - 4;
            const bool isSdta = std::memcmp(body, "sdta", 4) == 0;
            const bool isPdta = std::memcmp(body, "pdta", 4) == 0;
            if (isSdta || isPdta) {
                std::size_t p = 4;
                while (p + 8 <= bodySize + 4u) {
                    const std::uint8_t* sub = body + p;
                    const std::uint32_t subSize = readU32(sub + 4);
                    if (subSize > bodySize + 4u - p - 8)
                        break;
                    if (isSdta && std::memcmp(sub, "smpl", 4) == 0) {
                        smpl = sub + 8;
                        smplBytes = subSize;
                    } else if (isPdta && std::memcmp(sub, "shdr", 4) == 0) {
                        shdr = sub + 8;
                        shdrBytes = subSize;
                    }
                    p += 8 + subSize + (subSize & 1);
                }
            }
        }
        pos += 8 + chunkSize + (chunkSize & 1);
    }

    if (!shdr || shdrBytes % 46 != 0 || shdrBytes / 46 < 1) {
        addError(diagnostics, path, "pool.sf2_shdr_invalid",
                 "SoundFont shdr chunk missing or malformed: " + path);
        return false;
    }
    // Terminal EOS record is not a real sample.
    const std::uint32_t sampleCount = shdrBytes / 46 - 1;
    if (sampleIndex >= sampleCount) {
        addError(diagnostics, path, "pool.sf2_sample_index_out_of_range",
                 "sample index " + std::to_string(sampleIndex) + " out of range (container has "
                     + std::to_string(sampleCount) + " samples): " + path);
        return false;
    }

    const std::uint8_t* rec = shdr + static_cast<std::size_t>(sampleIndex) * 46;
    const std::uint32_t start = readU32(rec + 20);
    const std::uint32_t end = readU32(rec + 24);
    const std::uint32_t shdrRate = readU32(rec + 36);
    const std::uint16_t sampleType = readU16(rec + 44);
    const bool vorbis = (sampleType & 0x10) != 0;

    if (!smpl) {
        addError(diagnostics, path, "pool.sf2_sample_bounds_invalid",
                 "sdta smpl chunk missing: " + path);
        return false;
    }

    if (vorbis) {
        // SF3: start/end are BYTE offsets of an Ogg Vorbis stream in sdta.
        if (!(start < end) || end > smplBytes) {
            addError(diagnostics, path, "pool.sf2_sample_bounds_invalid",
                     "sample " + std::to_string(sampleIndex) + " byte range ["
                         + std::to_string(start) + ", " + std::to_string(end)
                         + ") exceeds sdta sample data: " + path);
            return false;
        }
        int vorbisError = 0;
        stb_vorbis* v = stb_vorbis_open_memory(smpl + start, static_cast<int>(end - start),
                                               &vorbisError, nullptr);
        if (!v) {
            addError(diagnostics, path, "sf3.vorbis_decode_failed",
                     "sample " + std::to_string(sampleIndex)
                         + ": Ogg Vorbis stream failed to open (stb_vorbis error "
                         + std::to_string(vorbisError) + "): " + path);
            return false;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(v);
        if (info.channels <= 0 || info.sample_rate == 0) {
            stb_vorbis_close(v);
            addError(diagnostics, path, "sf3.vorbis_decode_failed",
                     "sample " + std::to_string(sampleIndex)
                         + ": Ogg Vorbis stream declares no channels or rate: " + path);
            return false;
        }
        if (shdrRate != 0 && info.sample_rate != shdrRate) {
            // De-facto convention: the Ogg header wins on conflict.
            addDiag(diagnostics, Severity::Warning, path, "sf3.sample_rate_mismatch",
                    "sample " + std::to_string(sampleIndex) + ": shdr declares "
                        + std::to_string(shdrRate) + " Hz but the Ogg stream is "
                        + std::to_string(info.sample_rate) + " Hz; using the Ogg rate");
        }

        const int channels = info.channels;
        out.numChannels = static_cast<std::uint32_t>(channels);
        out.sampleRate = static_cast<double>(info.sample_rate);
        out.channels.assign(static_cast<std::size_t>(channels), {});

        float buffer[4096];
        const int framesPerCall = static_cast<int>((sizeof(buffer) / sizeof(float))
                                                   / static_cast<unsigned>(channels));
        std::uint64_t totalFrames = 0;
        for (;;) {
            const int got = stb_vorbis_get_samples_float_interleaved(
                v, channels, buffer, framesPerCall * channels);
            if (got <= 0)
                break;
            for (int f = 0; f < got; ++f)
                for (int ch = 0; ch < channels; ++ch)
                    out.channels[static_cast<std::size_t>(ch)].push_back(
                        buffer[static_cast<std::size_t>(f * channels + ch)]);
            totalFrames += static_cast<std::uint64_t>(got);
        }
        stb_vorbis_close(v);

        if (totalFrames == 0) {
            addError(diagnostics, path, "sf3.vorbis_decode_failed",
                     "sample " + std::to_string(sampleIndex)
                         + ": Ogg Vorbis stream decoded to zero frames: " + path);
            return false;
        }
        out.numFrames = totalFrames;
        return true;
    }

    // SF2: 16-bit PCM sample points.
    const std::uint64_t smplPoints = smplBytes / 2;
    if (!(start < end) || end > smplPoints) {
        addError(diagnostics, path, "pool.sf2_sample_bounds_invalid",
                 "sample " + std::to_string(sampleIndex) + " bounds [" + std::to_string(start)
                     + ", " + std::to_string(end) + ") exceed sdta sample data: " + path);
        return false;
    }
    if (shdrRate == 0) {
        addError(diagnostics, path, "pool.sf2_sample_rate_invalid",
                 "sample " + std::to_string(sampleIndex) + " declares sample rate 0: " + path);
        return false;
    }

    const std::uint64_t numFrames = end - start;
    out.numChannels = 1; // SF2 samples are mono records (stereo = linked pair)
    out.numFrames = numFrames;
    out.sampleRate = static_cast<double>(shdrRate);
    out.channels.assign(1, std::vector<float>(static_cast<std::size_t>(numFrames)));

    const std::uint8_t* sp = smpl + static_cast<std::size_t>(start) * 2;
    for (std::uint64_t i = 0; i < numFrames; ++i) {
        const auto sv = static_cast<std::int16_t>(readU16(sp + i * 2));
        out.channels[0][static_cast<std::size_t>(i)] = static_cast<float>(sv) / 32768.0f;
    }
    return true;
}

} // namespace

bool isSf2SampleUri(const std::string& path)
{
    return path.rfind(kScheme, 0) == 0;
}

bool readSf2Sample(const std::string& uri, DecodedWav& out,
                   std::vector<Diagnostic>* diagnostics)
{
    if (rtcheck::inRtSection())
        rtcheck::reportViolation("file I/O on audio thread (readSf2Sample)");

    std::string path;
    std::uint32_t sampleIndex = 0;
    if (!splitUri(uri, path, sampleIndex)) {
        addError(diagnostics, uri, "pool.sf2_uri_invalid",
                 "malformed sf2:// sample reference: " + uri);
        return false;
    }

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        addError(diagnostics, path, "pool.sample_open_failed",
                 "cannot open SoundFont container: " + path);
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char chunk[65536];
    std::size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);

    return decodeImpl(bytes.data(), bytes.size(), sampleIndex, path, out, diagnostics);
}

bool readSf2SampleFromMemory(const std::uint8_t* data, std::size_t size,
                             std::uint32_t sampleIndex, DecodedWav& out,
                             std::vector<Diagnostic>* diagnostics,
                             const std::string& label)
{
    return decodeImpl(data, size, sampleIndex, label, out, diagnostics);
}

} // namespace fbsampler::detail
