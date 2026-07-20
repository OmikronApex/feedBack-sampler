#include "fbsampler/config_service.h"

#include "json.h"
#include "scanner.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;
namespace cj = fbsampler::configjson;

namespace fbsampler {

namespace {

constexpr int kSettingsSchema = 1;
constexpr int kIndexSchema = 1;
constexpr const char* kSettingsFile = "settings.json";
constexpr const char* kIndexFile = "library-index.json";

Diagnostic diag(Severity sev, std::string code, std::string message,
                std::string file = {})
{
    Diagnostic d;
    d.severity = sev;
    d.code = std::move(code);
    d.message = std::move(message);
    d.location.file = std::move(file);
    return d;
}

std::string platformConfigDir()
{
    if (const char* env = std::getenv("FBSAMPLER_CONFIG_DIR");
        env != nullptr && env[0] != '\0')
        return env;
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA");
        appdata != nullptr && appdata[0] != '\0')
        return (fs::u8path(appdata) / "feedBack Sampler").u8string();
    return (fs::temp_directory_path() / "feedBack Sampler").u8string();
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        return (fs::u8path(home) / "Library" / "Application Support"
                / "feedBack Sampler")
            .u8string();
    return "feedBack Sampler";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME");
        xdg != nullptr && xdg[0] != '\0')
        return (fs::u8path(xdg) / "feedback-sampler").u8string();
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        return (fs::u8path(home) / ".config" / "feedback-sampler").u8string();
    return "feedback-sampler";
#endif
}

bool readFileText(const fs::path& p, std::string& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

const char* formatToString(LibraryFormat f)
{
    switch (f) {
    case LibraryFormat::sf2: return "sf2";
    case LibraryFormat::sf3: return "sf3";
    case LibraryFormat::sfz:
    default: return "sfz";
    }
}

bool formatFromString(const std::string& s, LibraryFormat& out)
{
    if (s == "sfz") {
        out = LibraryFormat::sfz;
        return true;
    }
    if (s == "sf2") {
        out = LibraryFormat::sf2;
        return true;
    }
    if (s == "sf3") {
        out = LibraryFormat::sf3;
        return true;
    }
    return false;
}

cj::ValuePtr settingsToJson(const SamplerSettings& s)
{
    auto obj = cj::Value::makeObject();
    obj->members["schemaVersion"] = cj::Value::makeInt(s.schemaVersion);
    obj->members["generation"] = cj::Value::makeInt(s.generation);
    auto folders = cj::Value::makeArray();
    for (const auto& f : s.libraryFolders)
        folders->arrayItems.push_back(cj::Value::makeString(f));
    obj->members["libraryFolders"] = folders;
    obj->members["voiceLimit"] = cj::Value::makeInt(s.voiceLimit);
    obj->members["ramStreamThresholdMb"] =
        cj::Value::makeInt(s.ramStreamThresholdMb);
    return obj;
}

SamplerSettings settingsFromJson(const cj::Value& v)
{
    SamplerSettings s;
    s.schemaVersion = static_cast<int>(v.getInt("schemaVersion", 1));
    s.generation = v.getInt("generation", 0);
    if (const cj::Value* folders = v.find("libraryFolders");
        folders != nullptr && folders->isArray())
        for (const auto& item : folders->arrayItems)
            if (item->type == cj::Value::Type::string)
                s.libraryFolders.push_back(item->stringValue);
    s.voiceLimit = static_cast<int>(v.getInt("voiceLimit", s.voiceLimit));
    s.ramStreamThresholdMb = static_cast<int>(
        v.getInt("ramStreamThresholdMb", s.ramStreamThresholdMb));
    return s;
}

cj::ValuePtr indexToJson(const LibraryIndex& idx)
{
    auto obj = cj::Value::makeObject();
    obj->members["schemaVersion"] = cj::Value::makeInt(idx.schemaVersion);
    obj->members["generation"] = cj::Value::makeInt(idx.generation);
    auto entries = cj::Value::makeArray();
    for (const auto& e : idx.entries) {
        auto entry = cj::Value::makeObject();
        entry->members["path"] = cj::Value::makeString(e.path);
        entry->members["rootFolder"] = cj::Value::makeString(e.rootFolder);
        entry->members["format"] =
            cj::Value::makeString(formatToString(e.format));
        entry->members["displayName"] = cj::Value::makeString(e.displayName);
        entry->members["sizeBytes"] =
            cj::Value::makeInt(static_cast<std::int64_t>(e.sizeBytes));
        entry->members["mtime"] = cj::Value::makeInt(e.mtime);
        entry->members["scannedAt"] = cj::Value::makeInt(e.scannedAt);
        entry->members["status"] =
            cj::Value::makeString(e.ok ? "ok" : "failed");
        if (!e.ok)
            entry->members["statusDetail"] =
                cj::Value::makeString(e.statusDetail);
        entries->arrayItems.push_back(entry);
    }
    obj->members["entries"] = entries;
    return obj;
}

LibraryIndex indexFromJson(const cj::Value& v)
{
    LibraryIndex idx;
    idx.schemaVersion = static_cast<int>(v.getInt("schemaVersion", 1));
    idx.generation = v.getInt("generation", 0);
    if (const cj::Value* entries = v.find("entries");
        entries != nullptr && entries->isArray()) {
        for (const auto& item : entries->arrayItems) {
            if (!item->isObject())
                continue;
            LibraryEntry e;
            e.path = item->getString("path", {});
            if (e.path.empty())
                continue;
            e.rootFolder = item->getString("rootFolder", {});
            if (!formatFromString(item->getString("format", "sfz"), e.format))
                continue; // unknown format (newer binary wrote it) — skip
            e.displayName = item->getString("displayName", {});
            e.sizeBytes =
                static_cast<std::uint64_t>(item->getInt("sizeBytes", 0));
            e.mtime = item->getInt("mtime", 0);
            e.scannedAt = item->getInt("scannedAt", 0);
            e.ok = item->getString("status", "ok") == "ok";
            e.statusDetail = item->getString("statusDetail", {});
            idx.entries.push_back(std::move(e));
        }
    }
    return idx;
}

} // namespace

