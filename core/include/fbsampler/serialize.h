#pragma once

#include "fbsampler/model.h"

#include <string>

namespace fbsampler {

/// Deterministic, byte-stable text dump for golden-file snapshots (SPEC.md
/// #Serialization). Not used for plugin state (Epic 6 owns that separately).
/// Fixed field order, std::to_chars float formatting, always "\n" endings.
std::string serializeModel(const InstrumentModel& model);

/// Inverse of serializeModel(). Returns false and leaves `out` unspecified on
/// malformed input.
bool parseModel(const std::string& text, InstrumentModel& out);

} // namespace fbsampler
