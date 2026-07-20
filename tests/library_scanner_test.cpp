// Story 3.2: library scanner — recognition, incremental rescan, removal.

#include "fbsampler/config_service.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace fbsampler;

namespace {

struct TempTree {
    fs::path dir;

    TempTree()
    {
        std::random_device rd;
        dir = fs::temp_directory_path()
              / ("fbsampler-scanner-test-" + std::to_string(rd()));
        fs::create_directories(dir);
    }

    ~TempTree()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void writeText(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

/// Minimal valid-enough RIFF sfbk header with an INFO/INAM chunk.
void writeSf2WithName(const fs::path& p, const std::string& name)
{
    std::string inam = name;
    inam.push_back('\0');
    if (inam.size() % 2 != 0)
        inam.push_back('\0');

    std::string info = "INFO";
    info += "INAM";
    const std::uint32_t inamSize = static_cast<std::uint32_t>(inam.size());
    info.append(reinterpret_cast<const char*>(&inamSize), 4);
    info += inam;

    std::string list = "LIST";
    const std::uint32_t listSize = static_cast<std::uint32_t>(info.size());
    list.append(reinterpret_cast<const char*>(&listSize), 4);
    list += info;

    std::string riff = "RIFF";
    const std::uint32_t riffSize =
        static_cast<std::uint32_t>(4 + list.size()); // "sfbk" + LIST
    riff.append(reinterpret_cast<const char*>(&riffSize), 4);
    riff += "sfbk";
    riff += list;

    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << riff;
}

bool hasError(const std::vector<Diagnostic>& diags)
{
    for (const auto& d : diags)
        if (d.severity == Severity::Error)
            return true;
    return false;
}

void designateRoot(ConfigService& service, const fs::path& root)
{
    SamplerSettings s = service.settings();
    s.libraryFolders = { root.u8string() };
    REQUIRE_FALSE(hasError(service.saveSettings(s)));
}

const LibraryEntry* findByName(const LibraryIndex& idx, const std::string& n)
{
    for (const auto& e : idx.entries)
        if (e.displayName == n)
            return &e;
    return nullptr;
}

} // namespace

TEST_CASE("scan recognizes sfz/sf2/sf3 in nested folders and skips junk",
          "[scanner]")
{
    TempTree tmp;
    const fs::path root = tmp.dir / "libs";
    writeText(root / "a" / "piano.sfz", "<region> sample=p.wav\n");
    writeSf2WithName(root / "b" / "deep" / "gm.sf2", "General MIDI");
    writeSf2WithName(root / "c" / "flute.SF3", "Flute Font");
    writeText(root / "junk.txt", "not a library");
    writeText(root / "a" / "readme.md", "docs");

    ConfigService service((tmp.dir / "config").u8string());
    designateRoot(service, root);
    REQUIRE_FALSE(hasError(service.scan()));

    const LibraryIndex idx = service.index();
    REQUIRE(idx.entries.size() == 3);

    const LibraryEntry* sfz = findByName(idx, "piano");
    REQUIRE(sfz != nullptr);
    CHECK(sfz->format == LibraryFormat::sfz);
    CHECK(sfz->sizeBytes > 0);
    CHECK(sfz->ok);

    // sf2/sf3 display names come from the INAM chunk, not the stem.
    const LibraryEntry* sf2 = findByName(idx, "General MIDI");
    REQUIRE(sf2 != nullptr);
    CHECK(sf2->format == LibraryFormat::sf2);

    const LibraryEntry* sf3 = findByName(idx, "Flute Font");
    REQUIRE(sf3 != nullptr);
    CHECK(sf3->format == LibraryFormat::sf3); // extension case-insensitive
}

TEST_CASE("incremental rescan re-examines only changed files", "[scanner]")
{
    TempTree tmp;
    const fs::path root = tmp.dir / "libs";
    writeText(root / "one.sfz", "<region> sample=1.wav\n");
    writeText(root / "two.sfz", "<region> sample=2.wav\n");

    ConfigService service((tmp.dir / "config").u8string());
    designateRoot(service, root);
    REQUIRE_FALSE(hasError(service.scan()));
    const LibraryIndex first = service.index();
    REQUIRE(first.entries.size() == 2);
    const auto scannedAtOne = findByName(first, "one")->scannedAt;
    const auto scannedAtTwo = findByName(first, "two")->scannedAt;

    // Touch only "two" (content change moves size and/or mtime).
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeText(root / "two.sfz", "<region> sample=2.wav\n<region> sample=3.wav\n");

    REQUIRE_FALSE(hasError(service.scan()));
    const LibraryIndex second = service.index();
    REQUIRE(second.entries.size() == 2);

    // Unchanged entry carried over verbatim (same scannedAt == not
    // re-examined); changed entry re-stamped.
    CHECK(findByName(second, "one")->scannedAt == scannedAtOne);
    CHECK(findByName(second, "two")->scannedAt >= scannedAtTwo);
    CHECK(findByName(second, "two")->sizeBytes
          > findByName(first, "two")->sizeBytes);
}

TEST_CASE("deleted file's entry is removed on rescan", "[scanner]")
{
    TempTree tmp;
    const fs::path root = tmp.dir / "libs";
    writeText(root / "keep.sfz", "<region> sample=k.wav\n");
    writeText(root / "gone.sfz", "<region> sample=g.wav\n");

    ConfigService service((tmp.dir / "config").u8string());
    designateRoot(service, root);
    REQUIRE_FALSE(hasError(service.scan()));
    REQUIRE(service.index().entries.size() == 2);

    fs::remove(root / "gone.sfz");
    REQUIRE_FALSE(hasError(service.scan()));

    const LibraryIndex idx = service.index();
    REQUIRE(idx.entries.size() == 1);
    CHECK(idx.entries[0].displayName == "keep");
}

TEST_CASE("missing designated folder produces a diagnostic, not a throw",
          "[scanner]")
{
    TempTree tmp;
    ConfigService service((tmp.dir / "config").u8string());
    designateRoot(service, tmp.dir / "does-not-exist");
    const auto diags = service.scan();
    bool sawMissing = false;
    for (const auto& d : diags)
        if (d.code == "config.scan.folder_missing")
            sawMissing = true;
    CHECK(sawMissing);
    CHECK(service.index().entries.empty());
}

TEST_CASE("progress callback fires with counts during scan", "[scanner]")
{
    TempTree tmp;
    const fs::path root = tmp.dir / "libs";
    writeText(root / "one.sfz", "x");
    writeText(root / "two.sfz", "y");

    ConfigService service((tmp.dir / "config").u8string());
    designateRoot(service, root);
    std::size_t calls = 0;
    std::size_t lastRecognized = 0;
    REQUIRE_FALSE(hasError(service.scan([&](const ScanProgress& p) {
        ++calls;
        lastRecognized = p.recognized;
        CHECK_FALSE(p.currentPath.empty());
    })));
    CHECK(calls == 2);
    CHECK(lastRecognized == 2);
}
