# Story 1.5: Play it live in a DAW

Status: ready-for-dev

## Story

As a DAW musician,
I want the VST3 plugin to load an .sfz and respond to my MIDI keyboard,
so that I can play an SFZ instrument in Reaper today.

## Acceptance Criteria

1. **Given** the plugin loaded in a VST3 host, **when** I load an .sfz via the minimal UI (file chooser + status text) and play, **then** notes sound with velocity, sustain and sostenuto pedals, pitch bend, and mapped CCs, with sample-accurate timing within the host buffer (NFR-2).
2. **Given** a library that fails to load, **when** loading completes, **then** the UI shows the human-readable Diagnostic and the host keeps running (FR-5 seed).

## Tasks / Subtasks

- [ ] Task 1: Wire core into the plugin processor (AC: 1)
  - [ ] `plugin/` AudioProcessor owns a core engine instance + pool; `processBlock` translates JUCE MIDI buffer → engine events with **sample-accurate offsets** (use `MidiMessage` sample positions within the block, don't quantize to block start)
  - [ ] prepareToPlay → engine sample-rate/block configuration; releaseResources sane; bypass/reset handled without deallocating on the audio thread
  - [ ] Stereo out; silence when no instrument loaded
- [ ] Task 2: Load path off the audio thread (AC: 1, 2)
  - [ ] Load command: background thread runs lower (Story 1.3) → validate → pool load → engine snapshot swap (Story 1.4 mechanism); audio thread never blocks; previous state keeps sounding until swap
  - [ ] Load result (success or Diagnostics list) marshalled to the UI thread safely
- [ ] Task 3: Minimal UI (AC: 1, 2)
  - [ ] Deliberately minimal editor: "Load SFZ…" button (async JUCE FileChooser) + status text area — **no styling effort**; the real UI is Epic 3 (v3 tokens), do not pre-build it
  - [ ] Status shows: loading…, loaded `<name>`, or the human-readable Diagnostic message(s) on failure (severity + message; selectable text is an Epic 3 concern, plain text fine here)
  - [ ] Failure keeps previous instrument playing and host running (FR-5 seed)
- [ ] Task 4: MIDI behaviors through the engine (AC: 1)
  - [ ] Note on/off + velocity, sustain (CC64) and sostenuto (CC66) pedals, pitch bend (respecting model bend range), mod wheel (CC1) and other CCs mapped by the loaded library
  - [ ] Verify sfizz's CC/pedal handling is reachable through the model+engine binding from 1.4; extend the binding if pedals/bend didn't survive lowering (they must be model-expressible, not an SFZ-text backdoor — AD-1)
- [ ] Task 5: Verify in hosts + CI (AC: 1, 2)
  - [ ] Manual: Reaper (Windows) — load .sfz, play with velocity/pedals/bend/CC1, confirm no dropouts and correct timing feel; malformed file shows diagnostic, host survives
  - [ ] pluginval strictness 5 still green in CI (now with real processing + editor)
  - [ ] Automated processBlock test at the shell level (headless): feed MIDI, assert non-silent output and RT-detector clean (reuse Story 1.4 detector where the platform allows)

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

### Debug Log References

### Completion Notes List

### File List
