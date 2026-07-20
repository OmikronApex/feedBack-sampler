#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "fbsampler/config_service.h"
#include "library_filter.h"

#include <functional>
#include <vector>

namespace fbsampler::ui {

// Self-contained browser pane (Story 3.3): search field, format filter
// pills, virtualized library list. Self-contained by design — Story 3.7
// re-parents it into an overlay below 900px width without rework. All
// actions run on the message thread; loading is delegated to the owner via
// onLoadRequested (which hands off to the processor's async command path).
class LibraryBrowserPane : public juce::Component,
                           private juce::ListBoxModel {
public:
    LibraryBrowserPane();

    /// Fired on Enter/double-click with the selected entry.
    std::function<void(const LibraryEntry&)> onLoadRequested;

    /// Replaces the data set (from the 3.2 index) and re-filters.
    void setEntries(std::vector<LibraryEntry> entries);

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key) override;

    /// Focus the search field (owner convenience).
    void focusSearch();

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width,
                          int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int lastRowSelected) override;
    juce::String getNameForRow(int rowNumber) override;
    juce::String getTooltipForRow(int rowNumber) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void refilter();
    void loadRow(int row);
    unsigned activePills() const;

    std::vector<LibraryEntry> entries_;
    std::vector<int> filtered_; // indices into entries_

    juce::TextEditor search_;
    juce::TextButton sfzPill_{"SFZ"}, soundfontPill_{"SF2/SF3"}, dsPill_{"DS"};
    juce::ListBox list_;
    // Story 3.6: selectable failure reason for the SELECTED failed row
    // (tooltips can't be selected).
    juce::TextEditor failureStrip_;
    bool failureStripVisible_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowserPane)
};

} // namespace fbsampler::ui
