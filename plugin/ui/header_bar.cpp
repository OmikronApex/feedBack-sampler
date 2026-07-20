#include "header_bar.h"

#include "fb_look_and_feel.h"

namespace fbsampler::ui {

namespace t = tokens;

HeaderBar::HeaderBar()
{
    browserToggle_.setClickingTogglesState(true);
    browserToggle_.setToggleState(true, juce::dontSendNotification);
    FbLookAndFeel::setSecondary(browserToggle_);
    browserToggle_.setTitle("Show or hide the library browser");
    browserToggle_.onClick = [this] {
        if (onBrowserToggled)
            onBrowserToggled(browserToggle_.getToggleState());
    };
    addAndMakeVisible(browserToggle_);

    wordmark_.setText("[dB]", juce::dontSendNotification);
    wordmark_.setFont(juce::Font(juce::FontOptions(t::type::headerPx,
                                                   juce::Font::bold)));
    wordmark_.setColour(juce::Label::textColourId, t::color::text);
    wordmark_.setAccessible(false); // decorative branding
    addAndMakeVisible(wordmark_);

    libraryName_.setText("No library loaded", juce::dontSendNotification);
    libraryName_.setFont(juce::Font(juce::FontOptions(t::type::titlePx)));
    libraryName_.setColour(juce::Label::textColourId, t::color::text);
    libraryName_.setTitle("Loaded library");
    // Truncate with ellipsis instead of squeezing glyphs (Story 3.7 reflow).
    libraryName_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(libraryName_);

    addChildComponent(badge_); // visible only with a loaded library

    // Voice count readout. Tabular figures per 3.1's documented decision:
    // Rubik defaults accepted; right-justified fixed-width slot so the
    // header never jitters as digits change.
    voiceCount_.setText("0", juce::dontSendNotification);
    voiceCount_.setFont(juce::Font(juce::FontOptions(t::type::bodyPx)));
    voiceCount_.setColour(juce::Label::textColourId, t::color::textDim);
    voiceCount_.setJustificationType(juce::Justification::centredRight);
    voiceCount_.setTitle("Active voices");
    addAndMakeVisible(voiceCount_);

    FbLookAndFeel::setSecondary(settingsGear_);
    settingsGear_.setTitle("Settings");
    settingsGear_.onClick = [this] {
        if (onSettingsClicked)
            onSettingsClicked();
    };
    addAndMakeVisible(settingsGear_);
}

void HeaderBar::setLibrary(const juce::String& name, FormatBadge::Format format,
                           bool hasLibrary)
{
    libraryName_.setText(hasLibrary ? name : juce::String("No library loaded"),
                         juce::dontSendNotification);
    badge_.setFormat(format);
    badgeVisible_ = hasLibrary;
    badge_.setVisible(badgeVisible_);
}

void HeaderBar::setBrowserToggle(bool visible)
{
    browserToggle_.setToggleState(visible, juce::dontSendNotification);
}

void HeaderBar::setVoiceCount(int voices)
{
    const juce::String text(voices);
    if (text != voiceCount_.getText())
        voiceCount_.setText(text, juce::dontSendNotification);
}

void HeaderBar::paint(juce::Graphics& g)
{
    g.fillAll(t::color::surface);
    g.setColour(t::color::border);
    g.fillRect(getLocalBounds().removeFromBottom(1));
}

void HeaderBar::resized()
{
    auto area = getLocalBounds();
    area.reduce(t::spacing::unit * 3, t::spacing::unit * 2);

    browserToggle_.setBounds(area.removeFromLeft(88));
    area.removeFromLeft(t::spacing::unit * 3);
    wordmark_.setBounds(area.removeFromLeft(64));
    area.removeFromLeft(t::spacing::unit * 2);

    settingsGear_.setBounds(area.removeFromRight(36));
    area.removeFromRight(t::spacing::unit * 2);
    voiceCount_.setBounds(area.removeFromRight(56));
    area.removeFromRight(t::spacing::unit * 2);

    if (badgeVisible_) {
        badge_.setBounds(area.removeFromRight(52).withSizeKeepingCentre(48, 20));
        area.removeFromRight(t::spacing::unit * 2);
    }
    libraryName_.setBounds(area);
}

} // namespace fbsampler::ui
