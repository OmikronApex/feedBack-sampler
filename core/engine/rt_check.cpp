#include "fbsampler/detail/rt_check.h"

#include <atomic>

namespace fbsampler::rtcheck {

namespace {
thread_local int gRtDepth = 0;
std::atomic<std::uint64_t> gViolations{0};
std::atomic<const char*> gLastViolation{nullptr};
} // namespace

bool inRtSection() noexcept
{
    return gRtDepth > 0;
}

SectionGuard::SectionGuard() noexcept
{
    ++gRtDepth;
}

SectionGuard::~SectionGuard() noexcept
{
    --gRtDepth;
}

void reportViolation(const char* what) noexcept
{
    gLastViolation.store(what, std::memory_order_relaxed);
    gViolations.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t violationCount() noexcept
{
    return gViolations.load(std::memory_order_relaxed);
}

const char* lastViolation() noexcept
{
    return gLastViolation.load(std::memory_order_relaxed);
}

void resetViolations() noexcept
{
    gViolations.store(0, std::memory_order_relaxed);
    gLastViolation.store(nullptr, std::memory_order_relaxed);
}

} // namespace fbsampler::rtcheck
