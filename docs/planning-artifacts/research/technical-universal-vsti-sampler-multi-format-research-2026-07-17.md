---
stepsCompleted: [1, 2, 3, 4, 5, 6]
inputDocuments: []
workflowType: 'research'
lastStep: 6
research_type: 'technical'
research_topic: 'Universal VSTi sampler with multi-format library support (Decent Sampler, SFZ, SF2/SF3) for the JUCE-based feedBack audio engine'
research_goals: 'Format deep-dive: understand SFZ opcodes, SF2/SF3 structure, and Decent Sampler XML in full-spec fidelity to design a unified internal sample model; AGPL-3.0-compatible licensing only'
user_name: 'OmikronApex'
date: '2026-07-17'
web_research_enabled: true
source_verification: true
---

# Research Report: technical

**Date:** 2026-07-17
**Author:** OmikronApex
**Research Type:** technical

---

## Research Overview

This research investigates building a universal VSTi sampler for the JUCE-based feedBack audio engine, supporting four open library formats — Decent Sampler (.dspreset), SFZ, SF2, and SF3 — at full-spec fidelity, under an AGPL-3.0 licensing constraint. It covers the format specifications in depth, candidate open-source engines and libraries, plugin and engine integration patterns, sampler-engine architecture (region model, voice engine, modulation matrix, real-time safety), and a phased implementation strategy with risks and success metrics.

Headline findings: the licensing constraint is fully satisfiable (JUCE 8 AGPLv3, sfizz BSD-2, FluidSynth LGPL-2.1); all four formats can be lowered into a single SFZ-superset internal model — a design validated by sfizz's architecture and its experimental Decent Sampler support; and "full-spec fidelity" is a long tail (best-in-class open SFZ v2/ARIA coverage is ~44–45%), so fidelity should be measured by real-world library corpus coverage. See the Research Synthesis section for the full executive summary and recommendations.

All claims were verified against current primary sources (format specifications, project repositories and documentation) with confidence levels noted where sources are informal or incomplete.

---

<!-- Content will be appended sequentially through research workflow steps -->

## Technical Research Scope Confirmation

**Research Topic:** Universal VSTi sampler with multi-format library support (Decent Sampler, SFZ, SF2/SF3) for the JUCE-based feedBack audio engine
**Research Goals:** Format deep-dive at full-spec fidelity — SFZ opcodes, SF2/SF3 structure, Decent Sampler XML — to design a unified internal sample model; AGPL-3.0-compatible licensing only

**Technical Research Scope:**

- Architecture Analysis - unified sample model, voice engine design, patterns from existing multi-format samplers
- Implementation Approaches - parsing strategies per format, full-fidelity opcode/modulator coverage
- Technology Stack - JUCE integration, AGPL-compatible candidate libraries (sfizz, FluidSynth, Vorbis decoders)
- Integration Patterns - VSTi plugin surface, feedBack engine embedding, format-agnostic preset APIs
- Performance Considerations - voice allocation, streaming vs. RAM, SF3 Vorbis decode cost, real-time safety

**Research Methodology:**

- Current web data with rigorous source verification
- Multi-source validation for critical technical claims
- Confidence level framework for uncertain information
- Comprehensive technical coverage with architecture-specific insights

**Scope Confirmed:** 2026-07-17

## Technology Stack Analysis

### Programming Languages

C++ (C++17/20) is the de facto language for real-time sampler engines: JUCE, sfizz, and FluidSynth are all C++ or C. All candidate reference implementations for the four target formats are C/C++ codebases, so the sampler engine should be C++ living alongside feedBack's existing JUCE engine.
_Popular Languages: C++ (JUCE, sfizz), C (FluidSynth, stb_vorbis)_
_Performance Characteristics: C++ with lock-free/RT-safe patterns is required for the audio thread; parsing/loading can be done off-thread_
_Source: https://github.com/sfztools/sfizz, https://www.fluidsynth.org/_

### Development Frameworks and Libraries

