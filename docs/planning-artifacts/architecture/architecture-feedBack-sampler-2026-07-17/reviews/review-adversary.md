# Adversarial Review — ARCHITECTURE-SPINE.md (feedBack-sampler)

- **Reviewed:** ARCHITECTURE-SPINE.md, 2026-07-17 draft
- **Lens:** construct pairs of one-level-down units that each obey every AD to the letter yet build incompatibly. Every pair found is a hole; each gets a proposed closing rule.
- **Verdict:** CONDITIONAL PASS — the spine's macro shape (frontends → IR → one engine, core/shell split, three-tier state) is sound and no AD contradicts another at its own altitude. But the canonical model, control map, diagnostic codes, pool ownership during snapshot swap, index-freshness, and the app→config channel are all named without being contracted. Six compliant-but-incompatible pairs below; each needs a new or tightened AD before units are cut.

---

## Finding 1 — The canonical model is named but not contracted: two frontends lower "correctly" into mutually incompatible IR

**Pair:** SFZ frontend + SF2 frontend (or either frontend + engine).

**Attack.** AD-1 says "every format lowers into the single canonical model; the mod matrix is extended with SF2 curve/source primitives." Nothing pins the semantics of that model. Both of these teams are fully compliant:

- SFZ frontend emits mod-matrix depths in *linear gain* and envelope times as *opcode seconds*, following SFZ opcode semantics, and treats unspecified fields as "engine default."
- SF2 frontend emits depths in *centibels/cents* (SF2 generator units), envelope times in *timecents*, and treats unspecified fields as "SF2 spec default" — because "extended with SF2 curve/source primitives" reads as license to carry SF2 units into the matrix.

The engine can interpret exactly one convention. Result: one format plays at wrong gain/pitch/envelope while every AD is satisfied, and the golden-file harness (AD-10) can't catch it — each frontend's snapshot is self-consistently wrong. Same hole for default values, region-crossfade semantics, and which mod-matrix primitive set version a frontend may assume.

**Closing rule (new AD-11 — Canonical model is a versioned, owned contract).** `core/model` owns a written canonical-model spec with: (a) a fixed unit system (pitch in cents, gain in dB, envelope/LFO times in seconds at engine rate, normalized 0–1 controls); (b) explicit defaults for every field — frontends emit fully-resolved regions, never "engine default"; (c) a model schema version; frontends declare the version they target and CI rejects mismatches; (d) a shared cross-format semantic conformance test (same musical gesture authored in DS/SFZ/SF2 must lower to equivalent matrix entries within tolerance) gating CI alongside the per-format golden files. Extending the mod-matrix primitive set is a model-spec change, not a frontend change.

## Finding 2 — "Declaration order" proxy mapping is frontend-defined: control identity is unstable across formats, rescans, and library edits

**Pair:** DS frontend + plugin-shell parameter layer (and DS frontend vs SFZ frontend).

**Attack.** AD-8 maps library controls "in declaration order onto 128 proxies." Compliant divergence:

- DS frontend orders by XML document order of `<control>`-ish elements; SFZ frontend orders by first appearance of `set_ccN`/label opcodes; SF2 frontend synthesizes macro controls in generator-enumeration order. All are "declaration order." A user's library edit (inserting one knob) silently shifts every proxy binding, so saved DAW automation (AD-3 stores "parameter values") now drives the wrong knobs — every AD honored, host projects corrupted in effect.
- The shell, equally compliant, may either rebind proxies on library switch or freeze them; AD-3's snapshot swap says nothing about when proxy meaning changes relative to the swap.

**Closing rule (tighten AD-8).** The canonical model owns an ordered `ControlMap` with *stable per-control IDs* (frontend-derived deterministic IDs, e.g. hash of format-native identifier, not position). Proxy binding is by ControlMap index, but plugin state persists control IDs alongside parameter values; on load, values are re-associated by ID and orphaned values are dropped with a diagnostic — never applied positionally. Frontends must document and freeze their ID-derivation rule; it is covered by golden files. Proxy re-mapping becomes visible only atomically with the AD-3 snapshot swap (see Finding 4).

## Finding 3 — "Stable code" diagnostics with no registry: colliding code spaces and an unmappable missing-library state

**Pair:** any two frontends + editor UI (consumer of codes).

**Attack.** The Diagnostic convention requires "severity + stable code + message + location," but code allocation has no owner. DS frontend mints `E001..`, SF2 frontend mints `E001..` with different meanings; the UI (which per AD-4 must render an *explicit missing-library state*, and per FR-5 must present load errors) switches on codes and mis-renders. Also: is "missing library" a config-service code, a loader code, or a shell state? Two compliant owners can both emit it, or neither.

**Closing rule (tighten Errors convention → AD-level).** One append-only diagnostic-code registry lives in `core/model` (single header/table), namespaced by component (`DS####`, `SFZ####`, `SF2####`, `POOL####`, `CFG####`, `ENG####`). Codes are never reused or renumbered; UI-significant states (missing-library, unresolvable-reference, newer-schema-read-only) are registry entries with one designated emitter each. Fuzz and golden harnesses assert emitted codes exist in the registry.

## Finding 4 — Pool memory has two de-facto owners during a snapshot swap: loader eviction vs. retiring engine model

