#pragma once

// Pure drop-decision logic (Story 3.6 AC3): what to do with a path dropped
// onto the editor. Unit-tested directly; the editor only executes actions.

#include "fbsampler/config_service.h"

#include <string>
#include <vector>

namespace fbsampler::ui {

struct DropAction {
    enum class Kind {
        addFolder,        // directory: add as library folder + scan
        registerAndLoad,  // recognized file outside configured folders (AD-4)
        rescanAndLoad,    // recognized file inside a configured folder
        rejectUnsupported // unrecognized extension: status text, no dialogs
    };
    Kind kind = Kind::rejectUnsupported;
    LibraryFormat format = LibraryFormat::sfz; // valid for the load kinds
};

/// `path` in native form; `isDirectory` from the filesystem;
/// `configuredFolders` from settings. A file counts as inside a folder when
/// that folder is a case-insensitive (Windows semantics handled by caller
/// passing normalized paths) prefix of the file path on a separator
/// boundary.
DropAction decideDropAction(const std::string& path, bool isDirectory,
                            const std::vector<std::string>& configuredFolders);

} // namespace fbsampler::ui
