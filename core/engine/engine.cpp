#include "fbsampler/engine.h"

#include "checked_mutex.h"
#include "fbsampler/detail/rt_check.h"
#include "model_to_sfz.h"

#include <sfizz.hpp>

#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace fbsampler {

namespace {

Diagnostic makeError(std::string code, std::string message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = std::move(code);
    d.message = std::move(message);
    return d;
}

/// RAII epoch marker: bumps `epoch` on construction and again on destruction,
/// so it is odd for the whole lifetime of the guard and even before/after.
/// `process()` holds one of these for its full body (every return path),
/// giving the control thread a way to prove no in-flight `process()` call can
/// still reference a snapshot it is about to free.
class RenderEpochGuard {
public:
    explicit RenderEpochGuard(std::atomic<std::uint64_t>& epoch) noexcept : epoch_(epoch)
    {
        epoch_.fetch_add(1, std::memory_order_acq_rel);
    }
    ~RenderEpochGuard() { epoch_.fetch_add(1, std::memory_order_release); }
    RenderEpochGuard(const RenderEpochGuard&) = delete;
    RenderEpochGuard& operator=(const RenderEpochGuard&) = delete;

private:
    std::atomic<std::uint64_t>& epoch_;
};

std::string joinPath(const std::string& root, const std::string& rel)
{
    if (root.empty())
        return rel;
    std::string joined = root;
    if (joined.back() != '/' && joined.back() != '\\')
        joined += '/';
    joined += rel;
    return joined;
}

} // namespace

/// One immutable model+pool binding (AD-3). Built entirely on the control
/// thread; after publication the audio thread only calls sfizz RT functions
/// on `synth`. Pool handles pinned here keep the samples resident (AD-2)
/// until the snapshot is retired.
struct EngineSnapshot {
    std::unique_ptr<sfz::Sfizz> synth;
    std::shared_ptr<SamplePool> pool;
    std::vector<SampleHandle> pinnedHandles;
    InstrumentModel model;
    std::string instrumentRoot;

    ~EngineSnapshot()
    {
        if (pool)
            for (SampleHandle h : pinnedHandles)
                pool->release(h);
    }
};

struct Engine::Impl {
    double sampleRate = 48000.0;
    int maxBlockFrames = 512;

    // AD-3 swap shape: audio thread acquire-loads `current` once per
    // process() call and renders the whole block through that pointer.
    // `renderEpoch` is bumped by process() (RenderEpochGuard) at entry and
    // exit -- odd means a process() call is currently in flight, even means
    // idle, and it only ever increases. A snapshot swapped out of `current`
    // is *not* freed immediately: it is held in `retiredPending` until the
    // *next* control-thread call, which first proves the audio thread has
    // gone idle since (waitForAudioThreadIdle) before actually destroying
    // it. That is the only way to know no in-flight process() call captured
    // the old pointer before the swap and is still using it.
    std::atomic<EngineSnapshot*> current{nullptr};
    std::atomic<std::uint64_t> renderEpoch{0};
    std::unique_ptr<EngineSnapshot> retiredPending;
    detail::CheckedMutex controlMutex;

    ~Impl()
    {
        delete current.exchange(nullptr, std::memory_order_acq_rel);
    }

    // Control-thread only. Blocks until no process() call is in flight, so
    // any call that might have captured a since-retired snapshot has
    // returned. Not RT-safe (spins/yields) -- never call from the audio
    // thread.
    void waitForAudioThreadIdle() noexcept
    {
        while ((renderEpoch.load(std::memory_order_acquire) & 1u) != 0)
            std::this_thread::yield();
    }

