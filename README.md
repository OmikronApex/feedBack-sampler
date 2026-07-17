# feedBack Sampler

Universal VSTi sampler. Working title (OQ-2). License: AGPL-3.0 (see `LICENSE`).

## Layout

- `core/` — `sampler-core` static library (namespace `fbsampler`). No JUCE GUI
  modules, ever; the build fails at configure if one appears (AD-6 guard).
  Public headers: `core/include/fbsampler/`.
- `plugin/` — JUCE VST3 shell ("feedBack Sampler"), links `sampler-core`.
- `tests/` — Catch2 tests (`golden/`, `render/`, `fuzz/`, `perf/` arrive later).
- `corpus/` — instrument corpus (Story 1.6).
- `cmake/` — dependency pins + build guards.

## Pinned dependencies (FetchContent, exact SHAs in `cmake/fbsampler_deps.cmake`)

| Dependency | Version | Commit |
|---|---|---|
| sfizz | 1.2.3 | `4e70dc0bef53b41f2853ed46e26f5911114c92d0` |
| JUCE | 8.0.14 | `2cdfca8feb300fb424002ba2c2751569e5bacb64` |
| Catch2 | 3.8.1 | `2b60af89e23d28eefc081bc930831ee9d45ea58b` |
| pluginval (CI only) | 1.0.4 | release binaries |

## Build

Requires CMake ≥ 3.28 and a C++17 toolchain. First configure downloads pinned
dependencies (network needed).

### Windows (Visual Studio 2022)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Linux

JUCE needs X11/ALSA dev packages:

```bash
sudo apt-get install libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev \
    libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype6-dev \
    libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Built VST3 lands under
`build/plugin/feedback-sampler-plugin_artefacts/<Config>/VST3/feedBack Sampler.vst3`.

## CI

`.github/workflows/ci.yml`: windows/macos/ubuntu matrix — build, ctest, and
pluginval 1.0.4 at strictness 5 against the built VST3. All three must be green.

## Conventions

Namespace `fbsampler` · files/dirs `snake_case` · types `PascalCase` ·
functions/vars `camelCase` · core/shell rule per AD-6.
