// Story 3.6 AC3: pure drop-decision logic — outside/inside/unrecognized.

#include "ui/drop_action.h"

#include <catch2/catch_test_macros.hpp>

using namespace fbsampler;
using fbsampler::ui::decideDropAction;
using fbsampler::ui::DropAction;

TEST_CASE("directory drop adds a library folder", "[dropaction]")
{
    const auto action = decideDropAction("C:/music/MyLib", true, {});
    CHECK(action.kind == DropAction::Kind::addFolder);
}

TEST_CASE("recognized file outside configured folders registers per AD-4",
          "[dropaction]")
{
    const std::vector<std::string> folders = { "C:/libraries" };

    SECTION("sfz")
    {
        const auto a = decideDropAction("C:/downloads/piano.sfz", false, folders);
        CHECK(a.kind == DropAction::Kind::registerAndLoad);
        CHECK(a.format == LibraryFormat::sfz);
    }
    SECTION("sf2, extension case-insensitive")
    {
        const auto a = decideDropAction("C:/downloads/GM.SF2", false, folders);
        CHECK(a.kind == DropAction::Kind::registerAndLoad);
        CHECK(a.format == LibraryFormat::sf2);
    }
    SECTION("sf3")
    {
        const auto a = decideDropAction("C:/downloads/muse.sf3", false, folders);
        CHECK(a.kind == DropAction::Kind::registerAndLoad);
        CHECK(a.format == LibraryFormat::sf3);
    }
    SECTION("no configured folders at all")
    {
        const auto a = decideDropAction("C:/downloads/piano.sfz", false, {});
        CHECK(a.kind == DropAction::Kind::registerAndLoad);
    }
}

TEST_CASE("recognized file inside a configured folder rescans, no new entry",
          "[dropaction]")
{
    const std::vector<std::string> folders = { "C:/libraries" };
    const auto a =
        decideDropAction("C:/libraries/vcsl/claves.sfz", false, folders);
    CHECK(a.kind == DropAction::Kind::rescanAndLoad);
    CHECK(a.format == LibraryFormat::sfz);
}

TEST_CASE("inside-check respects separator boundaries and slash style",
          "[dropaction]")
{
    // Prefix without a separator boundary is NOT inside.
    const auto sibling = decideDropAction("C:/librariesX/piano.sfz", false,
                                          { "C:/libraries" });
    CHECK(sibling.kind == DropAction::Kind::registerAndLoad);

    // Backslash paths normalize to the same folder.
    const auto back = decideDropAction("C:\\libraries\\piano.sfz", false,
                                       { "C:/libraries" });
    CHECK(back.kind == DropAction::Kind::rescanAndLoad);

#ifdef _WIN32
    // Windows: case-insensitive path comparison.
    const auto cased = decideDropAction("c:/LIBRARIES/piano.sfz", false,
                                        { "C:/libraries" });
    CHECK(cased.kind == DropAction::Kind::rescanAndLoad);
#endif
}

TEST_CASE("unrecognized extension is rejected without dialogs",
          "[dropaction]")
{
    CHECK(decideDropAction("C:/downloads/song.wav", false, {}).kind
          == DropAction::Kind::rejectUnsupported);
    CHECK(decideDropAction("C:/downloads/noext", false, {}).kind
          == DropAction::Kind::rejectUnsupported);
    CHECK(decideDropAction("C:/downloads/archive.zip", false,
                           { "C:/downloads" })
              .kind
          == DropAction::Kind::rejectUnsupported);
}
