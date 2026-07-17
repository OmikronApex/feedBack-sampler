#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbsampler {

/// Schema version stamped into every serialized model artifact. See SPEC.md.
constexpr int kModelSchemaVersion = 1;

enum class ModSourceKind {
    Cc,       // MIDI CC number (ModSource::ccNumber)
    Velocity, // note-on velocity
    KeyTrack, // key position
};

struct ModSource {
    ModSourceKind kind = ModSourceKind::Velocity;
    std::uint8_t ccNumber = 0; // only meaningful when kind == Cc
};

enum class ModTarget {
    Gain,  // depth in dB
    Pitch, // depth in cents
    Pan,   // depth normalized -1..1
};

/// A regional modulation-matrix connection. See SPEC.md#ModSource-ModTarget-ModMatrixEntry.
struct ModMatrixEntry {
    std::string sourceControlId; // references ControlMapEntry::id; empty == built-in source
    ModSource source;
    ModTarget target = ModTarget::Gain;
    float depth = 0.0f; // unit depends on target
    float curve = 0.0f; // normalized 0..1; 0 == linear
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
    std::string sampleFile; // path relative to the instrument root

    std::uint8_t loKey = 0;
    std::uint8_t hiKey = 127;
    std::uint8_t loVelocity = 0;
    std::uint8_t hiVelocity = 127;
    std::uint8_t rootKey = 60;

    float tuningCents = 0.0f;
    float gainDb = 0.0f;
    float pan = 0.0f; // normalized -1..1

    float offsetSeconds = 0.0f;

    bool loopEnabled = false;
    float loopStartSeconds = 0.0f;
    float loopEndSeconds = 0.0f;

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