struct ConfigService::Impl {
    std::string baseDir;

    mutable std::mutex mutex;
    SamplerSettings settings;
    LibraryIndex index;
    bool settingsReadOnly = false;
    bool indexReadOnly = false;

    fs::path settingsPath() const { return fs::u8path(baseDir) / kSettingsFile; }
    fs::path indexPath() const { return fs::u8path(baseDir) / kIndexFile; }

    /// Reads one file. Missing file => defaults, no diagnostic (first run).
    /// Corrupt file => diagnostic, keep `current`. Newer schema => read-only
    /// + diagnostic, expose parsed-what-we-can content.
    template <typename T, typename FromJson>
    void readOne(const fs::path& path, int knownSchema, FromJson fromJson,
                 T& current, bool& readOnly, std::vector<Diagnostic>& diags)
    {
        readOnly = false;
        std::string text;
        if (!readFileText(path, text)) {
            current = T{};
            return; // first run
        }
        std::string error;
        const cj::ValuePtr parsed = cj::parse(text, &error);
        if (parsed == nullptr || !parsed->isObject()) {
            diags.push_back(diag(Severity::Error, "config.file_corrupt",
                                 "cannot parse " + path.u8string() + ": "
                                     + (error.empty() ? "not an object"
                                                      : error),
                                 path.u8string()));
            return; // keep last good copy
        }
        const int schema =
            static_cast<int>(parsed->getInt("schemaVersion", 1));
        if (schema > knownSchema) {
            // AD-9 fail-soft: never crash, never rewrite; serve read-only.
            readOnly = true;
            diags.push_back(diag(
                Severity::Warning, "config.schema_newer",
                path.u8string() + " has schemaVersion "
                    + std::to_string(schema) + " newer than supported "
                    + std::to_string(knownSchema)
                    + "; file is read-only for this instance",
                path.u8string()));
        }
        current = fromJson(*parsed);
    }

    /// The single write path (AD-9): serialize -> temp file in the same dir
    /// -> atomic rename over the target. Caller has already merged + bumped
    /// generation.
    std::vector<Diagnostic> atomicWrite(const fs::path& target,
                                        const std::string& payload)
    {
        std::vector<Diagnostic> diags;
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);

        const fs::path temp = target.parent_path()
            / (target.filename().u8string() + ".tmp-"
               + std::to_string(static_cast<unsigned long long>(
                   std::hash<std::string>{}(payload) & 0xffffffu)));
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                diags.push_back(diag(Severity::Error, "config.write_failed",
                                     "cannot create temp file "
                                         + temp.u8string(),
                                     temp.u8string()));
                return diags;
            }
            out << payload;
            out.flush();
            if (!out) {
                diags.push_back(diag(Severity::Error, "config.write_failed",
                                     "short write to " + temp.u8string(),
                                     temp.u8string()));
                return diags;
            }
        }
        // MSVC/POSIX fs::rename both replace an existing regular file
        // atomically (MoveFileEx MOVEFILE_REPLACE_EXISTING / rename(2)).
        fs::rename(temp, target, ec);
        if (ec) {
            fs::remove(temp, ec);
            diags.push_back(diag(Severity::Error, "config.write_failed",
                                 "atomic rename to " + target.u8string()
                                     + " failed",
                                 target.u8string()));
        }
        return diags;
    }
};

