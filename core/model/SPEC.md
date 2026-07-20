# Canonical Model Spec — v0

**Schema version: 5** (`fbsampler::kModelSchemaVersion`, stamped into every serialized artifact)

This is the written contract for `fbsampler::InstrumentModel` (AD-11). Every frontend (SFZ,
SoundFont, Decent Sampler, ...) lowers its own format into this one representation; the engine
consumes only this representation. No frontend or the engine may special-case another frontend's
quirks — everything format-specific is resolved during lowering, before the model exists.

Modeled as an SFZ-superset (per the addendum): regions are data, the engine's voices are
execution. Field names and defaults mirror `sfz::Region` in sfizz (pinned at 1.2.3 via FetchContent; upstream
`src/sfizz/Region.h`, <https://github.com/sfztools/sfizz>) wherever v0 needs the same concept, so Story 1.4's
engine binding is a translation, not an impedance fight.

## Units (fixed, spine AD-11)

| Quantity        | Unit                    | Notes                                            |
|-----------------|-------------------------|---------------------------------------------------|
| Pitch / tuning  | **cents**               | 100 cents = 1 semitone                             |
| Gain             | **dB**                  | 0 dB = unity                                       |
| Envelope time    | **seconds**             | delay/attack/hold/decay/release                    |
| Sample positions | **`positionUnit`-tagged** | offset/loop points, in `Region::positionUnit` (`Frames` or `Seconds`) |
| Curves           | **normalized 0..1**     | 0 = linear default unless the field says otherwise |
| Audio samples     | **float32**            | engine detail, not part of the model               |

Sample-position fields (`offset`, `loopStart`, `loopEnd`) carry an explicit unit tag
(`SamplePositionUnit`) because source formats disagree: SFZ expresses them in sample frames,
other formats in seconds. Frontends record positions **exactly as the source format expresses
them** — they must not guess a sample rate to convert (frontends never open sample files, AD-2).
The engine converts to frames/seconds at bind time (Story 1.4), when the file's real rate is
known. Schema v1 stored these fields as assumed-rate seconds; v2 replaced that with the tagged
representation.

## v0 scope

v0 carries what SFZ v1 lowering (Story 1.3) and the engine (Story 1.4) require: regions with
key/velocity zones, sample reference, loop points, offset, tuning, a single amplitude ADSR
envelope, and a modulation-matrix skeleton. SF2 primitives (additional curve types, additional
modulation source semantics) are **reserved extension points**, not implemented in v0 — adding
them later must not change the meaning of any v0 field.

## Types

### `InstrumentModel`

| Field           | Type                        | Default                  |
|-----------------|-----------------------------|---------------------------|
| `schemaVersion` | `int`                       | `kModelSchemaVersion` (1) |
| `name`          | `string`                    | `""`                      |
| `regions`       | `vector<Region>`             | empty                     |
| `controls`      | `vector<ControlMapEntry>`    | empty                     |

### `Region`

| Field                 | Type       | Default | Notes |
|-----------------------|------------|---------|-------|
| `sampleFile`           | `string`   | `""`    | sample reference (see below); required (non-empty) |
| `loKey` / `hiKey`      | `uint8_t`  | 0 / 127 | inclusive MIDI key range |
| `loVelocity` / `hiVelocity` | `uint8_t` | 0 / 127 | inclusive MIDI velocity range |
| `rootKey`              | `uint8_t`  | 60      | MIDI key that plays the sample at its native pitch |
| `tuningCents`          | `float`    | 0       | fine tune, cents |
| `gainDb`               | `float`    | 0       | region gain, dB |
| `pan`                  | `float`    | 0       | normalized -1 (full left) .. 1 (full right) |
| `bendUpCents`          | `float`    | 200     | pitch-bend range at full bend up, cents (SFZ `bend_up` default) |
| `bendDownCents`        | `float`    | -200    | pitch-bend range at full bend down, cents (typically negative) |
| `positionUnit`         | `SamplePositionUnit` | `Frames` | unit of `offset` / `loopStart` / `loopEnd` |
| `offset`               | `double`   | 0       | start offset into the sample, in `positionUnit` |
| `loopEnabled`          | `bool`     | false   | |
| `loopStart` / `loopEnd` | `double`  | 0 / 0   | in `positionUnit`; ordering only meaningful when `loopEnabled` |
| `amplitudeEnvelope`    | `EnvelopeADSR` | see below | |
| `modMatrix`            | `vector<ModMatrixEntry>` | empty | regional modulation connections |

#### Sample references (schema v4)

`sampleFile` is either a plain path **relative to the instrument root** (schema v1–3 meaning,
unchanged), or — since schema v4 — a **container-sample URI** of the form

```
sf2://<container-path>#<sampleIndex>
```

referencing the `<sampleIndex>`-th `shdr` sample record (0-based) embedded inside the SoundFont
at `<container-path>`. The pool resolves both forms at `acquire()` time (AD-2: only the pool
touches sample bytes); frontends record the reference and shdr metadata only — embedded samples
are **never** extracted to temp files. Region sample-position fields (`offset`, loop points) for
container references are frames **relative to the referenced sample's start**, not to the
container's sample-data chunk.

### `EnvelopeADSR`

| Field            | Type    | Default | Notes |
|------------------|---------|---------|-------|
| `delaySeconds`   | `float` | 0       | must be >= 0 |
| `attackSeconds`  | `float` | 0       | must be >= 0 |
| `holdSeconds`    | `float` | 0       | must be >= 0 |
| `decaySeconds`   | `float` | 0       | must be >= 0 |
| `sustainLevel`   | `float` | 1       | normalized 0..1 |
| `releaseSeconds` | `float` | 0       | must be >= 0 |

### `ModSource` / `ModTarget` / `ModMatrixEntry`

Since schema v5 the matrix carries the full SF2 modulator shape (SoundFont 2.04 §9.5):
`output = curve(source) x curve(amountSource) x depth`, applied to `target`.

`ModSourceKind` is one of `None`, `Cc`, `Velocity`, `KeyTrack`, `PolyPressure`,
`ChannelPressure`, `PitchWheel`, `PitchWheelSensitivity`. `None` is only legal for an
**amount** source (it means "no scaling"); a primary source of `None` fails validation.
`ModSource::ccNumber` is only meaningful when `kind == Cc`.

#### `ModSource`

| Field       | Type           | Default   | Notes |
|-------------|----------------|-----------|-------|
| `kind`      | `ModSourceKind`| `Velocity`| |
| `ccNumber`  | `uint8_t`      | 0         | only when `kind == Cc` |
| `maxToMin`  | `bool`         | false     | direction: false = min→max, true = max→min (input inverted before the curve) |
| `bipolar`   | `bool`         | false     | polarity: false = curve output 0..1, true = -1..1 |
| `curve`     | `ModCurveType` | `Linear`  | shape, see below |

#### Curve shapes (`ModCurveType`, spec §9.5.2)

With normalized input `x` in [0, 1] (after direction inversion):

- `Linear`: `f(x) = x`
- `Concave`: `f(x) = -20/96 * log10(((127 - 127x)^2) / 127^2)` clamped to [0, 1] — slow start,
  the SF2 attenuation-controller shape (FluidSynth `fluid_conv.c` is the oracle-matching
  reference implementation)
- `Convex`: the mirror, `f(x) = 1 - Concave(1 - x)`
- `Switch`: `f(x) = 0` for `x < 0.5`, `1` otherwise

Bipolar maps the unipolar output `u` to `2u - 1`.

`ModTarget` is `Gain` (depth in dB), `Pitch` (depth in cents), `Pan` (depth normalized -1..1),
plus the reserved targets `FilterCutoff` (cents), `ReverbSend`, `ChorusSend` (normalized 0..1)
which lower and serialize but which the engine does not execute yet — the engine emits a
diagnostic when it drops one at bind time. Existing target semantics never change.

#### `ModMatrixEntry`

| Field               | Type         | Default              | Notes |
|---------------------|--------------|-----------------------|-------|
| `sourceControlId`    | `string`     | `""`                  | references a `ControlMapEntry::id`; empty means the source is a built-in, not a mapped control |
| `source`             | `ModSource`  | `{Velocity}`          | primary source |
| `amountSource`       | `ModSource`  | `{None}`              | secondary (amount) source; `None` = depth applied unscaled |
| `target`             | `ModTarget`  | `Gain`                | |
| `depth`              | `float`      | 0                     | unit depends on `target` (see above) |

### `ControlMapEntry`

Every library-defined control gets a stable identity minted at lowering time (AD-8: format-defined
identity — SFZ CC number/label, DS control id/name; never a list position) plus accessibility
metadata that survives lowering (AD-11).

| Field            | Type     | Default | Notes |
|------------------|----------|---------|-------|
| `id`             | `string` | `""`    | stable control ID, unique within the model; required (non-empty) |
| `displayName`    | `string` | `""`    | UI label |
| `accessibleName` | `string` | `""`    | screen-reader label |

### `Diagnostic`

The one error shape for the whole core API (spine Consistency Conventions). No exceptions cross
the core API; every fallible operation returns `std::vector<Diagnostic>` (empty = success).

| Field       | Type             | Notes |
|-------------|------------------|-------|
| `severity`  | `Severity`       | `Error` or `Warning` |
| `code`      | `string`         | stable, dotted, machine-checkable (e.g. `region.key_range_invalid`) |
| `message`   | `string`         | human-readable |
| `location`  | `SourceLocation` | `{file, line, column}`; `line`/`column` are `-1` and `file` is empty when the model was constructed in-memory rather than parsed from a text source — frontends (Story 1.3+) populate this from the original file |

## Validation (`validate(const InstrumentModel&) -> vector<Diagnostic>`)

Returns one `Diagnostic` per violation found (does not stop at the first). Codes are stable and
covered one-to-one by Catch2 tests. Range-constrained float fields must be **finite and in
range** — NaN or infinity fails the same code as an out-of-range value (NaN compares false
against every bound, so checks assert in-range rather than test out-of-range):

- `model.schema_version_mismatch` — `schemaVersion != kModelSchemaVersion`
- `model.duplicate_control_id` — two `controls` entries share the same non-empty `id`
- `region.sample_file_missing` — `sampleFile` is empty
- `region.key_range_invalid` — `loKey > hiKey`
- `region.key_out_of_midi_range` — `hiKey` or `rootKey` outside the MIDI range `[0, 127]`
- `region.velocity_range_invalid` — `loVelocity > hiVelocity`
- `region.velocity_out_of_midi_range` — `hiVelocity` outside the MIDI range `[0, 127]`
- `region.pan_out_of_range` — `pan` not finite or outside `[-1, 1]`
- `region.gain_not_finite` — `gainDb` is NaN or infinite
- `region.tuning_not_finite` — `tuningCents` is NaN or infinite
- `region.bend_range_out_of_range` — `bendUpCents` or `bendDownCents` not finite or outside `[-9600, 9600]`
- `region.offset_negative` — `offset` not finite or `< 0`
- `region.loop_range_invalid` — a loop bound is not finite or `< 0` (checked even when
  `loopEnabled` is false, so a bad bound cannot lie dormant until the loop is enabled), or
  `loopEnabled` and not `loopStart < loopEnd`
- `region.envelope_time_negative` — any of the envelope's time fields is not finite or negative
- `region.envelope_sustain_out_of_range` — `sustainLevel` not finite or outside `[0, 1]`
- `region.mod_depth_not_finite` — a `modMatrix` entry's `depth` is NaN or infinite
- `region.mod_source_none` — a `modMatrix` entry's primary `source.kind` is `None`
- `region.mod_cc_out_of_midi_range` — a `modMatrix` entry's `source.ccNumber > 127` (or `amountSource.ccNumber > 127`) when the respective `kind == Cc`
- `region.mod_source_unknown_control` — a `modMatrix` entry's `sourceControlId` is non-empty but does not match any `controls[i].id`
- `control.id_missing` — a `ControlMapEntry::id` is empty

Note (`loKey`/`loVelocity`): a lo bound above 127 always trips either the corresponding
`*_range_invalid` (lo > hi) or the `*_out_of_midi_range` code (hi must then also exceed 127), so
no separate lo-bound code is needed.

## Serialization (golden files)

`serializeModel()` / `parseModel()` produce a deterministic, byte-stable text dump — for golden
snapshot tests only, **not** plugin state (Epic 6 owns plugin state chunks separately).

- Fixed field order (declaration order above), one `key=value` line per field; array fields use
  `container[i].field=value` keys with an explicit `..._count=N` line before the elements.
  String values are escaped (`\` → `\\`, newline → `\n`, carriage return → `\r`) so a dump never
  contains a raw CR or a value-embedded newline.
- Floats are written with `std::to_chars` (shortest round-trip, locale-independent) — never
  `std::to_string`/iostream default formatting, both of which are locale- and precision-dependent.
- Always `\n` line endings, regardless of platform.
- First line is always `schema_version=<N>`.
- `parseModel()` is **strict**: every declared field present exactly once, every value cleanly
  parseable (full-string `std::from_chars`, exact enum tokens, bools only `0`/`1`, float/double
  values finite — the tokens `nan`/`inf` are rejected), counts capped sanely, and no unknown or
  duplicate keys. Malformed input returns `false`; it is never silently defaulted. Semantic
  checks (schema version match, value ranges) remain `validate()`'s job.

This is the format Story 1.3's per-frontend golden harness extends.

## Frontend robustness policy (normative)

Frontends repair rather than reject: every structurally-parseable source file must lower to a
model that passes `validate()`, with each repair surfaced as a warning diagnostic (AD-1: tracked,
never silent). This intentionally diverges from typical player behavior (which silently ignores
malformed regions) — the product goal is that the instrument loads and reports, not that it
byte-mimics other players' error handling. Concretely, for the SFZ frontend:

- Out-of-range values are **clamped** to the nearest legal value (`sfz.value_clamped`) — MIDI
  keys/velocities to `[0, 127]`, `pan` to `[-100, 100]`, `ampeg_sustain` to `[0, 100]`,
  `transpose` to `[-127, 127]` semitones, `tune` to `[-9600, 9600]` cents (the tuning clamps
  also keep the derived `tuningCents` finite for any finite input).
- Swapped ranges are **repaired** (`sfz.key_range_swapped` / `sfz.velocity_range_swapped`).
- Impossible loops are **disabled** (`sfz.loop_range_invalid`).
- Regions without a sample are **dropped** (`sfz.region_missing_sample`).
- Unparseable opcode values keep the current (possibly scope-inherited) value
  (`sfz.invalid_opcode_value`) — a failed opcode never resets inherited state.
- Numeric opcodes accept any parseable number even where the SFZ spec is stricter (e.g. a
  fractional `transpose` is honored as fractional semitones rather than rejected).

For the SF2 frontend (Story 2.1) the same policy applies at the zone level: structural
(chunk-level, SoundFont 2.04 §10) violations are `Error`s that suppress the model, but every
structurally-parseable file lowers — out-of-range generator values are clamped
(`sf2.value_clamped`), zones whose sample bounds fall outside the sample-data chunk are dropped
(`sf2.sample_bounds_invalid`), unexpressible generators are per-generator warnings
(`sf2.generator_unsupported`), and modulators are tracked as `sf2.modulator_unsupported`
until Story 2.2.
