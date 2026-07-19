#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fbsampler/sfz_frontend.h"
#include "fbsampler/validate.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace fbsampler;

namespace {

bool hasDiag(const LowerResult& result, const std::string& code, Severity severity)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&](const Diagnostic& d) { return d.code == code && d.severity == severity; });
}

bool hasError(const LowerResult& result)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

} // namespace

TEST_CASE("lowers a minimal region with key/vel ranges and units converted", "[sfz]")
{
    const auto result = lowerSfzText(
        "<region> sample=samples\\a.wav lokey=36 hikey=48 lovel=10 hivel=100 "
        "pitch_keycenter=42 transpose=1 tune=-25 volume=-6.5 pan=50\n");

    REQUIRE(result.model.has_value());
    REQUIRE(result.model->regions.size() == 1);
    const auto& r = result.model->regions.front();
    CHECK(r.sampleFile == "samples/a.wav"); // backslashes normalized
    CHECK(r.loKey == 36);
    CHECK(r.hiKey == 48);
    CHECK(r.loVelocity == 10);
    CHECK(r.hiVelocity == 100);
    CHECK(r.rootKey == 42);
    CHECK_THAT(r.tuningCents, Catch::Matchers::WithinAbs(75.0f, 1e-4)); // 1 st - 25 ct
    CHECK_THAT(r.gainDb, Catch::Matchers::WithinAbs(-6.5f, 1e-6));
    CHECK_THAT(r.pan, Catch::Matchers::WithinAbs(0.5f, 1e-6)); // -100..100 -> -1..1
    CHECK(validate(*result.model).empty());
}

TEST_CASE("key opcode sets lokey, hikey and root; note names accepted", "[sfz]")
{
    const auto result = lowerSfzText("<region> sample=a.wav key=c4\n");
    REQUIRE(result.model.has_value());
    const auto& r = result.model->regions.front();
    CHECK(r.loKey == 60);
    CHECK(r.hiKey == 60);
    CHECK(r.rootKey == 60);
}

TEST_CASE("group/global/master opcodes flatten into regions, later scopes win", "[sfz]")
{
    const auto result = lowerSfzText(
        "<global> volume=-3 lovel=1\n"
        "<master> pan=-100\n"
        "<group> lokey=24 hikey=36 volume=-9\n"
        "<region> sample=a.wav\n"
        "<region> sample=b.wav volume=0\n"
        "<group> lokey=48\n"
        "<region> sample=c.wav\n");

    REQUIRE(result.model.has_value());
    REQUIRE(result.model->regions.size() == 3);
    const auto& regions = result.model->regions;
    CHECK(regions[0].loKey == 24);
    CHECK(regions[0].gainDb == -9.0f);      // group overrides global
    CHECK(regions[0].pan == -1.0f);         // master applies
    CHECK(regions[0].loVelocity == 1);      // global applies
    CHECK(regions[1].gainDb == 0.0f);       // region overrides group
    CHECK(regions[2].loKey == 48);          // new group resets
    CHECK(regions[2].hiKey == 127);         // previous group's hikey gone
    CHECK(regions[2].gainDb == -3.0f);      // global survives group reset
}

TEST_CASE("loops, offset and envelope lower with unit conversion", "[sfz]")
{
    const auto result = lowerSfzText(
        "<region> sample=a.wav loop_mode=loop_continuous loop_start=44100 loop_end=88200 "
        "offset=22050 ampeg_delay=0.5 ampeg_attack=0.01 ampeg_hold=0.1 ampeg_decay=0.2 "
        "ampeg_sustain=80 ampeg_release=0.3\n");

    REQUIRE(result.model.has_value());
    const auto& r = result.model->regions.front();
    CHECK(r.loopEnabled);
    // Sample positions are stored as frames verbatim (positionUnit tag); the
    // engine converts once the sample's real rate is known (Story 1.4).
    CHECK(r.positionUnit == SamplePositionUnit::Frames);
    CHECK(r.loopStart == 44100.0);
    CHECK(r.loopEnd == 88200.0);
    CHECK(r.offset == 22050.0);
    CHECK_THAT(r.amplitudeEnvelope.delaySeconds, Catch::Matchers::WithinAbs(0.5f, 1e-6));
    CHECK_THAT(r.amplitudeEnvelope.attackSeconds, Catch::Matchers::WithinAbs(0.01f, 1e-6));
    CHECK_THAT(r.amplitudeEnvelope.holdSeconds, Catch::Matchers::WithinAbs(0.1f, 1e-6));
    CHECK_THAT(r.amplitudeEnvelope.decaySeconds, Catch::Matchers::WithinAbs(0.2f, 1e-6));
    CHECK_THAT(r.amplitudeEnvelope.sustainLevel, Catch::Matchers::WithinAbs(0.8f, 1e-6));
    CHECK_THAT(r.amplitudeEnvelope.releaseSeconds, Catch::Matchers::WithinAbs(0.3f, 1e-6));
}

