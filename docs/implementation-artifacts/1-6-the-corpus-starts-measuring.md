# Story 1.6: The corpus starts measuring

Status: ready-for-dev

## Story

As the maintainer,
I want the corpus manifest and offline render harness wired into CI,
so that fidelity is measured from the first release, not retrofitted.

## Acceptance Criteria

1. **Given** the corpus manifest listing initial SFZ entries (sfz community instruments), **when** the CI harness renders fixed MIDI through each corpus entry, **then** output diffs against checked-in reference captures (recorded once from the external oracle — Sforzando for SFZ — with the capture procedure documented in-repo) within NFR-5 thresholds and reports pass/fail per library.
2. **Given** a change that alters rendering of a corpus entry, **when** CI runs, **then** the regression is flagged with a per-library diff report.

## Tasks / Subtasks

- [ ] Task 1: Corpus manifest (AC: 1)
  - [ ] `corpus/manifest.{json|yaml}` — schema-versioned (spine convention): per entry: id, format, source URL, license, exact version/checksum, fixed MIDI test sequence ref, threshold overrides (optional), weight/rationale ("weighted by real-library usage")
  - [ ] Initial entries: a handful of freely-licensed sfz community instruments (small enough for CI download/cache; e.g. from the sfz community/sfzinstruments catalog — verify each license permits redistribution or fetch-at-build)
  - [ ] Documented policy in `corpus/README.md`: per-library (not per-opcode) tracking, how entries are added, how references are captured
- [ ] Task 2: Reference capture procedure (AC: 1)
  - [ ] Document in `corpus/README.md`: capture once from Sforzando (SFZ oracle) — exact player version, sample rate 48 kHz, the fixed MIDI file, offline/realtime capture method, normalization rules
  - [ ] Check in the captured reference renders (or store via Git LFS if size demands — decide and document; prefer short MIDI sequences that keep waves small)
  - [ ] Fixed MIDI sequences per entry checked in (cover velocity layers, sustain, release tails)
- [ ] Task 3: Corpus runner on the render harness (AC: 1, 2)
  - [ ] Extend the Story-1.4 offline render harness into a corpus runner: for each manifest entry → lower → validate → render fixed MIDI → diff against reference within NFR-5 thresholds
  - [ ] Diff metrics: define concretely (e.g. per-window RMS error + peak deviation + duration/silence checks) with default threshold + per-entry override; document in `corpus/README.md` — this instantiates "NFR-5 thresholds" for real
  - [ ] Output: per-library pass/fail report (machine-readable JSON + human summary) — the seed of the Epic-7 fidelity scoreboard
- [ ] Task 4: CI wiring (AC: 1, 2)
  - [ ] CI job runs the corpus suite (at least on Linux; all platforms if runtime allows) every change; corpus assets cached (actions/cache keyed on manifest checksum)
  - [ ] A rendering change vs. reference → job fails with the per-library diff report surfaced in the job output/artifacts (uploaded report artifact)
  - [ ] Keep runtime sane: corpus is small at this stage; note scaling strategy (nightly full run vs. per-push subset) in the README for later
- [ ] Task 5: Golden-snapshot integration (AC: 2)
  - [ ] Corpus entries also get golden lowering snapshots (Story 1.3 harness) so lowering drift and rendering drift are separately attributable

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

### Debug Log References

### Completion Notes List

### File List
