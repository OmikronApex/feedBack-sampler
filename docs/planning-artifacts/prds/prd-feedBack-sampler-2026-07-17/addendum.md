# Addendum — feedBack-sampler PRD

Technical depth and rationale that informs downstream work (architecture, epics) but does not belong in the PRD's capability narrative. Primary source: `docs/planning-artifacts/research/technical-universal-vsti-sampler-multi-format-research-2026-07-17.md`.

## Engine strategy (research conclusion, for architecture phase)

- Build on **sfizz** (BSD-2-Clause) as the SFZ/voice-engine foundation; extend rather than rewrite. sfizz coverage snapshot: ~96% SFZ v1, ~44% SFZ v2, ~45% ARIA — full fidelity is a multi-release long tail, measured by library-corpus coverage, not opcode count.
- Lower all four formats into one **SFZ-superset region/voice/modulation-matrix model**. sfizz's internals (Region-as-data, Voice-as-execution, generic modulation matrix) validate the design; it already has experimental .dspreset import.
- **FluidSynth** (LGPL-2.1) is the behavioral oracle for SF2/SF3 in rendering-regression tests; embed only if a dedicated SF2 voice path proves necessary (open architecture question: SF2 modulator matrix may not lower 1:1 to SFZ opcodes).
- **SF3**: decode Vorbis to PCM at load time (stb_vorbis or libvorbis); never decode on the audio thread in v1.
- Ship the engine core as a static library consumed by (a) JUCE plugin targets and (b) feedBack's in-app engine — sfizz/sfizz-ui split as the model. This split is also what keeps the v2 plugin-author API feasible without rework.

## Licensing matrix (NFR-6 backing)

| Component | License | AGPL-3.0 fit |
|---|---|---|
| JUCE 8 | AGPLv3 (dual) | native |
| sfizz | BSD-2-Clause | yes |
| FluidSynth/FluidLite | LGPL-2.1 | yes |
| stb_vorbis / libvorbis | PD-MIT / BSD-3 | yes |

## Toolchain and QA (for architecture/epics)

- CMake + JUCE 8 + Catch2; pamplejuce template as reference CI setup (GitHub Actions, signing/notarization).
- pluginval strictness ≥5 floor, 10 target, in CI (FR-16); ~3–9 min per platform warm.
- Golden-file tests: library → normalized region-model snapshot, per format.
- Rendering regression: offline-render fixed MIDI against corpus, diff vs. FluidSynth/Sforzando references (NFR-5).
- RT-safety: debug-build allocation/lock detectors around processBlock (NFR-1).

## Format reference sources

- SFZ: sfzformat.com (authoritative opcode catalog, v1/v2/ARIA); sfizz opcode support table for coverage tracking.
- SF2: SoundFont 2.04 spec (synthfont.com/sfspec24.pdf) — RIFF hydra, 58 generators, modulators, mandated structural validation.
- SF3: unofficial; FluidSynth wiki SoundFont3Format + MuseScore sf3convert behavior.
- Decent Sampler: official format documentation + developers guide (readthedocs, tracks releases); community XSD (praashie/DecentSampler-schema). Vendor-controlled, moving format — track per release; UI layer best-effort, audio layer conformance target.

## Deferred-scope rationale

- **Author API v2**: MIDI + preset selection via feedBack's plugin API is the likely v1.5 shape; full engine API (per-voice control) is the v2 decision. Deferred to keep v1 shippable solo.
- **MPE/MIDI 2.0**: JUCE VST3 note-expression has known quirks (works cleanly in AU); revisit when framework support settles.
- **In-app downloads**: curation + hosting + licensing-of-content questions; not a v1 problem.
