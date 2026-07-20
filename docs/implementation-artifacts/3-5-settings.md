---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.5: Settings

Status: review

## Story

As a DAW musician,
I want a settings overlay for folders, voice limit, and memory,
so that I configure once and play everywhere (FR21).

## Acceptance Criteria

1. **Given** the settings overlay open (scrim + hairline per DESIGN.md), **when** I add/remove folders, trigger rescan, or adjust voice limit and RAM/streaming threshold, **then** changes persist to the shared store via the config service and take effect without plugin reload; about/licenses (AGPL notices) are shown with selectable text.

## Tasks / Subtasks

- [x] Task 1: Overlay chrome (AC: 1)
  - [x] `SettingsOverlay` component: full-window scrim (black 60%) + centered panel (surface color, card radius, hairline border) per DESIGN.md Elevation; opened by the header gear (3.4), closed by Esc / scrim click / close button; keyboard focus trapped inside while open (3.7 will audit)
- [x] Task 2: Library folders section (AC: 1)
  - [x] List of configured folders (from 3.2 config service) with per-row remove; "Add folder" button → native directory chooser (async `juce::FileChooser` — the sync variants are deprecated/blocked in JUCE 8); "Rescan" button triggers the 3.2 incremental rescan on a background thread
  - [x] Remove is destructive-adjacent: button styled with destructive color per DESIGN.md ("remove folder" is the named destructive example); removing a folder removes its entries from the index on next write — confirm inline (small "Remove?" confirm state, not a modal-on-modal)
  - [x] Adding a folder triggers a scan of it immediately (first-run behavior per 3.2); progress surfaces via 3.6's banner (until 3.6 lands: plain status text)
