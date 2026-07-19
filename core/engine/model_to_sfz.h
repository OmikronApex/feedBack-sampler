#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/model.h"

#include <string>
#include <vector>

namespace fbsampler::detail {

/// Lower a canonical model instance to synthetic in-memory SFZ text for the
/// sfizz backend.
///
/// TEMPORARY v0 SEAM (Story 1.4 Dev Notes option (b), flagged for
/// replacement): sfizz 1.2.3's region construction is welded to its parser,
/// so v0 feeds the model through generated SFZ text. This caps fidelity at
/// what SFZ text can express and must be replaced by direct region
/// construction before the SF2-extension work (AD-1). The seam is entirely
/// private to core/engine/ — nothing outside the engine sees SFZ text.
///
/// `regionSampleRates[i]` is the source sample rate of regions[i]'s file
/// (from the pool), used to convert SamplePositionUnit::Seconds positions to
/// frames at bind time. A rate of 0 means unknown (position opcodes for that
/// region are dropped); if `diagnostics` is non-null and the region actually
/// had a nonzero offset or an enabled loop, a warning is appended so the drop
/// is never silent.
///
/// v0 mod-matrix mapping: Cc-source entries lower to volume_oncc /
/// pitch_oncc / pan_oncc; Velocity/KeyTrack sources rely on the engine's
/// built-in velocity tracking / keytracking defaults; `curve` is not yet
/// expressed. Documented as part of the seam's fidelity cap.
std::string modelToSfzText(const InstrumentModel& model,
                           const std::vector<double>& regionSampleRates,
                           std::vector<Diagnostic>* diagnostics = nullptr);

} // namespace fbsampler::detail
