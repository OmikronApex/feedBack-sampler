#pragma once

// SF2 curve shapes (SoundFont 2.04 §9.5.2; SPEC.md#Curve-shapes), shared by
// the bind seam (model_to_sfz.cpp) and pinned by unit tests. FluidSynth's
// fluid_conv.c tables are the oracle-matching reference implementation.
// Everything here is closed-form and allocation-free; the engine evaluates it
// at bind time only (the audio thread runs sfizz's table-backed curves).

#include "fbsampler/model.h"

#include <algorithm>
#include <cmath>

namespace fbsampler::detail {

inline float concaveShape(float x)
{
    // -20/96 * log10(((127 - 127x)^2) / 127^2), clamped to [0, 1].
    x = std::clamp(x, 0.0f, 1.0f);
    const float inv = 127.0f - 127.0f * x;
    if (inv <= 0.0f)
        return 1.0f;
    const float value = -20.0f / 96.0f * std::log10((inv * inv) / (127.0f * 127.0f));
    return std::clamp(value, 0.0f, 1.0f);
}

inline float curveShape(ModCurveType type, float x)
{
    switch (type) {
        case ModCurveType::Linear: return std::clamp(x, 0.0f, 1.0f);
        case ModCurveType::Concave: return concaveShape(x);
        case ModCurveType::Convex: return 1.0f - concaveShape(1.0f - x);
        case ModCurveType::Switch: return x < 0.5f ? 0.0f : 1.0f;
    }
    return x;
}

/// Full source mapping: normalized controller position -> normalized
/// contribution (unipolar 0..1 or bipolar -1..1), applying direction
/// inversion, the curve shape, then polarity.
inline float evalModSource(const ModSource& source, float x)
{
    if (source.maxToMin)
        x = 1.0f - x;
    const float u = curveShape(source.curve, x);
    return source.bipolar ? 2.0f * u - 1.0f : u;
}

} // namespace fbsampler::detail
