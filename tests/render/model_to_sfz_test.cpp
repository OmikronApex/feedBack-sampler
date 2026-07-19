// Unit tests for the engine's model->SFZ v0 bind seam (internal to
// core/engine/, tested directly through the core include path).

#include "engine/model_to_sfz.h"

#include <catch2/catch_test_macros.hpp>

using namespace fbsampler;

TEST_CASE("region basics lower to SFZ opcodes", "[model-to-sfz]")
{
    InstrumentModel model;
    Region r;
    r.sampleFile = "samples\\tone.wav";
    r.loKey = 40;
    r.hiKey = 52;
    r.loVelocity = 10;
    r.hiVelocity = 90;
    r.rootKey = 48;
    r.tuningCents = 25.4f;
    r.gainDb = -3.0f;
    r.pan = -0.5f;
    model.regions.push_back(r);

    const std::string text = detail::modelToSfzText(model, {44100.0});

    CHECK(text.find("sample=samples/tone.wav") != std::string::npos);
    CHECK(text.find("lokey=40 hikey=52 lovel=10 hivel=90 pitch_keycenter=48")
          != std::string::npos);
    CHECK(text.find("tune=25") != std::string::npos);
    CHECK(text.find("volume=-3") != std::string::npos);
    CHECK(text.find("pan=-50") != std::string::npos);
}

TEST_CASE("loop, offset and envelope lower with frame positions", "[model-to-sfz]")
{
    InstrumentModel model;
    Region r;
    r.sampleFile = "s.wav";
    r.positionUnit = SamplePositionUnit::Frames;
    r.offset = 200;
    r.loopEnabled = true;
    r.loopStart = 1000;
    r.loopEnd = 22000;
    r.amplitudeEnvelope.attackSeconds = 0.01f;
    r.amplitudeEnvelope.decaySeconds = 0.3f;
    r.amplitudeEnvelope.sustainLevel = 0.6f;
    r.amplitudeEnvelope.releaseSeconds = 0.4f;
    model.regions.push_back(r);

    const std::string text = detail::modelToSfzText(model, {44100.0});

    CHECK(text.find("offset=200") != std::string::npos);
    CHECK(text.find("loop_mode=loop_continuous loop_start=1000 loop_end=22000")
          != std::string::npos);
    CHECK(text.find("ampeg_attack=0.01") != std::string::npos);
    CHECK(text.find("ampeg_sustain=60") != std::string::npos);
    CHECK(text.find("ampeg_release=0.4") != std::string::npos);
}

TEST_CASE("seconds positions convert to frames at the pool's sample rate",
          "[model-to-sfz]")
{
    InstrumentModel model;
    Region r;
    r.sampleFile = "s.wav";
    r.positionUnit = SamplePositionUnit::Seconds;
    r.loopEnabled = true;
    r.loopStart = 0.5;  // 0.5 s @ 48 kHz = 24000 frames
    r.loopEnd = 1.0;
    model.regions.push_back(r);

    const std::string text = detail::modelToSfzText(model, {48000.0});
    CHECK(text.find("loop_start=24000 loop_end=48000") != std::string::npos);

    // Unknown rate: seconds positions cannot be converted; opcodes dropped.
    const std::string noRate = detail::modelToSfzText(model, {0.0});
    CHECK(noRate.find("loop_start") == std::string::npos);
}

TEST_CASE("Cc mod-matrix entries lower to oncc opcodes", "[model-to-sfz]")
{
    InstrumentModel model;
    Region r;
    r.sampleFile = "s.wav";

    ModMatrixEntry gain;
    gain.source.kind = ModSourceKind::Cc;
    gain.source.ccNumber = 11;
    gain.target = ModTarget::Gain;
    gain.depth = -6.0f;
    r.modMatrix.push_back(gain);

    ModMatrixEntry pan;
    pan.source.kind = ModSourceKind::Cc;
    pan.source.ccNumber = 10;
    pan.target = ModTarget::Pan;
    pan.depth = 1.0f;
    r.modMatrix.push_back(pan);

    ModMatrixEntry vel; // non-Cc sources rely on engine defaults (v0 seam cap)
    vel.source.kind = ModSourceKind::Velocity;
    vel.target = ModTarget::Gain;
    vel.depth = -12.0f;
    r.modMatrix.push_back(vel);

    model.regions.push_back(r);
    const std::string text = detail::modelToSfzText(model, {44100.0});

    CHECK(text.find("volume_oncc11=-6") != std::string::npos);
    CHECK(text.find("pan_oncc10=100") != std::string::npos);
}
