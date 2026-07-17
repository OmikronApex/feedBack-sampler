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

void validateEnvelope(const EnvelopeADSR& env, std::vector<Diagnostic>& out)
{
    if (env.delaySeconds < 0.0f || env.attackSeconds < 0.0f || env.holdSeconds < 0.0f
        || env.decaySeconds < 0.0f || env.releaseSeconds < 0.0f) {
        addError(out, "region.envelope_time_negative",
            "Envelope time field must be >= 0");
    }
    if (env.sustainLevel < 0.0f || env.sustainLevel > 1.0f) {
        addError(out, "region.envelope_sustain_out_of_range",
            "Envelope sustainLevel must be within [0, 1]");
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
    if (region.loVelocity > region.hiVelocity) {
        addError(out, "region.velocity_range_invalid", "Region loVelocity must be <= hiVelocity");
    }
    if (region.pan < -1.0f || region.pan > 1.0f) {
        addError(out, "region.pan_out_of_range", "Region pan must be within [-1, 1]");
    }
    if (!isFinite(region.gainDb)) {
        addError(out, "region.gain_not_finite", "Region gainDb must be finite");
    }
    if (!isFinite(region.tuningCents)) {
        addError(out, "region.tuning_not_finite", "Region tuningCents must be finite");
    }
    if (region.offsetSeconds < 0.0f) {
        addError(out, "region.offset_negative", "Region offsetSeconds must be >= 0");
    }
    if (region.loopEnabled) {
        if (region.loopStartSeconds < 0.0f || region.loopEndSeconds < 0.0f
            || region.loopStartSeconds >= region.loopEndSeconds) {
            addError(out, "region.loop_range_invalid",
                "Region loop range must have 0 <= loopStartSeconds < loopEndSeconds");
        }
    }

    validateEnvelope(region.amplitudeEnvelope, out);

    for (const auto& mod : region.modMatrix) {
        if (!isFinite(mod.depth)) {
            addError(out, "region.mod_depth_not_finite", "ModMatrixEntry depth must be finite");
        }
        if (mod.curve < 0.0f || mod.curve > 1.0f) {
            addError(out, "region.mod_curve_out_of_range",
                "ModMatrixEntry curve must be within [0, 1]");
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
