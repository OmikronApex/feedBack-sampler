#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbsampler {

/// Schema version stamped into every serialized model artifact. See SPEC.md.
/// v4: Region::sampleFile may carry a container-sample URI
/// ("sf2://<path>#<sampleIndex>") in addition to a plain relative path.
/// v5: ModSource gains SF2 modulator semantics (source kinds, direction,
/// polarity, typed curve shapes) and ModMatrixEntry gains a secondary
/// amount source; the old normalized `curve` float is subsumed.
constexpr int kModelSchemaVersion = 5;

/// Unit of a region's sample-position fields (offset / loop points).
/// Frontends record positions exactly as the source format expresses them;
/// the engine converts at bind time, when the sample file's real rate is
/// known (Story 1.4). Frontends never open sample files (AD-2), so a
/// frame-based format must not guess a rate to fake seconds.
enum class SamplePositionUnit {
    Frames,  // sample frames within the referenced file
    Seconds,
};

enum class ModSourceKind {
    None,                  // no source (only meaningful for an amount source)
    Cc,                    // MIDI CC number (ModSource::ccNumber)
    Velocity,              // note-on velocity
    KeyTrack,              // key position
    PolyPressure,          // polyphonic key pressure
    ChannelPressure,       // channel pressure (aftertouch)
    PitchWheel,            // pitch-bend wheel position
    PitchWheelSensitivity, // RPN 0 pitch-bend range
};

/// SF2-style controller-to-normalized mapping shapes (SoundFont 2.04 §9.5.2).
/// Formulas in SPEC.md#Curve-shapes; Linear is the v0..v4 default behavior.
enum class ModCurveType {
    Linear,
    Concave, // -20/96 * log10 based, slow start
    Convex,  // mirror of Concave, fast start
    Switch,  // 0 below the midpoint, 1 at/above
};

struct ModSource {
    ModSourceKind kind = ModSourceKind::Velocity;
    std::uint8_t ccNumber = 0;  // only meaningful when kind == Cc
    bool maxToMin = false;      // direction: false = min->max, true = max->min
    bool bipolar = false;       // polarity: false = 0..1, true = -1..1
    ModCurveType curve = ModCurveType::Linear;
};

enum class ModTarget {
    Gain,         // depth in dB
    Pitch,        // depth in cents
    Pan,          // depth normalized -1..1
    FilterCutoff, // depth in cents (reserved: engine executes when it grows a filter)
    ReverbSend,   // depth normalized 0..1 (reserved)
    ChorusSend,   // depth normalized 0..1 (reserved)
};

/// A regional modulation-matrix connection (SF2 modulator shape:
/// source x amountSource x depth -> target). See
/// SPEC.md#ModSource-ModTarget-ModMatrixEntry. `amountSource.kind == None`
/// (the default) means the depth is applied unscaled.
struct ModMatrixEntry {
    std::string sourceControlId; // references ControlMapEntry::id; empty == built-in source
    ModSource source;
    ModSource amountSource { ModSourceKind::None, 0, false, false, ModCurveType::Linear };
    ModTarget target = ModTarget::Gain;
    float depth = 0.0f; // unit depends on target
};

/// A single amplitude ADSR+ envelope. All time fields are seconds.
struct EnvelopeADSR {
    float delaySeconds = 0.0f;
    float attackSeconds = 0.0f;
    float holdSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f; // normalized 0..1
    float releaseSeconds = 0.0f;
};

/// A single SFZ-superset region. See SPEC.md#Region.
struct Region {
    /// Sample reference: either a path relative to the instrument root, or
    /// (schema v4+) a container-sample URI of the form
    /// "sf2://<container-path>#<sampleIndex>" for samples embedded inside a
    /// SoundFont. The pool resolves both forms (AD-2); frontends never
    /// extract embedded samples to temp files.
    std::string sampleFile;

    std::uint8_t loKey = 0;
    std::uint8_t hiKey = 127;
    std::uint8_t loVelocity = 0;
    std::uint8_t hiVelocity = 127;
    std::uint8_t rootKey = 60;

    float tuningCents = 0.0f;
    float gainDb = 0.0f;
    float pan = 0.0f; // normalized -1..1

    // Pitch-bend range in cents (SFZ bend_up/bend_down defaults). Kept in the
    // model so bend behavior is model-expressible, never an SFZ-text backdoor
    // (AD-1). bendDownCents is typically negative.
    float bendUpCents = 200.0f;
    float bendDownCents = -200.0f;

    SamplePositionUnit positionUnit = SamplePositionUnit::Frames;
    double offset = 0.0; // in positionUnit

    bool loopEnabled = false;
    double loopStart = 0.0; // in positionUnit
    double loopEnd = 0.0;   // in positionUnit

    EnvelopeADSR amplitudeEnvelope;

    std::vector<ModMatrixEntry> modMatrix;
};

/// A stable control identity + accessibility metadata (AD-8, AD-11).
struct ControlMapEntry {
    std::string id;             // stable control ID, unique within the model
    std::string displayName;    // UI label
    std::string accessibleName; // screen-reader label
};

/// The canonical instrument model (AD-11): every frontend lowers into this.
struct InstrumentModel {
    int schemaVersion = kModelSchemaVersion;
    std::string name;
    std::vector<Region> regions;
    std::vector<ControlMapEntry> controls;
};

} // namespace fbsampler
