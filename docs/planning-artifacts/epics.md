---
stepsCompleted: [1, 2, 3, 4]
inputDocuments:
  - docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md
  - docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md
  - docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md
  - docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/SOLUTION-DESIGN.md
  - docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md
  - docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/EXPERIENCE.md
---

# feedBack-sampler - Epic Breakdown

## Overview

This document provides the complete epic and story breakdown for feedBack-sampler, decomposing the requirements from the PRD, UX Design if it exists, and Architecture requirements into implementable stories.

## Requirements Inventory

### Functional Requirements

FR1: The sampler loads and plays SFZ instruments (.sfz), targeting full SFZ v1 fidelity at launch; SFZ v2/ARIA fidelity grows per release, prioritized by real library usage.
FR2: The sampler loads and plays SF2 SoundFont files: velocity layers, key/velocity zones, loops, envelopes, and expected SF2 modulator behavior.
FR3: The sampler loads and plays SF3 (Vorbis-compressed SoundFont) files with behavior identical to their SF2 equivalents.
FR4: The sampler loads and plays Decent Sampler presets (.dspreset) and bundled libraries (.dslibrary), reproducing mapping, envelopes, and audible behavior; library-defined UI controls honored per FR12.
FR5: A library that fails to load reports a human-readable reason and never crashes the host or silences other loaded state.
FR6: Format-fidelity conformance is tracked against a published corpus of real-world libraries per format; each release states what it plays correctly per NFR5 thresholds.
FR7: The user designates one or more library folders; the sampler scans them recursively and builds a persistent local library list across all four formats.
FR8: The library list shows name, format, and size; searchable/filterable; switching libraries causes no audio dropouts in the host.
FR9: SF2/SF3 bank/preset structure is browsable: a soundfont's presets appear as selectable entries.
FR10: Rescanning is incremental and user-triggered (plus on-first-run); no background filesystem watching in v1.
FR11: The sampler responds to standard MIDI: note on/off with velocity, sustain and sostenuto pedals, pitch bend, modulation and mapped CCs, with sample-accurate timing.
FR12: Library-exposed controls are presented: Decent Sampler UI controls, SFZ CC labels, and a generic set (volume, pan, tuning, ADSR override) when the library defines none.
FR13: Polyphony is managed automatically with a user-visible voice limit and graceful voice stealing; sustained playing on a large piano library produces no dropouts within NFR3.
FR14: Large libraries load without blocking playback or host UI: asynchronous loading with progress, playable-when-ready per preloaded region; preload heads + stream tails beyond a RAM threshold.
FR15: The sampler ships as VST3 on Windows, macOS, and Linux, plus AU on macOS; installers/packages per platform.
FR16: The plugin passes pluginval at high strictness and restores state correctly: a DAW project reopens with the same library, preset, and control values.
FR17: Plugin parameters (generic controls plus mapped library controls) are automatable from the host.
FR18: The identical engine runs inside feedBack: feedBack's MIDI routes to it, and the user selects sampler vs. legacy soundfont path in instrument settings (via feedBack's VST host per AD-5).
FR19: The embedded and standalone versions share the library list and settings storage (per-user config store).
FR20: The plugin UI follows feedBack's corporate design (v3 tokens); library-defined UI elements render within this design frame.
FR21: The v1 UI comprises three surfaces: library list, instrument view, and settings view.

### NonFunctional Requirements

NFR1: Real-time safety — no allocations, locks, or file I/O on the audio thread; verified by automated checks in CI.
NFR2: Latency — note-on to audio within the host buffer; no added internal latency beyond disk-streaming preload design.
NFR3: Performance — 128 voices of a typical piano library at <25% of one modern desktop core at 48 kHz/256 samples (validated on pinned reference hardware; CI regression-relative).
NFR4: Stability — malformed or hostile library files never crash the host; parsers are fuzz-tested.
NFR5: Fidelity — rendering-regression suite diffs output against reference engines (FluidSynth for SF2/SF3; Sforzando/sfizz for SFZ) within defined thresholds.
NFR6: Licensing — entire distribution AGPL-3.0-compliant; all embedded components license-compatible.
NFR7: Footprint — RAM dominated only by user-chosen library size; fixed engine overhead ≤150 MB beyond the sample pool.

### Additional Requirements

