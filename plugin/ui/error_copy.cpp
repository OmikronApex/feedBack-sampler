#include "error_copy.h"

namespace fbsampler::ui {

namespace {

struct CopyRule {
    const char* codePrefix; // matches Diagnostic::code by prefix
    const char* copy;       // problem + fix, UX-DR13 voice
};

// Ordered: first prefix match wins. Keep phrasing plain: problem, then next
// step. No jargon, no exclamation marks.
constexpr CopyRule kRules[] = {
    { "pool.file_missing",
      "Samples are missing — the library folder may have moved. "
      "Check the folder location or rescan." },
    { "pool.",
      "Some samples could not be read — check permissions or re-download "
      "the library." },
    { "engine.no_regions",
      "This library has no playable sounds — the file may be incomplete. "
      "Try re-downloading it." },
    { "engine.",
      "This library could not be prepared for playback — try reloading it." },
    { "sfz.file_unreadable",
      "Can't read this file — check permissions or re-download the "
      "library." },
    { "sfz.",
      "This SFZ file has entries the sampler can't use — the library may "
      "need an update. It was left unloaded." },
    { "sf2.file_unreadable",
      "Can't read this file — check permissions or re-download the "
      "library." },
    { "sf2.",
      "This SoundFont couldn't be opened — the file may be damaged. "
      "Try re-downloading it." },
    { "config.scan.folder_missing",
      "A library folder no longer exists — remove it in Settings or "
      "restore the folder." },
    { "config.",
      "Settings could not be saved — check that the configuration folder "
      "is writable." },
};

std::string fallbackCopy()
{
    return "This library could not be loaded — try rescanning your "
           "folders or re-downloading it.";
}

} // namespace

std::string userCopyForDiagnostic(const Diagnostic& diagnostic)
{
    for (const auto& rule : kRules)
        if (diagnostic.code.rfind(rule.codePrefix, 0) == 0)
            return rule.copy;
    return fallbackCopy();
}

std::string userCopyForFailure(const std::vector<Diagnostic>& diagnostics)
{
    for (const auto& d : diagnostics)
        if (d.severity == Severity::Error)
            return userCopyForDiagnostic(d);
    if (!diagnostics.empty())
        return userCopyForDiagnostic(diagnostics.front());
    return fallbackCopy();
}

std::string userCopyForStatusDetail(const std::string& statusDetail)
{
    // Stored shape: "code: message" (written by the load-failure path).
    Diagnostic d;
    const auto colon = statusDetail.find(':');
    d.code = colon == std::string::npos ? statusDetail
                                        : statusDetail.substr(0, colon);
    return userCopyForDiagnostic(d);
}

} // namespace fbsampler::ui
