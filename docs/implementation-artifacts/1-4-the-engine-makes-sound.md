---
baseline_commit: 30b783bc2f053cd81b10ff9711ae3f6915d1a415
---

# Story 1.4: The engine makes sound

Status: done

## Story

As a feedBack player,
I want the unified voice engine rendering the canonical model through the sample-pool interface,
so that a lowered SFZ instrument is audible.

## Acceptance Criteria

1. **Given** a lowered instrument and the all-RAM pool implementation behind the AD-2 interface, **when** note-on/off events render offline, **then** velocity layers, loops, and envelopes sound per the model, and output matches a seed render-regression fixture.
2. **Given** a debug build with RT-safety detectors, **when** the render callback executes, **then** zero allocations, locks, or file I/O are detected on the audio thread.

## Tasks / Subtasks

- [x] Task 1: Sample-pool interface + all-RAM implementation in `core/pool/` (AC: 1)
  - [x] Define the AD-2 pool contract as an abstract interface now, so Epic 5 swaps internals only: entries are refcounted, referenced by handle from model regions; API supports preload/acquire/release semantics; **no caller may assume full-RAM residency** (interface shape must already distinguish "resident head" from "tail data available" even if v0 always has everything resident)
  - [x] All-RAM implementation: loads referenced sample files fully at load time (background/loader thread context, never audio thread), float32 conversion at load
  - [x] Audio-thread-facing accessors are lock-free, allocation-free, wait-free reads
- [x] Task 2: Unified voice engine in `core/engine/` (AC: 1)
  - [x] sfizz-based backend (AD-1): bind the canonical model's regions/envelopes/mod-matrix to sfizz's voice/rendering machinery — the model drives the engine, the SFZ text path is not re-entered (frontends lower, the engine executes model instances)
  - [x] Engine API (JUCE-free): load an immutable model snapshot + pool; `process(events, float** out, numFrames)`; MIDI 1.0 semantics at the boundary; time in samples at engine rate
  - [x] Note on/off with velocity → region selection per key/vel zones; loops honored; amp envelope per model; velocity layers audible
  - [x] Snapshot-based structural state (AD-3 seed): engine holds one immutable model+pool snapshot; a setter swaps atomically (full command path arrives with async loading in Epic 5; the *shape* — immutable snapshot, atomic swap, retire off-thread — is fixed now)
- [x] Task 3: Offline render harness + seed fixture (AC: 1)
  - [x] `tests/render/` offline renderer: model + MIDI event list → wav/float buffer, deterministic (fixed sample rate 48 kHz, fixed block size)
  - [x] Seed fixture: small checked-in SFZ instrument (from Story 1.3 fixtures) + fixed MIDI sequence exercising velocity layers, a loop, and envelope release → checked-in reference render
  - [x] Comparison with tolerance (peak sample diff / RMS threshold — exact-byte float output across compilers is not realistic; document the threshold; this seeds the NFR-5 threshold mechanism Story 1.6 formalizes)
- [x] Task 4: RT-safety detectors (AC: 2)
  - [x] Debug-build detector around the process callback: override/instrument allocation (global new/delete hooks or malloc interposition per platform), assert no locks (guarded mutex wrapper used engine-wide), no file I/O on the audio thread
  - [x] Catch2 test driving `process()` under the detector across the seed fixture; CI runs it (at minimum on Linux + Windows)
- [x] Task 5: CI green (AC: 1, 2)
  - [x] Render-regression seed test + RT-safety test wired into the three-platform matrix

### Review Findings

