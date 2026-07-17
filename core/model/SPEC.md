# Canonical Model Spec — v0

**Schema version: 1** (`fbsampler::kModelSchemaVersion`, stamped into every serialized artifact)

This is the written contract for `fbsampler::InstrumentModel` (AD-11). Every frontend (SFZ,
SoundFont, Decent Sampler, ...) lowers its own format into this one representation; the engine
consumes only this representation. No frontend or the engine may special-case another frontend's
quirks — everything format-specific is resolved during lowering, before the model exists.

Modeled as an SFZ-superset (per the addendum): regions are data, the engine's voices are
execution. Field names and defaults mirror `sfz::Region` in vendored sfizz (see
`build/_deps/sfizz-src/src/sfizz/Region.h`) wherever v0 needs the same concept, so Story 1.4's
engine binding is a translation, not an impedance fight.

## Units (fixed, spine AD-11)

| Quantity        | Unit                    | Notes                                            |
|-----------------|-------------------------|---------------------------------------------------|
| Pitch / tuning  | **cents**               | 100 cents = 1 semitone                             |
| Gain             | **dB**                  | 0 dB = unity                                       |
| Time             | **seconds**             | delay/attack/hold/decay/release/offset/loop points |
| Curves           | **normalized 0..1**     | 0 = linear default unless the field says otherwise |
| Audio samples     | **float32**            | engine detail, not part of the model               |

Time-in-samples is an *engine* concept (Story 1.4 converts seconds → samples using the sample
file's rate at bind time). The model itself never stores a sample-frame count.

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
| `sampleFile`           | `string`   | `""`    | path relative to the instrument root; required (non-empty) |
| `loKey` / `hiKey`      | `uint8_t`  | 0 / 127 | inclusive MIDI key range |
| `loVelocity` / `hiVelocity` | `uint8_t` | 0 / 127 | inclusive MIDI velocity range |
| `rootKey`              | `uint8_t`  | 60      | MIDI key that plays the sample at its native pitch |
| `tuningCents`          | `float`    | 0       | fine tune, cents |
| `gainDb`               | `float`    | 0       | region gain, dB |
| `pan`                  | `float`    | 0       | normalized -1 (full left) .. 1 (full right) |
| `offsetSeconds`        | `float`    | 0       | start offset into the sample |
| `loopEnabled`          | `bool`     | false   | |
| `loopStartSeconds` / `loopEndSeconds` | `float` | 0 / 0 | only meaningful when `loopEnabled` |
| `amplitudeEnvelope`    | `EnvelopeADSR` | see below | |
| `modMatrix`            | `vector<ModMatrixEntry>` | empty | regional modulation connections |

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

`ModSourceKind` is `Cc`, `Velocity`, or `KeyTrack` (reserved: further SF2 source kinds land later
per AD-1, without changing these three). `ModSource::ccNumber` is only meaningful when
`kind == Cc`.

`ModTarget` is `Gain` (depth in dB), `Pitch` (depth in cents), or `Pan` (depth normalized -1..1).
Reserved: additional targets may be added later; existing target semantics never change.

| Field               | Type         | Default              | Notes |
|---------------------|--------------|-----------------------|-------|
| `sourceControlId`    | `string`     | `""`                  | references a `ControlMapEntry::id`; empty means the source is a built-in (velocity/keytrack), not a mapped control |
| `source`             | `ModSource`  | `{Velocity, 0}`       | |
| `target`             | `ModTarget`  | `Gain`                | |
| `depth`              | `float`      | 0                     | unit depends on `target` (see above) |
| `curve`              | `float`      | 0                     | normalized 0..1; 0 = linear |

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
covered one-to-one by Catch2 tests:

- `model.schema_version_mismatch` — `schemaVersion != kModelSchemaVersion`
- `model.duplicate_control_id` — two `controls` entries share the same non-empty `id`
- `region.sample_file_missing` — `sampleFile` is empty
- `region.key_range_invalid` — `loKey > hiKey`
- `region.velocity_range_invalid` — `loVelocity > hiVelocity`
- `region.pan_out_of_range` — `pan` outside `[-1, 1]`
- `region.gain_not_finite` — `gainDb` is NaN or infinite
- `region.tuning_not_finite` — `tuningCents` is NaN or infinite
- `region.offset_negative` — `offsetSeconds < 0`
- `region.loop_range_invalid` — `loopEnabled` and (`loopStartSeconds >= loopEndSeconds` or either is negative)
- `region.envelope_time_negative` — any of the envelope's time fields is negative
- `region.envelope_sustain_out_of_range` — `sustainLevel` outside `[0, 1]`
- `region.mod_depth_not_finite` — a `modMatrix` entry's `depth` is NaN or infinite
- `region.mod_curve_out_of_range` — a `modMatrix` entry's `curve` outside `[0, 1]`
- `region.mod_source_unknown_control` — a `modMatrix` entry's `sourceControlId` is non-empty but does not match any `controls[i].id`
- `control.id_missing` — a `ControlMapEntry::id` is empty

## Serialization (golden files)

`serializeModel()` / `parseModel()` produce a deterministic, byte-stable text dump — for golden
snapshot tests only, **not** plugin state (Epic 6 owns plugin state chunks separately).

- Fixed field order (declaration order above), one `key=value` line per field; array fields use
  `container[i].field=value` keys with an explicit `..._count=N` line before the elements.
  String values are escaped (`\` → `\\`, newline → `\n`).
- Floats are written with `std::to_chars` (shortest round-trip, locale-independent) — never
  `std::to_string`/iostream default formatting, both of which are locale- and precision-dependent.
- Always `\n` line endings, regardless of platform.
- First line is always `schema_version=<N>`.

This is the format Story 1.3's per-frontend golden harness extends.
