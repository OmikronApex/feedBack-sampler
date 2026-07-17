---
baseline_commit: 957bbfd3d5047f6bf20e5000566da6603f40f7c3
---

# Story 1.2: Canonical model v0 with a written contract

Status: in-progress

## Story

As a developer,
I want the canonical region/control/mod-matrix model with its written spec and validation suite,
so that every frontend lowers into one verifiable representation (AD-11).

## Acceptance Criteria

1. **Given** the model spec document in-repo, **when** a model instance is constructed, **then** units follow the spec (cents/dB/seconds/normalized curves), every field has the documented default, and the validation suite rejects out-of-contract instances.
2. **Given** a valid model instance, **when** serialized to a golden-file snapshot, **then** the dump is deterministic, schema-versioned, and byte-stable across platforms.

## Tasks / Subtasks

- [x] Task 1: Write the model spec document (AC: 1)
  - [x] `core/model/SPEC.md` (in-repo, versioned): the SFZ-superset region/voice/mod-matrix model
  - [x] Fixed units: pitch in **cents**, gain in **dB**, times in **seconds**, curves normalized **0..1** (spine AD-11); audio float32; time-in-samples applies inside the engine, the *model* uses seconds
  - [x] Explicit documented default for every field
  - [x] Schema version constant (start at 1) stamped into every serialized artifact
  - [x] Spec covers: regions (key/vel ranges, sample refs, loops, offsets, tuning), envelopes (ADSR+), the modulation matrix (source → target → depth/curve; extensible with SF2 curve/source primitives later per AD-1), and the **control map** (stable control ID + display/accessible name per AD-8/AD-11)
- [x] Task 2: Implement the model types in `core/model/` (AC: 1)
  - [x] Plain-data C++17 types in namespace `fbsampler`, public headers in `core/include/fbsampler/`
  - [x] No JUCE types in the public API; std-only value types
  - [x] Defaults in code match SPEC.md exactly (single source: consider generating or cross-checking constants)
- [x] Task 3: Model validation suite (AC: 1)
  - [x] `validate(const InstrumentModel&) -> std::vector<Diagnostic>` — range checks, unit sanity, referential integrity (regions reference existing samples/controls), required-field presence
  - [x] `Diagnostic` type per spine convention: severity + stable code + human message + source location; **no exceptions across the core API**
  - [x] Catch2 tests: valid instance passes; each out-of-contract mutation class is rejected with the right diagnostic code
- [x] Task 4: Deterministic golden-file serialization (AC: 2)
  - [x] Text dump (JSON or similar) of a model instance: stable field order, fixed float formatting (e.g. shortest round-trip or fixed precision — pick one and pin it), schema version header
  - [x] Byte-stable across platforms: no locale-dependent formatting, no pointer/ordering nondeterminism, `\n` line endings enforced
  - [x] Catch2 test: serialize → reparse → reserialize is byte-identical; a checked-in reference snapshot matches on all CI platforms
- [ ] Task 5: CI proof (AC: 1, 2)
  - [ ] All tests run in the Story-1.1 CI matrix on all three platforms; the cross-platform byte-stability test is the tripwire — **wiring done, execution unverified** (see Debug Log)

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

### Change Log

- 2026-07-17: Story 1.2 implementation — SPEC.md v0, model types, validation suite (15 diagnostic codes), deterministic golden-file serialization, tests wired into the existing CI matrix. **Not locally verified** — this session's sandbox cannot execute a C++ toolchain (see Debug Log); needs a real `ctest` run before moving to review/done.

