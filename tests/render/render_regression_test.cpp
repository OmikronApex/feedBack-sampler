// Story 1.4 AC 1: a lowered instrument rendered through the unified engine +
// all-RAM pool matches the checked-in seed reference render.
//
// Comparison uses thresholds, not byte equality: cross-compiler float drift
// (SIMD paths, FMA contraction, libm differences) makes exact-byte output
// unrealistic. Thresholds (peak sample diff and whole-render RMS diff) are
// deliberately tight enough to catch any behavioral change — a missing
// velocity layer, broken loop, or wrong envelope moves peak diff by orders of
// magnitude — while absorbing last-ulp drift. This seeds the NFR-5 threshold
// mechanism that Story 1.6 formalizes for the corpus.
//
// Regenerating the reference (after an *intentional* engine change):
//   FBSAMPLER_UPDATE_RENDER_FIXTURE=1 ctest -R render_regression
// then commit the updated fixtures/reference/seed_render.wav.

#include "../../core/pool/wav_reader.h"
#include "render_harness.h"
#include "seed_fixture.h"
#include "wav_io.h"

#include "fbsampler/sfz_frontend.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

namespace {

constexpr double kPeakDiffThreshold = 1e-2; // absolute, full-scale float
constexpr double kRmsDiffThreshold = 1e-3;  // absolute, over the whole render

} // namespace

TEST_CASE("seed instrument render matches the checked-in reference", "[render]")
{
    using namespace fbsampler;
    using namespace fbsampler::testutil;

    // Lower the seed instrument via the Story 1.3 frontend: the engine
    // consumes validated model instances only.
    LowerResult lowered = lowerSfzFile(seedInstrumentSfzPath());
    REQUIRE(lowered.model.has_value());

    auto pool = std::shared_ptr<SamplePool>(createAllRamSamplePool());
    RenderResult render = renderOffline(*lowered.model, pool, seedInstrumentRoot(),
                                        seedTimeline(), seedRenderSettings());
    for (const Diagnostic& d : render.diagnostics)
        INFO(d.code << ": " << d.message);
    REQUIRE(render.ok);
    REQUIRE(render.channels.size() == 2);

    // The render must actually contain sound (guards against a silently
    // dead engine passing a silent-vs-silent comparison).
    double energy = 0.0;
    for (const auto& ch : render.channels)
        for (float v : ch)
            energy += static_cast<double>(v) * v;
    REQUIRE(energy > 1.0);

    if (std::getenv("FBSAMPLER_UPDATE_RENDER_FIXTURE")) {
        REQUIRE(fbsampler::testutil::writeFloatWav(
            seedReferenceWavPath(), render.channels, seedRenderSettings().sampleRate));
        SUCCEED("reference render fixture updated");
        return;
    }

    fbsampler::detail::DecodedWav reference;
    std::vector<Diagnostic> refDiags;
    REQUIRE(fbsampler::detail::readWavFile(seedReferenceWavPath(), reference, &refDiags));
    REQUIRE(reference.numChannels == 2);
    REQUIRE(reference.numFrames == seedRenderSettings().totalFrames);

    double peakDiff = 0.0;
    double sumSquaredDiff = 0.0;
    for (std::uint32_t ch = 0; ch < 2; ++ch) {
        for (std::size_t i = 0; i < reference.channels[ch].size(); ++i) {
            const double diff = std::abs(static_cast<double>(render.channels[ch][i])
                                         - static_cast<double>(reference.channels[ch][i]));
            peakDiff = std::max(peakDiff, diff);
            sumSquaredDiff += diff * diff;
        }
    }
    const double rmsDiff = std::sqrt(
        sumSquaredDiff / (2.0 * static_cast<double>(reference.numFrames)));

    INFO("peak diff " << peakDiff << " (threshold " << kPeakDiffThreshold << ")");
    INFO("rms diff " << rmsDiff << " (threshold " << kRmsDiffThreshold << ")");
    CHECK(peakDiff <= kPeakDiffThreshold);
    CHECK(rmsDiff <= kRmsDiffThreshold);
}
