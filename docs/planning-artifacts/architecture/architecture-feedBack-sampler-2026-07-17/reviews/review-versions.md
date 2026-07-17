# Review: Versions & Reality-Check of Committed Decisions

- **Artifact:** ARCHITECTURE-SPINE.md (feedBack-sampler, 2026-07-17)
- **Lens:** were the committed facts (versions, tool existence, license, VST3 premise) verified against reality rather than asserted from training data?
- **Date:** 2026-07-17
- **Verdict:** PASS with advisories

## Checks performed (web, 2026-07-17)

| Claim in spine | Reality | Status |
| --- | --- | --- |
| JUCE 8.0.14 | 8.0.14 released June 23, 2026 — current latest (8.0.13 was May 2026) | Confirmed current |
| sfizz 1.2.3 | 1.2.3 is the latest stable release on sfztools/sfizz | Confirmed latest — but released **January 14, 2024**, i.e. ~2.5 years old (advisory below) |
| sfizz exists / fits engine role | Repo active; project split (2023) into `sfizz` (library) + `sfizz-ui` (plugins); library-as-engine usage is exactly the intended shape | Confirmed |
| sfizz license | **BSD-2-Clause** (permissive) — compatible with the spine's AGPL-3.0 distribution matrix; imposes no copyleft of its own | Confirmed plausible |
| sfizz SFZ coverage | Official opcode table: SFZ v1 ~96%, SFZ v2 ~44%, ARIA extensions ~45% | Confirmed; "superset IR over SFZ" premise plausible, but v2 gaps are real (advisory below) |
| pluginval exists, "latest" | Tracktion pluginval v1.0.4 (Dec 2024), active repo, CMake/CTest integration, strictness levels supported | Confirmed |
| pamplejuce (CI matrix reference) | sudara/pamplejuce active: JUCE 8, Catch2, pluginval, GitHub Actions cross-platform, CMake 3.25+ | Confirmed |
| Catch2 3.x | Catch2 v3 line is current standard; pamplejuce ships v3.7.x | Confirmed |
| AD-8 premise: VST3 forbids adding parameters after instantiation | Confirmed: VST3 parameter list/order is fixed at first release (referenced by position; backwards-compat requirement); JUCE APVTS explicitly does not support runtime add/remove of host-shared parameters. Fixed proxy-pool is the established industry workaround (e.g. VCV Rack 2 uses a 1024-slot pool) | Confirmed accurate |

## Findings

1. **[Advisory] sfizz release staleness.** 1.2.3 is correct as "latest" but dates from Jan 2024; upstream release cadence has stalled. The spine already hedges ("sfizz is the adopted foundation and is replaceable"), which is the right mitigation, but plan to pin a commit or vendor the source rather than depend on upstream releases, and expect to carry local patches (e.g. for newer compilers/CMake 3.28+).
2. **[Advisory] SFZ v2 coverage is 44%, not near-complete.** The unified-IR premise holds, but the fidelity harness (AD-10) should expect real gaps for SFZ v2/ARIA libraries; corpus selection should weight this.
3. **[Note] pluginval "latest" resolves to v1.0.4 (Dec 2024)** — fine, but pin the version in CI for reproducibility rather than literal "latest".
4. **[Confirmed] AD-8's VST3 premise is accurate** — fixed parameter list after first release is a genuine VST3/JUCE constraint; the 128-proxy-pool decision is a standard, well-precedented response.
5. **No stale or fabricated versions found.** JUCE 8.0.14 is the current release as of this review date; all named tools exist and match their stated roles.

## Sources

- https://github.com/juce-framework/JUCE/releases
- https://github.com/sfztools/sfizz/releases/tag/1.2.3
- https://github.com/sfztools/sfizz
- https://sfztools.github.io/sfizz/development/status/opcodes/
- https://github.com/Tracktion/pluginval/releases
- https://github.com/sudara/pamplejuce
- https://forums.steinberg.net/t/does-vst3-support-structural-changes-of-parameter-lists-when-the-plugin-is-already-up-and-running/709851
- https://forum.juce.com/t/adding-parameters-to-vst3-while-plugin-is-running/53324
