#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "format_badge.h"
#include "tokens.h"

#include <functional>

namespace fbsampler::ui {

// 48px header bar (Story 3.4, UX-DR2): [dB] wordmark (the only branding),
// loaded-library name, format badge, live voice count, browser toggle
// (left), settings gear (right). The voice count is polled by the owner via
// setVoiceCount (~10 Hz timer) — this component never touches the engine.
class HeaderBar : public juce::Component {
public:
    HeaderBar();

    std::function<void(bool)> onBrowserToggled;
    std::function<void()> onSettingsClicked; // Story 3.5 overlay; stubbed until then

    void setLibrary(const juce::String& name, FormatBadge::Format format,
                    bool hasLibrary);
    void setVoiceCount(int voices);

    /// Story 3.7: owner syncs toggle state on breakpoint mode changes so the
    /// button always reflects whether the browser is showing.
    void setBrowserToggle(bool visible);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton browserToggle_{"Library"};
    juce::Label wordmark_;
    juce::Label libraryName_;
    FormatBadge badge_;
    bool badgeVisible_ = false;
    juce::Label voiceCount_;
    juce::TextButton settingsGear_{juce::CharPointer_UTF8("\xe2\x9a\x99")}; // ⚙

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};

} // namespace fbsampler::ui
