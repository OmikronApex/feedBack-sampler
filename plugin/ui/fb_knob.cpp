#include "fb_knob.h"

namespace fbsampler::ui {

FbKnob::FbKnob()
    : juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    setMouseDragSensitivity(normalSensitivity);
    setScrollWheelEnabled(true); // mouse-wheel steps
    setWantsKeyboardFocus(true);
    // Value tooltip pill while dragging (styled by FbLookAndFeel::drawBubble).
    setPopupDisplayEnabled(true, false, nullptr);
    setVelocityBasedMode(false);
}

void FbKnob::setDefaultValue(double defaultValue)
{
    setDoubleClickReturnValue(true, defaultValue);
}

void FbKnob::mouseDown(const juce::MouseEvent& e)
{
    applyDragSensitivity(e.mods);
    juce::Slider::mouseDown(e);
}

void FbKnob::mouseDrag(const juce::MouseEvent& e)
{
    // Re-check each drag event so pressing/releasing Shift mid-drag switches
    // between coarse and fine adjustment.
    applyDragSensitivity(e.mods);
    juce::Slider::mouseDrag(e);
}

juce::String FbKnob::getTextFromValue(double value)
{
    switch (unit_) {
    case Unit::decibels:
        return juce::String(value, 1) + " dB";
    case Unit::percent:
        return juce::String(juce::roundToInt(value * 100.0)) + " %";
    case Unit::cents:
        return juce::String(juce::roundToInt(value)) + " cents";
    case Unit::seconds:
        return juce::String(value, 2) + " s";
    case Unit::plain:
    default:
        return juce::String(juce::roundToInt(value));
    }
}

bool FbKnob::keyPressed(const juce::KeyPress& key)
{
    // Full keyboard operability (Story 3.7 AC1): arrows step the value,
    // Shift+arrows step ~10x finer. JUCE rotary sliders have no built-in
    // arrow handling.
    const bool up = key.isKeyCode(juce::KeyPress::upKey)
                    || key.isKeyCode(juce::KeyPress::rightKey);
    const bool down = key.isKeyCode(juce::KeyPress::downKey)
                      || key.isKeyCode(juce::KeyPress::leftKey);
    if (!up && !down)
        return juce::Slider::keyPressed(key);

    const double step = keyStep(key.getModifiers().isShiftDown());
    setValue(getValue() + (up ? step : -step),
             juce::sendNotificationSync);
    return true;
}

double FbKnob::keyStep(bool fine) const
{
    const double interval = getInterval();
    const double range = getMaximum() - getMinimum();
    double step = interval > 0.0 ? interval : range / 100.0;
    if (fine)
        step = juce::jmax(interval > 0.0 ? interval : range / 1000.0,
                          step / fineFactor);
    return step;
}

void FbKnob::focusGained(FocusChangeType)
{
    repaint(); // focus ring painted in FbLookAndFeel::drawRotarySlider
}

void FbKnob::focusLost(FocusChangeType)
{
    repaint();
}

void FbKnob::applyDragSensitivity(const juce::ModifierKeys& mods)
{
    setMouseDragSensitivity(mods.isShiftDown()
                                ? normalSensitivity * fineFactor
                                : normalSensitivity);
}

} // namespace fbsampler::ui
