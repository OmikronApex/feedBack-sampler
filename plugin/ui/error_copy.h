#pragma once

// Pure error-copy translation layer (Story 3.6, UX-DR13): Diagnostic codes
// -> problem + fix phrasing. Never raw parser text, never "parse error",
// no exclamation marks. Unit-tested directly.

#include "fbsampler/diagnostic.h"

#include <string>
#include <vector>

namespace fbsampler::ui {

/// Maps one diagnostic to user copy: "<problem> — <next step>". Unmapped
/// codes fall back to a template that still names problem + next step.
std::string userCopyForDiagnostic(const Diagnostic& diagnostic);

/// Summarizes a load failure's diagnostics into one user-facing line
/// (first Error wins; Warnings only when no Error exists).
std::string userCopyForFailure(const std::vector<Diagnostic>& diagnostics);

/// Maps a stored index statusDetail (written at load-failure time, shaped
/// "code: message") back to user copy for row tooltips/strips.
std::string userCopyForStatusDetail(const std::string& statusDetail);

} // namespace fbsampler::ui
