#include "scanner.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace fbsampler::configdetail {

namespace {

std::string toLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

/// Recognize by extension. `.dspreset`/`.dslibrary` recognition lands with
/// Epic 4 — add cases here and extend LibraryFormat.
bool formatForExtension(const std::string& lowerExt, LibraryFormat& out)
{
    if (lowerExt == ".sfz") {
        out = LibraryFormat::sfz;
        return true;
    }
    if (lowerExt == ".sf2") {
        out = LibraryFormat::sf2;
        return true;
    }
    if (lowerExt == ".sf3") {
        out = LibraryFormat::sf3;
        return true;
    }
    return false;
}

std::int64_t nowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// file_clock ticks — not portable across platforms, but the index is a
/// local machine artifact and mtime is only compared against itself.
std::int64_t mtimeOf(const fs::path& p, std::error_code& ec)
{
    const auto t = fs::last_write_time(p, ec);
    if (ec)
        return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(
               t.time_since_epoch())
        .count();
}

/// Cheap INAM sniff for sf2/sf3 display names: parses only the RIFF INFO
/// LIST from the first 64 KiB — never lowers, never touches sample data.
/// Returns empty on any structural surprise (caller falls back to the stem).
std::string readSf2Name(const fs::path& p)
{
    std::FILE* f = nullptr;
#ifdef _WIN32
    f = _wfopen(p.wstring().c_str(), L"rb");
#else
    f = std::fopen(p.string().c_str(), "rb");
#endif
    if (f == nullptr)
        return {};
    unsigned char buf[65536];
    const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    if (n < 20 || std::memcmp(buf, "RIFF", 4) != 0
        || std::memcmp(buf + 8, "sfbk", 4) != 0)
        return {};

    auto readU32 = [&buf](std::size_t off) {
        return static_cast<std::uint32_t>(buf[off])
               | (static_cast<std::uint32_t>(buf[off + 1]) << 8)
               | (static_cast<std::uint32_t>(buf[off + 2]) << 16)
               | (static_cast<std::uint32_t>(buf[off + 3]) << 24);
    };

    std::size_t off = 12;
    while (off + 8 <= n) {
        const std::uint32_t chunkSize = readU32(off + 4);
        if (std::memcmp(buf + off, "LIST", 4) == 0 && off + 12 <= n
            && std::memcmp(buf + off + 8, "INFO", 4) == 0) {
            std::size_t sub = off + 12;
            const std::size_t listEnd =
                std::min<std::size_t>(n, off + 8 + chunkSize);
            while (sub + 8 <= listEnd) {
                const std::uint32_t subSize = readU32(sub + 4);
                if (std::memcmp(buf + sub, "INAM", 4) == 0) {
                    const std::size_t start = sub + 8;
                    const std::size_t end =
                        std::min<std::size_t>(listEnd, start + subSize);
                    if (start >= end)
                        return {};
                    std::string name(reinterpret_cast<const char*>(buf + start),
                                     end - start);
                    // Zero-terminated, possibly padded.
                    const auto z = name.find('\0');
                    if (z != std::string::npos)
                        name.resize(z);
                    return name;
                }
                sub += 8 + subSize + (subSize & 1u);
            }
            return {};
        }
        off += 8 + chunkSize + (chunkSize & 1u);
    }
    return {};
}

Diagnostic makeDiag(std::string code, std::string message, std::string file)
{
    Diagnostic d;
    d.severity = Severity::Warning;
    d.code = std::move(code);
    d.message = std::move(message);
    d.location.file = std::move(file);
    return d;
}

} // namespace

std::string canonicalKey(const std::string& path)
{
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(fs::u8path(path), ec);
    if (ec)
        canon = fs::u8path(path);
    std::string key = canon.u8string();
#ifdef _WIN32
    key = toLowerAscii(std::move(key));
#endif
    return key;
}

ScanResult scanFolders(const std::vector<std::string>& roots,
                       const std::vector<LibraryEntry>& previous,
                       const ConfigService::ProgressFn& progress)
{
    ScanResult result;

    std::unordered_map<std::string, const LibraryEntry*> known;
    for (const auto& e : previous)
        known.emplace(canonicalKey(e.path), &e);

    ScanProgress prog;
    const auto report = [&] {
        if (progress)
            progress(prog);
    };

    for (const auto& root : roots) {
        std::error_code ec;
        const fs::path rootPath = fs::u8path(root);
        if (!fs::exists(rootPath, ec) || ec) {
            result.diagnostics.push_back(makeDiag(
                "config.scan.folder_missing",
                "designated folder does not exist: " + root, root));
            continue;
        }

        fs::recursive_directory_iterator it(
            rootPath, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            result.diagnostics.push_back(
                makeDiag("config.scan.folder_unreadable",
                         "cannot enumerate folder: " + root + " ("
                             + ec.message() + ")",
                         root));
            continue;
        }

        const fs::recursive_directory_iterator end;
        while (it != end) {
            const fs::directory_entry& de = *it;
            std::error_code entryEc;

            LibraryFormat format{};
            const bool isFile = de.is_regular_file(entryEc);
            if (!entryEc && isFile
                && formatForExtension(
                    toLowerAscii(de.path().extension().u8string()), format)) {
                ++prog.examined;
                prog.currentPath = de.path().u8string();

                const std::string key = canonicalKey(de.path().u8string());
                std::error_code statEc;
                const std::uint64_t size = de.file_size(statEc);
                const std::int64_t mtime = mtimeOf(de.path(), statEc);

                const auto knownIt = known.find(key);
                if (knownIt != known.end() && !statEc
                    && knownIt->second->sizeBytes == size
                    && knownIt->second->mtime == mtime) {
                    // Unchanged: carry over without re-examination (FR10).
                    result.entries.push_back(*knownIt->second);
                } else {
                    LibraryEntry entry;
                    entry.path = de.path().u8string();
                    entry.rootFolder = root;
                    entry.format = format;
                    entry.sizeBytes = statEc ? 0 : size;
                    entry.mtime = mtime;
                    entry.scannedAt = nowUnixSeconds();
                    entry.displayName = de.path().stem().u8string();
                    if (statEc) {
                        entry.ok = false;
                        entry.statusDetail =
                            "cannot stat file: " + statEc.message();
                        result.diagnostics.push_back(makeDiag(
                            "config.scan.file_unreadable",
                            "cannot stat: " + entry.path, entry.path));
                    } else if (format == LibraryFormat::sf2
                               || format == LibraryFormat::sf3) {
                        const std::string inam = readSf2Name(de.path());
                        if (!inam.empty())
                            entry.displayName = inam;
                    }
                    result.entries.push_back(std::move(entry));
                }
                ++prog.recognized;
                report();
            }

            it.increment(entryEc);
            if (entryEc) {
                result.diagnostics.push_back(makeDiag(
                    "config.scan.folder_unreadable",
                    "enumeration error under " + root + ": "
                        + entryEc.message(),
                    root));
                break;
            }
        }
    }

    return result;
}

} // namespace fbsampler::configdetail