    void retire(EngineSnapshot* old)
    {
        if (retiredPending) {
            waitForAudioThreadIdle();
            retiredPending.reset(); // now provably unreachable from process()
        }
        retiredPending.reset(old);
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockFrames)
{
    std::lock_guard<detail::CheckedMutex> lock(impl_->controlMutex);
    impl_->sampleRate = sampleRate;
    impl_->maxBlockFrames = maxBlockFrames;
    // Caller contract: prepare() never runs concurrently with process(), so
    // applying CT settings to the live synth directly is safe here.
    if (EngineSnapshot* s = impl_->current.load(std::memory_order_acquire)) {
        s->synth->setSampleRate(static_cast<float>(sampleRate));
        s->synth->setSamplesPerBlock(maxBlockFrames);
    }
}

std::vector<Diagnostic> Engine::load(const InstrumentModel& model,
                                     std::shared_ptr<SamplePool> pool,
                                     const std::string& instrumentRoot)
{
    std::vector<Diagnostic> diagnostics;
    if (!pool) {
        diagnostics.push_back(makeError("engine.pool_missing", "no sample pool provided"));
        return diagnostics;
    }
    if (model.regions.empty()) {
        diagnostics.push_back(makeError("engine.no_regions",
            "model has no regions; the instrument cannot sound"));
        return diagnostics;
    }

    std::lock_guard<detail::CheckedMutex> lock(impl_->controlMutex);

    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->pool = pool;
    snapshot->model = model;
    snapshot->instrumentRoot = instrumentRoot;

    // Bind sample references through the pool (AD-2): pin every referenced
    // sample and learn its real rate for seconds->frames conversion.
    std::vector<double> regionRates(model.regions.size(), 0.0);
    bool bindFailed = false;
    for (std::size_t i = 0; i < model.regions.size(); ++i) {
        const std::string path = joinPath(instrumentRoot, model.regions[i].sampleFile);
        SampleHandle h = pool->acquire(path, &diagnostics);
        if (h == kInvalidSampleHandle) {
            bindFailed = true;
            continue;
        }
        snapshot->pinnedHandles.push_back(h);
        SampleInfo info;
        if (pool->info(h, info))
            regionRates[i] = info.sampleRate;
    }
    if (bindFailed)
        return diagnostics; // previous snapshot stays active

    // v0 seam: the sfizz backend re-reads sample bytes through its own
    // FilePool, so sample data is resident twice (pool + sfizz). Accepted for
    // v0 and tracked in deferred-work.md; Epic 5 unifies residency behind the
    // fbsampler pool when streaming lands.
    const std::string sfzText = detail::modelToSfzText(model, regionRates, &diagnostics);

    auto synth = std::make_unique<sfz::Sfizz>();
    synth->setSampleRate(static_cast<float>(impl_->sampleRate));
    synth->setSamplesPerBlock(impl_->maxBlockFrames);
    // All-RAM v0: force full preload so the audio thread never waits on
    // streaming. Not uint32 max: sfizz adds each region's offset to the
    // preload size internally, and the uint32 sum wrapping around leaves
    // offset regions silent. 2^30 frames (> 6 h at 48 kHz) still means
    // "everything" for any real sample while leaving headroom for offsets.
    synth->setPreloadSize(1u << 30);

    const std::string virtualPath = joinPath(instrumentRoot, "__fbsampler_model__.sfz");
    if (!synth->loadSfzString(virtualPath, sfzText)) {
        diagnostics.push_back(makeError(
            "engine.bind_failed",
            "engine backend rejected the lowered model (no regions bound)"));
        return diagnostics; // previous snapshot stays active
    }

    snapshot->synth = std::move(synth);

    EngineSnapshot* old = impl_->current.exchange(snapshot.release(),
                                                  std::memory_order_acq_rel);
    impl_->retire(old);
    return diagnostics;
}

void Engine::process(const EngineEvent* events, std::size_t numEvents,
                     float** out, std::size_t numFrames) noexcept
{
    RenderEpochGuard epochGuard(impl_->renderEpoch);
    EngineSnapshot* s = impl_->current.load(std::memory_order_acquire);
    if (!s) {
        for (int ch = 0; ch < 2; ++ch)
            std::memset(out[ch], 0, numFrames * sizeof(float));
        return;
    }

    if (!events)
        numEvents = 0;

    for (std::size_t i = 0; i < numEvents; ++i) {
        const EngineEvent& e = events[i];
        // Guard against a malformed/mistimed event rather than handing sfizz
        // an out-of-range sample offset: clamp into this block's range.
        const int delayFrames = e.delayFrames < 0
            ? 0
            : (static_cast<std::size_t>(e.delayFrames) >= numFrames
                   ? (numFrames > 0 ? static_cast<int>(numFrames) - 1 : 0)
                   : e.delayFrames);
        switch (e.type) {
        case EngineEvent::Type::NoteOn:
            s->synth->noteOn(delayFrames, e.note, e.velocity);
            break;
        case EngineEvent::Type::NoteOff:
            s->synth->noteOff(delayFrames, e.note, 0);
            break;
        case EngineEvent::Type::ControlChange:
            s->synth->cc(delayFrames, e.note, e.velocity);
            break;
        case EngineEvent::Type::PitchBend: {
            const int bend = e.bendValue < -8192 ? -8192
                : (e.bendValue > 8191 ? 8191 : e.bendValue);
            s->synth->pitchWheel(delayFrames, bend);
            break;
        }
        }
    }

    s->synth->renderBlock(out, numFrames, 1);
}

} // namespace fbsampler