TEST_CASE("cc labels become control-map entries with stable ids", "[sfz]")
{
    const auto result = lowerSfzText(
        "<control> label_cc74=Cutoff label_cc1=Vibrato\n"
        "<region> sample=a.wav\n");

    REQUIRE(result.model.has_value());
    REQUIRE(result.model->controls.size() == 2);
    CHECK(result.model->controls[0].id == "cc74"); // AD-8: identity from CC number
    CHECK(result.model->controls[0].displayName == "Cutoff");
    CHECK(result.model->controls[0].accessibleName == "Cutoff"); // AD-11
    CHECK(result.model->controls[1].id == "cc1");
}

TEST_CASE("default_path prefixes sample references", "[sfz]")
{
    const auto result = lowerSfzText(
        "<control> default_path=Samples\\Piano\\\n"
        "<region> sample=c4.wav\n");
    REQUIRE(result.model.has_value());
    CHECK(result.model->regions.front().sampleFile == "Samples/Piano/c4.wav");
}

TEST_CASE("default_path does not mangle absolute sample paths", "[sfz]")
{
    const auto result = lowerSfzText(
        "<control> default_path=Samples\\\n"
        "<region> sample=/abs/a.wav\n"
        "<region> sample=C:\\abs\\b.wav\n");
    REQUIRE(result.model.has_value());
    REQUIRE(result.model->regions.size() == 2);
    CHECK(result.model->regions[0].sampleFile == "/abs/a.wav");
    CHECK(result.model->regions[1].sampleFile == "C:/abs/b.wav");
}

TEST_CASE("invalid key value leaves inherited key range untouched", "[sfz]")
{
    const auto result = lowerSfzText(
        "<group> lokey=10 hikey=20 pitch_keycenter=15\n"
        "<region> sample=a.wav key=garbage\n");
    REQUIRE(result.model.has_value());
    const auto& r = result.model->regions.front();
    CHECK(r.loKey == 10);
    CHECK(r.hiKey == 20);
    CHECK(r.rootKey == 15);
    CHECK(hasDiag(result, "sfz.invalid_opcode_value", Severity::Warning));
}

TEST_CASE("huge finite tuning values are clamped, not lowered to infinity", "[sfz]")
{
    const auto result = lowerSfzText("<region> sample=a.wav transpose=3e38 tune=-3e38\n");
    REQUIRE(result.model.has_value()); // one bad opcode must not suppress the instrument
    CHECK(std::isfinite(result.model->regions.front().tuningCents));
    CHECK(hasDiag(result, "sfz.value_clamped", Severity::Warning));
    CHECK(validate(*result.model).empty());
}

TEST_CASE("unsupported opcodes and headers warn, never silently skip", "[sfz]")
{
    const auto result = lowerSfzText(
        "<curve> curve_index=1\n"
        "<region> sample=a.wav some_aria_opcode=3\n");
    REQUIRE(result.model.has_value()); // warnings never suppress the model
    CHECK(hasDiag(result, "sfz.header_unsupported", Severity::Warning));
    CHECK(hasDiag(result, "sfz.opcode_unsupported", Severity::Warning));
}

TEST_CASE("region without sample is dropped with a warning", "[sfz]")
{
    const auto result = lowerSfzText("<region> lokey=10 hikey=20\n");
    REQUIRE(result.model.has_value());
    CHECK(result.model->regions.empty());
    CHECK(hasDiag(result, "sfz.region_missing_sample", Severity::Warning));
}

TEST_CASE("out-of-range values are clamped or repaired so the model validates", "[sfz]")
{
    const auto result = lowerSfzText(
        "<region> sample=a.wav hikey=200 pan=500 ampeg_sustain=150\n"
        "<region> sample=b.wav lokey=60 hikey=40\n"
        "<region> sample=c.wav loop_mode=loop_continuous\n"); // loop points default 0/0

    REQUIRE(result.model.has_value());
    CHECK(validate(*result.model).empty());
    CHECK(hasDiag(result, "sfz.value_clamped", Severity::Warning));
    CHECK(hasDiag(result, "sfz.key_range_swapped", Severity::Warning));
    CHECK(hasDiag(result, "sfz.loop_range_invalid", Severity::Warning));
    CHECK_FALSE(result.model->regions[2].loopEnabled);
}