**Pair:** background loader (AD-3 command path) + sample pool (AD-2).

**Attack.** AD-2: pool owns all sample memory under a budget. AD-3: loader builds a *complete* new model, atomically swapped; old model "retired off-thread." Compliant collision: to preload library B within the budget, the loader (correctly, per AD-2's "configurable RAM budget") asks the pool to evict library A's heads — while the audio thread is still rendering the *old* snapshot referencing them. Either the pool evicts in-use memory (RT fault / glitch, violating nothing textual) or it refuses and the loader can't fit B, deadlocking the switch. Neither unit is wrong by its AD. A second variant: old snapshot retirement frees regions, but the pool doesn't know which entries just lost their last reference, so budget accounting drifts.

**Closing rule (tighten AD-2/AD-3 seam).** Pool entries are reference-counted by engine snapshots; a snapshot pins every entry it references at build time; eviction is legal only at refcount zero. During a switch the budget may transiently hold old+new preload heads (peak = 2× head budget is the accepted, documented cost, or the loader streams-only until retirement). Retirement — running strictly off-thread after the audio thread has published that it no longer reads the old snapshot (e.g. epoch/RCU-style grace) — releases the pins and returns budget. Voice tails referencing old samples end or fade before release; the ≤2 s switch budget (AD-7) includes this drain.

## Finding 5 — Library-index freshness: AD-9's last-writer-wins plus AD-4's "resolve against the current index" lets a compliant instance report a present library as missing

**Pair:** config service (AD-9) + resolver/loader (AD-4), across two plugin instances.

**Attack.** Instance A rescans folders and atomically rewrites `library-index.json` (compliant). Instance B, holding its in-memory index loaded at startup (nothing requires reload), resolves a saved reference (AD-4) against a stale index, misses, and — fully compliantly — shows the missing-library state for a library that exists on disk. Worse LWW variant: A's rescan of folder-set X and B's concurrent "add folder Y + rescan" both write the whole index; last writer silently discards the other's libraries. Every AD honored; shared store forked in effect.

**Closing rule (tighten AD-9/AD-4).** The index file carries a monotonic generation (and the service exposes it); resolution failure MUST reload the index once from disk before emitting the missing-library diagnostic. Last-writer-wins applies only to scalar settings; the library index is written merge-on-write (read-modify-write of entries keyed by library identity under the atomic-rename discipline), so concurrent scanners union rather than clobber. Optional: file-watch or generation-poll on the index is service-internal, but the "reload-before-declaring-missing" step is contract.

## Finding 6 — AD-5 + AD-9 jointly seal feedBack out of library management: no compliant channel exists for the app to register libraries

**Pair:** feedBack app integration (AD-5) + config service (AD-9).

**Attack.** FR-18/19 imply feedBack participates in the shared library world. AD-9: config files "written only by core's config service." AD-5: the app consumes *only the VST3 binary* — "no static-lib embed target," so the app cannot link core to reach that service. Two compliant builds: (a) the app writes `library-index.json` itself → violates AD-9's single-writer in spirit and forks schema handling the moment core migrates; (b) the app abstains → there is no path for "feedBack installs a bundled library" except a human clicking through the plugin editor. Both teams point at the spine and are right.

**Closing rule (new AD-12 — All library registration flows through core's service; the app reaches it via the plugin).** The only writers of the shared store are processes running core's config service — i.e., plugin instances (and a thin `fbsampler-cli` tool built from core, shipped alongside the plugin, if headless registration is needed for app-bundled content). feedBack registers libraries either by invoking that tool or via a defined host→plugin channel (e.g. a vendor-specific VST3 message / state-chunk hint the shell forwards to the config service). Direct file writes by any non-core binary are prohibited, explicitly.

---

## Minor observations (no AD needed, but note in companions)

- **AD-7 benchmark environment** is unpinned ("one modern desktop core"): CI runners vary; fix a reference machine/runner class or a relative-regression gate, else the budget is unenforceable as written.
- **AD-3 tier 1 vs tier 2 boundary for soundfont-preset index:** it is listed in plugin state (structural) but is also a natural automatable/program-change target; state explicitly that preset selection is a *command*, not a parameter, so no unit exposes it as an APVTS param.
- **Logging sink threading:** "core emits through an injected sink" — declare the sink contract thread-safety (called from loader + config threads, never audio), else shell and core make opposite assumptions.

## Summary of proposed closures

| # | Hole | Closure |
| - | --- | --- |
| 1 | Canonical model semantics unowned | New AD-11: versioned model spec, fixed units, fully-resolved defaults, cross-format conformance test |
| 2 | Proxy mapping by frontend-local "declaration order" | Tighten AD-8: ControlMap with stable control IDs; state re-associates by ID, never position |
| 3 | Diagnostic codes have no registry | Registry in core/model, namespaced, append-only, one emitter per UI-significant state |
| 4 | Pool ownership during snapshot swap | Snapshot-pinned refcounted pool entries; evict at refcount zero; retirement releases pins after grace |
| 5 | Stale index vs LWW clobber | Index generation + reload-before-missing; merge-on-write for index, LWW for settings only |
| 6 | App has no compliant path to register libraries | New AD-12: registration only via core's service, reached through plugin or shipped CLI |
