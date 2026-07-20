// Component gallery — throwaway dev target (Story 3.1 AC2), NOT a maintained
// product surface. Shows every FbLookAndFeel component in all states:
// buttons (primary/secondary x normal/hover/focused/disabled), knob, badges
// of all three format colors, progress 0/50/100, list rows
// normal/selected/failed, scrollbar, text editor, combo box, tooltip.
// Build with -DFBSAMPLER_BUILD_GALLERY=ON; CI does not build it.

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/fb_knob.h"
#include "ui/fb_look_and_feel.h"
#include "ui/format_badge.h"
#include "ui/library_list_row.h"
#include "ui/tokens.h"

namespace t = fbsampler::ui::tokens;
using fbsampler::ui::FbKnob;
using fbsampler::ui::FbLookAndFeel;
using fbsampler::ui::FormatBadge;
using fbsampler::ui::LibraryListRow;

namespace {

class RowStrip : public juce::Component {
public:
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        const int rowH = area.getHeight() / 3;
        LibraryListRow::paint(g, area.removeFromTop(rowH), "Salamander Piano",
                              FormatBadge::Format::sfz, "1.2 GB", {});
        LibraryListRow::paint(g, area.removeFromTop(rowH), "GeneralUser GS",
                              FormatBadge::Format::sf2, "30 MB",
                              {false, true, LibraryListRow::StatusDot::good});
        LibraryListRow::paint(g, area, "Broken Library",
                              FormatBadge::Format::ds, "12 MB",
                              {false, false, LibraryListRow::StatusDot::low});
    }
};

class GalleryComponent : public juce::Component {
public:
    GalleryComponent()
    {
        setLookAndFeel(&laf_);

        auto addButton = [this](juce::TextButton& b, const juce::String& text,
                                bool secondary, bool enabled) {
            b.setButtonText(text);
            FbLookAndFeel::setSecondary(b, secondary);
            b.setEnabled(enabled);
            addAndMakeVisible(b);
        };
        addButton(primary_, "Primary", false, true);
        addButton(primaryDisabled_, "Disabled", false, false);
        addButton(secondary_, "Secondary", true, true);
        addButton(secondaryDisabled_, "Disabled", true, false);

        knob_.setRange(0.0, 1.0);
        knob_.setDefaultValue(0.5);
        knob_.setValue(0.7);
        addAndMakeVisible(knob_);
        knobDisabled_.setRange(0.0, 1.0);
        knobDisabled_.setValue(0.3);
        knobDisabled_.setEnabled(false);
        addAndMakeVisible(knobDisabled_);
        knobLabel_.setText("CUTOFF", juce::dontSendNotification);
        knobLabel_.setColour(juce::Label::textColourId, t::color::textDim);
        knobLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knobLabel_);

        for (auto* b : { &sfzBadge_, &sf2Badge_, &sf3Badge_, &dsBadge_ })
            addAndMakeVisible(*b);

        for (auto* p : { &progress0_, &progress50_, &progress100_ })
            addAndMakeVisible(*p);

        editor_.setText("Selectable text editor");
        addAndMakeVisible(editor_);

        combo_.addItemList({ "Preset A", "Preset B", "Preset C" }, 1);
        combo_.setSelectedId(1);
        addAndMakeVisible(combo_);

        addAndMakeVisible(rows_);

        viewportContent_.setSize(200, 1200);
        viewport_.setViewedComponent(&viewportContent_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);

        setSize(760, 560);
    }

    ~GalleryComponent() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override { g.fillAll(t::color::bg); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(t::spacing::gutter);

        auto buttons = area.removeFromTop(40);
        for (auto* b :
             { &primary_, &primaryDisabled_, &secondary_, &secondaryDisabled_ }) {
            b->setBounds(buttons.removeFromLeft(120));
            buttons.removeFromLeft(t::spacing::unit * 2);
        }
        area.removeFromTop(t::spacing::gutter);

        auto knobRow = area.removeFromTop(96);
        knob_.setBounds(knobRow.removeFromLeft(88));
        knobDisabled_.setBounds(knobRow.removeFromLeft(88));
        knobLabel_.setBounds(knob_.getX(), knob_.getBottom() - 4,
                             knob_.getWidth(), 16);

        auto badges = knobRow.removeFromLeft(320).withSizeKeepingCentre(320, 22);
        for (auto* b : { &sfzBadge_, &sf2Badge_, &sf3Badge_, &dsBadge_ }) {
            static_cast<juce::Component*>(b)->setBounds(
                badges.removeFromLeft(56));
            badges.removeFromLeft(t::spacing::unit * 2);
        }
        area.removeFromTop(t::spacing::gutter);

        auto progressRow = area.removeFromTop(20);
        for (auto* p : { &progress0_, &progress50_, &progress100_ }) {
            p->setBounds(progressRow.removeFromLeft(160));
            progressRow.removeFromLeft(t::spacing::unit * 2);
        }
        area.removeFromTop(t::spacing::gutter);

        auto inputRow = area.removeFromTop(32);
        editor_.setBounds(inputRow.removeFromLeft(220));
        inputRow.removeFromLeft(t::spacing::unit * 2);
        combo_.setBounds(inputRow.removeFromLeft(160));
        area.removeFromTop(t::spacing::gutter);

        viewport_.setBounds(area.removeFromRight(120));
        rows_.setBounds(area.removeFromTop(96));
    }

private:
    FbLookAndFeel laf_;
    juce::TooltipWindow tooltips_{this};

    juce::TextButton primary_, primaryDisabled_, secondary_, secondaryDisabled_;
    FbKnob knob_, knobDisabled_;
    juce::Label knobLabel_;
    FormatBadge sfzBadge_{FormatBadge::Format::sfz};
    FormatBadge sf2Badge_{FormatBadge::Format::sf2};
    FormatBadge sf3Badge_{FormatBadge::Format::sf3};
    FormatBadge dsBadge_{FormatBadge::Format::ds};
    double p0_ = 0.0, p50_ = 0.5, p100_ = 1.0;
    juce::ProgressBar progress0_{p0_}, progress50_{p50_}, progress100_{p100_};
    juce::TextEditor editor_;
    juce::ComboBox combo_;
    RowStrip rows_;
    juce::Viewport viewport_;
    juce::Component viewportContent_;
};

class GalleryWindow : public juce::DocumentWindow {
public:
    GalleryWindow()
        : DocumentWindow("feedBack Sampler — Component Gallery",
                         t::color::bg, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new GalleryComponent(), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class GalleryApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "fbsampler-gallery"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    void initialise(const juce::String&) override
    {
        window_ = std::make_unique<GalleryWindow>();
    }
    void shutdown() override { window_.reset(); }

private:
    std::unique_ptr<GalleryWindow> window_;
};

} // namespace

START_JUCE_APPLICATION(GalleryApp)
