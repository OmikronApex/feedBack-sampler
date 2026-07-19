# Deferred Work

## Deferred from: code review of story 1-1-buildable-skeleton-on-three-platforms (2026-07-17)

- AppleClang atomic_queue suppression only scoped to `APPLE AND Clang`, not exercised elsewhere in CI matrix [cmake/fbsampler_deps.cmake:47] — a future Linux/Windows build using upstream Clang would hit the same `-Wmissing-template-arg-list-after-template-kw` error with no suppression path. Not currently hit since ubuntu-latest uses GCC.
- sfizz `FetchContent` `UPDATE_DISCONNECTED ON` risks stale vendored source if the patch file changes without clearing the build cache [cmake/fbsampler_deps.cmake:26] — standard FetchContent trade-off; a dev/CI machine could silently keep old (or differently patched) sfizz sources after a patch-file edit unless `_deps` is cleared.
- AD-6 guard (`fbsampler_assert_no_gui_modules`) only inspects `sampler-core`'s own LINK_LIBRARIES/INTERFACE_LINK_LIBRARIES, not transitive dependencies of linked libraries [cmake/fbsampler_guards.cmake:8-9] — if a dependency of `sampler-core` (e.g. sfizz) ever pulled in a GUI toolkit transitively, this guard would not catch it. No current dependency exercises this gap.
- sfizz patch's arm64/aarch64 exclusion regex is platform-agnostic though intended for macOS CI [cmake/patches/sfizz-1.2.3-fixes.patch] — the condition would also apply if Linux ARM64 were ever added to the CI matrix (currently x86_64-only); behavior on that path is unverified.

## Deferred from: code review of 1-3-sfz-frontend-lowers-real-instruments (2026-07-18)

- Sanitizer CI job passes sanitizer flags only via `CMAKE_*_FLAGS` — no linker flags and no canary check proving instrumentation is actually active; a toolchain quirk could silently produce an unsanitized-but-green job (.github/workflows/ci.yml:64-74).
- Model-validation diagnostics re-emitted through `lowerImpl` carry no source locations; if the "unreachable" clamp logic ever misses, users get raw `region.*` codes with empty locations (core/frontends/sfz/sfz_frontend.cpp:517-522).

## Deferred from: implementation of 1-4-the-engine-makes-sound (2026-07-19)

- **Synthetic-SFZ bind seam (flagged for replacement, AD-1):** the engine feeds sfizz via generated in-memory SFZ text (`core/engine/model_to_sfz.cpp`) because sfizz 1.2.3's region construction is welded to its parser. Caps model fidelity at what SFZ text can express (mod-matrix `curve`, Velocity/KeyTrack matrix entries, per-entry control-ID routing are not lowered). Must be replaced with direct region construction (likely a local sfizz patch exposing a programmatic hook) before SF2-extension work in Epic 2.
- **Double sample residency in v0:** the AD-2 pool pins and holds float32 sample data, and sfizz's internal FilePool loads its own copy (full preload). Samples are resident twice. Epic 5 unifies residency behind the fbsampler pool interface when streaming lands (core/engine/engine.cpp `Engine::load`).
- **File-I/O detection is seam-based, not syscall-level:** the RT detector reports allocation (global new/delete hooks, test binary) and locks (CheckedMutex) exhaustively, but file I/O only at fbsampler's own loading seams (`readWavFile`, pool acquire). A hypothetical direct OS-level read inside a dependency's render path would not be caught; syscall-level interposition (strace/ETW-based) could be added to a sanitizer-style CI job later.
- **All-RAM pool slot table is fixed at 4096 entries** (`core/pool/all_ram_pool.cpp kMaxEntries`) to keep audio-thread indexing lock-free without hazard pointers; exceeding it yields `pool.capacity_exceeded`. Revisit with the Epic 5 streaming pool.

## Deferred from: code review of story 1-4-the-engine-makes-sound (2026-07-19)

- `Engine::~Impl()` releases pool handles via `~EngineSnapshot()` from whatever thread destroys the `Engine`, with no assertion that this is never the audio thread — pre-existing generic RAII/lifetime contract shared by most such engines, not unique to this diff [core/engine/engine.cpp:72-75].
- `appendf()`'s fixed 256-byte buffer silently truncates on `vsnprintf` overflow with no return-value check — unreachable with current fixed small-value format strings, but a landmine if the format strings ever grow [core/engine/model_to_sfz.cpp:12-20].
- `toFrames()` silently clamps negative offset/loop values to 0 with no diagnostic — relies entirely on upstream Story 1.3 model validation never letting a negative value through [core/engine/model_to_sfz.cpp:22-28].
- `Engine::prepare()` called after `load()` with a genuinely different sample rate/block size mid-instrument is documented as supported behavior but has no test exercising it [core/engine/engine.h, core/engine/engine.cpp:87-99].

