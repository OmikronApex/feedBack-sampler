#pragma once

#include "fbsampler/diagnostic.h"
#include "fbsampler/sfz_frontend.h" // LowerResult

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fbsampler {

/// One entry of a SoundFont's preset directory (phdr), without lowering.
struct Sf2PresetInfo {
    int bank = 0;    // MIDI bank number (0-16383; 128 = percussion convention)
    int program = 0; // MIDI program number 0-127
    std::string name;
};

/// Result of enumerating a SoundFont's presets. `presets` is empty when a
/// structural (Severity::Error) diagnostic occurred. Never throws (spine
/// convention: no exceptions cross the core API).
struct Sf2PresetListResult {
    std::optional<std::vector<Sf2PresetInfo>> presets;
    std::vector<Diagnostic> diagnostics;
};

/// Cheap preset enumeration: parses the RIFF structure and phdr only, no zone
/// lowering. This is the API the preset browser (Story 2.4) builds on.
Sf2PresetListResult listSf2Presets(const std::string& path);

/// Lower ONE preset (selected by bank/program) of an .sf2 file from disk into
/// the canonical model. `InstrumentModel::name` is the preset name from phdr.
/// Sample references are recorded as container URIs
/// ("sf2://<path>#<sampleIndex>", SPEC.md#Sample-references) — the frontend
/// never decodes sample data (AD-2).
LowerResult lowerSf2Preset(const std::string& path, int bank, int program);

/// Lower from an in-memory byte span (fuzzing entry point, AD-10: parsers are
/// pure functions fuzzable in isolation). No file-system access.
/// `virtualPath` labels diagnostics and sample-reference URIs only.
LowerResult lowerSf2Bytes(const std::uint8_t* data, std::size_t size, int bank, int program,
                          const std::string& virtualPath = "<memory>");

/// Enumerate presets from an in-memory byte span (fuzzing entry point).
Sf2PresetListResult listSf2PresetsFromBytes(const std::uint8_t* data, std::size_t size,
                                            const std::string& virtualPath = "<memory>");

/// A parsed-once soundfont session (Story 2.4, FR-9): open the file once,
/// enumerate presets, lower any preset on demand WITHOUT re-parsing the
/// container. Sample data never lives here (AD-2 — the pool decodes on
/// acquire; presets within one soundfont share pooled samples through the
/// refcounted sf2:// reference scheme). Immutable after open; safe to share
/// across threads by const reference.
struct Sf2SessionResult;
Sf2SessionResult openSf2Session(const std::string& path);

class Sf2Session {
public:
    ~Sf2Session();
    Sf2Session(const Sf2Session&) = delete;
    Sf2Session& operator=(const Sf2Session&) = delete;

    /// Presets sorted bank ascending, program ascending (the order every
    /// established soundfont player presents). The vector index is the
    /// session's stable preset index.
    const std::vector<Sf2PresetInfo>& presets() const;

    /// Lower the preset at enumeration `index` (in-memory transforms only —
    /// no file I/O). Out-of-range index yields an error diagnostic.
    LowerResult lowerPreset(std::size_t index) const;

    /// The container path the session was opened from.
    const std::string& path() const;

private:
    friend struct Sf2SessionAccess;
    friend Sf2SessionResult openSf2Session(const std::string& path);
    Sf2Session();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Result of opening a session. `session` is null when a structural
/// (Severity::Error) diagnostic occurred.
struct Sf2SessionResult {
    std::shared_ptr<Sf2Session> session;
    std::vector<Diagnostic> diagnostics;
};

} // namespace fbsampler
