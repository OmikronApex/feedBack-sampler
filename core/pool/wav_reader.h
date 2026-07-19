#pragma once

#include "fbsampler/diagnostic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fbsampler::detail {

/// Decoded audio, deinterleaved float32. Internal to the pool (AD-2: only the
/// pool touches sample bytes) and the offline test tooling.
struct DecodedWav {
    std::uint32_t numChannels = 0;
    std::uint64_t numFrames = 0;
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels; // [channel][frame]
};

/// Minimal RIFF/WAVE reader: PCM 16/24/32-bit and IEEE float32, any channel
/// count. Returns false and appends a Diagnostic on failure. Control/loader
/// thread only (file I/O).
bool readWavFile(const std::string& path, DecodedWav& out,
                 std::vector<Diagnostic>* diagnostics);

} // namespace fbsampler::detail
