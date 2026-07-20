#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/model.h"
#include "fbsampler/pool.h"

#include <string>
#include <vector>

namespace fbsampler::detail {

/// Lower a canonical model instance to synthetic in-memory SFZ text for the
/// sfizz backend.
///
/// Story 2.2 seam decision: the bind path stays text-shaped but now targets
/// sfizz's EXTENDED dialect, which closes the expressiveness gaps that
/// deferred-work.md flagged for "direct region construction":
///  - embedded container samples (sf2:// references) are emitted as
///    `<sample> name=... base64data=...` headers, which sfizz loads straight
///    into its FilePool from RAM — no temp files, no disk;
///  - SF2 curve shapes are emitted as `<curve>` headers referenced by
///    `*_curveccN`, and velocity->gain curves as `amp_velcurve_N` points.
/// Direct sfizz region construction (a vendored patch) was therefore not
/// needed; the seam is still entirely private to core/engine/.
///
/// `regionSampleRates[i]` is the source sample rate of regions[i]'s file
/// (from the pool), used to convert SamplePositionUnit::Seconds positions to
/// frames at bind time. A rate of 0 means unknown (position opcodes for that
/// region are dropped with a warning when audible).
///
/// `pool`/`regionHandles` (aligned with model.regions) supply the decoded
/// audio for container-sample references; pass nullptr/empty when the model
/// has no such references (plain path regions ignore them).
///
/// Mod-matrix mapping: Cc -> Gain/Pitch/Pan lower to volume_/pitch_/pan_oncc
/// with a `<curve>` header when the shape is not default-linear;
/// Velocity -> Gain lowers to amp_veltrack + amp_velcurve points;
/// Velocity -> Pitch (linear) lowers to pitch_veltrack. Everything else is
/// dropped with a structured warning (engine.mod_source_unsupported /
/// engine.mod_target_unsupported / engine.mod_amount_source_dropped) — the
/// fidelity gap is tracked, never silent (AD-1).
std::string modelToSfzText(const InstrumentModel& model,
                           const std::vector<double>& regionSampleRates,
                           std::vector<Diagnostic>* diagnostics = nullptr,
                           const SamplePool* pool = nullptr,
                           const std::vector<SampleHandle>* regionHandles = nullptr);

} // namespace fbsampler::detail
