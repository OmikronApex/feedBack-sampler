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

// --- Story 2.4: soundfont preset browse/switch through the plugin shell ---

namespace {

std::string multibankSf2Path()
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/sf2/multibank.sf2";
}

} // namespace

TEST_CASE("soundfont load exposes presets and the preset parameter shows names", "[plugin][sf2]")
{
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    REQUIRE(processor.loadSf2FileSync(multibankSf2Path()));
    const auto presets = processor.currentPresets();
    REQUIRE(presets.size() == 3);
    CHECK(processor.currentPresetIndex() == 0);

    // Value-to-text: generic DAW parameter views show "bank:program name".
    juce::AudioProcessorParameter* param = nullptr;
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == "presetIndex")
                param = p;
    REQUIRE(param != nullptr);
    auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
    REQUIRE(ranged != nullptr);
    const auto text = param->getText(ranged->convertTo0to1(2.0f), 64);
    CHECK(text.contains("128:0"));
    CHECK(text.contains("Standard Kit"));
}

TEST_CASE("preset switch renders the new preset without reloading", "[plugin][sf2]")
{
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    REQUIRE(processor.loadSf2FileSync(multibankSf2Path()));
    REQUIRE(processor.applyPresetIndexSync(2)); // Standard Kit (bank 128)
    CHECK(processor.currentPresetIndex() == 2);
    CHECK(processor.getLoadStatusText().contains("Standard Kit"));

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
    REQUIRE(total > 1e-4);
}

TEST_CASE("plugin state round-trips the active soundfont preset", "[plugin][sf2]")
{
    JuceEnv env;

    juce::MemoryBlock state;
    {
        fbsampler::PluginProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        REQUIRE(processor.loadSf2FileSync(multibankSf2Path()));
        REQUIRE(processor.applyPresetIndexSync(1)); // Soft Lead 0:1
        processor.getStateInformation(state);
        REQUIRE(state.getSize() > 0);
    }

    fbsampler::PluginProcessor restored;
    restored.prepareToPlay(48000.0, 256);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    // Hosts restore through the async updater; the headless test applies the
    // pending state synchronously.
    REQUIRE(restored.applyPendingStateSync());
    CHECK(restored.currentPresetIndex() == 1);
    CHECK(restored.getLoadStatusText().contains("Soft Lead"));
}

TEST_CASE("cross-format switch keeps audio sounding, no dropout", "[plugin][browser]")
{
    // Story 3.3 AC2: library A (sfz) keeps sounding while B (sf2) loads on
    // the background loader thread; the snapshot swap is glitch-free. Notes
    // re-trigger per block: held-note sustain across the swap is the
    // documented 2.4 bound (crossfade polish is Story 5.3).
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    constexpr int kBlock = 256;
    processor.prepareToPlay(48000.0, kBlock);

    REQUIRE(processor.loadSfzFileSync(seedSfzPath()));

    juce::AudioBuffer<float> buffer(2, kBlock);
    bool sawSilentBlock = false;
    bool triggered = false;
    for (int block = 0; block < 200; ++block) {
        if (block == 20) {
            // Async load of the soundfont mid-render (the browser's path).
            REQUIRE(processor.loadSf2FileAsync(multibankSf2Path()));
            triggered = true;
        }
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        if (blockEnergy(buffer) <= 0.0)
            sawSilentBlock = true;
        if (triggered && !processor.isLoadInProgress() && block > 30)
            break;
    }
    // Give the loader thread time to finish if it has not already.
    for (int i = 0; i < 200 && processor.isLoadInProgress(); ++i)
        juce::Thread::sleep(10);

    CHECK_FALSE(sawSilentBlock);            // A never dropped out
    REQUIRE_FALSE(processor.isLoadInProgress());
    CHECK(processor.currentPresets().size() == 3); // B (sf2) is active

    // B renders sound through the same processor.
    double total = 0.0;
    for (int block = 0; block < 40; ++block) {
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        total += blockEnergy(buffer);
    }
    REQUIRE(total > 1e-4);

    // Second load while one is in flight returns false (browser ignores +
    // status text — the documented guard).
    // (Load finished above, so start one and immediately race another.)
    REQUIRE(processor.loadSfzFileAsync(juce::String(seedSfzPath())));
    CHECK_FALSE(processor.loadSf2FileAsync(multibankSf2Path()));
    for (int i = 0; i < 500 && processor.isLoadInProgress(); ++i)
        juce::Thread::sleep(10);
}