- **JUCE 8** — application/plugin framework; dual-licensed **AGPLv3 / commercial**. Since feedBack is AGPL-3.0, JUCE's AGPLv3 track is a clean fit at zero cost. _Source: https://github.com/juce-framework/JUCE/blob/master/LICENSE.md_
- **sfizz** (sfztools) — SFZ parser + synth C++ library, **BSD-2-Clause** (AGPL-compatible). Supports SFZv1 fully-targeted, partial SFZv2/ARIA, and notably **experimental partial Decent Sampler (.dspreset) support**. A JUCE wrapper existed (`sfizz-juce`, now superseded by `sfizz` + `sfizz-ui`). Strongest candidate as reference implementation or embedded engine for SFZ. _Source: https://github.com/sfztools/sfizz, https://sfztools.github.io/sfizz/development/status/opcodes/_
- **FluidSynth / FluidLite** — SF2/SF3 synthesizer, **LGPL-2.1** (GPLv3/AGPLv3-compatible). The canonical open SF2 renderer including the full modulator model; FluidLite is a lighter embeddable variant. _Source: https://www.fluidsynth.org/, https://deepwiki.com/divideconcept/FluidLite/4.1-soundfont-file-formats_
- **Ogg Vorbis decode** — needed for SF3 sample data: libvorbis (BSD-3) or stb_vorbis (public domain/MIT); both AGPL-compatible.

### Format Landscape (deep-dive targets)

