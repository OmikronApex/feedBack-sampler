#include "fb_look_and_feel.h"

#include "FbFontsData.h"

namespace fbsampler::ui {

namespace t = tokens;

FbLookAndFeel::FbLookAndFeel()
{
    rubik_ = juce::Typeface::createSystemTypefaceFor(
        FbFonts::Rubikwght_ttf, FbFonts::Rubikwght_ttfSize);
    // Fallback: if the embedded font fails to load, rubik_ stays null and
    // getTypefaceForFont falls through to the system sans default.

    setColour(juce::ResizableWindow::backgroundColourId, t::color::bg);
    setColour(juce::DocumentWindow::textColourId, t::color::text);

    setColour(juce::TextButton::buttonColourId, t::color::primary);
    setColour(juce::TextButton::buttonOnColourId, t::color::primaryHover);
    setColour(juce::TextButton::textColourOffId, t::color::onAccent);
    setColour(juce::TextButton::textColourOnId, t::color::onAccent);

    setColour(juce::Label::textColourId, t::color::text);

    setColour(juce::Slider::rotarySliderFillColourId, t::color::primary);
    setColour(juce::Slider::rotarySliderOutlineColourId, t::color::border);
    setColour(juce::Slider::textBoxTextColourId, t::color::text);
    setColour(juce::Slider::textBoxBackgroundColourId, t::color::surfaceInset);
    setColour(juce::Slider::textBoxOutlineColourId, t::color::border);

    setColour(juce::TextEditor::backgroundColourId, t::color::surfaceInset);
    setColour(juce::TextEditor::textColourId, t::color::text);
    setColour(juce::TextEditor::outlineColourId, t::color::border);
    setColour(juce::TextEditor::focusedOutlineColourId, t::color::focusRing);
    setColour(juce::TextEditor::highlightColourId,
              t::color::primary.withAlpha(0.35f));
    setColour(juce::CaretComponent::caretColourId, t::color::text);

    setColour(juce::ComboBox::backgroundColourId, t::color::surface);
    setColour(juce::ComboBox::textColourId, t::color::text);
    setColour(juce::ComboBox::outlineColourId, t::color::border);
    setColour(juce::ComboBox::arrowColourId, t::color::textDim);
    setColour(juce::ComboBox::focusedOutlineColourId, t::color::focusRing);

    setColour(juce::PopupMenu::backgroundColourId, t::color::surface);
    setColour(juce::PopupMenu::textColourId, t::color::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t::color::primary);
    setColour(juce::PopupMenu::highlightedTextColourId, t::color::onAccent);

    setColour(juce::ScrollBar::thumbColourId, t::color::border);

    setColour(juce::ProgressBar::backgroundColourId, t::color::border);
    setColour(juce::ProgressBar::foregroundColourId, t::color::primary);

    setColour(juce::TooltipWindow::backgroundColourId, t::color::surfaceInset);
    setColour(juce::TooltipWindow::textColourId, t::color::text);
    setColour(juce::TooltipWindow::outlineColourId, t::color::border);

    setColour(juce::BubbleComponent::backgroundColourId, t::color::surfaceInset);
    setColour(juce::BubbleComponent::outlineColourId, t::color::border);

    setColour(juce::AlertWindow::backgroundColourId, t::color::surface);
    setColour(juce::AlertWindow::textColourId, t::color::text);
    setColour(juce::AlertWindow::outlineColourId, t::color::border);
}

void FbLookAndFeel::setSecondary(juce::Button& button, bool secondary)
{
    button.getProperties().set("fb-secondary", secondary);
}

bool FbLookAndFeel::isSecondary(const juce::Button& button)
{
    return static_cast<bool>(button.getProperties()["fb-secondary"]);
}

void FbLookAndFeel::drawFocusRing(juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  float cornerRadius)
{
    g.setColour(t::color::focusRing);
    g.drawRoundedRectangle(
        bounds.expanded(t::metric::focusRingOffset
                        + t::metric::focusRingThickness * 0.5f),
        cornerRadius + t::metric::focusRingOffset,
        t::metric::focusRingThickness);
}

juce::Typeface::Ptr FbLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (rubik_ != nullptr)
        return rubik_;
    return juce::LookAndFeel_V4::getTypefaceForFont(font);
}

void FbLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                         const juce::Colour&,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(
        t::metric::focusRingOffset + t::metric::focusRingThickness);

    if (isSecondary(button)) {
        if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown) {
            g.setColour(t::color::surface);
            g.fillRoundedRectangle(bounds, t::radius::control);
        }
        g.setColour(t::color::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), t::radius::control, 1.0f);
    } else {
        g.setColour((shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
                        ? t::color::primaryHover
                        : t::color::primary);
        g.fillRoundedRectangle(bounds, t::radius::control);
    }

    if (button.hasKeyboardFocus(true))
        drawFocusRing(g, bounds, t::radius::control);
}

juce::Font FbLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return juce::Font(juce::FontOptions(t::type::bodyPx));
}

void FbLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                   bool, bool)
{
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(isSecondary(button) ? t::color::text : t::color::onAccent);
    g.drawText(button.getButtonText(), button.getLocalBounds(),
               juce::Justification::centred, true);
}

void FbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                     int height, float sliderPos,
                                     float rotaryStartAngle, float rotaryEndAngle,
                                     juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                               static_cast<float>(y),
                                               static_cast<float>(width),
                                               static_cast<float>(height))
                            .reduced(t::metric::focusRingOffset
                                     + t::metric::focusRingThickness);
    const float radius =
        juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f - 2.0f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax(2.0f, radius * 0.18f);
    const float arcRadius = radius - lineW / 2.0f;
    const float angle =
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(t::color::border);
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (slider.isEnabled()) {
        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                               rotaryStartAngle, angle, true);
        g.setColour(t::color::primary);
        g.strokePath(valueArc,
                     juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
    }

    // Pointer from arc toward centre.
    const juce::Point<float> tip(
        centre.x + (arcRadius - lineW) * std::cos(angle
                                                  - juce::MathConstants<float>::halfPi),
        centre.y + (arcRadius - lineW) * std::sin(angle
                                                  - juce::MathConstants<float>::halfPi));
    g.setColour(slider.isEnabled() ? t::color::text : t::color::textDim);
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(tip));

    if (slider.hasKeyboardFocus(true)) {
        const float d = 2.0f * (radius + t::metric::focusRingOffset);
        g.setColour(t::color::focusRing);
        g.drawEllipse(juce::Rectangle<float>(d, d).withCentre(centre),
                      t::metric::focusRingThickness);
    }
}

void FbLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y,
                                     int width, int height, float sliderPos,
                                     float minSliderPos, float maxSliderPos,
                                     juce::Slider::SliderStyle style,
                                     juce::Slider& slider)
{
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style,
                                     slider);
    if (slider.hasKeyboardFocus(true))
        drawFocusRing(g,
                      juce::Rectangle<float>(static_cast<float>(x),
                                             static_cast<float>(y),
                                             static_cast<float>(width),
                                             static_cast<float>(height))
                          .reduced(t::metric::focusRingOffset
                                   + t::metric::focusRingThickness),
                      t::radius::control);
}

void FbLookAndFeel::drawBubble(juce::Graphics& g, juce::BubbleComponent& bubble,
                               const juce::Point<float>&,
                               const juce::Rectangle<float>& body)
{
    // Value tooltip pill: surfaceInset fill, hairline border, no arrow —
    // flat depth per DESIGN.md.
    g.setColour(bubble.findColour(juce::BubbleComponent::backgroundColourId));
    g.fillRoundedRectangle(body, t::radius::pill);
    g.setColour(bubble.findColour(juce::BubbleComponent::outlineColourId));
    g.drawRoundedRectangle(body.reduced(0.5f), t::radius::pill, 1.0f);
}

juce::Font FbLookAndFeel::getSliderPopupFont(juce::Slider&)
{
    return juce::Font(juce::FontOptions(t::type::labelPx));
}

void FbLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width,
                                             int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width),
                           static_cast<float>(height), t::radius::control);
}

void FbLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width,
                                          int height, juce::TextEditor& editor)
{
    const juce::Rectangle<float> bounds(0.0f, 0.0f, static_cast<float>(width),
                                        static_cast<float>(height));
    if (editor.hasKeyboardFocus(true) && !editor.isReadOnly()) {
        drawFocusRing(g, bounds.reduced(t::metric::focusRingOffset
                                        + t::metric::focusRingThickness),
                      t::radius::control);
        g.setColour(editor.findColour(juce::TextEditor::focusedOutlineColourId));
    } else {
        g.setColour(editor.findColour(juce::TextEditor::outlineColourId));
    }
    g.drawRoundedRectangle(bounds.reduced(0.5f), t::radius::control, 1.0f);
}

void FbLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                 int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> bounds(0.0f, 0.0f, static_cast<float>(width),
                                        static_cast<float>(height));
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, t::radius::control);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), t::radius::control, 1.0f);

    juce::Path arrow;
    auto arrowSlot = bounds;
    const auto arrowZone =
        arrowSlot.removeFromRight(static_cast<float>(height))
            .reduced(static_cast<float>(height) * 0.33f);
    arrow.startNewSubPath(arrowZone.getX(), arrowZone.getCentreY() - 2.0f);
    arrow.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
    arrow.lineTo(arrowZone.getRight(), arrowZone.getCentreY() - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(arrow, juce::PathStrokeType(1.5f));

    if (box.hasKeyboardFocus(true))
        drawFocusRing(g,
                      bounds.reduced(t::metric::focusRingOffset
                                     + t::metric::focusRingThickness),
                      t::radius::control);
}

juce::Font FbLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(t::type::bodyPx));
}

void FbLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width,
                                            int height)
{
    g.fillAll(t::color::surface);
    g.setColour(t::color::border);
    g.drawRect(0, 0, width, height);
}

void FbLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x,
                                  int y, int width, int height,
                                  bool isScrollbarVertical,
                                  int thumbStartPosition, int thumbSize,
                                  bool isMouseOver, bool isMouseDown)
{
    // Track is transparent by spec — paint nothing behind the thumb.
    juce::Rectangle<float> thumb;
    if (isScrollbarVertical)
        thumb = { static_cast<float>(x)
                      + (static_cast<float>(width)
                         - t::metric::scrollbarThickness) / 2.0f,
                  static_cast<float>(thumbStartPosition),
                  static_cast<float>(t::metric::scrollbarThickness),
                  static_cast<float>(thumbSize) };
    else
        thumb = { static_cast<float>(thumbStartPosition),
                  static_cast<float>(y)
                      + (static_cast<float>(height)
                         - t::metric::scrollbarThickness) / 2.0f,
                  static_cast<float>(thumbSize),
                  static_cast<float>(t::metric::scrollbarThickness) };

    g.setColour((isMouseOver || isMouseDown) ? t::color::scrollThumbHover
                                             : t::color::border);
    g.fillRoundedRectangle(thumb, t::metric::scrollbarThickness / 2.0f);
}

void FbLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar,
                                    int width, int height, double progress,
                                    const juce::String&)
{
    // 2px primary fill on a border-colored track, vertically centred.
    const float barH = static_cast<float>(t::metric::progressBarHeight);
    const float yPos = (static_cast<float>(height) - barH) / 2.0f;
    const juce::Rectangle<float> track(0.0f, yPos, static_cast<float>(width),
                                       barH);
    g.setColour(bar.findColour(juce::ProgressBar::backgroundColourId));
    g.fillRoundedRectangle(track, barH / 2.0f);

    const auto clamped = juce::jlimit(0.0, 1.0, progress);
    if (clamped > 0.0) {
        g.setColour(bar.findColour(juce::ProgressBar::foregroundColourId));
        g.fillRoundedRectangle(
            track.withWidth(static_cast<float>(width * clamped)), barH / 2.0f);
    }
}

void FbLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text,
                                int width, int height)
{
    const juce::Rectangle<float> bounds(0.0f, 0.0f, static_cast<float>(width),
                                        static_cast<float>(height));
    g.setColour(t::color::surfaceInset);
    g.fillRoundedRectangle(bounds, t::radius::pill);
    g.setColour(t::color::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), t::radius::pill, 1.0f);
    g.setColour(t::color::text);
    g.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    g.drawText(text, bounds.reduced(t::spacing::unit * 2.0f, 0.0f),
               juce::Justification::centred, true);
}

juce::Rectangle<int> FbLookAndFeel::getTooltipBounds(
    const juce::String& tipText, juce::Point<int> screenPos,
    juce::Rectangle<int> parentArea)
{
    const juce::Font font{juce::FontOptions{t::type::labelPx}};
    juce::GlyphArrangement measure;
    measure.addLineOfText(font, tipText, 0.0f, 0.0f);
    const int w = static_cast<int>(
                      std::ceil(measure.getBoundingBox(0, -1, true).getWidth()))
                  + t::spacing::unit * 6;
    const int h = static_cast<int>(font.getHeight()) + t::spacing::unit * 3;
    return juce::Rectangle<int>(screenPos.x, screenPos.y + 16, w, h)
        .constrainedWithin(parentArea);
}

juce::Font FbLookAndFeel::getLabelFont(juce::Label& label)
{
    juce::ignoreUnused(label);
    return juce::Font(juce::FontOptions(t::type::bodyPx));
}

} // namespace fbsampler::ui
