---
title: feedBack-sampler — Universal VSTi Sampler PRD
status: final
created: 2026-07-17
updated: 2026-07-17
---

# feedBack-sampler — Universal VSTi Sampler PRD

## 1. Vision and Purpose

feedBack users play instruments via MIDI input, but today their playing is only audible through basic MIDI soundfonts. feedBack-sampler replaces that with a high-fidelity sampled instrument engine: a universal sampler that loads the major open sample-library formats — **Decent Sampler (.dspreset/.dslibrary), SFZ, SF2, and SF3** — so a player hears a realistic instrument, articulations included, instead of a General-MIDI approximation.

The sampler ships in two shapes from one engine:

1. **Embedded in feedBack** — the default sound of MIDI play in the app.
2. **A standalone VSTi plugin** — usable in any DAW (e.g. Reaper), released publicly under AGPL-3.0.

The public plugin is not a side effect; it is a launch. It serves the open sample-library community (Pianobook, sfz community, soundfont users) that currently juggles per-format players, and it earns feedBack credibility in that ecosystem.

[ASSUMPTION] The public plugin ships under the feedBack brand family ("feedBack Sampler" working name); no separate branding effort in v1.

## 2. Users

- **The feedBack player** — plays MIDI (keyboard or other controller) in feedBack and wants their playing to sound like a real instrument. Not an audio engineer; expects to pick a sound and play. Zero-config bias.
- **The DAW musician** — downloads the plugin for their DAW because it plays all four open formats in one instrument. Comfortable with plugin conventions (preset browsers, parameter automation). Judges the plugin against Decent Sampler, Sforzando, and soundfont players.
- **The feedBack plugin author** *(deferred to v2)* — wants to synthesize music programmatically, e.g. dynamic background tracks. v1 does not expose an author-facing API; the engine must not preclude one.

## 3. User Journeys

**UJ-1 — Lena hears her piano for the first time.** Lena, a feedBack user with a MIDI keyboard, has been practicing with the stock soundfont sound. After updating feedBack she opens instrument settings, sees a "Sampler" option with a library list, and picks a felt-piano library she dropped into her libraries folder earlier. She presses a key: velocity layers respond to her touch, the release rings naturally. She never sees the words SFZ or SF2 — just the library's name and a few controls the library exposes. She plays for an hour.

**UJ-2 — Marco consolidates his samplers in Reaper.** Marco produces in Reaper and owns a mix of Pianobook Decent Sampler libraries, sfz orchestral instruments, and an old .sf2 collection. He installs feedBack Sampler (VST3), points it at his sample folders, and the library list shows everything in one place. He loads an sfz string patch on one track and a .dslibrary pad on another, maps CC1 to dynamics, automates it, renders — and it sounds the way it does in the format's native player. He replaces two plugins in his template with one.

## 4. Features and Functional Requirements

### Group A — Format Support (the core promise)

- **FR-1** The sampler loads and plays SFZ instruments (.sfz), targeting full SFZ v1 fidelity at launch and SFZ v2/ARIA fidelity growing per release, prioritized by what real libraries use.
- **FR-2** The sampler loads and plays SoundFont SF2 files, including velocity layers, key/velocity zones, loops, envelopes, and the SF2 modulator behavior users expect from established soundfont players.
- **FR-3** The sampler loads and plays SF3 (Vorbis-compressed SoundFont) files with identical behavior to their SF2 equivalents.
- **FR-4** The sampler loads and plays Decent Sampler presets (.dspreset) and bundled libraries (.dslibrary), reproducing the library's mapping, envelopes, and audible behavior; library-defined UI controls are honored per FR-12.
- **FR-5** A library that fails to load reports a human-readable reason (missing samples, malformed file) and never crashes the host or silences other loaded state.
- **FR-6** Format-fidelity conformance is tracked against a published corpus of real-world libraries per format; each release states what it plays correctly, where "correctly" means passing the rendering-regression thresholds defined in NFR-5. [ASSUMPTION] Corpus initially: Pianobook top libraries, GeneralUser GS, sfz community test instruments.

### Group B — Library Management

- **FR-7** The user designates one or more library folders; the sampler scans them (recursively) and builds a persistent local library list across all four formats.
- **FR-8** The library list shows name, format, and size; the user can search/filter it and switch libraries without audio dropouts in the host.
- **FR-9** SF2/SF3 bank/preset structure is browsable: a soundfont's presets appear as selectable entries, not just the file.
- **FR-10** Rescanning is incremental and user-triggered (plus on-first-run); no background filesystem watching in v1.

### Group C — Playing and Sound

- **FR-11** The sampler responds to standard MIDI: note on/off with velocity, sustain and sostenuto pedals, pitch bend, modulation and other CCs that the loaded library maps, with sample-accurate timing.
- **FR-12** Library-exposed controls are presented to the user: Decent Sampler UI controls, SFZ-defined CC labels, and a generic set (volume, pan, tuning, ADSR override where the format allows) when the library defines none.
- **FR-13** Polyphony is managed automatically with a user-visible voice limit and graceful voice stealing; sustained playing on a large piano library produces no audio dropouts within the performance envelope of NFR-3.
- **FR-14** Large libraries load without blocking playback or the host UI: loading is asynchronous with progress indication, and playable-when-ready per preloaded region. [ASSUMPTION] v1 preloads sample heads and streams tails from disk for libraries beyond a RAM threshold.

### Group D — Plugin and Host Integration

