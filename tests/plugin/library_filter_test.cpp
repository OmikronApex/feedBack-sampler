// Story 3.3: pure filter logic for the library browser.

#include "ui/library_filter.h"

#include <catch2/catch_test_macros.hpp>

using fbsampler::LibraryEntry;
using fbsampler::LibraryFormat;
using fbsampler::ui::LibraryFilter;

namespace {

std::vector<LibraryEntry> fixtureEntries()
{
    auto make = [](const char* name, LibraryFormat f) {
        LibraryEntry e;
        e.displayName = name;
        e.format = f;
        return e;
    };
    return {
        make("Salamander Piano", LibraryFormat::sfz),
        make("GeneralUser GS", LibraryFormat::sf2),
        make("MuseScore Flute", LibraryFormat::sf3),
        make("Upright PIANO", LibraryFormat::sf2),
    };
}

} // namespace

TEST_CASE("empty query and no pills pass everything", "[filter]")
{
    const auto idx = LibraryFilter::filter(fixtureEntries(), "",
                                           LibraryFilter::pillNone);
    REQUIRE(idx == std::vector<int>{ 0, 1, 2, 3 });
}

TEST_CASE("query is case-insensitive substring on display name", "[filter]")
{
    const auto idx = LibraryFilter::filter(fixtureEntries(), "piano",
                                           LibraryFilter::pillNone);
    REQUIRE(idx == std::vector<int>{ 0, 3 });

    const auto upper = LibraryFilter::filter(fixtureEntries(), "GENERAL",
                                             LibraryFilter::pillNone);
    REQUIRE(upper == std::vector<int>{ 1 });

    const auto none = LibraryFilter::filter(fixtureEntries(), "zzz",
                                            LibraryFilter::pillNone);
    REQUIRE(none.empty());
}

TEST_CASE("format pills gate by format; soundfont pill covers sf2+sf3",
          "[filter]")
{
    const auto sfz = LibraryFilter::filter(fixtureEntries(), "",
                                           LibraryFilter::pillSfz);
    REQUIRE(sfz == std::vector<int>{ 0 });

    const auto sf = LibraryFilter::filter(fixtureEntries(), "",
                                          LibraryFilter::pillSoundfont);
    REQUIRE(sf == std::vector<int>{ 1, 2, 3 });

    // Multiple pills act as union.
    const auto both = LibraryFilter::filter(
        fixtureEntries(), "",
        LibraryFilter::pillSfz | LibraryFilter::pillSoundfont);
    REQUIRE(both == std::vector<int>{ 0, 1, 2, 3 });

    // DS pill alone matches nothing until Epic 4 adds the format.
    const auto ds = LibraryFilter::filter(fixtureEntries(), "",
                                          LibraryFilter::pillDs);
    REQUIRE(ds.empty());
}

TEST_CASE("query and pills combine with AND", "[filter]")
{
    const auto idx = LibraryFilter::filter(fixtureEntries(), "piano",
                                           LibraryFilter::pillSoundfont);
    REQUIRE(idx == std::vector<int>{ 3 });
}
