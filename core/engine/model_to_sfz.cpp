#include "model_to_sfz.h"

#include "mod_curves.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace fbsampler::detail {

namespace {

void appendf(std::string& out, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    out += buf;
}

void warn(std::vector<Diagnostic>* diagnostics, std::string code, std::string message)
{
    if (!diagnostics)
        return;
    Diagnostic d;
    d.severity = Severity::Warning;
    d.code = std::move(code);
    d.message = std::move(message);
    diagnostics->push_back(std::move(d));
}

std::uint64_t toFrames(double value, SamplePositionUnit unit, double sampleRate)
{
    const double frames = unit == SamplePositionUnit::Frames
        ? value
        : value * sampleRate;
    return frames > 0.0 ? static_cast<std::uint64_t>(frames + 0.5) : 0;
}

std::string sfzPath(const std::string& path)
{
    std::string p = path;
    for (char& c : p)
        if (c == '\\')
            c = '/';
    return p;
}

bool isContainerRef(const std::string& path)
{
    return path.rfind("sf2://", 0) == 0;
}

bool isDefaultLinearShape(const ModSource& s)
{
    return s.curve == ModCurveType::Linear && !s.maxToMin && !s.bipolar;
}

// ---------------------------------------------------------------------------
// Embedded-sample emission: float32 WAV blob -> base64 -> <sample> header.
// sfizz loads base64data-provided files straight into its FilePool from RAM
// (Synth::handleSampleOpcodes -> FilePool::loadFromRam) — no temp files.
// ---------------------------------------------------------------------------

void appendU32le(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 24) & 0xFF);
}

void appendU16le(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
}

std::vector<std::uint8_t> buildFloat32Wav(const SamplePool& pool, SampleHandle handle,
                                          const SampleInfo& info)
{
    const auto numChannels = static_cast<std::uint16_t>(info.numChannels);
    const auto numFrames = static_cast<std::uint32_t>(info.residentFrames);
    const std::uint32_t dataBytes = numFrames * numChannels * 4u;

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + dataBytes);
    wav.insert(wav.end(), { 'R', 'I', 'F', 'F' });
    appendU32le(wav, 36 + dataBytes);
    wav.insert(wav.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    appendU32le(wav, 16);
    appendU16le(wav, 3); // IEEE float
    appendU16le(wav, numChannels);
    appendU32le(wav, static_cast<std::uint32_t>(info.sampleRate));
    appendU32le(wav, static_cast<std::uint32_t>(info.sampleRate) * numChannels * 4u);
    appendU16le(wav, static_cast<std::uint16_t>(numChannels * 4u));
    appendU16le(wav, 32);
    wav.insert(wav.end(), { 'd', 'a', 't', 'a' });
    appendU32le(wav, dataBytes);

    for (std::uint32_t frame = 0; frame < numFrames; ++frame) {
        for (std::uint16_t ch = 0; ch < numChannels; ++ch) {
            const float* channel = pool.residentChannel(handle, ch);
            const float v = channel ? channel[frame] : 0.0f;
            std::uint32_t bits = 0;
            std::memcpy(&bits, &v, 4);
            appendU32le(wav, bits);
        }
    }
    return wav;
}

