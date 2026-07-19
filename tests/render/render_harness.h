#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/engine.h"
#include "fbsampler/model.h"
#include "fbsampler/pool.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fbsampler::testutil {

/// One timeline event for the offline renderer: `frame` is absolute time in
/// samples from render start.
struct TimelineEvent {
    std::uint64_t frame = 0;
    EngineEvent::Type type = EngineEvent::Type::NoteOn;
    std::uint8_t note = 60;      // note number, or CC number for ControlChange
    std::uint8_t velocity = 100; // velocity, or CC value for ControlChange
    int bendValue = 0;           // PitchBend only: -8192..8191
};

/// Deterministic offline render settings (Story 1.4 AC 1: fixed rate and
/// block size so renders are reproducible).
struct RenderSettings {
    double sampleRate = 48000.0;
    int blockFrames = 256;
    std::uint64_t totalFrames = 0;
    /// Wrap each process() call in an rtcheck section (RT-safety test).
    bool markRtSections = false;
};

struct RenderResult {
    std::vector<std::vector<float>> channels; // stereo, [2][totalFrames]
    std::vector<Diagnostic> diagnostics;
    bool ok = false;
};

/// Reusable offline renderer (kept as a library function, not test-inline:
/// Story 1.6 wires it to the corpus, Epic 2/4 diff it against oracles).
/// Loads `model` into a fresh Engine over `pool` and renders the timeline
/// block by block.
RenderResult renderOffline(const InstrumentModel& model,
                           const std::shared_ptr<SamplePool>& pool,
                           const std::string& instrumentRoot,
                           const std::vector<TimelineEvent>& timeline,
                           const RenderSettings& settings);

} // namespace fbsampler::testutil
