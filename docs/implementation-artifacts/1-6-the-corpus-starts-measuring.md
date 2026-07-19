---
baseline_commit: 30b783bc2f053cd81b10ff9711ae3f6915d1a415
---

# Story 1.6: The corpus starts measuring

Status: done

## Story

As the maintainer,
I want the corpus manifest and offline render harness wired into CI,
so that fidelity is measured from the first release, not retrofitted.

## Acceptance Criteria

1. **Given** the corpus manifest listing initial SFZ entries (sfz community instruments), **when** the CI harness renders fixed MIDI through each corpus entry, **then** output diffs against checked-in reference captures (recorded once from the external oracle — Sforzando for SFZ — with the capture procedure documented in-repo) within NFR-5 thresholds and reports pass/fail per library.
2. **Given** a change that alters rendering of a corpus entry, **when** CI runs, **then** the regression is flagged with a per-library diff report.

## Tasks / Subtasks

- [x] Task 1: Corpus manifest (AC: 1)
  - [x] `corpus/manifest.{json|yaml}` — schema-versioned (spine convention): per entry: id, format, source URL, license, exact version/checksum, fixed MIDI test sequence ref, threshold overrides (optional), weight/rationale ("weighted by real-library usage")
  - [x] Initial entries: a handful of freely-licensed sfz community instruments (small enough for CI download/cache; e.g. from the sfz community/sfzinstruments catalog — verify each license permits redistribution or fetch-at-build)
  - [x] Documented policy in `corpus/README.md`: per-library (not per-opcode) tracking, how entries are added, how references are captured
- [x] Task 2: Reference capture procedure (AC: 1)
  - [x] Document in `corpus/README.md`: capture once from Sforzando (SFZ oracle) — exact player version, sample rate 48 kHz, the fixed MIDI file, offline/realtime capture method, normalization rules
  - [x] Check in the captured reference renders (or store via Git LFS if size demands — decide and document; prefer short MIDI sequences that keep waves small)
  - [x] Fixed MIDI sequences per entry checked in (cover velocity layers, sustain, release tails)
- [x] Task 3: Corpus runner on the render harness (AC: 1, 2)
  - [x] Extend the Story-1.4 offline render harness into a corpus runner: for each manifest entry → lower → validate → render fixed MIDI → diff against reference within NFR-5 thresholds
  - [x] Diff metrics: define concretely (e.g. per-window RMS error + peak deviation + duration/silence checks) with default threshold + per-entry override; document in `corpus/README.md` — this instantiates "NFR-5 thresholds" for real
  - [x] Output: per-library pass/fail report (machine-readable JSON + human summary) — the seed of the Epic-7 fidelity scoreboard
- [x] Task 4: CI wiring (AC: 1, 2)
  - [x] CI job runs the corpus suite (at least on Linux; all platforms if runtime allows) every change; corpus assets cached (actions/cache keyed on manifest checksum)
  - [x] A rendering change vs. reference → job fails with the per-library diff report surfaced in the job output/artifacts (uploaded report artifact)
  - [x] Keep runtime sane: corpus is small at this stage; note scaling strategy (nightly full run vs. per-push subset) in the README for later
- [x] Task 5: Golden-snapshot integration (AC: 2)
  - [x] Corpus entries also get golden lowering snapshots (Story 1.3 harness) so lowering drift and rendering drift are separately attributable

## Dev Notes

