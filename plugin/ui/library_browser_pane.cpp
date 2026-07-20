#include "library_browser_pane.h"

#include "fb_look_and_feel.h"
#include "library_list_row.h"
#include "error_copy.h"
#include "tokens.h"

namespace fbsampler::ui {

namespace t = tokens;

namespace {

constexpr int kRowHeight = 28; // 4px grid

FormatBadge::Format badgeFormat(LibraryFormat f)
{
    switch (f) {
    case LibraryFormat::sf2: return FormatBadge::Format::sf2;
    case LibraryFormat::sf3: return FormatBadge::Format::sf3;
    case LibraryFormat::sfz:
    default: return FormatBadge::Format::sfz;
    }
}

juce::String sizeText(std::uint64_t bytes)
{
    if (bytes >= 1024ull * 1024ull * 1024ull)
        return juce::String(
                   static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 1)
               + " GB";
    if (bytes >= 1024ull * 1024ull)
        return juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0), 1)
               + " MB";
    return juce::String(static_cast<double>(bytes) / 1024.0, 1) + " KB";
}

const char* formatName(LibraryFormat f)
{
    switch (f) {
    case LibraryFormat::sf2: return "SF2";
    case LibraryFormat::sf3: return "SF3";
    case LibraryFormat::sfz:
    default: return "SFZ";
    }
}

void configurePill(juce::TextButton& pill, const juce::String& accessibleName,
                   std::function<void()> onToggle)
{
    pill.setClickingTogglesState(true);
    FbLookAndFeel::setSecondary(pill);
    pill.setTitle(accessibleName);
    pill.onClick = std::move(onToggle);
}

} // namespace

LibraryBrowserPane::LibraryBrowserPane()
{
    setWantsKeyboardFocus(false);

    search_.setTextToShowWhenEmpty("Search libraries...", t::color::textDim);
    search_.setTitle("Search libraries");
    search_.setEscapeAndReturnKeysConsumed(false);
    search_.onTextChange = [this] { refilter(); };
    // Arrow/Return keys the single-line TextEditor doesn't consume bubble up
    // to this component's keyPressed — that's how arrows from the search
    // field move the list selection (EXPERIENCE.md "type-to-search").
    addAndMakeVisible(search_);

    configurePill(sfzPill_, "Filter: SFZ format", [this] { refilter(); });
    configurePill(soundfontPill_, "Filter: SF2/SF3 format",
                  [this] { refilter(); });
    configurePill(dsPill_, "Filter: Decent Sampler format",
                  [this] { refilter(); });
    addAndMakeVisible(sfzPill_);
    addAndMakeVisible(soundfontPill_);
    addAndMakeVisible(dsPill_);

    failureStrip_.setReadOnly(true);
    failureStrip_.setMultiLine(true);
    failureStrip_.setEscapeAndReturnKeysConsumed(false); // Esc reaches owner
    failureStrip_.setTitle("Library load problem");
    addChildComponent(failureStrip_);

    list_.setModel(this);
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId,
                    juce::Colours::transparentBlack);
    list_.setTitle("Library list");
    addAndMakeVisible(list_);
}

void LibraryBrowserPane::setEntries(std::vector<LibraryEntry> entries)
{
    entries_ = std::move(entries);
    refilter();
}

void LibraryBrowserPane::paint(juce::Graphics& g)
{
    g.fillAll(t::color::sidebar);
    g.setColour(t::color::border);
    g.fillRect(getLocalBounds().removeFromRight(1));
}

void LibraryBrowserPane::resized()
{
    auto area = getLocalBounds().reduced(t::spacing::unit * 2);
    search_.setBounds(area.removeFromTop(30));
    area.removeFromTop(t::spacing::unit * 2);

    auto pillRow = area.removeFromTop(24);
    const int pillW = (pillRow.getWidth() - t::spacing::unit * 2) / 3;
    sfzPill_.setBounds(pillRow.removeFromLeft(pillW));
    pillRow.removeFromLeft(t::spacing::unit);
    soundfontPill_.setBounds(pillRow.removeFromLeft(pillW));
    pillRow.removeFromLeft(t::spacing::unit);
    dsPill_.setBounds(pillRow);
    area.removeFromTop(t::spacing::unit * 2);

    if (failureStripVisible_)
        failureStrip_.setBounds(area.removeFromBottom(56));
    list_.setBounds(area);
}

