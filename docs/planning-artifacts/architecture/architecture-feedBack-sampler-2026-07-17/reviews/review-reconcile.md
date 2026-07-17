# Reconciliation Review — ARCHITECTURE-SPINE.md vs PRD / Addendum / UX

Date: 2026-07-17
Inputs: prd.md, addendum.md, DESIGN.md, EXPERIENCE.md
Scope note: the spine's retirement of the addendum's "core lib consumed by feedBack in-app engine" line (AD-5 host-VST decision) is a known user decision and is NOT flagged here.

## Verdict: GAPS

The spine is strong and covers nearly all FR/NFR bindings, but a handful of quiet requirements from the UX spine and addendum did not land, and one AD statement quietly closes an escape hatch the addendum left open.

## Findings

### F1 — Crossfade-on-ready library switch not captured (EXPERIENCE, Component Patterns; FR-8/FR-14)
EXPERIENCE.md: "loading a library never interrupts audio of the current one until the new one is playable (crossfade-on-ready)." AD-3's atomic snapshot swap guarantees no-dropout switching, but says nothing about *when* the swap fires (only once the new model is playable) or the audible crossfade between old and new snapshots. This is an engine/loader obligation, not a UI nicety — it constrains the command-path design (two live snapshots during transition, RT-safe crossfade). Should be an explicit clause in AD-3 or AD-2.

### F2 — Drag-and-drop load path conflicts with AD-4's identity model (EXPERIENCE, Interaction Primitives)
EXPERIENCE.md: "dropping a library file or folder anywhere on the window opens/adds it." A dropped file that lives *outside* any designated library folder has no "format + library name + library-folder-relative path" identity, so AD-4 as written cannot persist it in plugin state. The spine must either (a) declare drop = implicit add-folder/import into the index, or (b) allow an absolute-path fallback identity — currently it forbids "a bare absolute path as the primary key" with no alternative. Unresolved contradiction.

### F3 — Recovery root-remap persistence not architecturally placed (EXPERIENCE, Flow 3; FR-19)
"Settings remember the remap for all libraries under the old root" implies a persisted path-remap table in the shared config store, consulted during AD-4 resolution. AD-4 covers fallback search + missing-library UI, and AD-9 covers the store, but neither names the remap record or its ownership. Quiet requirement dropped.

### F4 — Accessibility floor has no architectural binding (EXPERIENCE, Accessibility Floor; FR-20/21)
Full keyboard operability, JUCE accessibility API labels on every control (including DS artwork controls deriving accessible names from .dspreset), announced knob values, WCAG AA contrast. The capability map delegates FR-20/21 wholesale to "UX adopted," but the .dspreset→accessible-name requirement reaches into the DS frontend/canonical model (control metadata must survive lowering) — that is a core/IR concern, not editor styling. At minimum the canonical control map should be required to carry display/accessible names.

### F5 — Spine hardens two addendum positions without noting it
- AD-1: "FluidSynth is a test oracle only — never a runtime dependency" closes the addendum's explicit contingency ("embed only if a dedicated SF2 voice path proves necessary"; open question that SF2 modulator matrix may not lower 1:1). Fine as a decision, but the spine should own it as one (state the fallback if SF2 modulators don't lower cleanly: extend the mod matrix, never embed — which AD-1 half-says).
- AD-10/Stack: pluginval "strictness ≥ 5 floor" drops the addendum's "10 target." Minor, but the target was a stated QA aim.

## Minor notes (no action required)
- MIDI-learn is a v1 [ASSUMPTION] in EXPERIENCE.md but appears in the spine only under Deferred ("UX owns it"). Consistent if the deferral is read as UX-scoped, but the engine's CC-matrix hook for Learn CC should not be blocked by AD-8's proxy pool.
- FR-9 soundfont-preset browsing ("instant, same loaded file" per EXPERIENCE) fits AD-3's snapshot model only if preset switch within a loaded soundfont is a lightweight command, not a full re-lower; worth a sentence, not a gap.
- Counter-metric "embedded engine must not degrade app startup/install size" is naturally satisfied by AD-5 (out-of-process host); no flag.
- DESIGN.md token/`--fbv-*` theming contract is UI-internal; adoption via the FR-20/21 map row is adequate.
