---
baseline_commit: 7de5dd61c5fefb22c7a13bb5b04e0175e89efd34
---

# Story 1.3: SFZ frontend lowers real instruments

Status: done

## Story

As a DAW musician,
I want .sfz instruments parsed and lowered to the canonical model,
so that my SFZ libraries are represented correctly before a note is played.

## Acceptance Criteria

1. **Given** an SFZ v1 instrument (regions, key/vel ranges, loops, envelopes, CC labels), **when** the frontend lowers it, **then** the resulting model passes validation and its golden snapshot matches the reference.
2. **Given** a malformed or truncated .sfz file, **when** lowering is attempted, **then** structured Diagnostics (severity, code, message, location) are returned, no exception crosses the core API, and the seed fuzzer runs clean in CI.

## Tasks / Subtasks

- [x] Task 1: SFZ frontend in `core/frontends/sfz/` (AC: 1)
  - [x] Public API (JUCE-free): `lowerSfz(path/bytes) -> LowerResult { std::optional<InstrumentModel>, std::vector<Diagnostic> }` (exact shape to match the Diagnostic/API conventions from Story 1.2)
  - [x] Reuse sfizz's parser layer (`sfizz::Parser` / its opcode handling) rather than writing an SFZ parser from scratch — the vendored sfizz is the foundation, this frontend *translates its parse output* into the canonical model
  - [x] Lower SFZ v1 semantics: `<region>`/`<group>`/`<global>` hierarchy flattening, key/vel ranges (lokey/hikey/lovel/hivel, key), sample paths, loop mode/points, offset, tuning (transpose/tune/pitch_keycenter), amp envelope (ampeg_*), volume/pan, and `#include`/`#define`
  - [x] CC labels (`label_ccN`) → control-map entries with stable control ID = CC number + label (AD-8) and accessible name (AD-11)
  - [x] Sample references stay *references* (paths resolved relative to the .sfz), not loaded audio — loading is the pool's job (Story 1.4); frontends never touch sample data after lowering (AD-2)
- [x] Task 2: Diagnostics for bad input (AC: 2)
  - [x] Malformed/truncated files, unknown opcodes, missing sample files, bad values → Diagnostics with severity + stable code + message + file/line location
  - [x] Unknown-but-harmless opcodes = warning (fidelity gap tracked, never silent — AD-1 spirit); structural failures = error with no model returned
  - [x] `catch (...)` boundary at the frontend API: nothing throws across it (spine convention)
- [x] Task 3: Golden lowering snapshots (AC: 1)
  - [x] Small checked-in SFZ fixtures under `tests/golden/sfz/` exercising: multi-region key/vel layering, loops, envelope opcodes, CC labels, includes/defines, group inheritance
  - [x] Golden test: lower fixture → validate (Story 1.2 suite) → serialize → compare byte-exact against checked-in reference snapshot; regeneration path documented (e.g. `--update-goldens` flag or cmake target)
- [x] Task 4: Seed fuzzer (AC: 2)
  - [x] `tests/fuzz/sfz/` harness feeding bytes to the frontend; assert: no crash, no throw, no leak/UB (run under sanitizers)
  - [x] CI wiring: short deterministic fuzz pass on every build (e.g. libFuzzer where clang is available, or a corpus-replay + random-mutation fallback runner so Windows/MSVC CI still executes the harness); a seed corpus of valid + broken fixtures checked in
  - [x] ASan/UBSan job for the fuzz/parser tests on at least one platform (Linux)
- [x] Task 5: Validation + CI green (AC: 1, 2)
  - [x] Every lowered fixture passes the Story-1.2 model validation suite
  - [x] All of the above runs in the three-platform CI matrix (wired into ctest, which every matrix leg runs; Windows leg verified locally — mac/linux legs + the new sanitizer job verify on next push)

### Review Findings

