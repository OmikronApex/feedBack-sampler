#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin_processor.h"
#include "ui/fb_look_and_feel.h"
#include "ui/header_bar.h"
#include "ui/instrument_view_pane.h"
#include "ui/library_browser_pane.h"
#include "ui/drop_action.h"
#include "ui/empty_state.h"
#include "ui/scan_banner.h"
#include "ui/settings_overlay.h"

namespace fbsampler {

// Stories 3.3/3.4 IA shell: HeaderBar (48px), left library browser pane,
// instrument view in the main area. The editor only issues commands and
// renders results (AD-3: UI never mutates engine data directly).
class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::Timer {
public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override; // drop hint border
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override; // Esc closes overlay

    // Story 3.7 breakpoint state (also used by the automated tests).
    bool isBrowserOverlayMode() const { return overlayMode_; }
    bool isBrowserShowing() const { return browserVisible_; }

    // Drag-and-drop (Story 3.6 AC3): editor-wide target.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override; // ~10 Hz voice-count poll
    void refreshFromProcessor();
    void requestLoad(const LibraryEntry& entry);

    PluginProcessor& processor_;
    // Declared before any component so it is destroyed last; the destructor
    // additionally clears the editor's LookAndFeel before teardown (classic
    // JUCE dangling-LookAndFeel crash).
    ui::FbLookAndFeel lookAndFeel_;

    // Scrim behind the collapsed-browser overlay (Story 3.7, <900px).
    class BrowserScrim : public juce::Component {
    public:
        std::function<void()> onDismiss;
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.6f));
        }
        void mouseUp(const juce::MouseEvent&) override
        {
            if (onDismiss)
                onDismiss();
        }
    };

    void setBrowserShowing(bool visible);

    ui::HeaderBar header_;
    ui::LibraryBrowserPane browser_;
    bool browserVisible_ = true;
    bool overlayMode_ = false; // width < tokens::layout::browserBreakpoint
    BrowserScrim browserScrim_;
    ui::InstrumentViewPane instrumentView_;
    juce::Label statusLabel_; // load status strip under the instrument view
    ui::ScanBanner scanBanner_;
    ui::EmptyState emptyState_;
    juce::TooltipWindow tooltips_{this};
    bool dragHover_ = false;
    ui::SettingsOverlay settings_{processor_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace fbsampler
