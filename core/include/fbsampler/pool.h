#pragma once

#include "fbsampler/diagnostic.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fbsampler {

/// Opaque reference to a pooled sample. 0 is never a valid handle.
using SampleHandle = std::uint32_t;
constexpr SampleHandle kInvalidSampleHandle = 0;

/// Description of a pooled sample (AD-2).
///
/// `residentFrames` is how many leading frames are guaranteed resident in RAM
/// and readable via residentChannel(). Callers must NEVER assume
/// `residentFrames == numFrames`: the v0 all-RAM implementation happens to
/// keep everything resident, but the contract already distinguishes the
/// resident head from tail data so Epic 5 can swap in a streaming
/// implementation without touching callers.
struct SampleInfo {
    std::uint32_t numChannels = 0;
    std::uint64_t numFrames = 0;
    double sampleRate = 0.0;
    std::uint64_t residentFrames = 0;
};

/// The AD-2 sample-pool contract: the only component that touches sample
/// bytes. Frontends lower sample *references* (paths); the engine binds them
/// through this interface.
///
/// Threading contract:
///  - acquire()/retain()/release() run on control/loader threads only, never
///    the audio thread (they may perform file I/O and allocate).
///  - info()/residentChannel() are lock-free, allocation-free, wait-free
///    reads, safe on the audio thread for any handle whose acquisition
///    happened-before the read (e.g. published via an engine snapshot swap).
///
/// Entries are refcounted: acquire() of an already-pooled path bumps the
/// refcount and returns the same handle; release() drops it. A handle stays
/// valid while its refcount is > 0.
class SamplePool {
public:
    virtual ~SamplePool() = default;

    /// Load (or refcount-bump) the sample at `path`. Returns
    /// kInvalidSampleHandle on failure and, if `diagnostics` is non-null,
    /// appends the reason. Control/loader thread only.
    virtual SampleHandle acquire(const std::string& path,
                                 std::vector<Diagnostic>* diagnostics = nullptr) = 0;

    /// Bump the refcount of an already-acquired handle. Control thread only.
    virtual void retain(SampleHandle handle) = 0;

    /// Drop one reference. Control thread only.
    virtual void release(SampleHandle handle) = 0;

    /// Fill `out` with the sample description. Returns false for an invalid
    /// handle. Audio-thread safe.
    virtual bool info(SampleHandle handle, SampleInfo& out) const noexcept = 0;

    /// Pointer to the resident head of one deinterleaved float32 channel
    /// (`SampleInfo::residentFrames` frames long), or nullptr for an invalid
    /// handle/channel. Audio-thread safe.
    virtual const float* residentChannel(SampleHandle handle,
                                         std::uint32_t channel) const noexcept = 0;
};

/// v0 all-RAM implementation: loads referenced files fully at acquire() time
/// (float32 conversion at load), everything resident.
std::unique_ptr<SamplePool> createAllRamSamplePool();

} // namespace fbsampler
