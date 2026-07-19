#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/model.h"
#include "fbsampler/pool.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fbsampler {

/// One MIDI 1.0 event at the engine boundary. Time is expressed in samples at
/// the engine rate: `delayFrames` is the offset of the event within the
/// current process() block.
struct EngineEvent {
    enum class Type { NoteOn, NoteOff, ControlChange, PitchBend };
    Type type = Type::NoteOn;
    int delayFrames = 0;
    std::uint8_t note = 60;      // note number, or CC number for ControlChange
    std::uint8_t velocity = 0;   // velocity, or CC value 0..127 for ControlChange
    int bendValue = 0;           // PitchBend only: -8192..8191 (0 == center)
};

/// The unified voice engine (AD-1): renders canonical-model instances through
/// the sample-pool interface. JUCE-free (AD-6); no exceptions cross this API.
///
/// Structural state is snapshot-based (AD-3 seed): the engine holds exactly
/// one immutable model+pool snapshot; load() builds a new snapshot on the
/// control thread and swaps it in atomically, retiring the old one off the
/// audio thread. v0 loading is synchronous — the full async command path
/// arrives in Epic 5 — but the swap shape is fixed now.
///
/// Threading contract:
///  - prepare()/load() are control-thread calls; prepare() must not run
///    concurrently with process() (standard plugin prepare-to-play contract).
///  - process() is the audio-thread render callback: no allocation, locks,
///    or file I/O (NFR-1; enforced by the RT-safety detector in debug tests).
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Set the render sample rate and maximum block size. Call before load();
    /// calling after load() rebinds the current snapshot at the new settings.
    void prepare(double sampleRate, int maxBlockFrames);

    /// Bind an immutable model snapshot + pool. `instrumentRoot` is the
    /// directory the model's relative sample paths resolve against.
    /// Returns diagnostics; any Severity::Error means the previous snapshot
    /// (if any) stays active and audible.
    std::vector<Diagnostic> load(const InstrumentModel& model,
                                 std::shared_ptr<SamplePool> pool,
                                 const std::string& instrumentRoot);

    /// Render one block. `out` is an array of 2 channel pointers (stereo),
    /// each `numFrames` long; the engine overwrites them. Events must be
    /// ordered by delayFrames, each within [0, numFrames).
    void process(const EngineEvent* events, std::size_t numEvents,
                 float** out, std::size_t numFrames) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fbsampler
