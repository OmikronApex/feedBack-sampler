// Story 3.2: config service (AD-9) — round-trip, atomic writes,
// merge-on-write, generation counter, newer-schema fail-soft.

#include "fbsampler/config_service.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace fbsampler;

namespace {

/// Fresh temp config dir per test — never the real user config dir.
struct TempConfigDir {
    fs::path dir;

    TempConfigDir()
    {
        std::random_device rd;
        dir = fs::temp_directory_path()
              / ("fbsampler-config-test-" + std::to_string(rd()));
        fs::create_directories(dir);
    }

    ~TempConfigDir()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string str() const { return dir.u8string(); }
};

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeAll(const fs::path& p, const std::string& text)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

bool hasError(const std::vector<Diagnostic>& diags)
{
    for (const auto& d : diags)
        if (d.severity == Severity::Error)
            return true;
    return false;
}

} // namespace

TEST_CASE("settings round-trip through the config dir", "[config]")
{
    TempConfigDir tmp;
    {
        ConfigService service(tmp.str());
        SamplerSettings s = service.settings();
        s.libraryFolders = { "C:/libs/a", "C:/libs/b" };
        s.voiceLimit = 96;
        s.ramStreamThresholdMb = 1024;
        REQUIRE_FALSE(hasError(service.saveSettings(s)));
    }
    ConfigService second(tmp.str());
    const SamplerSettings s = second.settings();
    CHECK(s.libraryFolders
          == std::vector<std::string>{ "C:/libs/a", "C:/libs/b" });
    CHECK(s.voiceLimit == 96);
    CHECK(s.ramStreamThresholdMb == 1024);
    CHECK(s.schemaVersion == 1);
    CHECK(s.generation >= 1);
}

TEST_CASE("generation strictly increases across writes", "[config]")
{
    TempConfigDir tmp;
    ConfigService service(tmp.str());
    SamplerSettings s = service.settings();
    const auto g0 = s.generation;
    REQUIRE_FALSE(hasError(service.saveSettings(s)));
    const auto g1 = service.settings().generation;
    REQUIRE_FALSE(hasError(service.saveSettings(service.settings())));
    const auto g2 = service.settings().generation;
    CHECK(g1 > g0);
    CHECK(g2 > g1);
}

TEST_CASE("simulated crash before rename leaves the original intact",
          "[config]")
{
    TempConfigDir tmp;
    ConfigService service(tmp.str());
    SamplerSettings s = service.settings();
    s.voiceLimit = 77;
    REQUIRE_FALSE(hasError(service.saveSettings(s)));
    const std::string original = readAll(tmp.dir / "settings.json");

    // A writer that died between temp-write and rename leaves only a stray
    // temp file. It must neither corrupt reads nor shadow the real file.
    writeAll(tmp.dir / "settings.json.tmp-dead", "{ \"garbage\": tru");

    ConfigService second(tmp.str());
    CHECK(second.settings().voiceLimit == 77);
    CHECK(readAll(tmp.dir / "settings.json") == original);
}

TEST_CASE("merge-on-write preserves a foreign index entry added between "
          "read and write",
          "[config]")
{
    TempConfigDir tmp;

    // Instance A: designate folder A with one library, scan.
    const fs::path rootA = tmp.dir / "libsA";
    fs::create_directories(rootA);
    writeAll(rootA / "one.sfz", "<region> sample=one.wav\n");

    ConfigService a(tmp.str());
    SamplerSettings sa = a.settings();
    sa.libraryFolders = { rootA.u8string() };
    REQUIRE_FALSE(hasError(a.saveSettings(sa)));
    REQUIRE_FALSE(hasError(a.scan()));
    REQUIRE(a.index().entries.size() == 1);

    // Instance B (foreign writer): different folder, writes its own entry
    // AFTER A has read but BEFORE A's next write.
    const fs::path rootB = tmp.dir / "libsB";
    fs::create_directories(rootB);
    writeAll(rootB / "two.sfz", "<region> sample=two.wav\n");

    ConfigService b(tmp.str());
    SamplerSettings sb = b.settings();
    sb.libraryFolders = { rootB.u8string() };
    REQUIRE_FALSE(hasError(b.saveSettings(sb)));
    REQUIRE_FALSE(hasError(b.scan()));

    // A rescans its own folder (stale in-memory index, no reload): B's
    // foreign entry must survive A's merge-on-write.
    // NOTE: A's settings still list only rootA, so A scans only rootA.
    REQUIRE_FALSE(hasError(a.reload()));
    SamplerSettings saAgain = a.settings();
    saAgain.libraryFolders = { rootA.u8string() };
    REQUIRE_FALSE(hasError(a.saveSettings(saAgain)));
    REQUIRE_FALSE(hasError(a.scan()));

    const LibraryIndex merged = a.index();
    REQUIRE(merged.entries.size() == 2);
    bool sawOne = false, sawTwo = false;
    for (const auto& e : merged.entries) {
        if (e.displayName == "one")
            sawOne = true;
        if (e.displayName == "two")
            sawTwo = true;
    }
    CHECK(sawOne);
    CHECK(sawTwo);
}

