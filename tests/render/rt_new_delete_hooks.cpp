// Global allocation hooks for the RT-safety detector (Story 1.4 Task 4).
// Linked into the test binary only: production builds carry no allocation
// interposition, just the thread-local section flag. Any operator new/delete
// reached while the calling thread is inside a marked RT section is a
// violation (NFR-1: no allocation on the audio thread).

#include "fbsampler/detail/rt_check.h"

#include <cstdlib>
#include <new>

namespace {

void* checkedAlloc(std::size_t size)
{
    if (fbsampler::rtcheck::inRtSection())
        fbsampler::rtcheck::reportViolation("operator new on audio thread");
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}

void checkedFree(void* p) noexcept
{
    if (p && fbsampler::rtcheck::inRtSection())
        fbsampler::rtcheck::reportViolation("operator delete on audio thread");
    std::free(p);
}

} // namespace

void* operator new(std::size_t size) { return checkedAlloc(size); }
void* operator new[](std::size_t size) { return checkedAlloc(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (fbsampler::rtcheck::inRtSection())
        fbsampler::rtcheck::reportViolation("operator new on audio thread");
    return std::malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    if (fbsampler::rtcheck::inRtSection())
        fbsampler::rtcheck::reportViolation("operator new on audio thread");
    return std::malloc(size ? size : 1);
}

void operator delete(void* p) noexcept { checkedFree(p); }
void operator delete[](void* p) noexcept { checkedFree(p); }
void operator delete(void* p, std::size_t) noexcept { checkedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { checkedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { checkedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { checkedFree(p); }
