#pragma once

#include "fbsampler/diagnostic.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fbsampler {

// AD-9: settings.json + library-index.json in the platform per-user config
// dir, written ONLY through this service via temp-write + atomic rename with
// re-read-merge before every write. Public API is JUCE-free (AD-6): std
// types + Diagnostic only. Storage is JSON today; the API is
// storage-agnostic so a backend swap (SQLite, spine Deferred) stays internal.

/// Library formats the scanner recognizes. `.dspreset`/`.dslibrary`
/// recognition lands with Epic 4 — extend this enum plus the extension
/// dispatch in library_scanner.cpp.
enum class LibraryFormat {
    sfz,
    sf2,
    sf3,
};

/// One recognized library in the shared index. `path` is the absolute
/// canonical path of the library file — the entry identity (the index is a
/// local machine artifact; AD-4's relative-identity rule governs the plugin
/// STATE chunk, not this file). Compared case-insensitively on Windows.
struct LibraryEntry {
    std::string path;
    std::string rootFolder;   // designated folder it was found under
    LibraryFormat format = LibraryFormat::sfz;
    std::string displayName;
    std::uint64_t sizeBytes = 0;
    std::int64_t mtime = 0;     // file modification time, unix seconds
    std::int64_t scannedAt = 0; // when this entry was (re)examined, unix seconds
    bool ok = true;             // false => failed + statusDetail summary
    std::string statusDetail;
};

/// The shared library index (library-index.json).
struct LibraryIndex {
    int schemaVersion = 1;
    std::int64_t generation = 0;
    std::vector<LibraryEntry> entries;
};

/// settings.json v1. `ramStreamThresholdMb` is persisted now and consumed by
/// Epic 5. `voiceLimit` default mirrors the engine's (sfizz default 64).
struct SamplerSettings {
    int schemaVersion = 1;
    std::int64_t generation = 0;
    std::vector<std::string> libraryFolders;
    int voiceLimit = 64;
    int ramStreamThresholdMb = 512;
};

/// Progress callback payload for scans (Story 3.6's banner consumes this).
struct ScanProgress {
    std::string currentPath;
    std::size_t examined = 0;   // files stat'ed this scan
    std::size_t recognized = 0; // library files currently known
};

class ConfigService {
public:
    /// `baseDirOverride`: when non-empty, config files live there (tests MUST
    /// use this or the FBSAMPLER_CONFIG_DIR env var — never the real user
    /// dir). When empty, FBSAMPLER_CONFIG_DIR is honored, else the platform
    /// per-user config dir (Windows %APPDATA%/feedBack Sampler/, macOS
    /// ~/Library/Application Support/feedBack Sampler/, Linux
    /// $XDG_CONFIG_HOME|~/.config/feedback-sampler/).
    /// Reads both files immediately; missing files are NOT an error (first
    /// run) — defaults apply. Never scans.
    explicit ConfigService(std::string baseDirOverride = {});
    ~ConfigService();

    ConfigService(const ConfigService&) = delete;
    ConfigService& operator=(const ConfigService&) = delete;

    std::string configDir() const;

    /// Copies of the in-memory state (thread-safe).
    SamplerSettings settings() const;
    LibraryIndex index() const;

    /// True when the on-disk file carries a NEWER schemaVersion than this
    /// binary knows: the file is served read-only, writes fail soft with a
    /// Diagnostic, and the file is never rewritten (AD-9 fail-soft).
    bool settingsReadOnly() const;
    bool indexReadOnly() const;

    /// Re-reads both files from disk (e.g. to pick up another instance's
    /// writes). Diagnostics for unreadable/corrupt files; state keeps the
    /// last good copy on failure.
    std::vector<Diagnostic> reload();

    /// Persists settings: re-reads the current file, merges (last-writer-wins
    /// per setting — the provided values win; a concurrently bumped disk
    /// generation is respected), bumps generation, atomic-renames.
    std::vector<Diagnostic> saveSettings(const SamplerSettings& newSettings);

    using ProgressFn = std::function<void(const ScanProgress&)>;

    /// Scans settings().libraryFolders recursively and persists the merged
    /// index. Incremental: entries whose size+mtime are unchanged keep their
    /// stored data without re-examination; new/changed files are examined;
    /// entries whose file vanished are removed from the index (AD-4
    /// state-chunk resolution handles missing libraries separately).
    /// Cheap and safe by contract: stat + extension + header sniff only,
    /// never loads sample data, never throws. Call from a background/loader
    /// thread — NEVER the audio thread. `progress` (optional) fires per
    /// examined directory/file on the calling thread.
    std::vector<Diagnostic> scan(const ProgressFn& progress = {});

    /// AD-4: register ONE library file (e.g. dropped onto the editor) as an
    /// index entry so it has a durable identity for later state-chunk
    /// resolution. Entry shape matches scanned entries; rootFolder = the
    /// file's parent directory. Existing entry (same canonical path) is
    /// refreshed, not duplicated. Merge-on-write like every index write.
    std::vector<Diagnostic> registerFile(const std::string& path,
                                         LibraryFormat format);

    /// Record load success/failure against an entry (Story 3.6 failure
    /// state): sets status ok/failed + detail, persists via the same
    /// merge-on-write path. Unknown path is a no-op (no diagnostic spam).
    std::vector<Diagnostic> setEntryStatus(const std::string& path, bool ok,
                                           const std::string& statusDetail);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fbsampler