- [x] Task 3: Voice limit + RAM/streaming threshold (AC: 1)
  - [x] Voice-limit control (slider/stepper, sensible range e.g. 16–256, default = engine default): persists via config service `voiceLimit` and applies live — sfizz has a num-voices setting; apply on the control thread through the engine (verify where sfizz's voice-count setter is safe to call — likely requires the same non-concurrent-with-process contract as `prepare`; if unsafe live, apply via the snapshot/load path and document)
  - [x] RAM/streaming threshold (MB): persists as `ramStreamThresholdMb` NOW, consumed by Epic 5's streaming pool — label the control honestly (it has no audible effect until streaming lands); tooltip/help text says so in UX-DR13 voice
  - [x] "Take effect without plugin reload": settings write-through on change (config service single write path), voice limit applied live per above
- [x] Task 4: About / licenses (AC: 1)
  - [x] About section: plugin name/version (from `fbsampler/version.h`), schema/build info, AGPL-3.0 notice, and license texts/attributions for bundled components (JUCE, sfizz, stb_vorbis, Rubik/OFL, Catch2 in test builds only). Keep the notice list in a maintainable single place (e.g. generated header or checked-in `licenses.h`) — Story 7.4's audit extends it
  - [x] ALL text in about + folder paths selectable/copyable (UX-DR13 / v3 policy): use read-only `juce::TextEditor` (multi-line, borderless, styled) — `juce::Label` is NOT selectable
- [x] Task 5: Cross-instance behavior (AC: 1)
  - [x] Settings written by this instance are read by the next instance start (3.2's merge-on-write); no live cross-instance push in v1 (index/settings re-read on rescan or restart — matches FR10's no-background-watching decision). Document this bound in the code
- [x] Task 6: Tests (AC: 1)
  - [x] Settings round-trip through the real config service in a temp dir (set voice limit + threshold + folders → new service instance reads them)
  - [x] Voice-limit application: plugin-level headless test — set limit N, exceed with note-ons, assert active voices ≤ N (also seeds Story 5.4's ground)
  - [x] pluginval green (overlay open/close within editor lifecycle)

## Dev Notes

- **Dependency:** 3.2 (config service — the ONLY persistence path; never write JSON from the UI) and 3.1 (styling). Gear button exists in 3.4's header; if 3.4 isn't merged, mount the overlay from a temporary header stub and rebase.
- **AD-9 discipline:** the settings surface is exactly the write path feedBack will drive later — everything goes through `ConfigService`, no side files, no APVTS involvement (these are per-user machine settings, NOT plugin state; a DAW project must not capture voice limit or folders — do not put them in the state chunk).
- **Voice limit is a real engine question:** read `core/engine/engine.cpp` Impl before wiring — sfizz `setNumVoices` exists but resizes internals; calling it concurrently with `process()` is not safe. Safe patterns: apply during the prepare/load control-thread window, or enqueue to the loader thread and treat as a mini structural change. Choose, test, document. Header voice-count readout (3.4) gives immediate visual feedback that it applied.
- **Async FileChooser lifetime:** store the `FileChooser` as a member while the async callback is pending (stack-local chooser + async = classic JUCE crash).
- **Read before modifying:** `plugin/plugin_editor.{h,cpp}` (current shell), `core/include/fbsampler/config_service.h` (3.2's API), `core/engine/engine.{h,cpp}` (voice-limit application point), `core/include/fbsampler/version.h`.
- **Voice & tone:** studio-precise copy; no exclamation marks; e.g. threshold help: "Libraries larger than this stream from disk once streaming ships. Stored now, applied in a later update." — honest, plain.
- **Testing standards:** Catch2 + ctest; config tests against temp dirs (3.2 pattern); plugin-level tests in `tests/plugin/`.

### Project Structure Notes

- New: `plugin/ui/settings_overlay.{h,cpp}`, `plugin/ui/licenses.h` (or generated).
- Modified: `plugin/plugin_editor.{h,cpp}` (overlay mount + gear wiring), `plugin/plugin_processor.{h,cpp}` (voice-limit apply path), `plugin/CMakeLists.txt`, `tests/plugin/plugin_processor_test.cpp`, `tests/config_service_test.cpp` (settings round-trip if not already covered).

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.5, #FR21, #FR10, #UX-DR6]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md#Elevation (scrim + hairline), #Colors (destructive usage), Do's and Don'ts (selectable text)]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/EXPERIENCE.md#Information-Architecture (surface 3: folders, voice limit, RAM/streaming threshold, about/licenses)]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-9 (single write path), #AD-3 (what plugin state may NOT contain), #AD-7 (voice limit is one of exactly two memory knobs)]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md (AGPL licensing matrix for the about view)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- unique_ptr<FolderRow> with in-header dtor -> C2338 incomplete-type delete; fixed with out-of-line ~SettingsOverlay.
- Gallery target linked settings_overlay (which references PluginProcessor) -> LNK2019; split UI sources into gallery-safe vs processor-dependent lists.

### Completion Notes List

- SettingsOverlay: full-window 60% black scrim + centered surface panel with hairline border and card radius (DESIGN.md Elevation). Opened by the 3.4 gear; closed by Esc / scrim click / Close button; overlay covers the whole editor so keyboard focus stays inside (3.7 audits). Close returns focus to the instrument view.
- Folders: rows list ConfigService settings; per-row selectable path (read-only TextEditor); Remove styled destructive with inline two-step "Remove -> Remove?" confirm (no modal-on-modal); removing persists via ConfigService and triggers a rescan (entries under the removed root drop from the index on the scan write). Add folder = member-owned async juce::FileChooser (lifetime-safe), persists, then scans immediately (3.2 first-run behavior). Rescan button = processor.rescanLibrariesAsync(); progress = plain status text until 3.6's banner.
- Voice limit: engine gained setVoiceLimit (clamped 1..256), applied to each NEWLY BUILT snapshot's sfizz synth — sfizz setNumVoices resizes internals and is not audio-thread-concurrent-safe, so the documented live path is processor.applyVoiceLimit{Sync,Async}: store limit, rebuild the current snapshot on the loader thread (mini structural change). Persisted voiceLimit is read at processor startup and applied to every future snapshot. Slider 16-256, write-through on drag end.
- RAM/streaming threshold: persists ramStreamThresholdMb (Epic 5 consumer); honest help copy in UX-DR13 voice ("Stored now, applied in a later update."). No audible effect claimed.
- About: coreVersion() + model schema + AGPL notice + bundled-component attributions in single maintainable plugin/ui/licenses.h (7.4 extends); shown in a read-only multi-line TextEditor — selectable/copyable, as are folder paths.
- Cross-instance bound documented in the header comment: no live push in v1; other instances pick up settings on next rescan/restart (FR10 no-background-watching).
- AD-9/AD-3 discipline: everything persists through ConfigService; nothing enters the plugin state chunk; no APVTS involvement.
- Tests: settings round-trip already pinned by 3.2's config tests (voiceLimit/threshold/folders across service instances); new plugin-level voice-limit cap test (limit 4, pile on note-ons, activeVoiceCount <= 4 — seeds Story 5.4). Suite 153/153; pluginval strictness 5 SUCCESS (overlay lives inside editor lifecycle).

### File List

- plugin/ui/settings_overlay.h (new)
- plugin/ui/settings_overlay.cpp (new)
- plugin/ui/licenses.h (new)
- core/include/fbsampler/engine.h (modified — setVoiceLimit)
- core/engine/engine.cpp (modified — per-snapshot voice limit)
- plugin/plugin_processor.h (modified — applyVoiceLimit paths, startup apply)
- plugin/plugin_processor.cpp (modified)
- plugin/plugin_editor.h (modified — overlay mount)
- plugin/plugin_editor.cpp (modified)
- plugin/CMakeLists.txt (modified — processor-dependent UI source split)
- tests/plugin/plugin_processor_test.cpp (modified — voice-limit test)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-19: Story 3.5 implemented — settings overlay (folders/rescan, live voice limit via snapshot rebuild, persisted streaming threshold, selectable about/licenses). 153/153 tests, pluginval green.
