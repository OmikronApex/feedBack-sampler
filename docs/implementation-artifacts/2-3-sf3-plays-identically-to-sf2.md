---
baseline_commit: 2eed42351d273c3bcfcfc322315d8d4805e7e055
---

# Story 2.3: SF3 plays identically to SF2

Status: review

## Story

As a soundfont user,
I want Vorbis-compressed SF3 files to behave exactly like their SF2 equivalents,
so that my compressed soundfonts are first-class.

## Acceptance Criteria

1. **Given** an SF3 file and its SF2 equivalent, **when** both are lowered and rendered, **then** Vorbis decodes to PCM at load time (never on the audio thread), golden snapshots are structurally identical, and renders match within thresholds.

## Tasks / Subtasks

- [x] Task 1: SF3 detection and parsing (AC: 1)
  - [x] SF3 = SF2 container with `ifil` version 3.x and/or `sampleType` OR'd with `SF_SAMPLETYPE_VORBIS` (0x10); sdta holds concatenated Ogg Vorbis streams; shdr start/end are BYTE offsets into sdta for compressed samples (not frame indices), and startloop/endloop are frame offsets relative to the decoded sample start (FluidSynth wiki SoundFont3Format / MuseScore sf3convert conventions — the de-facto spec; no official one exists)
  - [x] Frontend (`core/frontends/sf2/`, same code path — sf3 is a flag, not a new frontend per spine Structural Seed) marks the sample reference as Vorbis-compressed; all hydra/generator/modulator lowering is byte-identical to SF2
- [x] Task 2: Vorbis decode in the pool at load time (AC: 1)
  - [x] Vendor **stb_vorbis** (public domain, single file — addendum licensing matrix; libvorbis acceptable fallback if stb hits a decode gap, decide once and record) under `core/pool/` or `cmake/` third-party convention
  - [x] The pool's embedded-sf2 reader (built in Story 2.1) decodes the Ogg stream to float32 PCM at `acquire()` time — control/loader thread only, upstream of everything RT (AD-2: "SF3 Vorbis is decoded to PCM at load/preload — upstream of the pool SF3 == SF2"). After acquire, `SampleInfo`/`residentChannel` are indistinguishable from an SF2-sourced sample
  - [x] Decode failure → `Diagnostic` (`sf3.vorbis_decode_failed`), `kInvalidSampleHandle`, no crash; truncated/corrupt Ogg data must not read out of bounds (stb_vorbis pushdata/memory API with explicit lengths)
- [x] Task 3: Structural-identity goldens (AC: 1)
  - [x] Build fixture pairs: take the Story-2.1 mini .sf2 fixtures, produce .sf3 siblings with a checked-in converter step (extend the fixture generator; encode with a pinned quality setting) — document regeneration
  - [x] Golden test asserts the .sf3 lowering snapshot is IDENTICAL to the .sf2 sibling's, modulo the sample-reference field (compressed flag/byte ranges differ by definition — normalize or exclude that field in the comparison and assert everything else byte-equal)
- [x] Task 4: Render-identity tests (AC: 1)
  - [x] Render the sf2/sf3 fixture pair through the offline harness with identical MIDI; diff sf3 output against sf2 output within NFR-5 thresholds (Vorbis is lossy — bit-equality is impossible; the pair-diff threshold may need a documented per-pair override vs. the corpus defaults, rationale recorded)
  - [x] RT-safety: run the existing debug allocation/lock detector over an SF3 render — proves no decode leaks onto the audio thread
- [x] Task 5: Fuzzer coverage (AC: 1)
  - [x] Extend the SF2 fuzz corpus with SF3 seeds (valid mini-sf3, truncated Ogg mid-stream, sampleType flag without Vorbis data, Vorbis data with lying shdr byte ranges); the pool decode path must be reachable from the fuzz entry (fuzz through lower + acquire, not lower alone — the decoder is the new attack surface)

## Dev Notes

