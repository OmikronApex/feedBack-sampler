// Unit tests for the AD-2 sample-pool contract and its v0 all-RAM
// implementation (Story 1.4 Task 1).

#include "fbsampler/pool.h"

#include "seed_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace fbsampler;

namespace {
std::string seedSamplePath(const char* name)
{
    return fbsampler::testutil::seedInstrumentRoot() + "/samples/" + name;
}
std::string formatFixturePath(const char* name)
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/formats/" + name;
}
} // namespace

TEST_CASE("all-RAM pool loads a wav fully resident as float32", "[pool]")
{
    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    SampleHandle h = pool->acquire(seedSamplePath("soft.wav"), &diags);
    REQUIRE(h != kInvalidSampleHandle);
    REQUIRE(diags.empty());

    SampleInfo info;
    REQUIRE(pool->info(h, info));
    CHECK(info.numChannels == 1);
    CHECK(info.numFrames == 22050);
    CHECK(info.sampleRate == 44100.0);
    // v0 all-RAM: everything resident — but reported through the contract's
    // resident-head field, never assumed by callers.
    CHECK(info.residentFrames == info.numFrames);

    const float* data = pool->residentChannel(h, 0);
    REQUIRE(data != nullptr);
    // 440 Hz sine at 0.3 amplitude: peak within (0.2, 0.35].
    float peak = 0.0f;
    for (std::uint64_t i = 0; i < info.residentFrames; ++i)
        peak = std::max(peak, std::abs(data[i]));
    CHECK(peak > 0.2f);
    CHECK(peak <= 0.35f);

    CHECK(pool->residentChannel(h, 1) == nullptr); // mono file
    pool->release(h);
}

TEST_CASE("acquire is refcounted and release invalidates at zero", "[pool]")
{
    auto pool = createAllRamSamplePool();
    SampleHandle a = pool->acquire(seedSamplePath("soft.wav"), nullptr);
    SampleHandle b = pool->acquire(seedSamplePath("soft.wav"), nullptr);
    REQUIRE(a != kInvalidSampleHandle);
    CHECK(a == b); // same entry, bumped refcount

    pool->release(a);
    SampleInfo info;
    CHECK(pool->info(b, info)); // still pinned by second reference

    pool->release(b);
    CHECK_FALSE(pool->info(b, info)); // refcount zero: handle dead
    CHECK(pool->residentChannel(b, 0) == nullptr);
}

TEST_CASE("acquire of a missing file fails with a diagnostic", "[pool]")
{
    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    SampleHandle h = pool->acquire(seedSamplePath("does_not_exist.wav"), &diags);
    CHECK(h == kInvalidSampleHandle);
    REQUIRE_FALSE(diags.empty());
    CHECK(diags[0].code == "pool.sample_open_failed");
}

TEST_CASE("invalid handles are rejected safely", "[pool]")
{
    auto pool = createAllRamSamplePool();
    SampleInfo info;
    CHECK_FALSE(pool->info(kInvalidSampleHandle, info));
    CHECK_FALSE(pool->info(999999, info));
    CHECK(pool->residentChannel(12345, 0) == nullptr);
}

TEST_CASE("acquire reuses released slots instead of growing forever", "[pool]")
{
    // Contract under test (SamplePool): repeated acquire/release of distinct
    // paths must not monotonically consume the fixed-size slot table, or a
    // long-running session would eventually hit pool.capacity_exceeded. Assert
    // the contract itself -- no capacity error across many more cycles than
    // the table holds -- rather than pinning to which exact slot gets reused.
    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diagnostics;
    for (int i = 0; i < 5000; ++i) {
        const char* path = (i % 2 == 0) ? "soft.wav" : "loud.wav";
        SampleHandle h = pool->acquire(seedSamplePath(path), &diagnostics);
        REQUIRE(h != kInvalidSampleHandle);
        pool->release(h);
    }
    CHECK(diagnostics.empty());
}

TEST_CASE("all-RAM pool decodes 24-bit PCM stereo", "[pool]")
{
    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    SampleHandle h = pool->acquire(formatFixturePath("pcm24_stereo.wav"), &diags);
    REQUIRE(h != kInvalidSampleHandle);
    REQUIRE(diags.empty());

    SampleInfo info;
    REQUIRE(pool->info(h, info));
    CHECK(info.numChannels == 2);
    CHECK(info.sampleRate == 44100.0);

    const float* left = pool->residentChannel(h, 0);
    const float* right = pool->residentChannel(h, 1);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    float peak = 0.0f;
    for (std::uint64_t i = 0; i < info.residentFrames; ++i)
        peak = std::max(peak, std::abs(left[i]));
    CHECK(peak > 0.2f);
    CHECK(peak <= 0.31f);
    pool->release(h);
}

TEST_CASE("all-RAM pool decodes WAVE_FORMAT_EXTENSIBLE PCM with >2 channels", "[pool]")
{
    auto pool = createAllRamSamplePool();
    std::vector<Diagnostic> diags;
    SampleHandle h = pool->acquire(formatFixturePath("extensible_pcm16_quad.wav"), &diags);
    REQUIRE(h != kInvalidSampleHandle);
    REQUIRE(diags.empty());

    SampleInfo info;
    REQUIRE(pool->info(h, info));
    CHECK(info.numChannels == 4);
    CHECK(info.sampleRate == 44100.0);

    for (std::uint32_t ch = 0; ch < info.numChannels; ++ch)
        REQUIRE(pool->residentChannel(h, ch) != nullptr);
    CHECK(pool->residentChannel(h, 4) == nullptr); // out of range
    pool->release(h);
}
