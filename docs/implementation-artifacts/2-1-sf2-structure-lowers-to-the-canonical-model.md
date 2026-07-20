---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 2.1: SF2 structure lowers to the canonical model

Status: review

## Story

As a DAW musician,
I want SF2 soundfonts parsed and their zones lowered to the canonical model,
so that my .sf2 collection is structurally correct in the sampler.

## Acceptance Criteria

1. **Given** a well-formed SF2 file (RIFF hydra, presets, instruments, samples), **when** the frontend lowers a preset, **then** key/velocity zones, sample loops, tuning, and envelope generators map to model regions passing validation, with golden snapshots matching references.
2. **Given** a truncated or structurally invalid SF2 (per the SoundFont 2.04 mandated validation), **when** lowering is attempted, **then** structured Diagnostics are returned, nothing crashes, and the SF2 fuzzer runs clean in CI.

## Tasks / Subtasks

- [x] Task 1: SF2 RIFF parser (AC: 1, 2)
  - [x] `core/frontends/sf2/` — RIFF chunk walker for the three top-level LIST chunks (INFO, sdta, pdta) and the nine hydra sub-chunks (phdr/pbag/pmod/pgen/inst/ibag/imod/igen/shdr)
  - [x] Implement SoundFont 2.04 §10 mandated structural validation: chunk sizes multiple of record size, terminal records present, bag/gen/mod index monotonicity, sample header bounds within sdta — every violation is a `Diagnostic` with a stable `sf2.*` code, never a throw across the core API
  - [x] Parse from an in-memory byte span as well as a file path (fuzzing entry point, mirrors `lowerSfzText`/`lowerSfzFile` split from Story 1.3)
- [x] Task 2: Embedded-sample references through the pool (AC: 1)
  - [x] SF2 samples live inside the .sf2 (sdta 16-bit PCM, optional sm24). Extend the sample-reference scheme so a model region can reference a sample *inside* a container file — do NOT extract to temp files. Recommended: a structured sample-reference string/URI the pool understands (e.g. `sf2://<path>#<sampleIndex>` or path + embedded-range fields on `Region`), plus a pool-side reader that decodes the referenced range to float32 at acquire() time (AD-2: only the pool touches sample bytes; the frontend records references and shdr metadata only)
  - [x] Model schema bump (v3 → v4) for whatever `Region` gains; update `SPEC.md`, `serialize.cpp`, `validate.cpp`, and regenerate existing goldens (schema version is stamped into golden files — expect all Epic-1 goldens to change in one commit with the bump)
  - [x] Stereo sample pairs: shdr `sampleLink`/`sampleType` — lower linked left/right mono samples into a correct stereo voice (v0 acceptable: two panned regions; document the choice)
