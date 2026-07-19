---
baseline_commit: 30b783bc2f053cd81b10ff9711ae3f6915d1a415
---

# Story 1.5: Play it live in a DAW

Status: done

## Story

As a DAW musician,
I want the VST3 plugin to load an .sfz and respond to my MIDI keyboard,
so that I can play an SFZ instrument in Reaper today.

## Acceptance Criteria

1. **Given** the plugin loaded in a VST3 host, **when** I load an .sfz via the minimal UI (file chooser + status text) and play, **then** notes sound with velocity, sustain and sostenuto pedals, pitch bend, and mapped CCs, with sample-accurate timing within the host buffer (NFR-2).
2. **Given** a library that fails to load, **when** loading completes, **then** the UI shows the human-readable Diagnostic and the host keeps running (FR-5 seed).

## Tasks / Subtasks

- [x] Task 1: Wire core into the plugin processor (AC: 1)
  - [x] `plugin/` AudioProcessor owns a core engine instance + pool; `processBlock` translates JUCE MIDI buffer → engine events with **sample-accurate offsets** (use `MidiMessage` sample positions within the block, don't quantize to block start)
  - [x] prepareToPlay → engine sample-rate/block configuration; releaseResources sane; bypass/reset handled without deallocating on the audio thread
  - [x] Stereo out; silence when no instrument loaded
- [x] Task 2: Load path off the audio thread (AC: 1, 2)
  - [x] Load command: background thread runs lower (Story 1.3) → validate → pool load → engine snapshot swap (Story 1.4 mechanism); audio thread never blocks; previous state keeps sounding until swap
  - [x] Load result (success or Diagnostics list) marshalled to the UI thread safely
- [x] Task 3: Minimal UI (AC: 1, 2)
  - [x] Deliberately minimal editor: "Load SFZ…" button (async JUCE FileChooser) + status text area — **no styling effort**; the real UI is Epic 3 (v3 tokens), do not pre-build it
  - [x] Status shows: loading…, loaded `<name>`, or the human-readable Diagnostic message(s) on failure (severity + message; selectable text is an Epic 3 concern, plain text fine here)
  - [x] Failure keeps previous instrument playing and host running (FR-5 seed)
- [x] Task 4: MIDI behaviors through the engine (AC: 1)
  - [x] Note on/off + velocity, sustain (CC64) and sostenuto (CC66) pedals, pitch bend (respecting model bend range), mod wheel (CC1) and other CCs mapped by the loaded library
  - [x] Verify sfizz's CC/pedal handling is reachable through the model+engine binding from 1.4; extend the binding if pedals/bend didn't survive lowering (they must be model-expressible, not an SFZ-text backdoor — AD-1)
- [ ] Task 5: Verify in hosts + CI (AC: 1, 2)
  - [ ] Manual: Reaper (Windows) — load .sfz, play with velocity/pedals/bend/CC1, confirm no dropouts and correct timing feel; malformed file shows diagnostic, host survives — **pending user verification**
  - [x] pluginval strictness 5 still green in CI (now with real processing + editor) — verified locally (Windows, SUCCESS); CI runs the same gate on all three platforms
  - [x] Automated processBlock test at the shell level (headless): feed MIDI, assert non-silent output and RT-detector clean (reuse Story 1.4 detector where the platform allows)

### Review Findings

- [x] [Review][Defer] `validate.cpp` doesn't enforce bend sign convention — `bendUpCents`/`bendDownCents` are only magnitude-checked ([-9600,9600]); asymmetric/inverted bend ranges are intentionally allowed flexibility, not a bug — deferred, by design [core/model/validate.cpp:75-79]
- [x] [Review][Patch] Velocity-0 Note On forwarded as NoteOn instead of NoteOff, risking stuck notes — `msg.isNoteOn(true)` matches a velocity-0 note-on (the MIDI running-status-off convention) before the `isNoteOff` branch is reached, so it dispatches `EngineEvent::Type::NoteOn` with velocity 0 rather than a NoteOff. Fixed: route velocity-0 note-on to NoteOff. [plugin/plugin_processor.cpp:99-106]
- [x] [Review][Patch] `PluginEditor::chooseAndLoad()` captures raw `this` in the async `FileChooser` completion lambda with no lifetime guard — if the editor is destroyed while the OS file dialog is still open, the callback can fire against a dangling `this`/`processor_`. Fixed: lambda now captures a `juce::Component::SafePointer<PluginEditor>` and no-ops if the editor was destroyed. [plugin/plugin_editor.cpp:48-61]
- [x] [Review][Patch] Sanitizer CI job omits the new `fbsampler-plugin-tests` target — the ASan/UBSan build step still only builds `fbsampler-tests fbsampler-render-tests sfz-fuzz-replay`, leaving the plugin-shell test (background load thread, atomic snapshot swap, MIDI translation — the highest-risk new code in this story) without sanitizer coverage. Fixed: added `fbsampler-plugin-tests` to the sanitizer build target list. [.github/workflows/ci.yml:123]
- [x] [Review][Patch] `pool_test.cpp`'s reuse test asserts an exact slot/handle identity (`b == a`) rather than the documented `SamplePool` contract (refcount/liveness) — brittle to any future change in slot-selection strategy that would still honor the interface. Fixed: test now cycles many acquire/release pairs and asserts no `capacity_exceeded` diagnostic, pinning the contract instead of the slot number. [tests/render/pool_test.cpp]
- [x] [Review][Defer] `Engine::load()`'s bind-failure path doesn't early-exit on the first `bindFailed` region — it keeps calling `pool->acquire()` (full file reads) for every remaining region even after the load is already known to fail, wasting I/O on the background thread on every retry of a bad load. — deferred, minor efficiency only, not correctness-affecting [core/engine/engine.cpp:128-141]
- [x] [Review][Defer] `Engine::process()` has no `numFrames == 0` guard — with `numEvents > 0` and `numFrames == 0`, events still dispatch to sfizz before a zero-length `renderBlock` call, an unreachable case via the current real caller (`plugin_processor.cpp` guards `numFrames > 0`) but a latent gap in the public API contract for any future/test caller. — deferred, unreachable via current call sites [core/engine/engine.cpp:175-217]
- [x] [Review][Defer] `AllRamSamplePool` slot reuse (`entries_[slot] = std::move(entry)`) replaces the `Entry` without a lock while `info()`/`residentChannel()` read `entries_[handle-1].get()` unlocked — currently safe only because `acquire()` never selects a slot with `refCount > 0`, and the audio thread only ever holds handles pinned (refCount-held) by the live `EngineSnapshot`, so no live/pinned slot is ever replaced concurrently. Worth an explicit invariant comment or assert guarding this. — deferred, unreachable via current call sites; hardening only [core/pool/all_ram_pool.cpp:43-89]

## Dev Notes

- **This is the walking skeleton's proof-of-life story** — first end-to-end: file → frontend → model → pool → engine → host audio. Keep every piece thin; hardening (async progress UX, crossfade switching, state save) belongs to Epics 3/5/6.
- **AD-3 shape holds even in v0:** structural change (library load) via command + immutable snapshot swap; UI never mutates engine data directly. The background-load + swap from Story 1.4 is the path — the UI only issues commands and renders results.
- **Sample-accurate timing (FR-11/NFR-2):** JUCE hands `MidiBuffer` with per-event sample positions; forward those offsets into the engine event list so notes trigger mid-block, not at block boundaries. This is a common LLM shortcut mistake — do not iterate messages and fire them all at offset 0.
- **State save/restore is NOT this story** (Epic 6). Implement get/setStateInformation as harmless no-ops/minimal stubs that pluginval tolerates.
- **Sostenuto (CC66)** is explicitly in the AC — verify sfizz supports it through the binding (sfizz does implement sostenuto); test it, don't assume.
- **Threading care in the editor:** load completion callback lands on a background thread; hop to the message thread (`juce::MessageManager::callAsync`) before touching components. FileChooser must be the async API (modal loops are banned in plugins and fail pluginval).
- **No config service yet:** the file chooser starts wherever; library folders/index arrive in Epic 3 (AD-9). Don't stub config files now.
- Depends on: Stories 1.1 (plugin target), 1.3 (SFZ frontend), 1.4 (engine + pool + swap).

### Project Structure Notes

- All work in `plugin/` (processor, minimal editor) plus small core API surface adjustments if the engine/loader API needs ergonomics for host use. Core stays JUCE-free at its public boundary (AD-6).

### References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-3, #AD-5, #AD-6, #Capability-→-Architecture-Map]
- [Source: docs/planning-artifacts/epics.md#Story-1.5]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-5, #FR-11, #NFR-2]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5)

### Debug Log References

- Sustain/sostenuto tests initially silent for the looped pad region: traced to a latent Story 1.4 bug — `Engine::load` set sfizz preload to `uint32_t` max; sfizz adds each region's `offset` to the preload size internally and the uint32 sum wrapped, leaving every offset region silent. Fixed to `1u << 30` frames (still "everything" for real samples). The seed render reference wav was regenerated (`FBSAMPLER_UPDATE_RENDER_FIXTURE=1`) because the old reference contained the silent pad.

### Completion Notes List

- Engine boundary extended: `EngineEvent` gains `ControlChange` (CC number in `note`, value in `velocity`) and `PitchBend` (`bendValue` -8192..8191) → sfizz `cc()` / `pitchWheel()`.
- Bend range made model-expressible (AD-1): `Region::bendUpCents`/`bendDownCents` (defaults 200/-200 per SFZ), parsed from `bend_up`/`bend_down` (clamped ±9600 with `sfz.value_clamped`), validated (`region.bend_range_out_of_range`), serialized, and emitted in the model→SFZ lowering. Model schema bumped v2 → v3 (SPEC.md updated, golden snapshots regenerated).
- Sustain (CC64) and sostenuto (CC66) verified through the binding by render tests — sfizz handles both natively; no model change needed for pedals (performance events, not instrument structure).
- `PluginProcessor` owns Engine + all-RAM pool; MIDI translated with per-event `samplePosition` offsets (verified by test: first 128 frames silent for a note-on at offset 128). Event scratch preallocated (4096 cap, drop beyond) — no audio-thread allocation; RT-detector clean in the headless shell test.
- Load path: `loadSfzFileAsync` spawns a background thread running lower → validate → engine snapshot swap; single in-flight load enforced by atomic flag; completion marshalled via `ChangeBroadcaster` (JUCE delivers on the message thread). Failure keeps the previous snapshot audible (tested).
- Minimal editor: "Load SFZ…" TextButton (async FileChooser) + status Label; no styling. State save/restore left as no-op stubs (Epic 6).
- Buses restricted to stereo-out only (was stereo-or-mono in the 1.1 shell); the engine renders exactly 2 channels.
- pluginval strictness 5 run locally on the Release VST3: SUCCESS. CI `ctest` now wrapped in `xvfb-run` on Linux because the new plugin-shell test binary links `juce_gui_basics`.
- All 77 ctest tests green (unit, golden, render regression, RT safety, MIDI behaviors, plugin shell, fuzz replay, AD-6 guard).
- Manual Reaper verification (user): notes/velocity layers confirmed working. First attempt froze the DAW after loading a real velocity bank — root cause: thousands of `sfz.opcode_unsupported` warnings concatenated into the status text; the Label re-laid-out the megabyte string every paint on the shared host message thread. Fixed by capping the status to errors-first + 12 diagnostic lines + "... and N more"; pinned by the "status text stays bounded" plugin test (5000-warning library, status < 4000 chars).

### File List

- core/include/fbsampler/model.h (modified — bend range fields, schema v3)
- core/include/fbsampler/engine.h (modified — ControlChange/PitchBend events)
- core/engine/engine.cpp (modified — event dispatch; preload overflow fix)
- core/engine/model_to_sfz.cpp (modified — emit bend_up/bend_down)
- core/model/serialize.cpp (modified — bend fields round-trip)
- core/model/validate.cpp (modified — bend range check)
- core/model/SPEC.md (modified — v3, bend fields, new diagnostic code)
- core/frontends/sfz/sfz_frontend.cpp (modified — parse bend_up/bend_down)
- plugin/plugin_processor.h (rewritten — engine + pool + async load)
- plugin/plugin_processor.cpp (rewritten)
- plugin/plugin_editor.h (new)
- plugin/plugin_editor.cpp (new)
- plugin/CMakeLists.txt (modified — editor source)
- tests/render/midi_behavior_test.cpp (new — pedals, bend, mapped CC)
- tests/render/render_harness.h / .cpp (modified — CC/bend timeline events)
- tests/plugin/plugin_processor_test.cpp (new — headless shell test)
- tests/CMakeLists.txt (modified — fbsampler-plugin-tests target)
- tests/sfz_frontend_test.cpp (modified — bend lowering tests)
- tests/model_validate_test.cpp (modified — bend validation test)
- tests/golden/model_golden_test.cpp (modified — schema v3)
- tests/golden/model_v0_reference.txt (regenerated — v3 + bend fields)
- tests/golden/sfz/*.expected.txt (regenerated — v3 + bend fields)
- tests/render/fixtures/reference/seed_render.wav (regenerated — pad audible after preload fix)
- .github/workflows/ci.yml (modified — xvfb-run for ctest on Linux)
- docs/implementation-artifacts/1-5-play-it-live-in-a-daw.md (this file)
- docs/implementation-artifacts/sprint-status.yaml (status updates)

## Change Log

- 2026-07-19: Story 1.5 implemented — VST3 plugin plays SFZ instruments live: engine wired into processor with sample-accurate MIDI, background load path with diagnostics UI, pedals/bend/CC through the model binding (bend range added to model, schema v3), headless shell test + local pluginval strictness-5 green. Fixed latent 1.4 preload-overflow bug that silenced offset regions; seed render reference regenerated.
