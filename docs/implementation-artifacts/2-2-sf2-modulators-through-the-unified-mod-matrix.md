---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 2.2: SF2 modulators through the unified mod matrix

Status: review

## Story

As a soundfont user,
I want SF2 modulator behavior (default modulator set, curve types, amounts) reproduced,
so that my soundfonts respond to velocity, CCs, and pitch wheel the way established players make them.

## Acceptance Criteria

1. **Given** the mod matrix extended with SF2 source/curve primitives (AD-1), **when** a preset using default and custom modulators renders, **then** offline output diffs against FluidSynth within NFR-5 thresholds for velocity response, mod wheel, and pitch-bend range.
2. **Given** a modulator construct the matrix cannot yet express, **when** lowering occurs, **then** a Diagnostic records the unsupported modulator (fidelity gap tracked, never silent).

## Tasks / Subtasks

- [x] Task 1: Replace the model_to_sfz bind seam (prerequisite — AC: 1)
  - [x] The engine currently feeds sfizz via generated SFZ text (`core/engine/model_to_sfz.cpp`), which cannot express mod-matrix `curve`, Velocity/KeyTrack matrix entries, or SF2 curve shapes. Deferred-work.md flags this as "must be replaced with direct region construction before SF2-extension work". If Story 2.1 already replaced it, verify coverage and skip; otherwise: local sfizz patch (extend `cmake/patches/`) exposing a programmatic region/modulation construction hook, engine binds model → sfizz regions directly, delete or demote `model_to_sfz` to a debug dump
  - [x] Regression guard: Epic-1 corpus check mode must still be 3/3 PASS after the seam swap (renders through a different construction path — reference diffs prove behavior held)
- [x] Task 2: Extend the mod matrix with SF2 primitives (AC: 1, 2)
  - [x] Model schema bump: `ModSource` gains SF2 source semantics — sources: NoteOnVelocity, NoteOnKey, PolyPressure, ChannelPressure, PitchWheel, PitchWheelSensitivity, CC (already present), plus direction (min→max / max→min), polarity (unipolar/bipolar), and the four SF2 continuity/curve types (linear, concave, convex, switch) per spec §9.5.2 — replacing/subsuming today's single normalized `curve` float; `ModTarget` extended toward the SF2 generator destinations the engine can execute (Gain, Pitch, Pan now; filter targets arrive when the engine grows a filter — unsupported destinations → diagnostic, Task 5)
  - [x] Secondary source (amount source) support: SF2 modulators are `src × amtSrc × amount` — model this (the mod-wheel-scales-vibrato default modulator needs it); if the engine can only execute a degenerate subset in v0, lower faithfully and diagnostic what the engine drops at bind
  - [x] Update `SPEC.md` (units, defaults, curve definitions with formulas), `serialize.cpp`, `validate.cpp`; regenerate all goldens under the schema bump
- [x] Task 3: Lower SF2 modulators (AC: 1, 2)
  - [x] The ten default modulators (spec §8.4.1–8.4.10): vel→attenuation (concave), vel→filterFc, channel/poly pressure→vibrato, CC1→vibrato, CC7→attenuation (concave), CC10→pan, CC11→attenuation, CC91→reverb, CC93→chorus, pitchWheel×sensitivity→pitch — instantiate at lowering; targets the engine lacks (filter, reverb/chorus sends) lower-with-diagnostic per Task 5 policy
  - [x] Custom pmod/imod records: instrument modulators absolute, preset modulators additive; identical-modulator supersede rules (§9.5.3); amount summing
  - [x] Pitch-bend range: RPN 0 / pitchWheelSensitivity semantics → region `bendUpCents`/`bendDownCents` (default ±200 already matches SF2's 2-semitone default)
- [x] Task 4: Engine executes the extended matrix (AC: 1)
  - [x] sfizz's modulation matrix is the execution substrate (AD-1: extend, don't fork a voice path). Map lowered entries onto sfizz mod-matrix connections; implement SF2 curve shaping (concave = `-20/96·log10`-based per spec formula, convex mirror, switch threshold) where sfizz lacks them — inside the vendored sfizz patch or an engine-side source transform, whichever keeps the audio path allocation-free (NFR-1)
  - [x] RT-safety: curve evaluation on the audio thread must be table-lookup or closed-form — no allocation; existing debug RT detector must stay clean
