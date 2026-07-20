#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "tokens.h"

namespace fbsampler::ui {

// Shared feedBack v3 LookAndFeel. Every chrome surface in plugin/ paints
// through this class + tokens.h — flat surface-step depth, no shadows, no
// gradients. Lifetime rule: components must never outlive the FbLookAndFeel
// they reference; owners call setLookAndFeel(nullptr) before destroying it.
class FbLookAndFeel : public juce::LookAndFeel_V4 {
public:
    FbLookAndFeel();

    // Mark a button as the secondary variant (transparent fill, border,
    // text-colored label). Default is the primary filled variant.
    static void setSecondary(juce::Button& button, bool secondary = true);
    static bool isSecondary(const juce::Button& button);

    // 2px focusRing outline, 2px offset — shared by every focusable
    // component (Story 3.7 relies on this being universal).
    static void drawFocusRing(juce::Graphics& g, juce::Rectangle<float> bounds,
                              float cornerRadius);

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    // Buttons
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    // Rotary knob
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    // Linear slider (settings): stock V4 paint + universal focus ring
    // (Story 3.7 — focus ring lives in the LookAndFeel, never per component).
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width,
                          int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    // Slider drag-value bubble -> surfaceInset pill
    void drawBubble(juce::Graphics& g, juce::BubbleComponent& bubble,
                    const juce::Point<float>& tip,
                    const juce::Rectangle<float>& body) override;
    juce::Font getSliderPopupFont(juce::Slider&) override;

    // Text editor
    void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                  juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                               juce::TextEditor& editor) override;

    // Combo box + popup menu
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    // Scrollbar: 6px thumb in border color, hover #475569, transparent track
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y,
                       int width, int height, bool isScrollbarVertical,
                       int thumbStartPosition, int thumbSize, bool isMouseOver,
                       bool isMouseDown) override;

    // Progress: thin 2px primary fill on border track
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width,
                         int height, double progress,
                         const juce::String& textToShow) override;

    // Tooltip: surfaceInset pill
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width,
                     int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                          juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;

    juce::Font getLabelFont(juce::Label& label) override;

private:
    // Rubik variable font (weights 300-900 in one file). JUCE 8 exposes the
    // default named instance (Regular 400); heavier scale weights (title 600,
    // header 700) fall back to JUCE's synthetic bold when Font::bold is set.
    // Documented trade-off: acceptable until JUCE grows variable-axis access.
    juce::Typeface::Ptr rubik_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FbLookAndFeel)
};

} // namespace fbsampler::ui
