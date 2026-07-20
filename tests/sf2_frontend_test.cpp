// SF2 frontend unit tests (Story 2.1): structural validation diagnostics,
// preset enumeration, unit conversion, robustness policy, and pool-side
// embedded-sample decoding via the sf2:// reference scheme.

#include <catch2/catch_test_macros.hpp>

#include "fbsampler/pool.h"
#include "fbsampler/sf2_frontend.h"
#include "fbsampler/validate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace fbsampler;

namespace {

std::string fixturePath(const std::string& name)
{
    return std::string(FBSAMPLER_GOLDEN_DIR) + "/sf2/" + name;
}

std::vector<std::uint8_t> readBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

bool hasError(const std::vector<Diagnostic>& diags, const std::string& code)
{
    return std::any_of(diags.begin(), diags.end(), [&](const Diagnostic& d) {
        return d.code == code && d.severity == Severity::Error;
    });
}

bool hasWarning(const std::vector<Diagnostic>& diags, const std::string& code)
{
    return std::any_of(diags.begin(), diags.end(), [&](const Diagnostic& d) {
        return d.code == code && d.severity == Severity::Warning;
    });
}

} // namespace

TEST_CASE("listSf2Presets enumerates bank/program/name without lowering", "[sf2]")
{
    const auto result = listSf2Presets(fixturePath("layers.sf2"));
    REQUIRE(result.presets.has_value());
    REQUIRE(result.presets->size() == 1);
    CHECK((*result.presets)[0].bank == 0);
    CHECK((*result.presets)[0].program == 1);
    CHECK((*result.presets)[0].name == "Layered Preset");
}

TEST_CASE("lowerSf2Preset uses the phdr preset name as the model name", "[sf2]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf2"), 0, 0);
    REQUIRE(result.model.has_value());
    CHECK(result.model->name == "Basic Preset");
    REQUIRE(result.model->regions.size() == 1);
    CHECK(result.model->regions[0].sampleFile.rfind("sf2://", 0) == 0);
    CHECK(result.model->regions[0].sampleFile.find("#0") != std::string::npos);
}

TEST_CASE("volume envelope timecents and sustain centibels convert to model units", "[sf2]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf2"), 0, 0);
    REQUIRE(result.model.has_value());
    const auto& env = result.model->regions[0].amplitudeEnvelope;
    // attackVolEnv = -7973 tc -> 2^(-7973/1200) ~ 0.01 s
    CHECK(std::abs(env.attackSeconds - 0.01f) < 0.0005f);
    // holdVolEnv = -12000 sentinel -> 0
    CHECK(env.holdSeconds == 0.0f);
    // decayVolEnv = -3986 tc -> ~0.1 s
    CHECK(std::abs(env.decaySeconds - 0.1f) < 0.005f);
    // sustainVolEnv = 200 cB attenuation -> 10^(-200/200) = 0.1
    CHECK(std::abs(env.sustainLevel - 0.1f) < 0.001f);
    // releaseVolEnv = -1200 tc -> 0.5 s
    CHECK(std::abs(env.releaseSeconds - 0.5f) < 0.005f);
}

TEST_CASE("loop points lower relative to the sample start in frames", "[sf2]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf2"), 0, 0);
    REQUIRE(result.model.has_value());
    const auto& r = result.model->regions[0];
    CHECK(r.positionUnit == SamplePositionUnit::Frames);
    CHECK(r.loopEnabled);
    CHECK(r.loopStart == 64.0);
    CHECK(r.loopEnd == 192.0);
    CHECK(r.tuningCents == 25.0f); // fineTune generator
}

TEST_CASE("preset zone generators act as additive offsets and ranges intersect", "[sf2]")
{
    const auto result = lowerSf2Preset(fixturePath("layers.sf2"), 0, 1);
    REQUIRE(result.model.has_value());
    // Fixture: 3 instrument zones; preset zone narrows keys to [40, 80].
    REQUIRE(result.model->regions.size() == 3);
    for (const auto& r : result.model->regions) {
        CHECK(r.loKey >= 40);
        CHECK(r.hiKey <= 80);
        // instrument global attenuation 50 cB + preset offset 25 cB = 75 cB
        // with the FluidSynth 0.4x emulation factor: -0.4 * 75 / 10 = -3 dB
        CHECK(std::abs(r.gainDb - (-3.0f)) < 0.0001f);
        // preset global pan 100 (0.1% units) -> +0.2
        CHECK(std::abs(r.pan - 0.2f) < 0.0001f);
        // preset coarseTune offset 2 semitones = +200 cents
        CHECK(r.tuningCents == 200.0f);
    }
    // Velocity layers preserved from the instrument zones.
    CHECK(result.model->regions[0].hiVelocity == 63);
    CHECK(result.model->regions[1].loVelocity == 64);
    // Third zone: overridingRootKey wins over shdr originalPitch.
    CHECK(result.model->regions[2].rootKey == 72);
}

