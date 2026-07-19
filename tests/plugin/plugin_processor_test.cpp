// Story 1.5 Task 5: headless processBlock test at the shell level — feed the
// processor real MIDI, assert non-silent output and RT-detector clean. Runs
// the actual PluginProcessor (same translation code the host exercises), not
// the core engine directly.

#include "plugin_processor.h"

#include "fbsampler/detail/rt_check.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

namespace {

// JUCE message manager for ChangeBroadcaster/Label-free headless use.
struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};

std::string seedSfzPath()
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/seed/seed.sfz";
}

double blockEnergy(const juce::AudioBuffer<float>& buffer)
{
    double e = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* p = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            e += static_cast<double>(p[i]) * p[i];
    }
    return e;
}

} // namespace

TEST_CASE("processor renders MIDI through the engine, RT-clean", "[plugin]")
{
    JuceEnv env;
    fbsampler::PluginProcessor processor;

    constexpr int kBlock = 256;
    processor.prepareToPlay(48000.0, kBlock);

    // Silence before any instrument is loaded.
    juce::AudioBuffer<float> buffer(2, kBlock);
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        REQUIRE(blockEnergy(buffer) == 0.0);
    }

    // Synchronous load (the same body the background loader runs).
    REQUIRE(processor.loadSfzFileSync(seedSfzPath()));
    CHECK(processor.getLoadStatusText().startsWith("loaded"));

    // Note-on mid-block: sample-accurate offset means the first `offset`
    // frames of the first block stay silent.
    constexpr int kOffset = 128;
    fbsampler::rtcheck::resetViolations();
    double total = 0.0;
    for (int block = 0; block < 40; ++block) {
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), kOffset);
        buffer.clear();
        {
            fbsampler::rtcheck::SectionGuard guard;
            processor.processBlock(buffer, midi);
        }
        if (block == 0) {
            for (int i = 0; i < kOffset; ++i) {
                REQUIRE(buffer.getReadPointer(0)[i] == 0.0f);
                REQUIRE(buffer.getReadPointer(1)[i] == 0.0f);
            }
        }
        total += blockEnergy(buffer);
    }
    REQUIRE(total > 1e-3);
    {
        INFO("RT violation: "
             << (fbsampler::rtcheck::lastViolation() ? fbsampler::rtcheck::lastViolation() : "none"));
        REQUIRE(fbsampler::rtcheck::violationCount() == 0);
    }
}

TEST_CASE("failed load reports a diagnostic and keeps the previous instrument", "[plugin]")
{
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    REQUIRE(processor.loadSfzFileSync(seedSfzPath()));

    // Malformed load fails...
    REQUIRE_FALSE(processor.loadSfzFileSync("definitely/not/here.sfz"));
    CHECK(processor.getLoadStatusText().contains("load failed"));

    // ...and the previous instrument still sounds (FR-5 seed).
    juce::AudioBuffer<float> buffer(2, 256);
    double total = 0.0;
    for (int block = 0; block < 40; ++block) {
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        total += blockEnergy(buffer);
    }
    REQUIRE(total > 1e-3);
}

TEST_CASE("status text stays bounded for libraries with thousands of warnings", "[plugin]")
{
    JuceEnv env;

    // A region per unsupported opcode: each lowers to a warning diagnostic.
    juce::String text = "<region> sample=missing.wav\n";
    for (int i = 0; i < 5000; ++i)
        text << "  some_unsupported_opcode_" << i << "=1\n";
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("fbsampler-warn-test");
    dir.createDirectory();
    juce::File sfz = dir.getChildFile("warnings.sfz");
    REQUIRE(sfz.replaceWithText(text));

    fbsampler::PluginProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    processor.loadSfzFileSync(sfz.getFullPathName().toStdString());

    // The UI renders this on the message thread: it must stay small no matter
    // how noisy the library is (the freeze regression this test pins).
    const juce::String status = processor.getLoadStatusText();
    CHECK(status.length() < 4000);
    CHECK(status.contains("more diagnostics"));

    dir.deleteRecursively();
}
