#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "fbsampler/config_service.h"
#include "tokens.h"

#include <functional>
#include <memory>
#include <vector>

namespace fbsampler {
class PluginProcessor;
}

namespace fbsampler::ui {

// Settings overlay (Story 3.5): full-window scrim (black 60%) + centered
// surface panel with hairline border per DESIGN.md Elevation. Everything
// persists through ConfigService only (AD-9) — these are per-user machine
// settings, never plugin state. Cross-instance bound (documented, FR10): no
// live push; other instances pick changes up on their next rescan/restart.
class SettingsOverlay : public juce::Component {
public:
    explicit SettingsOverlay(PluginProcessor& processor);
    ~SettingsOverlay() override; // out-of-line: FolderRow is incomplete here

    std::function<void()> onClosed;

    void open();
    void close();

    /// Launch the add-folder chooser directly (also used by the 3.6 empty
    /// state, without opening the whole overlay). Persists + scans on pick.
    void addFolder();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override; // scrim click closes
    bool keyPressed(const juce::KeyPress& key) override; // Esc closes

private:
    class FolderRow;

    void rebuildFolderList();
    void persistSettings(const std::function<void(SamplerSettings&)>& mutate);
    void removeFolder(const std::string& path);

    PluginProcessor& processor_;

    juce::Rectangle<int> panelBounds_;
    juce::Label title_;
    juce::TextButton closeButton_{"Close"};

    juce::Label foldersLabel_;
    std::vector<std::unique_ptr<FolderRow>> folderRows_;
    juce::TextButton addFolderButton_{"Add folder"};
    juce::TextButton rescanButton_{"Rescan"};
    std::unique_ptr<juce::FileChooser> chooser_; // member: async lifetime

    juce::Label voiceLabel_;
    juce::Slider voiceLimit_;
    juce::Label thresholdLabel_;
    juce::Slider ramThreshold_;
    juce::Label thresholdHelp_;

    juce::TextEditor about_; // read-only, selectable (Label is not)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace fbsampler::ui