- AD-1: One unified voice engine; all four formats lower at load time into the single SFZ-superset canonical model; mod matrix extended with SF2 primitives; FluidSynth is test-oracle only.
- AD-11: The canonical model is a versioned, written contract: fixed units, explicit defaults, schema version, shared model-validation suite every frontend must pass; accessibility names survive lowering.
- AD-2: One sample pool owns all sample memory/disk I/O; preload heads + stream tails; SF3 decoded to PCM at load; refcounted entries pinned by live engine snapshots; nothing allocates/locks/faults on the audio thread.
- AD-3: Three-tier state ownership — APVTS for automatable controls; structural changes via command path + immutable snapshot swap (old snapshot audible until new is playable-ready); shared config owned by core's config service; plugin state chunk is schema-versioned and minimal.
- AD-4: Saved library references are resolvable identities with a fixed resolution order; drag-and-dropped files register into the index; root-remap mappings persist; explicit missing-library recovery state.
- AD-5: feedBack integrates only via its existing VST host; feedBack installer bundles the plugin (standard VST3 location); public download is the identical binary; app requires ≥ bundled version.
- AD-6: sampler-core static lib (may use JUCE non-GUI internally; public API JUCE-free); shell depends on core, never the reverse; enforced via separate CMake targets.
- AD-7: Binding performance budget asserted by benchmark: absolutes on pinned reference machine per release, regression-relative in CI; library switch ≤2 s indexed; DSP inherits sfizz SIMD.
- AD-8: Stable control IDs minted at lowering; fixed pool of 128 proxy parameters; state/automation re-associate by control ID; generic controls are fixed parameters.
- AD-9: Global config = versioned JSON (settings.json + library-index.json) in per-user config dir; atomic temp-write+rename; generation counter + merge-on-write; single write path through core's config service.
- AD-10: Test infrastructure is architecture: golden-file lowering snapshots per format; offline corpus rendering regression vs oracles; per-frontend fuzzers; debug RT-safety detectors; CI builds/tests all three platforms every change; pluginval ≥5 floor / 10 target.
- Stack: C++17, JUCE 8.0.14, sfizz 1.2.3 vendored at pinned commit (local patches expected), CMake 3.28+, Catch2 3.x, pluginval 1.0.4, GitHub Actions pamplejuce-style matrix. No app starter template; pamplejuce is the CI/packaging reference for Epic-1-style setup.
- Repo structure seed: core/ (model, frontends, engine, pool, config), plugin/, tests/ (golden, render, fuzz, perf), corpus/, cmake/, .github/.
- Conventions: namespace fbsampler; one Diagnostic error shape (severity+code+message+location, no exceptions across core API); injected log sink, never on audio thread; schema version on every persisted artifact; float32 audio, time in samples, MIDI 1.0 at the boundary.
- Corpus manifest (Pianobook top libraries, GeneralUser GS, sfz community instruments) is a maintained artifact; corpus weighted by real-library usage, tracked per-library not per-opcode.

### UX Design Requirements

UX-DR1: Implement the feedBack v3 design-token set (DESIGN.md frontmatter: colors, Rubik typography scale, radii, 4px spacing, flat surface-step elevation) as the plugin's JUCE LookAndFeel — no colors outside the token set; no gamification visuals; gold only in SF2/SF3 badge.
UX-DR2: Header bar (48px): [dB] wordmark, loaded-library name, format badge, voice-count readout (tabular figures), browser toggle (left), settings gear (right).
UX-DR3: Component set styled to tokens: primary/secondary buttons, ring-style rotary knob (value arc, label, drag-vertical, Shift fine-adjust, double-click reset, scroll steps, value tooltip pill), library list row (hover, selected left-accent, status dot), format badge pills (SFZ primary-blue, SF2/SF3 gold, DS green — 20% fills), 2px progress bar, 6px scrollbars.
UX-DR4: Instrument view (home): DS artwork/controls unmodified inside the inset artwork frame (frame carries feedBack identity); generated control cards for SFZ CC labels and generic volume/pan/tuning/ADSR when library defines no UI; SF2/SF3 soundfont-preset selector row (bank/program dropdown with search, instant switch).
UX-DR5: Library browser: sidebar pane, virtualized list, live type-to-search, format filter pills, Enter loads, double-click loads, arrows navigate; collapses to overlay below 900px width.
UX-DR6: Settings overlay: library folders add/remove/rescan, voice limit, RAM/streaming threshold, about/licenses; scrim black 60% + hairline border.
UX-DR7: State patterns: empty state ("Add a library folder to get started." + primary button); non-blocking scan banner with progress + current path, list populates incrementally; per-row load progress + header spinner, playable-when-ready; load-failure row status dot + human-readable reason, previous library kept; standalone no-MIDI hint after 10 s, dismissible; silent host-state restore with "Locate library…" recovery card when missing (root-remap remembered for all libraries under the old root).
UX-DR8: Crossfade-on-ready library switching: loading a new library never interrupts audio of the current one until the new one is playable.
UX-DR9: MIDI-learn on every control via right-click → "Learn CC" (v1, cut only if scope presses).
UX-DR10: Drag-and-drop: dropping a library file or folder anywhere on the window opens/adds it (registers into the library index per AD-4).
UX-DR11: Accessibility floor: full keyboard operability of all three surfaces (tab traversal, arrows, Enter, Esc); 2px focus ring everywhere; WCAG AA contrast on designated token pairs; status colors never sole signal; JUCE screen-reader labels on all controls with knob values announced; artwork controls keep accessible names from .dspreset; text resizes with window.
UX-DR12: Window behavior: freely resizable, min 720×480 logical px, reflow at 900px; HiDPI vector chrome; smooth artwork interpolation; macOS AU host-resize conventions.
UX-DR13: Voice & tone: studio-precise error copy naming problem + fix (never "parse error"); no exclamation marks; all error text/paths/version strings selectable.

