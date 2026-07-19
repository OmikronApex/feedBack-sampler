#include "fbsampler/serialize.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace fbsampler {

namespace {

// Sanity cap on serialized container counts: golden files are small; a count
// beyond this is corrupt input, rejected before any allocation is attempted.
constexpr std::size_t kMaxCount = 65536;

std::string escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
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
            } else if (next == 'r') {
                out += '\r';
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

std::string formatDouble(double value)
{
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, result.ptr);
}

// Strict: the whole string must be consumed and parse cleanly. Non-finite
// tokens ("nan"/"inf") are rejected here — no model float field may legally
// hold them, so accepting them would only defer the failure past the parser's
// strictness contract.
bool parseFloat(const std::string& s, float& out)
{
    float value = 0.0f;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parseDouble(const std::string& s, double& out)
{
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parseInt(const std::string& s, int& out)
{
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

bool parseU8(const std::string& s, std::uint8_t& out)
{
    unsigned value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || value > 255) return false;
    out = static_cast<std::uint8_t>(value);
    return true;
}

bool parseCount(const std::string& s, std::size_t& out)
{
    unsigned long long value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || value > kMaxCount) return false;
    out = static_cast<std::size_t>(value);
    return true;
}

bool parseBool(const std::string& s, bool& out)
{
    if (s != "0" && s != "1") return false;
    out = (s == "1");
    return true;
}

const char* toString(SamplePositionUnit unit)
{
    switch (unit) {
        case SamplePositionUnit::Frames: return "Frames";
        case SamplePositionUnit::Seconds: return "Seconds";
    }
    return "Frames";
}

bool parseSamplePositionUnit(const std::string& s, SamplePositionUnit& out)
{
    if (s == "Frames") out = SamplePositionUnit::Frames;
    else if (s == "Seconds") out = SamplePositionUnit::Seconds;
    else return false;
    return true;
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

bool parseModSourceKind(const std::string& s, ModSourceKind& out)
{
    if (s == "Cc") out = ModSourceKind::Cc;
    else if (s == "Velocity") out = ModSourceKind::Velocity;
    else if (s == "KeyTrack") out = ModSourceKind::KeyTrack;
    else return false;
    return true;
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

bool parseModTarget(const std::string& s, ModTarget& out)
{
    if (s == "Gain") out = ModTarget::Gain;
    else if (s == "Pitch") out = ModTarget::Pitch;
    else if (s == "Pan") out = ModTarget::Pan;
    else return false;
    return true;
}

void writeLine(std::string& out, const std::string& key, const std::string& value)
{
    out += key;
    out += '=';
    out += value;
    out += '\n';
}

void writeLine(std::string& out, const std::string& key, float value) { writeLine(out, key, formatFloat(value)); }
void writeLine(std::string& out, const std::string& key, double value) { writeLine(out, key, formatDouble(value)); }
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
        writeLine(out, p + "bend_up_cents", r.bendUpCents);
        writeLine(out, p + "bend_down_cents", r.bendDownCents);
        writeLine(out, p + "position_unit", std::string(toString(r.positionUnit)));
        writeLine(out, p + "offset", r.offset);
        writeLine(out, p + "loop_enabled", r.loopEnabled);
        writeLine(out, p + "loop_start", r.loopStart);
        writeLine(out, p + "loop_end", r.loopEnd);
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

// Strict by design (SPEC.md#Serialization): every declared field must be
// present exactly once with a cleanly parseable value, and no unknown keys may
// remain. Semantic checks (schema version match, ranges) belong to validate().
bool parseModel(const std::string& text, InstrumentModel& out)
try {
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
        if (!fields.emplace(line.substr(0, eq), line.substr(eq + 1)).second) {
            return false; // duplicate key
        }
    }

    // Consumes the field so that leftover (unknown/stray) keys are detectable.
    auto take = [&](const std::string& key, std::string& value) -> bool {
        auto it = fields.find(key);
        if (it == fields.end()) return false;
        value = std::move(it->second);
        fields.erase(it);
        return true;
    };

    std::string v;
    InstrumentModel model;
    if (!take("schema_version", v) || !parseInt(v, model.schemaVersion)) return false;
    if (!take("name", v)) return false;
    model.name = unescape(v);

    std::size_t numRegions = 0;
    if (!take("region_count", v) || !parseCount(v, numRegions)) return false;
    model.regions.resize(numRegions);
    for (std::size_t i = 0; i < numRegions; ++i) {
        auto& r = model.regions[i];
        const auto p = prefix("region", i);
        if (!take(p + "sample_file", v)) return false;
        r.sampleFile = unescape(v);
        if (!take(p + "lo_key", v) || !parseU8(v, r.loKey)) return false;
        if (!take(p + "hi_key", v) || !parseU8(v, r.hiKey)) return false;
        if (!take(p + "lo_velocity", v) || !parseU8(v, r.loVelocity)) return false;
        if (!take(p + "hi_velocity", v) || !parseU8(v, r.hiVelocity)) return false;
        if (!take(p + "root_key", v) || !parseU8(v, r.rootKey)) return false;
        if (!take(p + "tuning_cents", v) || !parseFloat(v, r.tuningCents)) return false;
        if (!take(p + "gain_db", v) || !parseFloat(v, r.gainDb)) return false;
        if (!take(p + "pan", v) || !parseFloat(v, r.pan)) return false;
        if (!take(p + "bend_up_cents", v) || !parseFloat(v, r.bendUpCents)) return false;
        if (!take(p + "bend_down_cents", v) || !parseFloat(v, r.bendDownCents)) return false;
        if (!take(p + "position_unit", v) || !parseSamplePositionUnit(v, r.positionUnit)) return false;
        if (!take(p + "offset", v) || !parseDouble(v, r.offset)) return false;
        if (!take(p + "loop_enabled", v) || !parseBool(v, r.loopEnabled)) return false;
        if (!take(p + "loop_start", v) || !parseDouble(v, r.loopStart)) return false;
        if (!take(p + "loop_end", v) || !parseDouble(v, r.loopEnd)) return false;
        if (!take(p + "env.delay_seconds", v) || !parseFloat(v, r.amplitudeEnvelope.delaySeconds)) return false;
        if (!take(p + "env.attack_seconds", v) || !parseFloat(v, r.amplitudeEnvelope.attackSeconds)) return false;
        if (!take(p + "env.hold_seconds", v) || !parseFloat(v, r.amplitudeEnvelope.holdSeconds)) return false;
        if (!take(p + "env.decay_seconds", v) || !parseFloat(v, r.amplitudeEnvelope.decaySeconds)) return false;
        if (!take(p + "env.sustain_level", v) || !parseFloat(v, r.amplitudeEnvelope.sustainLevel)) return false;
        if (!take(p + "env.release_seconds", v) || !parseFloat(v, r.amplitudeEnvelope.releaseSeconds)) return false;

        std::size_t numMods = 0;
        if (!take(p + "mod_count", v) || !parseCount(v, numMods)) return false;
        r.modMatrix.resize(numMods);
        for (std::size_t j = 0; j < numMods; ++j) {
            auto& m = r.modMatrix[j];
            const auto mp = p + "mod[" + std::to_string(j) + "].";
            if (!take(mp + "source_control_id", v)) return false;
            m.sourceControlId = unescape(v);
            if (!take(mp + "source_kind", v) || !parseModSourceKind(v, m.source.kind)) return false;
            if (!take(mp + "source_cc_number", v) || !parseU8(v, m.source.ccNumber)) return false;
            if (!take(mp + "target", v) || !parseModTarget(v, m.target)) return false;
            if (!take(mp + "depth", v) || !parseFloat(v, m.depth)) return false;
            if (!take(mp + "curve", v) || !parseFloat(v, m.curve)) return false;
        }
    }

    std::size_t numControls = 0;
    if (!take("control_count", v) || !parseCount(v, numControls)) return false;
    model.controls.resize(numControls);
    for (std::size_t i = 0; i < numControls; ++i) {
        auto& c = model.controls[i];
        const auto p = prefix("control", i);
        if (!take(p + "id", v)) return false;
        c.id = unescape(v);
        if (!take(p + "display_name", v)) return false;
        c.displayName = unescape(v);
        if (!take(p + "accessible_name", v)) return false;
        c.accessibleName = unescape(v);
    }

    if (!fields.empty()) return false; // unknown/stray keys

    out = std::move(model);
    return true;
} catch (...) {
    // No exceptions cross the core API (spine Consistency Conventions). All
    // parsing is exception-free (std::from_chars); this only guards allocation
    // failure in container/string operations.
    return false;
}

} // namespace fbsampler
