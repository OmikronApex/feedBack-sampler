---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.6: States, errors, and drag-and-drop

Status: review

## Story

As a feedBack player,
I want clear states for empty, scanning, loading, and failure — and drag-and-drop loading,
so that the sampler always tells me what's happening in plain language (FR5).

## Acceptance Criteria

1. **Given** no folders configured, **when** the plugin opens, **then** the empty state invites "Add a library folder to get started." with a primary button; scanning shows a non-blocking banner with progress and current path, list populating incrementally.
2. **Given** a library that fails to load, **when** the list renders, **then** the row stays listed with a status dot and a human-readable reason on hover (problem + fix phrasing, selectable text, never "parse error"); the previous instrument keeps playing.
3. **Given** a library file or folder dropped anywhere on the window, **when** the drop lands, **then** it registers into the library index (single-file entry per AD-4) and loads.

## Tasks / Subtasks

- [x] Task 1: Empty state (AC: 1)
  - [x] When config service reports zero folders AND empty index: centered invite in the main area — exact copy "Add a library folder to get started." + primary "Add folder" button (opens the 3.5 folder chooser directly, not the whole settings overlay)
- [x] Task 2: Scanning banner (AC: 1)
  - [x] Non-blocking banner (top of browser pane or under header): 2px progress fill + current path text (truncated middle, selectable per UX-DR13), fed by the 3.2 scan progress callback marshalled to the message thread (`AsyncUpdater`/`MessageManager::callAsync` — callbacks arrive on the scan thread)
  - [x] List populates incrementally during scan: browser refreshes on batched progress ticks (throttle ~4 Hz — don't repaint per file)
- [x] Task 3: Loading state (AC: 2 context)
  - [x] Per-row progress fill while a library loads + small header spinner; today's load path is coarse (busy/done via ChangeBroadcaster) — show indeterminate row fill + spinner now; true per-region progress arrives with Epic 5's async loader (leave the hook shaped for a 0..1 value)
- [x] Task 4: Failure state + error copy (AC: 2)
  - [x] Load failure: index entry marked failed (3.2 status field) with a stored reason derived from Diagnostics; row keeps rendering with `low` status dot + tooltip reason on hover; previous instrument keeps playing (already guaranteed: `Engine::load` keeps the old snapshot on any Severity::Error — assert it in-test, don't reimplement)
  - [x] Error copy translation layer: map Diagnostic codes → problem + fix phrasing (pure function, e.g. missing samples → "3 samples missing — the library folder may have moved. Locate folder…"; unreadable file → "Can't read this file — check permissions or re-download the library."). NEVER surface raw parser text like "parse error"; no exclamation marks; fallback template for unmapped codes still names problem + next step
  - [x] Tooltip/error text selectable where presented persistently (tooltips can't be selected — also surface the reason in a selectable strip when the failed row is SELECTED)
- [x] Task 5: Drag-and-drop (AC: 3)
  - [x] Editor-wide `juce::FileDragAndDropTarget`: accept `.sfz`, `.sf2`, `.sf3` files and folders (folder → add as library folder + scan; recognized file → register + load). Visual drop hint (border highlight in primary) while dragging over
  - [x] Dropped file OUTSIDE any configured library folder: register into the index as a single-file entry per AD-4 (so it has an identity for later state-chunk resolution) via the config service, then load through the normal command path
  - [x] Dropped file INSIDE an existing library folder: just ensure it's indexed (incremental scan of that path) and load — no duplicate entry
  - [x] Unrecognized extension dropped: status text names the problem + supported formats, no dialog spam
- [x] Task 6: Tests (AC: 1, 2, 3)
  - [x] Error-copy mapper: pure-function Catch2 tests (known codes → expected copy shape; no "!"; unmapped code → fallback names problem + step)
  - [x] Previous-instrument-survives-failure: plugin-level test — load good library, attempt broken file, assert old snapshot still renders audio + status reflects failure (extends existing 1.5 failure test)
  - [x] Single-file registration: drop-path logic (extracted pure: path + index + folder list → action) tested for outside/inside/unrecognized cases; index round-trip shows single-file entry persisted
  - [x] Scan-progress marshalling: banner model updates from a fake scan feed without UI (pure view-model)

## Dev Notes

- **This story is mostly wiring + copy, not machinery.** Failure-keeps-playing is engine behavior since 1.4; scanning/progress comes from 3.2; rows from 3.3. The genuinely new pieces: error-copy mapper, drop handling, empty/banner states.
- **Extract view-models.** Every state decision (which state to show, what copy, drop action) must be a pure testable function/class; components only render them. This is the established epic-3 testing pattern (3.3 `LibraryFilter` precedent).
- **AD-4 single-file entry is a contract, not a convenience:** the entry minted at drop time is the identity Story 6.2's resolution will use. Store it with the same entry shape as scanned entries (root = its own parent dir, flagged `singleFile: true` if the schema needs it — coordinate with 3.2's index schema; bump index schemaVersion if extending, migration forward-only per AD-9).
- **Threading:** scan callbacks and load completion arrive off the message thread — all UI mutation via message-thread marshalling. Drop callbacks arrive on the message thread (JUCE) — but the load they trigger goes through the async loader as always.
- **Voice & tone (UX-DR13) is an AC here, not decoration:** problem + fix, no jargon, no exclamation marks, selectable. Review every string against EXPERIENCE.md's example before merging.
- **No-MIDI hint (EXPERIENCE.md, standalone, 10 s):** [ASSUMPTION]-flagged in UX and there is no standalone target yet — explicitly OUT of this story; note it in completion notes as deferred with the standalone target.
- **Read before modifying:** `plugin/ui/library_browser_pane.*` (3.3), `plugin/plugin_processor.cpp` load/status paths (Diagnostic surfacing — what text reaches `statusText_` today), `core/include/fbsampler/diagnostic.h` (code shape for the mapper), 3.2's scan progress callback signature.
- **Dependencies:** 3.2 + 3.3 required; 3.5's folder chooser reused for the empty-state button (extract the add-folder action so both call it).
- **Testing standards:** Catch2 + ctest; pure view-model tests; plugin-level tests in `tests/plugin/`; pluginval green (drag-and-drop target on the editor is exercised by editor lifecycle tests).

### Project Structure Notes

- New: `plugin/ui/error_copy.{h,cpp}` (Diagnostic → user copy), `plugin/ui/scan_banner.{h,cpp}`, `plugin/ui/empty_state.{h,cpp}`, drop-action pure logic file.
- Modified: `plugin/plugin_editor.{h,cpp}` (FileDragAndDropTarget, state switching), `plugin/ui/library_browser_pane.*` (row states, failure strip), `plugin/plugin_processor.{h,cpp}` (failure reason → index status write-back), `tests/*`.

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.6, #FR5, #UX-DR7, #UX-DR10, #UX-DR13]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/EXPERIENCE.md#State-Patterns (all five states, exact empty-state copy), #Voice-and-Tone (error copy example)]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-4 (single-file registration), #AD-9 (index write path), #Consistency-Conventions (Diagnostic shape)]
- [Source: docs/implementation-artifacts/1-5-play-it-live-in-a-daw.md (failure-keeps-host-running seed this hardens into real UX)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- Full ctest run 2026-07-20: 165/165 passed (Debug, Windows/MSVC).
- Fixed MSVC C4702 (unreachable code) in `PluginEditor::filesDropped` — single-payload loop rewritten as first-file `if`.
- Test bug caught during red phase: `ConfigService::index()` returns by value; binding `const LibraryEntry&` into the temporary dangles. Test copies the index first.

### Completion Notes List

- Empty state (`plugin/ui/empty_state.h`): exact copy "Add a library folder to get started." + primary Add-folder button that opens the 3.5 folder chooser directly; shown only when zero folders AND empty index.
- Scanning banner (`plugin/ui/scan_banner.h`): non-blocking, 2px primary activity line + current path; fed via `PluginProcessor::scanProgressText()` under `scanLock_`, notifications throttled to ~4 Hz through new pure `ui::ScanBannerModel` (`plugin/ui/scan_banner_model.h`) so text always tracks the feed while repaint ticks batch.
- Loading state: indeterminate row fill + header spinner via ChangeBroadcaster busy/done; hook left shaped for Epic 5's 0..1 per-region progress.
- Failure state: load failure writes `setEntryStatus(path, ok, detail)` back to the index; row keeps `low` status dot, tooltip via `userCopyForStatusDetail`, and a selectable failure strip appears when the failed row is selected. Previous-instrument-keeps-playing asserted in `plugin_processor_test.cpp` (not reimplemented — engine guarantee since 1.4).
- Error copy (`plugin/ui/error_copy.{h,cpp}`): pure Diagnostic→copy mapper, problem + fix phrasing, em-dash separator, no exclamation marks, raw parser text never surfaces, fallback template names problem + next step.
- Drag-and-drop: editor-wide `FileDragAndDropTarget` with primary border highlight; decision logic extracted pure into `ui::decideDropAction` (folder→addFolder, recognized outside→registerAndLoad per AD-4, inside→rescanAndLoad, unrecognized→status text naming supported formats). AD-4 single-file entry minted via `ConfigService::registerFile` (same entry shape, rootFolder = parent dir, merge-on-write, no duplicate on re-register) before the normal load path.
- Tests added this session: `tests/plugin/drop_action_test.cpp` (outside/inside/boundary/case/unsupported), `tests/plugin/scan_banner_model_test.cpp` (fake feed, text shape + 4 Hz throttle), `registerFile` round-trip persistence test in `tests/config_service_test.cpp`. Existing `error_copy_test.cpp` and previous-instrument-survives-failure test verified green.
- DEFERRED: no-MIDI hint (EXPERIENCE.md, standalone, 10 s) — [ASSUMPTION]-flagged in UX; no standalone target exists yet. Defer with the standalone target.

### File List

- plugin/ui/empty_state.h (new)
- plugin/ui/scan_banner.h (new)
- plugin/ui/scan_banner_model.h (new)
- plugin/ui/error_copy.h (new)
- plugin/ui/error_copy.cpp (new)
- plugin/ui/drop_action.h (new)
- plugin/ui/drop_action.cpp (new)
- plugin/plugin_editor.h (modified)
- plugin/plugin_editor.cpp (modified)
- plugin/plugin_processor.h (modified)
- plugin/plugin_processor.cpp (modified)
- plugin/ui/library_browser_pane.h (modified)
- plugin/ui/library_browser_pane.cpp (modified)
- plugin/CMakeLists.txt (modified)
- core/include/fbsampler/config_service.h (modified — registerFile/setEntryStatus)
- core/config/config_service.cpp (modified — registerFile/setEntryStatus)
- tests/plugin/drop_action_test.cpp (new)
- tests/plugin/scan_banner_model_test.cpp (new)
- tests/plugin/error_copy_test.cpp (new)
- tests/plugin/plugin_processor_test.cpp (modified)
- tests/config_service_test.cpp (modified)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-20: Story 3.6 complete — states (empty/scanning/loading/failure), error-copy mapper, editor-wide drag-and-drop with AD-4 single-file registration; added drop-action, scan-banner-model, and registerFile round-trip tests; extracted pure ScanBannerModel; fixed C4702 in filesDropped. 165/165 ctest green. Status → review.
