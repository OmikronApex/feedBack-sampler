#include "plugin_editor.h"

#include "ui/control_card_model.h"
#include "ui/tokens.h"

#include <algorithm>

namespace fbsampler {

namespace t = ui::tokens;

namespace {
constexpr int kHeaderHeight = 48;
constexpr int kSidebarWidth = 260;
constexpr int kStatusHeight = 56;

ui::FormatBadge::Format badgeForStatus(bool isSoundfont)
{
    return isSoundfont ? ui::FormatBadge::Format::sf2
                       : ui::FormatBadge::Format::sfz;
}

} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    // Capture the stored size FIRST: construction triggers resized() calls
    // (refreshFromProcessor) that would overwrite the persisted value.
    const int storedW = processor_.lastEditorWidth();
    const int storedH = processor_.lastEditorHeight();

    // Story 3.1: shared feedBack LookAndFeel applies plugin-wide from here.
    setLookAndFeel(&lookAndFeel_);

    setWantsKeyboardFocus(true); // Esc handling when nothing else consumes it

    header_.onBrowserToggled = [this](bool visible) {
        setBrowserShowing(visible);
    };
    header_.onSettingsClicked = [this] { settings_.open(); };
    addAndMakeVisible(header_);

    browser_.onLoadRequested = [this](const LibraryEntry& entry) {
        requestLoad(entry);
    };
    browserScrim_.onDismiss = [this] { setBrowserShowing(false); };
    addChildComponent(browserScrim_);
    addAndMakeVisible(browser_);

    instrumentView_.setWantsKeyboardFocus(true);
    instrumentView_.setTitle("Instrument view");
    instrumentView_.onCcChanged = [this](int cc, int value) {
        processor_.setControlValue(cc, value);
    };
    instrumentView_.onVolumePanChanged = [this](float volumeDb, float pan) {
        processor_.setMasterVolumeDb(volumeDb);
        processor_.setMasterPan(pan);
    };
    instrumentView_.onModelOffsetsCommitted =
        [this](float tuning, float attack, float release) {
            processor_.applyModelOffsetsAsync(tuning, attack, release);
        };
    instrumentView_.onPresetSelected = [this](int index) {
        processor_.selectPreset(index);
    };
    addAndMakeVisible(instrumentView_);

    statusLabel_.setJustificationType(juce::Justification::topLeft);
    statusLabel_.setMinimumHorizontalScale(1.0f);
    statusLabel_.setColour(juce::Label::textColourId, t::color::textDim);
    statusLabel_.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    statusLabel_.setTitle("Load status");
    addAndMakeVisible(statusLabel_);

    addChildComponent(scanBanner_);
    emptyState_.onAddFolder = [this] { settings_.addFolder(); };
    addChildComponent(emptyState_);

    addChildComponent(settings_); // Story 3.5 overlay, on top of everything
    settings_.onClosed = [this] { instrumentView_.grabKeyboardFocus(); };

    processor_.addChangeListener(this);
    refreshFromProcessor();

    startTimerHz(10); // voice-count readout poll

    setResizable(true, true);
    setResizeLimits(t::layout::minWidth, t::layout::minHeight, 4096, 4096);
    // Story 3.7: a reopened editor keeps its last size (per host session).
    setSize(storedW > 0 ? storedW : 960, storedH > 0 ? storedH : 600);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    // Clear before lookAndFeel_ is destroyed (child components hold raw
    // references to it).
    setLookAndFeel(nullptr);
    processor_.removeChangeListener(this);
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(t::color::bg);
}

void PluginEditor::paintOverChildren(juce::Graphics& g)
{
    if (dragHover_) {
        g.setColour(t::color::primary);
        g.drawRect(getLocalBounds(), 2); // drop hint
    }
}

void PluginEditor::resized()
{
    if (getWidth() >= t::layout::minWidth && getHeight() >= t::layout::minHeight)
        processor_.setLastEditorSize(getWidth(), getHeight());

    // Story 3.7 breakpoint: below browserBreakpoint the browser stops
    // docking and becomes an overlay. Crossing the breakpoint collapses /
    // re-docks it; the header toggle always reflects the result.
    const bool narrow = getWidth() < t::layout::browserBreakpoint;
    if (narrow != overlayMode_) {
        overlayMode_ = narrow;
        setBrowserShowing(!narrow);
        return; // setBrowserShowing re-enters resized() with settled state
    }

    auto area = getLocalBounds();
    header_.setBounds(area.removeFromTop(kHeaderHeight));

    if (scanBanner_.isVisible())
        scanBanner_.setBounds(area.removeFromTop(24));

    const auto contentArea = area;
    if (!overlayMode_ && browserVisible_)
        browser_.setBounds(area.removeFromLeft(kSidebarWidth));

    statusLabel_.setBounds(
        area.removeFromBottom(kStatusHeight).reduced(t::spacing::unit * 3));
    instrumentView_.setBounds(area);
    emptyState_.setBounds(area);

    // Overlay presentation: scrim over the content, browser panel on top of
    // it at the left edge (same pane, re-hosted — 3.3's design).
    const bool overlayOpen = overlayMode_ && browserVisible_;
    browserScrim_.setVisible(overlayOpen);
    browser_.setVisible(browserVisible_);
    if (overlayOpen) {
        browserScrim_.setBounds(contentArea);
        browser_.setBounds(contentArea.withWidth(kSidebarWidth));
        browserScrim_.toFront(false);
        browser_.toFront(false);
    }

    settings_.setBounds(getLocalBounds());
    if (settings_.isVisible())
        settings_.toFront(false); // settings stays above the browser overlay
}

