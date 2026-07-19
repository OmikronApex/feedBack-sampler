#include "render_harness.h"

#include "fbsampler/detail/rt_check.h"

#include <algorithm>

namespace fbsampler::testutil {

RenderResult renderOffline(const InstrumentModel& model,
                           const std::shared_ptr<SamplePool>& pool,
                           const std::string& instrumentRoot,
                           const std::vector<TimelineEvent>& timeline,
                           const RenderSettings& settings)
{
    RenderResult result;
    if (settings.totalFrames == 0 || settings.blockFrames <= 0)
        return result;

    Engine engine;
    engine.prepare(settings.sampleRate, settings.blockFrames);
    result.diagnostics = engine.load(model, pool, instrumentRoot);
    for (const Diagnostic& d : result.diagnostics)
        if (d.severity == Severity::Error)
            return result;

    std::vector<TimelineEvent> sorted = timeline;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const TimelineEvent& a, const TimelineEvent& b) {
                         return a.frame < b.frame;
                     });

    result.channels.assign(2, std::vector<float>(
                                  static_cast<std::size_t>(settings.totalFrames), 0.0f));

    std::vector<EngineEvent> blockEvents;
    blockEvents.reserve(sorted.size());
    std::vector<float> left(static_cast<std::size_t>(settings.blockFrames));
    std::vector<float> right(static_cast<std::size_t>(settings.blockFrames));
    float* out[2] = {left.data(), right.data()};

    std::size_t nextEvent = 0;
    for (std::uint64_t start = 0; start < settings.totalFrames;
         start += static_cast<std::uint64_t>(settings.blockFrames)) {
        const auto frames = static_cast<std::size_t>(
            std::min<std::uint64_t>(settings.blockFrames, settings.totalFrames - start));

        blockEvents.clear();
        while (nextEvent < sorted.size()
               && sorted[nextEvent].frame < start + frames) {
            const TimelineEvent& t = sorted[nextEvent];
            EngineEvent e;
            e.type = t.type;
            e.delayFrames = static_cast<int>(t.frame - start);
            e.note = t.note;
            e.velocity = t.velocity;
            e.bendValue = t.bendValue;
            blockEvents.push_back(e);
            ++nextEvent;
        }

        if (settings.markRtSections) {
            rtcheck::SectionGuard guard;
            engine.process(blockEvents.data(), blockEvents.size(), out, frames);
        } else {
            engine.process(blockEvents.data(), blockEvents.size(), out, frames);
        }

        std::copy_n(left.begin(), frames,
                    result.channels[0].begin() + static_cast<std::size_t>(start));
        std::copy_n(right.begin(), frames,
                    result.channels[1].begin() + static_cast<std::size_t>(start));
    }

    result.ok = true;
    return result;
}

} // namespace fbsampler::testutil