- [x] [Review][Decision] 44.1 kHz frame→seconds assumption — RESOLVED (2026-07-19): change the model. Store offset/loop points as sample frames with an explicit unit tag; engine converts when the real rate is known (Story 1.4). Converted to patch below.
- [x] [Review][Decision] Story-1.2 scope-mixing + statuses ahead of CI evidence — RESOLVED (2026-07-19): approved as one batch; CI evidence arrives on next push.
- [x] [Review][Decision] Repair-vs-reject policy — RESOLVED (2026-07-19): keep repair policy, document it in SPEC.md as the frontend's normative contract. Converted to patch below.
- [x] [Review][Patch] Model change (from D1): replace seconds-based `offsetSeconds`/`loopStartSeconds`/`loopEndSeconds` with frame-count + unit-tagged representation in model/SPEC/serialize/validate; frontend stores raw frames; regenerate golden snapshots; drop the 44.1 kHz assumption and its warning [core/model/SPEC.md, core/frontends/sfz/sfz_frontend.cpp]
- [x] [Review][Patch] Document repair policy (from D3): add the clamp/swap/disable robustness contract (incl. float `transpose` acceptance) to SPEC.md as normative frontend behavior [core/model/SPEC.md]
- [x] [Review][Patch] `fs` alias never defined/included — compiles only via sfizz header leakage of ghc::filesystem [core/frontends/sfz/sfz_frontend.cpp:449]
- [x] [Review][Patch] Invalid `key=` value collapses inherited lokey/hikey range to rootKey instead of leaving prior values intact [core/frontends/sfz/sfz_frontend.cpp:352-356]
- [x] [Review][Patch] `default_path` is prefixed onto absolute `sample=` paths, mangling them and triggering false `sfz.sample_file_missing` [core/frontends/sfz/sfz_frontend.cpp:347]
- [x] [Review][Patch] Huge finite `transpose`/`tune`/`volume` values overflow `tuningCents`/`gainDb` to inf → `validate()` errors suppress the whole instrument, defeating the clamp-and-warn contract [core/frontends/sfz/sfz_frontend.cpp:419]
- [x] [Review][Patch] `framesToSeconds` falls back to 0 on unparseable values, discarding group-inherited offset/loop values — inconsistent with every other opcode's fallback [core/frontends/sfz/sfz_frontend.cpp:322]
- [x] [Review][Patch] Opcodes inside unsupported headers (`<curve>`, `<effect>`) are silently dropped in `flushCurrentHeader` with no per-opcode diagnostics (AD-1) [core/frontends/sfz/sfz_frontend.cpp:214-229]
- [x] [Review][Patch] `serialize.cpp` `parseFloat` accepts `nan`/`inf` tokens and `validate()` only checks loop finiteness when `loopEnabled` — NaN loop bounds round-trip clean [core/model/serialize.cpp, core/model/validate.cpp:78]
- [x] [Review][Patch] Fuzz entry constructs `std::string` from possibly-null pointer (UB when libFuzzer passes nullptr,0) [tests/fuzz/sfz/sfz_fuzzer.cpp:17]
- [x] [Review][Patch] Replay runner hardening: missing corpus dir throws uncaught `filesystem_error`; directory-iteration order makes "deterministic" mutations platform-dependent; `sfz_fuzz_replay` ctest has no TIMEOUT [tests/fuzz/sfz/replay_main.cpp:81, tests/CMakeLists.txt]
- [x] [Review][Patch] Golden regeneration never checks stream state after write — truncated snapshot reported as success [tests/golden/sfz_golden_test.cpp:58-63]
- [x] [Review][Patch] Pathological-input test: `"\0x"` is an empty string (NUL never exercised); embedded-NUL input untested [tests/sfz_frontend_test.cpp:195-205]
- [x] [Review][Patch] Diagnostics polish: `rangeValue` location discarded so bad-value diags point at the opcode name; `-1/-1` internal-error sentinel violates the stated 1-based convention; `clampWarn` uses locale-dependent `std::to_string` [core/frontends/sfz/sfz_frontend.cpp:190,301-311,538]
- [x] [Review][Patch] Sanitizer CI job's `-E ad6_guard_negative` exclusion is undocumented in the workflow [.github/workflows/ci.yml]
- [x] [Review][Patch] `missing_sample.sfz` fixture has no byte-exact golden snapshot, unlike the other three fixtures [tests/golden/sfz_golden_test.cpp]
- [x] [Review][Patch] Text-mode no-filesystem guarantee rests on unverified sfizz `setMaximumIncludeDepth(1)` semantics — add a regression test that `#include` of an *existing* file in text mode still fails without disk access [core/frontends/sfz/sfz_frontend.cpp:496-501]
- [x] [Review][Defer] Sanitizer CI job passes flags only via `CMAKE_*_FLAGS` with no linker flags or canary proving instrumentation is active [.github/workflows/ci.yml:64-74] — deferred, works in practice; revisit when CI matrix evolves
- [x] [Review][Defer] Model-validation diags re-emitted through `lowerImpl` carry no source locations [core/frontends/sfz/sfz_frontend.cpp:517-522] — deferred, defensive path designed to be unreachable

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