std::string base64Encode(const std::vector<std::uint8_t>& data)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    const std::size_t rest = data.size() - i;
    if (rest == 1) {
        const std::uint32_t n = data[i] << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (rest == 2) {
        const std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Curve header registry: one <curve> per distinct source shape.
// ---------------------------------------------------------------------------

struct CurveRegistry {
    // key: (curve type, maxToMin, bipolar)
    std::map<std::tuple<int, bool, bool>, int> indices;
    std::string headers;

    int indexFor(const ModSource& source)
    {
        const auto key = std::make_tuple(static_cast<int>(source.curve), source.maxToMin,
                                         source.bipolar);
        const auto it = indices.find(key);
        if (it != indices.end())
            return it->second;
        // sfizz reserves curve indices 0-6 for its built-ins; ours start at 7.
        const int index = 7 + static_cast<int>(indices.size());
        indices.emplace(key, index);

        headers += "<curve> curve_index=" + std::to_string(index) + "\n";
        for (int i = 0; i < 128; ++i) {
            char buf[48];
            const float value = evalModSource(source, static_cast<float>(i) / 127.0f);
            std::snprintf(buf, sizeof(buf), "  v%03d=%.6f\n", i, static_cast<double>(value));
            headers += buf;
        }
        return index;
    }
};

} // namespace

std::string modelToSfzText(const InstrumentModel& model,
                           const std::vector<double>& regionSampleRates,
                           std::vector<Diagnostic>* diagnostics,
                           const SamplePool* pool,
                           const std::vector<SampleHandle>* regionHandles)
{
    std::string out;
    out += "// synthetic SFZ generated by fbsampler engine (extended-dialect bind seam)\n";

    // --- embedded container samples -> <sample> headers, emitted first.
    // The raw reference (with '#' and scheme) never appears in opcode text:
    // sanitized virtual names keep the parser away from directive characters.
    std::map<std::string, std::string> containerNames; // reference -> virtual name
    for (std::size_t i = 0; i < model.regions.size(); ++i) {
        const std::string& ref = model.regions[i].sampleFile;
        if (!isContainerRef(ref) || containerNames.count(ref))
            continue;
        const SampleHandle handle = (regionHandles && i < regionHandles->size())
            ? (*regionHandles)[i]
            : kInvalidSampleHandle;
        SampleInfo info;
        if (!pool || handle == kInvalidSampleHandle || !pool->info(handle, info)) {
            warn(diagnostics, "engine.embedded_sample_unbound",
                 "region " + std::to_string(i)
                     + ": container sample reference has no pooled data; region will not sound");
            continue;
        }
        const std::string name = "__fbsampler_smp_" + std::to_string(containerNames.size())
            + ".wav";
        containerNames.emplace(ref, name);
        out += "<sample> name=" + name + " base64data="
            + base64Encode(buildFloat32Wav(*pool, handle, info)) + "\n";
    }

    CurveRegistry curves;
    std::string regionsText;

    for (std::size_t i = 0; i < model.regions.size(); ++i) {
        const Region& r = model.regions[i];
        const double rate = i < regionSampleRates.size() ? regionSampleRates[i] : 0.0;
        const bool positionsUsable =
            r.positionUnit == SamplePositionUnit::Frames || rate > 0.0;

        if (!positionsUsable && diagnostics && (r.offset > 0.0 || r.loopEnabled)) {
            warn(diagnostics, "engine.position_unit_dropped",
                 "region " + std::to_string(i)
                     + ": sample rate unknown, offset/loop opcodes dropped (position unit is "
                       "Seconds)");
        }

        const auto containerIt = containerNames.find(r.sampleFile);
        if (isContainerRef(r.sampleFile) && containerIt == containerNames.end())
            continue; // unbound container sample, warned above
        const std::string sampleRef = containerIt != containerNames.end()
            ? containerIt->second
            : sfzPath(r.sampleFile);

        regionsText += "<region> sample=" + sampleRef + "\n";
        appendf(regionsText, "  lokey=%d hikey=%d lovel=%d hivel=%d pitch_keycenter=%d\n",
                r.loKey, r.hiKey, r.loVelocity, r.hiVelocity, r.rootKey);
        if (r.tuningCents != 0.0f)
            appendf(regionsText, "  tune=%d\n",
                    static_cast<int>(std::lround(r.tuningCents)));
        if (r.gainDb != 0.0f)
            appendf(regionsText, "  volume=%.6g\n", static_cast<double>(r.gainDb));
        if (r.pan != 0.0f)
            appendf(regionsText, "  pan=%.6g\n", static_cast<double>(r.pan) * 100.0);
        appendf(regionsText, "  bend_up=%d bend_down=%d\n",
                static_cast<int>(std::lround(r.bendUpCents)),
                static_cast<int>(std::lround(r.bendDownCents)));

        if (positionsUsable) {
            const double posRate = r.positionUnit == SamplePositionUnit::Seconds ? rate : 0.0;
            if (r.offset > 0.0)
                appendf(regionsText, "  offset=%llu\n",
                        static_cast<unsigned long long>(
                            toFrames(r.offset, r.positionUnit, posRate ? posRate : 1.0)));
            if (r.loopEnabled) {
                appendf(regionsText,
                        "  loop_mode=loop_continuous loop_start=%llu loop_end=%llu\n",
                        static_cast<unsigned long long>(
                            toFrames(r.loopStart, r.positionUnit, posRate ? posRate : 1.0)),
                        static_cast<unsigned long long>(
                            toFrames(r.loopEnd, r.positionUnit, posRate ? posRate : 1.0)));
            }
        }

        const EnvelopeADSR& env = r.amplitudeEnvelope;
        appendf(regionsText,
                "  ampeg_delay=%.6g ampeg_attack=%.6g ampeg_hold=%.6g"
                " ampeg_decay=%.6g ampeg_sustain=%.6g ampeg_release=%.6g\n",
                static_cast<double>(env.delaySeconds),
                static_cast<double>(env.attackSeconds),
                static_cast<double>(env.holdSeconds),
                static_cast<double>(env.decaySeconds),
                static_cast<double>(env.sustainLevel) * 100.0,
                static_cast<double>(env.releaseSeconds));

        // --- mod matrix. Velocity->Gain entries combine into one velocity
        // curve; Cc entries lower to *_oncc (+ <curve> when shaped); the
        // rest is dropped with a structured warning (AD-1).
        std::vector<const ModMatrixEntry*> velGain;
        bool velGainDropped = false;

        for (const ModMatrixEntry& mod : r.modMatrix) {
            if (mod.amountSource.kind != ModSourceKind::None) {
                warn(diagnostics, "engine.mod_amount_source_dropped",
                     "region " + std::to_string(i)
                         + ": modulator with a secondary amount source dropped (engine cannot "
                           "scale by a second controller yet)");
                continue;
            }
            if (mod.target == ModTarget::FilterCutoff || mod.target == ModTarget::ReverbSend
                || mod.target == ModTarget::ChorusSend) {
                warn(diagnostics, "engine.mod_target_unsupported",
                     "region " + std::to_string(i)
                         + ": modulator target not executable yet (filter/effect sends); "
                           "dropped");
                continue;
            }

            switch (mod.source.kind) {
            case ModSourceKind::Cc: {
                const int cc = mod.source.ccNumber;
                const bool shaped = !isDefaultLinearShape(mod.source);
                const int curveIndex = shaped ? curves.indexFor(mod.source) : -1;
                switch (mod.target) {
                case ModTarget::Gain:
                    appendf(regionsText, "  volume_oncc%d=%.6g\n", cc,
                            static_cast<double>(mod.depth));
                    if (shaped)
                        appendf(regionsText, "  volume_curvecc%d=%d\n", cc, curveIndex);
                    break;
                case ModTarget::Pitch:
                    appendf(regionsText, "  pitch_oncc%d=%d\n", cc,
                            static_cast<int>(std::lround(mod.depth)));
                    if (shaped)
                        appendf(regionsText, "  pitch_curvecc%d=%d\n", cc, curveIndex);
                    break;
                case ModTarget::Pan:
                    appendf(regionsText, "  pan_oncc%d=%.6g\n", cc,
                            static_cast<double>(mod.depth) * 100.0);
                    if (shaped)
                        appendf(regionsText, "  pan_curvecc%d=%d\n", cc, curveIndex);
                    break;
                default:
                    break;
                }
                break;
            }
            case ModSourceKind::Velocity:
                if (mod.target == ModTarget::Gain) {
                    velGain.push_back(&mod);
                } else if (mod.target == ModTarget::Pitch
                           && mod.source.curve == ModCurveType::Linear && !mod.source.bipolar) {
                    // pitch_veltrack: cents added at full velocity, linear.
                    const float depth = mod.source.maxToMin ? -mod.depth : mod.depth;
                    appendf(regionsText, "  pitch_veltrack=%d\n",
                            static_cast<int>(std::lround(depth)));
                } else {
                    velGainDropped = true;
                }
                break;
            case ModSourceKind::KeyTrack:
            case ModSourceKind::PolyPressure:
            case ModSourceKind::ChannelPressure:
            case ModSourceKind::PitchWheel:
            case ModSourceKind::PitchWheelSensitivity:
            case ModSourceKind::None:
                warn(diagnostics, "engine.mod_source_unsupported",
                     "region " + std::to_string(i)
                         + ": modulator source not executable by the engine yet; dropped");
                break;
            }
        }
        if (velGainDropped) {
            warn(diagnostics, "engine.mod_target_unsupported",
                 "region " + std::to_string(i)
                     + ": shaped velocity modulator target not executable; dropped");
        }

        if (!velGain.empty()) {
            // amp_velcurve_N pins the amplitude at each velocity; combined
            // multiplicatively across entries: amp(v) = prod 10^(depth_k *
            // shape_k(v/127) / 20). amp_veltrack=100 makes the points
            // authoritative.
            regionsText += "  amp_veltrack=100\n";
            for (int v = 1; v <= 127; ++v) {
                float gainDb = 0.0f;
                for (const ModMatrixEntry* mod : velGain)
                    gainDb += mod->depth * evalModSource(mod->source,
                                                      static_cast<float>(v) / 127.0f);
                const float amp = std::pow(10.0f, gainDb / 20.0f);
                appendf(regionsText, "  amp_velcurve_%d=%.6f\n", v,
                        static_cast<double>(std::clamp(amp, 0.0f, 1.0f)));
            }
        }
    }

    out += curves.headers;
    out += regionsText;
    return out;
}

} // namespace fbsampler::detail