ConfigService::ConfigService(std::string baseDirOverride)
    : impl_(std::make_unique<Impl>())
{
    impl_->baseDir = baseDirOverride.empty() ? platformConfigDir()
                                             : std::move(baseDirOverride);
    reload();
}

ConfigService::~ConfigService() = default;

std::string ConfigService::configDir() const { return impl_->baseDir; }

SamplerSettings ConfigService::settings() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->settings;
}

LibraryIndex ConfigService::index() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->index;
}

bool ConfigService::settingsReadOnly() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->settingsReadOnly;
}

bool ConfigService::indexReadOnly() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->indexReadOnly;
}

std::vector<Diagnostic> ConfigService::reload()
{
    std::vector<Diagnostic> diags;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->readOne(impl_->settingsPath(), kSettingsSchema, settingsFromJson,
                   impl_->settings, impl_->settingsReadOnly, diags);
    impl_->readOne(impl_->indexPath(), kIndexSchema, indexFromJson,
                   impl_->index, impl_->indexReadOnly, diags);
    return diags;
}

std::vector<Diagnostic>
ConfigService::saveSettings(const SamplerSettings& newSettings)
{
    std::vector<Diagnostic> diags;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->settingsReadOnly) {
        diags.push_back(diag(Severity::Error, "config.read_only",
                             "settings.json has a newer schema; not writing",
                             impl_->settingsPath().u8string()));
        return diags;
    }

    // Re-read + merge (AD-9): last-writer-wins per setting — the provided
    // values win wholesale for known keys; the generation counter respects a
    // concurrent bump on disk.
    SamplerSettings disk;
    bool diskReadOnly = false;
    impl_->readOne(impl_->settingsPath(), kSettingsSchema, settingsFromJson,
                   disk, diskReadOnly, diags);
    if (diskReadOnly) {
        impl_->settingsReadOnly = true;
        diags.push_back(diag(Severity::Error, "config.read_only",
                             "settings.json changed to a newer schema on disk",
                             impl_->settingsPath().u8string()));
        return diags;
    }

    SamplerSettings merged = newSettings;
    merged.schemaVersion = kSettingsSchema;
    merged.generation =
        std::max(disk.generation, newSettings.generation) + 1;

    auto writeDiags = impl_->atomicWrite(impl_->settingsPath(),
                                         cj::serialize(*settingsToJson(merged)));
    const bool failed = std::any_of(
        writeDiags.begin(), writeDiags.end(),
        [](const Diagnostic& d) { return d.severity == Severity::Error; });
    diags.insert(diags.end(), writeDiags.begin(), writeDiags.end());
    if (!failed)
        impl_->settings = merged;
    return diags;
}

std::vector<Diagnostic> ConfigService::scan(const ProgressFn& progress)
{
    // Snapshot inputs without holding the lock during the (potentially long)
    // filesystem walk.
    SamplerSettings settingsCopy;
    LibraryIndex previous;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->indexReadOnly)
            return { diag(Severity::Error, "config.read_only",
                          "library-index.json has a newer schema; not scanning",
                          impl_->indexPath().u8string()) };
        settingsCopy = impl_->settings;
        previous = impl_->index;
    }

    configdetail::ScanResult scanned = configdetail::scanFolders(
        settingsCopy.libraryFolders, previous.entries, progress);
    std::vector<Diagnostic> diags = std::move(scanned.diagnostics);

    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Re-read + per-entry merge (AD-9). Scanned roots are authoritative:
    // disk entries under a scanned root that we no longer found are dropped
    // (deleted files leave the index; AD-4 handles missing libraries at
    // state-restore time). Foreign entries under OTHER roots are preserved;
    // on identity collision the newer scannedAt wins.
    LibraryIndex disk;
    bool diskReadOnly = false;
    impl_->readOne(impl_->indexPath(), kIndexSchema, indexFromJson, disk,
                   diskReadOnly, diags);
    if (diskReadOnly) {
        impl_->indexReadOnly = true;
        diags.push_back(diag(Severity::Error, "config.read_only",
                             "library-index.json changed to a newer schema",
                             impl_->indexPath().u8string()));
        return diags;
    }

    std::unordered_set<std::string> scannedRoots;
    for (const auto& root : settingsCopy.libraryFolders)
        scannedRoots.insert(configdetail::canonicalKey(root));

    LibraryIndex merged;
    merged.schemaVersion = kIndexSchema;
    merged.generation =
        std::max(disk.generation, previous.generation) + 1;

    std::unordered_map<std::string, LibraryEntry> byKey;
    for (const auto& e : disk.entries) {
        if (scannedRoots.count(configdetail::canonicalKey(e.rootFolder)) > 0)
            continue; // authoritative rescan of this root replaces it below
        byKey.emplace(configdetail::canonicalKey(e.path), e);
    }
    for (const auto& e : scanned.entries) {
        const std::string key = configdetail::canonicalKey(e.path);
        const auto it = byKey.find(key);
        if (it == byKey.end() || e.scannedAt >= it->second.scannedAt)
            byKey[key] = e;
    }

    merged.entries.reserve(byKey.size());
    for (auto& [key, entry] : byKey)
        merged.entries.push_back(std::move(entry));
    std::sort(merged.entries.begin(), merged.entries.end(),
              [](const LibraryEntry& a, const LibraryEntry& b) {
                  return a.path < b.path;
              });

    auto writeDiags = impl_->atomicWrite(impl_->indexPath(),
                                         cj::serialize(*indexToJson(merged)));
    const bool failed = std::any_of(
        writeDiags.begin(), writeDiags.end(),
        [](const Diagnostic& d) { return d.severity == Severity::Error; });
    diags.insert(diags.end(), writeDiags.begin(), writeDiags.end());
    if (!failed)
        impl_->index = merged;
    return diags;
}