### FR Coverage Map

FR1: Epic 1 - SFZ loading/playback through the canonical model
FR2: Epic 2 - SF2 lowering incl. modulator behavior
FR3: Epic 2 - SF3 Vorbis decode, identical to SF2
FR4: Epic 4 - Decent Sampler frontend + artwork UI
FR5: Epic 3 - human-readable load errors (frontend diagnostics seeded in Epic 1)
FR6: Epic 1 (corpus established, tracked per format epic) / Epic 7 (published scoreboard)
FR7: Epic 3 - library folders + recursive scan + persistent index
FR8: Epic 3 - browsable/searchable list, dropout-free switching (crossfade hardened in Epic 5)
FR9: Epic 2 - soundfont bank/preset browsing
FR10: Epic 3 - incremental user-triggered rescan
FR11: Epic 1 - full MIDI response with sample-accurate timing
FR12: Epic 3 - generated/generic controls; Epic 4 - DS-defined controls
FR13: Epic 5 - voice limit, stealing, sustained-load robustness
FR14: Epic 5 - async loading, playable-when-ready, streaming
FR15: Epic 1 - VST3 builds on all three platforms; Epic 7 - AU + installers/packages
FR16: Epic 6 - pluginval + state save/restore
FR17: Epic 6 - host automation via proxy-parameter pool
FR18: OUT OF SCOPE for this plan (user decision: standalone VSTi first; feedBack integration is a future effort — AD-5 guarantees it needs only standard VST3 hosting)
FR19: Epic 3 - shared config store; Epic 7 - two-concurrent-instance verification
FR20: Epic 3 - v3 design tokens/LookAndFeel (artwork frame in Epic 4)
FR21: Epic 3 - three UI surfaces

## Epic List

### Epic 1: First Sound — play an SFZ instrument in a DAW
The walking skeleton that proves the architecture: repo + three-platform CI, vendored sfizz behind the canonical model v0, VST3 plugin shell with a deliberately minimal UI, MIDI in → audio out. Ships the corpus manifest and *seed* harnesses (golden/render/fuzz — minimal fixtures, not the full suite), and the sample-pool interface with a simple all-RAM implementation behind it (AD-2 contract fixed so no frontend ever assumes RAM residency; Epic 5 swaps internals only).
**FRs covered:** FR1, FR11 (+ FR15 partial: VST3 targets build on all platforms; FR6 partial: corpus manifest established)

### Epic 2: SoundFonts — SF2/SF3 play like the players users know
SF2 frontend lowering into the extended mod matrix, SF3 Vorbis decode at load, bank/preset browsing with instant preset switching. Corpus slice (GeneralUser GS et al.) green in the render harness before the epic closes.
**FRs covered:** FR2, FR3, FR9

### Epic 3: The Instrument — library management and the real UI
The v3-token LookAndFeel and all three surfaces: library browser, instrument view with generated controls, settings overlay. Folder scan + persistent shared library index (config service), search/filter, human-readable error states, drag-and-drop, accessibility floor, voice & tone. After this it feels like a product — and the UI exists before Decent Sampler artwork needs a frame.
**FRs covered:** FR5, FR7, FR8, FR10, FR12 (generated/generic), FR19, FR20, FR21

### Epic 4: Decent Sampler — Pianobook libraries with their own faces
.dspreset/.dslibrary frontend, DS-defined controls honored, artwork rendered unmodified inside the feedBack artwork frame. Corpus slice (Pianobook top libraries) green before close.
**FRs covered:** FR4 (+ FR12 partial: DS-defined controls)

### Epic 5: Big Libraries, Heavy Hands — streaming and performance
Sample-pool streaming internals (preload heads/stream tails, refcounted snapshot pinning) behind the Epic-1 interface, async playable-when-ready loading, crossfade-on-ready switching, voice management + stealing, perf benchmark enforcing AD-7 budgets.
**FRs covered:** FR13, FR14 (+ NFR2/3/7 enforcement, UX-DR8)

