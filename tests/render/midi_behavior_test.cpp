// Story 1.5 Task 4: MIDI behaviors through the engine binding — sustain
// (CC64), sostenuto (CC66), pitch bend (respecting the model bend range), and
// mapped CCs all reach sfizz through the model+engine path, never an SFZ-text
// backdoor (AD-1).

#include "render_harness.h"
#include "seed_fixture.h"

#include "fbsampler/sfz_frontend.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace fbsampler;
using namespace fbsampler::testutil;

namespace {

double energyBetween(const RenderResult& r, std::uint64_t fromFrame, std::uint64_t toFrame)
{
    double e = 0.0;
    for (const auto& ch : r.channels)
        for (std::uint64_t i = fromFrame; i < toFrame && i < ch.size(); ++i)
            e += static_cast<double>(ch[i]) * ch[i];
    return e;
}

InstrumentModel loweredSeedModel()
{
    LowerResult lowered = lowerSfzFile(seedInstrumentSfzPath());
    REQUIRE(lowered.model.has_value());
    return *lowered.model;
}

RenderResult renderSeed(const InstrumentModel& model,
                        const std::vector<TimelineEvent>& timeline,
                        std::uint64_t totalFrames = 144000)
{
    auto pool = std::shared_ptr<SamplePool>(createAllRamSamplePool());
    RenderSettings s;
    s.sampleRate = 48000.0;
    s.blockFrames = 256;
    s.totalFrames = totalFrames;
    RenderResult r = renderOffline(model, pool, seedInstrumentRoot(), timeline, s);
    REQUIRE(r.ok);
    return r;
}

} // namespace

TEST_CASE("sustain pedal (CC64) holds a note past its note-off", "[midi]")
{
    using T = EngineEvent::Type;
    const InstrumentModel model = loweredSeedModel();

    // Use the looped pad (key 36): a non-looped sample would end on its own
    // and mask the pedal. Released at 0.5 s, release tail 0.4 s: past 1.0 s
    // the unsustained note is silent while the sustained one keeps sounding.
    const std::vector<TimelineEvent> noPedal = {
        {0, T::NoteOn, 36, 100},
        {24000, T::NoteOff, 36, 0},
    };
    std::vector<TimelineEvent> withPedal = noPedal;
    withPedal.insert(withPedal.begin() + 1, {12000, T::ControlChange, 64, 127});

    const double tailNoPedal = energyBetween(renderSeed(model, noPedal), 48000, 144000);
    const double tailWithPedal = energyBetween(renderSeed(model, withPedal), 48000, 144000);

    REQUIRE(tailWithPedal > 1e-3);
    REQUIRE(tailWithPedal > tailNoPedal * 100.0);
}

TEST_CASE("sostenuto pedal (CC66) holds only notes already down", "[midi]")
{
    using T = EngineEvent::Type;
    const InstrumentModel model = loweredSeedModel();

    // Sostenuto captures only notes already down when it engages. Same looped
    // pad key both times; the only difference is pedal-vs-note ordering.
    const std::vector<TimelineEvent> capturedTimeline = {
        {0, T::NoteOn, 36, 100},
        {6000, T::ControlChange, 66, 127}, // pedal after note-on: captured
        {24000, T::NoteOff, 36, 0},
    };
    const std::vector<TimelineEvent> notCapturedTimeline = {
        {0, T::ControlChange, 66, 127},    // pedal before note-on: not captured
        {6000, T::NoteOn, 36, 100},
        {24000, T::NoteOff, 36, 0},
    };

    const double tailCaptured =
        energyBetween(renderSeed(model, capturedTimeline), 48000, 144000);
    const double tailNotCaptured =
        energyBetween(renderSeed(model, notCapturedTimeline), 48000, 144000);

    REQUIRE(tailCaptured > 1e-3);
    REQUIRE(tailCaptured > tailNotCaptured * 100.0);
}

TEST_CASE("pitch bend changes the rendered signal and respects the model bend range", "[midi]")
{
    using T = EngineEvent::Type;
    InstrumentModel model = loweredSeedModel();

    const std::vector<TimelineEvent> straight = {
        {0, T::NoteOn, 60, 100},
        {96000, T::NoteOff, 60, 0},
    };
    std::vector<TimelineEvent> bent = straight;
    bent.insert(bent.begin() + 1, {4800, T::PitchBend, 0, 0, 8191});

    const RenderResult a = renderSeed(model, straight, 96000);
    const RenderResult b = renderSeed(model, bent, 96000);

    // Full bend up must change the signal audibly after the bend point.
    double diff = 0.0;
    for (std::uint64_t i = 9600; i < 96000; ++i)
        diff += std::fabs(static_cast<double>(a.channels[0][i]) - b.channels[0][i]);
    REQUIRE(diff > 1.0);

    // Model-expressible bend range (AD-1): widening bendUpCents changes the
    // bent render again — proving the range flows model -> engine binding.
    for (Region& r : model.regions)
        r.bendUpCents = 1200.0f;
    const RenderResult c = renderSeed(model, bent, 96000);
    double diffRange = 0.0;
    for (std::uint64_t i = 9600; i < 96000; ++i)
        diffRange += std::fabs(static_cast<double>(b.channels[0][i]) - c.channels[0][i]);
    REQUIRE(diffRange > 1.0);
}

TEST_CASE("a mapped CC modulates through the model mod matrix", "[midi]")
{
    using T = EngineEvent::Type;
    InstrumentModel model = loweredSeedModel();

    // Map CC1 (mod wheel) to a strong gain cut on every region.
    for (Region& r : model.regions) {
        ModMatrixEntry m;
        m.source.kind = ModSourceKind::Cc;
        m.source.ccNumber = 1;
        m.target = ModTarget::Gain;
        m.depth = -60.0f; // dB at full CC
        r.modMatrix.push_back(m);
    }

    const std::vector<TimelineEvent> plain = {
        {0, T::NoteOn, 60, 100},
        {72000, T::NoteOff, 60, 0},
    };
    std::vector<TimelineEvent> damped = plain;
    damped.insert(damped.begin(), {0, T::ControlChange, 1, 127});

    const double loud = energyBetween(renderSeed(model, plain, 72000), 0, 72000);
    const double quiet = energyBetween(renderSeed(model, damped, 72000), 0, 72000);

    REQUIRE(loud > 1.0);
    REQUIRE(quiet < loud / 100.0);
}