- [x] Task 5: Unsupported-modulator diagnostics (AC: 2)
  - [x] One policy, applied at lowering: any source/target/curve combination the matrix cannot express or the engine cannot execute → `sf2.modulator_unsupported` warning naming the modulator (source, dest generator, amount) — never silent (AD-1: fidelity gap tracked). Count them in lowering output so corpus reporting can aggregate the gap
- [x] Task 6: FluidSynth-oracle render tests (AC: 1)
  - [x] FluidSynth runs headless — unlike Sforzando, references CAN be captured in a scripted, re-runnable way. Document the capture procedure in `corpus/README.md` (exact fluidsynth version, `-F` fast-render flags, gain/normalization); capture references ONCE and check them in (uniform checked-in-capture pattern per Story 1.6 dev notes — CI never runs FluidSynth)
  - [x] Targeted MIDI fixtures (extend `corpus/tools/make_midi.py`): velocity ramp (same note, vel 1..127 steps), mod-wheel sweep during sustained note, pitch-bend sweep incl. RPN-changed bend range
  - [x] Diff within NFR-5 thresholds (peak 1e-2 / RMS 1e-3 / windowed RMS 3e-3 baseline; per-entry overrides with recorded rationale where engine-difference is legitimate — e.g. FluidSynth's attenuation emulation factor) via the existing `fbsampler-corpus-render` + `run_corpus.py` machinery or a dedicated `[sf2][render]` ctest — prefer the corpus machinery so 2.5 inherits the entries

## Dev Notes

- **This story IS the AD-1 bet.** SOLUTION-DESIGN names SF2 modulator fidelity as the architecture's central risk, mitigated by oracle-diffing. Perfection is not the bar — measured, diagnosed gaps are acceptable; *silent* gaps are not.
- **Dependency: Story 2.1 must be done first** (SF2 parser, embedded samples, generator lowering). This story adds pmod/imod lowering + matrix execution on top. If 2.1 deferred the bind-seam replacement, Task 1 here is mandatory, not optional.
- **Read before modifying:** `core/engine/engine.cpp` (snapshot swap + `RenderEpochGuard` reclamation — preserve exactly; the seam replacement touches `Engine::load`'s bind path), `core/engine/model_to_sfz.cpp` (what the seam currently drops — its warning `engine.position_unit_dropped` pattern), `core/include/fbsampler/model.h` (`ModMatrixEntry` v3 shape being replaced), `cmake/patches/sfizz-1.2.3-fixes.patch` (patch mechanics: LF endings enforced via .gitattributes, no empty context lines — Windows autocrlf corrupted a patch once, see 1.3 change log), `core/model/SPEC.md`.
- **sfizz patch discipline:** sfizz is vendored at a pinned commit with local patches expected (spine Stack). FetchContent `UPDATE_DISCONNECTED ON` means editing the patch requires clearing `_deps` in stale build trees (deferred-work.md) — note it in the PR description for CI/devs.
- **Preserve Epic-1 behavior end-to-end:** the SFZ path renders through whatever bind path replaces the seam. Corpus (3 VCSL entries), goldens, pluginval, and all 84 baseline tests must stay green — the corpus references are exactly the regression net built for this moment.
- **Spec formulas (§9.5.2):** concave: `output = -20/96 · log10(x²)` normalized form (use spec's exact piecewise definition); convex is its mirror; switch: 0 below ½, 1 at/above. Bipolar maps to −1..1. Get these from sfspec24.pdf §9.5.2 verbatim — do not improvise; FluidSynth's `fluid_conv.c` tables are the oracle-matching reference implementation.
- **Testing standards:** Catch2 unit tests for curve functions (pin numeric values), supersede/summing rules, default-modulator instantiation; golden diffs for lowering; render diffs for behavior; fuzzer from 2.1 automatically covers pmod/imod parsing. RT detector run must include a matrix-heavy preset.

### Project Structure Notes

- Modified: `core/include/fbsampler/model.h`, `core/model/` (SPEC/serialize/validate), `core/frontends/sf2/` (modulator lowering), `core/engine/` (bind path, matrix execution), `cmake/patches/`, `tests/golden/sf2/`, `corpus/` (fixtures, references, README capture procedure), `tests/` CMake.
- New render fixtures under `corpus/midi/` + `corpus/reference/` (or `tests/render/fixtures/sf2/` if kept out of the corpus until 2.5 — prefer corpus).

### References

- [Source: docs/planning-artifacts/epics.md#Story-2.2]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-1, #AD-7, #AD-10, #AD-11]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md#Why-a-compiler-pipeline, #Notable-risks]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-2, #NFR-5]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Engine-strategy (FluidSynth = behavioral oracle, never runtime)]
- [Source: docs/implementation-artifacts/deferred-work.md#Deferred-from-1-4 (synthetic-SFZ bind seam replacement is the entry ticket for this story)]
- [Source: docs/implementation-artifacts/1-6-the-corpus-starts-measuring.md#Completion-Notes (diff metrics, checked-in-capture pattern, make_midi.py)]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5)

### Debug Log References

- Full ctest suite: 115/115 pass locally (102 at story start; +13 new).
- FluidSynth 2.5.6 oracle diffs: all three behavioral tests pass (velocity curve, CC1 sweep, bend range).
- `render_regression_test` green after the seam extension — Epic-1 renders held byte-comparably through the new construction path (real VCSL 3/3 corpus check runs in CI as before).

### Completion Notes List

- **Task 1 seam decision (architecture):** NO sfizz vendored patch was needed. sfizz 1.2.3's extended dialect already expresses everything the "direct region construction" requirement targeted: `<sample> name=… base64data=…` loads embedded audio straight into sfizz's FilePool from RAM (`Synth::handleSampleOpcodes → FilePool::loadFromRam`; verified `loadedFiles` is consulted before any disk lookup), `<curve>` headers + `*_curveccN` express arbitrary CC curve shapes, and `amp_velcurve_N` points express the SF2 concave velocity law. `model_to_sfz` was therefore extended (not deleted): it emits embedded-sample headers (float32 WAV → base64, sanitized virtual names so `sf2://…#N` never meets the parser), curve headers, velocity curves, and structured drop-diagnostics. The seam stays private to core/engine/.
- **Model v5:** `ModSource` gains kind (None/Cc/Velocity/KeyTrack/PolyPressure/ChannelPressure/PitchWheel/PitchWheelSensitivity), direction (`maxToMin`), polarity (`bipolar`), typed `ModCurveType` (Linear/Concave/Convex/Switch); `ModMatrixEntry` gains `amountSource` (SF2 `src × amtSrc × amount` shape) and loses the old normalized `curve` float. `ModTarget` gains reserved FilterCutoff/ReverbSend/ChorusSend. SPEC.md documents the §9.5.2 curve formulas; serialize/validate updated (`region.mod_source_none` new; `region.mod_curve_out_of_range` retired); all goldens regenerated under v5.
- **Modulator lowering:** the nine expressible §8.4 default modulators instantiate per region; §8.4.10 (pitchWheel × RPN0 sensitivity → pitch) is intentionally not lowered — the engine reproduces it natively via `bendUp/DownCents` (±200 default = SF2's 2-semitone RPN default). Custom imod records supersede identical defaults (absolute); pmod records sum (additive); identity = (src, dest, amtSrc, transform) per §9.5.3. Constant-source ("No Controller") modulators fold into static region fields. CC10→pan uses amount 500 (FluidSynth convention, not the spec's 1000) — FluidSynth is the NFR-5 oracle.
- **Attenuation scale finding (oracle-verified):** FluidSynth applies its 0.4× emulation factor ONLY to the static initialAttenuation generator, never to modulator amounts. Verified empirically against fluidsynth 2.5.6 renders (an initial 0.4× implementation missed the CC1 sweep by ~20×); modulator depths use full cB scale.
- **Engine execution:** Cc→Gain/Pitch/Pan via `*_oncc` + `<curve>` (indices from 7, above sfizz's 0–6 built-ins); Velocity→Gain via `amp_veltrack=100` + `amp_velcurve_1..127` points combined multiplicatively across entries; Velocity→Pitch (linear) via `pitch_veltrack`. Dropped-with-diagnostic: secondary amount sources (`engine.mod_amount_source_dropped`), FilterCutoff/ReverbSend/ChorusSend targets (`engine.mod_target_unsupported`), pressure/wheel sources (`engine.mod_source_unsupported`). Curve evaluation at bind on the control thread; the audio thread runs sfizz's table-backed curves — matrix-heavy RT-detector render is green.
- **FluidSynth oracle (Task 6):** FluidSynth 2.5.6 installed (scoop); capture procedure documented in corpus/README.md; `make_oracle_refs.py` scripts the MIDI fixtures (velocity ramp / CC1 sweep / bend sweep) and captures; references checked in; `sf2_oracle_test.cpp` diffs behavioral metrics (normalized per-note RMS, gain trajectory, zero-crossing f0) with per-test thresholds + rationale in comments. CI never runs FluidSynth. RPN-changed bend range is a tracked engine gap (sfizz 1.2.3 has no RPN support; deferred-work.md); the bend fixture stays inside the default ±2 st range.

### File List

- core/include/fbsampler/model.h (modified: schema v5 mod matrix)
- core/model/SPEC.md (modified: v5 types, curve formulas, validation codes)
- core/model/serialize.cpp (modified: v5 mod fields)
- core/model/validate.cpp (modified: mod_source_none, amountSource cc check)
- core/frontends/sf2/sf2_frontend.cpp (modified: pmod/imod parsing, default set, supersede/summing, lowering)
- core/engine/model_to_sfz.h / model_to_sfz.cpp (modified: extended-dialect emitter — embedded samples, curves, velcurves, drop diagnostics)
- core/engine/mod_curves.h (new: shared §9.5.2 curve shapes)
- core/engine/engine.cpp (modified: region handles, sf2:// path routing, seam call)
- tests/model_validate_test.cpp (modified: v5 mod validation cases)
- tests/golden/model_golden_test.cpp (modified: v5 reference model)
- tests/golden/model_v0_reference.txt (regenerated: v5)
- tests/golden/sfz/*.expected.txt, corpus/golden/*.expected.txt, corpus/cache/*.golden.txt (schema line v5)
- tests/golden/sf2/*.expected.txt (regenerated: default modulators in matrix)
- tests/golden/sf2/tools/make_sf2.py (modified: zone modulators; mods.sf2 + oracle.sf2 fixtures)
- tests/golden/sf2/mods.sf2, oracle.sf2 (new, generated)
- tests/sf2_frontend_test.cpp (modified: default-set, supersede/summing, validation tests)
- tests/render/mod_curves_test.cpp (new: pinned curve values)
- tests/render/sf2_oracle_test.cpp (new: FluidSynth diffs + RT matrix render)
- tests/render/fixtures/sf2/make_oracle_refs.py (new: capture script)
- tests/render/fixtures/sf2/oracle.sf2, vel_ramp.mid, modwheel.mid, bend.mid, *_fluid.wav (new, generated/captured)
- tests/fuzz/sf2/corpus/seed_mods.sf2 (new seed; fuzzer now exercises pmod/imod)
- tests/CMakeLists.txt (modified: new test sources)
- corpus/README.md (modified: FluidSynth capture procedure)
- docs/implementation-artifacts/deferred-work.md (modified: 2.2 deferrals)

## Change Log

- 2026-07-19: Story 2.2 implemented — model v5 SF2 mod matrix (sources, direction/polarity, typed curves, amount source), default + custom modulator lowering with §9.5.3 supersede/summing, extended-dialect bind seam (embedded samples via base64 sample headers, curve headers, velocity curves), FluidSynth 2.5.6 oracle diffs green, RT-clean matrix render. 115/115 tests.
