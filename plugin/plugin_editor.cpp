#include "plugin_editor.h"

namespace fbsampler {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    loadButton_.onClick = [this] { chooseAndLoad(); };
    addAndMakeVisible(loadButton_);

    statusLabel_.setJustificationType(juce::Justification::topLeft);
    statusLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(statusLabel_);

    processor_.addChangeListener(this);
    refreshStatus();

    setSize(420, 220);
}

PluginEditor::~PluginEditor()
{
    processor_.removeChangeListener(this);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    loadButton_.setBounds(area.removeFromTop(32).removeFromLeft(140));
    area.removeFromTop(8);
    statusLabel_.setBounds(area);
}

// ChangeListener delivery already happens on the message thread (JUCE
// dispatches async), so touching components here is safe even though the
// loader completes on a background thread.
void PluginEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshStatus();
}

void PluginEditor::refreshStatus()
{
    statusLabel_.setText(processor_.getLoadStatusText(),
                         juce::dontSendNotification);
}

void PluginEditor::chooseAndLoad()
{
    // Async FileChooser only: modal loops are banned in plugins and fail
    // pluginval. No configured start folder yet (library folders are Epic 3).
    chooser_ = std::make_unique<juce::FileChooser>(
        "Load SFZ instrument", juce::File(), "*.sfz");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<PluginEditor>(this)](const juce::FileChooser& fc) {
            if (safeThis == nullptr)
                return; // editor was destroyed while the dialog was open
            const juce::File file = fc.getResult();
            if (file.existsAsFile())
                safeThis->processor_.loadSfzFileAsync(file.getFullPathName());
        });
}

} // namespace fbsampler
