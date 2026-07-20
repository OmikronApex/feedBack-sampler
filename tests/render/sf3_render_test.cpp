// SF3 render-identity tests (Story 2.3 AC1): the sf2/sf3 fixture pair renders
// through the offline harness with identical MIDI; the sf3 output must match
// the sf2 output within thresholds. Vorbis is lossy, so bit-equality is
// impossible — the per-pair threshold (documented below) is wider than the
// corpus defaults with recorded rationale.

#include <catch2/catch_test_macros.hpp>

#include "fbsampler/pool.h"
#include "fbsampler/sf2_frontend.h"
#include "midi_file.h"
#include "render_harness.h"

#include <cmath>
#include <string>
#include <vector>

using namespace fbsampler;
using namespace fbsampler::testutil;

namespace {

constexpr double kRate = 44100.0;
constexpr std::uint64_t kTotalFrames = 2 * 44100;

std::string fixture(const std::string& name)
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/sf2/" + name;
}

RenderResult renderContainer(const std::string& container, bool markRt = false)
{
    const auto lowered = lowerSf2Preset(fixture(container), 0, 0);
    for (const auto& d : lowered.diagnostics) {
        INFO(d.code << ": " << d.message);
        CHECK(d.severity != Severity::Error);
    }
    REQUIRE(lowered.model.has_value());

    // Sustained note over the loop + release inside the window.
    std::vector<TimelineEvent> timeline;
    TimelineEvent on;
    on.frame = 0;
    on.type = EngineEvent::Type::NoteOn;
    on.note = 69;
    on.velocity = 100;
    timeline.push_back(on);
    TimelineEvent off = on;
    off.frame = static_cast<std::uint64_t>(1.5 * kRate);
    off.type = EngineEvent::Type::NoteOff;
    timeline.push_back(off);

    RenderSettings settings;
    settings.sampleRate = kRate;
    settings.blockFrames = 256;
    settings.totalFrames = kTotalFrames;
    settings.markRtSections = markRt;

    auto pool = createAllRamSamplePool();
    auto result = renderOffline(*lowered.model, std::move(pool), "", timeline, settings);
    for (const auto& d : result.diagnostics) {
        INFO(d.code << ": " << d.message);
        CHECK(d.severity != Severity::Error);
    }
    REQUIRE(result.ok);
    return result;
}

} // namespace

TEST_CASE("SF3 renders match the SF2 sibling within pair thresholds", "[sf2][sf3][render]")
{
    const auto sf2 = renderContainer("oracle.sf2");
    const auto sf3 = renderContainer("oracle.sf3");

    // Per-pair thresholds (vs corpus defaults peak 1e-2 / windowed RMS 3e-3):
    // windowed RMS diff < 1e-2, peak sample diff < 6e-2. Rationale: Vorbis
    // q6 is near-transparent for the band-limited triangle fixture but not
    // bit-exact; residual is codec noise plus up-to-one-frame decode
    // alignment, not a lowering or bind difference.
    for (int ch = 0; ch < 2; ++ch) {
        const auto& a = sf2.channels[static_cast<std::size_t>(ch)];
        const auto& b = sf3.channels[static_cast<std::size_t>(ch)];
        REQUIRE(a.size() == b.size());

        float peakDiff = 0.0f;
        const std::size_t window = 4410; // 100 ms
        for (std::size_t begin = 0; begin + window <= a.size(); begin += window) {
            double sum = 0.0;
            for (std::size_t i = begin; i < begin + window; ++i) {
                const float d = a[i] - b[i];
                peakDiff = std::max(peakDiff, std::abs(d));
                sum += static_cast<double>(d) * d;
            }
            const double rms = std::sqrt(sum / window);
            INFO("channel " << ch << " window at frame " << begin << ": rms diff " << rms);
            CHECK(rms < 1e-2);
        }
        INFO("channel " << ch << " peak diff " << peakDiff);
        CHECK(peakDiff < 6e-2f);
    }
}

TEST_CASE("SF3 render stays RT-clean (decode never touches the audio thread)",
          "[sf2][sf3][render][rt]")
{
    // markRtSections wraps every process() call in an rtcheck section; a
    // Vorbis decode (allocation, file I/O) leaking onto the audio thread
    // fails the render. Decode happens exclusively in pool acquire() at
    // load (AD-2).
    const auto result = renderContainer("oracle.sf3", /*markRt=*/true);
    REQUIRE(result.ok);
}
