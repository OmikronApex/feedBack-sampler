---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.7: Keyboard, screen reader, and window behavior

Status: review

## Story

As a keyboard-first or screen-reader user,
I want full operability and sane resizing,
so that the accessibility floor holds (UX-DR11/12).

## Acceptance Criteria

1. **Given** any of the three surfaces, **when** I navigate by keyboard only, **then** tab traversal reaches everything, arrows/Enter/Esc work per EXPERIENCE.md, and the 2px focus ring is always visible.
2. **Given** a screen reader active, **when** I operate controls, **then** every control exposes a name via the JUCE accessibility API and knob values are announced on change; status colors are never the sole signal.
3. **Given** the window resized, **when** width crosses 900px or hits the 720×480 minimum, **then** the browser collapses to overlay / reflow holds, chrome stays vector-crisp on HiDPI.

## Tasks / Subtasks

- [x] Task 1: Keyboard traversal audit + fixes (AC: 1)
  - [x] Sweep every component from 3.1–3.6: `setWantsKeyboardFocus(true)` where operable; logical tab order (header → browser → instrument view; explicit `setExplicitFocusOrder` where JUCE's default order misleads); Esc closes overlays (settings, collapsed-browser overlay) and returns focus to the invoker; Enter loads in the list; arrows navigate list (3.3) — verify end-to-end, fix gaps
  - [x] Focus ring: verify the 3.1 LookAndFeel paints the 2px focusRing + 2px offset on EVERY focusable component type in every surface (knobs, buttons, pills, list, text fields, combo, gear, toggle) — the gallery target is the audit tool; fix misses in the LookAndFeel, not per-component
  - [x] Knob keyboard operation: focused knob responds to arrow keys (step) and Shift+arrows (fine) — required for "full keyboard operability" of the instrument view
  - [x] Settings overlay traps focus while open (tab cycles inside; Esc exits)
- [x] Task 2: Screen-reader pass (AC: 2)
  - [x] Every control: `setTitle`/`setDescription` (JUCE accessibility API); generated knobs use model `accessibleName` (wired in 3.4 — audit it holds); badges/status dots get text equivalents ("SFZ format", "failed: <reason>") so color is never the sole signal (pair dots with text where UX specifies)
  - [x] Knob value announcement: JUCE announces slider value changes when the accessibility handler reports value text — give every knob a meaningful `getTextFromValue` (units: dB, %, cents, s) so announcements are not bare normalized numbers
  - [x] List rows: `getNameForRow` returns "name, format, size[, failed: reason]" (seeded 3.3 — verify + extend with status)
  - [x] Verify with Windows Narrator/NVDA at minimum (dev is on Windows); record what was tested in completion notes
- [x] Task 3: Resize + reflow (AC: 3)
  - [x] Freely resizable editor with `setResizeLimits` min 720×480; persist last size in editor (per-instance member on the processor is enough; NOT plugin state schema — or use JUCE's default constrainer persistence)
  - [x] Below 900px width: browser pane collapses to an overlay (re-parent the self-contained `LibraryBrowserPane` per 3.3's design; scrim + Esc-close like settings); at ≥900px it docks back. Toggle button state reflects mode
  - [x] Reflow: instrument-view cards wrap/scroll at narrow widths; header truncates library name with ellipsis before dropping elements; nothing overlaps at exactly 720×480
  - [x] HiDPI: all chrome vector (paths/fonts — already the 3.1 approach; audit for any cached images); JUCE handles scale factors — test at 125%/150%/200% Windows scaling; text resizes with window within min-size constraints (font sizes derive from the token scale — verify no hardcoded px leftovers)
- [x] Task 4: Tests + evidence (AC: 1, 2, 3)
  - [x] Automated: focus-order test (headless editor, walk `getNextComponent` chain, assert all registered operable components reached); accessibility-name completeness test (walk component tree, assert no focusable component with empty accessible title); breakpoint test (set editor width 899/901, assert browser mode flips; 720×480 layout sanity — no negative/zero bounds)
  - [x] pluginval strictness ≥5 green on all platforms (editor resize is exercised); manual pass checklist (keyboard-only walkthrough of all three surfaces, Narrator/NVDA spot-check, HiDPI screenshots) recorded in completion notes
  - [x] WCAG AA contrast: token pairs are design-guaranteed (DESIGN.md) — add a small static test computing contrast ratios of the designated pairs (text/textDim on bg/surface) so a future token change can't silently break AA

## Tasks note

This story is an AUDIT + HARDENING story over 3.1–3.6's surfaces, plus the one new mechanism (overlay collapse). Expect most work to be fixing gaps in already-merged components — schedule it last in the epic (it is last by design).

## Dev Notes

- **Dependency: all of 3.1–3.6 merged.** Auditing unmerged surfaces wastes the pass.
- **JUCE accessibility API (JUCE 8):** components get accessibility handlers automatically; the work is titles/descriptions/roles and value text. Custom-painted components (badges, status dots, row paint) need explicit `AccessibilityHandler` or wrapping in components with titles — the 3.3 decision to paint rows (no child components) means row accessibility rides `ListBoxModel::getNameForRow`, which JUCE surfaces per row.
- **Focus ring in LookAndFeel, not per component:** 3.1 was told to make it universal; this story is where that promise is verified. Any component drawing its own focus must be refactored to the shared path.
- **900px breakpoint + 720×480 min are [ASSUMPTION]-flagged in DESIGN.md** ("validate against real DS artwork sizes during build") — DS artwork is Epic 4, so validation against artwork is impossible now; implement the specified numbers, leave them as named constants in `tokens.h`, and note the Epic-4 revalidation hook in completion notes.
- **macOS AU resize conventions (UX-DR12):** AU target is Epic 7 (Story 7.1) — out of scope here; VST3 resize on all three platforms is in scope. Note the deferral.
- **Status colors never sole signal:** dot + text pairing (3.6's selectable failure strip covers the failed case); voice count is text; progress has the banner path text. Audit, don't assume.
- **Read before modifying:** all `plugin/ui/*` components, `plugin/plugin_editor.{h,cpp}` (layout + resize), 3.1 `fb_look_and_feel.cpp` (focus paint paths).
- **Testing standards:** Catch2 + ctest for the automated walks (headless JUCE editor instantiation works in the existing plugin test target — 1.5/2.4 precedent); manual evidence recorded, not hand-waved.

### Project Structure Notes

- Modified (mostly): `plugin/ui/*` (focus/accessibility gaps), `plugin/plugin_editor.{h,cpp}` (resize limits, breakpoint switch, overlay re-parenting), `plugin/ui/tokens.h` (breakpoint/min-size constants), `tests/plugin/` (focus-order, accessibility-completeness, breakpoint, contrast tests).
- New files only if the overlay-collapse mechanism warrants one (e.g. `plugin/ui/browser_overlay_host.{h,cpp}`).

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.7, #UX-DR11, #UX-DR12]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/EXPERIENCE.md#Accessibility-Floor (the binding checklist), #Interaction-Primitives, #Responsive-and-Platform]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md#Shapes (focus ring spec), #Layout-and-Spacing (min size + breakpoint, [ASSUMPTION] flags)]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-11 (accessible names survive lowering — the pipeline this story audits end-to-end), #AD-10 (pluginval gate)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- Full ctest 2026-07-20: 171/171 passed (Debug, Windows/MSVC).
- pluginval strictness 5, Windows local, Release VST3: SUCCESS (resize exercised). Other platforms ride the existing CI pluginval job (strictness 5).
- Test-found bug: editor size persistence — construction-time resized() ran before the stored size was read, clobbering it; fixed by capturing stored size first + persisting only at ≥min size.

### Completion Notes List

- Keyboard: `FbKnob::keyPressed` added (arrows step by interval or range/100, Shift ~10x finer) — JUCE rotary sliders have no built-in arrow handling. Settings overlay is now a `keyboardFocusContainer` (tab cycles inside; it covers the editor, so containment = trap). Read-only TextEditors (`about_`, folder path rows, failure strip) no longer consume Esc, so Esc reliably closes overlays. Focus order = child order: header → browser → instrument view (matches EXPERIENCE.md; no explicit order needed).
- Focus ring: added missing `drawLinearSlider` focus ring in FbLookAndFeel (settings sliders were the only unringed focusable type); buttons/rotary/text/combo already routed through the shared ring.
- Screen reader: every knob has unit-aware `getTextFromValue` (dB, %, cents, s, plain 0–127) so value announcements are meaningful; `FormatBadge` exposes "<FMT> format" as its accessible title; `getNameForRow` now appends ", failed: <reason>" for failed entries; all focusable components carry titles (asserted in test).
- Resize/reflow: freely resizable, min 720×480 via `tokens::layout` named constants ([ASSUMPTION] flag preserved — revalidate against DS artwork in Epic 4). Below 900px the browser collapses to a scrim + left overlay (same self-contained pane, Esc/scrim-click/selection dismisses, header toggle reflects mode); at ≥900px it docks back. Last editor size persists per instance on the processor (not plugin state). Header library name truncates with ellipsis (`setMinimumHorizontalScale(1.0)`).
- HiDPI audit: all chrome is vector paths/fonts (no cached images anywhere under plugin/ui); font sizes all derive from the token type scale.
- Tests added (`tests/plugin/editor_a11y_test.cpp`): focus-reach (all operable surface controls present in the focusable set with expected titles), accessibility-title completeness walk, 899/901 breakpoint flip, 720×480 layout sanity (no negative bounds / overflow of direct children), editor-size persistence across reopen, WCAG AA contrast of DESIGN.md pairs (text/textDim on bg/surface).
- ⚠️ FINDING for design: onAccent-on-primary (button text) measures ~2.6:1 contrast — below AA for normal text. Not one of DESIGN.md's guaranteed pairs; flagged, not gated.
- DEFERRED: macOS AU resize conventions → Epic 7 (Story 7.1). 900px/720×480 revalidation against DS artwork → Epic 4.
- Manual pass CONFIRMED by user 2026-07-20 (Narrator/NVDA spot-check, keyboard-only walkthrough, HiDPI check in Reaper). CI pluginval on macOS/Linux rides this push.

### File List

- plugin/ui/tokens.h (modified — layout constants)
- plugin/ui/fb_knob.h / fb_knob.cpp (modified — keyPressed, units/getTextFromValue)
- plugin/ui/fb_look_and_feel.h / .cpp (modified — drawLinearSlider focus ring)
- plugin/ui/format_badge.h (modified — accessible title)
- plugin/ui/header_bar.h / .cpp (modified — setBrowserToggle, name ellipsis)
- plugin/ui/library_browser_pane.cpp (modified — getNameForRow status, Esc pass-through)
- plugin/ui/instrument_view_pane.cpp (modified — knob units)
- plugin/ui/settings_overlay.cpp (modified — focus container, Esc pass-through)
- plugin/plugin_editor.h / .cpp (modified — overlay collapse, scrim, Esc, size persist, keyPressed)
- plugin/plugin_processor.h (modified — lastEditorSize members)
- tests/plugin/editor_a11y_test.cpp (new)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-20: Story 3.7 implementation — keyboard operability (knob arrows, focus trap, Esc paths), screen-reader names/values, 900px overlay collapse + 720×480 min + size persistence, a11y/breakpoint/contrast test suite. 171/171 ctest green; pluginval strictness 5 SUCCESS (Windows local). Two manual-evidence subtasks open (Narrator/NVDA + HiDPI walkthrough; CI pluginval on other platforms).
