#include "fbsampler/pool.h"

#include "../engine/checked_mutex.h"
#include "fbsampler/detail/rt_check.h"
#include "wav_reader.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace fbsampler {

namespace {

/// v0 all-RAM pool. The slot table is sized once at construction and never
/// reallocated, so the audio thread can index it lock-free while the control
/// thread fills new slots; a slot becomes visible through an acquire/release
/// `live` flag. release() at refcount zero frees the bulk sample memory but
/// keeps the slot, so a handle can only dangle if its owner dropped the
/// refcount that pinned it (AD-2: snapshots pin every handle they publish).
///
/// Safety invariant (load-bearing, not just a nicety): acquire()'s slot-reuse
/// path (`entries_[slot] = std::move(entry)`) overwrites a released slot with
/// no lock held against a concurrent info()/residentChannel() read, and with
/// no generation tag on the returned handle. This is only sound because
/// `Engine` (core/engine/engine.cpp) never lets a handle outlive the
/// EngineSnapshot that pinned it -- its epoch-gated snapshot reclamation
/// (RenderEpochGuard / waitForAudioThreadIdle) guarantees release() for a
/// snapshot's handles never runs while the audio thread could still be
/// reading them. Any future caller that holds a SampleHandle outside an
/// EngineSnapshot's lifetime would break this invariant and needs its own
/// generation-tagged handle scheme.
class AllRamSamplePool final : public SamplePool {
public:
    static constexpr std::size_t kMaxEntries = 4096;

    AllRamSamplePool() : entries_(kMaxEntries) {}

    SampleHandle acquire(const std::string& path,
                         std::vector<Diagnostic>* diagnostics) override
    {
        if (rtcheck::inRtSection())
            rtcheck::reportViolation("pool acquire (file I/O) on audio thread");

        std::lock_guard<detail::CheckedMutex> lock(mutex_);

        // First pass: look for an already-pooled live entry (refcount bump)
        // or, failing that, a released (refCount==0) slot to reuse so the
        // fixed slot table doesn't get monotonically consumed by a session
        // that repeatedly loads and unloads instruments.
        std::size_t reuseSlot = used_;
        for (std::size_t i = 0; i < used_; ++i) {
            Entry& e = *entries_[i];
            if (e.refCount > 0 && e.path == path) {
                ++e.refCount;
                return static_cast<SampleHandle>(i + 1);
            }
            if (e.refCount == 0 && reuseSlot == used_)
                reuseSlot = i;
        }

        if (reuseSlot == used_ && used_ >= kMaxEntries) {
            if (diagnostics) {
                Diagnostic d;
                d.severity = Severity::Error;
                d.code = "pool.capacity_exceeded";
                d.message = "sample pool slot capacity exceeded";
                d.location.file = path;
                diagnostics->push_back(std::move(d));
            }
            return kInvalidSampleHandle;
        }

        detail::DecodedWav wav;
        if (!detail::readWavFile(path, wav, diagnostics))
            return kInvalidSampleHandle;

        auto entry = std::make_unique<Entry>();
        entry->path = path;
        entry->refCount = 1;
        entry->info.numChannels = wav.numChannels;
        entry->info.numFrames = wav.numFrames;
        entry->info.sampleRate = wav.sampleRate;
        // v0: everything resident. The contract still reports the resident
        // head separately so no caller grows a full-residency assumption.
        entry->info.residentFrames = wav.numFrames;
        entry->channels = std::move(wav.channels);
        entry->channelPointers.reserve(entry->channels.size());
        for (auto& ch : entry->channels)
            entry->channelPointers.push_back(ch.data());

        const std::size_t slot = reuseSlot;
        entries_[slot] = std::move(entry);
        entries_[slot]->live.store(true, std::memory_order_release);
        if (slot == used_)
            ++used_;
        return static_cast<SampleHandle>(slot + 1);
    }

    void retain(SampleHandle handle) override
    {
        if (rtcheck::inRtSection())
            rtcheck::reportViolation("pool retain on audio thread");

        std::lock_guard<detail::CheckedMutex> lock(mutex_);
        Entry* e = entryFor(handle);
        if (e && e->refCount > 0)
            ++e->refCount;
    }

    void release(SampleHandle handle) override
    {
        if (rtcheck::inRtSection())
            rtcheck::reportViolation("pool release on audio thread");

        std::lock_guard<detail::CheckedMutex> lock(mutex_);
        Entry* e = entryFor(handle);
        if (!e || e->refCount == 0) {
            // Double-release, or release() on a handle whose slot was never
            // acquired: always a caller bug. No live audio-thread impact
            // (info()/residentChannel() already gate on the `live` flag),
            // but surface it loudly in debug builds instead of silently
            // no-opping.
            assert(false && "release() called with refCount already 0 or on an unknown handle");
            return;
        }
        if (--e->refCount == 0) {
            e->live.store(false, std::memory_order_release);
            e->channels.clear();
            e->channelPointers.clear();
            e->path.clear();
        }
    }

    bool info(SampleHandle handle, SampleInfo& out) const noexcept override
    {
        const Entry* e = liveEntryFor(handle);
        if (!e)
            return false;
        out = e->info;
        return true;
    }

    const float* residentChannel(SampleHandle handle,
                                 std::uint32_t channel) const noexcept override
    {
        const Entry* e = liveEntryFor(handle);
        if (!e || channel >= e->channelPointers.size())
            return nullptr;
        return e->channelPointers[channel];
    }

private:
    struct Entry {
        std::string path;
        std::uint32_t refCount = 0;
        std::atomic<bool> live{false};
        SampleInfo info;
        std::vector<std::vector<float>> channels;
        std::vector<const float*> channelPointers;
    };

    Entry* entryFor(SampleHandle handle)
    {
        if (handle == kInvalidSampleHandle || handle > used_)
            return nullptr;
        return entries_[handle - 1].get();
    }

    const Entry* liveEntryFor(SampleHandle handle) const noexcept
    {
        if (handle == kInvalidSampleHandle || handle > kMaxEntries)
            return nullptr;
        const Entry* e = entries_[handle - 1].get();
        if (!e || !e->live.load(std::memory_order_acquire))
            return nullptr;
        return e;
    }

    mutable detail::CheckedMutex mutex_;
    // Fixed-size slot table: element addresses and the vector buffer itself
    // never move, which is what makes lock-free audio-thread indexing sound.
    std::vector<std::unique_ptr<Entry>> entries_;
    std::size_t used_ = 0;
};

} // namespace

std::unique_ptr<SamplePool> createAllRamSamplePool()
{
    return std::make_unique<AllRamSamplePool>();
}

} // namespace fbsampler
