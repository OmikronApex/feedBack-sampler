---
baseline_commit: 6dd627fcda5f96089112bd65434e334a4f288a58
---

# Story 1.1: Buildable skeleton on three platforms

Status: done

## Story

As a developer (solo maintainer),
I want the repo skeleton with core/shell CMake targets, vendored sfizz, and a three-platform CI matrix,
so that every subsequent story lands on green, verifiable infrastructure.

## Acceptance Criteria

1. **Given** a fresh clone on Windows, macOS, or Linux, **when** the documented CMake build runs, **then** `sampler-core` (static lib) and an empty VST3 plugin target compile and link, with sfizz vendored at a pinned commit, **and** core links no JUCE GUI module (build fails if a GUI dependency is introduced).
2. **Given** a push to the repository, **when** GitHub Actions runs, **then** all three platforms build and run a trivial Catch2 test, and pluginval (strictness 5) passes against the empty plugin.

## Tasks / Subtasks

- [x] Task 1: Repo skeleton per the architecture structural seed (AC: 1)
  - [x] Create directory layout: `core/` (`model/`, `frontends/`, `engine/`, `pool/`, `config/`, `include/fbsampler/`), `plugin/`, `tests/` (`golden/`, `render/`, `fuzz/`, `perf/`), `corpus/`, `cmake/`, `.github/`
  - [x] Top-level `CMakeLists.txt` requiring CMake 3.28+, C++17
  - [x] README with documented build commands per platform
- [x] Task 2: Vendor dependencies at pinned versions (AC: 1)
  - [x] Vendor sfizz 1.2.3 at a pinned commit (git submodule or FetchContent with exact SHA — record the SHA in-repo; local patches are expected later, so prefer a vendoring mechanism that tolerates patching, e.g. submodule of a fork or subtree)
  - [x] Add JUCE 8.0.14 (FetchContent or submodule, pinned)
  - [x] Add Catch2 3.x (pinned)
- [x] Task 3: `sampler-core` static-lib target (AC: 1)
  - [x] CMake target `sampler-core`: static lib, namespace `fbsampler`, public headers under `core/include/fbsampler/`
  - [x] May link JUCE non-GUI modules internally only; public API speaks only own types + std
  - [x] Enforce the no-GUI rule in the build: core's link list must not contain `juce_gui_basics`/`juce_gui_extra`/`juce_graphics` (e.g. a CMake check that fails configure/build if a GUI module appears in `sampler-core`'s LINK_LIBRARIES)
  - [x] Seed a trivial public header + translation unit (e.g. `fbsampler/version.h`) so the lib actually compiles/links
- [x] Task 4: Empty VST3 plugin shell target (AC: 1, 2)
  - [x] `plugin/` JUCE plugin target (juce_add_plugin, VST3 format on all platforms) linking `sampler-core`
  - [x] Minimal AudioProcessor passing audio through silence; editor can be the default/generic one
  - [x] Plugin metadata: working name "feedBack Sampler" (OQ-2 placeholder), sensible bundle/manufacturer IDs
- [x] Task 5: Test target + CI matrix (AC: 2)
  - [x] Catch2 test target with one trivial passing test linking `sampler-core`
  - [x] `.github/workflows/` pamplejuce-style matrix: windows-latest, macos-latest, ubuntu-latest; build + ctest on every push/PR
  - [x] Linux CI deps (X11/ALSA dev packages for JUCE)
  - [x] Download/pin pluginval 1.0.4 in CI; run at strictness 5 against the built VST3 on all three platforms; failure fails the build
- [x] Task 6: Verify green (AC: 1, 2)
  - [x] Local build passes on the dev machine (Windows)
  - [x] CI passes on all three platforms (github.com/OmikronApex/feedBack-sampler, run 4: windows/linux/macos all success incl. pluginval strictness 5)

## Dev Notes

