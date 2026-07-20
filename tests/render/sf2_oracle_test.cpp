// FluidSynth-oracle render diffs for the SF2 modulator path (Story 2.2 AC1,
// NFR-5). The checked-in *_fluid.wav references were captured once from
// FluidSynth 2.5.6 (tests/render/fixtures/sf2/make_oracle_refs.py documents
// the exact command; corpus/README.md documents the procedure) — CI never
// runs FluidSynth.
//
// Metric design: the two engines differ legitimately in absolute gain
// staging, interpolation and filter defaults, so raw-waveform diffs would
// measure the engines, not the modulators. Each test instead compares the
// BEHAVIORAL response the story names, in a normalized form:
//  - velocity response: per-note RMS normalized to the loudest note
//    (gain-invariant; pins the concave velocity curve),
//  - mod wheel: windowed-RMS trajectory normalized to the pre-sweep window
//    (pins the custom CC1 -> attenuation modulator),
//  - pitch bend: fundamental-frequency estimates per window (pins the
//    default ±2-semitone bend range).
// Tolerances below are the per-entry thresholds with their rationale.

#include <catch2/catch_test_macros.hpp>

#include "fbsampler/pool.h"
#include "fbsampler/sf2_frontend.h"
#include "midi_file.h"
#include "pool/wav_reader.h"
#include "render_harness.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace fbsampler;
using namespace fbsampler::testutil;

namespace {

constexpr double kRate = 44100.0;
constexpr std::uint64_t kTotalFrames = 4 * 44100;

std::string fixture(const std::string& name)
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/sf2/" + name;
}

// Mono mix of a stereo (or mono) buffer.
std::vector<float> monoMix(const std::vector<std::vector<float>>& channels)
{
    REQUIRE(!channels.empty());
    std::vector<float> out(channels[0].size(), 0.0f);
    for (const auto& ch : channels)
        for (std::size_t i = 0; i < out.size() && i < ch.size(); ++i)
            out[i] += ch[i] / static_cast<float>(channels.size());
    return out;
}

std::vector<float> renderOurs(const std::string& midiName)
{
    const auto lowered = lowerSf2Preset(fixture("oracle.sf2"), 0, 0);
    for (const auto& d : lowered.diagnostics) {
        INFO(d.code << ": " << d.message);
        CHECK(d.severity != Severity::Error);
    }
    REQUIRE(lowered.model.has_value());

    std::vector<TimelineEvent> timeline;
    std::string error;
    REQUIRE(loadMidiTimeline(fixture(midiName), kRate, &timeline, &error));

    RenderSettings settings;
    settings.sampleRate = kRate;
    settings.blockFrames = 256;
    settings.totalFrames = kTotalFrames;

    auto pool = createAllRamSamplePool();
    const auto result = renderOffline(*lowered.model, std::move(pool), "", timeline, settings);
    for (const auto& d : result.diagnostics) {
        INFO(d.code << ": " << d.message);
        CHECK(d.severity != Severity::Error);
    }
    REQUIRE(result.ok);
    return monoMix(result.channels);
}

std::vector<float> loadFluidReference(const std::string& name)
{
    detail::DecodedWav wav;
    std::vector<Diagnostic> diags;
    REQUIRE(detail::readWavFile(fixture(name), wav, &diags));
    REQUIRE(wav.sampleRate == kRate);
    return monoMix(wav.channels);
}

float rmsWindow(const std::vector<float>& signal, double fromSeconds, double toSeconds)
{
    const auto begin = static_cast<std::size_t>(fromSeconds * kRate);
    const auto end = static_cast<std::size_t>(toSeconds * kRate);
    REQUIRE(end <= signal.size());
    REQUIRE(begin < end);
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(end - begin)));
}

// Fundamental estimate: positive-going zero crossings per second.
float estimateF0(const std::vector<float>& signal, double fromSeconds, double toSeconds)
{
    const auto begin = static_cast<std::size_t>(fromSeconds * kRate);
    const auto end = static_cast<std::size_t>(toSeconds * kRate);
    REQUIRE(end <= signal.size());
    int crossings = 0;
    for (std::size_t i = begin + 1; i < end; ++i)
        if (signal[i - 1] <= 0.0f && signal[i] > 0.0f)
            ++crossings;
    return static_cast<float>(crossings / (toSeconds - fromSeconds));
}

} // namespace

