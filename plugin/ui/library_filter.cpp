#include "library_filter.h"

#include <algorithm>
#include <cctype>

namespace fbsampler::ui {

namespace {

std::string toLower(const std::string& s)
{
    std::string out(s.size(), '\0');
    std::transform(s.begin(), s.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

bool LibraryFilter::formatMatchesPills(LibraryFormat format,
                                       unsigned activePills)
{
    if (activePills == pillNone)
        return true;
    switch (format) {
    case LibraryFormat::sfz: return (activePills & pillSfz) != 0;
    case LibraryFormat::sf2:
    case LibraryFormat::sf3: return (activePills & pillSoundfont) != 0;
    }
    return false;
}

bool LibraryFilter::nameMatchesQuery(const std::string& displayName,
                                     const std::string& query)
{
    if (query.empty())
        return true;
    return toLower(displayName).find(toLower(query)) != std::string::npos;
}

std::vector<int> LibraryFilter::filter(const std::vector<LibraryEntry>& entries,
                                       const std::string& query,
                                       unsigned activePills)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const LibraryEntry& e = entries[static_cast<std::size_t>(i)];
        if (formatMatchesPills(e.format, activePills)
            && nameMatchesQuery(e.displayName, query))
            out.push_back(i);
    }
    return out;
}

} // namespace fbsampler::ui
