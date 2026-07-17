---
title: feedBack Sampler — EXPERIENCE.md
status: final
created: 2026-07-17
updated: 2026-07-17
sources:
  - ../../prds/prd-feedBack-sampler-2026-07-17/prd.md
---

# feedBack Sampler — Experience

> Layout reference: [mockups/key-screens.html](mockups/key-screens.html) covers all three IA surfaces. On conflict, this spine and DESIGN.md win over any mock.

## Foundation

Desktop plugin UI (JUCE-rendered), one codebase for standalone VSTi/AU and hosted-inside-feedBack (which treats it like any other VST — same editor window). Freely resizable, min 720×480 (see DESIGN.md Layout). Visual identity: DESIGN.md is the reference; no external UI system — JUCE components styled to the token set.

## Information Architecture

Three surfaces (PRD FR-21), one window:

1. **Instrument view** (home) — header bar + the loaded library's controls: Decent Sampler artwork/controls in the artwork frame, or generated controls (SFZ CC labels, generic volume/pan/tuning/ADSR) laid out in cards when the library defines no UI. SF2/SF3 adds a soundfont-preset selector row.
2. **Library browser** — sidebar pane (collapses to overlay below 900px): search field, format filter pills, library list; selecting loads and returns focus to the instrument view.
3. **Settings** — overlay panel: library folders (add/remove/rescan), voice limit, RAM/streaming threshold, about/licenses.

Navigation: browser toggle (left), settings gear (right), both in the header. No deeper hierarchy in v1.

## Voice and Tone

Studio-precise, zero jargon about formats' internals. Errors name the problem and the fix: "3 samples missing — the library folder may have moved. Locate folder…" — never "parse error." Empty states invite: "Add a library folder to get started." No exclamation marks, no gamification copy. All error text, paths, and version strings selectable (v3 policy).

## Component Patterns

- **Library list**: virtualized; type-to-search filters live; Enter loads selection; loading a library never interrupts audio of the current one until the new one is playable (crossfade-on-ready). Failed libraries stay listed with status dot + reason tooltip.
- **Soundfont preset selector**: bank/program dropdown with search; changing preset is instant (same loaded file).
- **Knobs/sliders**: drag vertical, fine-adjust with Shift, double-click reset, scroll-wheel steps; every control host-automatable and MIDI-learnable via right-click → "Learn CC". [ASSUMPTION] MIDI-learn in v1 — cheap with the engine's CC matrix; cut if scope presses.
- **Artwork frame**: DS-defined controls behave per their library definition; frame chrome provides the consistent parts (preset name, badge).

## State Patterns

- **Empty** (no folders): centered invite + "Add folder" primary button.
- **Scanning**: non-blocking banner with progress and current path; list populates incrementally.
- **Loading a library**: progress fill in the list row + header spinner; instrument playable-when-ready per region (PRD FR-14); UI never blocks.
- **Load failure**: row status dot {colors.low}, human-readable reason (PRD FR-5); instrument view keeps previous library.
- **No MIDI input detected** (standalone): passive hint in header after 10s of no events, dismissible. [ASSUMPTION]
- **Host state restore**: silent; if the library file is missing on reopen, instrument view shows a "Locate library…" recovery card instead of failing silently.

## Interaction Primitives

Keyboard: full tab traversal; arrows navigate the library list; Enter loads; Esc closes overlays. Mouse: single-click select, double-click load (list); knob gestures above. Text fields standard OS editing. Drag-and-drop: dropping a library file or folder anywhere on the window opens/adds it. [ASSUMPTION]

## Accessibility Floor

- Full keyboard operability of all three surfaces; visible focus (DESIGN.md focus ring) everywhere.
- Contrast: token pairs meet WCAG AA on their designated surfaces ({colors.text}/{colors.textDim} on {colors.bg}/{colors.surface}); status colors never the sole signal (dots pair with text).
- Screen-reader labels on all controls (JUCE accessibility API); knob values announced on change.
- Respects host/OS UI scale; text resizes with window within min-size constraints.
- Library artwork is exempt (author-supplied), but every artwork control retains an accessible name from the .dspreset definition.

## Key Flows

**Flow 1 — Lena picks her piano (embedded).** Lena opens the sampler like any VST in feedBack. Empty state invites her to add her libraries folder; scan banner runs; the list fills. She types "felt", presses Enter — progress fills the row, and *the moment the first regions are resident she plays a note and it sounds* (climax). Full library finishes loading behind her playing. She never sees a format name unless she looks at the badge.

**Flow 2 — Marco consolidates in Reaper (standalone).** Marco adds his three sample folders in Settings once. In the browser he filters by format pill "SFZ", loads a string patch, right-clicks the dynamics knob → Learn CC → moves his mod wheel (climax: the knob follows). He saves the Reaper project; on reopen the same patch, preset, and CC mapping restore silently. On another track he loads a .dslibrary — its own artwork appears inside the feedBack frame.

**Flow 3 — recovery.** Marco moved his samples drive. Project reopens: recovery card "Library not found — Locate…" ; he points at the new path; settings remember the remap for all libraries under the old root. [ASSUMPTION] Root-remap behavior.

## Responsive & Platform

Reflow at 900px (browser becomes overlay); min 720×480. Windows/macOS/Linux parity; macOS honors AU host resize conventions; no touch/mobile targets in v1. HiDPI: all chrome vector/token-driven; library artwork scales with nearest-neighbor avoided (smooth interpolation).
