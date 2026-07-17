# Rubric Review — ARCHITECTURE-SPINE.md (feedBack-sampler)

Reviewed: 2026-07-17
Verdict: **PASS with findings** — the spine fixes the real divergence points (format lowering, sample memory, state ownership, embed path, dependency direction) with enforceable rules; no critical findings, but two high findings need resolution before epics are cut.

## Checklist assessment

1. **Divergence points for the level below** — Strong. AD-1/AD-2/AD-3 fix exactly the seams where independently-built epics (per-format frontends, pool, param layer, config) would otherwise fork. One miss: parameter-identity stability across library versions (F-2).
2. **Rules enforceable** — Mostly yes (build-enforced dependency direction, CI-gated harnesses, schema-versioned artifacts). Two rules lean aspirational as written (F-3, F-4).
3. **Deferred safety** — Deferred items are genuinely internal or product-timing; none lets two units diverge structurally. Voice-stealing and SQLite swap are correctly firewalled behind AD-1/AD-9.
4. **Tech currency** — Plausible: JUCE 8.0.x, sfizz 1.2.3, CMake 3.28+, Catch2 3.x, pluginval, pamplejuce-style GH Actions matrix are all current, real, and mutually compatible (C++17 floor matches sfizz).
5. **Dimension coverage** — CI/test/perf ops are unusually well covered (AD-7, AD-10). Gaps: plugin distribution/update channel into feedBack (F-1), and a thin word on background-thread model beyond the loader (F-5).

## Findings

### High

- **F-1 — Plugin delivery into feedBack is silent (ops/deployment envelope).** AD-5 says feedBack loads "the same plugin binary," but nothing decides how that binary reaches the app: bundled with feedBack releases vs. separately installed, discovery path, and app↔plugin version-compatibility policy. Two teams (app release, sampler release) can independently pick incompatible answers; DAW-side install location is also undecided (system VST3 dir vs. custom). Installer *tech* is legitimately deferred, but the delivery/versioning *contract* is spine altitude. Add an AD or explicit open question.
- **F-2 — AD-8 declaration-order mapping is not stable across library versions.** If a library update inserts/removes a control, every proxy shifts and saved DAW automation silently retargets the wrong control. AD-8 prevents the VST3 count problem but not this divergence; the state chunk (AD-3) stores parameter values without fixing mapping identity. Rule should state whether mapping is (order at first load, persisted in the chunk) or (recomputed each load, breakage accepted) — either is fine, silence is not.

### Medium

- **F-3 — AD-7 budget lacks a measurement fixture.** "One modern desktop core" asserted "by a CI benchmark" is unenforceable on heterogeneous GH Actions runners — the rule will either flake or be quietly relaxed. Pin a reference runner/machine class and a noise-tolerance policy, or restate the budget relative to a calibrated baseline measured on the same runner.
- **F-4 — AD-4 "fallback search" is underspecified.** Resolution order (index → library-folder scan → user prompt?) and what happens on ambiguous matches (two libraries, same name) is left open; two stories implementing load-time resolution could differ observably. One sentence fixing precedence and ambiguity handling closes it.

### Low

- **F-5 — Threading model beyond the loader is implicit.** AD-3 defines the loader/snapshot path, but ownership of other background work (library scanning, streaming I/O threads, UI timers) and their shutdown ordering isn't stated; probably fine to inherit from sfizz/JUCE conventions, but worth a line in Consistency Conventions.
- **F-6 — AU target appears only in Stack.** AU (mac) is listed but no AD or harness mentions AU validation (auval vs pluginval); minor, since pluginval covers AU, but say so.
- **F-7 — Localization/accessibility of the editor** is owned by UX docs per the capability map; acceptable, but the spine could note the delegation explicitly.

## Notes on strengths

Compiler-pipeline framing is the right paradigm for a multi-format sampler; AD-1's "FluidSynth is a test oracle only" and AD-10's four CI-gated harnesses are exemplary enforceability. AD-6's build-enforced core/shell split cleanly protects the deferred v2 author API.
