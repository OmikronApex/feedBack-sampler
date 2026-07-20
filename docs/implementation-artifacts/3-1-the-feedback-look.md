---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.1: The feedBack look

Status: review

## Story

As a feedBack user,
I want the plugin styled in the v3 design language,
so that the sampler is recognizably a feedBack product (FR20).

## Acceptance Criteria

1. **Given** the DESIGN.md token set (colors, Rubik scale, radii, 4px spacing), **when** any chrome renders, **then** it uses only token values via a shared LookAndFeel — no out-of-set colors, flat surface-step depth, gold only in SF2/SF3 badges.
2. **Given** the component inventory (buttons, knob, list row, badges, progress, scrollbars), **when** each renders in a component gallery build (a throwaway dev target — not a maintained product surface), **then** it matches DESIGN.md specs including knob gestures (drag-vertical, Shift fine, double-click reset, scroll steps, value tooltip).

## Tasks / Subtasks

- [x] Task 1: Token layer (AC: 1)
  - [x] Create `plugin/ui/tokens.h` — every DESIGN.md frontmatter value as named constants: colors (bg `#0f172a`, surface `#1e293b`, surfaceInset `#0b1220`, sidebar `#111827`, primary `#0ea5e9`, primaryHover `#38bdf8`, onAccent `#f8fafc`, destructive `#ef4444`, text `#f8fafc`, textDim `#94a3b8`, border `#334155`, good `#22c55e`, mid `#eab308`, low `#ef4444`, gold `#e8c040`, focusRing `#38bdf8`), radii (control 0.5rem, card 0.75rem, pill 999px, frame 0.35rem), 4px spacing unit, type scale (body 0.875rem, label 0.75rem, title 1rem/600, header 1.25rem/700). Convert rem → logical px at 16px base
  - [x] No raw hex anywhere else in `plugin/` — grep-enforceable rule; add a comment stating it
- [x] Task 2: Rubik font embedded (AC: 1)
  - [x] Embed Rubik weights 300–700 (Google Fonts, OFL 1.1 — AGPL-compatible; record license file alongside) via `juce_add_binary_data` CMake target; register through `juce::Typeface::createSystemTypefaceFor` in the LookAndFeel; fallback to system sans if load fails
  - [x] Tabular figures for numeric readouts (voice count, CC values): Rubik has no dedicated tabular feature reliably exposed through JUCE — use fixed-advance layout (monospaced digits via `Font::withExtraKerningFactor` is NOT sufficient; measure widest digit and pad) or accept Rubik defaults and document; decide in-code with a comment
- [x] Task 3: `FbLookAndFeel` (AC: 1, 2)
  - [x] `plugin/ui/fb_look_and_feel.{h,cpp}` deriving `juce::LookAndFeel_V4`: buttons (primary filled/hover, secondary transparent+border), rotary knob (ring track in border color, value arc in primary, label below in textDim/label-scale), text editor, combo box, popup menu, scrollbar (6px thumb border-color, hover `#475569`, transparent track), progress bar (2px primary on border track), tooltip (surfaceInset pill)
  - [x] Focus-visible: 2px focusRing outline, 2px offset — override `drawButtonBackground`/component focus paint paths so EVERY focusable component shows it (Story 3.7 depends on this being universal)
  - [x] Flat depth only: no drop shadows anywhere; depth = bg → surface → surfaceInset steps
- [x] Task 4: Reusable components (AC: 2)
  - [x] `FbKnob` (rotary slider subclass or configured `juce::Slider`): drag-vertical, Shift = fine adjust (~10x finer), double-click reset to default, mouse-wheel steps, value tooltip pill (surfaceInset) shown while dragging
  - [x] `FormatBadge` component: pill, uppercase label-scale text, 20%-opacity fill + full-strength text — SFZ primary, SF2/SF3 gold, DS good-green (DS badge ships now even though the DS frontend is Epic 4; it costs nothing)
  - [x] `LibraryListRow` paint helper: transparent base, surface hover, selected 2px left accent in primary, status dot slot (good/mid/low)
- [x] Task 5: Component gallery dev target (AC: 2)
  - [x] New CMake target `fbsampler-gallery` (GUI app, EXCLUDE_FROM_ALL or gated by `FBSAMPLER_BUILD_GALLERY` option, default OFF in CI) showing every component in all states (normal/hover/focused/disabled, badges of all three formats, progress at 0/50/100, list rows normal/selected/failed)
  - [x] Gallery links `plugin` UI sources, NOT the plugin target itself — no processor needed
- [x] Task 6: Wire into existing editor shell (AC: 1)
  - [x] Set `FbLookAndFeel` as the editor's LookAndFeel and restyle the existing throwaway editor (load button + status label) with tokens — full editor replacement is Stories 3.3/3.4; this story only proves the LookAndFeel applies plugin-wide
  - [x] pluginval stays green (editor open/close cycles exercise LookAndFeel lifetime — set/clear LookAndFeel in constructor/destructor correctly, never a dangling static)

## Dev Notes

