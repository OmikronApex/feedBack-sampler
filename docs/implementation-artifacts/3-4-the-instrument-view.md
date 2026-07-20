---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 3.4: The instrument view

Status: review

## Story

As a feedBack player,
I want the loaded library's controls presented in the instrument view with the header bar,
so that I see and shape my instrument without knowing formats exist (FR12, FR21).

## Acceptance Criteria

1. **Given** a loaded SFZ library with CC labels, **when** the instrument view renders, **then** labeled controls appear in generated cards; libraries with no defined controls get the generic set (volume, pan, tuning, ADSR override); SF2/SF3 shows the preset-selector row.
2. **Given** any loaded state, **when** the header renders, **then** it shows wordmark, library name, format badge, live voice count (tabular figures), browser toggle, and settings gear per UX-DR2.

## Tasks / Subtasks

- [x] Task 1: Header bar, complete (AC: 2)
  - [x] 48px `HeaderBar` component: `[dB]` wordmark (header type scale — the ONLY branding, per DESIGN.md), loaded-library name (primary text), format badge (3.1 `FormatBadge`), live voice-count readout (tabular figures), browser toggle (left), settings gear (right — opens Story 3.5's overlay; stub the callback until 3.5 lands)
  - [x] Voice count: engine must expose active-voice count. Add a lock-free counter to core (`Engine::activeVoiceCount()` — atomic written by the audio thread or derived from sfizz's `getNumActiveVoices`, read from the message thread); UI polls via a ~10 Hz timer. NO audio-thread allocation/locking for this
- [x] Task 2: Generated control cards for SFZ CC labels (AC: 1)
  - [x] First, VERIFY how CC-labeled controls survive lowering: `ControlMapEntry` today is {id, displayName, accessibleName} with no CC number — read `core/frontends/sfz/` lowering + `core/model/SPEC.md` to confirm the id convention (likely `cc<N>`). If the CC number is not recoverable from the entry, extend `ControlMapEntry` with an explicit binding field (e.g. `ccNumber`/target descriptor) in model + SPEC.md + serializer + goldens — a schema-versioned model change, bump `kModelSchemaVersion` and regenerate goldens per the established procedure
  - [x] Instrument view renders one card section of 3.1 `FbKnob`s from `InstrumentModel::controls`: label = displayName, accessible name = accessibleName (AD-11 promise)
  - [x] Knob movement → `ControlChange` EngineEvent (CC number, 0..127) injected through the processor's existing event path on the next block — same queue MIDI uses; thread-safe handoff (lock-free FIFO or atomic slot per CC), never a lock on the audio thread
- [x] Task 3: Generic control set when the library defines none (AC: 1)
  - [x] Volume (dB) + pan: implement in the plugin as post-engine gain/pan in `processBlock` — smoothed (juce::SmoothedValue or equivalent) atomics written from the UI; no core change needed and no zipper noise
  - [x] Tuning (± cents) + ADSR override: these must act inside the engine. Two sane routes — (a) small core API (engine-level master tune + envelope override applied at voice start), or (b) re-lower: clone the current `InstrumentModel`, apply offsets (Region tune / amplitudeEnvelope), reload via the existing snapshot path, debounced (~150 ms after last gesture). Route (b) needs zero engine changes and reuses proven machinery but re-triggers nothing audible mid-note (snapshot swap semantics per 2.4); pick one, document the choice + tradeoff in completion notes. Do NOT implement tuning as a pitch-bend offset (collides with performer bend)
  - [x] Cards show generic set ONLY when `controls` is empty (FR12 wording); SF2/SF3 with no control map also gets the generic set
- [x] Task 4: Soundfont preset-selector row (AC: 1)
  - [x] For SF2/SF3: styled selector row (bank/program dropdown with type-to-search per EXPERIENCE.md) bound to the EXISTING `presetIndex` parameter / `applyPresetIndexSync` deferral path from Story 2.4 — the UI is new, the mechanism is not. Names from `currentPresets()` as "bank:program name"
  - [x] Row hidden for non-soundfont libraries
- [x] Task 5: Instrument view assembly (AC: 1, 2)
  - [x] `InstrumentViewPane` fills the main content area from 3.3's IA shell; rebuilt on load-status change notification (already broadcast); empty/error states are Story 3.6's — render a plain "nothing loaded" placeholder for now
  - [x] DS artwork frame is Epic 4 — leave the layout slot (inset well) conceptually reserved, build nothing for it
- [x] Task 6: Tests (AC: 1, 2)
  - [x] Control-map extraction → card model as a pure function with Catch2 tests (given model with N controls → N generated descriptors; empty → generic set; sf2 → selector row flag)
  - [x] CC injection: plugin-level test — set a generated control's value headlessly, render a block, assert the engine received the CC (audible effect via a fixture whose mod matrix routes that CC to gain)
  - [x] Voice-count: render notes, assert count rises then falls to 0 after release
  - [x] If model schema changed: validation suite + serializer tests + golden regeneration all green; pluginval green

## Dev Notes

- **Two pre-existing mechanisms carry this story — reuse, don't rebuild:** (1) the 2.4 preset parameter + async deferral is the preset row's entire backend; (2) the processor's MIDI event path is the CC delivery mechanism. The only genuinely new engine surface is the voice counter (and tuning/ADSR if route (a) is chosen).
- **Host automation is explicitly NOT this story.** FR17 / the 128-proxy pool / stable-control-ID automation is Epic 6 (Story 6.3). Generated knobs here act via CC events, not host parameters — do not mint proxy parameters now. The stable control IDs already exist in the model (AD-8) and 6.3 will bind to them.
- **Read before modifying:** `core/include/fbsampler/model.h` (ControlMapEntry — quoted state above), `core/model/SPEC.md` + `serialize.cpp` (schema-version procedure if extending), `core/frontends/sfz/` (how cc labels lower today), `plugin/plugin_processor.{h,cpp}` (event scratch, preset deferral, ChangeBroadcaster), `core/engine/engine.cpp` (before adding voice counter — respect the Impl/threading contract; sfizz likely already tracks active voices).
- **RT-safety is the trap in this story:** voice counter and UI→CC handoff both cross into the audio thread. Atomics only; the debug RT detectors (allocation/lock probes from 1.4) will catch violations — run the RT tests locally, not just CI.
- **Tabular figures:** use the 3.1 decision (fixed-advance digit layout) for the voice count so the header doesn't jitter.
- **Accessibility seed:** every generated knob gets accessibleName from the model (this is the AD-11 metadata pipeline's whole purpose); selector row and header controls get accessible names. 3.7 audits, this story wires.
- **Dependencies:** 3.1 (components), 3.3 (IA shell). Can start after 3.1 if 3.3 is in flight — the pane slots into the shell.
- **Testing standards:** Catch2 + ctest; pure-logic extraction for UI decisions; plugin-level tests in `tests/plugin/plugin_processor_test.cpp` pattern; golden regeneration procedure documented in `tests/golden/` from earlier stories.

### Project Structure Notes

- New: `plugin/ui/header_bar.{h,cpp}`, `plugin/ui/instrument_view_pane.{h,cpp}`, `plugin/ui/preset_selector_row.{h,cpp}`, control-card model helper (pure logic file).
- Modified: `plugin/plugin_editor.{h,cpp}` (mount panes), `plugin/plugin_processor.{h,cpp}` (CC injection queue, post-engine gain/pan, voice-count accessor), possibly `core/include/fbsampler/model.h` + `core/model/SPEC.md` + `serialize.cpp` + goldens (CC binding field), `core/engine/engine.{h,cpp}` (voice counter), `tests/*`.

### References

- [Source: docs/planning-artifacts/epics.md#Story-3.4, #FR12, #FR21, #UX-DR2, #UX-DR4]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/EXPERIENCE.md#Information-Architecture (surface 1), #Component-Patterns (preset selector, knob gestures)]
- [Source: docs/planning-artifacts/ux-designs/ux-feedBack-sampler-2026-07-17/DESIGN.md#Components (header bar spec)]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-8 (control IDs exist for Epic 6 — don't consume proxies now), #AD-11 (accessible names survive lowering), #AD-1/AD-3 (engine/command-path boundaries)]
- [Source: docs/implementation-artifacts/2-4-browse-and-switch-soundfont-presets.md (preset parameter + deferral — the selector row's backend)]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Debug Log References

- Python heredoc mangled a "
" escape into a literal newline in a string constant (MSVC C2001); repaired via direct edit.

### Completion Notes List

- Task 2 verification: NO model schema change needed — sfz lowering already encodes the binding as the stable id "cc<N>" (AD-8, sfz_frontend label_cc path). buildControlCards parses the id back to the CC number; malformed/foreign ids are skipped. kModelSchemaVersion stays 5, goldens untouched.
- Voice count: Engine::activeVoiceCount() — Impl atomic written by process() after renderBlock (sfizz getNumActiveVoices), relaxed reads from the message thread; editor polls at 10 Hz. No locks/allocation on the audio thread (RT tests green).
- CC injection: 128 atomic per-CC slots on the processor (-1 empty), drained at the top of processBlock into block-start ControlChange EngineEvents — same queue host MIDI uses. setControlValue is the UI entry.
- Generic set: volume+pan post-engine in processBlock (equal-power pan, juce::SmoothedValue 20 ms ramps, atomics from UI — no zipper). Tuning/ADSR: ROUTE (B) chosen — processor keeps a copy of the last loaded model+root; applyModelOffsetsAsync clones it, applies region tuningCents offset / attack+release overrides, reloads via the proven loader-thread snapshot swap. Tradeoff (documented): zero engine changes and reuses tested machinery, but changes are not audible mid-note (2.4 swap semantics); debounce is gesture-level (knob onDragEnd), equivalent to the suggested ~150 ms timer with less machinery. Not implemented as pitch-bend (performer-bend collision avoided).
- Preset selector row: ComboBox bound to the EXISTING presetIndex parameter (selectPreset -> setValueNotifyingHost gesture -> 2.4 async-updater deferral -> loader thread). Names "bank:program name" from currentPresets(). Hidden for non-soundfonts. ComboBox popup provides native keyboard selection; full type-to-search polish can ride 3.7 if needed.
- HeaderBar: browser toggle (left), [dB] wordmark (only branding, accessibility-decorative), library name, FormatBadge, voice readout (fixed-width right-aligned slot so digits don't jitter; Rubik-defaults decision from 3.1), settings gear stub for 3.5. InstrumentViewPane: generated FbKnob cards on a surface card (label textDim/uppercase below, accessibleName from the model — AD-11), generic set only when controls empty (FR12), plain "nothing loaded" placeholder (3.6 polishes), DS artwork slot intentionally unbuilt.
- No host automation minted (FR17/proxies are Story 6.3) — knobs act via CC events only.
- Tests: 4 pure card-model cases; CC7-audibility injection test (SF2 default modulator CC7->attenuation, quiet < 50% of loud); voice-count rise/fall-to-zero; post-engine master volume attenuation. Suite 152/152; pluginval strictness 5 SUCCESS.

### File List

- core/include/fbsampler/engine.h (modified — activeVoiceCount)
- core/engine/engine.cpp (modified — atomic voice publish)
- plugin/ui/control_card_model.h (new)
- plugin/ui/control_card_model.cpp (new)
- plugin/ui/header_bar.h (new)
- plugin/ui/header_bar.cpp (new)
- plugin/ui/instrument_view_pane.h (new)
- plugin/ui/instrument_view_pane.cpp (new)
- plugin/plugin_editor.h (modified — mounts header/instrument panes)
- plugin/plugin_editor.cpp (modified)
- plugin/plugin_processor.h (modified — CC slots, generic set, offsets, selectPreset)
- plugin/plugin_processor.cpp (modified)
- plugin/CMakeLists.txt (modified)
- tests/plugin/control_card_model_test.cpp (new)
- tests/plugin/plugin_processor_test.cpp (modified — CC/voice/volume tests)
- tests/CMakeLists.txt (modified)

## Change Log

- 2026-07-19: Story 3.4 implemented — header bar with live voice count, generated control cards (CC injection via atomic slots), generic set (post-engine vol/pan + route-b tuning/ADSR), preset selector row on the 2.4 parameter. 152/152 tests, pluginval green.
