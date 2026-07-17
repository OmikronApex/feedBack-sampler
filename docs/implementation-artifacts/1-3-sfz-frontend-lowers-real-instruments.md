# Story 1.3: SFZ frontend lowers real instruments

Status: ready-for-dev

## Story

As a DAW musician,
I want .sfz instruments parsed and lowered to the canonical model,
so that my SFZ libraries are represented correctly before a note is played.

## Acceptance Criteria

1. **Given** an SFZ v1 instrument (regions, key/vel ranges, loops, envelopes, CC labels), **when** the frontend lowers it, **then** the resulting model passes validation and its golden snapshot matches the reference.
2. **Given** a malformed or truncated .sfz file, **when** lowering is attempted, **then** structured Diagnostics (severity, code, message, location) are returned, no exception crosses the core API, and the seed fuzzer runs clean in CI.

## Tasks / Subtasks

- [ ] Task 1: SFZ frontend in `core/frontends/sfz/` (AC: 1)
  - [ ] Public API (JUCE-free): `lowerSfz(path/bytes) -> LowerResult { std::optional<InstrumentModel>, std::vector<Diagnostic> }` (exact shape to match the Diagnostic/API conventions from Story 1.2)
  - [ ] Reuse sfizz's parser layer (`sfizz::Parser` / its opcode handling) rather than writing an SFZ parser from scratch — the vendored sfizz is the foundation, this frontend *translates its parse output* into the canonical model
  - [ ] Lower SFZ v1 semantics: `<region>`/`<group>`/`<global>` hierarchy flattening, key/vel ranges (lokey/hikey/lovel/hivel, key), sample paths, loop mode/points, offset, tuning (transpose/tune/pitch_keycenter), amp envelope (ampeg_*), volume/pan, and `#include`/`#define`
  - [ ] CC labels (`label_ccN`) → control-map entries with stable control ID = CC number + label (AD-8) and accessible name (AD-11)
  - [ ] Sample references stay *references* (paths resolved relative to the .sfz), not loaded audio — loading is the pool's job (Story 1.4); frontends never touch sample data after lowering (AD-2)
- [ ] Task 2: Diagnostics for bad input (AC: 2)
  - [ ] Malformed/truncated files, unknown opcodes, missing sample files, bad values → Diagnostics with severity + stable code + message + file/line location
  - [ ] Unknown-but-harmless opcodes = warning (fidelity gap tracked, never silent — AD-1 spirit); structural failures = error with no model returned
  - [ ] `catch (...)` boundary at the frontend API: nothing throws across it (spine convention)
- [ ] Task 3: Golden lowering snapshots (AC: 1)
  - [ ] Small checked-in SFZ fixtures under `tests/golden/sfz/` exercising: multi-region key/vel layering, loops, envelope opcodes, CC labels, includes/defines, group inheritance
  - [ ] Golden test: lower fixture → validate (Story 1.2 suite) → serialize → compare byte-exact against checked-in reference snapshot; regeneration path documented (e.g. `--update-goldens` flag or cmake target)
- [ ] Task 4: Seed fuzzer (AC: 2)
  - [ ] `tests/fuzz/sfz/` harness feeding bytes to the frontend; assert: no crash, no throw, no leak/UB (run under sanitizers)
  - [ ] CI wiring: short deterministic fuzz pass on every build (e.g. libFuzzer where clang is available, or a corpus-replay + random-mutation fallback runner so Windows/MSVC CI still executes the harness); a seed corpus of valid + broken fixtures checked in
  - [ ] ASan/UBSan job for the fuzz/parser tests on at least one platform (Linux)
- [ ] Task 5: Validation + CI green (AC: 1, 2)
  - [ ] Every lowered fixture passes the Story-1.2 model validation suite
  - [ ] All of the above runs in the three-platform CI matrix

## Dev Notes

- **Do not write an SFZ parser.** sfizz 1.2.3 is vendored (Story 1.1) with ~96% SFZ v1 coverage; its parser is battle-tested. The frontend's job is *lowering* — mapping sfizz's parsed opcode stream/region data into the canonical model with spec units (cents/dB/seconds, Story 1.2). Investigate `sfizz::Parser` + `sfizz::Region` before deciding the exact integration seam; if sfizz's parser API is too entangled with its Synth, parse via `sfizz::Parser`'s client/listener interface (it exists for tooling) rather than instantiating a full Synth.
- **Unit conversion is where fidelity bugs will live.** SFZ expresses tuning in cents (fine) but volume in dB, times in seconds — mostly aligned with the model spec; double-check every opcode's SFZ default against the model default and convert explicitly. sfzformat.com is the authoritative opcode catalog (addendum).
- **The compiler-pipeline invariant (AD-1):** everything format-specific ends at lowering. No SFZ-specific flags may leak into the model (AD-11: "format-specific semantics must be expressed in model terms, never carried as frontend-private flags").
- **Scope: SFZ v1 for this story.** v2/ARIA opcodes encountered → warning diagnostic, not silent skip (tracked fidelity gap). Full v1 fidelity is the *launch* target; this story establishes the path with the fixture set, not exhaustive opcode coverage — coverage grows via corpus work (Story 1.6, Epic 7).
- **Parsers are the attack surface** (NFR-4, AD-10): they're isolated pure functions precisely so they can be fuzzed alone. Keep the frontend free of global state and file-system side effects beyond reading the given file + its includes.
- **Missing sample files:** lowering should still succeed structurally where possible, with per-sample error/warning diagnostics — FR-5's "3 samples missing" style reporting (Epic 3/4) builds on this. A missing sample must not abort the whole instrument.
- Depends on: Story 1.1 (repo/targets/CI), Story 1.2 (model, validation suite, Diagnostic, golden serialization).

### Project Structure Notes

- `core/frontends/sfz/` implementation; fixtures + goldens in `tests/golden/sfz/`; fuzz harness in `tests/fuzz/sfz/`.
- The golden harness built here (fixture → lower → validate → snapshot-diff) is the per-format pattern Stories 2.1 and 4.1 will clone — keep it format-parameterized where cheap.

### References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-1, #AD-2, #AD-8, #AD-10, #AD-11, #Consistency-Conventions]
- [Source: docs/planning-artifacts/epics.md#Story-1.3]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Engine-strategy, #Format-reference-sources]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-1, #FR-5, #NFR-4]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
