// fbsampler-corpus-render: render one corpus entry and diff it against its
// reference capture (Story 1.6, AC 1-2). One entry per invocation; the
// orchestration (manifest parsing, asset fetching, per-library report
// aggregation) lives in corpus/tools/run_corpus.py. Exit 0 = entry passed.

#include "corpus_render.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace fbsampler::testutil;

namespace {

void jsonEscapeInto(std::string& out, const std::string& s)
{
    static const char* const kHex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20) {
            // Any other C0 control character: JSON requires these escaped
            // too, or a message containing one (e.g. echoed raw bytes from a
            // malformed SFZ file) produces invalid JSON that later chokes
            // run_corpus.py's json.load().
            out += "\\u00";
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
}

int usage()
{
    std::fprintf(stderr,
                 "usage: fbsampler-corpus-render --sfz FILE --midi FILE --frames N\n"
                 "         [--reference WAV] [--peak X] [--rms X] [--window-rms X]\n"
                 "         [--write-wav WAV] [--golden FILE] [--json FILE]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::string sfz, midi, reference, writeWav, golden, jsonPath;
    std::uint64_t frames = 0;
    CorpusThresholds thresholds;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (arg == "--sfz" && value)
            sfz = argv[++i];
        else if (arg == "--midi" && value)
            midi = argv[++i];
        else if (arg == "--frames" && value)
            frames = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--reference" && value)
            reference = argv[++i];
        else if (arg == "--peak" && value)
            thresholds.peak = std::strtod(argv[++i], nullptr);
        else if (arg == "--rms" && value)
            thresholds.rms = std::strtod(argv[++i], nullptr);
        else if (arg == "--window-rms" && value)
            thresholds.windowRms = std::strtod(argv[++i], nullptr);
        else if (arg == "--write-wav" && value)
            writeWav = argv[++i];
        else if (arg == "--golden" && value)
            golden = argv[++i];
        else if (arg == "--json" && value)
            jsonPath = argv[++i];
        else
            return usage();
    }
    if (sfz.empty() || midi.empty() || frames == 0)
        return usage();

    const CorpusEntryResult r =
        runCorpusEntry(sfz, midi, frames, reference, thresholds, writeWav, golden);

    std::string json = "{\n";
    auto boolField = [&](const char* name, bool v, bool comma = true) {
        json += std::string("  \"") + name + "\": " + (v ? "true" : "false")
                + (comma ? ",\n" : "\n");
    };
    auto numField = [&](const char* name, double v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "  \"%s\": %.9g,\n", name, v);
        json += buf;
    };
    boolField("loaded", r.loaded);
    boolField("rendered", r.rendered);
    boolField("ref_compared", r.refCompared);
    numField("energy", r.energy);
    numField("peak_diff", r.peakDiff);
    numField("rms_diff", r.rmsDiff);
    numField("worst_window_rms_diff", r.worstWindowRmsDiff);
    numField("rendered_frames", static_cast<double>(r.renderedFrames));
    numField("reference_frames", static_cast<double>(r.referenceFrames));
    numField("warning_count", r.warningCount);
    json += "  \"error\": \"";
    jsonEscapeInto(json, r.error);
    json += "\",\n";
    boolField("passed", r.passed, false);
    json += "}\n";

    if (!jsonPath.empty()) {
        std::FILE* f = std::fopen(jsonPath.c_str(), "wb");
        if (!f || std::fwrite(json.data(), 1, json.size(), f) != json.size()) {
            std::fprintf(stderr, "cannot write %s\n", jsonPath.c_str());
            if (f)
                std::fclose(f);
            return 2;
        }
        std::fclose(f);
    }
    std::fputs(json.c_str(), stdout);
    return r.passed ? 0 : 1;
}
