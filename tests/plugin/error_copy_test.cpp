// Story 3.6: error-copy mapper — problem + fix, never jargon.

#include "ui/error_copy.h"

#include <catch2/catch_test_macros.hpp>

using namespace fbsampler;
using fbsampler::ui::userCopyForDiagnostic;
using fbsampler::ui::userCopyForFailure;
using fbsampler::ui::userCopyForStatusDetail;

namespace {

Diagnostic diag(const char* code, Severity sev = Severity::Error)
{
    Diagnostic d;
    d.severity = sev;
    d.code = code;
    d.message = "raw parser text that must never surface";
    return d;
}

void checkShape(const std::string& copy)
{
    CHECK_FALSE(copy.empty());
    CHECK(copy.find('!') == std::string::npos);        // no exclamation marks
    CHECK(copy.find("parse error") == std::string::npos);
    CHECK(copy.find("raw parser text") == std::string::npos);
    // Problem + fix phrasing: an em dash separates problem from next step.
    CHECK(copy.find("\xe2\x80\x94") != std::string::npos);
}

} // namespace

TEST_CASE("known codes map to problem + fix copy", "[errorcopy]")
{
    const auto missing = userCopyForDiagnostic(diag("pool.file_missing"));
    checkShape(missing);
    CHECK(missing.find("missing") != std::string::npos);
    CHECK(missing.find("rescan") != std::string::npos);

    const auto unreadable = userCopyForDiagnostic(diag("sfz.file_unreadable"));
    checkShape(unreadable);
    CHECK(unreadable.find("permissions") != std::string::npos);
}

TEST_CASE("unmapped code falls back to problem + next step", "[errorcopy]")
{
    const auto copy = userCopyForDiagnostic(diag("totally.unknown_code"));
    checkShape(copy);
}

TEST_CASE("failure summary prefers the first Error diagnostic", "[errorcopy]")
{
    const auto copy = userCopyForFailure({ diag("sfz.some_warning",
                                               Severity::Warning),
                                           diag("pool.file_missing") });
    CHECK(copy.find("missing") != std::string::npos);
}

TEST_CASE("stored statusDetail (code: message) maps back to copy",
          "[errorcopy]")
{
    const auto copy =
        userCopyForStatusDetail("sf2.riff_truncated: RIFF chunk short read");
    checkShape(copy);
    CHECK(copy.find("SoundFont") != std::string::npos);
}