TEST_CASE("linked stereo pair lowers as two hard-panned regions", "[sf2]")
{
    const auto result = lowerSf2Preset(fixturePath("stereo.sf2"), 0, 2);
    REQUIRE(result.model.has_value());
    REQUIRE(result.model->regions.size() == 2);
    CHECK(result.model->regions[0].pan == -1.0f); // sampleType 4 = left
    CHECK(result.model->regions[1].pan == 1.0f);  // sampleType 2 = right
}

TEST_CASE("byte-span and file entry points lower identically", "[sf2]")
{
    const auto bytes = readBytes(fixturePath("basic.sf2"));
    const auto fromBytes = lowerSf2Bytes(bytes.data(), bytes.size(), 0, 0, "virtual.sf2");
    const auto fromFile = lowerSf2Preset(fixturePath("basic.sf2"), 0, 0);
    REQUIRE(fromBytes.model.has_value());
    REQUIRE(fromFile.model.has_value());
    REQUIRE(fromBytes.model->regions.size() == fromFile.model->regions.size());
    CHECK(fromBytes.model->regions[0].loopStart == fromFile.model->regions[0].loopStart);
    // Only the container path inside the URI differs.
    CHECK(fromBytes.model->regions[0].sampleFile == "sf2://virtual.sf2#0");
}

TEST_CASE("preset not found is a structured error, model suppressed", "[sf2]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf2"), 5, 99);
    CHECK_FALSE(result.model.has_value());
    CHECK(hasError(result.diagnostics, "sf2.preset_not_found"));
}

