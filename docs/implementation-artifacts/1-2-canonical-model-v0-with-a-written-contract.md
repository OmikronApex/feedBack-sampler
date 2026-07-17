# Story 1.2: Canonical model v0 with a written contract

Status: ready-for-dev

## Story

As a developer,
I want the canonical region/control/mod-matrix model with its written spec and validation suite,
so that every frontend lowers into one verifiable representation (AD-11).

## Acceptance Criteria

1. **Given** the model spec document in-repo, **when** a model instance is constructed, **then** units follow the spec (cents/dB/seconds/normalized curves), every field has the documented default, and the validation suite rejects out-of-contract instances.
2. **Given** a valid model instance, **when** serialized to a golden-file snapshot, **then** the dump is deterministic, schema-versioned, and byte-stable across platforms.

## Tasks / Subtasks

- [ ] Task 1: Write the model spec document (AC: 1)
  - [ ] `core/model/SPEC.md` (in-repo, versioned): the SFZ-superset region/voice/mod-matrix model
  - [ ] Fixed units: pitch in **cents**, gain in **dB**, times in **seconds**, curves normalized **0..1** (spine AD-11); audio float32; time-in-samples applies inside the engine, the *model* uses seconds
  - [ ] Explicit documented default for every field
  - [ ] Schema version constant (start at 1) stamped into every serialized artifact
  - [ ] Spec covers: regions (key/vel ranges, sample refs, loops, offsets, tuning), envelopes (ADSR+), the modulation matrix (source → target → depth/curve; extensible with SF2 curve/source primitives later per AD-1), and the **control map** (stable control ID + display/accessible name per AD-8/AD-11)
- [ ] Task 2: Implement the model types in `core/model/` (AC: 1)
  - [ ] Plain-data C++17 types in namespace `fbsampler`, public headers in `core/include/fbsampler/`
  - [ ] No JUCE types in the public API; std-only value types
  - [ ] Defaults in code match SPEC.md exactly (single source: consider generating or cross-checking constants)
- [ ] Task 3: Model validation suite (AC: 1)
  - [ ] `validate(const InstrumentModel&) -> std::vector<Diagnostic>` — range checks, unit sanity, referential integrity (regions reference existing samples/controls), required-field presence
  - [ ] `Diagnostic` type per spine convention: severity + stable code + human message + source location; **no exceptions across the core API**
  - [ ] Catch2 tests: valid instance passes; each out-of-contract mutation class is rejected with the right diagnostic code
- [ ] Task 4: Deterministic golden-file serialization (AC: 2)
  - [ ] Text dump (JSON or similar) of a model instance: stable field order, fixed float formatting (e.g. shortest round-trip or fixed precision — pick one and pin it), schema version header
  - [ ] Byte-stable across platforms: no locale-dependent formatting, no pointer/ordering nondeterminism, `\n` line endings enforced
  - [ ] Catch2 test: serialize → reparse → reserialize is byte-identical; a checked-in reference snapshot matches on all CI platforms
- [ ] Task 5: CI proof (AC: 1, 2)
  - [ ] All tests run in the Story-1.1 CI matrix on all three platforms; the cross-platform byte-stability test is the tripwire

## Dev Notes

- **AD-11 is the whole point:** the model is a *versioned written contract*, not a code convention. Two frontends must not be able to disagree about defaults or units while both "passing". The validation suite is the conformance gate every frontend (Stories 1.3, 2.1, 4.1) must pass.
- **Design the model as sfizz-superset** — sfizz's internals (Region-as-data, Voice-as-execution, generic mod matrix) are the validated shape (addendum). Don't invent an exotic IR; mirror sfizz region semantics so Story 1.4's engine binding is a translation, not an impedance fight. Study `sfizz::Region` in the vendored source before finalizing field lists.
- **v0 scope discipline:** model v0 needs what SFZ v1 lowering (Story 1.3) and the engine (1.4) require — regions, key/vel zones, loops, envelopes, tuning, CC-mapped controls, mod-matrix skeleton. SF2 primitives (curve types, source semantics) are *extension points* in the spec, not implemented now. Note them as reserved.
- **Control map now, not later:** every library-defined control gets a stable control ID minted at lowering (AD-8: format-defined identity — SFZ CC number/label, DS control id/name; never list position) plus display/accessible name (AD-11 — accessibility metadata survives lowering). Model v0 must carry both fields even though the proxy-parameter pool arrives in Epic 6.
- **Serialization is for golden files, not plugin state.** Plugin state chunks (Epic 6) are separate and minimal. Don't conflate.
- **Float determinism is the hard part of AC 2.** Avoid `std::to_string`/iostream default formatting (locale + precision traps). Use a fixed algorithm (e.g. printf `%.17g` with locale-independent code, or integer-quantized fields where the spec allows). Test with values like 0.1, denormals, and -0.0.
- **Diagnostic type lives here** (first story to need it) in `core/include/fbsampler/diagnostic.h` — it's the one error shape for the whole core API (spine Consistency Conventions). Severity enum, stable string/enum code, message, source location (file/line/column or byte offset). Frontends return `std::vector<Diagnostic>`.

### Project Structure Notes

- `core/model/` implementation, `core/include/fbsampler/` public headers, `tests/golden/` gets the reference snapshot fixture + tests (this seeds the golden harness that Story 1.3 extends per-format).
- Naming: files `snake_case`, types `PascalCase`, functions `camelCase`, namespace `fbsampler`.

### References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-11, #AD-8, #AD-1, #Consistency-Conventions]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md#Why-a-compiler-pipeline]
- [Source: docs/planning-artifacts/epics.md#Story-1.2]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Engine-strategy]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
