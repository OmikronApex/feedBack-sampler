#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbsampler::testutil {

/// Write deinterleaved float32 channels as an IEEE-float WAVE file.
/// Test tooling only (fixture regeneration + debugging renders).
bool writeFloatWav(const std::string& path,
                   const std::vector<std::vector<float>>& channels,
                   double sampleRate);

/// Write deinterleaved channels as PCM16 WAVE. Used for the checked-in corpus
/// reference renders (Story 1.6): half the size of float32, and the ±3e-5
/// quantization floor sits orders of magnitude under the NFR-5 thresholds.
bool writePcm16Wav(const std::string& path,
                   const std::vector<std::vector<float>>& channels,
                   double sampleRate);

} // namespace fbsampler::testutil