TEST_CASE("garbage and truncated input produce structured diagnostics, never crash", "[sf2]")
{
    SECTION("not a RIFF file")
    {
        const std::uint8_t garbage[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
        const auto result = lowerSf2Bytes(garbage, sizeof(garbage), 0, 0);
        CHECK_FALSE(result.model.has_value());
        CHECK(hasError(result.diagnostics, "sf2.not_soundfont"));
    }
    SECTION("null/empty input")
    {
        const auto result = lowerSf2Bytes(nullptr, 0, 0, 0);
        CHECK_FALSE(result.model.has_value());
        CHECK(hasError(result.diagnostics, "sf2.not_soundfont"));
    }
    SECTION("RIFF sfbk with no chunks")
    {
        const std::uint8_t empty[] = { 'R', 'I', 'F', 'F', 4, 0, 0, 0, 's', 'f', 'b', 'k' };
        const auto result = lowerSf2Bytes(empty, sizeof(empty), 0, 0);
        CHECK_FALSE(result.model.has_value());
        CHECK(hasError(result.diagnostics, "sf2.chunk_missing"));
    }
    SECTION("truncated mid-pdta")
    {
        auto bytes = readBytes(fixturePath("basic.sf2"));
        bytes.resize(bytes.size() * 2 / 3);
        // Fix the RIFF size so the walk sees the truncation as a chunk issue.
        const auto result = lowerSf2Bytes(bytes.data(), bytes.size(), 0, 0);
        CHECK_FALSE(result.model.has_value());
        const bool structural = hasError(result.diagnostics, "sf2.chunk_truncated")
            || hasError(result.diagnostics, "sf2.chunk_missing")
            || hasError(result.diagnostics, "sf2.hydra_chunk_missing");
        CHECK(structural);
    }
}

TEST_CASE("hydra chunk size not a record multiple is a mandated validation error", "[sf2]")
{
    auto bytes = readBytes(fixturePath("basic.sf2"));
    // Find the phdr sub-chunk and corrupt its size field by +1.
    const std::uint8_t needle[4] = { 'p', 'h', 'd', 'r' };
    auto it = std::search(bytes.begin(), bytes.end(), std::begin(needle), std::end(needle));
    REQUIRE(it != bytes.end());
    const std::size_t sizeOffset = static_cast<std::size_t>(it - bytes.begin()) + 4;
    bytes[sizeOffset] += 1;
    // Growing phdr by one byte makes it a non-multiple of 38 — but also shifts
    // subsequent parsing; either sf2.chunk_size_invalid or a truncation error
    // is acceptable, crash is not.
    const auto result = lowerSf2Bytes(bytes.data(), bytes.size(), 0, 0);
    CHECK_FALSE(result.model.has_value());
    const bool structural = hasError(result.diagnostics, "sf2.chunk_size_invalid")
        || hasError(result.diagnostics, "sf2.chunk_truncated")
        || hasError(result.diagnostics, "sf2.hydra_chunk_missing");
    CHECK(structural);
}

TEST_CASE("modulator content is tracked as unsupported, never silent", "[sf2]")
{
    // Splice one non-terminal pmod record (10 bytes) in front of the terminal:
    // find the pmod chunk, grow it by one record.
    auto bytes = readBytes(fixturePath("basic.sf2"));
    const std::uint8_t needle[4] = { 'p', 'm', 'o', 'd' };
    auto it = std::search(bytes.begin(), bytes.end(), std::begin(needle), std::end(needle));
    REQUIRE(it != bytes.end());
    const std::size_t chunkPos = static_cast<std::size_t>(it - bytes.begin());
    // Patch sizes: pmod chunk 10 -> 20, and every enclosing size += 10.
    auto patchU32 = [&](std::size_t at, std::int32_t delta) {
        std::uint32_t v = static_cast<std::uint32_t>(bytes[at]) | (bytes[at + 1] << 8)
            | (bytes[at + 2] << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
        v = static_cast<std::uint32_t>(static_cast<std::int64_t>(v) + delta);
        bytes[at] = v & 0xFF;
        bytes[at + 1] = (v >> 8) & 0xFF;
        bytes[at + 2] = (v >> 16) & 0xFF;
        bytes[at + 3] = (v >> 24) & 0xFF;
    };
    patchU32(chunkPos + 4, 10);
    // Insert a zeroed (but non-terminal-position) modulator record.
    bytes.insert(it + 8, 10, std::uint8_t { 0 });
    patchU32(4, 10); // RIFF size
    // pdta LIST size: locate it.
    const std::uint8_t pdta[4] = { 'p', 'd', 't', 'a' };
    auto pd = std::search(bytes.begin(), bytes.end(), std::begin(pdta), std::end(pdta));
    REQUIRE(pd != bytes.end());
    patchU32(static_cast<std::size_t>(pd - bytes.begin()) - 4, 10);

    const auto result = lowerSf2Bytes(bytes.data(), bytes.size(), 0, 0);
    REQUIRE(result.model.has_value());
    CHECK(hasWarning(result.diagnostics, "sf2.modulator_unsupported"));
}

TEST_CASE("lowered SF2 models pass canonical validation", "[sf2]")
{
    for (const char* name : { "basic.sf2", "layers.sf2", "stereo.sf2", "unsupported.sf2" }) {
        const auto list = listSf2Presets(fixturePath(name));
        REQUIRE(list.presets.has_value());
        for (const auto& p : *list.presets) {
            const auto result = lowerSf2Preset(fixturePath(name), p.bank, p.program);
            REQUIRE(result.model.has_value());
            CHECK(validate(*result.model).empty());
        }
    }
}

TEST_CASE("default modulators instantiate on every region", "[sf2][mod]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf2"), 0, 0);
    REQUIRE(result.model.has_value());
    const auto& mods = result.model->regions[0].modMatrix;

    // Expressible defaults: vel->Gain, vel->FilterCutoff, CC7->Gain,
    // CC10->Pan, CC11->Gain, CC91->Reverb, CC93->Chorus. The two
    // vibrato-LFO defaults are tracked as unsupported.
    REQUIRE(mods.size() == 7);

    const auto& velGain = mods[0]; // §8.4.1
    CHECK(velGain.source.kind == ModSourceKind::Velocity);
    CHECK(velGain.source.curve == ModCurveType::Concave);
    CHECK(velGain.source.maxToMin);
    CHECK_FALSE(velGain.source.bipolar);
    CHECK(velGain.target == ModTarget::Gain);
    // 960 cB attenuation, full scale (no 0.4x factor on modulators): -96 dB
    CHECK(std::abs(velGain.depth - (-96.0f)) < 0.001f);

    const auto& pan = mods[3]; // §8.4.6 CC10 -> pan
    CHECK(pan.source.kind == ModSourceKind::Cc);
    CHECK(pan.source.ccNumber == 10);
    CHECK(pan.source.bipolar);
    CHECK(pan.target == ModTarget::Pan);
    CHECK(pan.depth == 1.0f); // 500 / 500

    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const Diagnostic& d) { return d.code == "sf2.modulator_unsupported"; }));
}

