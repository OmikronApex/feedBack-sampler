#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "fbsampler/config_service.h"
#include "fbsampler/diagnostic.h"
#include "fbsampler/engine.h"
#include "fbsampler/pool.h"
#include "fbsampler/sf2_frontend.h"

#include <array>
#include <atomic>
#include <functional>
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
                        public juce::ChangeBroadcaster,
                        private juce::AudioProcessorParameter::Listener,
                        private juce::AsyncUpdater {
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

    // --- Soundfont preset session (Story 2.4, FR-9) ---

    /// Open a soundfont, enumerate its presets, and load one synchronously
    /// (loader-thread body; also the headless test entry). `presetIndex < 0`
    /// selects the first preset. Returns true when the engine accepted the
    /// snapshot.
    bool loadSf2FileSync(const std::string& sf2Path, int presetIndex = -1);

    /// Async wrapper mirroring loadSfzFileAsync.
    bool loadSf2FileAsync(const juce::String& sf2Path);

    /// Switch the active soundfont preset (background/loader path — never
    /// call on the audio thread; parameter changes defer here through the
    /// async updater). No file re-read: lowers from the parsed session and
    /// rebinds through the shared pool.
    bool applyPresetIndexSync(int presetIndex);

    /// The open session's presets (empty when no soundfont is loaded).
    std::vector<Sf2PresetInfo> currentPresets() const;

    /// Currently active preset index (-1 when none / not a soundfont).
    int currentPresetIndex() const { return currentPresetIndex_.load(); }

    /// Apply state restored via setStateInformation synchronously (test
    /// entry; hosts get the same effect via the async updater).
    bool applyPendingStateSync();

    // --- Instrument view surface (Story 3.4) ---

    /// Live voice count (lock-free, published by the audio thread).
    int activeVoiceCount() const { return engine_.activeVoiceCount(); }

    /// Control map of the currently loaded model (empty when none/generic).
    std::vector<ControlMapEntry> currentControls() const;

    /// True when the active library is a soundfont (preset row visible).
    bool isSoundfontLoaded() const;

    /// UI -> engine CC injection: value 0..127 for `ccNumber`, delivered as
    /// a ControlChange EngineEvent at the start of the next audio block via
    /// an atomic per-CC slot (never a lock on the audio thread).
    void setControlValue(int ccNumber, int value0to127);

    /// Generic set (library defines no controls): post-engine volume/pan,
    /// smoothed in processBlock — no core change, no zipper noise.
    void setMasterVolumeDb(float volumeDb);
    void setMasterPan(float pan); // -1..1

    /// Generic tuning (± cents) and ADSR override: route (b) — re-lower the
    /// stored model with offsets applied and swap snapshots through the
    /// existing loader path (debounce at the gesture level: call on gesture
    /// end). attack/release < 0 leave the region envelope untouched.
    bool applyModelOffsetsAsync(float tuningCents, float attackSeconds,
                                float releaseSeconds);

    /// Voice limit (Story 3.5, AD-7): stored in the engine and applied by
    /// rebuilding the current snapshot (sfizz's voice resize is not safe
    /// concurrent with process). Sync = loader-thread/test body; async =
    /// UI entry with the shared busy guard. Persistence is the caller's job
    /// (settings UI writes through ConfigService).
    bool applyVoiceLimitSync(int maxVoices);
    bool applyVoiceLimitAsync(int maxVoices);

    /// Select a soundfont preset from the UI (message thread): sets the
    /// existing presetIndex parameter, which defers through the 2.4 path.
    void selectPreset(int presetIndex);

    // --- Shared library index (Story 3.2, AD-9) ---

    /// The per-user config service. Constructed at processor startup; the
    /// index is READ at startup, never rescanned automatically (a second
    /// instance in another host sees the first instance's scan results
    /// without any work). The editor (Story 3.3) consumes index(); Story
    /// 3.5's settings UI reads/writes through this service only.
    ConfigService& configService() { return *configService_; }

    /// User-triggered incremental rescan on the shared loader thread.
    /// Returns false when a load/scan is already in flight. Completion is
    /// announced via sendChangeMessage().
    bool rescanLibrariesAsync();

    /// Story 3.6 scanning banner: true while a scan runs; latest progress
    /// line ("current path (n libraries)") for the banner, updated at ~4 Hz
    /// via sendChangeMessage.
    bool isScanInProgress() const { return scanBusy_.load(); }
    juce::String scanProgressText() const;

    /// Story 3.7: last editor size, per-instance only (NOT plugin state
    /// schema) — a reopened editor keeps its size within the host session.
    void setLastEditorSize(int w, int h) { lastEditorW_ = w; lastEditorH_ = h; }
    int lastEditorWidth() const { return lastEditorW_; }
    int lastEditorHeight() const { return lastEditorH_; }

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override;

    void setLoadStatusText(const juce::String& text);
    void joinLoadThread();
    void startLoaderThread(std::function<void()> body);
    bool applyPendingStateSyncBody();

    Engine engine_;
    std::shared_ptr<SamplePool> pool_;
    std::unique_ptr<ConfigService> configService_;

    // Story 3.4: UI -> engine CC handoff. Slot value -1 == empty; otherwise
    // 0..127 pending CC value, drained at the top of processBlock.
    std::array<std::atomic<int>, 128> pendingCc_{};

    // Post-engine generic volume/pan (smoothed on the audio thread).
    std::atomic<float> masterVolumeDb_{0.0f};
    std::atomic<float> masterPan_{0.0f};
    juce::SmoothedValue<float> smoothedGainL_{1.0f}, smoothedGainR_{1.0f};

    // Copy of the last successfully loaded model + root for offset re-lower
    // (generic tuning/ADSR, route (b)). Guarded by modelLock_.
    mutable juce::CriticalSection modelLock_;
    InstrumentModel currentModel_;
    std::string currentRoot_;
    bool hasModel_ = false;

    // Audio-thread scratch: preallocated in prepareToPlay; events beyond
    // capacity are dropped rather than allocating on the audio thread.
    std::vector<EngineEvent> eventScratch_;

    std::thread loadThread_;
    std::atomic<bool> loadBusy_{false};
    std::atomic<bool> scanBusy_{false};
    mutable juce::CriticalSection scanLock_;
    juce::String scanProgressText_;
    int lastEditorW_ = 0, lastEditorH_ = 0; // 0 = never opened

    mutable juce::CriticalSection statusLock_;
    juce::String statusText_{"no instrument loaded"};

    // Soundfont session state (Story 2.4). The session is immutable once
    // opened; the shared_ptr swap is guarded by sessionLock_ so the
    // parameter text callback can read names from any thread.
    mutable juce::CriticalSection sessionLock_;
    std::shared_ptr<Sf2Session> session_;
    std::atomic<int> currentPresetIndex_{-1};

    // AD-8: dedicated generic parameter with a stable identity — never one
    // of the 128 library-control proxies. Owned by the AudioProcessor.
    juce::AudioParameterInt* presetParam_ = nullptr;
    std::atomic<bool> presetParamPending_{false};

    // State restore (AD-3 chunk: library reference + preset + params).
    struct PendingState {
        juce::String path;   // instrument file (sfz or sf2)
        bool isSf2 = false;
        int bank = -1;       // preferred identity: bank/program
        int program = -1;
        int presetIndex = -1; // fallback
    };
    mutable juce::CriticalSection pendingStateLock_;
    PendingState pendingState_;
    std::atomic<bool> statePending_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace fbsampler
