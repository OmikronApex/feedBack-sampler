#include "fbsampler/validate.h"

#include <cmath>
#include <unordered_set>

namespace fbsampler {

namespace {

void addError(std::vector<Diagnostic>& out, std::string code, std::string message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = std::move(code);
    d.message = std::move(message);
    out.push_back(std::move(d));
}

bool isFinite(float v)
{
    return std::isfinite(v);
}

// Range predicates are written so NaN fails them: every comparison with NaN is
// false, so checks must assert in-range (finite && lo <= v <= hi) rather than
// test for out-of-range (v < lo || v > hi), which NaN slips through.
bool inRange(float v, float lo, float hi)
{
    return isFinite(v) && v >= lo && v <= hi;
}

void validateEnvelope(const EnvelopeADSR& env, std::vector<Diagnostic>& out)
{
    const auto validTime = [](float t) { return isFinite(t) && t >= 0.0f; };
    if (!validTime(env.delaySeconds) || !validTime(env.attackSeconds) || !validTime(env.holdSeconds)
        || !validTime(env.decaySeconds) || !validTime(env.releaseSeconds)) {
        addError(out, "region.envelope_time_negative",
            "Envelope time field must be finite and >= 0");
    }
    if (!inRange(env.sustainLevel, 0.0f, 1.0f)) {
        addError(out, "region.envelope_sustain_out_of_range",
            "Envelope sustainLevel must be finite and within [0, 1]");
    }
}

void validateRegion(const Region& region, const std::unordered_set<std::string>& controlIds,
    std::vector<Diagnostic>& out)
{
    if (region.sampleFile.empty()) {
        addError(out, "region.sample_file_missing", "Region sampleFile must not be empty");
    }
    if (region.loKey > region.hiKey) {
        addError(out, "region.key_range_invalid", "Region loKey must be <= hiKey");
    }
    if (region.hiKey > 127 || region.rootKey > 127) {
        addError(out, "region.key_out_of_midi_range",
            "Region key fields must be within the MIDI range [0, 127]");
    }
    if (region.loVelocity > region.hiVelocity) {
        addError(out, "region.velocity_range_invalid", "Region loVelocity must be <= hiVelocity");
    }
    if (region.hiVelocity > 127) {
        addError(out, "region.velocity_out_of_midi_range",
            "Region velocity fields must be within the MIDI range [0, 127]");
    }
    if (!inRange(region.pan, -1.0f, 1.0f)) {
        addError(out, "region.pan_out_of_range", "Region pan must be finite and within [-1, 1]");
    }
    if (!isFinite(region.gainDb)) {
        addError(out, "region.gain_not_finite", "Region gainDb must be finite");
    }
    if (!isFinite(region.tuningCents)) {
        addError(out, "region.tuning_not_finite", "Region tuningCents must be finite");
    }
    if (!(std::isfinite(region.offset) && region.offset >= 0.0)) {
        addError(out, "region.offset_negative", "Region offset must be finite and >= 0");
    }
    // Loop bounds must be sane even when the loop is disabled: a NaN/negative
    // bound must not survive a round-trip only to detonate when the loop is
    // later enabled. The ordering constraint applies only when enabled.
    if (!(std::isfinite(region.loopStart) && std::isfinite(region.loopEnd)
            && region.loopStart >= 0.0 && region.loopEnd >= 0.0
            && (!region.loopEnabled || region.loopStart < region.loopEnd))) {
        addError(out, "region.loop_range_invalid",
            "Region loop bounds must be finite and >= 0, with loopStart < loopEnd when "
            "loopEnabled");
    }

    validateEnvelope(region.amplitudeEnvelope, out);

    for (const auto& mod : region.modMatrix) {
        if (!isFinite(mod.depth)) {
            addError(out, "region.mod_depth_not_finite", "ModMatrixEntry depth must be finite");
        }
        if (!inRange(mod.curve, 0.0f, 1.0f)) {
            addError(out, "region.mod_curve_out_of_range",
                "ModMatrixEntry curve must be finite and within [0, 1]");
        }
        if (mod.source.kind == ModSourceKind::Cc && mod.source.ccNumber > 127) {
            addError(out, "region.mod_cc_out_of_midi_range",
                "ModSource ccNumber must be within the MIDI range [0, 127]");
        }
        if (!mod.sourceControlId.empty() && controlIds.find(mod.sourceControlId) == controlIds.end()) {
            addError(out, "region.mod_source_unknown_control",
                "ModMatrixEntry sourceControlId '" + mod.sourceControlId
                    + "' does not match any ControlMapEntry::id");
        }
    }
}

} // namespace

std::vector<Diagnostic> validate(const InstrumentModel& model)
{
    std::vector<Diagnostic> out;

    if (model.schemaVersion != kModelSchemaVersion) {
        addError(out, "model.schema_version_mismatch",
            "InstrumentModel.schemaVersion does not match kModelSchemaVersion");
    }

    std::unordered_set<std::string> controlIds;
    std::unordered_set<std::string> seenIds;
    for (const auto& control : model.controls) {
        if (control.id.empty()) {
            addError(out, "control.id_missing", "ControlMapEntry.id must not be empty");
            continue;
        }
        if (!seenIds.insert(control.id).second) {
            addError(out, "model.duplicate_control_id",
                "Duplicate ControlMapEntry.id '" + control.id + "'");
        }
        controlIds.insert(control.id);
    }

    for (const auto& region : model.regions) {
        validateRegion(region, controlIds, out);
    }

    return out;
}

} // namespace fbsampler
