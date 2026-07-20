#include "instrument_view_pane.h"

namespace fbsampler::ui {

namespace t = tokens;

namespace {
constexpr int kKnobSize = 72;
constexpr int kLabelHeight = 16;
constexpr int kCardWidth = 96;
constexpr int kCardHeight = kKnobSize + kLabelHeight + 8;
} // namespace

InstrumentViewPane::InstrumentViewPane()
{
    placeholder_.setText("Nothing loaded — pick a library from the browser",
                         juce::dontSendNotification);
    placeholder_.setColour(juce::Label::textColourId, t::color::textDim);
    placeholder_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(placeholder_);

    presetLabel_.setText("PRESET", juce::dontSendNotification);
    presetLabel_.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
    presetLabel_.setColour(juce::Label::textColourId, t::color::textDim);
    addChildComponent(presetLabel_);

    presetBox_.setTitle("Soundfont preset");
    presetBox_.setTextWhenNothingSelected("Select preset...");
    presetBox_.onChange = [this] {
        const int idx = presetBox_.getSelectedId() - 1;
        if (idx >= 0 && onPresetSelected)
            onPresetSelected(idx);
    };
    addChildComponent(presetBox_);
}

void InstrumentViewPane::rebuild(const ControlCardModel& model,
                                 const std::vector<juce::String>& presetNames,
                                 int activePresetIndex, bool hasLibrary)
{
    hasLibrary_ = hasLibrary;
    placeholder_.setVisible(!hasLibrary_);

    cards_.clear();
    if (hasLibrary_) {
        for (const auto& d : model.cards) {
            Card card;
            card.descriptor = d;
            card.knob = std::make_unique<FbKnob>();
            card.label = std::make_unique<juce::Label>();

            card.label->setText(juce::String(d.label).toUpperCase(),
                                juce::dontSendNotification);
            card.label->setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
            card.label->setColour(juce::Label::textColourId, t::color::textDim);
            card.label->setJustificationType(juce::Justification::centred);
            card.label->setAccessible(false); // knob carries the name

            auto& knob = *card.knob;
            knob.setTitle(d.accessibleName); // AD-11 accessible name
            using Kind = ControlCardDescriptor::Kind;
            switch (d.kind) {
            case Kind::cc:
                knob.setRange(0.0, 127.0, 1.0);
                knob.setUnit(FbKnob::Unit::plain);
                knob.setDefaultValue(0.0);
                knob.onValueChange = [this, cc = d.ccNumber, &knob] {
                    if (onCcChanged)
                        onCcChanged(cc, static_cast<int>(knob.getValue()));
                };
                break;
            case Kind::volume:
                knob.setRange(-60.0, 12.0, 0.1);
                knob.setUnit(FbKnob::Unit::decibels);
                knob.setDefaultValue(0.0);
                knob.setValue(0.0, juce::dontSendNotification);
                knob.onValueChange = [this] { notifyGeneric(false); };
                break;
            case Kind::pan:
                knob.setRange(-1.0, 1.0, 0.01);
                knob.setUnit(FbKnob::Unit::percent);
                knob.setDefaultValue(0.0);
                knob.setValue(0.0, juce::dontSendNotification);
                knob.onValueChange = [this] { notifyGeneric(false); };
                break;
            case Kind::tuning:
                knob.setRange(-100.0, 100.0, 1.0);
                knob.setUnit(FbKnob::Unit::cents);
                knob.setDefaultValue(0.0);
                knob.setValue(0.0, juce::dontSendNotification);
                knob.onDragEnd = [this] { notifyGeneric(true); };
                break;
            case Kind::attack:
            case Kind::release:
                knob.setRange(0.0, 4.0, 0.01);
                knob.setUnit(FbKnob::Unit::seconds);
                knob.setDefaultValue(0.0);
                knob.setValue(0.0, juce::dontSendNotification);
                knob.onDragEnd = [this] { notifyGeneric(true); };
                break;
            }
            addAndMakeVisible(knob);
            addAndMakeVisible(*card.label);
            cards_.push_back(std::move(card));
        }
    }

    presetRowVisible_ = hasLibrary_ && model.showPresetRow;
    presetBox_.setVisible(presetRowVisible_);
    presetLabel_.setVisible(presetRowVisible_);
    if (presetRowVisible_) {
        presetBox_.clear(juce::dontSendNotification);
        int id = 1;
        for (const auto& name : presetNames)
            presetBox_.addItem(name, id++);
        if (activePresetIndex >= 0)
            presetBox_.setSelectedId(activePresetIndex + 1,
                                     juce::dontSendNotification);
    }

    resized();
    repaint();
}

void InstrumentViewPane::notifyGeneric(bool gestureEnd)
{
    float volumeDb = 0.0f, pan = 0.0f, tuning = 0.0f;
    float attack = -1.0f, release = -1.0f;
    for (const auto& card : cards_) {
        const float v = static_cast<float>(card.knob->getValue());
        using Kind = ControlCardDescriptor::Kind;
        switch (card.descriptor.kind) {
        case Kind::volume: volumeDb = v; break;
        case Kind::pan: pan = v; break;
        case Kind::tuning: tuning = v; break;
        case Kind::attack: attack = v; break;
        case Kind::release: release = v; break;
        case Kind::cc: break;
        }
    }
    if (gestureEnd) {
        if (onModelOffsetsCommitted)
            onModelOffsetsCommitted(tuning, attack, release);
    } else if (onVolumePanChanged) {
        onVolumePanChanged(volumeDb, pan);
    }
}

void InstrumentViewPane::paint(juce::Graphics& g)
{
    g.fillAll(t::color::bg);
    if (hasLibrary_ && !cards_.empty()) {
        // Card surface behind the knob section (flat surface-step depth).
        auto cardArea = getLocalBounds().reduced(t::spacing::gutter);
        const int rows =
            1 + (static_cast<int>(cards_.size()) - 1)
                    / juce::jmax(1, (cardArea.getWidth() + t::spacing::unit * 2)
                                        / (kCardWidth + t::spacing::unit * 2));
        int top = cardArea.getY();
        if (presetRowVisible_)
            top += 40;
        g.setColour(t::color::surface);
        g.fillRoundedRectangle(
            juce::Rectangle<int>(cardArea.getX(), top, cardArea.getWidth(),
                                 rows * (kCardHeight + t::spacing::unit * 2)
                                     + t::spacing::unit * 2)
                .toFloat(),
            t::radius::card);
    }
}

void InstrumentViewPane::resized()
{
    auto area = getLocalBounds().reduced(t::spacing::gutter);
    placeholder_.setBounds(getLocalBounds());

    if (presetRowVisible_) {
        auto row = area.removeFromTop(32);
        presetLabel_.setBounds(row.removeFromLeft(64));
        presetBox_.setBounds(row.removeFromLeft(280));
        area.removeFromTop(t::spacing::unit * 2);
    }

    area.reduce(t::spacing::unit * 2, t::spacing::unit * 2);
    int x = area.getX(), y = area.getY() + t::spacing::unit * 2;
    for (auto& card : cards_) {
        if (x + kCardWidth > area.getRight()) {
            x = area.getX();
            y += kCardHeight + t::spacing::unit * 2;
        }
        card.knob->setBounds(x + (kCardWidth - kKnobSize) / 2, y, kKnobSize,
                             kKnobSize);
        card.label->setBounds(x, y + kKnobSize + 4, kCardWidth, kLabelHeight);
        x += kCardWidth + t::spacing::unit * 2;
    }
}

} // namespace fbsampler::ui
