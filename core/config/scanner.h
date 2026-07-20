#pragma once

// Internal scanner API for core/config — not public.

#include "fbsampler/config_service.h"

#include <string>
#include <vector>

namespace fbsampler::configdetail {

struct ScanResult {
    std::vector<LibraryEntry> entries;
    std::vector<Diagnostic> diagnostics;
};

/// Recursive scan of `roots`. Incremental against `previous`: entries whose
/// size+mtime match are carried over without re-examination. Stat + extension
/// + header sniff only; never loads sample data; never throws.
ScanResult scanFolders(const std::vector<std::string>& roots,
                       const std::vector<LibraryEntry>& previous,
                       const ConfigService::ProgressFn& progress);

/// Entry-identity key: weakly-canonical absolute path, lowercased on Windows
/// (case-insensitive filesystem).
std::string canonicalKey(const std::string& path);

} // namespace fbsampler::configdetail