## References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-11, #AD-8, #AD-1, #Consistency-Conventions]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md#Why-a-compiler-pipeline]
- [Source: docs/planning-artifacts/epics.md#Story-1.2]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Engine-strategy]

## Dev Agent Record

### Agent Model Used

claude-sonnet-5 (Claude Code)

### Implementation Plan

- Studied `sfz::Region` in vendored sfizz (`build/_deps/sfizz-src/src/sfizz/Region.h`) per Dev Notes before finalizing v0 field lists; mirrored its region-as-data shape (key/vel ranges, sample ref, loop, offset, tuning, amp envelope, mod connections) at v0 scope, leaving SF2-only primitives as reserved extension points (not implemented).
- `Diagnostic`/`SourceLocation` in `core/include/fbsampler/diagnostic.h`, separate from `model.h`, since it's a general core-API error shape, not model-specific.
- `validate()` collects every violation rather than stopping at the first (spec requires per-mutation-class Catch2 coverage, which needs all codes reachable independently in one pass over a single mutated model).
- Serialization: hand-rolled deterministic `key=value` line format (not a JSON library dependency) with `std::to_chars`/`std::from_chars` for locale-independent, shortest-round-trip float formatting, per Dev Notes' explicit warning against `std::to_string`/iostream defaults.
- `parseModel()` wraps its body in try/catch and returns `bool` rather than throwing, consistent with "no exceptions across the core API" even though this is a golden-file test helper, not part of `InstrumentModel`'s own public surface.

### Debug Log References

- **Local build/test execution was not possible in this session's sandbox.** The C++ toolchain present (MinGW g++ 16.1.0) fails to invoke `cc1plus.exe` in this environment — both direct `g++` invocation and a CMake+Ninja configure of a trivial `int main(){}` program fail silently/with a generic "compiler is not able to compile a simple test program" error, with no underlying diagnostic. This reproduces even for code with zero dependency on this story's changes, so it's an environment limitation, not a defect introduced here.
- All code was written and manually traced for correctness (type/logic review, hand-verified control flow through `validate()` and `serialize`/`parseModel()`), but **no test in this story has been executed**. The checked-in golden reference (`tests/golden/model_v0_reference.txt`) was derived by hand from `serializeModel()`'s field order and `std::to_chars`' documented shortest-round-trip behavior for a set of numeric values chosen specifically because their shortest decimal representation is unambiguous (0.1, -0.0, -1, 0.01, 0.8, 0.5, 0.25, and small integers) — no value requiring judgment calls about `to_chars`' scientific-notation formatting (e.g. denormals) was placed in the byte-compared golden file; those are covered instead by a separate bit-exact round-trip test that doesn't depend on knowing the exact formatted string.
- **Action needed before this story can honestly be marked done:** run `ctest --test-dir build -C Release --output-on-failure` on a machine with a working toolchain (the Windows dev machine used for Story 1.1, or via CI) and confirm all new tests (`model_validate_test.cpp`, `golden/model_golden_test.cpp`) pass, in particular the golden-snapshot comparison, since that's the one test whose exact expected text I could not verify by execution.

### Completion Notes List

- `core/model/SPEC.md`: v0 written contract — units, defaults, schema version, region/envelope/mod-matrix/control-map field tables, validation code list, serialization format.
- Model types (`core/include/fbsampler/model.h`): `InstrumentModel`, `Region`, `EnvelopeADSR`, `ModSource`/`ModSourceKind`/`ModTarget`/`ModMatrixEntry`, `ControlMapEntry`; plain std-only data, namespace `fbsampler`.
- `core/include/fbsampler/diagnostic.h`: `Severity`, `SourceLocation`, `Diagnostic`.
- `validate()` (`core/model/validate.cpp` + `core/include/fbsampler/validate.h`): 15 diagnostic codes covering schema version, control-id presence/uniqueness, and per-region range/finiteness/loop/envelope/mod-matrix checks including referential integrity against the control map.
- `serializeModel()`/`parseModel()` (`core/model/serialize.cpp` + `core/include/fbsampler/serialize.h`): deterministic `key=value` text dump, fixed field order, `std::to_chars`/`from_chars` floats, `\n`-only line endings, string escaping for `\` and newline.
- Tests: `tests/model_validate_test.cpp` (valid instance + one test per diagnostic code + a multi-violation test), `tests/golden/model_golden_test.cpp` (determinism, round-trip byte-identity, checked-in golden snapshot compare, malformed-input rejection, bit-exact round-trip for tricky floats including denormals and negative zero).
- Wired into the existing Story-1.1 CI matrix with no CI file changes: new sources added to `sampler-core` in `core/CMakeLists.txt`; new test files added to the existing `fbsampler-tests` Catch2 binary in `tests/CMakeLists.txt` (auto-discovered via `catch_discover_tests`, already run by `ctest` in `.github/workflows/ci.yml`).

### File List

- core/model/SPEC.md
- core/include/fbsampler/model.h
- core/include/fbsampler/diagnostic.h
- core/include/fbsampler/validate.h
- core/model/validate.cpp
- core/include/fbsampler/serialize.h
- core/model/serialize.cpp
- core/CMakeLists.txt (modified: added model/validate.cpp, model/serialize.cpp to sampler-core sources)
- tests/model_validate_test.cpp
- tests/golden/model_golden_test.cpp
- tests/golden/model_v0_reference.txt
- tests/CMakeLists.txt (modified: added new test sources + FBSAMPLER_GOLDEN_DIR compile definition)
- docs/implementation-artifacts/1-2-canonical-model-v0-with-a-written-contract.md (story tracking)
- docs/implementation-artifacts/sprint-status.yaml (status tracking)
