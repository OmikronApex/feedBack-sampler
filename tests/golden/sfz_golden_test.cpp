// Per-frontend golden lowering harness (Story 1.3): fixture -> lower ->
// validate (Story 1.2 suite) -> serialize -> byte-exact compare against the
// checked-in snapshot. Stories 2.1 / 4.1 clone this pattern for their formats:
// only the fixture list and the lower() call are format-specific.
//
// Regenerating snapshots: set FBSAMPLER_UPDATE_GOLDENS=1 in the environment
// and run the [sfz][golden] tests once; the expected files are rewritten from
// the current lowering output. Inspect the diff before committing.

#include <catch2/catch_test_macros.hpp>

#include "fbsampler/serialize.h"
#include "fbsampler/sfz_frontend.h"
#include "fbsampler/validate.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace fbsampler;

namespace {

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open()); // fail loudly, not as an empty-string mismatch
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool updateGoldensRequested()
{
    const char* env = std::getenv("FBSAMPLER_UPDATE_GOLDENS");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
}

void checkFixture(const std::string& name)
{
    const std::string dir = std::string(FBSAMPLER_GOLDEN_DIR) + "/sfz/";
    const auto result = lowerSfzFile(dir + name + ".sfz");

    for (const auto& d : result.diagnostics) {
        INFO(d.code << ": " << d.message << " (" << d.location.file << ":" << d.location.line
                    << ")");
        CHECK(d.severity != Severity::Error);
    }
    REQUIRE(result.model.has_value());
    REQUIRE(validate(*result.model).empty());

    const auto text = serializeModel(*result.model);
    const std::string expectedPath = dir + name + ".expected.txt";

    if (updateGoldensRequested()) {
        std::ofstream out(expectedPath, std::ios::binary);
        REQUIRE(out.is_open());
        out << text;
        out.close();
        // A truncated snapshot (disk full, failed flush) must fail the run,
        // not silently corrupt the golden baseline.
        REQUIRE(out.good());
        SUCCEED("golden snapshot regenerated: " + expectedPath);
        return;
    }

    REQUIRE(text == readFile(expectedPath));
}

} // namespace

TEST_CASE("SFZ golden: multi-region key/vel layering with group inheritance", "[sfz][golden]")
{
    checkFixture("layers");
}

TEST_CASE("SFZ golden: loops, offset and amplitude envelope", "[sfz][golden]")
{
    checkFixture("loops_env");
}

TEST_CASE("SFZ golden: CC labels, default_path, includes and defines", "[sfz][golden]")
{
    checkFixture("controls");
}

TEST_CASE("SFZ missing sample file warns per region without aborting", "[sfz][golden]")
{
    // FR-5 groundwork: a missing sample is a per-region warning; the
    // instrument still lowers with the region kept as a reference.
    const std::string dir = std::string(FBSAMPLER_GOLDEN_DIR) + "/sfz/";
    const auto result = lowerSfzFile(dir + "missing_sample.sfz");
    REQUIRE(result.model.has_value());
    REQUIRE(result.model->regions.size() == 2);
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const Diagnostic& d) {
                          return d.code == "sfz.sample_file_missing"
                              && d.severity == Severity::Warning;
                      }));

    // And the lowered result is snapshot-pinned like every other fixture, so a
    // regression in how a missing-sample region serializes is caught byte-exactly.
    checkFixture("missing_sample");
}
