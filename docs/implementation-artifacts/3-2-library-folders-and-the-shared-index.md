---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.2: Library folders and the shared index

Status: review

## Story

As a feedBack player,
I want to designate library folders that are scanned into a persistent list,
so that all my libraries — all four formats — are known in one place, shared between feedBack and any DAW (FR7, FR19).

## Acceptance Criteria

1. **Given** one or more designated folders, **when** a recursive scan runs, **then** all recognized libraries land in `library-index.json` (per-user config dir, atomic writes, generation counter, merge-on-write per AD-9) with name, format, and size.
2. **Given** a second plugin instance started in another host after the first has scanned, **when** it starts, **then** it reads the same index without rescanning; a user-triggered rescan is incremental and only re-examines changed entries (FR10). (Concurrent-write safety is deliberately verified later, by Story 7.3's two-instance test.)

## Tasks / Subtasks

- [x] Task 1: Config service skeleton in core (AC: 1)
  - [x] `core/config/config_service.{h→include/fbsampler/config_service.h, cpp}` — owns `settings.json` + `library-index.json` in the platform per-user config dir (Windows `%APPDATA%/feedBack Sampler/`, macOS `~/Library/Application Support/feedBack Sampler/`, Linux `$XDG_CONFIG_HOME|~/.config/feedback-sampler/`). Public API is JUCE-free (AD-6): std types + fbsampler types only. Internally may use JUCE non-GUI modules (juce_core `File`/`JSON`) OR hand-rolled JSON — prefer juce_core since it's already linked to core-internal deps; keep it out of public headers
  - [x] Config-dir override hook for tests (env var or injectable base path) — tests MUST NOT write the real user config dir
  - [x] Both files carry `schemaVersion` (start at 1) + monotonic `generation` counter. Unknown NEWER schema → fail soft: read-only mode + Diagnostic (never crash, never rewrite the file)
- [x] Task 2: Atomic merge-on-write (AC: 1, 2)
  - [x] Single write path: temp file in same dir → atomic rename (`ReplaceFileW`/`rename`); on write, RE-READ current file, merge (per-entry merge for index: union by entry identity, newest `scannedAt` wins per entry; last-writer-wins per setting), bump generation, then rename. This is the AD-9 contract Story 7.3 will stress — get the re-read-merge-rename ordering right now
  - [x] Entry identity: absolute canonical path of the library file (the index is a local machine artifact — absolute paths are fine HERE; AD-4's relative-identity rule governs the plugin STATE chunk, not the index). Store per entry: path, root folder it was found under, format, display name, size bytes, mtime, `scannedAt`, and status (ok | failed + diagnostic summary)
- [x] Task 3: Scanner (AC: 1, 2)
  - [x] Recursive scan of designated folders recognizing by extension: `.sfz`, `.sf2`, `.sf3` (`.dspreset`/`.dslibrary` recognition lands with Epic 4 — leave the format enum and dispatch extensible, add a code comment). Display name: filename stem for sfz; for sf2/sf3 read the INAM chunk via the existing parser if cheap, else stem — do NOT fully lower during scan
  - [x] Scan must be cheap and safe: stat + extension + (optionally) header sniff only; never load sample data; a scan of thousands of files completes in seconds; unreadable files/dirs produce Diagnostics, never throw across the core API
  - [x] Incremental rescan: compare stored size+mtime per known entry, re-examine only new/changed/removed paths; full scan only for folders never scanned. First run (empty index) scans everything
  - [x] Scan runs on a caller-provided/background thread with a progress callback (current path, counts) — Story 3.6's banner consumes this; NEVER on the audio thread
- [x] Task 4: Settings surface in the service (AC: 1)
  - [x] `settings.json` v1 keys: `libraryFolders[]`, `voiceLimit` (default from engine), `ramStreamThresholdMb` (persisted now, consumed by Epic 5) — Story 3.5's UI reads/writes through this service only
- [x] Task 5: Plugin integration (AC: 2)
  - [x] Processor (or a small owner object in the shell) instantiates the config service, reads the index at startup WITHOUT rescanning; exposes index + rescan trigger to the editor (Story 3.3 consumes). First-run (no config at all) → auto-scan is triggered on first folder add, not at startup
- [x] Task 6: Tests (AC: 1, 2)
  - [x] Catch2, against a temp config dir: round-trip settings + index; atomic write leaves no partial file on simulated failure (write temp, kill before rename → original intact); merge-on-write preserves a foreign entry added between read and write; generation strictly increases; newer-schema file → read-only + Diagnostic
  - [x] Scanner: fixture tree with nested sfz/sf2/sf3 + junk files → correct entries; touch one file → incremental rescan re-examines only it; deleted file → entry removed (or marked missing — pick one, document; recommend: removed from index, AD-4 state-chunk resolution handles missing separately)
  - [x] Second-instance read: construct service #2 on same dir → sees identical index, no scan performed (assert via scan-callback never firing)

## Dev Notes

- **This story creates `core/config/` from empty** (.gitkeep today). It is the single most contract-laden story in the epic: AD-9 is quoted in full in the References — read it before designing. One write path, versioned JSON, forward-only migration, fail-soft on newer schema.
- **Public API JUCE-free (AD-6):** `include/fbsampler/config_service.h` speaks std + `Diagnostic` only. The existing AD-6 guard (`tests/ad6_guard_negative`) enforces no GUI modules; keep juce_core usage in the .cpp.
- **Do NOT put the index in plugin state.** AD-3: host-saved chunk contains only library reference + preset + params. The index is shared per-user config precisely so DAW projects never embed the library list.
- **Diagnostic convention:** severity + stable code + message + location; no exceptions across the core API (architecture Consistency Conventions). Scanner and service return/accumulate Diagnostics.
- **Threading:** the service is called from message/loader threads only. Internal mutex for the in-memory copy is fine; no audio-thread access ever exists.
- **feedBack app note:** feedBack manages folders through the hosted plugin's settings surface, never by writing these files itself (AD-9) — no extra API surface needed for it.
- **SQLite is explicitly deferred** (spine Deferred) — JSON now; keep the service API storage-agnostic so a backend swap stays internal.
- **Windows paths:** canonicalize consistently (juce File does this); index may be read by instances launched from different working dirs. Watch case-insensitivity on Windows for entry identity — compare canonicalized.
- **Testing standards:** Catch2 + ctest, tests in `tests/` (new `tests/config_service_test.cpp`, `tests/library_scanner_test.cpp`); fixtures generated in-test in temp dirs (existing tests' pattern); full suite currently 127/127 — keep green.

### Project Structure Notes

- New: `core/include/fbsampler/config_service.h` (service + index/settings types + scanner API — or split `library_index.h`), `core/config/config_service.cpp`, `core/config/library_scanner.cpp`, `tests/config_service_test.cpp`, `tests/library_scanner_test.cpp`.
- Modified: `core/CMakeLists.txt` (config sources), `plugin/plugin_processor.{h,cpp}` (service ownership + index accessor + rescan trigger), `tests/CMakeLists.txt`.

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.2, #FR7, #FR10, #FR19]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-9 (the binding contract for this story), #AD-3 (what plugin state may contain), #AD-4 (index registers dropped files — Story 3.6 consumes), #AD-6, #Consistency-Conventions (Diagnostic, schema versions)]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md#One-engine-one-pool-one-snapshot (shared config rationale)]
- [Source: docs/implementation-artifacts/2-4-browse-and-switch-soundfont-presets.md (state-chunk shape this must NOT duplicate; loader-thread pattern in the processor)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- ConfigService is non-copyable (unique_ptr Impl, deleted copy) — test helper returning it by value failed; switched to configure-in-place helper.

### Completion Notes List

- Core stays fully JUCE-free: config storage uses a small hand-rolled strict JSON reader/writer internal to core/config (core never linked juce_core before; adding it would compile JUCE module sources into the static lib and collide with the plugin's own JUCE objects). Storage-agnostic service API per AD-9 deferred-SQLite note.
- `ConfigService` (public header `include/fbsampler/config_service.h`, std + Diagnostic only, AD-6 clean): owns settings.json + library-index.json in the platform per-user dir (Windows %APPDATA%/feedBack Sampler, macOS Application Support, Linux XDG); FBSAMPLER_CONFIG_DIR env var + constructor base-dir override for tests.
- AD-9 write path: serialize -> temp file in same dir -> fs::rename (atomic replace on MSVC STL/POSIX). Every write re-reads the disk file first and merges: settings last-writer-wins per setting with generation = max(disk, mem)+1; index per-entry union keyed by canonical path (lowercased on Windows), newest scannedAt wins, scanned roots authoritative (deleted files leave the index — documented choice; AD-4 handles missing-at-restore). Generation strictly increases; stray temp files never shadow reads.
- Newer schemaVersion -> read-only mode + Diagnostic, file never rewritten; corrupt file -> Diagnostic, last good in-memory copy retained. schemaVersion 1 on both files.
- Scanner: recursive, extension dispatch (.sfz/.sf2/.sf3 case-insensitive; .dspreset/.dslibrary marked for Epic 4 in code), stat + bounded 64KiB RIFF INAM sniff for sf2/sf3 display names — never loads sample data, never throws; unreadable dirs/files -> Diagnostics; skip_permission_denied. Incremental: size+mtime match carries the stored entry without re-examination. Progress callback (path + counts) on caller's thread for Story 3.6's banner.
- Plugin: processor constructs ConfigService at startup (reads index, NEVER auto-scans), exposes configService() + rescanLibrariesAsync() on the existing loader thread with loadBusy_ guard + ChangeBroadcaster completion (Story 3.3 consumes).
- Tests: 12 new Catch2 cases in temp dirs — settings/index round-trip, strictly increasing generation, stray-temp crash simulation leaves original intact, foreign-entry merge preservation, newer-schema read-only fail-soft, corrupt-file retention, second-instance reads without scan, fixture-tree recognition incl. INAM names + case-insensitive extension, incremental rescan touches only changed file, deleted-entry removal, missing-folder diagnostic, progress counts. Suite 140/140; pluginval strictness 5 SUCCESS.

### File List

- core/include/fbsampler/config_service.h (new)
- core/config/json.h (new)
- core/config/json.cpp (new)
- core/config/config_service.cpp (new)
- core/config/scanner.h (new)
- core/config/library_scanner.cpp (new)
- core/CMakeLists.txt (modified)
- plugin/plugin_processor.h (modified)
- plugin/plugin_processor.cpp (modified)
- tests/config_service_test.cpp (new)
- tests/library_scanner_test.cpp (new)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-19: Story 3.2 implemented — AD-9 config service (atomic merge-on-write JSON), incremental library scanner, processor integration. Tests 140/140, pluginval green.
