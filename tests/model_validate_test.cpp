#include <catch2/catch_test_macros.hpp>

#include "fbsampler/validate.h"

#include <algorithm>
#include <limits>

using namespace fbsampler;

namespace {

InstrumentModel makeValidModel()
{
    InstrumentModel model;
    model.schemaVersion = kModelSchemaVersion;
    model.name = "test instrument";

    ControlMapEntry control;
    control.id = "cc74";
    control.displayName = "Filter Cutoff";
    control.accessibleName = "Filter cutoff control";
    model.controls.push_back(control);

    Region region;
    region.sampleFile = "samples/c4.wav";
    region.loKey = 0;
    region.hiKey = 127;
    region.loVelocity = 0;
    region.hiVelocity = 127;
    region.rootKey = 60;
    region.tuningCents = 0.0f;
    region.gainDb = 0.0f;
    region.pan = 0.0f;
    region.offsetSeconds = 0.0f;
    region.loopEnabled = true;
    region.loopStartSeconds = 0.1f;
    region.loopEndSeconds = 1.0f;
    region.amplitudeEnvelope.sustainLevel = 1.0f;

    ModMatrixEntry mod;
    mod.sourceControlId = "cc74";
    mod.source.kind = ModSourceKind::Cc;
    mod.source.ccNumber = 74;
    mod.target = ModTarget::Gain;
    mod.depth = -6.0f;
    mod.curve = 0.5f;
    region.modMatrix.push_back(mod);

    model.regions.push_back(region);
    return model;
}

bool hasCode(const std::vector<Diagnostic>& diagnostics, const std::string& code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [&](const Diagnostic& d) { return d.code == code; });
}

} // namespace

TEST_CASE("a valid model passes validation", "[model][validate]")
{
    const auto model = makeValidModel();
    REQUIRE(validate(model).empty());
}

TEST_CASE("schema version mismatch is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.schemaVersion = kModelSchemaVersion + 1;
    REQUIRE(hasCode(validate(model), "model.schema_version_mismatch"));
}

TEST_CASE("duplicate control id is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.controls.push_back(model.controls.front());
    REQUIRE(hasCode(validate(model), "model.duplicate_control_id"));
}

TEST_CASE("empty control id is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.controls.front().id.clear();
    REQUIRE(hasCode(validate(model), "control.id_missing"));
}

TEST_CASE("missing sample file is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().sampleFile.clear();
    REQUIRE(hasCode(validate(model), "region.sample_file_missing"));
}

TEST_CASE("inverted key range is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().loKey = 100;
    model.regions.front().hiKey = 10;
    REQUIRE(hasCode(validate(model), "region.key_range_invalid"));
}

TEST_CASE("inverted velocity range is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().loVelocity = 100;
    model.regions.front().hiVelocity = 10;
    REQUIRE(hasCode(validate(model), "region.velocity_range_invalid"));
}

TEST_CASE("out-of-range pan is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().pan = 1.5f;
    REQUIRE(hasCode(validate(model), "region.pan_out_of_range"));
}

TEST_CASE("non-finite gain is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().gainDb = std::numeric_limits<float>::infinity();
    REQUIRE(hasCode(validate(model), "region.gain_not_finite"));
}

TEST_CASE("non-finite tuning is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().tuningCents = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(hasCode(validate(model), "region.tuning_not_finite"));
}

TEST_CASE("negative offset is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().offsetSeconds = -0.5f;
    REQUIRE(hasCode(validate(model), "region.offset_negative"));
}

TEST_CASE("invalid loop range is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().loopStartSeconds = 1.0f;
    model.regions.front().loopEndSeconds = 0.5f;
    REQUIRE(hasCode(validate(model), "region.loop_range_invalid"));
}

TEST_CASE("negative envelope time is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().amplitudeEnvelope.attackSeconds = -0.1f;
    REQUIRE(hasCode(validate(model), "region.envelope_time_negative"));
}

TEST_CASE("out-of-range envelope sustain is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().amplitudeEnvelope.sustainLevel = 1.5f;
    REQUIRE(hasCode(validate(model), "region.envelope_sustain_out_of_range"));
}

TEST_CASE("non-finite mod depth is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().modMatrix.front().depth = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(hasCode(validate(model), "region.mod_depth_not_finite"));
}

TEST_CASE("out-of-range mod curve is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().modMatrix.front().curve = 2.0f;
    REQUIRE(hasCode(validate(model), "region.mod_curve_out_of_range"));
}

TEST_CASE("mod matrix referencing an unknown control is rejected", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().modMatrix.front().sourceControlId = "does-not-exist";
    REQUIRE(hasCode(validate(model), "region.mod_source_unknown_control"));
}

TEST_CASE("multiple violations are all reported, not just the first", "[model][validate]")
{
    auto model = makeValidModel();
    model.regions.front().loKey = 100;
    model.regions.front().hiKey = 10;
    model.regions.front().pan = 5.0f;
    const auto diagnostics = validate(model);
    REQUIRE(hasCode(diagnostics, "region.key_range_invalid"));
    REQUIRE(hasCode(diagnostics, "region.pan_out_of_range"));
}
