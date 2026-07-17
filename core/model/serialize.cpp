#include "fbsampler/serialize.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace fbsampler {

namespace {

std::string escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char next = s[i + 1];
            if (next == '\\') {
                out += '\\';
                ++i;
            } else if (next == 'n') {
                out += '\n';
                ++i;
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Shortest round-trip, locale-independent (SPEC.md#Serialization) — never
// std::to_string/iostream default formatting.
std::string formatFloat(float value)
{
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, result.ptr);
}

float parseFloat(const std::string& s)
{
    float value = 0.0f;
    std::from_chars(s.data(), s.data() + s.size(), value);
    return value;
}

const char* toString(ModSourceKind kind)
{
    switch (kind) {
        case ModSourceKind::Cc: return "Cc";
        case ModSourceKind::Velocity: return "Velocity";
        case ModSourceKind::KeyTrack: return "KeyTrack";
    }
    return "Velocity";
}

ModSourceKind parseModSourceKind(const std::string& s)
{
    if (s == "Cc") return ModSourceKind::Cc;
    if (s == "KeyTrack") return ModSourceKind::KeyTrack;
    return ModSourceKind::Velocity;
}

const char* toString(ModTarget target)
{
    switch (target) {
        case ModTarget::Gain: return "Gain";
        case ModTarget::Pitch: return "Pitch";
        case ModTarget::Pan: return "Pan";
    }
    return "Gain";
}

ModTarget parseModTarget(const std::string& s)
{
    if (s == "Pitch") return ModTarget::Pitch;
    if (s == "Pan") return ModTarget::Pan;
    return ModTarget::Gain;
}

void writeLine(std::string& out, const std::string& key, const std::string& value)
{
    out += key;
    out += '=';
    out += value;
    out += '\n';
}

void writeLine(std::string& out, const std::string& key, float value) { writeLine(out, key, formatFloat(value)); }
void writeLine(std::string& out, const std::string& key, int value) { writeLine(out, key, std::to_string(value)); }
void writeLine(std::string& out, const std::string& key, unsigned value) { writeLine(out, key, std::to_string(value)); }
void writeLine(std::string& out, const std::string& key, bool value) { writeLine(out, key, std::string(value ? "1" : "0")); }

std::string prefix(const std::string& container, std::size_t index)
{
    return container + "[" + std::to_string(index) + "].";
}

} // namespace

std::string serializeModel(const InstrumentModel& model)
{
    std::string out;
    writeLine(out, "schema_version", model.schemaVersion);
    writeLine(out, "name", escape(model.name));

    writeLine(out, "region_count", static_cast<unsigned>(model.regions.size()));
    for (std::size_t i = 0; i < model.regions.size(); ++i) {
        const auto& r = model.regions[i];
        const auto p = prefix("region", i);
        writeLine(out, p + "sample_file", escape(r.sampleFile));
        writeLine(out, p + "lo_key", static_cast<unsigned>(r.loKey));
        writeLine(out, p + "hi_key", static_cast<unsigned>(r.hiKey));
        writeLine(out, p + "lo_velocity", static_cast<unsigned>(r.loVelocity));
        writeLine(out, p + "hi_velocity", static_cast<unsigned>(r.hiVelocity));
        writeLine(out, p + "root_key", static_cast<unsigned>(r.rootKey));
        writeLine(out, p + "tuning_cents", r.tuningCents);
        writeLine(out, p + "gain_db", r.gainDb);
        writeLine(out, p + "pan", r.pan);
        writeLine(out, p + "offset_seconds", r.offsetSeconds);
        writeLine(out, p + "loop_enabled", r.loopEnabled);
        writeLine(out, p + "loop_start_seconds", r.loopStartSeconds);
        writeLine(out, p + "loop_end_seconds", r.loopEndSeconds);
        writeLine(out, p + "env.delay_seconds", r.amplitudeEnvelope.delaySeconds);
        writeLine(out, p + "env.attack_seconds", r.amplitudeEnvelope.attackSeconds);
        writeLine(out, p + "env.hold_seconds", r.amplitudeEnvelope.holdSeconds);
        writeLine(out, p + "env.decay_seconds", r.amplitudeEnvelope.decaySeconds);
        writeLine(out, p + "env.sustain_level", r.amplitudeEnvelope.sustainLevel);
        writeLine(out, p + "env.release_seconds", r.amplitudeEnvelope.releaseSeconds);

        writeLine(out, p + "mod_count", static_cast<unsigned>(r.modMatrix.size()));
        for (std::size_t j = 0; j < r.modMatrix.size(); ++j) {
            const auto& m = r.modMatrix[j];
            const auto mp = p + "mod[" + std::to_string(j) + "].";
            writeLine(out, mp + "source_control_id", escape(m.sourceControlId));
            writeLine(out, mp + "source_kind", std::string(toString(m.source.kind)));
            writeLine(out, mp + "source_cc_number", static_cast<unsigned>(m.source.ccNumber));
            writeLine(out, mp + "target", std::string(toString(m.target)));
            writeLine(out, mp + "depth", m.depth);
            writeLine(out, mp + "curve", m.curve);
        }
    }

    writeLine(out, "control_count", static_cast<unsigned>(model.controls.size()));
    for (std::size_t i = 0; i < model.controls.size(); ++i) {
        const auto& c = model.controls[i];
        const auto p = prefix("control", i);
        writeLine(out, p + "id", escape(c.id));
        writeLine(out, p + "display_name", escape(c.displayName));
        writeLine(out, p + "accessible_name", escape(c.accessibleName));
    }

    return out;
}

bool parseModel(const std::string& text, InstrumentModel& out)
{
    std::unordered_map<std::string, std::string> fields;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) return false;
        fields[line.substr(0, eq)] = line.substr(eq + 1);
    }

    auto require = [&](const std::string& key) -> const std::string* {
        auto it = fields.find(key);
        return it == fields.end() ? nullptr : &it->second;
    };

    try {
        const auto* schemaVersion = require("schema_version");
        const auto* regionCount = require("region_count");
        const auto* controlCount = require("control_count");
        if (!schemaVersion || !regionCount || !controlCount) return false;

        InstrumentModel model;
        model.schemaVersion = std::stoi(*schemaVersion);
        if (const auto* name = require("name")) model.name = unescape(*name);

        const auto numRegions = static_cast<std::size_t>(std::stoul(*regionCount));
        model.regions.resize(numRegions);
        for (std::size_t i = 0; i < numRegions; ++i) {
            auto& r = model.regions[i];
            const auto p = prefix("region", i);
            if (const auto* v = require(p + "sample_file")) r.sampleFile = unescape(*v);
            if (const auto* v = require(p + "lo_key")) r.loKey = static_cast<std::uint8_t>(std::stoul(*v));
            if (const auto* v = require(p + "hi_key")) r.hiKey = static_cast<std::uint8_t>(std::stoul(*v));
            if (const auto* v = require(p + "lo_velocity")) r.loVelocity = static_cast<std::uint8_t>(std::stoul(*v));
            if (const auto* v = require(p + "hi_velocity")) r.hiVelocity = static_cast<std::uint8_t>(std::stoul(*v));
            if (const auto* v = require(p + "root_key")) r.rootKey = static_cast<std::uint8_t>(std::stoul(*v));
            if (const auto* v = require(p + "tuning_cents")) r.tuningCents = parseFloat(*v);
            if (const auto* v = require(p + "gain_db")) r.gainDb = parseFloat(*v);
            if (const auto* v = require(p + "pan")) r.pan = parseFloat(*v);
            if (const auto* v = require(p + "offset_seconds")) r.offsetSeconds = parseFloat(*v);
            if (const auto* v = require(p + "loop_enabled")) r.loopEnabled = (*v == "1");
            if (const auto* v = require(p + "loop_start_seconds")) r.loopStartSeconds = parseFloat(*v);
            if (const auto* v = require(p + "loop_end_seconds")) r.loopEndSeconds = parseFloat(*v);
            if (const auto* v = require(p + "env.delay_seconds")) r.amplitudeEnvelope.delaySeconds = parseFloat(*v);
            if (const auto* v = require(p + "env.attack_seconds")) r.amplitudeEnvelope.attackSeconds = parseFloat(*v);
            if (const auto* v = require(p + "env.hold_seconds")) r.amplitudeEnvelope.holdSeconds = parseFloat(*v);
            if (const auto* v = require(p + "env.decay_seconds")) r.amplitudeEnvelope.decaySeconds = parseFloat(*v);
            if (const auto* v = require(p + "env.sustain_level")) r.amplitudeEnvelope.sustainLevel = parseFloat(*v);
            if (const auto* v = require(p + "env.release_seconds")) r.amplitudeEnvelope.releaseSeconds = parseFloat(*v);

            const auto* modCount = require(p + "mod_count");
            if (!modCount) return false;
            const auto numMods = static_cast<std::size_t>(std::stoul(*modCount));
            r.modMatrix.resize(numMods);
            for (std::size_t j = 0; j < numMods; ++j) {
                auto& m = r.modMatrix[j];
                const auto mp = p + "mod[" + std::to_string(j) + "].";
                if (const auto* v = require(mp + "source_control_id")) m.sourceControlId = unescape(*v);
                if (const auto* v = require(mp + "source_kind")) m.source.kind = parseModSourceKind(*v);
                if (const auto* v = require(mp + "source_cc_number")) m.source.ccNumber = static_cast<std::uint8_t>(std::stoul(*v));
                if (const auto* v = require(mp + "target")) m.target = parseModTarget(*v);
                if (const auto* v = require(mp + "depth")) m.depth = parseFloat(*v);
                if (const auto* v = require(mp + "curve")) m.curve = parseFloat(*v);
            }
        }

        const auto numControls = static_cast<std::size_t>(std::stoul(*controlCount));
        model.controls.resize(numControls);
        for (std::size_t i = 0; i < numControls; ++i) {
            auto& c = model.controls[i];
            const auto p = prefix("control", i);
            if (const auto* v = require(p + "id")) c.id = unescape(*v);
            if (const auto* v = require(p + "display_name")) c.displayName = unescape(*v);
            if (const auto* v = require(p + "accessible_name")) c.accessibleName = unescape(*v);
        }

        out = std::move(model);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fbsampler
