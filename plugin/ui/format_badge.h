#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "tokens.h"

namespace fbsampler::ui {

// Format badge pill per DESIGN.md: 20%-opacity format-colored fill,
// full-strength uppercase label-scale text. SFZ = primary, SF2/SF3 = gold
// (the ONLY place gold appears), DS = good-green (frontend lands Epic 4;
// the badge ships now because it costs nothing).
class FormatBadge : public juce::Component {
public:
    enum class Format { sfz, sf2, sf3, ds };

    explicit FormatBadge(Format format = Format::sfz) { setFormat(format); }

    void setFormat(Format format)
    {
        format_ = format;
        // Color is never the sole signal (Story 3.7 AC2): the badge exposes
        // its format as text to assistive tech.
        setTitle(labelFor(format) + " format");
        repaint();
    }

    Format getFormat() const { return format_; }

    static juce::Colour colourFor(Format format)
    {
        switch (format) {
        case Format::sf2:
        case Format::sf3: return tokens::color::gold;
        case Format::ds: return tokens::color::good;
        case Format::sfz:
        default: return tokens::color::primary;
        }
    }

    static juce::String labelFor(Format format)
    {
        switch (format) {
        case Format::sf2: return "SF2";
        case Format::sf3: return "SF3";
        case Format::ds: return "DS";
        case Format::sfz:
        default: return "SFZ";
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto colour = colourFor(format_);
        g.setColour(colour.withAlpha(tokens::metric::badgeFillAlpha));
        g.fillRoundedRectangle(bounds, tokens::radius::pill);
        g.setColour(colour);
        g.setFont(juce::Font(juce::FontOptions(tokens::type::labelPx)));
        g.drawText(labelFor(format_), bounds, juce::Justification::centred,
                   false);
    }

private:
    Format format_ = Format::sfz;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FormatBadge)
};

} // namespace fbsampler::ui
