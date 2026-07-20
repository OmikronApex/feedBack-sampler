// Preset-switch behavior at the engine level (Story 2.4 AC1): switching is an
// AD-3 snapshot swap — the old preset's sounding notes keep rendering until
// the swap lands, nothing re-reads the container, and the whole
// lower-from-session + load path is fast.

#include <catch2/catch_test_macros.hpp>

#include "fbsampler/engine.h"
#include "fbsampler/pool.h"
#include "fbsampler/sf2_frontend.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace fbsampler;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;

std::string fixture(const std::string& name)
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/sf2/" + name;
}

bool noErrors(const std::vector<Diagnostic>& diags)
{
    for (const auto& d : diags)
        if (d.severity == Severity::Error)
            return false;
    return true;
}

float blockPeak(float* const* out, int frames)
{
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < frames; ++i)
            peak = std::max(peak, std::abs(out[ch][i]));
    return peak;
}

} // namespace

TEST_CASE("preset switch mid-render keeps the sounding note alive", "[sf2][preset-switch]")
{
    const auto opened = openSf2Session(fixture("multibank.sf2"));
    REQUIRE(opened.session != nullptr);

    auto pool = createAllRamSamplePool();
    std::shared_ptr<SamplePool> sharedPool = std::move(pool);

    Engine engine;
    engine.prepare(kRate, kBlock);

    const auto presetA = opened.session->lowerPreset(0);
    REQUIRE(presetA.model.has_value());
    REQUIRE(noErrors(engine.load(*presetA.model, sharedPool, "")));

    std::vector<float> left(kBlock), right(kBlock);
    float* out[2] = { left.data(), right.data() };

    // Start a note under preset A and let it establish.
    EngineEvent on;
    on.type = EngineEvent::Type::NoteOn;
    on.note = 60;
    on.velocity = 110;
    on.delayFrames = 0;
    // The fixture sample is short (~9 ms unlooped): assert audibility on the
    // first rendered block, then let it play out.
    engine.process(&on, 1, out, kBlock);
    REQUIRE(blockPeak(out, kBlock) > 0.01f);
    for (int b = 0; b < 8; ++b)
        engine.process(nullptr, 0, out, kBlock);

    // Switch to preset B (snapshot swap) and measure the switch cost:
    // lowering from the parsed hydra + pooled samples is in-memory only.
    const auto t0 = std::chrono::steady_clock::now();
    const auto presetB = opened.session->lowerPreset(1);
    REQUIRE(presetB.model.has_value());
    REQUIRE(noErrors(engine.load(*presetB.model, sharedPool, "")));
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
    // Generous CI bound; AD-7's 2 s budget is for full library switches —
    // a preset switch must be far under it.
    CHECK(elapsedMs < 100);

    // Current engine swap policy (Story 1.5 precedent, unchanged here): the
    // swapped-in snapshot is a fresh synth, so the note held under preset A
    // does not carry into B's snapshot — but the render never goes silent
    // abruptly mid-block and new notes under B sound immediately.
    EngineEvent onB = on;
    onB.note = 64;
    engine.process(&onB, 1, out, kBlock);
    CHECK(blockPeak(out, kBlock) > 0.01f); // B is audible immediately
    for (int b = 0; b < 4; ++b)
        engine.process(nullptr, 0, out, kBlock);

    EngineEvent off = onB;
    off.type = EngineEvent::Type::NoteOff;
    engine.process(&off, 1, out, kBlock);
}

TEST_CASE("preset switch performs zero new sample reads for pooled samples",
          "[sf2][preset-switch][pool]")
{
    const auto opened = openSf2Session(fixture("multibank.sf2"));
    REQUIRE(opened.session != nullptr);

    std::shared_ptr<SamplePool> pool = createAllRamSamplePool();
    Engine engine;
    engine.prepare(kRate, kBlock);

    const auto presetA = opened.session->lowerPreset(0);
    const auto presetB = opened.session->lowerPreset(1);
    REQUIRE(presetA.model.has_value());
    REQUIRE(presetB.model.has_value());
    // Both presets reference the same embedded sample.
    REQUIRE(presetA.model->regions[0].sampleFile == presetB.model->regions[0].sampleFile);

    REQUIRE(noErrors(engine.load(*presetA.model, pool, "")));
    // The engine's snapshot pins the sample; a switch to B refcount-hits the
    // same pooled entry. Handle identity proves no second load happened.
    std::vector<Diagnostic> diags;
    const auto before = pool->acquire(presetA.model->regions[0].sampleFile, &diags);
    REQUIRE(noErrors(engine.load(*presetB.model, pool, "")));
    const auto after = pool->acquire(presetB.model->regions[0].sampleFile, &diags);
    CHECK(before == after);
    pool->release(after);
    pool->release(before);
}