bool PluginEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && overlayMode_ && browserVisible_) {
        setBrowserShowing(false);
        return true;
    }
    return false;
}

void PluginEditor::setBrowserShowing(bool visible)
{
    browserVisible_ = visible;
    browser_.setVisible(visible);
    header_.setBrowserToggle(visible);
    if (!visible && overlayMode_)
        instrumentView_.grabKeyboardFocus(); // focus returns to the invoker side
    resized();
}

// ChangeListener delivery already happens on the message thread (JUCE
// dispatches async), so touching components here is safe even though the
// loader completes on a background thread.
void PluginEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
}

void PluginEditor::timerCallback()
{
    header_.setVoiceCount(processor_.activeVoiceCount());
}

void PluginEditor::refreshFromProcessor()
{
    statusLabel_.setText(processor_.getLoadStatusText(),
                         juce::dontSendNotification);
    browser_.setEntries(processor_.configService().index().entries);

    const juce::String status = processor_.getLoadStatusText();
    const bool hasLibrary = status.startsWith("loaded");
    const bool isSoundfont = processor_.isSoundfontLoaded();

    juce::String libraryName;
    if (hasLibrary)
        libraryName = status.upToFirstOccurrenceOf("\n", false, false)
                          .fromFirstOccurrenceOf("loaded ", false, false);
    header_.setLibrary(libraryName, badgeForStatus(isSoundfont), hasLibrary);

    const auto cardModel =
        ui::buildControlCards(processor_.currentControls(), isSoundfont);
    std::vector<juce::String> presetNames;
    for (const auto& p : processor_.currentPresets())
        presetNames.push_back(juce::String(p.bank) + ":"
                              + juce::String(p.program) + " "
                              + juce::String(p.name));
    instrumentView_.rebuild(cardModel, presetNames,
                            processor_.currentPresetIndex(), hasLibrary);

    // Story 3.6 state switching: scanning banner + empty state.
    const bool scanning = processor_.isScanInProgress();
    scanBanner_.setVisible(scanning);
    if (scanning)
        scanBanner_.setProgressText(processor_.scanProgressText());

    const bool empty = !hasLibrary
        && processor_.configService().settings().libraryFolders.empty()
        && processor_.configService().index().entries.empty();
    emptyState_.setVisible(empty);
    instrumentView_.setVisible(!empty);
    resized();
}

bool PluginEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return !files.isEmpty();
}

void PluginEditor::fileDragEnter(const juce::StringArray&, int, int)
{
    dragHover_ = true;
    repaint();
}

void PluginEditor::fileDragExit(const juce::StringArray&)
{
    dragHover_ = false;
    repaint();
}

void PluginEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover_ = false;
    repaint();

    // One drop payload at a time keeps state comprehensible.
    if (!files.isEmpty()) {
        const auto& file = files.getReference(0);
        const std::string path = file.toStdString();
        const bool isDir = juce::File(file).isDirectory();
        const auto action = ui::decideDropAction(
            path, isDir,
            processor_.configService().settings().libraryFolders);

        switch (action.kind) {
        case ui::DropAction::Kind::addFolder: {
            SamplerSettings s = processor_.configService().settings();
            if (std::find(s.libraryFolders.begin(), s.libraryFolders.end(),
                          path) == s.libraryFolders.end()) {
                s.libraryFolders.push_back(path);
                processor_.configService().saveSettings(s);
            }
            processor_.rescanLibrariesAsync();
            break;
        }
        case ui::DropAction::Kind::registerAndLoad:
            // AD-4: mint the single-file index entry FIRST so the library
            // has an identity, then load through the normal command path.
            processor_.configService().registerFile(path, action.format);
            [[fallthrough]];
        case ui::DropAction::Kind::rescanAndLoad: {
            if (action.kind == ui::DropAction::Kind::rescanAndLoad)
                processor_.configService().registerFile(path, action.format);
            LibraryEntry entry;
            entry.path = path;
            entry.format = action.format;
            requestLoad(entry);
            break;
        }
        case ui::DropAction::Kind::rejectUnsupported:
            // UX-DR13: name the problem + supported formats, no dialogs.
            statusLabel_.setText(
                juce::File(file).getFileName()
                    + " is not a supported library. Supported formats: "
                      "SFZ, SF2, SF3.",
                juce::dontSendNotification);
            break;
        }
    }
    refreshFromProcessor();
}

void PluginEditor::requestLoad(const LibraryEntry& entry)
{
    // Guard choice (documented): a load while one is in flight is IGNORED —
    // the processor returns false and updates the status text; no queueing.
    // Dispatch through the EXISTING command path only (AD-3).
    const juce::String path(entry.path);
    switch (entry.format) {
    case LibraryFormat::sfz: processor_.loadSfzFileAsync(path); break;
    case LibraryFormat::sf2:
    case LibraryFormat::sf3: processor_.loadSf2FileAsync(path); break;
    }
    // In overlay mode a selection also dismisses the overlay.
    if (overlayMode_ && browserVisible_)
        setBrowserShowing(false);
    // Selection loads and returns focus to the instrument view area
    // (EXPERIENCE.md IA).
    instrumentView_.grabKeyboardFocus();
}

} // namespace fbsampler
