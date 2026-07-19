#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/model.h"

#include <optional>
#include <string>
#include <vector>

namespace fbsampler {

/// Result of lowering a frontend format into the canonical model.
/// `model` is empty when a structural (Severity::Error) diagnostic occurred;
/// warnings alone never suppress the model. Never throws (spine convention:
/// no exceptions cross the core API).
struct LowerResult {
    std::optional<InstrumentModel> model;
    std::vector<Diagnostic> diagnostics;
};

/// Lower an .sfz file from disk into the canonical model. `#include` resolves
/// relative to the file; referenced sample files are checked for existence
/// (missing samples produce per-region warnings, never abort the instrument).
LowerResult lowerSfzFile(const std::string& path);

/// Lower SFZ text from memory. Performs no file-system access: `#include`
/// directives fail with a parse error and sample references are not checked
/// for existence. This is the fuzzing entry point (AD-10: parsers are pure
/// functions fuzzable in isolation). `virtualPath` only labels diagnostics
/// and the instrument name.
LowerResult lowerSfzText(const std::string& text,
                         const std::string& virtualPath = "<memory>");

} // namespace fbsampler
