# Deferred Work

## Deferred from: code review of story 1-1-buildable-skeleton-on-three-platforms (2026-07-17)

- AppleClang atomic_queue suppression only scoped to `APPLE AND Clang`, not exercised elsewhere in CI matrix [cmake/fbsampler_deps.cmake:47] — a future Linux/Windows build using upstream Clang would hit the same `-Wmissing-template-arg-list-after-template-kw` error with no suppression path. Not currently hit since ubuntu-latest uses GCC.
- sfizz `FetchContent` `UPDATE_DISCONNECTED ON` risks stale vendored source if the patch file changes without clearing the build cache [cmake/fbsampler_deps.cmake:26] — standard FetchContent trade-off; a dev/CI machine could silently keep old (or differently patched) sfizz sources after a patch-file edit unless `_deps` is cleared.
- AD-6 guard (`fbsampler_assert_no_gui_modules`) only inspects `sampler-core`'s own LINK_LIBRARIES/INTERFACE_LINK_LIBRARIES, not transitive dependencies of linked libraries [cmake/fbsampler_guards.cmake:8-9] — if a dependency of `sampler-core` (e.g. sfizz) ever pulled in a GUI toolkit transitively, this guard would not catch it. No current dependency exercises this gap.
- sfizz patch's arm64/aarch64 exclusion regex is platform-agnostic though intended for macOS CI [cmake/patches/sfizz-1.2.3-fixes.patch] — the condition would also apply if Linux ARM64 were ever added to the CI matrix (currently x86_64-only); behavior on that path is unverified.
