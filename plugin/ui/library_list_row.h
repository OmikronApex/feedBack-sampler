#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "format_badge.h"
#include "tokens.h"

namespace fbsampler::ui {

// Paint helper for library list rows per DESIGN.md: transparent base,
// surface hover, selected rows get a 2px left accent in primary; optional
// status dot (good/mid/low) on the right. Header-only — the browser
// (Story 3.3) composes it into its ListBox model.
struct LibraryListRow {
    enum class StatusDot { none, good, mid, low };

    struct State {
        bool hovered = false;
        bool selected = false;
        StatusDot dot = StatusDot::none;
    };

    static juce::Colour dotColour(StatusDot dot)
    {
        switch (dot) {
        case StatusDot::good: return tokens::color::good;
        case StatusDot::mid: return tokens::color::mid;
        case StatusDot::low: return tokens::color::low;
        case StatusDot::none:
        default: return juce::Colours::transparentBlack;
        }
    }

    static void paint(juce::Graphics& g, juce::Rectangle<int> bounds,
                      const juce::String& name, FormatBadge::Format format,
                      const juce::String& sizeText, const State& state)
    {
        namespace t = tokens;
        auto area = bounds;

        if (state.hovered || state.selected) {
            g.setColour(t::color::surface);
            g.fillRect(area);
        }
        if (state.selected) {
            g.setColour(t::color::primary);
            g.fillRect(area.toFloat().removeFromLeft(
                t::metric::listRowAccentWidth));
        }

        area.reduce(t::spacing::unit * 3, 0);

        // Status dot slot (right edge).
        if (state.dot != StatusDot::none) {
            auto dotArea = area.removeFromRight(t::spacing::unit * 3);
            g.setColour(dotColour(state.dot));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f)
                              .withCentre(dotArea.toFloat().getCentre()));
        }

        // Size text (textDim, label scale).
        if (sizeText.isNotEmpty()) {
            g.setColour(t::color::textDim);
            g.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
            const auto sizeArea = area.removeFromRight(t::spacing::unit * 14);
            g.drawText(sizeText, sizeArea, juce::Justification::centredRight,
                       false);
        }

        // Format badge pill.
        {
            const auto badgeArea =
                area.removeFromRight(t::spacing::unit * 10)
                    .withSizeKeepingCentre(t::spacing::unit * 9,
                                           t::spacing::unit * 4 + 2);
            const auto colour = FormatBadge::colourFor(format);
            g.setColour(colour.withAlpha(t::metric::badgeFillAlpha));
            g.fillRoundedRectangle(badgeArea.toFloat(), t::radius::pill);
            g.setColour(colour);
            g.setFont(juce::Font(juce::FontOptions(t::type::labelPx)));
            g.drawText(FormatBadge::labelFor(format), badgeArea,
                       juce::Justification::centred, false);
        }

        // Name (primary text, body scale).
        g.setColour(t::color::text);
        g.setFont(juce::Font(juce::FontOptions(t::type::bodyPx)));
        g.drawText(name, area, juce::Justification::centredLeft, true);
    }
};

} // namespace fbsampler::ui
