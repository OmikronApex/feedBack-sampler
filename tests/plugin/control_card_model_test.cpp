// Story 3.4: pure control-card decision logic.

#include "ui/control_card_model.h"

#include <catch2/catch_test_macros.hpp>

using fbsampler::ControlMapEntry;
using fbsampler::ui::buildControlCards;
using fbsampler::ui::ControlCardDescriptor;

namespace {

ControlMapEntry entry(const char* id, const char* name)
{
    ControlMapEntry e;
    e.id = id;
    e.displayName = name;
    e.accessibleName = name;
    return e;
}

} // namespace

TEST_CASE("N labeled controls produce N cc cards with labels", "[cards]")
{
    const auto model = buildControlCards(
        { entry("cc1", "Vibrato"), entry("cc74", "Brightness") }, false);
    REQUIRE(model.cards.size() == 2);
    CHECK(model.cards[0].kind == ControlCardDescriptor::Kind::cc);
    CHECK(model.cards[0].ccNumber == 1);
    CHECK(model.cards[0].label == "Vibrato");
    CHECK(model.cards[0].accessibleName == "Vibrato"); // AD-11 pipeline
    CHECK(model.cards[1].ccNumber == 74);
    CHECK_FALSE(model.showPresetRow);
}

TEST_CASE("empty control map yields the generic set", "[cards]")
{
    const auto model = buildControlCards({}, false);
    REQUIRE(model.cards.size() == 5);
    CHECK(model.cards[0].kind == ControlCardDescriptor::Kind::volume);
    CHECK(model.cards[1].kind == ControlCardDescriptor::Kind::pan);
    CHECK(model.cards[2].kind == ControlCardDescriptor::Kind::tuning);
    CHECK(model.cards[3].kind == ControlCardDescriptor::Kind::attack);
    CHECK(model.cards[4].kind == ControlCardDescriptor::Kind::release);
}

TEST_CASE("soundfont sets the preset-row flag; no control map -> generic set",
          "[cards]")
{
    const auto model = buildControlCards({}, true);
    CHECK(model.showPresetRow);
    CHECK(model.cards.size() == 5); // sf2 with no control map -> generic
}

TEST_CASE("malformed control ids are skipped, not bound", "[cards]")
{
    const auto model = buildControlCards(
        { entry("weird", "X"), entry("cc300", "Y"), entry("cc7", "Volume") },
        false);
    REQUIRE(model.cards.size() == 1);
    CHECK(model.cards[0].ccNumber == 7);
}