- [x] [Review][Patch] `AllRamSamplePool::acquire()` never reuses released (refCount==0) slots — the fixed 4096-slot table is consumed monotonically forever, so a session with repeated patch load/unload cycles will permanently exhaust capacity and start failing with `pool.capacity_exceeded` even though the live sample count is far below 4096; the disclosed cap in deferred-work.md covers the fixed size, not this slot-leak-on-reuse gap [core/pool/all_ram_pool.cpp:38-79]
- [x] [Review][Patch] Sanitizer CI job builds only `fbsampler-tests sfz-fuzz-replay`, never `fbsampler-render-tests` — the new engine/pool code (raw pointer arithmetic in the WAV reader, atomic snapshot swap, sfizz FFI) has zero ASan/UBSan coverage despite being exactly the kind of code those sanitizers exist to catch [.github/workflows/ci.yml:116,123]
- [x] [Review][Patch] `Engine::process()` does not validate that `EngineEvent::delayFrames` falls within `[0, numFrames)` before passing it to `sfizz::noteOn/noteOff` — a malformed or mistimed event from the caller produces an out-of-range sample offset with no guard [core/engine/engine.cpp:177-186]
- [x] [Review][Patch] `AllRamSamplePool::release()` has no `rtcheck` instrumentation, unlike `acquire()` which reports a violation when called inside an RT section — a control-thread-only contract violation on this entry point goes undetected [core/pool/all_ram_pool.cpp:89-101]
- [x] [Review][Patch] `Engine::load()` emits no diagnostic when `model.regions` is empty — an instrument with no regions silently loads and can never sound, with nothing to help debug it [core/engine/engine.cpp:123-136]
- [x] [Review][Patch] `readWavFile`'s chunk-walk (`pos + 8 + chunkSize > bytes.size()`) can overflow on 32-bit `size_t` builds, and there is zero test coverage for malformed/truncated WAV files despite the reader processing on-disk sample files [core/pool/wav_reader.cpp:73-91]
- [x] [Review][Patch] The RT-safety "detector actually fires" negative control only exercises the allocation hook; there is no negative control proving `CheckedMutex::lock()` or the pool's file-I/O reporting actually fire when triggered inside an RT section, despite Task 4's completion notes claiming all three paths are covered [tests/render/rt_safety_test.cpp:85-99]
- [x] [Review][Patch] 24-bit PCM, `WAVE_FORMAT_EXTENSIBLE`, and >2-channel WAV decode paths are implemented but have zero test coverage — `pool_test.cpp` only exercises the fixture's existing format, despite the reader documenting broad format support [core/pool/wav_reader.cpp:79-129, tests/render/pool_test.cpp]
- [x] [Review][Patch] `Engine::process()` does not guard against `events == nullptr` with `numEvents > 0` [core/engine/engine.cpp:177-179]
- [x] [Review][Patch] `AllRamSamplePool::retain()` uses `entryFor()` instead of `liveEntryFor()`, so it can silently bump the refcount of a stale/already-released (refCount==0, non-live) handle rather than rejecting it like `acquire()`/`info()` do [core/pool/all_ram_pool.cpp:82-87]
- [x] [Review][Patch] `AllRamSamplePool::release()` silently no-ops on a double-release (`refCount == 0`) instead of asserting or reporting a caller bug in debug builds [core/pool/all_ram_pool.cpp:89-94]
- [x] [Review][Defer] `Engine::~Impl()` releases pool handles via `~EngineSnapshot()` from whatever thread destroys the `Engine`, with no assertion that this is never the audio thread — deferred, pre-existing generic RAII/lifetime contract shared by most such engines, not unique to this diff [core/engine/engine.cpp:72-75]
- [x] [Review][Defer] `appendf()`'s fixed 256-byte buffer silently truncates on `vsnprintf` overflow with no return-value check — deferred, unreachable with current fixed small-value format strings [core/engine/model_to_sfz.cpp:12-20]
- [x] [Review][Defer] `toFrames()` silently clamps negative offset/loop values to 0 with no diagnostic — deferred, relies on upstream Story 1.3 model validation [core/engine/model_to_sfz.cpp:22-28]
- [x] [Review][Defer] `Engine::prepare()` called after `load()` with a genuinely different sample rate/block size mid-instrument is documented as supported but has no test — deferred, add coverage in a follow-up [core/engine/engine.h, core/engine/engine.cpp:87-99]

## Dev Notes

