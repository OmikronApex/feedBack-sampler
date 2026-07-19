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
    region.positionUnit = SamplePositionUnit::Frames;
    region.offset = 0.0;
    region.loopEnabled = true;
    region.loopStart = 4410.0;
    region.loopEnd = 44100.0;
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

bool bitsEqual(float a, float b)
{
    std::uint32_t ba;
    std::uint32_t bb;
    std::memcpy(&ba, &a, sizeof(ba));
    std::memcpy(&bb, &b, sizeof(bb));
    return ba == bb;
}

bool bitsEqual(double a, double b)
{
    std::uint64_t ba;
    std::uint64_t bb;
    std::memcpy(&ba, &a, sizeof(ba));
    std::memcpy(&bb, &b, sizeof(bb));
    return ba == bb;
}

// Field-wise, bit-exact equality. Deliberately NOT implemented by comparing
// serializeModel() output: that would be circular — a field dropped
// symmetrically by both serializeModel() and parseModel() would be invisible.
bool modelsEqual(const InstrumentModel& a, const InstrumentModel& b)
{
    if (a.schemaVersion != b.schemaVersion || a.name != b.name
        || a.regions.size() != b.regions.size() || a.controls.size() != b.controls.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        const auto& ra = a.regions[i];
        const auto& rb = b.regions[i];
        if (ra.sampleFile != rb.sampleFile || ra.loKey != rb.loKey || ra.hiKey != rb.hiKey
            || ra.loVelocity != rb.loVelocity || ra.hiVelocity != rb.hiVelocity
            || ra.rootKey != rb.rootKey || !bitsEqual(ra.tuningCents, rb.tuningCents)
            || !bitsEqual(ra.gainDb, rb.gainDb) || !bitsEqual(ra.pan, rb.pan)
            || ra.positionUnit != rb.positionUnit
            || !bitsEqual(ra.offset, rb.offset) || ra.loopEnabled != rb.loopEnabled
            || !bitsEqual(ra.loopStart, rb.loopStart)
            || !bitsEqual(ra.loopEnd, rb.loopEnd)
            || !bitsEqual(ra.amplitudeEnvelope.delaySeconds, rb.amplitudeEnvelope.delaySeconds)
            || !bitsEqual(ra.amplitudeEnvelope.attackSeconds, rb.amplitudeEnvelope.attackSeconds)
            || !bitsEqual(ra.amplitudeEnvelope.holdSeconds, rb.amplitudeEnvelope.holdSeconds)
            || !bitsEqual(ra.amplitudeEnvelope.decaySeconds, rb.amplitudeEnvelope.decaySeconds)
            || !bitsEqual(ra.amplitudeEnvelope.sustainLevel, rb.amplitudeEnvelope.sustainLevel)
            || !bitsEqual(ra.amplitudeEnvelope.releaseSeconds, rb.amplitudeEnvelope.releaseSeconds)
            || ra.modMatrix.size() != rb.modMatrix.size()) {
            return false;
        }
        for (std::size_t j = 0; j < ra.modMatrix.size(); ++j) {
            const auto& ma = ra.modMatrix[j];
            const auto& mb = rb.modMatrix[j];
            if (ma.sourceControlId != mb.sourceControlId || ma.source.kind != mb.source.kind
                || ma.source.ccNumber != mb.source.ccNumber || ma.target != mb.target
                || !bitsEqual(ma.depth, mb.depth) || !bitsEqual(ma.curve, mb.curve)) {
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < a.controls.size(); ++i) {
        const auto& ca = a.controls[i];
        const auto& cb = b.controls[i];
        if (ca.id != cb.id || ca.displayName != cb.displayName
            || ca.accessibleName != cb.accessibleName) {
            return false;
        }
    }
    return true;
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open()); // fail loudly, not as an empty-string mismatch
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
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
    REQUIRE(text.rfind("schema_version=3\n", 0) == 0);
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

TEST_CASE("parseModel is strict, never silently defaulting", "[model][serialize]")
{
    const auto reference = serializeModel(makeReferenceModel());
    InstrumentModel model;

    SECTION("truncated input is rejected")
    {
        const auto truncated = reference.substr(0, reference.size() / 2);
        REQUIRE_FALSE(parseModel(truncated, model));
    }
    SECTION("numeric garbage is rejected, not parsed as 0")
    {
        auto text = reference;
        const std::string needle = "region[0].pan=-1";
        text.replace(text.find(needle), needle.size(), "region[0].pan=garbage");
        REQUIRE_FALSE(parseModel(text, model));
    }
    SECTION("unknown enum token is rejected, not coerced to a default")
    {
        auto text = reference;
        const std::string needle = "region[0].mod[0].target=Pan";
        text.replace(text.find(needle), needle.size(), "region[0].mod[0].target=Filter");
        REQUIRE_FALSE(parseModel(text, model));
    }
    SECTION("non-canonical bool is rejected")
    {
        auto text = reference;
        const std::string needle = "region[0].loop_enabled=1";
        text.replace(text.find(needle), needle.size(), "region[0].loop_enabled=true");
        REQUIRE_FALSE(parseModel(text, model));
    }
    SECTION("duplicate key is rejected")
    {
        REQUIRE_FALSE(parseModel(reference + "name=second\n", model));
    }
    SECTION("unknown stray key is rejected")
    {
        REQUIRE_FALSE(parseModel(reference + "region[9].pan=0\n", model));
    }
    SECTION("non-finite float tokens are rejected")
    {
        auto text = reference;
        const std::string needle = "region[0].tuning_cents=0.1";
        text.replace(text.find(needle), needle.size(), "region[0].tuning_cents=nan");
        REQUIRE_FALSE(parseModel(text, model));

        text = reference;
        const std::string needle2 = "region[0].loop_end=44100";
        text.replace(text.find(needle2), needle2.size(), "region[0].loop_end=inf");
        REQUIRE_FALSE(parseModel(text, model));
    }
    SECTION("negative or absurd counts are rejected before allocation")
    {
        REQUIRE_FALSE(parseModel("schema_version=1\nname=x\nregion_count=-1\ncontrol_count=0\n", model));
        REQUIRE_FALSE(parseModel("schema_version=1\nname=x\nregion_count=99999999999\ncontrol_count=0\n", model));
    }
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
