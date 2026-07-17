# Reconciliation — technical research doc vs. prd.md + addendum.md

Input: `docs/planning-artifacts/research/technical-universal-vsti-sampler-multi-format-research-2026-07-17.md`

## Coverage check

| Research finding | Landed in |
|---|---|
| AGPL licensing matrix (JUCE/sfizz/FluidSynth/Vorbis) | NFR-6 + addendum matrix |
| Unified SFZ-superset model, sfizz foundation | addendum (engine strategy) |
| sfizz coverage ~96/44/45% → corpus-based fidelity | FR-1, FR-6, §6 metrics, addendum |
| SF2 preset/bank hierarchy browsable | FR-9 |
| SF3 = SF2 + Vorbis, decode at load | FR-3 + addendum |
| Decent Sampler moving format, UI best-effort | FR-4/FR-12/FR-20 frame + addendum |
| RT-safety canon, streaming, voice stealing | NFR-1/2, FR-13/14 |
| pluginval, golden-file, rendering regression, fuzzing | FR-16, NFR-4/5, addendum QA |
| Engine-as-static-library shared app/plugin | FR-18/19 + addendum |
| MPE/VST3 note-expression caveat | Group E + addendum |
| Phased roadmap (spike → DS → SF2/SF3 → fidelity climb) | §7 release shape (coarse) + addendum; detailed phasing left to epics — intentional |
| SF2-modulator-lowering open question | addendum (flagged for architecture) |

## Gaps found

1. **Minor**: research KPI "opcode support table published per release" is implied by FR-6 ("each release states what it plays correctly") but the public-facing conformance-publishing idea could be made explicit. → folded into FR-6 wording already; acceptable.
2. **None qualitative** — no tone/voice/feel content in the input was dropped.

Verdict: no PRD changes required.