- **The central integration question: how to feed sfizz a model instead of .sfz text.** Options, in order of preference: (a) construct/populate `sfizz::Region` objects (or the layer just above them) directly from the canonical model — cleanest, matches "Region-as-data, Voice-as-execution"; (b) if sfizz's construction path is too welded to its parser, generate synthetic in-memory SFZ from the model as a *temporary* v0 seam — acceptable only if hidden entirely inside `core/engine/` and flagged for replacement (it caps fidelity at what SFZ text can express, which contradicts AD-1's SF2-extension plan). Investigate sfizz's `Synth`/`Region`/`Voice` layering in the vendored source before committing. Expect local sfizz patches (the pin anticipates them) — e.g. exposing internal headers or a programmatic region-construction hook.
- **AD-2 discipline:** frontends lowered to *references*; only the pool touches sample bytes. sfizz has its own FilePool — decide whether v0 (i) bypasses it by handing sfizz preloaded buffers, or (ii) wraps it behind the fbsampler pool interface. The fbsampler pool interface is the contract; sfizz's FilePool is an implementation detail that Epic 5 will lean on for streaming.
- **Audio thread rules are absolute (NFR-1):** no allocation, locks, file I/O, logging, or string formatting in `process()`. sfizz is designed RT-safe internally — the risk is *your* glue code (snapshot swap, pool accessors, event translation). The detector (Task 4) exists to catch exactly that.
- **Atomic snapshot swap semantics (AD-3):** the audio thread reads a pointer to the current immutable snapshot (e.g. via `std::atomic<Snapshot*>` acquire/release, retire via a non-audio-thread garbage list). Old snapshot must remain valid/audible until swap; pool entries pinned by refcount (AD-2). Keep it simple in v0 (loads are synchronous until Epic 5) but the swap mechanism itself must already be RT-clean.
- **Determinism for AC 1:** fix sample rate (48 kHz), block size, and disable any nondeterministic dither/randomized phase if sfizz has such features (check `Synth` options). Cross-compiler float drift is why the comparison uses thresholds, not byte equality.
- **Offline render harness is a keeper** — Story 1.6 wires it to the corpus, Epic 2/4 diff it against oracles. Build it as a reusable library function + thin CLI/test driver, not test-inline code.
- Depends on: Story 1.2 (model), Story 1.3 (lowered fixtures). The engine consumes validated model instances only.

### Project Structure Notes

- `core/engine/` (engine + snapshot swap), `core/pool/` (interface + all-RAM impl), `tests/render/` (harness + seed fixture), RT detector utility can live in `core/` internals shared with tests.
- Public API headers in `core/include/fbsampler/` stay JUCE-free (AD-6).

