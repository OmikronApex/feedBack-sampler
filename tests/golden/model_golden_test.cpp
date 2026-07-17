#include <catch2/catch_test_macros.hpp>

#include "fbsampler/serialize.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

using namespace fbsampler;

namespace {

// Fixed reference instance for the checked-in golden snapshot. Every numeric
// field uses a value whose shortest round-trip decimal representation is
// unambiguous (SPEC.md#Serialization) so the snapshot stays byte-stable
// across platforms/toolchains without depending on any single compiler's
// std::to_chars quirks for harder cases (those are covered separately below).
InstrumentModel makeReferenceModel()
{
    InstrumentModel model;
    model.schemaVersion = kModelSchemaVersion;
    model.name = "Golden \\ Instrument\nwith escapes";

    ControlMapEntry control;
    control.id = "cc74";
    control.displayName = "Filter Cutoff";
    control.accessibleName = "Filter cutoff control";
    model.controls.push_back(control);

    Region region;
    region.sampleFile = "samples/c4.wav";
    region.loKey = 0;
    region.hiKey = 60;
    region.loVelocity = 0;
    region.hiVelocity = 127;
    region.rootKey = 60;
    region.tuningCents = 0.1f;
    region.gainDb = -0.0f;
    region.pan = -1.0f;
    region.loopEnabled = true;
    region.loopStartSeconds = 0.1f;
    region.loopEndSeconds = 1.0f;
    region.amplitudeEnvelope.attackSeconds = 0.01f;
    region.amplitudeEnvelope.sustainLevel = 0.8f;

    ModMatrixEntry mod;
    mod.sourceControlId = "cc74";
    mod.source.kind = ModSourceKind::Cc;
    mod.source.ccNumber = 74;
    mod.target = ModTarget::Pan;
    mod.depth = 0.5f;
    mod.curve = 0.25f;
    region.modMatrix.push_back(mod);

    model.regions.push_back(region);

    Region silentRegion;
    silentRegion.sampleFile = "samples/rest.wav";
    model.regions.push_back(silentRegion);

    return model;
}

bool modelsEqual(const InstrumentModel& a, const InstrumentModel& b)
{
    return serializeModel(a) == serializeModel(b);
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool bitsEqual(float a, float b)
{
    std::uint32_t ba;
    std::uint32_t bb;
    std::memcpy(&ba, &a, sizeof(ba));
    std::memcpy(&bb, &b, sizeof(bb));
    return ba == bb;
}

} // namespace

TEST_CASE("serialize is deterministic across repeated calls", "[model][serialize]")
{
    const auto model = makeReferenceModel();
    REQUIRE(serializeModel(model) == serializeModel(model));
}

TEST_CASE("serialize -> parse -> serialize is byte-identical", "[model][serialize]")
{
    const auto model = makeReferenceModel();
    const auto text = serializeModel(model);

    InstrumentModel roundTripped;
    REQUIRE(parseModel(text, roundTripped));
    REQUIRE(modelsEqual(model, roundTripped));

    const auto reserialized = serializeModel(roundTripped);
    REQUIRE(text == reserialized);
}

TEST_CASE("serialized output starts with the schema version and uses \\n endings only", "[model][serialize]")
{
    const auto text = serializeModel(makeReferenceModel());
    REQUIRE(text.rfind("schema_version=1\n", 0) == 0);
    REQUIRE(text.find('\r') == std::string::npos);
}

TEST_CASE("serialized output matches the checked-in reference snapshot", "[model][serialize][golden]")
{
    const auto text = serializeModel(makeReferenceModel());
    const auto reference = readFile(std::string(FBSAMPLER_GOLDEN_DIR) + "/model_v0_reference.txt");
    REQUIRE(text == reference);
}

TEST_CASE("parseModel rejects malformed input", "[model][serialize]")
{
    InstrumentModel model;
    REQUIRE_FALSE(parseModel("not a valid line without equals\n", model));
    REQUIRE_FALSE(parseModel("", model));
}

// Float determinism is the hard part of AC2 (Dev Notes): denormals, negative
// zero, and 0.1-style values are exactly the cases where naive formatting
// (std::to_string/iostream defaults) loses precision or is locale-dependent.
// These are checked via bit-exact round-trip rather than a fixed golden
// string, since some of these values (denorm_min in particular) sit at the
// edge of what's practical to hand-verify outside a running toolchain.
TEST_CASE("tricky float values survive serialize -> parse bit-exact", "[model][serialize][float]")
{
    auto model = makeReferenceModel();

    const float trickyValues[] = {
        0.1f,
        -0.0f,
        std::numeric_limits<float>::denorm_min(),
        -std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::min(),
    };

    for (float value : trickyValues) {
        model.regions.front().tuningCents = value;

        const auto text = serializeModel(model);
        InstrumentModel roundTripped;
        REQUIRE(parseModel(text, roundTripped));
        REQUIRE(bitsEqual(roundTripped.regions.front().tuningCents, value));
    }
}
