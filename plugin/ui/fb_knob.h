#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "tokens.h"

namespace fbsampler::ui {

// feedBack rotary knob per DESIGN.md: drag-vertical, Shift = fine adjust
// (~10x finer), double-click resets to default, mouse-wheel steps, value
// tooltip pill shown while dragging. Label rendering (textDim, label scale)
// belongs to the owner via an attached juce::Label below the knob.
//
// Story 3.7: full keyboard operation (arrows step, Shift+arrows fine) and
// unit-aware value text so screen readers announce "−6.0 dB", not "0.83".
class FbKnob : public juce::Slider {
public:
    // Unit for value text (tooltip pill + accessibility announcements).
    enum class Unit { plain, decibels, percent, cents, seconds };

    FbKnob();

    // Sets range + the value restored by double-click reset.
    void setDefaultValue(double defaultValue);

    void setUnit(Unit unit) { unit_ = unit; }

    juce::String getTextFromValue(double value) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    void applyDragSensitivity(const juce::ModifierKeys& mods);
    double keyStep(bool fine) const;

    Unit unit_ = Unit::plain;

    static constexpr int normalSensitivity = 250; // px for full range (JUCE default)
    static constexpr int fineFactor = 10;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FbKnob)
};

} // namespace fbsampler::ui
