---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.3: The library browser

Status: review

## Story

As a DAW musician,
I want to search, filter, and load libraries from a browser pane,
so that my whole collection is playable from one list (FR8).

## Acceptance Criteria

1. **Given** an indexed collection, **when** I type in the search field or toggle format filter pills, **then** the virtualized list filters live; rows show name, format badge, size; arrows navigate; Enter or double-click loads.
2. **Given** audio playing from the current library, **when** I load another library, **then** the current one keeps sounding until the new one is ready (command path + snapshot swap), with no host dropout.

## Tasks / Subtasks

- [x] Task 1: Editor layout restructure (AC: 1)
  - [x] Replace the Story-1.5 placeholder editor layout with the real IA shell: header bar strip (48px, minimal for now — full header content is Story 3.4), left sidebar browser pane (sidebar color `#111827`), main content area (instrument view placeholder until 3.4). Editor resizable; use the 720×480 minimum from DESIGN.md (full resize/reflow behavior is Story 3.7 — just don't paint yourself into a fixed-size corner)
  - [x] Browser toggle button in the header (left) shows/hides the pane
- [x] Task 2: Browser pane (AC: 1)
  - [x] Search field (JUCE TextEditor, styled by 3.1 LookAndFeel): live filter on each keystroke, case-insensitive substring on display name
  - [x] Format filter pills (SFZ / SF2/SF3 / DS): toggleable, multiple allowed, none active = all formats; DS pill present but naturally empty until Epic 4
  - [x] Virtualized list: `juce::ListBox` (it virtualizes — only visible rows are painted/created) with the 3.1 `LibraryListRow` paint (name, format badge, size right-aligned in textDim; failed rows show status dot — full failure UX is Story 3.6, render the dot slot now)
  - [x] Data source: the 3.2 index via the processor accessor; refresh on rescan/change notification (ChangeBroadcaster pattern already used for load status)
- [x] Task 3: Keyboard + mouse interaction (AC: 1)
  - [x] Up/Down arrows move selection; Enter loads selection; single-click selects; double-click loads; typing in search keeps list navigable (arrow keys from the search field move list selection — EXPERIENCE.md "type-to-search filters live")
  - [x] Selection loads and returns focus to the instrument view area (EXPERIENCE.md IA)
- [x] Task 4: Load through the existing command path (AC: 2)
  - [x] Dispatch by entry format: `.sfz` → `loadSfzFileAsync`, `.sf2`/`.sf3` → `loadSf2FileAsync`. NO new loading mechanism — these already run on the loader thread and swap immutable snapshots (AD-3); the old snapshot keeps rendering until the new one is accepted
  - [x] Row shows in-flight state (reuse `isLoadInProgress` + change notifications; per-row progress fill polish belongs to Story 3.6)
  - [x] Guard: a second load while one is in flight — current processor returns false; browser must surface that gracefully (queue or ignore+status, pick one, document)
- [x] Task 5: Tests (AC: 1, 2)
  - [x] Filter logic extracted into a pure, testable function/class (`LibraryFilter`: query + format set → filtered indices) with Catch2 tests — don't bury filtering inside the ListBox model
  - [x] Switch-while-sounding at plugin level: extend `tests/plugin/plugin_processor_test.cpp` — render notes from library A, trigger load of B mid-render, assert no dropout/silence gap in A's tail and B active after (the 2.4 switch test is the template; this exercises the sfz↔sf2 cross-format path too)
  - [x] pluginval green (editor now has real focus traversal + a ListBox — pluginval cycles editor open/close)

## Dev Notes

- **The engine already guarantees AC 2.** Snapshot swap + epoch-gated retirement landed in 1.4–1.6 and was re-verified in 2.4. Do NOT build any new switching machinery; the browser is a *front end to the existing command path*. Known bound (documented in 2.4): notes held under the old snapshot don't sustain into the new one — the swap itself is glitch-free; full crossfade-on-ready polish is Epic 5 (Story 5.3). Match 2.4's documented behavior, don't try to exceed it here.
- **Read before modifying:** `plugin/plugin_editor.{h,cpp}` (placeholder being replaced — keep the load-status ChangeListener wiring), `plugin/plugin_processor.h` (load API, `currentPresets`, change broadcasting), Story 3.2's index accessor once merged.
- **Dependencies:** 3.1 (LookAndFeel/components) and 3.2 (index) must land first. If 3.2's plugin accessor shape shifts, the browser only consumes `struct LibraryEntry {name, format, sizeBytes, path, status}` — keep coupling that thin.
- **ListBox specifics (JUCE 8):** implement `juce::ListBoxModel`; `paintListBoxItem` uses the 3.1 row paint helper — no per-row child components (keeps it virtualized and cheap). Row height ~28–32px per 4px grid. Keyboard: ListBox has built-in arrow handling; forward Enter/double-click via `listBoxItemDoubleClicked` + `returnKeyPressed`.
- **Accessibility seed (full floor is 3.7):** give search field, pills, and list rows accessible names now — retrofitting is costlier. ListBoxModel `getNameForRow` returns "name, format, size".
- **The sidebar collapses to overlay below 900px** — that reflow is Story 3.7's; structure the browser as a self-contained `Component` (`LibraryBrowserPane`) so 3.7 can re-parent it into an overlay without rework.
- **No audio-thread contact:** all browser actions run on the message thread and hand off to the loader thread via the existing async API. Never call sync load from UI.
- **Voice & tone:** no error copy invented here — failure surfaces are Story 3.6's; keep any interim status text within UX-DR13 (plain problem+fix language, no "parse error").
- **Testing standards:** Catch2 + ctest; plugin-level tests in `tests/plugin/`; UI logic must be extracted to pure classes for testing (the editor itself is exercised by pluginval, not unit tests).

### Project Structure Notes

- New: `plugin/ui/library_browser_pane.{h,cpp}`, `plugin/ui/library_filter.{h,cpp}` (pure logic), `tests/plugin/library_filter_test.cpp` (or fold into plugin test target).
- Modified: `plugin/plugin_editor.{h,cpp}` (IA shell layout), `plugin/CMakeLists.txt`, `tests/plugin/plugin_processor_test.cpp` (cross-library switch test), `tests/CMakeLists.txt`.

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.3, #FR8, #UX-DR5]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/EXPERIENCE.md#Information-Architecture (browser is surface 2), #Component-Patterns (library list behavior), #Interaction-Primitives]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md#Components (list row, badges, scrollbar)]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-3 (command path — the load mechanism this story must reuse)]
- [Source: docs/implementation-artifacts/2-4-browse-and-switch-soundfont-presets.md#Completion-Notes (swap behavior precedent, switch-test template)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- None of note; single-line TextEditor bubbles unconsumed arrow/Return keys to the parent, which is how search-field arrows drive list selection without a KeyListener.

### Completion Notes List

- Editor rebuilt as the IA shell: 48px header strip (surface tone + hairline; wordmark label + "Library" browser toggle — full header content is 3.4), left LibraryBrowserPane (sidebar #111827 token, 260px), main content component holding the 1.5 placeholder (load button + status) until 3.4. Resizable with 720x480 minimum (setResizeLimits); reflow/overlay collapse deferred to 3.7 as specced.
- LibraryBrowserPane self-contained (3.7 can re-parent into overlay): styled search TextEditor with live case-insensitive filtering, three toggleable filter pills (SFZ / SF2/SF3 / DS — DS present, empty until Epic 4, none active = all), virtualized juce::ListBox (28px rows, paintListBoxItem via 3.1 LibraryListRow — no per-row components), failed rows render the low status dot slot.
- Filter logic extracted to pure JUCE-free LibraryFilter (query substring + pill mask -> indices) with 4 Catch2 cases (empty, case-insensitivity, pill gating incl. sf2+sf3 union pill and empty DS, AND combination).
- Keyboard/mouse: arrows move selection (from search field too — unconsumed keys bubble to the pane), Enter + double-click load, load returns focus to the content area. Accessibility seed: titles on search/pills/list, getNameForRow returns "name, format, size".
- Loads dispatch ONLY through the existing async command path (sfz -> loadSfzFileAsync, sf2/sf3 -> loadSf2FileAsync); no new machinery. Guard documented in code: load-while-busy is ignored (processor returns false + status text), no queue. Data source: processor.configService().index() refreshed on every ChangeBroadcaster notification (covers rescan + load status).
- Cross-format switch test added to plugin_processor_test.cpp: seed sfz sounding, sf2 loaded async mid-render, no silent block during the switch, sf2 presets active + audible after, busy-guard returns false. Suite 145/145; pluginval strictness 5 SUCCESS (editor open/close with ListBox + focus traversal).

### File List

- plugin/ui/library_filter.h (new)
- plugin/ui/library_filter.cpp (new)
- plugin/ui/library_browser_pane.h (new)
- plugin/ui/library_browser_pane.cpp (new)
- plugin/plugin_editor.h (modified — IA shell)
- plugin/plugin_editor.cpp (modified — IA shell)
- plugin/CMakeLists.txt (modified)
- tests/plugin/library_filter_test.cpp (new)
- tests/plugin/plugin_processor_test.cpp (modified — cross-format switch test)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-19: Story 3.3 implemented — library browser pane (search, pills, virtualized list), IA shell editor, pure filter logic, cross-format switch test. 145/145 tests, pluginval green.