- **SFZ** — plain-text opcode format; the authoritative community spec is sfzformat.com, which catalogs SFZv1, SFZv2 (flex EGs, `<effect>` headers, curves) and ARIA extensions per-opcode with per-player support matrices. Full-spec fidelity means several hundred opcodes plus the modulation matrix (flex EGs/LFOs, MIDI conditions, `_oncc` real-time modulation). sfizz publishes an opcode support table that quantifies what "full coverage" looks like in practice. _Source: https://sfzformat.com/opcodes/, http://ariaengine.com/overview/sfz-format/sfz-opcodes/_
- **SF2 (SoundFont 2.04)** — binary RIFF format: INFO / sdta (sample data) / pdta ("hydra": preset headers, zones, PGEN/PMOD generator & modulator lists, instruments, sample headers). Spec mandates structural validation (monotonic gen/mod indices, sub-chunk size checks) with defined rejection rules. Full fidelity = 58 generators + the default & custom modulator model. _Source: http://www.synthfont.com/sfspec24.pdf, https://mrtenz.github.io/soundfont2/getting-started/soundfont2-structure.html_
- **SF3** — unofficial SF2 extension (originated in MuseScore's sf3convert): identical hydra, but samples Ogg-Vorbis-compressed, flagged via bit 4 of `sfSampleType`; sample offsets become byte offsets into compressed data. FluidSynth wiki + a community RFC are the closest thing to a spec. _Source: https://github.com/FluidSynth/fluidsynth/wiki/SoundFont3Format_
- **Decent Sampler (.dspreset)** — XML format (`<DecentSampler>` root, groups/samples/effects/UI/MIDI bindings); documented in the official format documentation and a full developer's guide (v1.23.x); a community XSD schema exists. Bundled `.dslibrary` is a zip of the preset + samples. _Source: https://decentsampler-developers-guide.readthedocs.io/en/latest/introduction.html, https://www.decentsamples.com/docs/format-documentation.html, https://github.com/praashie/DecentSampler-schema_

### Storage and Asset Handling

No databases involved; the "storage" problem is sample asset management: RIFF binary parsing (SF2/SF3), zip container reading (.dslibrary, SFZ in some distributions), WAV/AIFF/FLAC/Ogg sample decode (JUCE audio format readers cover these), and disk-streaming vs. RAM-loading strategy for large libraries.

### Development Tools and Platforms

CMake-based builds (JUCE 8, sfizz, FluidSynth all ship CMake); plugin targets VST3/AU/LV2 via JUCE's plugin client; validation tooling: pluginval, sfizz's opcode test suites, reference soundfonts (GeneralUser GS) and Pianobook Decent Sampler libraries as conformance corpora.

### Technology Adoption Trends

The SFZ ecosystem consolidated around sfizz as the leading open engine; SF3 adoption grew via MuseScore; Decent Sampler became the dominant freeware "boutique library" format (Pianobook ecosystem). Notably, sfizz's experimental dspreset support signals convergence: an SFZ-style internal model can act as the superset representation for the other formats.
_Confidence: High for licensing and format facts (primary sources); Medium for sfizz Decent Sampler coverage depth (marked experimental)._
_Source: https://sfzlab.github.io/sfz-website/software/, https://github.com/sfztools/sfizz_

## Integration Patterns Analysis

### Plugin API Surface (external integration)

The sampler exposes itself as a VSTi via JUCE's `AudioProcessor`, which compiles to VST3/AU/AUv3/LV2 from one codebase, and the same class doubles as the hosting abstraction — relevant since feedBack's engine already hosts plugins. VST3 note-expression/MPE has known JUCE quirks (works cleanly in AU; `supportsMPE` handling on VST3 has reported issues), worth flagging if per-note expression matters.
_Source: https://docs.juce.com/develop/classjuce_1_1AudioProcessor.html, https://deepwiki.com/juce-framework/JUCE/4.1-audio-plugin-system-and-formats, https://forum.juce.com/t/audioprocessor-supportsmpe-missing-on-vst3/44347_

### Engine Embedding Patterns (internal integration)

Two viable patterns for feedBack:

1. **Embed reference engines behind a facade** — sfizz exposes clean C and C++ APIs: a `Synth` object that loads an SFZ file, receives delay-ordered MIDI-type events (notes, CCs, aftertouch, pitch-wheel), and renders via a block callback — the exact shape of a JUCE `processBlock`. FluidSynth exposes an analogous C API (`fluid_synth_*`) plus a pluggable SoundFont loader interface (`sfont.h`) for custom loading. A thin format-router facade picks the engine per loaded library.
2. **Unified internal model** — translate all four formats at load time into one internal region/voice/modulation model (SFZ-shaped, as the superset), with a single voice engine. Harder, but matches the stated full-fidelity, format-deep-dive goal; sfizz's internal architecture (parser → region model → voice engine) is the reference design to study.
_Source: https://sfztools.github.io/sfizz/api/sfizz.hpp/, https://github.com/sfztools/sfizz/blob/develop/src/sfizz.h, https://sfztools.github.io/sfizz/engine_description/, https://github.com/FluidSynth/fluidsynth/blob/master/include/fluidsynth/sfont.h_

### Communication Protocols and Event Model

MIDI 1.0 events (note on/off, CC, pitch bend, channel/poly aftertouch) are the lingua franca of all four formats' modulation sources: SFZ `_oncc`/MIDI-condition opcodes, SF2's default modulator sources, Decent Sampler `<midi>` bindings. Events must be sample-accurate (delay-ordered within the block) — sfizz explicitly declares behavior undefined otherwise. Host automation maps through JUCE parameters; consider MIDI 2.0/MPE as a forward-looking axis.
_Source: https://sfztools.github.io/sfizz/api/sfizz.hpp/, https://sfzformat.com/opcodes/_

### Data Formats and Container Interoperability

- **Text/XML**: SFZ (`#include`, `#define` preprocessing) and .dspreset XML — parse off-thread; community XSD available for validation.
- **Binary RIFF**: SF2/SF3 share the hydra; a single RIFF reader with a sample-decode strategy (PCM vs. Vorbis) covers both.
- **Containers**: `.dslibrary` (zip), zipped SFZ distributions — JUCE's `ZipFile` + audio format readers (WAV/AIFF/FLAC/Ogg) handle extraction and decode natively.
- **Preset/state**: the plugin's own state should serialize a reference to the source library + user tweaks, format-agnostically.
_Source: https://decentsampler-developers-guide.readthedocs.io/en/latest/introduction.html, http://www.synthfont.com/sfspec24.pdf_

### Interoperability Insight

sfizz's experimental .dspreset import demonstrates the key interoperability pattern: lower every format into an SFZ-superset region model rather than N independent engines. SF2's generator/modulator model is the hardest lowering (its modulator matrix doesn't map 1:1 to SFZ opcodes); FluidSynth's loader API or a bespoke SF2 voice path may be needed for full fidelity.
_Confidence: High on API shapes (primary docs); Medium on SF2→SFZ lowering completeness — flagged for architecture-phase validation._

## Architectural Patterns and Design

### System Architecture Patterns

The proven multi-format sampler architecture is a layered pipeline: **format front-ends (parsers) → unified region/instrument model → voice engine → DSP/effects → plugin shell**. sfizz's internals validate the shape: a `Synth` coordinator, semi-passive **Region** description objects that activate on MIDI-event conditions, **Voices** (polyphony pool) that get linked to a region while playing and reset to idle, plus shared **common resources** (preloaded files, EGs, LFOs). An internal **modulation matrix** links all modulation sources (CCs, LFOs, EGs) to targets — the general mechanism that can also host SF2's modulator model and Decent Sampler bindings.
_Source: https://sfz.tools/sfizz/engine_description/, https://deepwiki.com/sfztools/sfizz_

### Design Principles and Best Practices

- **Parsers produce a normalized model, never drive DSP directly** — format quirks stay at the boundary; the voice engine sees only the internal model.
- **Region = data, Voice = execution**: keeps N-format lowering testable (golden-file tests: input library → expected region model).
- **Modulation as a generic matrix** rather than per-format special cases; SFZ flex EGs/LFOs, SF2 modulators, and DS bindings become entries in the same matrix with per-format source/curve semantics.
- **Structural validation at load** per the SF2 spec's mandated rejection rules (monotonic gen/mod indices, chunk-size checks).
_Source: https://sfz.tools/sfizz/engine_description/, http://www.synthfont.com/sfspec24.pdf_

### Real-Time Safety and Threading Patterns

Canonical RT-audio rules apply: no locks, no allocation, no I/O on the audio thread; pre-allocate everything; communicate via lock-free ring buffers/message queues. Voice management best practices: handle-based access with generation counters (prevents use-after-free on voice recycling) and priority-based voice stealing. Disk streaming uses per-voice lock-free ring buffers fed by a worker thread, with a preloaded head buffer per sample so playback starts instantly while the stream catches up.
_Source: http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing, https://generalistprogrammer.com/tutorials/game-audio-programming-complete-sound-engine-guide-2025, https://www.kvraudio.com/forum/viewtopic.php?t=267190_

### Scalability and Performance Patterns

- **Preload-head + stream-tail** for large libraries (orchestral SFZ sets easily exceed RAM); pure-RAM loading acceptable for typical SF2/SF3 and Decent Sampler libraries.
- **SF3 decode strategy**: decompress Vorbis samples to PCM at load time (RAM cost, simple RT path) rather than decoding on the audio thread — the FluidSynth approach. Streaming compressed audio would put a decoder in the RT path; avoid for v1.
- **Polyphony budgeting**: per-group polyphony (SFZ `polyphony`/`note_polyphony`, DS group tags) plus a global cap with priority stealing.
- **SIMD** for resampling/mixing inner loops (sfizz uses dedicated SIMD helpers).
_Source: https://deepwiki.com/sfztools/sfizz, https://github.com/FluidSynth/fluidsynth/wiki/SoundFont3Format_

### Data Architecture Patterns

The unified instrument model needs: regions (key/velocity/CC/round-robin conditions), sample references (file + offset/loop points + root key + tuning), per-region playback params (envelopes, filters, EQ, pan), a modulation matrix, and format-specific extension slots (SF2 preset/instrument two-level hierarchy; DS UI/effects description). SF2's two-level preset→instrument→zone hierarchy flattens into regions with inherited generators — a well-understood lowering documented in SF2 implementation guides.
_Source: https://basicsynth.com/uploads/SF2-DLS.pdf, http://www.synthfont.com/sfspec24.pdf_

### Deployment and Operations Architecture

Ship as a JUCE plugin target (VST3/AU/LV2) plus an in-app engine module for feedBack; share the engine core as a static library between both. CI validation with pluginval and format-conformance corpora (Pianobook DS libraries, GeneralUser GS soundfont, sfz test suites).
_Confidence: High — patterns corroborated across sfizz internals, FluidSynth, and RT-audio canon._

## Implementation Approaches and Technology Adoption

### Technology Adoption Strategies

The full-fidelity goal reframes "build vs. integrate": even the best open SFZ engine is not full-spec. sfizz's own support table reports roughly **SFZ v1 ~96%, SFZ v2 ~44%, ARIA ~45%** coverage (figures vary somewhat across snapshots). Practical adoption path: **start from sfizz (BSD-2) as the SFZ/voice-engine foundation** — fork or embed — and (a) extend its opcode coverage toward v2/ARIA, (b) add first-class SF2/SF3 and Decent Sampler front-ends that lower into its region/modulation model, using FluidSynth as the behavioral reference for SF2 modulator semantics. This is a gradual-adoption strategy: ship with sfizz's current fidelity, then close the gap release by release, measured against the public opcode table.
_Source: https://sfz.tools/sfizz/development/status/opcodes/, https://github.com/sfztools/sfizz_

### Development Workflows and Tooling

CMake + JUCE 8 is the standard toolchain; the pamplejuce template documents the reference setup (JUCE 8, Catch2 v3 via FetchContent, pluginval, GitHub Actions, signing/notarization). pluginval can be added as a CMake target for local debugging with good stack traces; full pluginval CI runs take ~3–9 min per platform on warm caches.
_Source: https://github.com/sudara/pamplejuce, https://github.com/Tracktion/pluginval, https://thewolfsound.com/how-to-build-audio-plugin-with-juce-cpp-framework-cmake-and-unit-tests/_

### Testing and Quality Assurance

- **Plugin conformance**: pluginval at strictness ≥5 (host-compatibility floor) up to 10 (parameter fuzzing, state-restoration cycles) in CI.
- **Format conformance**: golden-file tests — parse a library, snapshot the normalized region model; per-format corpora (Pianobook .dspreset libraries, GeneralUser GS SF2, sfz test suites).
- **Rendering regression**: offline-render known MIDI against known libraries and diff audio against references (the approach sfizz uses to match Sforzando's output levels).
- **RT-safety**: assert no allocations/locks on the audio thread (e.g., wrap `processBlock` in allocation detectors in debug builds).
_Source: https://github.com/Tracktion/pluginval, https://sfz.tools/sfizz/development/status/_

### Deployment and Operations Practices

Engine core as a static library consumed by (1) the JUCE plugin targets and (2) feedBack's in-app engine — mirroring the sfizz/sfizz-ui split. AGPL-3.0 obligations: publish source of the combined work; BSD-2 (sfizz), LGPL-2.1 (FluidSynth, if used), and libvorbis/stb_vorbis all compose cleanly into an AGPLv3 distribution.
_Source: https://github.com/sfztools/sfizz-ui, https://github.com/juce-framework/JUCE/blob/master/LICENSE.md_

### Risk Assessment and Mitigation

| Risk | Severity | Mitigation |
|---|---|---|
| SFZ v2/ARIA full fidelity is a long tail (~55% of v2/ARIA opcodes unimplemented in sfizz) | High | Scope fidelity by *library corpus coverage*, not raw opcode count; prioritize opcodes used by real libraries |
| SF2 modulator model mismatch with SFZ-style matrix | Medium | Generic modulation matrix designed up front; FluidSynth as behavioral oracle in rendering regression tests |
| Decent Sampler is a moving, vendor-controlled format (frequent releases; UI/effects semantics only prose-documented) | Medium | Track the developer guide per release; treat UI layer as best-effort, audio layer as conformance target |
| SF3 has no official spec | Low | Follow FluidSynth wiki + sf3convert behavior; validate against MuseScore soundfonts |
| RT-safety regressions | Medium | Debug-build allocation/lock detectors + pluginval fuzzing in CI |

## Technical Research Recommendations

### Implementation Roadmap

1. **Phase 0 — Spike**: embed sfizz in a JUCE `AudioProcessor`, load an SFZ + a .dspreset (its experimental path), render in feedBack's engine.
2. **Phase 1 — Unified model**: define the internal region/modulation model (SFZ-superset); build the Decent Sampler front-end (easiest, XML) against it.
3. **Phase 2 — SF2/SF3 front-end**: RIFF hydra parser + Vorbis decode-at-load; rendering regression vs. FluidSynth.
4. **Phase 3 — Fidelity climb**: extend v2/ARIA opcode coverage guided by corpus analysis; DS UI/effects layer.

### Technology Stack Recommendations

C++17/20 · JUCE 8 (AGPLv3) · sfizz (BSD-2) as engine foundation · FluidSynth (LGPL-2.1) as SF2 behavioral reference (embed only if a dedicated SF2 voice path proves necessary) · stb_vorbis or libvorbis for SF3 · CMake + Catch2 + pluginval + GitHub Actions.

### Skill Development Requirements

Real-time C++ audio (lock-free patterns), DSP fundamentals (resampling, envelopes, filters), RIFF binary parsing, SFZ opcode semantics, JUCE plugin architecture.

### Success Metrics and KPIs

- % of a defined real-world library corpus that loads and renders correctly per format (primary fidelity metric)
- sfizz-style opcode support table published per release
- pluginval strictness-10 pass on all targets/platforms
- Zero audio-thread allocations/locks (automated check)
- Rendering-diff RMS vs. reference engines under threshold for the regression corpus

## Research Synthesis

### Executive Summary

Building a universal VSTi sampler for feedBack is technically well-grounded and licensing-clean. The AGPL-3.0 constraint is fully satisfied: JUCE 8 (AGPLv3 track), sfizz (BSD-2-Clause), FluidSynth (LGPL-2.1), and libvorbis/stb_vorbis all compose into an AGPLv3 distribution at zero licensing cost.

The central architectural finding is that all four target formats can be lowered into a single **SFZ-superset region/voice/modulation-matrix model** — the design sfizz already validates internally, including its experimental .dspreset import. Decent Sampler is the easiest front-end (well-documented XML with a community XSD); SF2 is fully specified (RIFF hydra, 58 generators plus a modulator model, with mandated structural validation) and FluidSynth serves as the behavioral oracle; SF3 is SF2 with Vorbis-compressed samples, best handled by decoding to PCM at load time.

The honest caveat on "full-spec fidelity": the best open engine reaches ~96% SFZ v1 coverage but only ~44–45% of SFZ v2/ARIA opcodes. Full fidelity is therefore a multi-release long tail. The recommended strategy is to build on sfizz as the engine foundation, add SF2/SF3 and Decent Sampler front-ends that lower into its model, and measure fidelity by real-world library corpus coverage rather than raw opcode count.

**Key Technical Findings:**

- Licensing: every candidate component is AGPL-3.0-compatible — no blockers, no fees
- Architecture: layered pipeline (parsers → unified region model → voice engine → plugin shell); the generic modulation matrix is the unification point for SFZ EGs/LFOs, SF2 modulators, and DS bindings
- Hardest lowering: SF2's modulator matrix does not map 1:1 to SFZ opcodes; may require a dedicated SF2 voice path
- Real-time canon: no locks/allocation/IO on the audio thread; preload-head + stream-tail for large libraries; SF3 Vorbis decode at load, never on the audio thread

**Technical Recommendations:**

1. Phase-0 spike: embed sfizz in a JUCE `AudioProcessor` inside feedBack; load an SFZ and a .dspreset
2. Design the generic modulation matrix up front — retrofitting it is the expensive mistake
3. Build the Decent Sampler front-end first (easiest), then SF2/SF3, then climb the SFZ v2/ARIA tail guided by corpus analysis
4. Golden-file model tests + rendering-regression vs. FluidSynth/Sforzando; pluginval strictness-10 and RT-safety assertions in CI
5. Ship the engine core as a static library shared by the plugin targets and feedBack's in-app engine

### Table of Contents

1. Technical Research Scope Confirmation
2. Technology Stack Analysis (languages, libraries, format landscape, trends)
3. Integration Patterns Analysis (plugin surface, engine embedding, event model, containers)
4. Architectural Patterns and Design (region/voice model, modulation matrix, RT safety, scalability)
5. Implementation Approaches and Technology Adoption (strategy, tooling, testing, risks)
6. Technical Research Recommendations (roadmap, stack, skills, KPIs)
7. Research Synthesis (this section)

### Conclusion and Next Steps

The research goals — a format deep-dive at full-spec fidelity to design a unified internal sample model under AGPL-3.0 constraints — are achieved: the specs are mapped, the reference implementations identified, the unified-model architecture validated against sfizz's proven design, and the fidelity gap quantified. Next steps in the BMad flow: fold these findings into the product brief and PRD (scoping fidelity by corpus coverage), then let the architecture phase resolve the flagged open question (SF2 modulator lowering vs. dedicated SF2 voice path).

**Technical Research Completion Date:** 2026-07-17
**Source Verification:** all claims cited against current primary sources; confidence levels noted inline
**Overall Confidence:** High (specs, licensing, APIs); Medium (SF2→SFZ lowering completeness, sfizz DS coverage depth)
