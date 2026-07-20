#include "drop_action.h"

#include <algorithm>
#include <cctype>

namespace fbsampler::ui {

namespace {

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string normalize(std::string s)
{
    std::replace(s.begin(), s.end(), '\\', '/');
#ifdef _WIN32
    s = toLower(std::move(s));
#endif
    return s;
}

bool extensionFormat(const std::string& path, LibraryFormat& out)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    const std::string ext = toLower(path.substr(dot));
    if (ext == ".sfz") {
        out = LibraryFormat::sfz;
        return true;
    }
    if (ext == ".sf2") {
        out = LibraryFormat::sf2;
        return true;
    }
    if (ext == ".sf3") {
        out = LibraryFormat::sf3;
        return true;
    }
    return false;
}

bool isUnderFolder(const std::string& normalizedFile,
                   const std::string& folder)
{
    std::string root = normalize(folder);
    if (root.empty())
        return false;
    if (root.back() != '/')
        root.push_back('/');
    return normalizedFile.rfind(root, 0) == 0;
}

} // namespace

DropAction decideDropAction(const std::string& path, bool isDirectory,
                            const std::vector<std::string>& configuredFolders)
{
    DropAction action;
    if (isDirectory) {
        action.kind = DropAction::Kind::addFolder;
        return action;
    }

    LibraryFormat format{};
    if (!extensionFormat(path, format)) {
        action.kind = DropAction::Kind::rejectUnsupported;
        return action;
    }
    action.format = format;

    const std::string normalized = normalize(path);
    for (const auto& folder : configuredFolders) {
        if (isUnderFolder(normalized, folder)) {
            action.kind = DropAction::Kind::rescanAndLoad;
            return action;
        }
    }
    action.kind = DropAction::Kind::registerAndLoad; // AD-4 single-file entry
    return action;
}

} // namespace fbsampler::ui