- **This is AD-10 made real:** fidelity infrastructure is architecture, and the corpus starts measuring in Epic 1 — Epics 2 and 4 only *extend* the manifest (SF2/SF3 slice vs FluidSynth, DS slice vs Decent Sampler captures) and Epic 7 publishes the scoreboard. Build the runner format-agnostic: entry → (frontend by format) → render → diff. Only the SFZ path is live now.
- **Oracle references are captured once and checked in** — CI never runs Sforzando (it's closed-source, GUI, not CI-able). The documented capture procedure is what makes the references reproducible/re-capturable. FluidSynth (Epic 2) *can* run headless, but the checked-in-capture pattern stays uniform.
- **Threshold design matters:** absolute byte equality is impossible across engines; too-loose thresholds measure nothing. Start strict on structural properties (note onsets present, envelope shape, loop continuity) with a documented numeric tolerance; per-entry overrides absorb known engine differences. Record every override's rationale in the manifest.
- **Licensing care:** corpus libraries are redistributed or fetched — each entry's license field is mandatory; only include freely-licensed instruments (CC0/CC-BY/GPL-compatible). GeneralUser GS and Pianobook entries arrive in later epics with the same rule.
- **PRD v1 gate context (§6):** 100% of corpus loads, ≥90% renders correctly per format — the report format should already express these two numbers per format so the gate is computable from day one.
- **Repo size discipline:** sample libraries can be large. Prefer tiny instruments now; if references/samples exceed a few MB, use Git LFS or CI-time fetch with checksums — decide once, document in `corpus/README.md`.
- Depends on: Story 1.3 (SFZ frontend + goldens), Story 1.4 (render harness). Runs headless via `sampler-core` only — no plugin involvement (AD-10: harnesses drive core directly).

### Project Structure Notes

- `corpus/` (manifest, README, MIDI fixtures, references or LFS pointers), `tests/render/` (corpus runner), `.github/workflows/` (corpus job + caching).

### References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-10, #Consistency-Conventions]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md#Quality-is-infrastructure]
- [Source: docs/planning-artifacts/epics.md#Story-1.6]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-6, #NFR-5, #§6-Success-Metrics]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Toolchain-and-QA, #Format-reference-sources]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5)

### Debug Log References

- Local: 84/84 ctest pass (Release, Windows), including 6 new SMF-reader and corpus-entry tests.
- Corpus check mode: 3/3 PASS, diffs at the PCM16 quantization floor (peak ~1.9e-5 vs 1e-2 threshold).
- Negative check: claves render diffed against ocarina reference → peak 0.135, `passed: false`.

### Completion Notes List

- **Architecture:** split orchestration from rendering. `corpus/tools/run_corpus.py` (stdlib-only Python) parses the manifest, fetches assets at a pinned VCSL commit with per-file sha256 verification (hard-fail on mismatch), and aggregates the per-library report with per-format `load_pct`/`render_pass_pct` (PRD §6 gate numbers). `fbsampler-corpus-render` (C++, links `sampler-core` only — AD-10 headless) renders one entry: lower → validate → SMF timeline → `renderOffline` → diff.
- **Initial entries (3, all VCSL, CC0-1.0, ~1 MB each):** recorder staccato (PCM16/48k, offset/tune/ampeg), ocarina staccato (PCM24/44.1k — exercises bit-depth + rate conversion), claves (three lovel/hivel velocity layers). Chosen after verifying WAV-only reader support and warn-don't-abort opcode policy.
- **Fixed MIDI checked in as real .mid files** (generated by `corpus/tools/make_midi.py`): identical bytes drive the Sforzando oracle capture and the offline runner. A minimal SMF format-0/1 reader (`tests/render/midi_file.*`) converts to `TimelineEvent`s; unit tests pin delta/varint decoding, tempo scaling, running status, note-on-vel-0, track merging, and truncation rejection.
- **References are provisional self-captures** (user-approved decision): Sforzando is GUI-only and cannot run in CI, so references were recorded from this engine via `--update-references` and marked `reference_provenance: provisional-self-capture`. They pin today's behavior — any rendering change is flagged from day one; the documented capture procedure swaps in true oracle captures later without touching the machinery. References are PCM16 stereo (2.3 MB total, checked in; LFS decision documented in README).
- **Diff metrics (NFR-5 instantiated):** peak 1e-2, whole-render RMS 1e-3, worst 4096-frame window RMS 3e-3, exact duration/shape check, silence guard (energy floor) so silent-vs-silent can never pass. Per-entry overrides in manifest require recorded rationale.
- **Task 5:** each entry gets a lowering golden (`corpus/golden/*.expected.txt`, Story-1.3 `serializeModel` byte-exact compare, done in the runner) so lowering drift and rendering drift are separately attributable. The self-test in `corpus_render_test.cpp` runs the whole entry pipeline on the checked-in seed instrument, network-free, on every platform.
- **CI:** new `corpus` job (Linux, per-push): builds only `fbsampler-corpus-render`, restores `corpus/cache` via `actions/cache` keyed on `hashFiles('corpus/manifest.json')`, runs the suite, uploads `corpus-report.json` artifact with `if: always()`. Scaling strategy (per-push subset vs nightly full, `tier` field) documented in README.

### File List

- corpus/manifest.json (new)
- corpus/README.md (rewritten from placeholder)
- corpus/tools/make_midi.py (new)
- corpus/tools/run_corpus.py (new)
- corpus/midi/vcsl-baroque-soprano-recorder-staccato.mid (new, generated)
- corpus/midi/vcsl-ocarina-small-staccato.mid (new, generated)
- corpus/midi/vcsl-claves.mid (new, generated)
- corpus/reference/vcsl-baroque-soprano-recorder-staccato.wav (new, provisional capture)
- corpus/reference/vcsl-ocarina-small-staccato.wav (new, provisional capture)
- corpus/reference/vcsl-claves.wav (new, provisional capture)
- corpus/golden/vcsl-baroque-soprano-recorder-staccato.expected.txt (new)
- corpus/golden/vcsl-ocarina-small-staccato.expected.txt (new)
- corpus/golden/vcsl-claves.expected.txt (new)
- tests/render/midi_file.h (new)
- tests/render/midi_file.cpp (new)
- tests/render/midi_file_test.cpp (new)
- tests/render/corpus_render.h (new)
- tests/render/corpus_render.cpp (new)
- tests/render/corpus_render_main.cpp (new)
- tests/render/corpus_render_test.cpp (new)
- tests/render/fixtures/seed/seed_corpus_test.mid (new, generated)
- tests/render/wav_io.h (modified: writePcm16Wav)
- tests/render/wav_io.cpp (modified: writePcm16Wav)
- tests/CMakeLists.txt (modified: new sources + fbsampler-corpus-render target)
- .github/workflows/ci.yml (modified: corpus job)
- .gitignore (modified: corpus/cache/)
- docs/implementation-artifacts/1-6-the-corpus-starts-measuring.md (modified)
- docs/implementation-artifacts/sprint-status.yaml (modified)

### Review Findings

- [x] [Review][Patch] Engine snapshot reclamation is unsound — `retired.clear()` in `Engine::prepare()`/`Engine::load()` can free an `EngineSnapshot` (and its sfizz synth + pinned sample handles) that the audio thread may still be mid-`process()` on, since nothing proves the audio thread has moved past the previous snapshot before the next control-thread call clears it. Fixed: added `RenderEpochGuard`/`renderEpoch` — `process()` bumps an atomic epoch at entry and exit for every return path; a retired snapshot is now held in `retiredPending` and only actually destroyed on the *next* control-thread call, after `waitForAudioThreadIdle()` proves the epoch went idle in between. [core/engine/engine.cpp]
- [x] [Review][Patch] Sample-pool slot reuse compounds the reclamation gap above: `AllRamSamplePool::acquire()` recycles a released slot with no generation tag. Fixed per decision: no separate pool-side generation counter — added an explicit invariant comment on `AllRamSamplePool` documenting that its slot-reuse safety depends entirely on Engine's epoch-gated reclamation (above) never letting a handle outlive its `EngineSnapshot`. [core/pool/all_ram_pool.cpp]
- [x] [Review][Patch] `PluginProcessor::isBusesLayoutSupported` now accepts stereo only; the pre-existing HEAD version (30b783b) accepted stereo OR mono, with no comment or rationale in the diff explaining the removal. Fixed: restored the `|| juce::AudioChannelSet::mono()` check. [plugin/plugin_processor.cpp]
- [x] [Review][Defer] Corpus manifest schema has a free-text `rationale` field but no explicit `weight`, though Task 1 literally asks for "weight/rationale ('weighted by real-library usage')" and `run_corpus.py`'s per-format rollup does not weight by usage anywhere. Decision: prose rationale satisfies intent at the current 3-entry scale; a structured `weight` field with no consumer would be speculative schema. Revisit once the corpus grows large enough (Epic 2/4) that per-format aggregation needs real weighting. — deferred, decision made during this review

- [x] [Review][Patch] `run_corpus.py`'s outer `try` doesn't wrap the manifest `json.load()` and doesn't catch `json.JSONDecodeError`/`FileNotFoundError`, so a malformed manifest or a bad `--tool` path crashes with an uncaught traceback instead of the documented exit-code-2 path. Fixed: manifest load wrapped in its own try/except, and `OSError`/`json.JSONDecodeError` added to the main try's except clause. [corpus/tools/run_corpus.py]
- [x] [Review][Patch] `jsonEscapeInto` only escapes `"`, `\`, `\n` — other control characters (e.g. `\t`, `\r`) that can appear in a `Diagnostic::message` echoed from malformed SFZ input pass through unescaped, producing invalid JSON that trips the gap above when `run_corpus.py` parses it. Fixed: now escapes `\r`, `\t`, and any other C0 control byte as `\u00XX`. [tests/render/corpus_render_main.cpp]
- [x] [Review][Patch] `modelToSfzText` silently drops `offset`/`loop` opcodes when the region's sample rate is unknown (`positionsUsable == false`), with no diagnostic surfaced — a region can silently play from frame 0 unlooped with no visible error. Fixed: `modelToSfzText` now takes an optional `std::vector<Diagnostic>*` and pushes a `engine.position_unit_dropped` warning when a region with a nonzero offset or enabled loop hits this path; `Engine::load()` passes its diagnostics vector through. [core/engine/model_to_sfz.h, core/engine/model_to_sfz.cpp, core/engine/engine.cpp]
- [x] [Review][Patch] The CI corpus-report upload uses `if-no-files-found: ignore`; the infra-error exit path (checksum mismatch, missing tool) never writes `corpus-report.json`, so that failure class produces a failed job with no report artifact at all — the "per-library diff report" guarantee doesn't hold for it. Fixed at the source instead of in ci.yml: `run_corpus.py` now writes a minimal `{schema_version, entries: [], formats: {}, error}` report on every infra-error exit path, so the artifact always exists for `if: always()` to upload. Verified: bad `--tool` path now exits 2 and still produces a report with the error message. [corpus/tools/run_corpus.py]

- [x] [Review][Defer] Corpus references are self-captures from the engine itself, not Sforzando-oracle captures, so the corpus can currently only catch regressions, never initial fidelity gaps against the oracle — deferred, pre-existing (already documented as a user-approved interim decision in this story's Completion Notes; re-surfaced for visibility only).
- [x] [Review][Defer] `Engine::prepare()` mutates the live synth while documenting (but not asserting) that it never runs concurrently with `process()` — same unenforced-contract class as the reclamation findings above; deferred pending that redesign rather than patched standalone. [core/engine/engine.cpp:87-98] — deferred, pre-existing
- [x] [Review][Defer] RT-safety detector (`CheckedMutex`/`rtcheck`) only reports violations via an atomic counter and depends on the test binary's allocation hooks; the header doc doesn't foreground that production builds have no enforcement for the allocation class — documentation-accuracy gap, not a functional regression in this diff. [core/include/fbsampler/detail/rt_check.h:19-26] — deferred, pre-existing
- [x] [Review][Defer] Corpus CI job fetches pinned assets from live GitHub raw URLs on every uncached run with no retry/fallback beyond the manifest-hash cache — accepted network dependency, documented in README. [.github/workflows/ci.yml corpus job] — deferred, pre-existing
- [x] [Review][Defer] `midi_file.cpp`'s alien-chunk handling increments a `uint16_t trackCount` that can wrap at 65535 alien chunks, silently truncating remaining tracks — real in theory, practically unreachable for the small, hand-authored corpus/test MIDI files this parser processes. [tests/render/midi_file.cpp:208-215] — deferred, pre-existing
- [x] [Review][Defer] `tests/render/wav_io.cpp` test-fixture writer doesn't assert equal channel-array lengths (OOB read) or guard NaN samples bypassing the int16 clamp (UB on cast) — confined to internal test-fixture generation with controlled inputs, not shipped/production code. [tests/render/wav_io.cpp:33-58,96-101] — deferred, pre-existing
- [x] [Review][Defer] No structured field records *why* a per-entry threshold override was applied (Dev Notes ask for a recorded rationale); currently moot since none of the 3 entries use an override. [corpus/manifest.json schema] — deferred, pre-existing

## Change Log

- 2026-07-19: Story 1.6 implemented — corpus manifest (3 VCSL CC0 entries, pinned + sha256), SMF reader, corpus entry renderer + diff (NFR-5 thresholds), provisional self-captured references + lowering goldens, run_corpus.py report (per-format load/render-pass %), CI corpus job with asset caching and report artifact. All 84 tests pass; corpus check mode 3/3 PASS.