bool LibraryBrowserPane::keyPressed(const juce::KeyPress& key)
{
    // Reached via the search field's parent chain: arrows move list
    // selection while typing continues in the field; Enter loads.
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey) {
        const int rows = getNumRows();
        if (rows == 0)
            return true;
        const int current = list_.getSelectedRow();
        const int next = key == juce::KeyPress::downKey
                             ? juce::jmin(current + 1, rows - 1)
                             : juce::jmax(current - 1, 0);
        list_.selectRow(next);
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        loadRow(list_.getSelectedRow());
        return true;
    }
    return false;
}

void LibraryBrowserPane::focusSearch()
{
    search_.grabKeyboardFocus();
}

int LibraryBrowserPane::getNumRows()
{
    return static_cast<int>(filtered_.size());
}

void LibraryBrowserPane::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                          int width, int height,
                                          bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= getNumRows())
        return;
    const LibraryEntry& e =
        entries_[static_cast<std::size_t>(filtered_[static_cast<std::size_t>(rowNumber)])];

    LibraryListRow::State state;
    state.selected = rowIsSelected;
    // Failure UX is Story 3.6; render the status dot slot now.
    state.dot = e.ok ? LibraryListRow::StatusDot::none
                     : LibraryListRow::StatusDot::low;
    LibraryListRow::paint(g, { 0, 0, width, height }, e.displayName,
                          badgeFormat(e.format), sizeText(e.sizeBytes), state);
}

void LibraryBrowserPane::listBoxItemDoubleClicked(int row,
                                                  const juce::MouseEvent&)
{
    loadRow(row);
}

void LibraryBrowserPane::returnKeyPressed(int lastRowSelected)
{
    loadRow(lastRowSelected);
}

juce::String LibraryBrowserPane::getNameForRow(int rowNumber)
{
    if (rowNumber < 0 || rowNumber >= getNumRows())
        return {};
    const LibraryEntry& e =
        entries_[static_cast<std::size_t>(filtered_[static_cast<std::size_t>(rowNumber)])];
    juce::String name = juce::String(e.displayName) + ", "
                        + formatName(e.format) + ", " + sizeText(e.sizeBytes);
    if (!e.ok)
        name += ", failed: " + juce::String(userCopyForStatusDetail(e.statusDetail));
    return name;
}

juce::String LibraryBrowserPane::getTooltipForRow(int rowNumber)
{
    if (rowNumber < 0 || rowNumber >= getNumRows())
        return {};
    const LibraryEntry& e =
        entries_[static_cast<std::size_t>(filtered_[static_cast<std::size_t>(rowNumber)])];
    if (e.ok)
        return {};
    return juce::String(userCopyForStatusDetail(e.statusDetail));
}

void LibraryBrowserPane::selectedRowsChanged(int lastRowSelected)
{
    bool show = false;
    if (lastRowSelected >= 0 && lastRowSelected < getNumRows()) {
        const LibraryEntry& e = entries_[static_cast<std::size_t>(
            filtered_[static_cast<std::size_t>(lastRowSelected)])];
        if (!e.ok) {
            failureStrip_.setText(
                juce::String(userCopyForStatusDetail(e.statusDetail)),
                juce::dontSendNotification);
            show = true;
        }
    }
    if (show != failureStripVisible_) {
        failureStripVisible_ = show;
        failureStrip_.setVisible(show);
        resized();
    }
}

void LibraryBrowserPane::refilter()
{
    filtered_ = LibraryFilter::filter(
        entries_, search_.getText().toStdString(), activePills());
    list_.updateContent();
    list_.repaint();
}

void LibraryBrowserPane::loadRow(int row)
{
    if (row < 0 || row >= getNumRows())
        return;
    if (onLoadRequested)
        onLoadRequested(
            entries_[static_cast<std::size_t>(filtered_[static_cast<std::size_t>(row)])]);
}

unsigned LibraryBrowserPane::activePills() const
{
    unsigned pills = LibraryFilter::pillNone;
    if (sfzPill_.getToggleState())
        pills |= LibraryFilter::pillSfz;
    if (soundfontPill_.getToggleState())
        pills |= LibraryFilter::pillSoundfont;
    if (dsPill_.getToggleState())
        pills |= LibraryFilter::pillDs;
    return pills;
}

} // namespace fbsampler::ui