TEST_CASE("SF2 velocity response tracks FluidSynth's concave curve", "[sf2][render][oracle]")
{
    const auto ours = renderOurs("vel_ramp.mid");
    const auto fluid = loadFluidReference("vel_ramp_fluid.wav");

    // Notes at 0.5 s spacing; sample the steady interior of each note.
    // Normalize both curves to their loudest (vel 127) note, then compare
    // pointwise. Tolerance 0.06 (normalized amplitude): the concave curve
    // itself spans 0..1, and the residual difference is envelope-shape and
    // interpolation, not the velocity law.
    const float oursRef = rmsWindow(ours, 3.55, 3.85);
    const float fluidRef = rmsWindow(fluid, 3.55, 3.85);
    REQUIRE(oursRef > 0.0f);
    REQUIRE(fluidRef > 0.0f);

    for (int i = 0; i < 8; ++i) {
        const double from = i * 0.5 + 0.05;
        const double to = i * 0.5 + 0.35;
        const float a = rmsWindow(ours, from, to) / oursRef;
        const float b = rmsWindow(fluid, from, to) / fluidRef;
        INFO("note " << i << ": ours " << a << " fluid " << b);
        CHECK(std::abs(a - b) < 0.06f);
    }
}

TEST_CASE("SF2 custom CC1 modulator tracks FluidSynth's attenuation sweep",
          "[sf2][render][oracle]")
{
    const auto ours = renderOurs("modwheel.mid");
    const auto fluid = loadFluidReference("modwheel_fluid.wav");

    // CC1 ramps 0 -> 127 over 0.5..3.0 s while a note sustains. Compare the
    // gain trajectory normalized to the pre-sweep window. Tolerance 0.05
    // normalized: both engines execute the identical linear 960 cB modulator;
    // the residual is envelope/settling differences at window edges.
    const float oursRef = rmsWindow(ours, 0.2, 0.45);
    const float fluidRef = rmsWindow(fluid, 0.2, 0.45);
    REQUIRE(oursRef > 0.0f);
    REQUIRE(fluidRef > 0.0f);

    for (double t = 0.7; t < 3.2; t += 0.25) {
        const float a = rmsWindow(ours, t, t + 0.2) / oursRef;
        const float b = rmsWindow(fluid, t, t + 0.2) / fluidRef;
        INFO("window at " << t << ": ours " << a << " fluid " << b);
        CHECK(std::abs(a - b) < 0.05f);
    }
}

TEST_CASE("matrix-heavy SF2 render stays RT-clean", "[sf2][render][rt]")
{
    // Dev-note requirement: the RT detector run must include a matrix-heavy
    // preset. oracle.sf2 carries the default modulator set plus a custom CC1
    // modulator; markRtSections wraps every process() call in an rtcheck
    // section (allocation/lock/file-I/O violations fail the render).
    const auto lowered = lowerSf2Preset(fixture("oracle.sf2"), 0, 0);
    REQUIRE(lowered.model.has_value());

    std::vector<TimelineEvent> timeline;
    std::string error;
    REQUIRE(loadMidiTimeline(fixture("modwheel.mid"), kRate, &timeline, &error));

    RenderSettings settings;
    settings.sampleRate = kRate;
    settings.blockFrames = 256;
    settings.totalFrames = kTotalFrames;
    settings.markRtSections = true;

    auto pool = createAllRamSamplePool();
    const auto result = renderOffline(*lowered.model, std::move(pool), "", timeline, settings);
    for (const auto& d : result.diagnostics) {
        INFO(d.code << ": " << d.message);
        CHECK(d.severity != Severity::Error);
    }
    REQUIRE(result.ok);
}

TEST_CASE("SF2 pitch-bend range tracks FluidSynth at the default 2 semitones",
          "[sf2][render][oracle]")
{
    const auto ours = renderOurs("bend.mid");
    const auto fluid = loadFluidReference("bend_fluid.wav");

    // f0 estimates in three steady windows: pre-bend (center), full up
    // (+1 st, held 1.5..1.75), full down (-2 st, held 3.0..3.5). Tolerance:
    // 2% relative — zero-crossing estimation on a triangle is exact to the
    // period; 2% absorbs the window straddling one period boundary.
    const struct { double from, to; } windows[] = {
        { 0.1, 0.45 },   // center
        { 1.55, 1.72 },  // +1 semitone
        { 3.1, 3.45 },   // -2 semitones
    };
    for (const auto& w : windows) {
        const float a = estimateF0(ours, w.from, w.to);
        const float b = estimateF0(fluid, w.from, w.to);
        INFO("window " << w.from << ".." << w.to << ": ours " << a << " Hz, fluid " << b
                       << " Hz");
        REQUIRE(b > 0.0f);
        CHECK(std::abs(a - b) / b < 0.02f);
    }
}
