#pragma once

#include "fbsampler/model.h"

#include <string>

namespace fbsampler {

/// Deterministic, byte-stable text dump for golden-file snapshots (SPEC.md
/// #Serialization). Not used for plugin state (Epic 6 owns that separately).
/// Fixed field order, std::to_chars float formatting, always "\n" endings.
std::string serializeModel(const InstrumentModel& model);

/// Inverse of serializeModel(). Strict: every declared field must be present
/// exactly once with a cleanly parseable value and no unknown keys may remain
/// (SPEC.md#Serialization). Returns false on malformed input, in which case
/// `out` is left untouched. Semantic checks (schema version match, value
/// ranges) are validate()'s job, not the parser's.
bool parseModel(const std::string& text, InstrumentModel& out);

} // namespace fbsampler
