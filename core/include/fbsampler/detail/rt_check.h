#pragma once

#include <cstdint>

// RT-safety detector (NFR-1). The audio thread marks its render sections with
// SectionGuard; any code that would violate audio-thread rules (allocation,
// locks, file I/O) checks inRtSection() and reports. The allocation hook
// itself (global operator new/delete override) lives in the test binary so
// production builds carry only the cheap thread-local flag; lock and file-I/O
// checks are wired permanently into the engine's CheckedMutex wrapper and the
// pool's loading path.
//
// Everything here is allocation-free and lock-free by construction.

namespace fbsampler::rtcheck {

/// True while the calling thread is inside a marked RT section.
bool inRtSection() noexcept;

/// RAII marker for an RT section (nestable).
class SectionGuard {
public:
    SectionGuard() noexcept;
    ~SectionGuard() noexcept;
    SectionGuard(const SectionGuard&) = delete;
    SectionGuard& operator=(const SectionGuard&) = delete;
};

/// Record a violation observed inside an RT section. `what` must be a string
/// literal (stored by pointer, never copied).
void reportViolation(const char* what) noexcept;

/// Number of violations since the last reset.
std::uint64_t violationCount() noexcept;

/// Description of the most recent violation (string literal), or nullptr.
const char* lastViolation() noexcept;

void resetViolations() noexcept;

} // namespace fbsampler::rtcheck