TEST_CASE("instrument modulators supersede defaults; preset modulators add", "[sf2][mod]")
{
    const auto result = lowerSf2Preset(fixturePath("mods.sf2"), 0, 4);
    REQUIRE(result.model.has_value());
    const auto& mods = result.model->regions[0].modMatrix;

    // The imod (vel->atten, amount 480) has the same identity as default
    // §8.4.1, so it REPLACES it: depth -480/10 = -48 dB.
    const auto velGain = std::find_if(mods.begin(), mods.end(), [](const ModMatrixEntry& m) {
        return m.source.kind == ModSourceKind::Velocity && m.target == ModTarget::Gain;
    });
    REQUIRE(velGain != mods.end());
    CHECK(std::abs(velGain->depth - (-48.0f)) < 0.001f);

    // The pmod (CC2 -> fineTune, amount 100) is a new connection.
    const auto cc2 = std::find_if(mods.begin(), mods.end(), [](const ModMatrixEntry& m) {
        return m.source.kind == ModSourceKind::Cc && m.source.ccNumber == 2;
    });
    REQUIRE(cc2 != mods.end());
    CHECK(cc2->target == ModTarget::Pitch);
    CHECK(cc2->depth == 100.0f);
}

TEST_CASE("modulator-carrying models pass canonical validation", "[sf2][mod]")
{
    for (const char* name : { "basic.sf2", "mods.sf2", "oracle.sf2" }) {
        const auto list = listSf2Presets(fixturePath(name));
        REQUIRE(list.presets.has_value());
        for (const auto& p : *list.presets) {
            const auto result = lowerSf2Preset(fixturePath(name), p.bank, p.program);
            REQUIRE(result.model.has_value());
            CHECK(validate(*result.model).empty());
        }
    }
}

TEST_CASE("session enumerates presets sorted by bank then program", "[sf2][session]")
{
    const auto opened = openSf2Session(fixturePath("multibank.sf2"));
    REQUIRE(opened.session != nullptr);
    const auto& presets = opened.session->presets();
    // On-disk order is deliberately scrambled; enumeration must sort
    // bank asc, program asc (incl. the bank-128 percussion convention).
    REQUIRE(presets.size() == 3);
    CHECK(presets[0].bank == 0);
    CHECK(presets[0].program == 0);
    CHECK(presets[0].name == "Bright Lead");
    CHECK(presets[1].bank == 0);
    CHECK(presets[1].program == 1);
    CHECK(presets[1].name == "Soft Lead");
    CHECK(presets[2].bank == 128);
    CHECK(presets[2].program == 0);
    CHECK(presets[2].name == "Standard Kit");
}

TEST_CASE("session lowers any preset without re-reading the container", "[sf2][session]")
{
    const auto opened = openSf2Session(fixturePath("multibank.sf2"));
    REQUIRE(opened.session != nullptr);

    for (std::size_t i = 0; i < opened.session->presets().size(); ++i) {
        const auto lowered = opened.session->lowerPreset(i);
        REQUIRE(lowered.model.has_value());
        CHECK(lowered.model->name == opened.session->presets()[i].name);
        CHECK(validate(*lowered.model).empty());
    }

    const auto outOfRange = opened.session->lowerPreset(99);
    CHECK_FALSE(outOfRange.model.has_value());
    CHECK(hasError(outOfRange.diagnostics, "sf2.preset_not_found"));
}