- **AD-6 boundary:** ALL of this lives in `plugin/` — core links no JUCE GUI module and the build enforces it (`tests/ad6_guard_negative` exists; don't trip it). Never include UI headers from `core/`.
- **LookAndFeel lifetime rule (classic JUCE crash):** components must not outlive the LookAndFeel they reference. Own one `FbLookAndFeel` instance in the editor (or a SharedResourcePointer) and call `setLookAndFeel(nullptr)` in destructors before it dies.
- **JUCE 8.0.14** is the pinned version (architecture Stack table). Use `LookAndFeel_V4` as base; JUCE 8's renderer changes (Direct2D on Windows) mean text metrics can differ per platform — don't hardcode pixel text positions, use `juce::Font` metrics.
- **Existing editor:** `plugin/plugin_editor.{h,cpp}` is a deliberate Story-1.5 placeholder (file-chooser button + status label, 420×220). Restyle it, don't rebuild it — 3.3/3.4 replace the layout. Don't grow this story into the browser.
- **Gallery is throwaway by contract** (epics text: "not a maintained product surface"). Keep it one file; no tests against it; CI need not build it.
- **New subdirectory `plugin/ui/`** for tokens + LookAndFeel + components; keeps `plugin_processor`/`plugin_editor` at top level per existing layout. snake_case files, PascalCase types, camelCase functions (repo convention).
- **Fonts in CMake:** `juce_add_binary_data(FbSamplerFonts SOURCES ...ttf)` then link into the plugin target and gallery. Add the OFL license text to the repo (e.g. `plugin/ui/fonts/OFL.txt`) — NFR6 license audit (Story 7.4) will sweep it.
- **Do not add colors, shadows, or gradients beyond DESIGN.md.** Gold appears ONLY in the SF2/SF3 badge. No gamification visuals.
- **Testing:** UI is verified via the gallery + pluginval; add a small Catch2 test only for pure logic (e.g. token rem→px conversion) if any. ctest suite must stay green (currently 127/127); pluginval strictness 5 floor in CI.

### Project Structure Notes

- New: `plugin/ui/tokens.h`, `plugin/ui/fb_look_and_feel.{h,cpp}`, `plugin/ui/fb_knob.{h,cpp}`, `plugin/ui/format_badge.{h,cpp}`, `plugin/ui/library_list_row.{h,cpp}` (paint helper may start header-only), `plugin/ui/fonts/` (Rubik TTFs + OFL.txt), `plugin/gallery_main.cpp` (dev target).
- Modified: `plugin/CMakeLists.txt` (binary data + gallery target + new sources), `plugin/plugin_editor.cpp` (apply LookAndFeel).

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.1, #UX-Design-Requirements UX-DR1/2/3]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md — token frontmatter is the normative value source; Components section for per-component specs; Do's and Don'ts]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-6 (core/shell split), #Stack (JUCE 8.0.14)]
- [Source: docs/implementation-artifacts/1-5-play-it-live-in-a-daw.md (editor placeholder origin, pluginval gate)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- MSVC: `GlyphArrangement::getStringWidthInt` unavailable in JUCE 8.0.14 -> measured via `GlyphArrangement::addLineOfText` + `getBoundingBox`.
- MSVC most-vexing-parse on `const juce::Font font(juce::FontOptions(...))` -> brace-init.
- fbsampler-plugin-tests LNK2019 (FbLookAndFeel ctor) -> added ui sources + fbsampler-fonts to the test target.

### Completion Notes List

- Token layer `plugin/ui/tokens.h`: all DESIGN.md frontmatter values as named constants; rem->px at 16px base verified by static_asserts (the "pure logic" test). Grep-enforced no-raw-hex rule stated in the header; verified: no hex literals in plugin/ outside tokens.h.
- Rubik embedded as the Google Fonts variable TTF (weights 300-900 in one file) via `juce_add_binary_data` (namespace FbFonts); OFL.txt recorded at plugin/ui/fonts/OFL.txt for the NFR6 audit. Registered through `Typeface::createSystemTypefaceFor` with system-sans fallback when load fails. Tabular figures: JUCE 8 exposes no reliable variable-axis/tabular access for Rubik — accepted Rubik defaults, documented in fb_look_and_feel.h.
- `FbLookAndFeel` (LookAndFeel_V4): primary/secondary buttons, ring rotary (border track, primary arc), text editor, combo box, popup menu, 6px scrollbar (hover #475569 token), 2px progress bar, surfaceInset tooltip + slider drag bubble pill. Universal focus-visible: 2px focusRing, 2px offset via shared drawFocusRing (buttons, combo, text editor, rotary). Flat depth only — no shadows/gradients.
- `FbKnob`: drag-vertical, Shift = 10x fine (rechecked per drag event), double-click reset, wheel steps, drag-value pill via popup display. `FormatBadge`: SFZ primary / SF2+SF3 gold (only gold use) / DS good-green. `LibraryListRow`: header-only paint helper (transparent base, surface hover, 2px primary left accent, status-dot slot).
- Gallery: `fbsampler-gallery` juce_add_gui_app behind FBSAMPLER_BUILD_GALLERY (default OFF, CI skips); links UI sources + fonts, not the plugin target. Built and launched locally.
- Editor: owns FbLookAndFeel (declared first, cleared with setLookAndFeel(nullptr) in dtor before destruction — no dangling LnF), bg-token paint, gutter spacing. pluginval strictness 5: SUCCESS. ctest 128/128 green. AD-6 untouched (all work in plugin/).

### File List

- plugin/ui/tokens.h (new)
- plugin/ui/fb_look_and_feel.h (new)
- plugin/ui/fb_look_and_feel.cpp (new)
- plugin/ui/fb_knob.h (new)
- plugin/ui/fb_knob.cpp (new)
- plugin/ui/format_badge.h (new)
- plugin/ui/library_list_row.h (new)
- plugin/ui/fonts/Rubik-wght.ttf (new)
- plugin/ui/fonts/OFL.txt (new)
- plugin/gallery_main.cpp (new)
- plugin/CMakeLists.txt (modified)
- plugin/plugin_editor.h (modified)
- plugin/plugin_editor.cpp (modified)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-19: Story 3.1 implemented — v3 token layer, Rubik-embedded FbLookAndFeel, FbKnob/FormatBadge/LibraryListRow, gallery dev target, editor restyled. Tests 128/128, pluginval strictness 5 green.