## Deferred from: code review of story 1-5-play-it-live-in-a-daw (2026-07-19)

- `validate.cpp` doesn't enforce bend sign convention (`bendUpCents`/`bendDownCents` only magnitude-checked, not sign) — confirmed by author as intentional: asymmetric/inverted bend ranges are a legitimate use case, not a bug [core/model/validate.cpp:75-79].
- `Engine::load()`'s bind-failure path doesn't early-exit on the first `bindFailed` region — keeps calling `pool->acquire()` (full file reads) for every remaining region even after the load is already known to fail, wasting I/O on the background thread on every retry of a bad load [core/engine/engine.cpp:128-141].
- `Engine::process()` has no `numFrames == 0` guard — with `numEvents > 0` and `numFrames == 0`, events still dispatch to sfizz before a zero-length `renderBlock` call; unreachable via the current real caller (`plugin_processor.cpp` guards `numFrames > 0`) but a latent gap in the public API contract for any future/test caller [core/engine/engine.cpp:175-217].
- `AllRamSamplePool` slot reuse (`entries_[slot] = std::move(entry)`) replaces the `Entry` without a lock while `info()`/`residentChannel()` read `entries_[handle-1].get()` unlocked — currently safe only because `acquire()` never selects a slot with `refCount > 0`, and the audio thread only ever holds handles pinned by the live `EngineSnapshot`, so no live/pinned slot is ever replaced concurrently. Worth an explicit invariant comment or assert guarding this [core/pool/all_ram_pool.cpp:43-89].

## Deferred from: code review of story 1-6-the-corpus-starts-measuring (2026-07-19)

- Corpus references are self-captures from the engine itself, not Sforzando-oracle captures, so the corpus can currently only catch regressions, never initial fidelity gaps against the oracle — already documented as a user-approved interim decision in the story's Completion Notes; re-surfaced for visibility only, not a new finding [corpus/README.md, corpus/manifest.json `reference_provenance`].
- `Engine::prepare()` mutates the live synth while documenting (but not asserting) that it never runs concurrently with `process()` — same unenforced-contract class as the engine-snapshot reclamation decision raised in this review; deferred pending that redesign rather than patched standalone [core/engine/engine.cpp:87-98].
- RT-safety detector (`CheckedMutex`/`rtcheck`) only reports violations via an atomic counter and depends on the test binary's allocation hooks; the header doc doesn't foreground that production builds have no enforcement for the allocation class — documentation-accuracy gap, not a functional regression in this diff [core/include/fbsampler/detail/rt_check.h:19-26].
- Corpus CI job fetches pinned assets from live GitHub raw URLs on every uncached run with no retry/fallback beyond the manifest-hash cache — accepted network dependency, documented in README [.github/workflows/ci.yml corpus job].
- `midi_file.cpp`'s alien-chunk handling increments a `uint16_t trackCount` that can wrap at 65535 alien chunks, silently truncating remaining tracks — real in theory, practically unreachable for the small, hand-authored corpus/test MIDI files this parser processes [tests/render/midi_file.cpp:208-215].
- `tests/render/wav_io.cpp` test-fixture writer doesn't assert equal channel-array lengths (OOB read) or guard NaN samples bypassing the int16 clamp (UB on cast) — confined to internal test-fixture generation with controlled inputs, not shipped/production code [tests/render/wav_io.cpp:33-58,96-101].
- No structured field records *why* a per-entry threshold override was applied (Dev Notes ask for a recorded rationale); currently moot since none of the 3 corpus entries use an override [corpus/manifest.json schema].
- Corpus manifest has no explicit numeric/ordinal `weight` field (Task 1 literally asks for "weight/rationale ('weighted by real-library usage')"); decided during this review that the free-text `rationale` field satisfies intent at the current 3-entry scale, since a `weight` field with no consumer in `run_corpus.py`'s aggregation would be speculative schema. Revisit once the corpus grows large enough (Epic 2/4) that per-format aggregation needs real weighting [corpus/manifest.json schema].