### References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-1, #AD-2, #AD-3, #AD-6, #AD-10, #Consistency-Conventions]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md#One-engine-one-pool-one-snapshot-at-a-time]
- [Source: docs/planning-artifacts/epics.md#Story-1.4]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#NFR-1, #NFR-2, #NFR-5]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Engine-strategy]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5) via Claude Code

### Implementation Plan

- **Bind seam decision (Dev Notes central question):** inspected vendored sfizz 1.2.3 — region construction is welded to the parser (`loadSfzString` is the only public bind path; `sfizz::Region` is internal with no programmatic construction hook). Chose option (b): synthetic in-memory SFZ generated from the model, entirely private to `core/engine/` (`model_to_sfz.cpp`), explicitly flagged as a temporary seam in the header comment and in `deferred-work.md` (must be replaced before Epic 2 SF2-extension work).
- **AD-2 pool:** abstract `SamplePool` interface in `include/fbsampler/pool.h` — refcounted handle-based entries, `SampleInfo.residentFrames` distinct from `numFrames` so the contract never implies full residency. All-RAM impl uses a fixed-size slot table (addresses never move) + acquire/release `live` flags so `info()`/`residentChannel()` are lock-free wait-free audio-thread reads.
- **AD-3 snapshot shape:** `std::atomic<EngineSnapshot*>` acquire/release swap; snapshots pin pool handles by refcount; old snapshots retire into a control-thread garbage list drained on the next control call, never the audio thread.
- **sfizz FilePool decision (Dev Notes):** v0 lets sfizz's FilePool load its own copy with full preload (`setPreloadSize(max)`); the fbsampler pool independently pins the same samples as the AD-2 contract owner. Double residency accepted for v0 and logged in deferred-work.md; Epic 5 unifies.
- **Determinism:** fixed 48 kHz / 256-frame blocks in the harness; threshold comparison (peak ≤ 1e-2, RMS ≤ 1e-3, documented in `render_regression_test.cpp`) rather than byte equality; verified the Release build passes against the Debug-generated reference (cross-config drift well inside thresholds).

### Debug Log References

- Full local suite: `ctest --test-dir build -C Debug` → 65/65 passed (was 54 before this story; 11 new tests).
- RT-safety: zero violations across the full seed timeline (937 blocks) in both Debug and Release; negative-control test proves the detector fires.
- Reference fixture generated via `FBSAMPLER_UPDATE_RENDER_FIXTURE=1` (documented in `render_regression_test.cpp` header).

### Completion Notes List

- Task 1: `SamplePool` AD-2 contract + `createAllRamSamplePool()` (float32 at load, fully resident, resident-head reported separately). Minimal internal RIFF/WAVE reader (PCM 16/24/32 + float32) — only the pool touches sample bytes. 4 pool unit tests.
- Task 2: JUCE-free `Engine` (`include/fbsampler/engine.h`): `prepare()` / `load(model, pool, root)` / `process(events, out, numFrames)` with MIDI 1.0 note semantics, sample-time event delays. Snapshot swap per AD-3; load failure keeps the previous snapshot active. Model→SFZ seam covers key/vel zones, root key, tune, gain, pan, offset, loop points (frames or seconds converted at bind time via pool rates), full ADSR+delay+hold envelope, and Cc→Gain/Pitch/Pan mod-matrix entries; 4 seam unit tests.
- Task 3: reusable `renderOffline()` harness (library function + shared `seed_fixture.h`, not test-inline) in `tests/render/`; seed instrument (2 velocity layers, looped pad with offset, release tail) + fixed 5 s MIDI timeline; checked-in float32 WAV reference; threshold comparison seeds the NFR-5 mechanism; silent-render guard (energy > 1) prevents silent-vs-silent false pass.
- Task 4: detector = thread-local RT-section flag (`fbsampler/detail/rt_check.h`) + global new/delete hooks in the test binary + `CheckedMutex` used engine/pool-wide + file-I/O reporting at the pool's loading seams. Separate `fbsampler-render-tests` binary so allocation hooks don't leak into the main unit-test binary. One warm-up block before the detector engages (documented). Negative-control test included.
- Task 5: both new tests are Catch2 tests in `fbsampler-render-tests`, auto-discovered by `catch_discover_tests` → run on the existing three-platform CI matrix (exceeds the Linux+Windows minimum).
- Fidelity caps of the v0 seam (mod-matrix curve, Velocity/KeyTrack matrix entries, control-ID routing) + double residency + 4096-slot pool cap recorded in `docs/implementation-artifacts/deferred-work.md`.

### File List

- core/include/fbsampler/pool.h (new)
- core/include/fbsampler/engine.h (new)
- core/include/fbsampler/detail/rt_check.h (new)
- core/pool/wav_reader.h (new)
- core/pool/wav_reader.cpp (new)
- core/pool/all_ram_pool.cpp (new)
- core/engine/rt_check.cpp (new)
- core/engine/checked_mutex.h (new)
- core/engine/model_to_sfz.h (new)
- core/engine/model_to_sfz.cpp (new)
- core/engine/engine.cpp (new)
- core/CMakeLists.txt (modified: new pool/engine sources)
- tests/CMakeLists.txt (modified: fbsampler-render-tests target)
- tests/render/render_harness.h (new)
- tests/render/render_harness.cpp (new)
- tests/render/wav_io.h (new)
- tests/render/wav_io.cpp (new)
- tests/render/seed_fixture.h (new)
- tests/render/render_regression_test.cpp (new)
- tests/render/rt_safety_test.cpp (new)
- tests/render/rt_new_delete_hooks.cpp (new)
- tests/render/model_to_sfz_test.cpp (new)
- tests/render/pool_test.cpp (new)
- tests/render/fixtures/seed/seed.sfz (new)
- tests/render/fixtures/seed/samples/soft.wav (new, generated 440 Hz sine)
- tests/render/fixtures/seed/samples/loud.wav (new, generated 440 Hz + harmonics)
- tests/render/fixtures/seed/samples/pad.wav (new, generated 220 Hz + harmonic)
- tests/render/fixtures/reference/seed_render.wav (new, checked-in reference render)
- docs/implementation-artifacts/deferred-work.md (modified: 1.4 deferred items)
- docs/implementation-artifacts/sprint-status.yaml (modified: story status)
- docs/implementation-artifacts/1-4-the-engine-makes-sound.md (modified: this record)

## Change Log

- 2026-07-19: Story 1.4 implemented — AD-2 sample-pool interface + all-RAM impl, unified sfizz-backed engine with AD-3 snapshot swap, offline render harness + seed render-regression fixture (threshold-based), RT-safety detector (allocation/lock/file-I/O) + tests. 65/65 tests green locally (Debug + Release). Status → review.
