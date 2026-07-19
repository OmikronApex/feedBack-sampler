#pragma once

#include "render_harness.h"

#include <string>
#include <vector>

namespace fbsampler::testutil {

// The seed fixture (Story 1.4 AC 1): fixed MIDI sequence over the checked-in
// seed instrument, rendered at fixed settings. Shared by the render-regression
// and RT-safety tests so both drive identical engine work.

inline std::string seedInstrumentSfzPath()
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/seed/seed.sfz";
}

inline std::string seedInstrumentRoot()
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/seed";
}

inline std::string seedReferenceWavPath()
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/reference/seed_render.wav";
}

/// Fixed sequence: soft layer, loud layer (velocity layers audible), then the
/// looped pad held past its sample length (loop audible) with an envelope
/// release tail inside the render window.
inline std::vector<TimelineEvent> seedTimeline()
{
    using T = EngineEvent::Type;
    return {
        {0, T::NoteOn, 60, 40},        // soft velocity layer
        {24000, T::NoteOff, 60, 0},    // release at 0.5 s
        {48000, T::NoteOn, 60, 110},   // loud velocity layer
        {72000, T::NoteOff, 60, 0},
        {96000, T::NoteOn, 36, 100},   // looped pad, held 2 s (> sample length)
        {192000, T::NoteOff, 36, 0},   // release tail decays before end
    };
}

inline RenderSettings seedRenderSettings(bool markRtSections = false)
{
    RenderSettings s;
    s.sampleRate = 48000.0;
    s.blockFrames = 256;
    s.totalFrames = 240000; // 5 s
    s.markRtSections = markRtSections;
    return s;
}

} // namespace fbsampler::testutil