TEST_CASE("malformed input yields error diagnostics with location, no model, no throw", "[sfz]")
{
    // A dangling <region with no closing bracket is a hard parse error.
    const auto result = lowerSfzText("<region\nsample=a.wav\n", "broken.sfz");
    CHECK_FALSE(result.model.has_value());
    REQUIRE(hasError(result));
    const auto it = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
                                 [](const Diagnostic& d) { return d.severity == Severity::Error; });
    CHECK(it->code == "sfz.parse_error");
    CHECK(it->location.file == "broken.sfz");
    CHECK(it->location.line >= 1);
}

TEST_CASE("defines expand and includes are rejected in text mode", "[sfz]")
{
    const auto defined = lowerSfzText(
        "#define $KEY 62\n<region> sample=a.wav key=$KEY\n");
    REQUIRE(defined.model.has_value());
    CHECK(defined.model->regions.front().rootKey == 62);

    // Text mode must not touch the file system: #include is a parse error.
    const auto included = lowerSfzText("#include \"other.sfz\"\n");
    CHECK_FALSE(included.model.has_value());
    CHECK(hasError(included));
}

TEST_CASE("text mode rejects includes even when the target file exists", "[sfz]")
{
    // Regression guard for the include-depth trick (AD-10): the no-filesystem
    // guarantee must hold when the include target is a real, readable file
    // next to the virtual path — not only when the target is absent.
    namespace tfs = std::filesystem;
    const tfs::path dir = tfs::temp_directory_path() / "fbsampler_sfz_include_test";
    tfs::create_directories(dir);
    {
        std::ofstream target(dir / "other.sfz", std::ios::binary);
        REQUIRE(target.is_open());
        target << "<region> sample=a.wav\n";
    }

    const auto result = lowerSfzText("#include \"other.sfz\"\n",
                                     (dir / "main.sfz").string());
    CHECK_FALSE(result.model.has_value());
    CHECK(hasError(result));

    std::error_code ec;
    tfs::remove_all(dir, ec);
}

TEST_CASE("missing file path yields an error, not an exception", "[sfz]")
{
    const auto result = lowerSfzFile("definitely/not/here.sfz");
    CHECK_FALSE(result.model.has_value());
    CHECK(hasError(result));
}

TEST_CASE("empty and pathological inputs never crash or throw", "[sfz]")
{
    using namespace std::string_literals;
    // string_literals so embedded NULs survive ("\0x"s is 2 bytes, not an
    // empty C string).
    for (const auto& text : { ""s, "\n"s, "<>"s, "="s, "<region>"s, "#define"s, "#include"s,
                              "<region> sample="s, "$"s, "<region> sample=a.wav key="s,
                              "\0x"s, "<region>\0sample=a.wav"s }) {
        const auto result = lowerSfzText(text);
        // Either outcome is fine; the contract is only: no throw, and errors
        // imply no model.
        if (hasError(result))
            CHECK_FALSE(result.model.has_value());
    }
}

TEST_CASE("bend_up/bend_down lower into the model bend range", "[sfz]")
{
    const auto result = lowerSfzText(
        "<region> sample=a.wav bend_up=1200 bend_down=-1200\n"
        "<region> sample=b.wav\n");
    REQUIRE(result.model.has_value());
    REQUIRE(result.model->regions.size() == 2);
    CHECK(result.model->regions[0].bendUpCents == 1200.0f);
    CHECK(result.model->regions[0].bendDownCents == -1200.0f);
    // SFZ defaults when unspecified.
    CHECK(result.model->regions[1].bendUpCents == 200.0f);
    CHECK(result.model->regions[1].bendDownCents == -200.0f);
}

TEST_CASE("out-of-range bend values are clamped with a warning", "[sfz]")
{
    const auto result = lowerSfzText("<region> sample=a.wav bend_up=20000\n");
    REQUIRE(result.model.has_value());
    CHECK(result.model->regions[0].bendUpCents == 9600.0f);
    bool clamped = false;
    for (const auto& d : result.diagnostics)
        if (d.code == "sfz.value_clamped")
            clamped = true;
    CHECK(clamped);
}