TEST_CASE("UI CC injection reaches the engine (CC7 volume audible)", "[plugin][cards]")
{
    // Story 3.4: setControlValue drains into a block-start ControlChange.
    // multibank.sf2 carries the SF2 default modulator set, which routes CC7
    // to attenuation — CC7 low must render less energy than CC7 high.
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    constexpr int kBlock = 256;
    processor.prepareToPlay(48000.0, kBlock);
    REQUIRE(processor.loadSf2FileSync(multibankSf2Path()));

    auto renderWithCc7 = [&](int ccValue) {
        processor.setControlValue(7, ccValue);
        juce::AudioBuffer<float> buffer(2, kBlock);
        double total = 0.0;
        for (int block = 0; block < 40; ++block) {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            buffer.clear();
            processor.processBlock(buffer, midi);
            total += blockEnergy(buffer);
        }
        // release everything before the next pass
        for (int block = 0; block < 20; ++block) {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            buffer.clear();
            processor.processBlock(buffer, midi);
        }
        return total;
    };

    const double loud = renderWithCc7(127);
    const double quiet = renderWithCc7(8);
    REQUIRE(loud > 1e-4);
    CHECK(quiet < loud * 0.5);
}

TEST_CASE("voice count rises with notes and falls to zero after release", "[plugin][cards]")
{
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    constexpr int kBlock = 256;
    processor.prepareToPlay(48000.0, kBlock);
    REQUIRE(processor.loadSfzFileSync(seedSfzPath()));
    CHECK(processor.activeVoiceCount() == 0);

    juce::AudioBuffer<float> buffer(2, kBlock);
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
    CHECK(processor.activeVoiceCount() > 0);

    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
    // Render until the release tails finish.
    int blocks = 0;
    while (processor.activeVoiceCount() > 0 && blocks++ < 2000) {
        juce::MidiBuffer midi;
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
    CHECK(processor.activeVoiceCount() == 0);
}

TEST_CASE("generic master volume attenuates post-engine output", "[plugin][cards]")
{
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    constexpr int kBlock = 256;
    processor.prepareToPlay(48000.0, kBlock);
    REQUIRE(processor.loadSfzFileSync(seedSfzPath()));

    auto render = [&] {
        juce::AudioBuffer<float> buffer(2, kBlock);
        double total = 0.0;
        for (int block = 0; block < 40; ++block) {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            buffer.clear();
            processor.processBlock(buffer, midi);
            total += blockEnergy(buffer);
        }
        for (int block = 0; block < 400; ++block) {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            buffer.clear();
            processor.processBlock(buffer, midi);
            if (processor.activeVoiceCount() == 0)
                break;
        }
        return total;
    };

    processor.setMasterVolumeDb(0.0f);
    const double unity = render();
    processor.setMasterVolumeDb(-40.0f);
    const double quiet = render();
    REQUIRE(unity > 1e-4);
    CHECK(quiet < unity * 0.05);
}

TEST_CASE("voice limit caps active voices", "[plugin][settings]")
{
    // Story 3.5: limit applies live via snapshot rebuild (also seeds 5.4).
    JuceEnv env;
    fbsampler::PluginProcessor processor;
    constexpr int kBlock = 256;
    processor.prepareToPlay(48000.0, kBlock);
    REQUIRE(processor.loadSfzFileSync(seedSfzPath()));
    REQUIRE(processor.applyVoiceLimitSync(4));

    juce::AudioBuffer<float> buffer(2, kBlock);
    for (int block = 0; block < 8; ++block) {
        juce::MidiBuffer midi;
        // Pile on more notes than the limit allows, every block.
        for (int n = 0; n < 4; ++n)
            midi.addEvent(juce::MidiMessage::noteOn(
                              1, 40 + block * 4 + n, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
    CHECK(processor.activeVoiceCount() > 0);
    CHECK(processor.activeVoiceCount() <= 4);
}
