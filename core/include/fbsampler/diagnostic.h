#pragma once

#include <string>

namespace fbsampler {

/// Severity of a Diagnostic. See SPEC.md#Diagnostic.
enum class Severity {
    Error,
    Warning,
};

/// Where a Diagnostic originated. Unpopulated (line/column == -1, file empty)
/// when the model was constructed in-memory rather than parsed from text.
struct SourceLocation {
    std::string file;
    int line = -1;
    int column = -1;
};

/// The one error shape for the core API (spine Consistency Conventions).
/// No exceptions cross the core API; fallible operations return
/// std::vector<Diagnostic> (empty == success).
struct Diagnostic {
    Severity severity = Severity::Error;
    std::string code;    // stable, dotted, e.g. "region.key_range_invalid"
    std::string message; // human-readable
    SourceLocation location;
};

} // namespace fbsampler
