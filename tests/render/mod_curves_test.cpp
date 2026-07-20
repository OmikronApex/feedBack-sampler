// SF2 curve-shape unit tests (Story 2.2): pin numeric values of the §9.5.2
// mapping functions against hand-computed spec values (FluidSynth's
// fluid_conv.c tables are the cross-reference).

#include <catch2/catch_test_macros.hpp>

#include "engine/mod_curves.h"

#include <cmath>

using namespace fbsampler;
using namespace fbsampler::detail;

TEST_CASE("linear shape is identity, clamped", "[sf2][curves]")
{
    CHECK(curveShape(ModCurveType::Linear, 0.0f) == 0.0f);
    CHECK(curveShape(ModCurveType::Linear, 0.5f) == 0.5f);
    CHECK(curveShape(ModCurveType::Linear, 1.0f) == 1.0f);
    CHECK(curveShape(ModCurveType::Linear, -1.0f) == 0.0f);
    CHECK(curveShape(ModCurveType::Linear, 2.0f) == 1.0f);
}

TEST_CASE("concave shape matches the spec formula endpoints and midpoint", "[sf2][curves]")
{
    CHECK(concaveShape(0.0f) == 0.0f);
    CHECK(concaveShape(1.0f) == 1.0f);
    // x = 0.5: -20/96 * log10(((127*0.5)^2)/127^2) = -20/96 * log10(0.25)
    //        = -20/96 * (-0.60206) = 0.125429
    CHECK(std::abs(concaveShape(0.5f) - 0.125429f) < 1e-4f);
    // x = 100/127 (CC value 100): inv = 27; -20/96*log10(27^2/127^2) = 0.28017
    CHECK(std::abs(concaveShape(100.0f / 127.0f) - 0.28017f) < 1e-3f);
    // Monotonic non-decreasing over the full range.
    float prev = -1.0f;
    for (int i = 0; i <= 127; ++i) {
        const float v = concaveShape(static_cast<float>(i) / 127.0f);
        CHECK(v >= prev);
        prev = v;
    }
}

TEST_CASE("convex is the mirror of concave", "[sf2][curves]")
{
    for (int i = 0; i <= 127; ++i) {
        const float x = static_cast<float>(i) / 127.0f;
        CHECK(std::abs(curveShape(ModCurveType::Convex, x)
                       - (1.0f - concaveShape(1.0f - x))) < 1e-6f);
    }
    CHECK(curveShape(ModCurveType::Convex, 0.0f) == 0.0f);
    CHECK(curveShape(ModCurveType::Convex, 1.0f) == 1.0f);
}

TEST_CASE("switch shape thresholds at the midpoint", "[sf2][curves]")
{
    CHECK(curveShape(ModCurveType::Switch, 0.0f) == 0.0f);
    CHECK(curveShape(ModCurveType::Switch, 0.49f) == 0.0f);
    CHECK(curveShape(ModCurveType::Switch, 0.5f) == 1.0f);
    CHECK(curveShape(ModCurveType::Switch, 1.0f) == 1.0f);
}

TEST_CASE("direction and polarity wrap the curve", "[sf2][curves]")
{
    ModSource s;
    s.curve = ModCurveType::Linear;

    SECTION("max-to-min inverts the input")
    {
        s.maxToMin = true;
        CHECK(evalModSource(s, 0.0f) == 1.0f);
        CHECK(evalModSource(s, 1.0f) == 0.0f);
    }
    SECTION("bipolar maps 0..1 to -1..1")
    {
        s.bipolar = true;
        CHECK(evalModSource(s, 0.0f) == -1.0f);
        CHECK(evalModSource(s, 0.5f) == 0.0f);
        CHECK(evalModSource(s, 1.0f) == 1.0f);
    }
    SECTION("default velocity-attenuation shape: full velocity means no attenuation")
    {
        s.curve = ModCurveType::Concave;
        s.maxToMin = true;
        CHECK(evalModSource(s, 1.0f) == 0.0f);
        CHECK(evalModSource(s, 0.0f) == 1.0f);
    }
}
