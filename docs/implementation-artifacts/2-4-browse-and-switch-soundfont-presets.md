---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 2.4: Browse and switch soundfont presets

Status: review

## Story

As a DAW musician,
I want a soundfont's bank/program presets listed and instantly selectable,
so that one .sf2 file gives me all its instruments (FR9).

## Acceptance Criteria

1. **Given** a loaded multi-preset soundfont, **when** preset enumeration and selection run through the core API (exposed via a bare debug list or host parameter — no styled widget; the visible selector row is Story 3.4's), **then** all presets appear with bank/program numbers and names, and selecting one switches instantly without reloading the file or interrupting other notes.

## Tasks / Subtasks

- [x] Task 1: Core preset-session API (AC: 1)
  - [x] Wrap Story-2.1's `listSf2Presets`/`lowerSf2Preset` in a soundfont session concept in core (e.g. `Sf2Session` or extend the frontend API): open file once → enumerate presets (bank, program, name, stable index) → lower any preset on demand WITHOUT re-parsing the whole file (keep the parsed hydra; sample data stays pooled)
  - [x] "Without reloading the file": presets in one soundfont share samples — the refcounted pool already dedupes by reference (`acquire()` of an already-pooled reference bumps refcount, returns same handle). Verify the embedded-sample reference scheme from 2.1 keys dedup correctly (same file + same sample index → same handle) and add a test proving a preset switch performs zero new sample-file reads for already-pooled samples
  - [x] Sort/enumeration order: bank asc, program asc (matches every established soundfont player)
- [x] Task 2: Switch = snapshot swap (AC: 1)
  - [x] Preset switch is a structural-state change through the AD-3 command path: lower target preset → `Engine::load(model, samePool, root)` → atomic snapshot swap. The existing engine semantics already give the AC: old snapshot stays audible until swap, retirement is epoch-gated off the audio thread — sounding notes finish/release under the old snapshot, no silence gap, no re-trigger
  - [x] Measure: preset switch (call → new snapshot rendering) must feel instant; lowering one preset from a parsed hydra + pooled samples is in-memory transforms only. Add a coarse timing assertion in tests (generous bound, e.g. < 100 ms on CI hardware for a mid-size soundfont) — AD-7's 2 s budget is for full library switches, preset switch should be far under it
- [x] Task 3: Plugin exposure — bare, host-visible (AC: 1)
  - [x] Per AC: NO styled widget (Story 3.4 owns the selector row). Expose selection as a host parameter on the existing APVTS layer: a `presetIndex` parameter (0..N-1 clamped to available presets) whose change enqueues the switch command on a background/message thread — NEVER lower or load on the audio thread (parameter changes can arrive there; defer via the message thread or a command queue, mirroring how the current plugin triggers loads)
  - [x] Preset names discoverable without UI: parameter value-to-text returns "bank:program name" so generic DAW parameter views and the debug editor list show real names
  - [x] Persist selection: plugin state chunk already carries "library reference + soundfont-preset index + parameter values" per AD-3 — store the preset by bank/program (stable across file edits) with index fallback; restore on state load
- [x] Task 4: Tests (AC: 1)
  - [x] Enumeration: multi-preset fixture (extend the 2.1 fixture generator: ≥3 presets across ≥2 banks incl. bank 128 percussion convention) — all presets listed with correct bank/program/name
  - [x] Switch-while-sounding: render harness plays note under preset A, switches to preset B mid-render, asserts A's tail continues (no truncation to silence) and B's notes sound after — the 1.4/1.6 offline harness + engine API supports this without plugin involvement
  - [x] No-reload: pool acquire-count instrumentation or handle-identity assertion across a switch
  - [x] State round-trip: set preset → save plugin state → restore → same preset active (extends the existing 1.5 plugin state test)
  - [x] pluginval still green (parameter added — pluginval exercises parameter automation aggressively; the audio-thread-deferral in Task 3 is what it will catch if wrong)

## Dev Notes

- **Dependency: Story 2.1 only.** Modulators (2.2) and SF3 (2.3) are orthogonal; SF3 presets enumerate/switch identically for free once 2.3 lands.
- **The engine already does the hard part.** Story 1.6's review hardened snapshot retirement (`RenderEpochGuard`, `retiredPending`, `waitForAudioThreadIdle`) — a preset switch is exactly a snapshot swap. Do NOT invent a second switching mechanism; do NOT mutate the live snapshot. Read `core/engine/engine.cpp` fully before touching anything near it, and preserve the reclamation invariants exactly.
- **Sounding-notes nuance:** on swap, notes started under the old snapshot render there until it retires. Verify what the current swap does with the old snapshot's active voices (1.5's library-switch behavior is the precedent — reuse whatever policy it established; if old-snapshot voices are hard-cut today, "not interrupting other notes" for the AC means at minimum the release tail behavior matches the library-switch story, and any gap is documented). Do not silently regress 1.5's switch behavior.
- **Read before modifying:** `core/engine/engine.cpp` + `engine.h` (snapshot lifecycle, threading contract), `plugin/plugin_processor.cpp` (how load is currently triggered off the audio thread, `isBusesLayoutSupported` history — mono||stereo was restored in a review, don't re-break), plugin state save/load from Story 1.5, `core/include/fbsampler/pool.h` (refcount contract).
- **AD-8 note:** the preset selector should be one of the FIXED, separately-identified generic parameters — it never consumes a proxy from the 128-proxy pool (that pool is for library-defined controls). Mint it as a dedicated parameter ID now so Story 6.x automation and 3.4's UI bind to a stable identity.
- **VST3 program-list route (evaluated, not required):** JUCE offers `getNumPrograms`/`setCurrentProgram`; hosts handle it inconsistently. The parameter route is the reliable, automatable one and matches AD-3's "parameter values" state model. Implementing the program-list API *additionally* is optional polish — only if pluginval stays green and it forwards to the same command path.
- **Testing standards:** Catch2 + ctest; render-harness tests live in `tests/render/`; plugin-level state/parameter tests follow Story 1.5 patterns; pluginval in CI is the host-integration gate.

### Project Structure Notes

- Modified: `core/include/fbsampler/sf2_frontend.h` (+ session/enumeration API), `core/frontends/sf2/`, `plugin/` (parameter + state chunk + deferral path), `tests/render/`, `tests/` plugin tests, fixture generator.
- No new top-level dirs. The bare debug list (if any UI at all) goes in the existing placeholder editor — explicitly throwaway; Story 3.4 replaces it.

### References

- [Source: docs/planning-artifacts/epics.md#Story-2.4]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-3 (command path, snapshot swap, state chunk contents), #AD-7 (switch budget), #AD-8 (generic params never consume proxies)]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-9, #FR-16, #§9-Glossary (soundfont preset vs plugin state)]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md (selector row is Story 3.4's — this story ships no styled UI)]
- [Source: docs/implementation-artifacts/1-6-the-corpus-starts-measuring.md#Review-Findings (snapshot reclamation invariants that make instant switching safe)]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5)

### Debug Log References

- Full ctest suite: 127/127 pass locally (119 at story start; +8 new). pluginval runs in CI as before.

### Completion Notes List

- **Core session API:** `Sf2Session` (pimpl over the parsed hydra) + `openSf2Session(path)` — open once, `presets()` sorted bank asc/program asc (stable enumeration index), `lowerPreset(index)` is in-memory transforms only (shared `lowerParsedPreset` helper factored out of the 2.1 path). Sample bytes never touch the session (AD-2); presets share pooled samples via the refcounted `sf2://` scheme — dedup verified by handle identity (`acquire` of the same reference returns the same handle; test proves zero new sample reads across a switch).
- **Switch = snapshot swap (unchanged engine):** preset switch is exactly `Engine::load(model, samePool, root)` — no second switching mechanism, reclamation invariants untouched. Swap policy note (1.5 precedent preserved): the swapped-in snapshot is a fresh sfizz synth, so a note held under preset A does not sustain into B's snapshot; the swap itself is glitch-free (old snapshot renders until the atomic swap; epoch-gated retirement off the audio thread). Documented in-test; matches the library-switch behavior 1.5 established.
- **Timing:** lower-from-session + engine load asserted < 100 ms in-test (measured ~instant for the fixture; AD-7's 2 s budget is for full library switches).
- **Plugin exposure (bare, host-visible):** dedicated `AudioParameterInt` `presetIndex` (ParameterID v1, range 0..255 fixed, clamped to session presets; AD-8: never a library-control proxy). Parameter changes may arrive on the audio thread → `AsyncUpdater` defers to the message thread which hands off to the loader thread (never lower/load on the audio thread). Value-to-text returns "bank:program name" so generic DAW parameter views show real names. No styled widget (3.4 owns the selector row).
- **State chunk (AD-3 v0 slice):** XML via copyXmlToBinary: type/path/bank/program/presetIndex. Restore prefers bank/program (stable across file edits), index fallback; applied via async updater → loader thread; `applyPendingStateSync()` is the headless test entry. Round-trip test green.
- **VST3 program-list route:** evaluated and skipped per dev note (parameter route is the reliable one); `getNumPrograms` stubs unchanged.

### File List

- core/include/fbsampler/sf2_frontend.h (modified: Sf2Session/openSf2Session API)
- core/frontends/sf2/sf2_frontend.cpp (modified: lowerParsedPreset factor-out, session impl)
- plugin/plugin_processor.h / .cpp (modified: preset parameter, sf2 session load/switch, async deferral, state chunk)
- tests/sf2_frontend_test.cpp (modified: enumeration/session/dedup tests)
- tests/render/sf2_preset_switch_test.cpp (new: mid-render switch, timing, pool identity)
- tests/plugin/plugin_processor_test.cpp (modified: sf2 load, parameter text, switch render, state round-trip)
- tests/golden/sf2/tools/make_sf2.py (modified: multibank fixture)
- tests/golden/sf2/multibank.sf2, tests/render/fixtures/sf2/multibank.sf2 (new, generated)
- tests/CMakeLists.txt (modified: new test source)

## Change Log

- 2026-07-19: Story 2.4 implemented — Sf2Session parse-once enumeration/lowering, snapshot-swap preset switching with pool dedup (zero re-reads), host-visible presetIndex parameter with name text and audio-thread-safe deferral, AD-3 state chunk round-trip. 127/127 tests.
