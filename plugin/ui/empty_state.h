#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "tokens.h"

#include <functional>

namespace fbsampler::ui {

// Empty state (Story 3.6 AC1, exact EXPERIENCE.md copy): shown when no
// folders are configured and the index is empty.
class EmptyState : public juce::Component {
public:
    EmptyState()
    {
        message_.setText("Add a library folder to get started.",
                         juce::dontSendNotification);
        message_.setColour(juce::Label::textColourId, tokens::color::text);
        message_.setFont(juce::Font(juce::FontOptions(tokens::type::titlePx)));
        message_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(message_);

        addButton_.setButtonText("Add folder");
        addButton_.setTitle("Add a library folder");
        addButton_.onClick = [this] {
            if (onAddFolder)
                onAddFolder();
        };
        addAndMakeVisible(addButton_);
    }

    std::function<void()> onAddFolder;

    void resized() override
    {
        auto area = getLocalBounds().withSizeKeepingCentre(320, 84);
        message_.setBounds(area.removeFromTop(28));
        area.removeFromTop(tokens::spacing::unit * 4);
        addButton_.setBounds(area.withSizeKeepingCentre(120, 32));
    }

private:
    juce::Label message_;
    juce::TextButton addButton_; // primary variant by default
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmptyState)
};

} // namespace fbsampler::ui
