#include "corpus_render.h"

#include "../../core/pool/wav_reader.h"
#include "midi_file.h"
#include "wav_io.h"

#include "fbsampler/serialize.h"
#include "fbsampler/sf2_frontend.h"
#include "fbsampler/sfz_frontend.h"
#include "fbsampler/validate.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace fbsampler::testutil {

namespace {

std::string dirName(const std::string& path)
{
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? std::string(".") : path.substr(0, pos);
}

} // namespace

CorpusEntryResult runCorpusEntry(const std::string& instrumentPath,
                                 const std::string& format, int bank, int program,
                                 const std::string& midiPath,
                                 std::uint64_t totalFrames,
                                 const std::string& referenceWavPath,
                                 const CorpusThresholds& thresholds,
                                 const std::string& writeWavPath,
                                 const std::string& writeGoldenPath)
{
    CorpusEntryResult result;
    const std::string& sfzPath = instrumentPath;

    // Lower + validate (AC 1: "100% of corpus loads" is computable from
    // this). Entry -> (frontend by format) -> render -> diff: only the
    // frontend call is format-specific (Story 1.6 design, extended by 2.5).
    LowerResult lowered;
    if (format == "sf2" || format == "sf3") {
        lowered = lowerSf2Preset(instrumentPath, bank, program);
    } else if (format == "sfz" || format.empty()) {
        lowered = lowerSfzFile(instrumentPath);
    } else {
        result.error = "unknown corpus entry format: " + format;
        return result;
    }
    for (const Diagnostic& d : lowered.diagnostics)
        if (d.severity == Severity::Warning)
            ++result.warningCount;
    if (!lowered.model.has_value()) {
        result.error = "lowering produced no model";
        return result;
    }
    if (!validate(*lowered.model).empty()) {
        result.error = "lowered model failed validation";
        return result;
    }
    result.loaded = true;

    // Golden lowering snapshot (Story 1.6 Task 5): lets lowering drift and
    // rendering drift be attributed separately.
    if (!writeGoldenPath.empty()) {
        std::ofstream out(writeGoldenPath, std::ios::binary);
        if (!out.is_open()) {
            result.error = "cannot write golden snapshot: " + writeGoldenPath;
            return result;
        }
        out << serializeModel(*lowered.model);
    }

    RenderSettings settings;
    settings.sampleRate = 48000.0;
    settings.blockFrames = 256;
    settings.totalFrames = totalFrames;

    std::vector<TimelineEvent> timeline;
    std::string midiError;
    if (!loadMidiTimeline(midiPath, settings.sampleRate, &timeline, &midiError)) {
        result.error = "MIDI fixture: " + midiError;
        return result;
    }

    auto pool = std::shared_ptr<SamplePool>(createAllRamSamplePool());
    RenderResult render =
        renderOffline(*lowered.model, pool, dirName(sfzPath), timeline, settings);
    if (!render.ok || render.channels.size() != 2) {
        result.error = "offline render failed";
        return result;
    }
    result.renderedFrames = totalFrames;

    for (const auto& ch : render.channels)
        for (float v : ch)
            result.energy += static_cast<double>(v) * v;
    if (result.energy < thresholds.minEnergy) {
        result.error = "render is silent (energy below silence guard)";
        return result;
    }
    result.rendered = true;

    if (!writeWavPath.empty() && !writePcm16Wav(writeWavPath, render.channels, 48000.0)) {
        result.error = "cannot write render wav: " + writeWavPath;
        return result;
    }

    if (referenceWavPath.empty()) {
        result.passed = true; // load/render-only mode (reference capture)
        return result;
    }

    fbsampler::detail::DecodedWav reference;
    if (!fbsampler::detail::readWavFile(referenceWavPath, reference, nullptr)) {
        result.error = "cannot read reference: " + referenceWavPath;
        return result;
    }
    result.referenceFrames = reference.numFrames;
    if (reference.numChannels != 2 || reference.numFrames != totalFrames) {
        result.refCompared = true;
        result.error = "reference shape mismatch (duration/channel check failed)";
        return result;
    }

    double sumSquaredDiff = 0.0;
    const auto window = static_cast<std::size_t>(std::max(1, thresholds.windowFrames));
    for (std::uint32_t ch = 0; ch < 2; ++ch) {
        double windowSum = 0.0;
        std::size_t windowCount = 0;
        for (std::size_t i = 0; i < reference.channels[ch].size(); ++i) {
            const double diff = std::abs(static_cast<double>(render.channels[ch][i])
                                         - static_cast<double>(reference.channels[ch][i]));
            result.peakDiff = std::max(result.peakDiff, diff);
            sumSquaredDiff += diff * diff;
            windowSum += diff * diff;
            if (++windowCount == window || i + 1 == reference.channels[ch].size()) {
                result.worstWindowRmsDiff =
                    std::max(result.worstWindowRmsDiff,
                             std::sqrt(windowSum / static_cast<double>(windowCount)));
                windowSum = 0.0;
                windowCount = 0;
            }
        }
    }
    result.rmsDiff =
        std::sqrt(sumSquaredDiff / (2.0 * static_cast<double>(reference.numFrames)));
    result.refCompared = true;
    result.passed = result.peakDiff <= thresholds.peak && result.rmsDiff <= thresholds.rms
                    && result.worstWindowRmsDiff <= thresholds.windowRms;
    if (!result.passed)
        result.error = "diff exceeds NFR-5 thresholds";
    return result;
}

} // namespace fbsampler::testutil