- [x] Task 3: Generator lowering (AC: 1)
  - [x] Zone algebra per spec §8.5/§9.4: instrument-zone generators are absolute, preset-zone generators are *additive offsets*; global zones supply defaults; keyRange/velRange intersection between preset and instrument zones
  - [x] Structural generators → model: keyRange/velRange → lo/hiKey, lo/hiVelocity; overridingRootKey/originalPitch → rootKey; coarseTune/fineTune/scaleTuning → tuningCents; initialAttenuation → gainDb (spec unit is centibels of attenuation; note the well-known real-world 0.4× emulation factor question — follow FluidSynth's interpretation since it is the NFR-5 oracle, and record the choice in a comment + dev note); pan (0.1% units) → pan −1..1
  - [x] sampleModes → loopEnabled (mode 1 continuous; mode 3 loop-until-release may degrade to continuous loop with a `sf2.loop_mode_approximated` diagnostic if the model cannot express it); startloopAddrs/endloopAddrs + startAddrsOffset (+ coarse variants) → offset/loopStart/loopEnd in Frames (`SamplePositionUnit::Frames` — engine converts at bind, Story 1.4 pattern)
  - [x] Volume envelope: delay/attack/hold/decay/release VolEnv (timecents → seconds: `2^(tc/1200)`; clamp the −12000 sentinel to 0), sustainVolEnv (centibels of attenuation → normalized sustain level) → `EnvelopeADSR`; keynumToVolEnvHold/Decay unsupported in v0 → diagnostic
  - [x] Generators the model cannot yet express (filter Fc/Q, mod/vib LFO, mod envelope, chorus/reverb sends, exclusiveClass) → per-generator `sf2.generator_unsupported` warning — tracked, never silent (AD-1); region still lowers
- [x] Task 4: Preset enumeration surface (AC: 1)
  - [x] `lowerSf2Preset(path, bank, program)` lowers ONE preset to one `InstrumentModel`; a cheap `listSf2Presets(path)` returns bank/program/name entries without lowering — this is the API Story 2.4 builds on. `InstrumentModel::name` = preset name from phdr
  - [x] Public header `core/include/fbsampler/sf2_frontend.h`: fbsampler types + std only (AD-6), `LowerResult`-shaped return like Story 1.3
- [x] Task 5: Golden snapshots (AC: 1)
  - [x] Clone the Story-1.3 golden harness shape (`tests/golden/sfz_golden_test.cpp` is explicitly format-parameterized for this): `tests/golden/sf2/` fixtures + `.expected.txt` snapshots, `FBSAMPLER_UPDATE_GOLDENS=1` regeneration path
  - [x] Fixtures: hand-build tiny .sf2 files with a checked-in generator script (like `corpus/tools/make_midi.py` precedent) — velocity layers, key splits, looped sample, preset-offset-over-instrument generators, global zones, stereo pair. Keep them a few KB
- [x] Task 6: SF2 fuzzer in CI (AC: 2)
  - [x] Clone `tests/fuzz/sfz/` pattern: shared `LLVMFuzzerTestOneInput` against the byte-span entry, deterministic corpus-replay ctest target `sf2_fuzz_replay` (all platforms), libFuzzer under `-DFBSAMPLER_LIBFUZZER=ON`, seeds = valid mini-sf2 + truncations + garbage; runs in the existing `linux-asan-ubsan` job

## Dev Notes

- **Scope fence:** this story is STRUCTURE only — zones, loops, tuning, volume envelope. Modulators (pmod/imod, default modulator set, curve types) are Story 2.2; Vorbis/SF3 is 2.3; preset *switching* UX is 2.4. Emit `sf2.modulator_unsupported`-class diagnostics for pmod/imod content encountered now, don't lower it.
- **⚠ Engine bind seam (deferred-work.md, flagged blocking for Epic 2):** the engine currently binds models by generating SFZ text (`core/engine/model_to_sfz.cpp`) — it cannot express mod-matrix curves or embedded-sample references. For THIS story the seam question bites at rendering time: `model_to_sfz` emits `sample=` paths, which cannot address sf2-embedded samples. Two options: (a) do the seam replacement here (direct sfizz region construction via local patch), or (b) minimal bridge — since the pool decodes embedded samples to float32 anyway, note that sfizz's own parser also loads .sf2 sample data; the clean fix is (a). Whichever is chosen, Story 2.2 hard-requires the seam replaced (curves are inexpressible in SFZ text) — doing it here front-loads the risk where it belongs. Coordinate: this story's AC is satisfiable at the lowering/golden level without engine changes, but 2.5's corpus rendering needs bind to work, so don't leave the seam question to 2.5.
- **AD-2 discipline:** the frontend parses the hydra and shdr records (metadata) but must NOT decode sdta sample bytes itself — the pool reader does that at acquire(). Parsing the file once in the frontend (structure) and once in the pool (referenced sample ranges) is acceptable and keeps the ownership rule clean.
- **Read before modifying:** `core/include/fbsampler/model.h` (v3, `SamplePositionUnit` tag pattern), `core/model/SPEC.md` + `serialize.cpp` + `validate.cpp` (schema-version stamping, normative repair policy), `core/frontends/sfz/sfz_frontend.cpp` (diagnostic codes/clamp-and-warn policy to mirror), `core/pool/all_ram_pool.cpp` + `wav_reader.cpp` (where the embedded-sf2 reader plugs in; note the fixed 4096-slot table), `core/include/fbsampler/pool.h` (acquire contract).
- **Robustness policy mirrors 1.3 (normative in SPEC.md):** clamp-and-warn out-of-range values, repair swapped ranges, disable impossible loops, drop sample-less zones — every structurally-parseable file must lower to a model that passes `validate()` (run inside the frontend; validation errors suppress the model).
- **Diagnostics:** 1-based locations where meaningful (chunk offsets are fine as location text), stable `sf2.*` codes, locale-free messages, `catch (...)` boundary → `sf2.internal_error`.
- **SF2 spec:** SoundFont 2.04, synthfont.com/sfspec24.pdf (PRD addendum "Format reference sources"). Generator semantics: §8.1.2/8.1.3; zone rules §8.5/§9.4; mandated validation §10. Units cheat sheet: timecents `2^(tc/1200)` s, centibels /10 dB (attenuation positive), cents pitch, pan permille/10.
- **Testing:** Catch2 3.x via ctest, three-platform CI auto-pickup; sanitizer job covers the fuzzer. 84 tests green at baseline — do not regress.

### Project Structure Notes

- New: `core/frontends/sf2/` (parser + lowering, snake_case files), `core/include/fbsampler/sf2_frontend.h`, `tests/golden/sf2/`, `tests/fuzz/sf2/`, fixture generator under `tests/golden/sf2/tools/` or `corpus/tools/`.
- Modified: `core/CMakeLists.txt`, `tests/CMakeLists.txt`, `core/model/SPEC.md`, `core/model/serialize.cpp`, `core/model/validate.cpp`, `core/include/fbsampler/model.h`, `core/pool/` (embedded-sample reader), existing goldens (schema bump), `.github/workflows/ci.yml` only if a new job is needed (prefer none).

### References

- [Source: docs/planning-artifacts/epics.md#Story-2.1]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-1, #AD-2, #AD-6, #AD-10, #AD-11, #Consistency-Conventions]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-2, #FR-5, #NFR-4]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Format-reference-sources]
- [Source: docs/implementation-artifacts/1-3-sfz-frontend-lowers-real-instruments.md#Completion-Notes (frontend/golden/fuzz patterns to clone)]
- [Source: docs/implementation-artifacts/deferred-work.md#Deferred-from-1-4 (model_to_sfz seam must be replaced before SF2-extension work)]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5)

### Debug Log References

- Full ctest suite: 102/102 pass locally (baseline was 84; +18 new SF2 tests incl. `sf2_fuzz_replay`).
- `sf2-fuzz-replay`: 7 seeds × 64 mutants each, no crash.

### Completion Notes List

- **Sample-reference scheme (design decision):** chose the URI form `sf2://<container-path>#<sampleIndex>` carried in `Region::sampleFile` — no new Region fields. Schema bumped v3 → v4 to document the URI semantics (SPEC.md#Sample-references); all Epic-1 goldens changed only in their `schema_version` line, verified byte-exact by the golden suites.
- **Pool-side reader:** `core/pool/sf2_sample_reader.cpp` decodes the referenced 16-bit sdta range to float32 mono at `acquire()` time; `all_ram_pool.cpp` routes `sf2://` URIs to it. sm24 (24-bit refinement) ignored in v0 (deferred-work.md).
- **initialAttenuation:** follows FluidSynth's 0.4× emulation-factor interpretation (`gainDb = −0.4 · cB / 10`), since FluidSynth is the NFR-5 oracle. Recorded in a comment at the conversion site.
- **Stereo pairs (v0 choice, documented):** linked left/right mono samples lower as two hard-panned regions (sampleType 4 → pan −1, type 2 → +1, generator pan summed then clamped). True stereo voice deferred (deferred-work.md).
- **loop-until-release (sampleModes=3):** approximated as continuous loop with `sf2.loop_mode_approximated` warning (model cannot express release-phase loop exit).
- **Structural validation (§10):** chunk-size multiples, terminal records, bag/gen/mod monotonicity and index bounds are `Error`s (`sf2.chunk_size_invalid`, `sf2.terminal_record_missing`, `sf2.index_not_monotonic`, `sf2.index_out_of_range`, `sf2.chunk_truncated`, `sf2.hydra_chunk_missing`, `sf2.chunk_missing`, `sf2.not_soundfont`); zone-level issues repair-and-warn per the normative robustness policy (`sf2.value_clamped`, `sf2.sample_bounds_invalid`, `sf2.zone_ignored`, `sf2.zone_range_empty`, `sf2.key_range_swapped`, `sf2.velocity_range_swapped`, `sf2.loop_range_invalid`, `sf2.rom_sample_unsupported`, `sf2.generator_unsupported`, `sf2.preset_generator_ignored`, `sf2.modulator_unsupported`). `catch (...)` boundary → `sf2.internal_error`.
- **⚠ Engine bind seam decision:** NOT replaced in this story. SF2 models lower/validate/golden correctly and the pool decodes embedded samples, but `model_to_sfz` cannot emit `sf2://` references, so SF2 presets do not render through the engine yet. The seam replacement is the first task of Story 2.2 (which hard-requires it for curves) — before 2.5's corpus rendering. Recorded in deferred-work.md.
- **Fixtures:** deterministic generator `tests/golden/sf2/tools/make_sf2.py` (integer triangle waves, no float math) builds 4 golden fixtures (basic / layers / stereo / unsupported, ~1–1.5 KB each) and the fuzz seed corpus (3 valid minis + truncations + garbage).
- **Fuzzer:** shared `LLVMFuzzerTestOneInput` in `tests/fuzz/sf2/sf2_fuzzer.cpp` exercises both `listSf2PresetsFromBytes` and `lowerSf2Bytes` for every enumerated preset; reuses the sfz `replay_main.cpp` deterministic runner; `sf2-fuzz-replay` added to the linux-asan-ubsan build target list (no new CI job).

### File List

- core/include/fbsampler/sf2_frontend.h (new)
- core/frontends/sf2/sf2_frontend.cpp (new)
- core/pool/sf2_sample_reader.h (new)
- core/pool/sf2_sample_reader.cpp (new)
- core/pool/all_ram_pool.cpp (modified: sf2:// URI routing in acquire)
- core/include/fbsampler/model.h (modified: schema v4, sampleFile doc)
- core/model/SPEC.md (modified: v4, sample-reference URI section, SF2 robustness policy)
- core/CMakeLists.txt (modified: new sources)
- tests/CMakeLists.txt (modified: sf2 test/fuzz targets)
- tests/sf2_frontend_test.cpp (new)
- tests/golden/sf2_golden_test.cpp (new)
- tests/golden/sf2/basic.sf2, layers.sf2, stereo.sf2, unsupported.sf2 (new, generated)
- tests/golden/sf2/basic.expected.txt, layers.expected.txt, stereo.expected.txt, unsupported.expected.txt (new, generated snapshots)
- tests/golden/sf2/tools/make_sf2.py (new)
- tests/fuzz/sf2/sf2_fuzzer.cpp (new)
- tests/fuzz/sf2/corpus/* (new, generated seeds)
- tests/golden/model_golden_test.cpp (modified: v4 prefix assertion)
- tests/golden/model_v0_reference.txt (modified: schema line)
- tests/golden/sfz/*.expected.txt (modified: schema line, 4 files)
- corpus/golden/*.expected.txt (modified: schema line, 3 files)
- corpus/cache/*.result.json.golden.txt (modified: schema line, 3 files)
- .gitattributes (modified: sf2 binary attrs)
- .github/workflows/ci.yml (modified: sf2-fuzz-replay in sanitizer build targets)
- docs/implementation-artifacts/deferred-work.md (modified: 2.1 deferrals)

## Change Log

- 2026-07-19: Story 2.1 implemented — SF2 RIFF/hydra parser with §10 structural validation, zone algebra + generator lowering, `sf2://` embedded-sample references through the pool (schema v4), preset enumeration API, golden fixtures/snapshots, deterministic fuzzer wired into CI. 102/102 tests green.
