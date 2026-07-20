#pragma once

#include "fbsampler/config_service.h"

#include <string>
#include <vector>

namespace fbsampler::ui {

// Pure, JUCE-free filter logic for the library browser (kept out of the
// ListBox model so it is unit-testable). Pills: SFZ, SoundFont (SF2+SF3
// share one pill), DS (empty until Epic 4).
struct LibraryFilter {
    enum PillMask : unsigned {
        pillSfz = 1u << 0,
        pillSoundfont = 1u << 1, // sf2 + sf3
        pillDs = 1u << 2,
        pillNone = 0u, // no pill active == all formats pass
    };

    /// Returns indices into `entries` that match: case-insensitive substring
    /// of `query` on displayName AND format allowed by `activePills`
    /// (pillNone = every format). Order preserved.
    static std::vector<int> filter(const std::vector<LibraryEntry>& entries,
                                   const std::string& query,
                                   unsigned activePills);

    static bool formatMatchesPills(LibraryFormat format, unsigned activePills);
    static bool nameMatchesQuery(const std::string& displayName,
                                 const std::string& query);
};

} // namespace fbsampler::ui