### Epic 6: Trustworthy in Projects — state, automation, resilience
pluginval-clean behavior, schema-versioned state chunks, library-identity resolution + missing-library recovery + root remap, 128-proxy pool with stable control IDs, host automation, MIDI-learn. May run before Epic 5 if a beta needs project-safety first.
**FRs covered:** FR16, FR17 (+ AD-4/AD-8, UX-DR9)

### Epic 7: Launch — fidelity scoreboard, feedBack integration, distribution
Published per-release fidelity results from the corpus that has run since Epic 1, AU target, installers/packages, AGPL license audit. Includes an explicit two-concurrent-host-instance test proving the shared library list (FR19). feedBack integration (FR18) is out of scope for this plan — standalone VSTi first.
**FRs covered:** FR6, FR15, FR19 (verification) (+ NFR5 publication, NFR6)

## Epic 1: First Sound — play an SFZ instrument in a DAW

The walking skeleton that proves the architecture: repo + three-platform CI, vendored sfizz behind the canonical model v0, VST3 plugin shell with a deliberately minimal UI, MIDI in → audio out, corpus manifest and seed harnesses, sample-pool interface with all-RAM implementation.

### Story 1.1: Buildable skeleton on three platforms

As a developer (solo maintainer),
I want the repo skeleton with core/shell CMake targets, vendored sfizz, and a three-platform CI matrix,
So that every subsequent story lands on green, verifiable infrastructure.

**Acceptance Criteria:**

**Given** a fresh clone on Windows, macOS, or Linux
**When** the documented CMake build runs
**Then** `sampler-core` (static lib) and an empty VST3 plugin target compile and link, with sfizz vendored at a pinned commit
**And** core links no JUCE GUI module (build fails if a GUI dependency is introduced)

**Given** a push to the repository
**When** GitHub Actions runs
**Then** all three platforms build and run a trivial Catch2 test, and pluginval (strictness 5) passes against the empty plugin

### Story 1.2: Canonical model v0 with a written contract

As a developer,
I want the canonical region/control/mod-matrix model with its written spec and validation suite,
So that every frontend lowers into one verifiable representation (AD-11).

**Acceptance Criteria:**

**Given** the model spec document in-repo
**When** a model instance is constructed
**Then** units follow the spec (cents/dB/seconds/normalized curves), every field has the documented default, and the validation suite rejects out-of-contract instances

**Given** a valid model instance
**When** serialized to a golden-file snapshot
**Then** the dump is deterministic, schema-versioned, and byte-stable across platforms

### Story 1.3: SFZ frontend lowers real instruments

As a DAW musician,
I want .sfz instruments parsed and lowered to the canonical model,
So that my SFZ libraries are represented correctly before a note is played.

**Acceptance Criteria:**

**Given** an SFZ v1 instrument (regions, key/vel ranges, loops, envelopes, CC labels)
**When** the frontend lowers it
**Then** the resulting model passes validation and its golden snapshot matches the reference

**Given** a malformed or truncated .sfz file
**When** lowering is attempted
**Then** structured Diagnostics (severity, code, message, location) are returned, no exception crosses the core API, and the seed fuzzer runs clean in CI

### Story 1.4: The engine makes sound

As a feedBack player,
I want the unified voice engine rendering the canonical model through the sample-pool interface,
So that a lowered SFZ instrument is audible.

**Acceptance Criteria:**

**Given** a lowered instrument and the all-RAM pool implementation behind the AD-2 interface
**When** note-on/off events render offline
**Then** velocity layers, loops, and envelopes sound per the model, and output matches a seed render-regression fixture

**Given** a debug build with RT-safety detectors
**When** the render callback executes
**Then** zero allocations, locks, or file I/O are detected on the audio thread

### Story 1.5: Play it live in a DAW

As a DAW musician,
I want the VST3 plugin to load an .sfz and respond to my MIDI keyboard,
So that I can play an SFZ instrument in Reaper today.

**Acceptance Criteria:**

**Given** the plugin loaded in a VST3 host
**When** I load an .sfz via the minimal UI (file chooser + status text) and play
**Then** notes sound with velocity, sustain and sostenuto pedals, pitch bend, and mapped CCs, with sample-accurate timing within the host buffer (NFR-2)

**Given** a library that fails to load
**When** loading completes
**Then** the UI shows the human-readable Diagnostic and the host keeps running (FR-5 seed)

### Story 1.6: The corpus starts measuring

As the maintainer,
I want the corpus manifest and offline render harness wired into CI,
So that fidelity is measured from the first release, not retrofitted.

**Acceptance Criteria:**

**Given** the corpus manifest listing initial SFZ entries (sfz community instruments)
**When** the CI harness renders fixed MIDI through each corpus entry
**Then** output diffs against checked-in reference captures (recorded once from the external oracle — Sforzando for SFZ — with the capture procedure documented in-repo) within NFR-5 thresholds and reports pass/fail per library

