#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "tokens.h"

namespace fbsampler::ui {

// Non-blocking scanning banner (Story 3.6): thin primary progress line +
// current path (middle-truncated by the Label's ellipsis, selectable copy
// lives in the load-status strip). Fed at ~4 Hz from the processor's scan
// progress relay; indeterminate for now (the scanner has no total).
class ScanBanner : public juce::Component {
public:
    ScanBanner()
    {
        text_.setColour(juce::Label::textColourId, tokens::color::textDim);
        text_.setFont(juce::Font(juce::FontOptions(tokens::type::labelPx)));
        text_.setTitle("Library scan progress");
        addAndMakeVisible(text_);
    }

    void setProgressText(const juce::String& text)
    {
        text_.setText(text, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tokens::color::surface);
        g.setColour(tokens::color::border);
        g.fillRect(getLocalBounds().removeFromBottom(3).removeFromTop(1));
        // Indeterminate 2px activity line in primary.
        g.setColour(tokens::color::primary);
        g.fillRect(0, getHeight() - 2, getWidth(), 2);
    }

    void resized() override
    {
        text_.setBounds(getLocalBounds().reduced(tokens::spacing::unit * 3,
                                                 tokens::spacing::unit));
    }

private:
    juce::Label text_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanBanner)
};

} // namespace fbsampler::ui
