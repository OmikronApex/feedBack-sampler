#pragma once

#include "fbsampler/detail/rt_check.h"

#include <mutex>

namespace fbsampler::detail {

/// Engine-wide mutex wrapper (NFR-1): locking inside a marked RT section is a
/// detector violation. Every mutex in engine/pool code must be a CheckedMutex
/// so the RT-safety test (Story 1.4 Task 4) catches accidental locking on the
/// audio thread.
class CheckedMutex {
public:
    void lock()
    {
        if (rtcheck::inRtSection())
            rtcheck::reportViolation("mutex lock on audio thread");
        mutex_.lock();
    }

    void unlock() { mutex_.unlock(); }

    bool try_lock()
    {
        if (rtcheck::inRtSection())
            rtcheck::reportViolation("mutex try_lock on audio thread");
        return mutex_.try_lock();
    }

private:
    std::mutex mutex_;
};

} // namespace fbsampler::detail