**Given** a change that alters rendering of a corpus entry
**When** CI runs
**Then** the regression is flagged with a per-library diff report

## Epic 2: SoundFonts — SF2/SF3 play like the players users know

SF2 frontend lowering into the extended mod matrix, SF3 Vorbis decode at load, bank/preset browsing with instant preset switching, corpus slice green vs FluidSynth.

### Story 2.1: SF2 structure lowers to the canonical model

As a DAW musician,
I want SF2 soundfonts parsed and their zones lowered to the canonical model,
So that my .sf2 collection is structurally correct in the sampler.

**Acceptance Criteria:**

**Given** a well-formed SF2 file (RIFF hydra, presets, instruments, samples)
**When** the frontend lowers a preset
**Then** key/velocity zones, sample loops, tuning, and envelope generators map to model regions passing validation, with golden snapshots matching references

**Given** a truncated or structurally invalid SF2 (per the SoundFont 2.04 mandated validation)
**When** lowering is attempted
**Then** structured Diagnostics are returned, nothing crashes, and the SF2 fuzzer runs clean in CI

### Story 2.2: SF2 modulators through the unified mod matrix

As a soundfont user,
I want SF2 modulator behavior (default modulator set, curve types, amounts) reproduced,
So that my soundfonts respond to velocity, CCs, and pitch wheel the way established players make them.

**Acceptance Criteria:**

**Given** the mod matrix extended with SF2 source/curve primitives (AD-1)
**When** a preset using default and custom modulators renders
**Then** offline output diffs against FluidSynth within NFR-5 thresholds for velocity response, mod wheel, and pitch-bend range

**Given** a modulator construct the matrix cannot yet express
**When** lowering occurs
**Then** a Diagnostic records the unsupported modulator (fidelity gap tracked, never silent)

### Story 2.3: SF3 plays identically to SF2

As a soundfont user,
I want Vorbis-compressed SF3 files to behave exactly like their SF2 equivalents,
So that my compressed soundfonts are first-class.

**Acceptance Criteria:**

**Given** an SF3 file and its SF2 equivalent
**When** both are lowered and rendered
**Then** Vorbis decodes to PCM at load time (never on the audio thread), golden snapshots are structurally identical, and renders match within thresholds

### Story 2.4: Browse and switch soundfont presets

As a DAW musician,
I want a soundfont's bank/program presets listed and instantly selectable,
So that one .sf2 file gives me all its instruments (FR9).

**Acceptance Criteria:**