claude-fable-5 (Claude Code)

### Debug Log References

- Local Windows/MSVC: full build clean, `ctest -C Release` 49/49 passed (incl. new `[sfz]` unit tests, 4 SFZ golden tests, `sfz_fuzz_replay`).
- Golden snapshots generated via `FBSAMPLER_UPDATE_GOLDENS=1`, then hand-verified (inheritance overrides, unit conversions, control-map entries) and re-run in compare mode.

### Completion Notes List

- **Integration seam:** sfizz 1.2.3 exposes a standalone `sfizz::parser` CMake target (`sfz::Parser` + `sfz::ParserListener`) with no Synth entanglement. `sampler-core` links it PRIVATE; the public header (`fbsampler/sfz_frontend.h`) speaks only fbsampler types + std. The listener implementation uses the *low-level* callbacks (`onParseHeader`/`onParseOpcode`/`onParseError`/`onParseWarning`) because only those carry source ranges for Diagnostics; block assembly and `<global>/<master>/<group>/<region>` flattening happen in the frontend.
- **API shape:** `lowerSfzFile(path)` and `lowerSfzText(text, virtualPath)` both return `LowerResult { std::optional<InstrumentModel>, std::vector<Diagnostic> }`. Two named functions instead of one overload because `path` and `bytes` are both strings. Text mode performs zero file-system access: parser include depth is set so any `#include` fails as a parse error (fuzzing entry point, AD-10); file mode resolves includes relative to the .sfz and existence-checks sample references (missing sample = per-region warning, region kept — FR-5 groundwork).
- **Unit conversions:** pan -100..100 → -1..1; ampeg_sustain % → 0..1; transpose semitones ×100 + tune cents → tuningCents; volume dB pass-through. **Known fidelity gap (deliberate):** SFZ `offset`/`loop_start`/`loop_end` are sample frames, the model stores seconds, and frontends may not open sample files (AD-2) — frames are converted at an assumed 44100 Hz with a once-per-file `sfz.frame_units_assumed_rate` warning (AD-1: tracked, never silent). Revisit in Story 1.4 when the engine knows real rates.
- **Robustness policy:** out-of-range values are clamped (warning `sfz.value_clamped`), swapped ranges repaired (`sfz.key_range_swapped`/`sfz.velocity_range_swapped`), impossible loops disabled (`sfz.loop_range_invalid`), sample-less regions dropped (`sfz.region_missing_sample`) — so every structurally-parseable file lowers to a model that passes the Story-1.2 validation suite (asserted by running `validate()` inside the frontend; validation errors would suppress the model). Unsupported opcodes/headers → `sfz.opcode_unsupported`/`sfz.header_unsupported` warnings. `catch (...)` boundary returns `sfz.internal_error`.
- **Diagnostics locations** are 1-based (sfizz counts from 0; converted at the boundary).
- **Golden regeneration path:** `FBSAMPLER_UPDATE_GOLDENS=1` env var + run the `[sfz][golden]` tests (documented in `tests/golden/sfz_golden_test.cpp` header). Harness is format-parameterized in shape (fixture list + one lower() call) for Stories 2.1/4.1 to clone.
- **Fuzzing:** shared `LLVMFuzzerTestOneInput` entry point; default build is a deterministic corpus-replay + xorshift-mutation runner registered as ctest test `sfz_fuzz_replay` (runs on all three platforms incl. MSVC); `-DFBSAMPLER_LIBFUZZER=ON` (clang) builds the same entry against libFuzzer for coverage-guided runs. 8-seed corpus (valid + broken + binary garbage) checked in. New CI job `linux-asan-ubsan` builds tests + fuzz runner with ASan/UBSan (`-fno-sanitize-recover=all`) and runs ctest.
- Three-platform CI matrix picks up all new tests automatically via ctest; Windows verified locally, mac/linux + sanitizer job verify on next push.

