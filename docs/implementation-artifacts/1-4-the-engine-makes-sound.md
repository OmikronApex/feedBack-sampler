# Story 1.4: The engine makes sound

Status: ready-for-dev

## Story

As a feedBack player,
I want the unified voice engine rendering the canonical model through the sample-pool interface,
so that a lowered SFZ instrument is audible.

## Acceptance Criteria

1. **Given** a lowered instrument and the all-RAM pool implementation behind the AD-2 interface, **when** note-on/off events render offline, **then** velocity layers, loops, and envelopes sound per the model, and output matches a seed render-regression fixture.
2. **Given** a debug build with RT-safety detectors, **when** the render callback executes, **then** zero allocations, locks, or file I/O are detected on the audio thread.

## Tasks / Subtasks

- [ ] Task 1: Sample-pool interface + all-RAM implementation in `core/pool/` (AC: 1)
  - [ ] Define the AD-2 pool contract as an abstract interface now, so Epic 5 swaps internals only: entries are refcounted, referenced by handle from model regions; API supports preload/acquire/release semantics; **no caller may assume full-RAM residency** (interface shape must already distinguish "resident head" from "tail data available" even if v0 always has everything resident)
  - [ ] All-RAM implementation: loads referenced sample files fully at load time (background/loader thread context, never audio thread), float32 conversion at load
  - [ ] Audio-thread-facing accessors are lock-free, allocation-free, wait-free reads
- [ ] Task 2: Unified voice engine in `core/engine/` (AC: 1)
  - [ ] sfizz-based backend (AD-1): bind the canonical model's regions/envelopes/mod-matrix to sfizz's voice/rendering machinery — the model drives the engine, the SFZ text path is not re-entered (frontends lower, the engine executes model instances)
  - [ ] Engine API (JUCE-free): load an immutable model snapshot + pool; `process(events, float** out, numFrames)`; MIDI 1.0 semantics at the boundary; time in samples at engine rate
  - [ ] Note on/off with velocity → region selection per key/vel zones; loops honored; amp envelope per model; velocity layers audible
  - [ ] Snapshot-based structural state (AD-3 seed): engine holds one immutable model+pool snapshot; a setter swaps atomically (full command path arrives with async loading in Epic 5; the *shape* — immutable snapshot, atomic swap, retire off-thread — is fixed now)
- [ ] Task 3: Offline render harness + seed fixture (AC: 1)
  - [ ] `tests/render/` offline renderer: model + MIDI event list → wav/float buffer, deterministic (fixed sample rate 48 kHz, fixed block size)
  - [ ] Seed fixture: small checked-in SFZ instrument (from Story 1.3 fixtures) + fixed MIDI sequence exercising velocity layers, a loop, and envelope release → checked-in reference render
  - [ ] Comparison with tolerance (peak sample diff / RMS threshold — exact-byte float output across compilers is not realistic; document the threshold; this seeds the NFR-5 threshold mechanism Story 1.6 formalizes)
- [ ] Task 4: RT-safety detectors (AC: 2)
  - [ ] Debug-build detector around the process callback: override/instrument allocation (global new/delete hooks or malloc interposition per platform), assert no locks (guarded mutex wrapper used engine-wide), no file I/O on the audio thread
  - [ ] Catch2 test driving `process()` under the detector across the seed fixture; CI runs it (at minimum on Linux + Windows)
- [ ] Task 5: CI green (AC: 1, 2)
  - [ ] Render-regression seed test + RT-safety test wired into the three-platform matrix

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

### Debug Log References

### Completion Notes List

### File List