- **This story is the walking-skeleton substrate.** No audio, no parsing — only infrastructure. Resist scope creep: later stories (1.2–1.6) add the model, frontends, engine, and harnesses on top of these exact targets.
- **AD-6 (core/shell rule) is enforced here, mechanically.** `sampler-core` = static lib, zero UI/host/editor deps, public API JUCE-free. Shell depends on core, never the reverse. The "build fails if GUI dependency introduced" AC needs an actual CMake guard, not a convention.
- **Stack (binding, from spine):** C++17 · JUCE 8.0.14 · sfizz 1.2.3 pinned commit · CMake 3.28+ · Catch2 3.x · pluginval 1.0.4 · GitHub Actions. pamplejuce (github.com/sudara/pamplejuce) is the CI/packaging *reference*, not a template to fork — this repo has its own structure.
- **sfizz notes:** upstream cadence is stalled (last release Jan 2024) — pin the exact commit for release 1.2.3 and document it. sfizz itself needs its own deps (abseil etc. are vendored inside sfizz); build it via its CMake with plugins/JACK/rendering clients OFF (`SFIZZ_JACK=OFF`, `SFIZZ_RENDER=OFF`, shared=OFF) — only the static library is needed.
- **AGPL-3.0 distribution** (NFR-6): JUCE under its AGPL option, sfizz BSD-2 — both compatible. Add a LICENSE file (AGPL-3.0) now.
- **Conventions locked from the start** (spine Consistency Conventions): namespace `fbsampler`; files/dirs `snake_case`; types `PascalCase`; functions/vars `camelCase`.
- **pluginval in CI:** official binaries are downloadable per platform; typical invocation `pluginval --strictness-level 5 --validate <plugin.vst3>`. Budget ~3–9 min warm per platform (addendum). Pin version 1.0.4.
- **Windows is the primary dev machine** (this workspace). Ensure the documented build works with Visual Studio generator or Ninja; don't assume bash-only scripts locally (CI runners can use bash).

### Project Structure Notes

- Structure must match the spine's Structural Seed exactly (see References). Public headers live at `core/include/fbsampler/`, not scattered.
- `corpus/` stays empty except a placeholder README until Story 1.6.

### References

- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#Stack, #Structural-Seed, #AD-6, #AD-10]
- [Source: docs/planning-artifacts/epics.md#Story-1.1]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Toolchain-and-QA, #Licensing-matrix]

## Dev Agent Record

### Agent Model Used

claude-fable-5 (Claude Code)

### Implementation Plan

- FetchContent with exact SHAs chosen over submodules: umbrella git repo at `C:/Users/rkasp/PycharmProjects` makes submodules awkward; FetchContent records pins in-repo (`cmake/fbsampler_deps.cmake`) and tolerates future patches via `PATCH_COMMAND`.
- Pins resolved from upstream tags: sfizz 1.2.3 → `4e70dc0bef53b41f2853ed46e26f5911114c92d0`, JUCE 8.0.14 → `2cdfca8feb300fb424002ba2c2751569e5bacb64`, Catch2 v3.8.1 → `2b60af89e23d28eefc081bc930831ee9d45ea58b`.
- sfizz built as static lib only: `SFIZZ_JACK/RENDER/SHARED/DEMOS/DEVTOOLS/BENCHMARKS/TESTS=OFF`; linked PRIVATE into `sampler-core` to prove vendoring.
- AD-6 guard: `fbsampler_assert_no_gui_modules()` in `cmake/fbsampler_guards.cmake` scans LINK_LIBRARIES + INTERFACE_LINK_LIBRARIES of `sampler-core` for `juce_gui_basics|juce_gui_extra|juce_graphics`, FATAL_ERROR at configure.

### Debug Log References

- Configure warnings (benign): sfizz probes Qt5 (devtools, OFF) and libsamplerate (optional resampler fallback) — both absent, build unaffected.
- Guard negative-tested: temporarily adding `juce::juce_gui_basics` to `sampler-core` fails configure with "AD-6 violation"; reverted, clean re-configure + tests green.

### Completion Notes List

- Skeleton per structural seed created; empty dirs held by `.gitkeep`; `corpus/` has placeholder README (Story 1.6).
- `sampler-core` static lib (namespace `fbsampler`) with public `fbsampler/version.h` + `coreVersion()`; public API JUCE-free.
- VST3 shell "feedBack Sampler" (company "feedBack", codes Fdbk/Fbsm, bundle `com.feedback.feedback-sampler`), IS_SYNTH, MIDI in, silence output, generic editor.
- Catch2 test `version_test.cpp` links `sampler-core`; ctest 1/1 passed.
- CI matrix (`.github/workflows/ci.yml`): windows/macos/ubuntu-latest, Linux X11/ALSA deps, build + ctest + pluginval 1.0.4 strictness 5 (xvfb on Linux), failure fails build.
- LICENSE AGPL-3.0 added (NFR-6).
- Local Windows verification: VS2022 Release configure/build green, ctest passed, `feedBack Sampler.vst3` artefact produced.
- Per user decision, feedBack-sampler git-initialized standalone and pushed to public repo github.com/OmikronApex/feedBack-sampler.
- CI iterations to green: (1) sfizz 1.2.3 upstream bugs — GCC rejects `0x1.fffffep-1` hexfloat in Voice.cpp; SfizzConfig.cmake applies ARM32 `-mfpu`/`-mfloat-abi` on arm64 macOS → fixed via idempotent FetchContent PATCH_COMMAND (`cmake/patches/sfizz-1.2.3-fixes.patch` + `apply_patch.cmake`). (2) AppleClang 16+ errors on `template` keyword without arg list in sfizz's vendored atomic_queue submodule → scoped `-Wno-missing-template-arg-list-after-template-kw` on sfizz targets (submodule unpatachable via git apply). (3) Transient macOS runner DNS failure downloading pluginval → curl retries.
- Final CI run: all three platforms success — build, ctest, pluginval strictness 5 against the VST3.

### File List

- CMakeLists.txt
- cmake/fbsampler_deps.cmake
- cmake/fbsampler_guards.cmake
- cmake/patches/sfizz-1.2.3-fixes.patch
- cmake/patches/apply_patch.cmake
- core/CMakeLists.txt
- core/include/fbsampler/version.h
- core/config/version.cpp
- core/model/.gitkeep
- core/frontends/.gitkeep
- core/engine/.gitkeep
- core/pool/.gitkeep
- plugin/CMakeLists.txt
- plugin/plugin_processor.h
- plugin/plugin_processor.cpp
- tests/CMakeLists.txt
- tests/version_test.cpp
- tests/golden/.gitkeep
- tests/render/.gitkeep
- tests/fuzz/.gitkeep
- tests/perf/.gitkeep
- corpus/README.md
- .github/workflows/ci.yml
- README.md
- LICENSE
- .gitignore
- docs/implementation-artifacts/1-1-buildable-skeleton-on-three-platforms.md (story tracking)
- docs/implementation-artifacts/sprint-status.yaml (status tracking)

## Change Log

- 2026-07-17: Story 1.1 implementation — full skeleton, pinned deps, core lib + AD-6 guard, VST3 shell, tests + CI. Local Windows build/tests green.
- 2026-07-17: Repo pushed to github.com/OmikronApex/feedBack-sampler (public, per user). sfizz 1.2.3 patched (GCC hexfloat, arm64 flags), AppleClang atomic_queue suppression, pluginval download retries. CI green on windows/linux/macos incl. pluginval strictness 5. Status → review.

### Review Findings

- [x] [Review][Decision] AD-6 guard has no automated regression test — resolved: user chose option 1 (add automated coverage now). Implemented as `tests/ad6_guard_negative/` (isolated CMake fixture linking a fake `juce_gui_basics` target) + a `ctest` entry in `tests/CMakeLists.txt` (`ad6_guard_negative`) asserting configure fails with the "AD-6 violation" message. Verified locally: fixture correctly fails configure with the expected message.
- [x] [Review][Patch] Version drift between CMake project version and `fbsampler::coreVersion()` [core/config/version.cpp:7] — fixed: `core/config/version.cpp` → `version.cpp.in`, generated via `configure_file(@ONLY)` from `${PROJECT_VERSION}` in `core/CMakeLists.txt`.
- [x] [Review][Patch] pluginval binary may not be executable after unzip on macOS/Linux (no `chmod +x`) [.github/workflows/ci.yml:60] — fixed: added `chmod +x` on non-Windows after unzip.
- [x] [Review][Patch] Hardcoded VST3 artifact path is fragile across JUCE version bumps [.github/workflows/ci.yml:22,27,32] — fixed: added a "Locate VST3 artifact" step that finds the `.vst3` bundle at runtime instead of hardcoding its path per-OS.
- [x] [Review][Patch] No timeout on the "Run pluginval" CI step [.github/workflows/ci.yml:62] — fixed: added `timeout-minutes: 10` to the pluginval run step.
- [x] [Review][Defer] AppleClang atomic_queue suppression only scoped to `APPLE AND Clang`, not exercised elsewhere in CI matrix [cmake/fbsampler_deps.cmake:47] — deferred, pre-existing scope limitation not hit by current CI matrix
- [x] [Review][Defer] sfizz `FetchContent` `UPDATE_DISCONNECTED ON` risks stale vendored source if the patch file changes without clearing the build cache [cmake/fbsampler_deps.cmake:26] — deferred, standard FetchContent trade-off, not a defect
- [x] [Review][Defer] AD-6 guard only inspects immediate LINK_LIBRARIES, not transitive dependencies of linked libs [cmake/fbsampler_guards.cmake:8-9] — deferred, no current dependency exercises this gap
- [x] [Review][Defer] sfizz patch's arm64/aarch64 exclusion regex is platform-agnostic though intended for macOS [cmake/patches/sfizz-1.2.3-fixes.patch] — deferred, untested path (no ARM Linux in CI matrix)
