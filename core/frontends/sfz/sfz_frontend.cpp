// SFZ frontend: lowers sfizz's parse stream into the canonical model (AD-1).
//
// This deliberately does NOT parse SFZ itself — sfizz 1.2.3's sfz::Parser
// (its tooling-oriented listener seam, no Synth involved) handles the format,
// including #include / #define expansion. This file only translates the
// opcode stream into fbsampler::InstrumentModel with spec units
// (cents / dB / seconds, SPEC.md#Units) and structured Diagnostics.
//
// Everything format-specific ends here: no SFZ-private flag may leak into the
// model (AD-11); unsupported-but-harmless opcodes become warnings, never
// silent skips (AD-1).

#include "fbsampler/sfz_frontend.h"

#include "fbsampler/validate.h"

#include "Opcode.h"
#include "parser/Parser.h"
#include "parser/ParserListener.h"

// sfizz's Parser API is expressed in terms of the `fs` alias from
// <ghc/fs_std.hpp> (std::filesystem where available, ghc::filesystem
// otherwise). Include it explicitly — this file uses `fs` directly and must
// name the same type the Parser API takes, not rely on transitive leakage.
#include <ghc/fs_std.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fbsampler {

namespace {

// SFZ v1 ranges for tuning opcodes (sfzformat.com). Clamping here keeps the
// derived tuningCents finite for any finite input — huge values would
// otherwise overflow to infinity in `transpose*100 + tune` and trip
// validation, suppressing the whole instrument for one bad opcode.
constexpr float kTransposeMaxSemitones = 127.0f;
constexpr float kTuneMaxCents = 9600.0f;

Diagnostic makeDiag(Severity severity, std::string code, std::string message,
                    SourceLocation location)
{
    Diagnostic d;
    d.severity = severity;
    d.code = std::move(code);
    d.message = std::move(message);
    d.location = std::move(location);
    return d;
}

SourceLocation toLocation(const sfz::SourceRange& range)
{
    SourceLocation loc;
    if (range.start) {
        loc.file = range.start.filePath->string();
        // sfizz Reader counts from 0; Diagnostics use the human 1-based
        // convention.
        loc.line = static_cast<int>(range.start.lineNumber) + 1;
        loc.column = static_cast<int>(range.start.columnNumber) + 1;
    }
    return loc;
}

bool parseFloatValue(const std::string& s, float& out)
{
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return false;
    const auto end = s.find_last_not_of(" \t") + 1;
    float value = 0.0f;
    const auto result = std::from_chars(s.data() + begin, s.data() + end, value);
    if (result.ec != std::errc() || result.ptr != s.data() + end || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

bool parseIntValue(const std::string& s, int& out)
{
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return false;
    const auto end = s.find_last_not_of(" \t") + 1;
    int value = 0;
    const auto result = std::from_chars(s.data() + begin, s.data() + end, value);
    if (result.ec != std::errc() || result.ptr != s.data() + end)
        return false;
    out = value;
    return true;
}

// SFZ v1 allows note names ("c4", "f#3", "bb2") wherever a key number fits.
// Octave -1 starts at MIDI 0 (c-1 == 0, c4 == 60), matching sfizz.
bool parseNoteName(const std::string& s, int& out)
{
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return false;
    const auto end = s.find_last_not_of(" \t") + 1;
    std::size_t i = begin;

    static constexpr int kIndex[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a..g
    const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    if (letter < 'a' || letter > 'g')
        return false;
    int note = kIndex[letter - 'a'];
    ++i;

    if (i < end && (s[i] == '#' || s[i] == 'b')) {
        note += (s[i] == '#') ? 1 : -1;
        ++i;
    }

    if (i >= end)
        return false;
    int octave = 0;
    const auto result = std::from_chars(s.data() + i, s.data() + end, octave);
    if (result.ec != std::errc() || result.ptr != s.data() + end)
        return false;

    out = (octave + 1) * 12 + note;
    return true;
}

bool parseKeyValue(const std::string& s, int& out)
{
    return parseIntValue(s, out) || parseNoteName(s, out);
}

// Replaces SFZ's habitual backslash separators so sample paths stay portable
// references relative to the .sfz root (SPEC.md#Region).
std::string normalizeSamplePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// Platform-independent: SFZ files travel between OSes, so a POSIX-rooted
// ("/x") or drive-lettered ("C:...") reference must count as absolute on every
// platform — fs::path::is_absolute() would say no for "/x" on Windows and for
// "C:..." on POSIX.
bool isAbsoluteSamplePath(const std::string& normalized)
{
    if (!normalized.empty() && normalized.front() == '/')
        return true;
    return normalized.size() >= 2 && normalized[1] == ':'
        && std::isalpha(static_cast<unsigned char>(normalized[0]));
}

// Locale-independent float formatting for diagnostic messages: the model's
// determinism story (SPEC.md#Serialization) forbids std::to_string, whose
// output depends on the C locale (e.g. ',' decimal separator).
std::string formatFloat(float value)
{
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, result.ptr);
}

struct LocatedOpcode {
    std::string name;
    std::string value;
    SourceLocation location;      // of the opcode name (fallback anchor)
    SourceLocation valueLocation; // of the value token — bad-value diagnostics point here
};

/// Listener that assembles the <global>/<master>/<group>/<region> hierarchy
/// from the low-level callbacks (the only ones carrying source ranges) and
/// flattens it per region: later scopes override earlier ones opcode-by-opcode
/// in stream order, which is exactly SFZ v1 inheritance.
class Lowerer : public sfz::ParserListener {
public:
    Lowerer(bool checkSampleExistence, std::vector<Diagnostic>& diags)
        : checkSampleExistence_(checkSampleExistence)
        , diags_(diags)
    {
    }

    InstrumentModel takeModel() { return std::move(model_); }

    bool sawError() const { return sawError_; }

    void onParseBegin() override {}

    void onParseEnd() override { flushCurrentHeader(); }

    void onParseHeader(const sfz::SourceRange& range, const std::string& header) override
    {
        flushCurrentHeader();
        currentHeader_ = header;
        currentHeaderLocation_ = toLocation(range);
        currentOpcodes_.clear();

        if (header == "region") {
            // handled on flush
        } else if (header == "group") {
            groupOps_.clear();
        } else if (header == "master") {
            masterOps_.clear();
            groupOps_.clear();
        } else if (header == "global") {
            globalOps_.clear();
            masterOps_.clear();
            groupOps_.clear();
        } else if (header == "control") {
            // handled on flush
        } else {
            warn("sfz.header_unsupported",
                 "unsupported header <" + header + "> ignored", toLocation(range));
        }
    }

    void onParseOpcode(const sfz::SourceRange& rangeOpcode, const sfz::SourceRange& rangeValue,
                       const std::string& name, const std::string& value) override
    {
        currentOpcodes_.push_back({ name, value, toLocation(rangeOpcode), toLocation(rangeValue) });
    }

    void onParseError(const sfz::SourceRange& range, const std::string& message) override
    {
        sawError_ = true;
        diags_.push_back(makeDiag(Severity::Error, "sfz.parse_error", message, toLocation(range)));
    }

    void onParseWarning(const sfz::SourceRange& range, const std::string& message) override
    {
        warn("sfz.parse_warning", message, toLocation(range));
    }

private:
    void warn(std::string code, std::string message, SourceLocation location)
    {
        diags_.push_back(makeDiag(Severity::Warning, std::move(code), std::move(message),
                                  std::move(location)));
    }

    void flushCurrentHeader()
    {
        if (currentHeader_ == "region")
            lowerRegion();
        else if (currentHeader_ == "control")
            lowerControl();
        else if (currentHeader_ == "global")
            globalOps_ = std::move(currentOpcodes_);
        else if (currentHeader_ == "master")
            masterOps_ = std::move(currentOpcodes_);
        else if (currentHeader_ == "group")
            groupOps_ = std::move(currentOpcodes_);
        else if (!currentHeader_.empty()) {
            // Opcodes inside an unsupported header would otherwise vanish
            // behind the single header warning (AD-1: never silent) —
            // surface each one individually.
            for (const auto& op : currentOpcodes_)
                warn("sfz.opcode_unsupported",
                     "opcode '" + op.name + "' inside unsupported header <" + currentHeader_
                         + "> ignored",
                     op.location);
        }

        currentHeader_.clear();
        currentOpcodes_.clear();
    }

    void lowerControl()
    {
        for (const auto& op : currentOpcodes_) {
            if (op.name == "default_path") {
                defaultPath_ = normalizeSamplePath(op.value);
                if (!defaultPath_.empty() && defaultPath_.back() != '/')
                    defaultPath_ += '/';
            } else if (op.name.rfind("label_cc", 0) == 0) {
                lowerCcLabel(op);
            } else {
                warn("sfz.opcode_unsupported",
                     "unsupported <control> opcode '" + op.name + "' ignored", op.location);
            }
        }
    }

    void lowerCcLabel(const LocatedOpcode& op)
    {
        int cc = 0;
        const std::string digits = op.name.substr(8); // after "label_cc"
        if (!parseIntValue(digits, cc) || cc < 0 || cc > 127) {
            warn("sfz.invalid_opcode_value",
                 "'" + op.name + "' does not name a CC in [0, 127]", op.location);
            return;
        }
        // AD-8: stable identity = CC number; the label is presentation.
        const std::string id = "cc" + std::to_string(cc);
        for (auto& control : model_.controls) {
            if (control.id == id) {
                control.displayName = op.value;
                control.accessibleName = op.value; // AD-11
                return;
            }
        }
        ControlMapEntry entry;
        entry.id = id;
        entry.displayName = op.value;
        entry.accessibleName = op.value; // AD-11: accessibility survives lowering
        model_.controls.push_back(entry);
    }

    float parseFloatOr(const LocatedOpcode& op, float fallback)
    {
        float value = fallback;
        if (!parseFloatValue(op.value, value)) {
            warn("sfz.invalid_opcode_value",
                 "'" + op.name + "=" + op.value + "' is not a finite number", op.valueLocation);
            return fallback;
        }
        return value;
    }

    std::uint8_t parseMidiOr(const LocatedOpcode& op, std::uint8_t fallback)
    {
        int value = 0;
        if (!parseKeyValue(op.value, value)) {
            warn("sfz.invalid_opcode_value",
                 "'" + op.name + "=" + op.value + "' is not a MIDI number or note name",
                 op.valueLocation);
            return fallback;
        }
        if (value < 0 || value > 127) {
            warn("sfz.value_clamped",
                 "'" + op.name + "=" + op.value + "' clamped to the MIDI range [0, 127]",
                 op.valueLocation);
            value = std::clamp(value, 0, 127);
        }
        return static_cast<std::uint8_t>(value);
    }

    float clampWarn(const LocatedOpcode& op, float value, float lo, float hi)
    {
        if (value < lo || value > hi) {
            warn("sfz.value_clamped",
                 "'" + op.name + "=" + op.value + "' clamped to [" + formatFloat(lo) + ", "
                     + formatFloat(hi) + "]",
                 op.valueLocation);
            return std::clamp(value, lo, hi);
        }
        return value;
    }

    // Sample-position opcodes (offset / loop points) are SFZ frame counts and
    // are stored as frames verbatim (Region::positionUnit == Frames); the
    // engine converts once the sample's real rate is known (Story 1.4, AD-2).
    // Unparseable values keep the current (possibly scope-inherited) value,
    // consistent with every other opcode's fallback policy.
    double parseFramesOr(const LocatedOpcode& op, double current)
    {
        const float parsed = parseFloatOr(op, static_cast<float>(current));
        return std::max(0.0, static_cast<double>(parsed));
    }

    void lowerRegion()
    {
        Region region;
        float transposeSemitones = 0.0f;
        float tuneCents = 0.0f;
        SourceLocation regionLocation = currentHeaderLocation_;

        // SFZ inheritance: global, then master, then group, then the region's
        // own opcodes; later assignments win.
        std::vector<const LocatedOpcode*> merged;
        merged.reserve(globalOps_.size() + masterOps_.size() + groupOps_.size()
                       + currentOpcodes_.size());
        for (const auto* scope : { &globalOps_, &masterOps_, &groupOps_, &currentOpcodes_ })
            for (const auto& op : *scope)
                merged.push_back(&op);

        for (const auto* opPtr : merged) {
            const auto& op = *opPtr;
            const std::string& name = op.name;

            if (name == "sample") {
                // default_path applies to relative references only; an
                // absolute sample path must pass through untouched.
                const std::string normalized = normalizeSamplePath(op.value);
                region.sampleFile = isAbsoluteSamplePath(normalized)
                    ? normalized
                    : defaultPath_ + normalized;
            } else if (name == "lokey") {
                region.loKey = parseMidiOr(op, region.loKey);
            } else if (name == "hikey") {
                region.hiKey = parseMidiOr(op, region.hiKey);
            } else if (name == "key") {
                // On an unparseable value, leave the (possibly scope-inherited)
                // range untouched instead of collapsing lo/hi to the fallback.
                int key = 0;
                if (!parseKeyValue(op.value, key)) {
                    warn("sfz.invalid_opcode_value",
                         "'" + name + "=" + op.value + "' is not a MIDI number or note name",
                         op.valueLocation);
                } else {
                    if (key < 0 || key > 127) {
                        warn("sfz.value_clamped",
                             "'" + name + "=" + op.value + "' clamped to the MIDI range [0, 127]",
                             op.valueLocation);
                        key = std::clamp(key, 0, 127);
                    }
                    region.loKey = static_cast<std::uint8_t>(key);
                    region.hiKey = static_cast<std::uint8_t>(key);
                    region.rootKey = static_cast<std::uint8_t>(key);
                }
            } else if (name == "lovel") {
                region.loVelocity = parseMidiOr(op, region.loVelocity);
            } else if (name == "hivel") {
                region.hiVelocity = parseMidiOr(op, region.hiVelocity);
            } else if (name == "pitch_keycenter") {
                region.rootKey = parseMidiOr(op, region.rootKey);
            } else if (name == "transpose") {
                transposeSemitones = clampWarn(op, parseFloatOr(op, transposeSemitones),
                                               -kTransposeMaxSemitones, kTransposeMaxSemitones);
            } else if (name == "tune") {
                tuneCents = clampWarn(op, parseFloatOr(op, tuneCents),
                                      -kTuneMaxCents, kTuneMaxCents);
            } else if (name == "bend_up" || name == "bendup") {
                region.bendUpCents = clampWarn(op, parseFloatOr(op, region.bendUpCents),
                                               -kTuneMaxCents, kTuneMaxCents);
            } else if (name == "bend_down" || name == "benddown") {
                region.bendDownCents = clampWarn(op, parseFloatOr(op, region.bendDownCents),
                                                 -kTuneMaxCents, kTuneMaxCents);
            } else if (name == "volume") {
                region.gainDb = parseFloatOr(op, region.gainDb);
            } else if (name == "pan") {
                // SFZ pan is -100..100; the model normalizes to -1..1.
                region.pan = clampWarn(op, parseFloatOr(op, region.pan * 100.0f), -100.0f, 100.0f)
                    / 100.0f;
            } else if (name == "offset") {
                region.offset = parseFramesOr(op, region.offset);
            } else if (name == "loop_mode" || name == "loopmode") {
                if (op.value == "loop_continuous" || op.value == "loop_sustain") {
                    region.loopEnabled = true;
                } else if (op.value == "no_loop" || op.value == "one_shot") {
                    region.loopEnabled = false;
                } else {
                    warn("sfz.invalid_opcode_value",
                         "'" + name + "=" + op.value + "' is not an SFZ v1 loop mode",
                         op.location);
                }
            } else if (name == "loop_start" || name == "loopstart") {
                region.loopStart = parseFramesOr(op, region.loopStart);
            } else if (name == "loop_end" || name == "loopend") {
                region.loopEnd = parseFramesOr(op, region.loopEnd);
            } else if (name == "ampeg_delay") {
                region.amplitudeEnvelope.delaySeconds =
                    std::max(0.0f, parseFloatOr(op, region.amplitudeEnvelope.delaySeconds));
            } else if (name == "ampeg_attack") {
                region.amplitudeEnvelope.attackSeconds =
                    std::max(0.0f, parseFloatOr(op, region.amplitudeEnvelope.attackSeconds));
            } else if (name == "ampeg_hold") {
                region.amplitudeEnvelope.holdSeconds =
                    std::max(0.0f, parseFloatOr(op, region.amplitudeEnvelope.holdSeconds));
            } else if (name == "ampeg_decay") {
                region.amplitudeEnvelope.decaySeconds =
                    std::max(0.0f, parseFloatOr(op, region.amplitudeEnvelope.decaySeconds));
            } else if (name == "ampeg_sustain") {
                // SFZ sustain is a percentage 0..100; the model is 0..1.
                region.amplitudeEnvelope.sustainLevel =
                    clampWarn(op,
                              parseFloatOr(op, region.amplitudeEnvelope.sustainLevel * 100.0f),
                              0.0f, 100.0f)
                    / 100.0f;
            } else if (name == "ampeg_release") {
                region.amplitudeEnvelope.releaseSeconds =
                    std::max(0.0f, parseFloatOr(op, region.amplitudeEnvelope.releaseSeconds));
            } else {
                // Unknown or v2/ARIA opcode: tracked fidelity gap, never a
                // silent skip (AD-1). Coverage grows via corpus work.
                warn("sfz.opcode_unsupported",
                     "unsupported opcode '" + name + "' ignored", op.location);
            }
        }

        region.tuningCents = transposeSemitones * 100.0f + tuneCents;

        if (region.loKey > region.hiKey) {
            warn("sfz.key_range_swapped",
                 "lokey > hikey; range swapped to stay playable", regionLocation);
            std::swap(region.loKey, region.hiKey);
        }
        if (region.loVelocity > region.hiVelocity) {
            warn("sfz.velocity_range_swapped",
                 "lovel > hivel; range swapped to stay playable", regionLocation);
            std::swap(region.loVelocity, region.hiVelocity);
        }
        if (region.loopEnabled
            && !(region.loopStart >= 0.0 && region.loopStart < region.loopEnd)) {
            warn("sfz.loop_range_invalid",
                 "loop enabled but loop points do not satisfy 0 <= start < end; loop disabled",
                 regionLocation);
            region.loopEnabled = false;
        }

        if (region.sampleFile.empty()) {
            // A region without a sample cannot sound; drop it (players ignore
            // such regions) but never silently.
            warn("sfz.region_missing_sample",
                 "<region> has no 'sample' opcode; region dropped", regionLocation);
            return;
        }

        if (checkSampleExistence_) {
            const fs::path resolved = baseDir_ / fs::u8path(region.sampleFile);
            std::error_code ec;
            if (!fs::exists(resolved, ec) || ec) {
                // FR-5-style reporting: a missing sample must not abort the
                // instrument — the region stays, flagged.
                warn("sfz.sample_file_missing",
                     "sample file not found: " + region.sampleFile, regionLocation);
            }
        }

        model_.regions.push_back(std::move(region));
    }

public:
    void setBaseDirectory(fs::path dir) { baseDir_ = std::move(dir); }
    void setName(std::string name) { model_.name = std::move(name); }

private:
    bool checkSampleExistence_ = false;
    fs::path baseDir_;
    std::vector<Diagnostic>& diags_;

    InstrumentModel model_;
    std::string defaultPath_;

    std::string currentHeader_;
    SourceLocation currentHeaderLocation_;
    std::vector<LocatedOpcode> currentOpcodes_;
    std::vector<LocatedOpcode> globalOps_;
    std::vector<LocatedOpcode> masterOps_;
    std::vector<LocatedOpcode> groupOps_;

    bool sawError_ = false;
};

LowerResult lowerImpl(const fs::path& path, const std::string* textOrNull)
{
    LowerResult result;

    Lowerer lowerer(/*checkSampleExistence=*/textOrNull == nullptr, result.diagnostics);
    lowerer.setBaseDirectory(path.parent_path());
    lowerer.setName(path.stem().u8string());

    sfz::Parser parser;
    parser.setListener(&lowerer);
    parser.setRecursiveIncludeGuardEnabled(true);
    if (textOrNull != nullptr) {
        // No file-system access in text mode: the root "file" itself occupies
        // the one allowed include slot, so any #include hits the depth limit
        // and fails as a parse error instead of touching the disk (AD-10).
        parser.setMaximumIncludeDepth(1);
        parser.parseString(path, *textOrNull);
    } else {
        parser.parseFile(path);
    }

    const bool hasError = lowerer.sawError()
        || std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
    if (hasError)
        return result;

    InstrumentModel model = lowerer.takeModel();

    // The lowered model must honor the Story-1.2 contract; clamping above is
    // designed to make violations unreachable, but verify anyway so a lowering
    // bug surfaces as Diagnostics, not as a corrupt model downstream.
    auto validationDiags = validate(model);
    const bool validationFailed = !validationDiags.empty();
    for (auto& d : validationDiags)
        result.diagnostics.push_back(std::move(d));
    if (validationFailed)
        return result;

    result.model = std::move(model);
    return result;
}

} // namespace

LowerResult lowerSfzFile(const std::string& path)
{
    try {
        return lowerImpl(fs::u8path(path), nullptr);
    } catch (const std::exception& e) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sfz.internal_error",
                                              std::string("internal error: ") + e.what(),
                                              SourceLocation { path, -1, -1 }));
        return result;
    } catch (...) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sfz.internal_error",
                                              "unknown internal error",
                                              SourceLocation { path, -1, -1 }));
        return result;
    }
}

LowerResult lowerSfzText(const std::string& text, const std::string& virtualPath)
{
    try {
        return lowerImpl(fs::u8path(virtualPath), &text);
    } catch (const std::exception& e) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sfz.internal_error",
                                              std::string("internal error: ") + e.what(),
                                              SourceLocation { virtualPath, -1, -1 }));
        return result;
    } catch (...) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sfz.internal_error",
                                              "unknown internal error",
                                              SourceLocation { virtualPath, -1, -1 }));
        return result;
    }
}

} // namespace fbsampler