### File List

- `core/include/fbsampler/sfz_frontend.h` (new)
- `core/frontends/sfz/sfz_frontend.cpp` (new)
- `core/CMakeLists.txt` (modified: frontend source + sfizz::parser link)
- `tests/sfz_frontend_test.cpp` (new)
- `tests/golden/sfz_golden_test.cpp` (new)
- `tests/golden/sfz/layers.sfz`, `loops_env.sfz`, `controls.sfz`, `defines.inc`, `missing_sample.sfz` (new fixtures)
- `tests/golden/sfz/layers.expected.txt`, `loops_env.expected.txt`, `controls.expected.txt` (new golden snapshots)
- `tests/golden/sfz/samples/soft.wav`, `hard.wav`, `upper.wav`, `pad.wav`, `hit.wav` (new zero-byte placeholders so fixtures resolve)
- `tests/fuzz/sfz/sfz_fuzzer.cpp`, `tests/fuzz/sfz/replay_main.cpp` (new)
- `tests/fuzz/sfz/corpus/` — 8 seed files (new)
- `tests/CMakeLists.txt` (modified: test sources + fuzz targets/option)
- `.gitattributes` (modified: LF for sfz fixtures/goldens, corpus binary-safe)
- `.github/workflows/ci.yml` (modified: `linux-asan-ubsan` job)
- `docs/implementation-artifacts/1-3-sfz-frontend-lowers-real-instruments.md`, `docs/implementation-artifacts/sprint-status.yaml` (story tracking)

## Change Log

- 2026-07-19: Code review (3 layers, 21 triaged findings): all 17 patches applied. **Schema v2**: sample positions (offset/loop points) now stored as frames with a `SamplePositionUnit` tag instead of assumed-44.1kHz seconds (`sfz.frame_units_assumed_rate` warning removed; engine converts at bind time, Story 1.4); repair policy documented as normative in SPEC.md; frontend hardening (explicit ghc/fs_std.hpp include, absolute-path samples, inherited-value fallbacks, tuning clamps, per-opcode diags in unsupported headers, value-token diagnostic locations, locale-free messages); strict parser rejects nan/inf; loop bounds validated even when disabled; fuzz harness hardening (null-input guard, sorted corpus, error_code dir handling, ctest TIMEOUT); goldens regenerated (incl. new missing_sample snapshot); include-depth regression test added. 54/54 local tests green. Status → done (per D2: CI evidence on next push).
- 2026-07-18: Story 1.3 implemented — SFZ frontend (sfizz-parser-based lowering to canonical model), structured diagnostics with locations, golden lowering snapshots (4 fixtures), seed fuzzer (replay + optional libFuzzer) and Linux ASan/UBSan CI job. All local tests green (49/49).
