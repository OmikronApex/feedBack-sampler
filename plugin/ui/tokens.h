#pragma once

#include <juce_graphics/juce_graphics.h>

// feedBack v3 design tokens — the ONLY place raw color/size values may live
// in plugin/. Source of truth: docs/planning-artifacts/ux-designs/
// ux-feedBack-sampler-2026-07-17/DESIGN.md frontmatter.
//
// RULE (grep-enforceable): no raw hex color literals anywhere else under
// plugin/ — every paint call goes through fbsampler::ui::tokens.

namespace fbsampler::ui::tokens {

// ---- Colors ----------------------------------------------------------------
namespace color {
inline const juce::Colour bg{0xff0f172a};
inline const juce::Colour surface{0xff1e293b};
inline const juce::Colour surfaceInset{0xff0b1220};
inline const juce::Colour sidebar{0xff111827};
inline const juce::Colour primary{0xff0ea5e9};
inline const juce::Colour primaryHover{0xff38bdf8};
inline const juce::Colour onAccent{0xfff8fafc};
inline const juce::Colour destructive{0xffef4444};
inline const juce::Colour text{0xfff8fafc};
inline const juce::Colour textDim{0xff94a3b8};
inline const juce::Colour border{0xff334155};
inline const juce::Colour good{0xff22c55e};
inline const juce::Colour mid{0xffeab308};
inline const juce::Colour low{0xffef4444};
inline const juce::Colour gold{0xffe8c040};      // SF2/SF3 badge ONLY
inline const juce::Colour focusRing{0xff38bdf8};
inline const juce::Colour scrollThumbHover{0xff475569}; // DESIGN.md scrollbar spec
} // namespace color

// ---- rem -> logical px (16px base) -----------------------------------------
inline constexpr float remBasePx = 16.0f;
inline constexpr float remToPx(float rem) { return rem * remBasePx; }

// ---- Radii -----------------------------------------------------------------
namespace radius {
inline constexpr float control = 0.5f * remBasePx;  // 8px
inline constexpr float card = 0.75f * remBasePx;    // 12px
inline constexpr float pill = 999.0f;
inline constexpr float frame = 0.35f * remBasePx;   // 5.6px
} // namespace radius

// ---- Spacing (4px base unit) -----------------------------------------------
namespace spacing {
inline constexpr int unit = 4;                          // 0.25rem
inline constexpr int gutter = 16;                       // 1rem
inline constexpr float cardPaddingV = 0.85f * remBasePx; // 13.6px
inline constexpr float cardPaddingH = 1.0f * remBasePx;  // 16px
} // namespace spacing

// ---- Type scale ------------------------------------------------------------
namespace type {
inline constexpr float bodyPx = 0.875f * remBasePx;   // 14px
inline constexpr float labelPx = 0.75f * remBasePx;   // 12px
inline constexpr float titlePx = 1.0f * remBasePx;    // 16px, weight 600
inline constexpr float headerPx = 1.25f * remBasePx;  // 20px, weight 700
} // namespace type

// ---- Layout breakpoints (Story 3.7) -----------------------------------------
// [ASSUMPTION per DESIGN.md]: 900px breakpoint and 720x480 minimum are to be
// revalidated against real Decent Sampler artwork sizes in Epic 4.
namespace layout {
inline constexpr int minWidth = 720;
inline constexpr int minHeight = 480;
inline constexpr int browserBreakpoint = 900; // below: browser = overlay
} // namespace layout

// ---- Misc component metrics ------------------------------------------------
namespace metric {
inline constexpr int scrollbarThickness = 6;
inline constexpr int progressBarHeight = 2;
inline constexpr float focusRingThickness = 2.0f;
inline constexpr float focusRingOffset = 2.0f;
inline constexpr float listRowAccentWidth = 2.0f;
inline constexpr float badgeFillAlpha = 0.20f; // 20%-opacity badge fill
} // namespace metric

// Compile-time verification of the rem -> px conversion (DESIGN.md values at
// 16px base). Serves as the "pure logic" test for this header.
static_assert(remToPx(0.5f) == 8.0f, "control radius = 8px");
static_assert(remToPx(0.75f) == 12.0f, "card radius = 12px");
static_assert(remToPx(0.25f) == 4.0f, "spacing unit = 4px");
static_assert(remToPx(0.875f) == 14.0f, "body = 14px");
static_assert(remToPx(0.75f) == 12.0f, "label = 12px");
static_assert(remToPx(1.25f) == 20.0f, "header = 20px");

} // namespace fbsampler::ui::tokens
