#pragma once

#include "fbsampler/diagnostic.h"
#include "wav_reader.h" // DecodedWav

#include <cstdint>
#include <string>
#include <vector>

namespace fbsampler::detail {

/// True when `path` is a container-sample URI ("sf2://<path>#<sampleIndex>",
/// SPEC.md#Sample-references).
bool isSf2SampleUri(const std::string& path);

/// Decode ONE embedded SoundFont sample referenced by an sf2:// URI to
/// deinterleaved float32 (mono; SF2 samples are per-channel mono records).
/// The 16-bit sdta smpl data is decoded; the optional sm24 extension is
/// ignored in v0. Returns false and appends a Diagnostic on failure.
/// Control/loader thread only (file I/O). AD-2: this is the only place
/// SoundFont sample bytes are touched.
bool readSf2Sample(const std::string& uri, DecodedWav& out,
                   std::vector<Diagnostic>* diagnostics);

/// Same decode against an in-memory container image (fuzzing entry point —
/// the Vorbis decoder is the new attack surface and must be reachable
/// without disk I/O). `label` only annotates diagnostics.
bool readSf2SampleFromMemory(const std::uint8_t* data, std::size_t size,
                             std::uint32_t sampleIndex, DecodedWav& out,
                             std::vector<Diagnostic>* diagnostics,
                             const std::string& label = "<memory>");

} // namespace fbsampler::detail
