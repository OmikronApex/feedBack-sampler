// Story 1.4 AC 2: zero allocations, locks, or file I/O detected on the audio
// thread while the render callback executes over the seed fixture.
//
// Detector coverage:
//  - Allocation: global operator new/delete overrides in this test binary
//    (rt_new_delete_hooks.cpp) report any allocation inside a marked RT
//    section. This catches C++ allocations from our glue *and* from sfizz.
//  - Locks: every engine/pool mutex is a detail::CheckedMutex, which reports
//    when locked inside an RT section.
//  - File I/O: the pool's loading path (the only fbsampler code that opens
//    files) reports when entered inside an RT section.
//
// One warm-up block runs before the detector engages: first-callback
// one-time lazy setup is allowed by the prepare-to-play contract; the
// steady-state callback must be clean.

#include "render_harness.h"
#include "seed_fixture.h"

#include "engine/checked_mutex.h"
#include "fbsampler/detail/rt_check.h"
#include "fbsampler/pool.h"
#include "fbsampler/sfz_frontend.h"
#include "pool/wav_reader.h"

#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <vector>

TEST_CASE("render callback is allocation-, lock- and file-I/O-free", "[rt-safety]")
{
    using namespace fbsampler;
    using namespace fbsampler::testutil;

    LowerResult lowered = lowerSfzFile(seedInstrumentSfzPath());
    REQUIRE(lowered.model.has_value());

    auto pool = std::shared_ptr<SamplePool>(createAllRamSamplePool());

    Engine engine;
    const RenderSettings settings = seedRenderSettings();
    engine.prepare(settings.sampleRate, settings.blockFrames);
    std::vector<Diagnostic> diags =
        engine.load(*lowered.model, pool, seedInstrumentRoot());
    for (const Diagnostic& d : diags)
        INFO(d.code << ": " << d.message);
    for (const Diagnostic& d : diags)
        REQUIRE(d.severity != Severity::Error);

    std::vector<float> left(static_cast<std::size_t>(settings.blockFrames));
    std::vector<float> right(static_cast<std::size_t>(settings.blockFrames));
    float* out[2] = {left.data(), right.data()};

    // Warm-up block (outside the detector).
    engine.process(nullptr, 0, out, static_cast<std::size_t>(settings.blockFrames));

    rtcheck::resetViolations();

    // Drive the full seed timeline through process() with every block inside
    // a marked RT section, so the detector observes the exact per-block work
    // AC 1 measures.
    std::vector<TimelineEvent> timeline = seedTimeline();
    std::size_t nextEvent = 0;
    std::vector<EngineEvent> blockEvents;
    for (std::uint64_t start = 0; start < settings.totalFrames;
         start += static_cast<std::uint64_t>(settings.blockFrames)) {
        const auto frames = static_cast<std::size_t>(settings.blockFrames);
        blockEvents.clear();
        while (nextEvent < timeline.size()
               && timeline[nextEvent].frame < start + frames) {
            EngineEvent e;
            e.type = timeline[nextEvent].type;
            e.delayFrames = static_cast<int>(timeline[nextEvent].frame - start);
            e.note = timeline[nextEvent].note;
            e.velocity = timeline[nextEvent].velocity;
            blockEvents.push_back(e);
            ++nextEvent;
        }
        {
            rtcheck::SectionGuard guard;
            engine.process(blockEvents.data(), blockEvents.size(), out, frames);
        }
    }

    if (rtcheck::violationCount() != 0)
        INFO("last violation: " << rtcheck::lastViolation());
    CHECK(rtcheck::violationCount() == 0);
}

TEST_CASE("detector actually fires on violations", "[rt-safety]")
{
    // Negative controls: prove the detector reports on each of the three
    // paths it claims to cover (allocation, locks, file I/O), so the zero
    // seen in the seed-timeline test above means "clean", not "disconnected".
    using namespace fbsampler;

    SECTION("allocation")
    {
        rtcheck::resetViolations();
        {
            rtcheck::SectionGuard guard;
            volatile int* p = new int(42); // allocation inside RT section
            delete p;
        }
        CHECK(rtcheck::violationCount() > 0);
        rtcheck::resetViolations();
    }

    SECTION("lock")
    {
        rtcheck::resetViolations();
        {
            detail::CheckedMutex m;
            rtcheck::SectionGuard guard;
            std::lock_guard<detail::CheckedMutex> lock(m); // lock inside RT section
        }
        CHECK(rtcheck::violationCount() > 0);
        rtcheck::resetViolations();
    }

    SECTION("file I/O")
    {
        rtcheck::resetViolations();
        {
            rtcheck::SectionGuard guard;
            detail::DecodedWav wav;
            // Path need not exist: readWavFile reports the RT violation
            // before attempting to open the file.
            detail::readWavFile("__nonexistent__.wav", wav, nullptr);
        }
        CHECK(rtcheck::violationCount() > 0);
        rtcheck::resetViolations();
    }
}
