#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/model.h"

#include <vector>

namespace fbsampler {

/// Checks a model against the SPEC.md contract: range checks, unit sanity,
/// referential integrity, required-field presence. Returns one Diagnostic
/// per violation found (does not stop at the first); empty == valid.
std::vector<Diagnostic> validate(const InstrumentModel& model);

} // namespace fbsampler