std::vector<Diagnostic> ConfigService::registerFile(const std::string& path,
                                                    LibraryFormat format)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->indexReadOnly)
        return { diag(Severity::Error, "config.read_only",
                      "library-index.json has a newer schema; not writing",
                      impl_->indexPath().u8string()) };

    std::vector<Diagnostic> diags;
    const fs::path file = fs::u8path(path);
    std::error_code ec;
    LibraryEntry entry;
    entry.path = path;
    entry.rootFolder = file.parent_path().u8string(); // AD-4 single-file root
    entry.format = format;
    entry.displayName = file.stem().u8string();
    entry.sizeBytes = fs::file_size(file, ec);
    if (ec)
        entry.sizeBytes = 0;
    const auto mtime = fs::last_write_time(file, ec);
    entry.mtime = ec ? 0
                     : std::chrono::duration_cast<std::chrono::seconds>(
                           mtime.time_since_epoch())
                           .count();
    entry.scannedAt = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

    // Merge-on-write: re-read disk, replace/insert this one entry by
    // identity, bump generation, atomic rename.
    LibraryIndex disk;
    bool diskReadOnly = false;
    impl_->readOne(impl_->indexPath(), kIndexSchema, indexFromJson, disk,
                   diskReadOnly, diags);
    if (diskReadOnly) {
        impl_->indexReadOnly = true;
        return diags;
    }
    LibraryIndex merged = disk;
    merged.schemaVersion = kIndexSchema;
    merged.generation =
        std::max(disk.generation, impl_->index.generation) + 1;
    const std::string key = configdetail::canonicalKey(path);
    bool replaced = false;
    for (auto& e : merged.entries)
        if (configdetail::canonicalKey(e.path) == key) {
            e = entry;
            replaced = true;
        }
    if (!replaced)
        merged.entries.push_back(entry);

    auto writeDiags = impl_->atomicWrite(impl_->indexPath(),
                                         cj::serialize(*indexToJson(merged)));
    const bool failed = std::any_of(
        writeDiags.begin(), writeDiags.end(),
        [](const Diagnostic& d) { return d.severity == Severity::Error; });
    diags.insert(diags.end(), writeDiags.begin(), writeDiags.end());
    if (!failed)
        impl_->index = merged;
    return diags;
}

std::vector<Diagnostic> ConfigService::setEntryStatus(
    const std::string& path, bool ok, const std::string& statusDetail)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->indexReadOnly)
        return {};

    std::vector<Diagnostic> diags;
    LibraryIndex disk;
    bool diskReadOnly = false;
    impl_->readOne(impl_->indexPath(), kIndexSchema, indexFromJson, disk,
                   diskReadOnly, diags);
    if (diskReadOnly) {
        impl_->indexReadOnly = true;
        return diags;
    }
    const std::string key = configdetail::canonicalKey(path);
    bool changed = false;
    for (auto& e : disk.entries)
        if (configdetail::canonicalKey(e.path) == key) {
            e.ok = ok;
            e.statusDetail = ok ? std::string() : statusDetail;
            changed = true;
        }
    if (!changed)
        return diags; // unknown path: no-op by contract

    disk.generation = std::max(disk.generation, impl_->index.generation) + 1;
    auto writeDiags = impl_->atomicWrite(impl_->indexPath(),
                                         cj::serialize(*indexToJson(disk)));
    const bool failed = std::any_of(
        writeDiags.begin(), writeDiags.end(),
        [](const Diagnostic& d) { return d.severity == Severity::Error; });
    diags.insert(diags.end(), writeDiags.begin(), writeDiags.end());
    if (!failed)
        impl_->index = disk;
    return diags;
}

} // namespace fbsampler
