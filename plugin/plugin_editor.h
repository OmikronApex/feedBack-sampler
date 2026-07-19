#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin_processor.h"

namespace fbsampler {

// Deliberately minimal Story 1.5 editor: a "Load SFZ..." button plus status
// text. No styling — the real UI is Epic 3 (v3 tokens). The editor only
// issues load commands and renders results (AD-3: UI never mutates engine
// data directly).
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::ChangeListener {
public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshStatus();
    void chooseAndLoad();

    PluginProcessor& processor_;
    juce::TextButton loadButton_{"Load SFZ..."};
    juce::Label statusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace fbsampler