**Given** a loaded multi-preset soundfont
**When** preset enumeration and selection run through the core API (exposed via a bare debug list or host parameter — no styled widget; the visible selector row is Story 3.4's)
**Then** all presets appear with bank/program numbers and names, and selecting one switches instantly without reloading the file or interrupting other notes

### Story 2.5: SoundFont corpus slice goes green

As the maintainer,
I want GeneralUser GS and the SF2/SF3 corpus entries measured in CI,
So that soundfont fidelity is proven before the epic closes.

**Acceptance Criteria:**

**Given** the corpus manifest extended with the SF2/SF3 slice
**When** the render harness runs in CI
**Then** each entry loads (100%) and renders within thresholds vs. FluidSynth (≥90% per the v1 gate), with per-library results reported

## Epic 3: The Instrument — library management and the real UI

The v3-token LookAndFeel and all three surfaces: library browser, instrument view with generated controls, settings overlay; folder scan + persistent shared library index; error states; drag-and-drop; accessibility floor.

### Story 3.1: The feedBack look

As a feedBack user,
I want the plugin styled in the v3 design language,
So that the sampler is recognizably a feedBack product (FR20).

**Acceptance Criteria:**

**Given** the DESIGN.md token set (colors, Rubik scale, radii, 4px spacing)
**When** any chrome renders
**Then** it uses only token values via a shared LookAndFeel — no out-of-set colors, flat surface-step depth, gold only in SF2/SF3 badges

**Given** the component inventory (buttons, knob, list row, badges, progress, scrollbars)
**When** each renders in a component gallery build (a throwaway dev target — not a maintained product surface)
**Then** it matches DESIGN.md specs including knob gestures (drag-vertical, Shift fine, double-click reset, scroll steps, value tooltip)

### Story 3.2: Library folders and the shared index

As a feedBack player,
I want to designate library folders that are scanned into a persistent list,
So that all my libraries — all four formats — are known in one place, shared between feedBack and any DAW (FR7, FR19).

**Acceptance Criteria:**

**Given** one or more designated folders
**When** a recursive scan runs
**Then** all recognized libraries land in `library-index.json` (per-user config dir, atomic writes, generation counter, merge-on-write per AD-9) with name, format, and size

**Given** a second plugin instance started in another host after the first has scanned
**When** it starts
**Then** it reads the same index without rescanning; a user-triggered rescan is incremental and only re-examines changed entries (FR10). (Concurrent-write safety is deliberately verified later, by Story 7.3's two-instance test.)

### Story 3.3: The library browser

As a DAW musician,
I want to search, filter, and load libraries from a browser pane,
So that my whole collection is playable from one list (FR8).

**Acceptance Criteria:**

**Given** an indexed collection
**When** I type in the search field or toggle format filter pills
**Then** the virtualized list filters live; rows show name, format badge, size; arrows navigate; Enter or double-click loads

**Given** audio playing from the current library
**When** I load another library
**Then** the current one keeps sounding until the new one is ready (command path + snapshot swap), with no host dropout

### Story 3.4: The instrument view

As a feedBack player,
I want the loaded library's controls presented in the instrument view with the header bar,
So that I see and shape my instrument without knowing formats exist (FR12, FR21).

**Acceptance Criteria:**

**Given** a loaded SFZ library with CC labels
**When** the instrument view renders
**Then** labeled controls appear in generated cards; libraries with no defined controls get the generic set (volume, pan, tuning, ADSR override); SF2/SF3 shows the preset-selector row

**Given** any loaded state
**When** the header renders
**Then** it shows wordmark, library name, format badge, live voice count (tabular figures), browser toggle, and settings gear per UX-DR2

### Story 3.5: Settings

As a DAW musician,
I want a settings overlay for folders, voice limit, and memory,
So that I configure once and play everywhere (FR21).

**Acceptance Criteria:**

**Given** the settings overlay open (scrim + hairline per DESIGN.md)
**When** I add/remove folders, trigger rescan, or adjust voice limit and RAM/streaming threshold
**Then** changes persist to the shared store via the config service and take effect without plugin reload; about/licenses (AGPL notices) are shown with selectable text

### Story 3.6: States, errors, and drag-and-drop

As a feedBack player,
I want clear states for empty, scanning, loading, and failure — and drag-and-drop loading,
So that the sampler always tells me what's happening in plain language (FR5).

**Acceptance Criteria:**

**Given** no folders configured
**When** the plugin opens
**Then** the empty state invites "Add a library folder to get started." with a primary button; scanning shows a non-blocking banner with progress and current path, list populating incrementally

**Given** a library that fails to load
**When** the list renders
**Then** the row stays listed with a status dot and a human-readable reason on hover (problem + fix phrasing, selectable text, never "parse error"); the previous instrument keeps playing

**Given** a library file or folder dropped anywhere on the window
**When** the drop lands
**Then** it registers into the library index (single-file entry per AD-4) and loads

### Story 3.7: Keyboard, screen reader, and window behavior

As a keyboard-first or screen-reader user,
I want full operability and sane resizing,
So that the accessibility floor holds (UX-DR11/12).

**Acceptance Criteria:**

**Given** any of the three surfaces
**When** I navigate by keyboard only
**Then** tab traversal reaches everything, arrows/Enter/Esc work per EXPERIENCE.md, and the 2px focus ring is always visible

**Given** a screen reader active
**When** I operate controls
**Then** every control exposes a name via the JUCE accessibility API and knob values are announced on change; status colors are never the sole signal

**Given** the window resized
**When** width crosses 900px or hits the 720×480 minimum
**Then** the browser collapses to overlay / reflow holds, chrome stays vector-crisp on HiDPI

## Epic 4: Decent Sampler — Pianobook libraries with their own faces

.dspreset/.dslibrary frontend, DS-defined controls honored, artwork rendered unmodified inside the feedBack artwork frame, Pianobook corpus slice green.

### Story 4.1: .dspreset audio behavior lowers correctly

As a feedBack player,
I want Decent Sampler presets to sound like they do in Decent Sampler,
So that Pianobook-style libraries are faithful (FR4).

**Acceptance Criteria:**

**Given** a .dspreset with sample mappings, velocity layers, round-robins, and envelopes
**When** the frontend lowers it
**Then** the model passes validation, golden snapshots match references, and audible behavior (mapping, envelopes, releases) matches the format documentation

**Given** a malformed .dspreset (broken XML, missing samples)
**When** lowering is attempted
**Then** structured Diagnostics name the problem (e.g. "3 samples missing"), nothing crashes, and the DS fuzzer runs clean in CI

### Story 4.2: .dslibrary bundles load whole

As a DAW musician,
I want bundled .dslibrary files to load directly,
So that downloaded libraries work without unpacking (FR4).

**Acceptance Criteria:**

**Given** a .dslibrary bundle
**When** it is scanned or loaded
**Then** contained presets appear in the index and load with samples resolved from inside the bundle, behaving identically to the unpacked equivalent

### Story 4.3: DS controls and artwork inside the feedBack frame

As a feedBack player,
I want the library's own knobs and artwork rendered live in the artwork frame,
So that each library keeps its face while the chrome stays feedBack (FR12, FR20).

**Acceptance Criteria:**

**Given** a .dspreset defining UI controls (knobs, sliders, images)
**When** the instrument view renders
**Then** controls appear per the library definition inside the inset artwork frame, bound to their engine targets, with artwork unmodified and smoothly interpolated; frame chrome carries preset name and badge

**Given** the accessibility floor
**When** a screen reader inspects artwork controls
**Then** each retains an accessible name from the .dspreset definition (AD-11)

### Story 4.4: Pianobook corpus slice goes green

As the maintainer,
I want the top Pianobook/DS corpus entries measured in CI,
So that DS fidelity is proven before the epic closes.

**Acceptance Criteria:**

**Given** the corpus manifest extended with the DS slice
**When** the render harness runs
**Then** each entry loads (100%) and renders within thresholds vs. checked-in Decent Sampler reference captures (recorded once from the native player, capture procedure documented in-repo; ≥90% per the v1 gate), with per-library results reported

## Epic 5: Big Libraries, Heavy Hands — streaming and performance

Sample-pool streaming internals behind the Epic-1 interface, async playable-when-ready loading, crossfade-on-ready switching, voice management + stealing, perf benchmark enforcing AD-7 budgets.

### Story 5.1: The pool streams from disk

As a feedBack player,
I want large libraries to play with heads in RAM and tails streamed,
So that multi-GB pianos fit in bounded memory (FR14, NFR7).

**Acceptance Criteria:**

**Given** a library exceeding the RAM/streaming threshold
**When** it loads behind the unchanged pool interface
**Then** sample heads preload, tails stream via a background I/O thread, and playback is gapless at the AD-7 latency budget; below-threshold libraries stay fully resident

**Given** the RT-safety detectors active
**When** streaming playback runs
**Then** the audio thread never touches disk, allocates, or locks; underruns surface as counted diagnostics, not glitches passed silently

### Story 5.2: Playable when ready

As a feedBack player,
I want to play the moment the first regions are resident,
So that a big library never makes me wait for a full load (FR14).

**Acceptance Criteria:**

**Given** an asynchronous library load in progress
**When** preloaded regions become resident
**Then** those regions are playable immediately while loading continues behind, with row progress + header spinner reflecting true progress and the host UI never blocking

### Story 5.3: Crossfade-on-ready switching

As a DAW musician,
I want library switches that never interrupt sound,
So that switching mid-session is seamless (FR8, UX-DR8).

**Acceptance Criteria:**

**Given** audio sounding from the current library
**When** I load a replacement
**Then** the old snapshot stays audible until the new one is playable-ready, the swap is atomic, and the retiring snapshot's pool entries stay pinned until its voices end (refcount to zero, then eviction)

### Story 5.4: Voices managed, never dropped

As a feedBack player,
I want automatic polyphony with a visible limit and graceful stealing,
So that heavy sustain-pedal playing stays clean (FR13).

**Acceptance Criteria:**

**Given** the user-set voice limit (settings + header voice count)
**When** demand exceeds it on a large piano library with sustain held
**Then** voices steal gracefully (inherited sfizz policy: quietest/oldest with fade) producing no dropouts within the NFR3 envelope

### Story 5.5: The budget is a test

As the maintainer,
I want the AD-7 performance budgets asserted by benchmark,
So that performance is held, not hoped for (NFR3, NFR7).

**Acceptance Criteria:**

**Given** the perf benchmark target in `tests/perf`
**When** it runs on the pinned reference machine
**Then** 128 piano voices measure ≤25% of one core at 48 kHz/256, engine overhead ≤150 MB beyond the pool, library switch ≤2 s indexed

**Given** CI on heterogeneous runners
**When** the benchmark runs per change
**Then** results compare regression-relative against the runner-class baseline and fail the build on significant regression

## Epic 6: Trustworthy in Projects — state, automation, resilience

pluginval-clean behavior, schema-versioned state chunks, library-identity resolution + missing-library recovery + root remap, 128-proxy pool with stable control IDs, host automation, MIDI-learn.

### Story 6.1: Projects reopen exactly as saved

As a DAW musician,
I want plugin state saved and restored by the host,
So that a project reopens with the same library, preset, and control values (FR16).

**Acceptance Criteria:**

**Given** a session with a loaded library, soundfont preset, and adjusted controls
**When** the host saves and later restores the project
**Then** the schema-versioned state chunk restores library reference, preset index, and parameter values silently and exactly

**Given** a state chunk from a newer plugin version
**When** an older binary loads it
**Then** it fails soft with a clear notice — never a crash or silent corruption

### Story 6.2: Libraries survive reorganized folders

As a DAW musician,
I want saved projects to find their libraries even after I move my sample folders,
So that old projects keep working (AD-4).

**Acceptance Criteria:**

**Given** a saved library reference (format + name + folder-relative path)
**When** the file has moved
**Then** resolution follows the fixed order (exact path → index lookup by format+name → forced index reload) before showing the "Library not found — Locate…" recovery card

**Given** the user locates a moved library root
**When** they confirm the new path
**Then** the old→new root mapping persists in the config store and resolves all libraries under the old root, in every instance

### Story 6.3: Library controls are host-automatable

As a DAW musician,
I want library-defined controls exposed as automatable parameters,
So that I can automate dynamics and library knobs from my DAW (FR17).

**Acceptance Criteria:**

**Given** a loaded library with defined controls
**When** the host lists parameters
**Then** controls map onto the 128-proxy pool with stable control IDs; generic controls are fixed parameters; automation writes and reads correctly at block accuracy

**Given** a library update that reorders or inserts controls
**When** a saved project reopens
**Then** automation re-associates by control ID — no lane retargets to a different control; controls beyond 128 stay UI-operable

### Story 6.4: MIDI-learn on every control

As a DAW musician,
I want right-click → "Learn CC" on any control,
So that my hardware maps in seconds (UX-DR9).

**Acceptance Criteria:**

**Given** any knob or slider (generated, generic, or DS-defined)
**When** I choose Learn CC and move a hardware controller
**Then** the CC binds, the control follows subsequent CC input, the mapping saves in plugin state, and right-click offers unlearn

### Story 6.5: Host-grade robustness certified

As the maintainer,
I want pluginval at maximum strictness plus host-lifecycle hardening,
So that the plugin behaves under every host's quirks (FR16).

**Acceptance Criteria:**

**Given** the CI pluginval gate raised to strictness 10 (target per AD-10)
**When** validation runs on all three platforms
**Then** it passes: lifecycle stress, threading checks, parameter fuzzing, state round-trips, editor open/close cycles

**Given** hostile host behavior (rapid open/close, sample-rate changes, offline render)
**When** exercised in tests
**Then** no crashes, hangs, leaks, or stuck voices

## Epic 7: Launch — fidelity scoreboard and distribution

Published per-release fidelity results, AU target, installers/packages, AGPL license audit, two-concurrent-instance shared-library verification. feedBack integration (FR18) deliberately out of scope — standalone VSTi first.

### Story 7.1: AU on macOS

As a Logic/GarageBand user,
I want the sampler as an Audio Unit,
So that macOS-native hosts get first-class support (FR15).

**Acceptance Criteria:**

**Given** the AU target built from the same shell
**When** auval and pluginval run
**Then** validation passes; state, parameters, and editor resize behave identically to VST3, honoring AU host-resize conventions

### Story 7.2: Releases people can actually install

As a DAW musician,
I want per-platform release artifacts on GitHub Releases,
So that installation is straightforward on Windows, macOS, and Linux (FR15).

**Acceptance Criteria:**

**Given** the release pipeline
**When** a tagged build runs
**Then** it publishes per-platform archives (Windows VST3, macOS VST3+AU, Linux VST3) to GitHub Releases with clear install instructions per platform, each passing a clean-machine install test into the standard plugin locations

**Given** the unsigned macOS build
**When** a user follows the documented install steps
**Then** the Gatekeeper quarantine workaround (right-click → Open / `xattr` command) is clearly explained in the release notes and README

**And** code signing / notarization (Windows cert, Apple Developer account) is explicitly deferred as an optional later upgrade to the release pipeline — no procurement blocks v1

### Story 7.3: One library list, everywhere, at once

As a user running multiple hosts together,
I want all plugin instances to share one library list safely,
So that adding a folder in one instance shows up in every other (FR19).

**Acceptance Criteria:**

**Given** two plugin instances running concurrently in separate hosts
**When** a folder is added and a rescan runs in one while the other reads/writes settings
**Then** both converge on the same index (generation counter + merge-on-write), with no lost entries, no corruption, and no crash — verified by an automated two-instance test

### Story 7.4: The scoreboard and the license audit

As the maintainer,
I want the per-release fidelity results published and the AGPL audit complete,
So that the launch claim — "plays these libraries correctly" — is documented and legally clean (FR6, NFR5, NFR6).

**Acceptance Criteria:**

**Given** the corpus results accumulated since Epic 1
**When** a release is cut
**Then** a generated conformance report (per format: % loads, % renders correctly) ships with the release notes and meets the v1 gate (100% load, ≥90% render per format)

**Given** the full dependency tree
**When** the license audit runs
**Then** every component is AGPL-3.0-compatible per the addendum matrix, notices are complete in the About view, and corresponding-source publication is verified