- **FR-15** The sampler ships as VST3 on Windows, macOS, and Linux, and additionally AU on macOS; installers/packages per platform.
- **FR-16** The plugin passes host-compatibility validation (pluginval at high strictness) and behaves correctly on state save/restore: a DAW project reopens with the same library, preset, and control values.
- **FR-17** Plugin parameters (the generic controls plus mapped library controls) are automatable from the host.
- **FR-18** The identical engine runs embedded in feedBack: feedBack's MIDI input routes to it, and the user selects sampler vs. legacy soundfont path in feedBack's instrument settings.
- **FR-19** The embedded and standalone versions share the library list and settings storage. [ASSUMPTION] Per-user config location shared between app and plugin.
- **FR-20** The plugin UI follows feedBack's corporate design (colors, typography, iconography, control styling) so the standalone VSTi is recognizably a feedBack product and the embedded view feels native to the app. Library-defined UI elements (FR-12, e.g. Decent Sampler controls) render within this design frame rather than replacing it. [NOTE FOR PM] Point the UX phase at feedBack's design system/style-guide source.
- **FR-21** The v1 UI comprises three surfaces: the library list (FR-7/8/9), the instrument view (library-defined and generic controls, FR-12), and a settings view (folders, voice limit, audio options). [NOTE FOR PM] Detailed screen design is delegated to the bmad-ux phase; this FR fixes the inventory so "simple UI" has a testable boundary.

### Group E — Out of Scope for v1 (explicit)

- Plugin-author programmatic API (deferred to v2; engine architecture must not preclude it — see addendum).
- In-app library downloads/marketplace.
- Sample editing, custom mapping creation, or library authoring tools.
- MPE/MIDI 2.0 per-note expression (revisit in v2; VST3 note-expression has known framework caveats).

## 5. Non-Functional Requirements

- **NFR-1 Real-time safety:** no allocations, locks, or file I/O on the audio thread; verified by automated checks in CI.
- **NFR-2 Latency:** note-on to audio within the host buffer; no added internal latency beyond disk-streaming preload design.
- **NFR-3 Performance:** [ASSUMPTION] 128 voices of a typical piano library at <25% of one modern desktop core at 48 kHz/256 samples; targets refined during architecture.
- **NFR-4 Stability:** malformed or hostile library files must never crash the host; parsers are fuzz-tested.
- **NFR-5 Fidelity:** rendering-regression suite diffs output against reference engines (FluidSynth for SF2/SF3; Sforzando/sfizz for SFZ) within defined thresholds.
- **NFR-6 Licensing:** entire distribution AGPL-3.0-compliant; all embedded components license-compatible (see addendum).
- **NFR-7 Footprint:** RAM use is dominated only by user-chosen library size; fixed engine overhead is bounded. [NOTE FOR PM] Numeric footprint bounds set during architecture.

## 6. Success Metrics

- feedBack users switch: share of MIDI-input sessions using the sampler over the legacy soundfont path grows release over release.
- Corpus fidelity: % of the conformance corpus that loads and renders correctly, published per release and trending up (v1 gate: [ASSUMPTION] 100% of corpus loads, ≥90% renders correctly per format).
- External adoption: downloads and qualitative reception (DAW-community feedback, issue tracker signal) of the public plugin.
- **Counter-metrics:** support burden (crash reports per install must stay near zero — stability beats coverage); feedBack app size/complexity (embedded engine must not degrade app startup or install size materially); fidelity-chasing must not stall releases (long-tail opcodes land incrementally, never block).

## 7. Release Shape

- **v1 (this PRD):** the four formats at launch-grade fidelity, library list UI, three platforms, embedded + standalone.
- **v2 (signaled, not specified):** plugin-author API, MPE/note expression, deeper SFZ v2/ARIA coverage, possible curated library discovery.

## 8. Open Questions

- OQ-1: Windows-only first public beta, or hold launch for all three platforms? (Solo dev; macOS signing/notarization is real work.)
- OQ-2: Name/branding of the public plugin ("feedBack Sampler" placeholder).
- OQ-3: The sampler is intended to replace feedBack's legacy soundfont path once stable (the switch-rate metric in §6 assumes this); open is only the sunset timing and any fallback obligations.

## 9. Glossary

- **Library** — a loadable sample instrument in any supported format: an .sfz file with its samples, an SF2/SF3 soundfont file, or a Decent Sampler .dspreset/.dslibrary.
- **Soundfont preset** — a bank/program entry *inside* an SF2/SF3 file (FR-9); one soundfont contains many.
- **Plugin state** — the host-saved configuration of the plugin (selected library, soundfont preset, control values; FR-16). Distinct from *soundfont preset*.
- **Library-defined controls** — parameters a library exposes (Decent Sampler UI controls, SFZ CC labels); rendered inside the feedBack design frame (FR-12, FR-20).
- **Generic controls** — the sampler's own always-available parameters (volume, pan, tuning, ADSR override, voice limit).
- **Corpus** — the published set of real-world libraries used to measure fidelity (FR-6, NFR-5).

## 10. Assumptions Index

- A1 (§1): public plugin ships under feedBack brand family, working name "feedBack Sampler" — see OQ-2.
- A2 (FR-6): initial corpus = Pianobook top libraries, GeneralUser GS, sfz community test instruments.
- A3 (FR-14): preload-head + disk-stream-tail for libraries beyond a RAM threshold.
- A4 (FR-19): per-user config location shared between app and plugin.
- A5 (NFR-3): 128 voices of a typical piano library at <25% of one modern desktop core at 48 kHz/256 samples.
- A6 (§6): v1 fidelity gate — 100% of corpus loads, ≥90% renders correctly per format.
