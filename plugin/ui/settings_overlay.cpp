#include "settings_overlay.h"

#include "../plugin_processor.h"
#include "fb_look_and_feel.h"
#include "fbsampler/model.h"
#include "fbsampler/version.h"
#include "licenses.h"

namespace fbsampler::ui {

namespace t = tokens;

// One configured folder: selectable path + inline two-step remove
// ("Remove" -> "Remove?" confirm state; destructive color, never a
// modal-on-modal).
class SettingsOverlay::FolderRow : public juce::Component {
public:
    FolderRow(std::string path, std::function<void(const std::string&)> onRemove)
        : path_(std::move(path)), onRemove_(std::move(onRemove))
    {
        pathText_.setReadOnly(true);
        pathText_.setMultiLine(false);
        pathText_.setEscapeAndReturnKeysConsumed(false); // Esc closes overlay
        pathText_.setText(juce::String(path_), juce::dontSendNotification);
        pathText_.setTitle("Library folder path");
        addAndMakeVisible(pathText_);

        removeButton_.setButtonText("Remove");
        removeButton_.setColour(juce::TextButton::buttonColourId,
                                t::color::destructive);
        removeButton_.setTitle("Remove this library folder");
        removeButton_.onClick = [this] {
            if (!confirming_) {
                confirming_ = true;
                removeButton_.setButtonText("Remove?");
                return;
            }
            if (onRemove_)
                onRemove_(path_);
        };
        addAndMakeVisible(removeButton_);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        removeButton_.setBounds(area.removeFromRight(88));
        area.removeFromRight(t::spacing::unit * 2);
        pathText_.setBounds(area);
    }

private:
    std::string path_;
    std::function<void(const std::string&)> onRemove_;
    juce::TextEditor pathText_;
    juce::TextButton removeButton_;
    bool confirming_ = false;
};

SettingsOverlay::SettingsOverlay(PluginProcessor& processor)
    : processor_(processor)
{
    setWantsKeyboardFocus(true);
    // Story 3.7: focus trap — while the overlay is open, tab cycles inside
    // it (it covers the whole editor, so containment = trapping).
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    title_.setText("Settings", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(t::type::headerPx,
                                                juce::Font::bold)));
    title_.setColour(juce::Label::textColourId, t::color::text);
    addAndMakeVisible(title_);

    FbLookAndFeel::setSecondary(closeButton_);
    closeButton_.setTitle("Close settings");
    closeButton_.onClick = [this] { close(); };
    addAndMakeVisible(closeButton_);

    foldersLabel_.setText("LIBRARY FOLDERS", juce::dontSendNotification);
    foldersLabel_.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    foldersLabel_.setColour(juce::Label::textColourId, t::color::textDim);
    addAndMakeVisible(foldersLabel_);

    addFolderButton_.setTitle("Add a library folder");
    addFolderButton_.onClick = [this] { addFolder(); };
    addAndMakeVisible(addFolderButton_);

    FbLookAndFeel::setSecondary(rescanButton_);
    rescanButton_.setTitle("Rescan library folders");
    rescanButton_.onClick = [this] { processor_.rescanLibrariesAsync(); };
    addAndMakeVisible(rescanButton_);

    voiceLabel_.setText("VOICE LIMIT", juce::dontSendNotification);
    voiceLabel_.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    voiceLabel_.setColour(juce::Label::textColourId, t::color::textDim);
    addAndMakeVisible(voiceLabel_);

    voiceLimit_.setSliderStyle(juce::Slider::LinearHorizontal);
    voiceLimit_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
    voiceLimit_.setRange(16.0, 256.0, 1.0);
    voiceLimit_.setTitle("Voice limit");
    voiceLimit_.onDragEnd = [this] {
        const int limit = static_cast<int>(voiceLimit_.getValue());
        persistSettings([limit](SamplerSettings& s) { s.voiceLimit = limit; });
        processor_.applyVoiceLimitAsync(limit); // live, via snapshot rebuild
    };
    addAndMakeVisible(voiceLimit_);

    thresholdLabel_.setText("RAM / STREAMING THRESHOLD (MB)",
                            juce::dontSendNotification);
    thresholdLabel_.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    thresholdLabel_.setColour(juce::Label::textColourId, t::color::textDim);
    addAndMakeVisible(thresholdLabel_);

    ramThreshold_.setSliderStyle(juce::Slider::LinearHorizontal);
    ramThreshold_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    ramThreshold_.setRange(64.0, 8192.0, 64.0);
    ramThreshold_.setTitle("RAM streaming threshold in megabytes");
    ramThreshold_.onDragEnd = [this] {
        const int mb = static_cast<int>(ramThreshold_.getValue());
        persistSettings(
            [mb](SamplerSettings& s) { s.ramStreamThresholdMb = mb; });
    };
    addAndMakeVisible(ramThreshold_);

    // Honest label (UX-DR13): stored now, no audible effect until Epic 5.
    thresholdHelp_.setText(
        "Libraries larger than this stream from disk once streaming ships. "
        "Stored now, applied in a later update.",
        juce::dontSendNotification);
    thresholdHelp_.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    thresholdHelp_.setColour(juce::Label::textColourId, t::color::textDim);
    addAndMakeVisible(thresholdHelp_);