TEST_CASE("presets sharing a sample deduplicate through the pool", "[sf2][session][pool]")
{
    // Bright Lead (idx 0) and Soft Lead (idx 1) reference the same embedded
    // sample; the refcounted pool must key both to ONE handle — a preset
    // switch performs zero new sample-file reads for already-pooled samples.
    const auto opened = openSf2Session(fixturePath("multibank.sf2"));
    REQUIRE(opened.session != nullptr);
    const auto a = opened.session->lowerPreset(0);
    const auto b = opened.session->lowerPreset(1);
    REQUIRE(a.model.has_value());
    REQUIRE(b.model.has_value());
    REQUIRE(a.model->regions[0].sampleFile == b.model->regions[0].sampleFile);

    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    const auto h1 = pool->acquire(a.model->regions[0].sampleFile, &diags);
    const auto h2 = pool->acquire(b.model->regions[0].sampleFile, &diags);
    REQUIRE(h1 != kInvalidSampleHandle);
    CHECK(h1 == h2); // refcount bump, not a second load
    pool->release(h2);
    pool->release(h1);
}

TEST_CASE("pool acquires embedded SF2 samples through the sf2:// reference", "[sf2][pool]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf2"), 0, 0);
    REQUIRE(result.model.has_value());
    const std::string& uri = result.model->regions[0].sampleFile;

    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    const SampleHandle handle = pool->acquire(uri, &diags);
    for (const auto& d : diags)
        INFO(d.code << ": " << d.message);
    REQUIRE(handle != kInvalidSampleHandle);

    SampleInfo info;
    REQUIRE(pool->info(handle, info));
    CHECK(info.numChannels == 1);
    CHECK(info.numFrames == 256); // fixture triangle sample length
    CHECK(info.sampleRate == 22050.0);
    CHECK(info.residentFrames == info.numFrames);

    const float* ch = pool->residentChannel(handle, 0);
    REQUIRE(ch != nullptr);
    // Triangle wave starts at -amp (frame 0) and is non-silent.
    float peak = 0.0f;
    for (std::uint64_t i = 0; i < info.numFrames; ++i)
        peak = std::max(peak, std::abs(ch[i]));
    CHECK(peak > 0.3f);

    pool->release(handle);
}

TEST_CASE("pool decodes SF3 Vorbis samples to PCM at acquire time", "[sf2][sf3][pool]")
{
    const auto result = lowerSf2Preset(fixturePath("basic.sf3"), 0, 0);
    REQUIRE(result.model.has_value());
    const std::string& uri = result.model->regions[0].sampleFile;

    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    const SampleHandle handle = pool->acquire(uri, &diags);
    for (const auto& d : diags)
        INFO(d.code << ": " << d.message);
    REQUIRE(handle != kInvalidSampleHandle);

    // After acquire, an SF3-sourced sample is indistinguishable from SF2:
    // decoded PCM, correct rate, everything resident (AD-2).
    SampleInfo info;
    REQUIRE(pool->info(handle, info));
    CHECK(info.numChannels >= 1);
    CHECK(info.sampleRate == 22050.0);
    CHECK(info.numFrames > 0);
    CHECK(info.residentFrames == info.numFrames);

    const float* ch = pool->residentChannel(handle, 0);
    REQUIRE(ch != nullptr);
    float peak = 0.0f;
    for (std::uint64_t i = 0; i < info.numFrames; ++i)
        peak = std::max(peak, std::abs(ch[i]));
    CHECK(peak > 0.2f); // decoded triangle is non-silent

    pool->release(handle);
}

TEST_CASE("pool rejects malformed sf2 references with structured diagnostics", "[sf2][pool]")
{
    auto pool = createAllRamSamplePool();

    SECTION("bad index")
    {
        std::vector<Diagnostic> diags;
        const auto handle =
            pool->acquire("sf2://" + fixturePath("basic.sf2") + "#99", &diags);
        CHECK(handle == kInvalidSampleHandle);
        CHECK(hasError(diags, "pool.sf2_sample_index_out_of_range"));
    }
    SECTION("malformed URI")
    {
        std::vector<Diagnostic> diags;
        const auto handle = pool->acquire("sf2://nofragment", &diags);
        CHECK(handle == kInvalidSampleHandle);
        CHECK(hasError(diags, "pool.sf2_uri_invalid"));
    }
    SECTION("missing container")
    {
        std::vector<Diagnostic> diags;
        const auto handle = pool->acquire("sf2://does_not_exist.sf2#0", &diags);
        CHECK(handle == kInvalidSampleHandle);
        CHECK(hasError(diags, "pool.sample_open_failed"));
    }
}
