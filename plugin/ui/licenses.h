#pragma once

// Story 3.5: single maintainable notice list for the About view. Story 7.4's
// license audit extends this. Full license texts ship with the distribution;
// this view carries the attribution notices (selectable/copyable).

namespace fbsampler::ui {

inline constexpr const char* kLicenseNotices = R"(feedBack Sampler
Licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
Source code: available per the AGPL notice accompanying the distribution.

Bundled components:

- JUCE 8 (AGPL-3.0 option) - (c) Raw Material Software Ltd.
- sfizz (BSD-2-Clause) - SFZTools contributors.
- stb_vorbis (public domain / MIT option) - Sean Barrett.
- Rubik font (SIL Open Font License 1.1) - The Rubik Project Authors.
  License text: plugin/ui/fonts/OFL.txt.
- Catch2 (BSL-1.0) - test builds only; not shipped in the plugin binary.
)";

} // namespace fbsampler::ui