    about_.setReadOnly(true);
    about_.setMultiLine(true);
    about_.setEscapeAndReturnKeysConsumed(false); // Esc must close overlay
    about_.setScrollbarsShown(true);
    about_.setTitle("About and licenses");
    addAndMakeVisible(about_);

    setVisible(false);
}

SettingsOverlay::~SettingsOverlay() = default;

void SettingsOverlay::open()
{
    const SamplerSettings s = processor_.configService().settings();
    voiceLimit_.setValue(s.voiceLimit, juce::dontSendNotification);
    ramThreshold_.setValue(s.ramStreamThresholdMb, juce::dontSendNotification);
    about_.setText(juce::String("feedBack Sampler ") + coreVersion() + "\n"
                       + "Model schema v"
                       + juce::String(kModelSchemaVersion) + "\n\n"
                       + kLicenseNotices,
                   juce::dontSendNotification);
    rebuildFolderList();
    setVisible(true);
    toFront(true);
    grabKeyboardFocus(); // focus trapped: overlay covers the whole editor
}

void SettingsOverlay::close()
{
    setVisible(false);
    if (onClosed)
        onClosed();
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.6f)); // scrim per DESIGN.md
    g.setColour(t::color::surface);
    g.fillRoundedRectangle(panelBounds_.toFloat(), t::radius::card);
    g.setColour(t::color::border);
    g.drawRoundedRectangle(panelBounds_.toFloat().reduced(0.5f),
                           t::radius::card, 1.0f);
}

void SettingsOverlay::resized()
{
    panelBounds_ = getLocalBounds().withSizeKeepingCentre(
        juce::jmin(560, getWidth() - t::spacing::gutter * 2),
        juce::jmin(520, getHeight() - t::spacing::gutter * 2));

    auto area = panelBounds_.reduced(t::spacing::gutter);
    auto top = area.removeFromTop(32);
    closeButton_.setBounds(top.removeFromRight(80));
    title_.setBounds(top);
    area.removeFromTop(t::spacing::unit * 2);

    foldersLabel_.setBounds(area.removeFromTop(18));
    for (auto& row : folderRows_) {
        row->setBounds(area.removeFromTop(26));
        area.removeFromTop(t::spacing::unit);
    }
    auto folderButtons = area.removeFromTop(28);
    addFolderButton_.setBounds(folderButtons.removeFromLeft(110));
    folderButtons.removeFromLeft(t::spacing::unit * 2);
    rescanButton_.setBounds(folderButtons.removeFromLeft(90));
    area.removeFromTop(t::spacing::unit * 3);

    voiceLabel_.setBounds(area.removeFromTop(18));
    voiceLimit_.setBounds(area.removeFromTop(28));
    area.removeFromTop(t::spacing::unit * 2);
    thresholdLabel_.setBounds(area.removeFromTop(18));
    ramThreshold_.setBounds(area.removeFromTop(28));
    thresholdHelp_.setBounds(area.removeFromTop(18));
    area.removeFromTop(t::spacing::unit * 3);

    about_.setBounds(area);
}

void SettingsOverlay::mouseUp(const juce::MouseEvent& e)
{
    if (!panelBounds_.contains(e.getPosition()))
        close();
}

bool SettingsOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) {
        close();
        return true;
    }
    return false;
}

void SettingsOverlay::rebuildFolderList()
{
    folderRows_.clear();
    for (const auto& folder : processor_.configService().settings().libraryFolders) {
        auto row = std::make_unique<FolderRow>(
            folder, [this](const std::string& path) { removeFolder(path); });
        addAndMakeVisible(*row);
        folderRows_.push_back(std::move(row));
    }
    resized();
    repaint();
}

void SettingsOverlay::persistSettings(
    const std::function<void(SamplerSettings&)>& mutate)
{
    // AD-9: the config service is the ONLY write path; write-through on
    // every change so settings take effect without plugin reload.
    SamplerSettings s = processor_.configService().settings();
    mutate(s);
    processor_.configService().saveSettings(s);
}

void SettingsOverlay::addFolder() // public: 3.6 empty state calls directly
{
    chooser_ = std::make_unique<juce::FileChooser>("Choose a library folder");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<SettingsOverlay>(this)](
            const juce::FileChooser& fc) {
            if (safeThis == nullptr)
                return;
            const juce::File dir = fc.getResult();
            if (!dir.isDirectory())
                return;
            const std::string path = dir.getFullPathName().toStdString();
            safeThis->persistSettings([&path](SamplerSettings& s) {
                for (const auto& existing : s.libraryFolders)
                    if (existing == path)
                        return;
                s.libraryFolders.push_back(path);
            });
            safeThis->rebuildFolderList();
            // First-run behavior (3.2): adding a folder scans it immediately.
            safeThis->processor_.rescanLibrariesAsync();
        });
}

void SettingsOverlay::removeFolder(const std::string& path)
{
    persistSettings([&path](SamplerSettings& s) {
        s.libraryFolders.erase(
            std::remove(s.libraryFolders.begin(), s.libraryFolders.end(), path),
            s.libraryFolders.end());
    });
    rebuildFolderList();
    // Entries under the removed root leave the index on the next scan write.
    processor_.rescanLibrariesAsync();
}

} // namespace fbsampler::ui
