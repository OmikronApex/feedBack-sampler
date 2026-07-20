#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "control_card_model.h"
#include "fb_knob.h"
#include "tokens.h"

#include <functional>
#include <memory>
#include <vector>

namespace fbsampler::ui {

// Instrument view (Story 3.4): generated control cards from the model's
// control map (or the generic set), plus the soundfont preset-selector row.
// Empty/error state polish is Story 3.6 — plain placeholder for now. The DS
// artwork frame slot (inset well) is Epic 4; nothing is built for it.
class InstrumentViewPane : public juce::Component {
public:
    InstrumentViewPane();

    /// CC knob moved: (ccNumber, value 0..127).
    std::function<void(int, int)> onCcChanged;
    /// Generic volume/pan moved: (volumeDb, pan -1..1) — continuous.
    std::function<void(float, float)> onVolumePanChanged;
    /// Generic tuning/ADSR gesture ENDED: (tuningCents, attackS, releaseS).
    std::function<void(float, float, float)> onModelOffsetsCommitted;
    /// Preset selected in the selector row.
    std::function<void(int)> onPresetSelected;

    /// Rebuild from the current load state.
    void rebuild(const ControlCardModel& model,
                 const std::vector<juce::String>& presetNames,
                 int activePresetIndex, bool hasLibrary);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Card {
        ControlCardDescriptor descriptor;
        std::unique_ptr<FbKnob> knob;
        std::unique_ptr<juce::Label> label;
    };

    void notifyGeneric(bool gestureEnd);

    std::vector<Card> cards_;
    juce::ComboBox presetBox_;
    juce::Label presetLabel_;
    bool presetRowVisible_ = false;
    bool hasLibrary_ = false;
    juce::Label placeholder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentViewPane)
};

} // namespace fbsampler::ui
