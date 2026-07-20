#pragma once

// Pure, JUCE-free instrument-view decision logic (Story 3.4): which control
// cards to render for a loaded model. Unit-tested directly.

#include "fbsampler/model.h"

#include <string>
#include <vector>

namespace fbsampler::ui {

struct ControlCardDescriptor {
    enum class Kind {
        cc,       // generated from a ControlMapEntry (ccNumber valid)
        volume,   // generic set
        pan,
        tuning,
        attack,
        release,
    };
    Kind kind = Kind::cc;
    int ccNumber = -1; // valid for Kind::cc
    std::string label;
    std::string accessibleName;
};

struct ControlCardModel {
    std::vector<ControlCardDescriptor> cards;
    bool showPresetRow = false;
};

/// Control-map entries lower with id "cc<N>" (AD-8, sfz frontend); entries
/// whose id doesn't follow that convention are skipped (nothing to bind).
/// Empty control map -> the generic set (volume, pan, tuning, ADSR override,
/// FR12); soundfonts additionally get the preset-selector row.
ControlCardModel buildControlCards(const std::vector<ControlMapEntry>& controls,
                                   bool isSoundfont);

} // namespace fbsampler::ui
