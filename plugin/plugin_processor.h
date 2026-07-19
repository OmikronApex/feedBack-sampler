#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "fbsampler/diagnostic.h"
#include "fbsampler/engine.h"
#include "fbsampler/pool.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fbsampler {

// Story 1.5: the walking skeleton's proof-of-life host shell. Owns one core
// Engine + SamplePool, translates the host MIDI buffer into engine events with
// sample-accurate offsets, and runs library loads on a background thread so
// the audio thread never blocks (AD-3 shape: UI issues a load command, the
// engine swaps an immutable snapshot).
//
// Listeners (the editor) observe load status via ChangeBroadcaster; status
// text is read under a lock from any thread.
class PluginProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster {
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // Epic 6 concern; harmless stubs for now (pluginval-tolerated).
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /// Kick off an asynchronous load of `sfzPath` on a background thread.
    /// Returns false (and updates status) if a load is already in flight.
    /// Completion is announced via sendChangeMessage() from the loader thread.
    bool loadSfzFileAsync(const juce::String& sfzPath);

    /// Synchronous load path (loader-thread body; also the headless test
    /// entry). Returns true when the engine accepted the new snapshot.
    bool loadSfzFileSync(const std::string& sfzPath);

    /// Current human-readable load status (thread-safe).
    juce::String getLoadStatusText() const;

    /// True while a background load is running.
    bool isLoadInProgress() const { return loadBusy_.load(); }

private:
    void setLoadStatusText(const juce::String& text);
    void joinLoadThread();

    Engine engine_;
    std::shared_ptr<SamplePool> pool_;

    // Audio-thread scratch: preallocated in prepareToPlay; events beyond
    // capacity are dropped rather than allocating on the audio thread.
    std::vector<EngineEvent> eventScratch_;

    std::thread loadThread_;
    std::atomic<bool> loadBusy_{false};

    mutable juce::CriticalSection statusLock_;
    juce::String statusText_{"no instrument loaded"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace fbsampler
