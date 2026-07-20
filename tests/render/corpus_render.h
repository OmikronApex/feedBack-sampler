#pragma once

#include "render_harness.h"

#include <string>

namespace fbsampler::testutil {

/// NFR-5 diff thresholds (defaults match the Story-1.4 render-regression
/// values; per-entry overrides live in corpus/manifest.json with rationale).
struct CorpusThresholds {
    double peak = 1e-2;        // max absolute per-sample deviation
    double rms = 1e-3;         // whole-render RMS deviation
    double windowRms = 3e-3;   // worst per-window RMS deviation
    int windowFrames = 4096;
    double minEnergy = 1e-2;   // silence guard: sum of squares over the render
};

/// One corpus entry, rendered and (optionally) diffed. All the fields the
/// per-library report needs; the runner serializes this to JSON.
struct CorpusEntryResult {
    bool loaded = false;    // lowering produced a validated model
    bool rendered = false;  // offline render completed with sound in it
    bool refCompared = false;
    bool passed = false;    // loaded && rendered && diff within thresholds
    double energy = 0.0;
    double peakDiff = 0.0;
    double rmsDiff = 0.0;
    double worstWindowRmsDiff = 0.0;
    std::uint64_t renderedFrames = 0;
    std::uint64_t referenceFrames = 0;
    int warningCount = 0;
    std::string error; // first hard failure, human-readable
};

/// Lower `instrumentPath` through the frontend selected by `format`
/// ("sfz", or "sf2"/"sf3" with `bank`/`program` naming the preset under
/// test — Story 2.5), render the timeline in `midiPath` for `totalFrames`
/// at 48 kHz, and diff against `referenceWavPath` (skipped when empty: the
/// result then reports load/render status only, used by
/// --update-references). When `writeWavPath` is non-empty the render is
/// written there as PCM16.
CorpusEntryResult runCorpusEntry(const std::string& instrumentPath,
                                 const std::string& format, int bank, int program,
                                 const std::string& midiPath,
                                 std::uint64_t totalFrames,
                                 const std::string& referenceWavPath,
                                 const CorpusThresholds& thresholds,
                                 const std::string& writeWavPath = {},
                                 const std::string& writeGoldenPath = {});

} // namespace fbsampler::testutil