- **Dependency: Story 2.1** (SF2 parser + embedded-sample pool reader) is the substrate; this story adds one decode step inside the pool reader plus detection in the frontend. Story 2.2 is NOT a dependency (modulators are format-orthogonal), but if 2.2 landed first, its render fixtures gain sf3 siblings cheaply.
- **The whole point is equivalence upstream of the pool.** Nothing downstream — engine, matrix, corpus runner — may know SF3 exists. If you find yourself branching on "is sf3" anywhere outside the frontend's sample-reference construction and the pool's decode, the design is drifting; stop and fix.
- **Read before modifying:** `core/pool/all_ram_pool.cpp` (acquire path, 4096-slot table, slot-reuse invariant comment from the 1.6 review — do not disturb the epoch-gated reclamation assumptions), `core/pool/wav_reader.cpp` (existing reader shape the sf2/sf3 reader sits beside), `core/frontends/sf2/` as built by 2.1, `tests/golden/sf2/` fixture generator.
- **stb_vorbis integration:** use the memory API (`stb_vorbis_open_memory`) on the extracted sdta byte range; decode whole-sample to float32 (all-RAM v0 — Epic 5 streaming revisits, but compressed samples will likely stay decode-at-load even then). Compile it in exactly one TU with `STB_VORBIS_HEADER_ONLY` discipline to avoid ODR issues. License note for NFR-6: public domain / MIT dual — record in whatever third-party manifest exists.
- **Sample-rate care:** decoded rate comes from the Ogg header; shdr `dwSampleRate` should agree but the Ogg header wins on conflict (emit `sf3.sample_rate_mismatch` warning). Loop points are frames-in-decoded-PCM per sf3convert convention — verify against a MuseScore-converted real file, not just our own fixtures (circular-fixture risk: our encoder and decoder agreeing proves little; one externally-produced sf3, e.g. a MuseScore-shipped soundfont excerpt with compatible license, belongs in the test set or at minimum in 2.5's corpus slice).
- **CI runtime:** Vorbis decode of tiny fixtures is milliseconds; no new CI job needed — existing test + sanitizer jobs pick everything up via ctest.
- **Testing standards:** Catch2; golden byte-compare via existing `serializeModel`; render diff via existing wav diff machinery (`tests/render/`); fuzz replay ctest target pattern from 1.3/2.1.

### Project Structure Notes

- New: vendored `stb_vorbis` (single header + one TU), SF3 fixture siblings under `tests/golden/sf2/` (same dir — same frontend), fuzz seeds under `tests/fuzz/sf2/corpus/`.
- Modified: `core/frontends/sf2/` (detection), `core/pool/` (decode in embedded reader), `tests/CMakeLists.txt`, fixture generator, `corpus/README.md` if conventions documented there.
- No new frontend directory — spine Structural Seed says `frontends/sf2/ (sf3 = sf2 + vorbis decode)`.

### References

- [Source: docs/planning-artifacts/epics.md#Story-2.3]
- [Source: docs/planning-artifacts/architecture/architecture-feedBack-sampler-2026-07-17/ARCHITECTURE-SPINE.md#AD-2 (decode at load, upstream of pool SF3 == SF2), #Structural-Seed]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md#FR-3, #NFR-1]
- [Source: docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/addendum.md#Engine-strategy (stb_vorbis or libvorbis), #Licensing-matrix, #Format-reference-sources (FluidSynth wiki SoundFont3Format + sf3convert)]
- [Source: docs/implementation-artifacts/2-1-sf2-structure-lowers-to-the-canonical-model.md (embedded-sample reference scheme this builds on)]

## Dev Agent Record

### Agent Model Used

Claude Fable 5 (claude-fable-5)

### Debug Log References

- Full ctest suite: 119/119 pass locally (115 at story start; +4 new).
- `sf2-fuzz-replay`: 11 seeds (incl. 3 SF3 seeds) × mutations, no crash; the fuzz entry now exercises the pool decode path (`readSf2SampleFromMemory`) as well as lowering.

### Completion Notes List

- **Frontend detection is a flag, not a fork:** `sampleType & 0x10` marks Vorbis; the only SF3-specific frontend behavior is (a) shdr start/end validated as BYTE offsets into sdta, (b) startloop/endloop taken as frames relative to the decoded sample start (loop rebase skipped), (c) decoded-length-dependent clamps skipped (the pool owns decode). All hydra/generator/modulator lowering is byte-identical to SF2 — proven by the structural-identity golden (sf3 snapshot == sf2 snapshot after normalizing only the container extension in the URI).
- **stb_vorbis decision (recorded):** stb_vorbis v1.22 chosen (public domain/MIT dual per the addendum licensing matrix), NOT libvorbis. Twist discovered at link time: sfizz's vendored st_audiofile already compiles stb_vorbis v1.22 with external linkage, so defining it again is an ODR/LNK2005 clash — our TU includes the vendored header with `STB_VORBIS_HEADER_ONLY` (declarations only) and links sfizz's definitions. The vendored `core/pool/stb_vorbis.h` pins the declaration set at v1.22 independent of sfizz's include layout; versions verified identical.
- **Decode at acquire() only (AD-2):** `readSf2Sample` branches on the shdr Vorbis flag and decodes the byte range via `stb_vorbis_open_memory` to float32 (any channel count) on the control/loader thread. After acquire, `SampleInfo`/`residentChannel` are indistinguishable from SF2. Ogg header sample rate wins over a conflicting shdr rate (`sf3.sample_rate_mismatch` warning). Decode failures → `sf3.vorbis_decode_failed`, `kInvalidSampleHandle`, no crash.
- **Nothing downstream knows SF3 exists:** engine, seam, matrix, pool URI scheme unchanged — same `sf2://<path>#<index>` reference; the pool reads the Vorbis flag from the container itself.
- **Fixtures:** `build_sf3` sibling builder in make_sf2.py (ffmpeg/libvorbis at `-q:a 6`, regeneration-time dependency only; encoded bytes checked in); basic.sf3 + oracle.sf3 generated. Render-identity thresholds (windowed RMS < 1e-2, peak diff < 6e-2 vs corpus defaults) documented in-test with rationale (codec noise + up-to-one-frame decode alignment).
- **RT-safety:** SF3 render under `markRtSections` green — decode never touches the audio thread.
- **Externally-produced SF3 validation deferred to 2.5** (per dev note: "at minimum in 2.5's corpus slice") — our encoder/decoder agreeing is circular; a MuseScore-converted file belongs in the corpus slice. Recorded in deferred-work.md.

### File List

- core/pool/stb_vorbis.h (new, vendored v1.22 — declarations pin)
- core/pool/sf2_sample_reader.h / .cpp (modified: memory entry point, Vorbis decode branch)
- core/frontends/sf2/sf2_frontend.cpp (modified: Vorbis flag, byte-offset bounds, relative loops)
- tests/golden/sf2/tools/make_sf2.py (modified: encode_vorbis + build_sf3 + sf3 fixtures/seeds)
- tests/golden/sf2/basic.sf3, oracle.sf3 (new, generated)
- tests/golden/sf2_golden_test.cpp (modified: structural-identity test)
- tests/sf2_frontend_test.cpp (modified: pool SF3 decode test)
- tests/render/sf3_render_test.cpp (new: render-identity + RT tests)
- tests/render/fixtures/sf2/oracle.sf3 (new, generated)
- tests/fuzz/sf2/sf2_fuzzer.cpp (modified: decode-path coverage)
- tests/fuzz/sf2/corpus/seed_basic.sf3, seed_sf3_truncated_ogg.sf3, seed_sf3_lying_flag.sf3 (new)
- tests/CMakeLists.txt (modified: sf3 test source, fuzz include dirs)
- .gitattributes (modified: sf3/fixture binary attrs)
- docs/implementation-artifacts/deferred-work.md (modified: 2.3 deferral)

## Change Log

- 2026-07-19: Story 2.3 implemented — SF3 detection in the SF2 frontend (flag, byte offsets, decoded-relative loops), stb_vorbis decode in the pool at acquire (linking sfizz's compiled v1.22), structural-identity goldens, render-identity + RT tests, SF3 fuzz seeds with decoder-reachable fuzz entry. 119/119 tests.
