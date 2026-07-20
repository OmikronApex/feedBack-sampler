// Story 3.7: keyboard traversal, accessible-name completeness, breakpoint
// behavior, and WCAG AA contrast of the designated token pairs.

#include "plugin_editor.h"
#include "plugin_processor.h"
#include "ui/tokens.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace fbsampler;
namespace t = fbsampler::ui::tokens;

namespace {

struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};

struct EditorFixture {
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    PluginEditor* fb = nullptr;

    EditorFixture()
    {
        editor.reset(processor.createEditor());
        fb = dynamic_cast<PluginEditor*>(editor.get());
    }
};

// isShowing() is false headless (no desktop peer) — check the visibility
// chain up to the editor instead.
bool visibleWithin(const juce::Component& c, const juce::Component& root)
{
    for (const juce::Component* p = &c; p != nullptr && p != &root;
         p = p->getParentComponent())
        if (!p->isVisible())
            return false;
    return true;
}

void walk(juce::Component& c, const std::function<void(juce::Component&)>& fn)
{
    fn(c);
    for (auto* child : c.getChildren())
        walk(*child, fn);
}

// WCAG 2.x relative luminance + contrast ratio.
double channelLin(double srgb)
{
    return srgb <= 0.04045 ? srgb / 12.92
                           : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double luminance(juce::Colour c)
{
    return 0.2126 * channelLin(c.getFloatRed())
           + 0.7152 * channelLin(c.getFloatGreen())
           + 0.0722 * channelLin(c.getFloatBlue());
}

double contrastRatio(juce::Colour a, juce::Colour b)
{
    const double la = luminance(a), lb = luminance(b);
    const double lighter = std::max(la, lb), darker = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

} // namespace

TEST_CASE("focus order reaches every operable surface control", "[editor][a11y]")
{
    JuceEnv env;
    EditorFixture fx;
    REQUIRE(fx.fb != nullptr);
    fx.fb->setSize(960, 600); // docked mode: all three surfaces present

    // Collect focusable, showing components via the editor's focus chain
    // source of truth (component tree walk; JUCE traverser order derives
    // from the same set).
    std::vector<juce::String> titles;
    walk(*fx.fb, [&titles, &fx](juce::Component& c) {
        if (c.getWantsKeyboardFocus() && visibleWithin(c, *fx.fb))
            titles.push_back(c.getTitle());
    });

    auto reached = [&titles](const juce::String& title) {
        for (const auto& s : titles)
            if (s == title)
                return true;
        return false;
    };

    CHECK(reached("Show or hide the library browser"));
    CHECK(reached("Settings"));
    CHECK(reached("Search libraries"));
    CHECK(reached("Library list"));
    CHECK(reached("Filter: SFZ format"));
    // The main area shows the instrument view — or, on a machine with no
    // configured folders and an empty index (fresh CI), the empty state
    // with its Add-folder button. Either way it must be reachable.
    CHECK((reached("Instrument view") || reached("Add a library folder")));
}

TEST_CASE("no showing focusable component lacks an accessible title",
          "[editor][a11y]")
{
    JuceEnv env;
    EditorFixture fx;
    REQUIRE(fx.fb != nullptr);
    fx.fb->setSize(960, 600);

    walk(*fx.fb, [&fx](juce::Component& c) {
        if (!c.getWantsKeyboardFocus() || !visibleWithin(c, *fx.fb)
            || !c.isAccessible())
            return;
        // The editor itself is a container, not an operable control.
        if (dynamic_cast<juce::AudioProcessorEditor*>(&c) != nullptr)
            return;
        INFO("untitled focusable component: "
             << typeid(c).name());
        CHECK(c.getTitle().isNotEmpty());
    });
}

TEST_CASE("browser collapses to overlay below the 900px breakpoint",
          "[editor][breakpoint]")
{
    JuceEnv env;
    EditorFixture fx;
    REQUIRE(fx.fb != nullptr);

    fx.fb->setSize(901, 600);
    CHECK_FALSE(fx.fb->isBrowserOverlayMode());
    CHECK(fx.fb->isBrowserShowing()); // docked

    fx.fb->setSize(899, 600);
    CHECK(fx.fb->isBrowserOverlayMode());
    CHECK_FALSE(fx.fb->isBrowserShowing()); // collapsed on crossing

    fx.fb->setSize(901, 600);
    CHECK_FALSE(fx.fb->isBrowserOverlayMode());
    CHECK(fx.fb->isBrowserShowing()); // docks back
}

TEST_CASE("layout stays sane at the 720x480 minimum", "[editor][breakpoint]")
{
    JuceEnv env;
    EditorFixture fx;
    REQUIRE(fx.fb != nullptr);

    fx.fb->setSize(t::layout::minWidth, t::layout::minHeight);
    walk(*fx.fb, [&fx](juce::Component& c) {
        if (!visibleWithin(c, *fx.fb))
            return;
        CHECK(c.getWidth() >= 0);
        CHECK(c.getHeight() >= 0);
        if (c.getParentComponent() == fx.fb) {
            CHECK(c.getRight() <= fx.fb->getWidth());
            CHECK(c.getBottom() <= fx.fb->getHeight());
        }
    });
}

TEST_CASE("editor size persists per instance across editor reopen",
          "[editor][breakpoint]")
{
    JuceEnv env;
    PluginProcessor processor;
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(processor.createEditor());
        ed->setSize(1024, 700);
    }
    std::unique_ptr<juce::AudioProcessorEditor> again(processor.createEditor());
    CHECK(again->getWidth() == 1024);
    CHECK(again->getHeight() == 700);
}

TEST_CASE("designated token pairs hold WCAG AA contrast", "[tokens][a11y]")
{
    // DESIGN.md-guaranteed pairs; a token change may not silently break AA.
    CHECK(contrastRatio(t::color::text, t::color::bg) >= 4.5);
    CHECK(contrastRatio(t::color::text, t::color::surface) >= 4.5);
    CHECK(contrastRatio(t::color::textDim, t::color::bg) >= 4.5);
    CHECK(contrastRatio(t::color::textDim, t::color::surface) >= 4.5);
    // NOTE: onAccent-on-primary measures ~2.6:1 — flagged to design in the
    // story completion notes; it is not one of DESIGN.md's guaranteed pairs.
}