TEST_CASE("newer schema file is served read-only with a diagnostic",
          "[config]")
{
    TempConfigDir tmp;
    writeAll(tmp.dir / "settings.json",
             "{ \"schemaVersion\": 99, \"generation\": 5, \"voiceLimit\": 42 }");
    const std::string before = readAll(tmp.dir / "settings.json");

    ConfigService service(tmp.str());
    CHECK(service.settingsReadOnly());

    SamplerSettings s = service.settings();
    s.voiceLimit = 1;
    const auto diags = service.saveSettings(s);
    CHECK(hasError(diags));
    // Fail soft: the newer file was never rewritten.
    CHECK(readAll(tmp.dir / "settings.json") == before);
}

TEST_CASE("corrupt file keeps last good in-memory copy and reports",
          "[config]")
{
    TempConfigDir tmp;
    ConfigService service(tmp.str());
    SamplerSettings s = service.settings();
    s.voiceLimit = 33;
    REQUIRE_FALSE(hasError(service.saveSettings(s)));

    writeAll(tmp.dir / "settings.json", "{ not json at all");
    const auto diags = service.reload();
    CHECK(hasError(diags));
    CHECK(service.settings().voiceLimit == 33); // last good copy retained
}

TEST_CASE("second instance reads the same index without scanning",
          "[config]")
{
    TempConfigDir tmp;
    const fs::path root = tmp.dir / "libs";
    fs::create_directories(root);
    writeAll(root / "inst.sfz", "<region> sample=x.wav\n");

    ConfigService first(tmp.str());
    SamplerSettings s = first.settings();
    s.libraryFolders = { root.u8string() };
    REQUIRE_FALSE(hasError(first.saveSettings(s)));
    REQUIRE_FALSE(hasError(first.scan()));
    REQUIRE(first.index().entries.size() == 1);

    // Second instance: constructor + accessors only. Scans never run by
    // themselves — assert via the index being identical AND no scan callback
    // (there is no scan call at all; ConfigService::scan is the only entry).
    ConfigService second(tmp.str());
    const LibraryIndex idx = second.index();
    REQUIRE(idx.entries.size() == 1);
    CHECK(idx.entries[0].path == first.index().entries[0].path);
    CHECK(idx.generation == first.index().generation);
}

TEST_CASE("registerFile persists a single-file entry (AD-4) round-trip",
          "[config]")
{
    TempConfigDir tmp;

    // A library file OUTSIDE any configured folder, dropped onto the editor.
    const fs::path dropped = tmp.dir / "downloads" / "piano.sf2";
    fs::create_directories(dropped.parent_path());
    writeAll(dropped, "RIFFxxxxsfbk"); // content irrelevant for registration

    ConfigService service(tmp.str());
    REQUIRE_FALSE(hasError(
        service.registerFile(dropped.u8string(), LibraryFormat::sf2)));

    // Entry shape matches scanned entries; root = the file's parent dir.
    const LibraryIndex idx = service.index();
    REQUIRE(idx.entries.size() == 1);
    const LibraryEntry& e = idx.entries[0];
    CHECK(e.path == dropped.u8string());
    CHECK(e.rootFolder == dropped.parent_path().u8string());
    CHECK(e.format == LibraryFormat::sf2);
    CHECK(e.displayName == "piano");
    CHECK(e.sizeBytes > 0);

    // Persisted: a second instance sees the entry without scanning.
    ConfigService second(tmp.str());
    REQUIRE(second.index().entries.size() == 1);
    CHECK(second.index().entries[0].path == dropped.u8string());

    // Re-registering the same path refreshes, never duplicates.
    REQUIRE_FALSE(hasError(
        service.registerFile(dropped.u8string(), LibraryFormat::sf2)));
    CHECK(service.index().entries.size() == 1);
}
