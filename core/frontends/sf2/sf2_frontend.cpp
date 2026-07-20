// SF2 frontend (Story 2.1): parses the SoundFont 2.04 RIFF structure (hydra)
// and lowers ONE preset's zones into the canonical model (AD-1, AD-11).
//
// Story 2.2 adds modulator lowering: the §8.4 default modulator set plus
// custom pmod/imod records lower into the model's SF2-shaped mod matrix
// (supersede/summing per §9.5.3); combinations the matrix cannot express are
// tracked as sf2.modulator_unsupported, never silent. Vorbis/SF3 is 2.3.
//
// AD-2 discipline: this file reads hydra + shdr *metadata* only. Sample bytes
// (sdta) are never decoded here — regions record container-sample URIs
// ("sf2://<path>#<sampleIndex>", SPEC.md#Sample-references) that the pool
// resolves at acquire() time.
//
// Spec: SoundFont 2.04 (synthfont.com/sfspec24.pdf). Generator semantics
// §8.1.2/8.1.3, zone algebra §8.5/§9.4, mandated structural validation §10.

#include "fbsampler/sf2_frontend.h"

#include "fbsampler/validate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fbsampler {

namespace {

// ---------------------------------------------------------------------------
// Diagnostics helpers
// ---------------------------------------------------------------------------

Diagnostic makeDiag(Severity severity, std::string code, std::string message,
                    const std::string& file)
{
    Diagnostic d;
    d.severity = severity;
    d.code = std::move(code);
    d.message = std::move(message);
    d.location.file = file;
    return d;
}

// ---------------------------------------------------------------------------
// Little-endian span reader
// ---------------------------------------------------------------------------

std::uint16_t readU16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::int16_t readS16(const std::uint8_t* p)
{
    return static_cast<std::int16_t>(readU16(p));
}

std::uint32_t readU32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// Fixed-size SF2 name field: NUL-terminated within 20 bytes, but corrupt
// files may fill all 20 — never read past the field.
std::string readName20(const std::uint8_t* p)
{
    std::size_t len = 0;
    while (len < 20 && p[len] != 0)
        ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

// ---------------------------------------------------------------------------
// Parsed hydra records
// ---------------------------------------------------------------------------

struct PresetRec {
    std::string name;
    std::uint16_t preset = 0;
    std::uint16_t bank = 0;
    std::uint16_t bagNdx = 0;
};

struct BagRec {
    std::uint16_t genNdx = 0;
    std::uint16_t modNdx = 0;
};

struct GenRec {
    std::uint16_t oper = 0;
    std::int16_t amount = 0; // also carries the two range bytes for range gens
};

/// Raw pmod/imod record (SoundFont 2.04 §7.4/§7.8).
struct ModRec {
    std::uint16_t srcOper = 0;    // SFModulator: source
    std::uint16_t destOper = 0;   // generator destination
    std::int16_t amount = 0;
    std::uint16_t amtSrcOper = 0; // SFModulator: amount source
    std::uint16_t transOper = 0;  // transform (0 = linear, 2 = absolute)
};

struct InstRec {
    std::string name;
    std::uint16_t bagNdx = 0;
};

struct SampleRec {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t startLoop = 0;
    std::uint32_t endLoop = 0;
    std::uint32_t sampleRate = 0;
    std::uint8_t originalPitch = 60;
    std::int8_t pitchCorrection = 0;
    std::uint16_t sampleLink = 0;
    std::uint16_t sampleType = 0;
};

struct Sf2File {
    std::vector<PresetRec> presets; // includes terminal EOP record
    std::vector<BagRec> pbags;
    std::vector<GenRec> pgens;
    std::vector<ModRec> pmods; // includes terminal record
    std::vector<InstRec> insts; // includes terminal EOI record
    std::vector<BagRec> ibags;
    std::vector<GenRec> igens;
    std::vector<ModRec> imods; // includes terminal record
    std::vector<SampleRec> samples; // includes terminal EOS record
    std::uint64_t smplSampleCount = 0; // 16-bit sample points in the sdta smpl chunk
    std::uint64_t smplByteCount = 0;   // raw smpl chunk bytes (SF3 byte-offset bounds)
};

// ---------------------------------------------------------------------------
// Generator IDs (SoundFont 2.04 §8.1.2)
// ---------------------------------------------------------------------------

enum Gen : int {
    kGenStartAddrsOffset = 0,
    kGenEndAddrsOffset = 1,
    kGenStartloopAddrsOffset = 2,
    kGenEndloopAddrsOffset = 3,
    kGenStartAddrsCoarseOffset = 4,
    kGenEndAddrsCoarseOffset = 12,
    kGenPan = 17,
    kGenDelayVolEnv = 33,
    kGenAttackVolEnv = 34,
    kGenHoldVolEnv = 35,
    kGenDecayVolEnv = 36,
    kGenSustainVolEnv = 37,
    kGenReleaseVolEnv = 38,
    kGenKeynumToVolEnvHold = 39,
    kGenKeynumToVolEnvDecay = 40,
    kGenInstrument = 41,
    kGenKeyRange = 43,
    kGenVelRange = 44,
    kGenStartloopAddrsCoarseOffset = 45,
    kGenKeynum = 46,
    kGenVelocity = 47,
    kGenInitialAttenuation = 48,
    kGenEndloopAddrsCoarseOffset = 50,
    kGenCoarseTune = 51,
    kGenFineTune = 52,
    kGenSampleID = 53,
    kGenSampleModes = 54,
    kGenScaleTuning = 56,
    kGenExclusiveClass = 57,
    kGenOverridingRootKey = 58,
    kGenCount = 61,
};

const char* genName(int oper)
{
    switch (oper) {
        case 0: return "startAddrsOffset";
        case 1: return "endAddrsOffset";
        case 2: return "startloopAddrsOffset";
        case 3: return "endloopAddrsOffset";
        case 4: return "startAddrsCoarseOffset";
        case 5: return "modLfoToPitch";
        case 6: return "vibLfoToPitch";
        case 7: return "modEnvToPitch";
        case 8: return "initialFilterFc";
        case 9: return "initialFilterQ";
        case 10: return "modLfoToFilterFc";
        case 11: return "modEnvToFilterFc";
        case 12: return "endAddrsCoarseOffset";
        case 13: return "modLfoToVolume";
        case 15: return "chorusEffectsSend";
        case 16: return "reverbEffectsSend";
        case 17: return "pan";
        case 21: return "delayModLFO";
        case 22: return "freqModLFO";
        case 23: return "delayVibLFO";
        case 24: return "freqVibLFO";
        case 25: return "delayModEnv";
        case 26: return "attackModEnv";
        case 27: return "holdModEnv";
        case 28: return "decayModEnv";
        case 29: return "sustainModEnv";
        case 30: return "releaseModEnv";
        case 31: return "keynumToModEnvHold";
        case 32: return "keynumToModEnvDecay";
        case 33: return "delayVolEnv";
        case 34: return "attackVolEnv";
        case 35: return "holdVolEnv";
        case 36: return "decayVolEnv";
        case 37: return "sustainVolEnv";
        case 38: return "releaseVolEnv";
        case 39: return "keynumToVolEnvHold";
        case 40: return "keynumToVolEnvDecay";
        case 41: return "instrument";
        case 43: return "keyRange";
        case 44: return "velRange";
        case 45: return "startloopAddrsCoarseOffset";
        case 46: return "keynum";
        case 47: return "velocity";
        case 48: return "initialAttenuation";
        case 50: return "endloopAddrsCoarseOffset";
        case 51: return "coarseTune";
        case 52: return "fineTune";
        case 53: return "sampleID";
        case 54: return "sampleModes";
        case 56: return "scaleTuning";
        case 57: return "exclusiveClass";
        case 58: return "overridingRootKey";
        default: return "unknown";
    }
}

// Generators the v0 model cannot express: lowered as per-generator warnings
// (AD-1: tracked fidelity gap, never silent); the region still lowers.
bool isUnsupportedGen(int oper)
{
    switch (oper) {
        case 5: case 6: case 7:      // LFO/env -> pitch
        case 8: case 9:              // filter Fc/Q
        case 10: case 11:            // LFO/env -> filter
        case 13:                     // modLfoToVolume
        case 15: case 16:            // chorus/reverb sends
        case 21: case 22: case 23: case 24: // LFO params
        case 25: case 26: case 27: case 28: case 29: case 30: // mod envelope
        case 31: case 32:            // keynum -> mod env
        case kGenKeynumToVolEnvHold:
        case kGenKeynumToVolEnvDecay:
        case kGenEndAddrsOffset:
        case kGenEndAddrsCoarseOffset:
        case kGenKeynum:
        case kGenVelocity:
        case kGenExclusiveClass:
            return true;
        default:
            return false;
    }
}

// Generators that are only meaningful at the instrument level; a preset zone
// carrying one is spec-invalid and ignored (SoundFont 2.04 §8.5).
bool isInstrumentOnlyGen(int oper)
{
    switch (oper) {
        case kGenStartAddrsOffset:
        case kGenEndAddrsOffset:
        case kGenStartloopAddrsOffset:
        case kGenEndloopAddrsOffset:
        case kGenStartAddrsCoarseOffset:
        case kGenEndAddrsCoarseOffset:
        case kGenStartloopAddrsCoarseOffset:
        case kGenEndloopAddrsCoarseOffset:
        case kGenKeynum:
        case kGenVelocity:
        case kGenSampleModes:
        case kGenExclusiveClass:
        case kGenOverridingRootKey:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// RIFF parsing + SoundFont 2.04 §10 mandated structural validation.
// Every violation is a Diagnostic with a stable sf2.* code — never a throw
// across the core API. Chunk offsets are reported in the message text.
// ---------------------------------------------------------------------------

struct ChunkView {
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    std::size_t fileOffset = 0;
    bool present = false;
};

std::string atOffset(std::size_t offset)
{
    return " (chunk at byte offset " + std::to_string(offset) + ")";
}

bool parseSf2(const std::uint8_t* data, std::size_t size, const std::string& file,
              Sf2File& out, std::vector<Diagnostic>& diags)
{
    auto err = [&](std::string code, std::string message) {
        diags.push_back(makeDiag(Severity::Error, std::move(code), std::move(message), file));
        return false;
    };

    if (data == nullptr || size < 12 || std::memcmp(data, "RIFF", 4) != 0
        || std::memcmp(data + 8, "sfbk", 4) != 0) {
        return err("sf2.not_soundfont", "not a RIFF sfbk (SoundFont) file");
    }
    const std::uint32_t riffSize = readU32(data + 4);
    // The RIFF payload may not exceed the actual bytes; a shorter declared
    // size just limits the walk (truncated files fail on missing chunks).
    const std::size_t limit = std::min<std::size_t>(size, 8 + static_cast<std::size_t>(riffSize));

    ChunkView infoList, sdtaList, pdtaList;

    std::size_t pos = 12;
    while (pos + 8 <= limit) {
        const std::uint8_t* hdr = data + pos;
        const std::uint32_t chunkSize = readU32(hdr + 4);
        if (chunkSize > limit - pos - 8) {
            return err("sf2.chunk_truncated",
                       "chunk declares " + std::to_string(chunkSize)
                           + " bytes but only " + std::to_string(limit - pos - 8)
                           + " remain" + atOffset(pos));
        }
        if (std::memcmp(hdr, "LIST", 4) == 0 && chunkSize >= 4) {
            const std::uint8_t* body = hdr + 8;
            ChunkView view { body + 4, chunkSize - 4, pos + 12, true };
            if (std::memcmp(body, "INFO", 4) == 0) infoList = view;
            else if (std::memcmp(body, "sdta", 4) == 0) sdtaList = view;
            else if (std::memcmp(body, "pdta", 4) == 0) pdtaList = view;
        }
        pos += 8 + chunkSize + (chunkSize & 1); // chunks are word-aligned
    }

    if (!pdtaList.present)
        return err("sf2.chunk_missing", "LIST pdta chunk (hydra) not found");
    if (!sdtaList.present)
        return err("sf2.chunk_missing", "LIST sdta chunk (sample data) not found");
    // INFO is mandatory per spec §5 but carries no lowering-relevant data;
    // its absence must not block a structurally-lowerable file.
    if (!infoList.present) {
        diags.push_back(makeDiag(Severity::Warning, "sf2.chunk_missing",
                                 "LIST INFO chunk not found; continuing", file));
    }

    // Locate the smpl chunk inside sdta (16-bit PCM). sm24 is ignored in v0
    // (24-bit extension; the pool decodes 16-bit only).
    std::uint32_t smplBytes = 0;
    {
        std::size_t p = 0;
        while (p + 8 <= sdtaList.size) {
            const std::uint8_t* hdr = sdtaList.data + p;
            const std::uint32_t chunkSize = readU32(hdr + 4);
            if (chunkSize > sdtaList.size - p - 8)
                break;
            if (std::memcmp(hdr, "smpl", 4) == 0)
                smplBytes = chunkSize;
            p += 8 + chunkSize + (chunkSize & 1);
        }
    }
    out.smplSampleCount = smplBytes / 2;
    out.smplByteCount = smplBytes;

    // Hydra sub-chunks inside pdta.
    struct SubChunk {
        const char* id;
        std::uint32_t recordSize;
        ChunkView view;
    };
    SubChunk subs[] = {
        { "phdr", 38, {} }, { "pbag", 4, {} }, { "pmod", 10, {} }, { "pgen", 4, {} },
        { "inst", 22, {} }, { "ibag", 4, {} }, { "imod", 10, {} }, { "igen", 4, {} },
        { "shdr", 46, {} },
    };
    {
        std::size_t p = 0;
        while (p + 8 <= pdtaList.size) {
            const std::uint8_t* hdr = pdtaList.data + p;
            const std::uint32_t chunkSize = readU32(hdr + 4);
            if (chunkSize > pdtaList.size - p - 8) {
                return err("sf2.chunk_truncated",
                           "pdta sub-chunk declares " + std::to_string(chunkSize)
                               + " bytes beyond the pdta LIST"
                               + atOffset(pdtaList.fileOffset + p));
            }
            for (auto& sub : subs) {
                if (std::memcmp(hdr, sub.id, 4) == 0)
                    sub.view = ChunkView { hdr + 8, chunkSize, pdtaList.fileOffset + p + 8, true };
            }
            p += 8 + chunkSize + (chunkSize & 1);
        }
    }

    for (const auto& sub : subs) {
        if (!sub.view.present)
            return err("sf2.hydra_chunk_missing",
                       std::string("hydra sub-chunk '") + sub.id + "' not found");
        // §10: each hydra chunk size must be a multiple of its record size.
        if (sub.view.size % sub.recordSize != 0)
            return err("sf2.chunk_size_invalid",
                       std::string("hydra sub-chunk '") + sub.id + "' size "
                           + std::to_string(sub.view.size) + " is not a multiple of "
                           + std::to_string(sub.recordSize) + atOffset(sub.view.fileOffset));
        // §10: the terminal record (EOP/EOI/EOS/terminal bag/gen/mod) must
        // exist, so every chunk holds at least one record.
        if (sub.view.size / sub.recordSize < 1)
            return err("sf2.terminal_record_missing",
                       std::string("hydra sub-chunk '") + sub.id
                           + "' is empty (terminal record missing)"
                           + atOffset(sub.view.fileOffset));
    }

    const ChunkView& phdr = subs[0].view;
    const ChunkView& pbag = subs[1].view;
    const ChunkView& pmod = subs[2].view;
    const ChunkView& pgen = subs[3].view;
    const ChunkView& inst = subs[4].view;
    const ChunkView& ibag = subs[5].view;
    const ChunkView& imod = subs[6].view;
    const ChunkView& igen = subs[7].view;
    const ChunkView& shdr = subs[8].view;

    const std::size_t presetCount = phdr.size / 38;
    out.presets.reserve(presetCount);
    for (std::size_t i = 0; i < presetCount; ++i) {
        const std::uint8_t* p = phdr.data + i * 38;
        PresetRec rec;
        rec.name = readName20(p);
        rec.preset = readU16(p + 20);
        rec.bank = readU16(p + 22);
        rec.bagNdx = readU16(p + 24);
        out.presets.push_back(std::move(rec));
    }

    const std::size_t pbagCount = pbag.size / 4;
    out.pbags.reserve(pbagCount);
    for (std::size_t i = 0; i < pbagCount; ++i) {
        const std::uint8_t* p = pbag.data + i * 4;
        out.pbags.push_back({ readU16(p), readU16(p + 2) });
    }

    const std::size_t pgenCount = pgen.size / 4;
    out.pgens.reserve(pgenCount);
    for (std::size_t i = 0; i < pgenCount; ++i) {
        const std::uint8_t* p = pgen.data + i * 4;
        out.pgens.push_back({ readU16(p), readS16(p + 2) });
    }
    const std::size_t pmodCount = pmod.size / 10;
    out.pmods.reserve(pmodCount);
    for (std::size_t i = 0; i < pmodCount; ++i) {
        const std::uint8_t* p = pmod.data + i * 10;
        out.pmods.push_back({ readU16(p), readU16(p + 2), readS16(p + 4), readU16(p + 6),
                              readU16(p + 8) });
    }

    const std::size_t instCount = inst.size / 22;
    out.insts.reserve(instCount);
    for (std::size_t i = 0; i < instCount; ++i) {
        const std::uint8_t* p = inst.data + i * 22;
        InstRec rec;
        rec.name = readName20(p);
        rec.bagNdx = readU16(p + 20);
        out.insts.push_back(std::move(rec));
    }

    const std::size_t ibagCount = ibag.size / 4;
    out.ibags.reserve(ibagCount);
    for (std::size_t i = 0; i < ibagCount; ++i) {
        const std::uint8_t* p = ibag.data + i * 4;
        out.ibags.push_back({ readU16(p), readU16(p + 2) });
    }

    const std::size_t igenCount = igen.size / 4;
    out.igens.reserve(igenCount);
    for (std::size_t i = 0; i < igenCount; ++i) {
        const std::uint8_t* p = igen.data + i * 4;
        out.igens.push_back({ readU16(p), readS16(p + 2) });
    }
    const std::size_t imodCount = imod.size / 10;
    out.imods.reserve(imodCount);
    for (std::size_t i = 0; i < imodCount; ++i) {
        const std::uint8_t* p = imod.data + i * 10;
        out.imods.push_back({ readU16(p), readU16(p + 2), readS16(p + 4), readU16(p + 6),
                              readU16(p + 8) });
    }

    const std::size_t sampleCount = shdr.size / 46;
    out.samples.reserve(sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const std::uint8_t* p = shdr.data + i * 46;
        SampleRec rec;
        rec.name = readName20(p);
        rec.start = readU32(p + 20);
        rec.end = readU32(p + 24);
        rec.startLoop = readU32(p + 28);
        rec.endLoop = readU32(p + 32);
        rec.sampleRate = readU32(p + 36);
        rec.originalPitch = p[40];
        rec.pitchCorrection = static_cast<std::int8_t>(p[41]);
        rec.sampleLink = readU16(p + 42);
        rec.sampleType = readU16(p + 44);
        out.samples.push_back(std::move(rec));
    }

    // §10: bag-index monotonicity across phdr/inst, and gen/mod-index
    // monotonicity across the bags, with the terminal record's indices in
    // bounds. A violation makes zone extents undecidable — structural error.
    auto checkMonotonic = [&](const char* what, std::size_t count,
                              auto&& indexAt, std::size_t boundExclusive) {
        std::uint32_t prev = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t v = indexAt(i);
            if (i > 0 && v < prev) {
                err("sf2.index_not_monotonic",
                    std::string(what) + " indices decrease at record " + std::to_string(i));
                return false;
            }
            if (v >= boundExclusive) {
                err("sf2.index_out_of_range",
                    std::string(what) + " index " + std::to_string(v) + " at record "
                        + std::to_string(i) + " exceeds available records");
                return false;
            }
            prev = v;
        }
        return true;
    };

    if (!checkMonotonic("phdr preset-bag", out.presets.size(),
                        [&](std::size_t i) { return out.presets[i].bagNdx; }, out.pbags.size()))
        return false;
    if (!checkMonotonic("inst instrument-bag", out.insts.size(),
                        [&](std::size_t i) { return out.insts[i].bagNdx; }, out.ibags.size()))
        return false;
    if (!checkMonotonic("pbag generator", out.pbags.size(),
                        [&](std::size_t i) { return out.pbags[i].genNdx; }, out.pgens.size()))
        return false;
    if (!checkMonotonic("pbag modulator", out.pbags.size(),
                        [&](std::size_t i) { return out.pbags[i].modNdx; }, out.pmods.size()))
        return false;
    if (!checkMonotonic("ibag generator", out.ibags.size(),
                        [&](std::size_t i) { return out.ibags[i].genNdx; }, out.igens.size()))
        return false;
    if (!checkMonotonic("ibag modulator", out.ibags.size(),
                        [&](std::size_t i) { return out.ibags[i].modNdx; }, out.imods.size()))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Zone algebra + generator lowering (§8.5 / §9.4)
// ---------------------------------------------------------------------------

struct GenSet {
    std::array<std::int16_t, kGenCount> value {};
    std::array<bool, kGenCount> set {};

    void apply(int oper, std::int16_t amount)
    {
        if (oper >= 0 && oper < kGenCount) {
            value[static_cast<std::size_t>(oper)] = amount;
            set[static_cast<std::size_t>(oper)] = true;
        }
    }

    bool has(int oper) const { return set[static_cast<std::size_t>(oper)]; }
    std::int16_t get(int oper, std::int16_t fallback) const
    {
        return has(oper) ? value[static_cast<std::size_t>(oper)] : fallback;
    }
};

struct KeyVelRange {
    int lo = 0;
    int hi = 127;
};

KeyVelRange rangeFromAmount(std::int16_t amount)
{
    // Range generators pack lo in the low byte, hi in the high byte.
    const auto u = static_cast<std::uint16_t>(amount);
    KeyVelRange r;
    r.lo = u & 0xFF;
    r.hi = (u >> 8) & 0xFF;
    return r;
}

// timecents -> seconds: 2^(tc/1200); the -12000 sentinel (and anything below)
// clamps to 0 (SoundFont 2.04 §8.1.2). Upper clamp keeps absurd corrupt
// values finite (spec maxima are <= 8000 tc ~ 101.6 s).
float timecentsToSeconds(std::int16_t tc)
{
    if (tc <= -12000)
        return 0.0f;
    const float clamped = std::min<float>(static_cast<float>(tc), 8000.0f);
    return std::pow(2.0f, clamped / 1200.0f);
}

struct LowerContext {
    const Sf2File& sf;
    const std::string& file;
    std::string containerRef; // path used in sf2:// URIs
    std::vector<Diagnostic>& diags;
    std::vector<std::string> warnedModIdentities; // one unsupported-mod warning per identity

    void warn(std::string code, std::string message)
    {
        diags.push_back(makeDiag(Severity::Warning, std::move(code), std::move(message), file));
    }
};

// Effective generator value: instrument-zone generators are absolute values;
// preset-zone generators are additive offsets (§9.4). Range generators and
// instrument-only generators are handled by the caller.
int effectiveGen(const GenSet& inst, const GenSet& preset, int oper, std::int16_t defaultValue)
{
    const int base = inst.get(oper, defaultValue);
    const int offset = preset.has(oper) ? preset.get(oper, 0) : 0;
    return base + offset;
}

int clampWarn(LowerContext& ctx, const char* what, int value, int lo, int hi)
{
    if (value < lo || value > hi) {
        ctx.warn("sf2.value_clamped",
                 std::string(what) + " value " + std::to_string(value) + " clamped to ["
                     + std::to_string(lo) + ", " + std::to_string(hi) + "]");
        return std::clamp(value, lo, hi);
    }
    return value;
}

// Lower one (instrument zone x preset zone) pair into a model region.
// Returns false (with a warning already emitted) when the zone cannot sound.
bool lowerZonePair(LowerContext& ctx, const GenSet& izone, const GenSet& pzone,
                   const std::string& zoneLabel, Region& out)
{
    // --- sample reference ---
    // The terminal EOS record is not a real sample, so valid indices are
    // [0, samples.size() - 1).
    const int sampleId = izone.get(kGenSampleID, -1);
    if (sampleId < 0 || static_cast<std::size_t>(sampleId) + 1 >= ctx.sf.samples.size()) {
        ctx.warn("sf2.zone_ignored",
                 zoneLabel + ": sampleID " + std::to_string(sampleId)
                     + " does not reference a real sample; zone dropped");
        return false;
    }
    const SampleRec& s = ctx.sf.samples[static_cast<std::size_t>(sampleId)];

    if (s.sampleType & 0x8000) {
        ctx.warn("sf2.rom_sample_unsupported",
                 zoneLabel + ": ROM sample '" + s.name + "' unsupported; zone dropped");
        return false;
    }
    // SF3 (de-facto spec: FluidSynth wiki SoundFont3Format / sf3convert):
    // sampleType OR'd with 0x10 marks an Ogg Vorbis stream; shdr start/end
    // are then BYTE offsets into sdta and startloop/endloop are frame
    // offsets relative to the DECODED sample start. Everything else about
    // the zone lowers byte-identically to SF2 — downstream of the pool,
    // SF3 == SF2 (AD-2).
    const bool vorbis = (s.sampleType & 0x10) != 0;
    // §10: sample header bounds must lie within the sdta smpl data
    // (points for PCM, bytes for a compressed stream).
    const std::uint64_t bound = vorbis ? ctx.sf.smplByteCount : ctx.sf.smplSampleCount;
    if (!(s.start < s.end && s.end <= bound)) {
        ctx.warn("sf2.sample_bounds_invalid",
                 zoneLabel + ": sample '" + s.name + "' bounds [" + std::to_string(s.start)
                     + ", " + std::to_string(s.end) + ") exceed sdta sample data ("
                     + std::to_string(bound) + (vorbis ? " bytes" : " points")
                     + "); zone dropped");
        return false;
    }

    out.sampleFile = "sf2://" + ctx.containerRef + "#" + std::to_string(sampleId);

    // --- key/velocity ranges: intersection of preset and instrument zones ---
    KeyVelRange key = izone.has(kGenKeyRange) ? rangeFromAmount(izone.get(kGenKeyRange, 0))
                                              : KeyVelRange {};
    KeyVelRange vel = izone.has(kGenVelRange) ? rangeFromAmount(izone.get(kGenVelRange, 0))
                                              : KeyVelRange {};
    if (pzone.has(kGenKeyRange)) {
        const KeyVelRange pk = rangeFromAmount(pzone.get(kGenKeyRange, 0));
        key.lo = std::max(key.lo, pk.lo);
        key.hi = std::min(key.hi, pk.hi);
    }
    if (pzone.has(kGenVelRange)) {
        const KeyVelRange pv = rangeFromAmount(pzone.get(kGenVelRange, 0));
        vel.lo = std::max(vel.lo, pv.lo);
        vel.hi = std::min(vel.hi, pv.hi);
    }
    // A swapped range within one zone is repaired (robustness policy); an
    // empty preset/instrument intersection means the zone can never sound.
    if (key.lo > key.hi) {
        if (izone.has(kGenKeyRange) && pzone.has(kGenKeyRange)) {
            ctx.warn("sf2.zone_range_empty",
                     zoneLabel + ": preset/instrument key ranges do not intersect; zone dropped");
            return false;
        }
        ctx.warn("sf2.key_range_swapped", zoneLabel + ": key range swapped to stay playable");
        std::swap(key.lo, key.hi);
    }
    if (vel.lo > vel.hi) {
        if (izone.has(kGenVelRange) && pzone.has(kGenVelRange)) {
            ctx.warn("sf2.zone_range_empty",
                     zoneLabel
                         + ": preset/instrument velocity ranges do not intersect; zone dropped");
            return false;
        }
        ctx.warn("sf2.velocity_range_swapped",
                 zoneLabel + ": velocity range swapped to stay playable");
        std::swap(vel.lo, vel.hi);
    }
    out.loKey = static_cast<std::uint8_t>(clampWarn(ctx, "keyRange.lo", key.lo, 0, 127));
    out.hiKey = static_cast<std::uint8_t>(clampWarn(ctx, "keyRange.hi", key.hi, 0, 127));
    out.loVelocity = static_cast<std::uint8_t>(clampWarn(ctx, "velRange.lo", vel.lo, 0, 127));
    out.hiVelocity = static_cast<std::uint8_t>(clampWarn(ctx, "velRange.hi", vel.hi, 0, 127));

    // --- root key: overridingRootKey wins; else shdr originalPitch; else 60
    // (spec: an out-of-range originalPitch "should be assumed" 60) ---
    const int ork = izone.get(kGenOverridingRootKey, -1);
    if (ork >= 0 && ork <= 127)
        out.rootKey = static_cast<std::uint8_t>(ork);
    else if (s.originalPitch <= 127)
        out.rootKey = s.originalPitch;
    else
        out.rootKey = 60;

    // --- tuning: coarse (semitones) + fine (cents) + shdr pitchCorrection.
    // scaleTuning != 100 (key tracking scale) is not model-expressible in v0.
    const int coarse = clampWarn(ctx, "coarseTune",
                                 effectiveGen(izone, pzone, kGenCoarseTune, 0), -120, 120);
    const int fine = clampWarn(ctx, "fineTune",
                               effectiveGen(izone, pzone, kGenFineTune, 0), -99, 99);
    out.tuningCents = static_cast<float>(coarse) * 100.0f + static_cast<float>(fine)
        + static_cast<float>(s.pitchCorrection);
    const int scaleTuning = effectiveGen(izone, pzone, kGenScaleTuning, 100);
    if (scaleTuning != 100) {
        ctx.warn("sf2.generator_unsupported",
                 zoneLabel + ": scaleTuning=" + std::to_string(scaleTuning)
                     + " (non-standard key tracking) is not supported; 100 assumed");
    }

    // --- attenuation -> gain. Spec unit is centibels of attenuation
    // (positive = quieter). Real-world soundfonts are authored against the
    // de-facto 0.4x emulation factor (hardware EMU8000 behavior); FluidSynth
    // — the NFR-5 fidelity oracle — applies it, so we follow FluidSynth
    // rather than the literal spec value. Recorded as a dev note in the
    // story.
    const int attenCb = clampWarn(ctx, "initialAttenuation",
                                  effectiveGen(izone, pzone, kGenInitialAttenuation, 0), 0, 1440);
    out.gainDb = -0.4f * static_cast<float>(attenCb) / 10.0f;

    // --- pan: 0.1% units, -500 (full left) .. +500 (full right) -> -1..1 ---
    const int panRaw = clampWarn(ctx, "pan", effectiveGen(izone, pzone, kGenPan, 0), -500, 500);
    float pan = static_cast<float>(panRaw) / 500.0f;

    // --- stereo pairs (v0 choice, documented): a linked left/right mono
    // sample lowers as its own hard-panned region; the paired zone supplies
    // the other side. Generator pan is summed then clamped. ---
    const int typeBits = s.sampleType & 0x7FFF & ~0x10;
    if (typeBits == 4)
        pan -= 1.0f; // left
    else if (typeBits == 2)
        pan += 1.0f; // right
    else if (typeBits == 8) {
        ctx.warn("sf2.generator_unsupported",
                 zoneLabel + ": linked sample type 8 is not supported; treated as mono");
    }
    out.pan = std::clamp(pan, -1.0f, 1.0f);

    // --- sample offsets and loop points, in frames relative to the sample's
    // start (SamplePositionUnit::Frames — the engine converts at bind time,
    // Story 1.4 pattern). shdr addresses are absolute within sdta. ---
    const auto offsetOf = [&](int fineGen, int coarseGen) -> std::int64_t {
        return static_cast<std::int64_t>(effectiveGen(izone, pzone, fineGen, 0))
            + 32768ll * static_cast<std::int64_t>(effectiveGen(izone, pzone, coarseGen, 0));
    };
    // Frame count is known for PCM only; a compressed stream's decoded
    // length is the pool's business (decode happens at acquire(), AD-2).
    const auto sampleFrames = vorbis
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(s.end) - static_cast<std::int64_t>(s.start);
    std::int64_t startOffset = offsetOf(kGenStartAddrsOffset, kGenStartAddrsCoarseOffset);
    if (startOffset < 0 || startOffset >= sampleFrames) {
        if (startOffset != 0) {
            ctx.warn("sf2.value_clamped",
                     zoneLabel + ": start offset " + std::to_string(startOffset)
                         + " outside the sample; clamped");
        }
        startOffset = std::clamp<std::int64_t>(startOffset, 0, std::max<std::int64_t>(0, sampleFrames - 1));
    }
    out.positionUnit = SamplePositionUnit::Frames;
    out.offset = static_cast<double>(startOffset);

    // SF3 loop points are already relative to the decoded sample start; SF2
    // loop points are absolute within sdta and rebase against s.start.
    const std::int64_t loopBase = vorbis ? 0 : static_cast<std::int64_t>(s.start);
    std::int64_t loopStart = static_cast<std::int64_t>(s.startLoop) - loopBase
        + offsetOf(kGenStartloopAddrsOffset, kGenStartloopAddrsCoarseOffset);
    std::int64_t loopEnd = static_cast<std::int64_t>(s.endLoop) - loopBase
        + offsetOf(kGenEndloopAddrsOffset, kGenEndloopAddrsCoarseOffset);

    // --- sampleModes: 0/2 unlooped, 1 continuous, 3 loop-until-release
    // (approximated as continuous — the model cannot yet express release
    // phase exit; AD-1: tracked, never silent). ---
    const int mode = izone.get(kGenSampleModes, 0) & 3;
    bool loopEnabled = (mode == 1 || mode == 3);
    if (mode == 3) {
        ctx.warn("sf2.loop_mode_approximated",
                 zoneLabel + ": loop-until-release (sampleModes=3) approximated as a "
                             "continuous loop");
    }
    if (loopEnabled && !(loopStart >= 0 && loopStart < loopEnd && loopEnd <= sampleFrames)) {
        ctx.warn("sf2.loop_range_invalid",
                 zoneLabel + ": loop points [" + std::to_string(loopStart) + ", "
                     + std::to_string(loopEnd)
                     + ") do not satisfy 0 <= start < end within the sample; loop disabled");
        loopEnabled = false;
    }
    out.loopEnabled = loopEnabled;
    out.loopStart = static_cast<double>(std::max<std::int64_t>(0, loopStart));
    out.loopEnd = static_cast<double>(std::max<std::int64_t>(0, loopEnd));
    if (!loopEnabled && (out.loopStart >= out.loopEnd)) {
        // Keep disabled-loop bounds trivially valid for validate().
        out.loopStart = 0.0;
        out.loopEnd = 0.0;
    }

    // --- volume envelope (§8.1.2 gens 33-38): timecents -> seconds,
    // sustain centibels of attenuation -> normalized level ---
    auto envSeconds = [&](int gen) {
        const int tc = effectiveGen(izone, pzone, gen, -12000);
        return timecentsToSeconds(static_cast<std::int16_t>(
            std::clamp(tc, -32768, 32767)));
    };
    out.amplitudeEnvelope.delaySeconds = envSeconds(kGenDelayVolEnv);
    out.amplitudeEnvelope.attackSeconds = envSeconds(kGenAttackVolEnv);
    out.amplitudeEnvelope.holdSeconds = envSeconds(kGenHoldVolEnv);
    out.amplitudeEnvelope.decaySeconds = envSeconds(kGenDecayVolEnv);
    out.amplitudeEnvelope.releaseSeconds = envSeconds(kGenReleaseVolEnv);
    const int sustainCb = clampWarn(ctx, "sustainVolEnv",
                                    effectiveGen(izone, pzone, kGenSustainVolEnv, 0), 0, 1440);
    out.amplitudeEnvelope.sustainLevel =
        std::clamp(std::pow(10.0f, -static_cast<float>(sustainCb) / 200.0f), 0.0f, 1.0f);

    // --- generators the model cannot express: per-generator warning ---
    for (int oper = 0; oper < kGenCount; ++oper) {
        if (!isUnsupportedGen(oper))
            continue;
        const bool inInst = izone.has(oper);
        const bool inPreset = pzone.has(oper);
        if (inInst || inPreset) {
            ctx.warn("sf2.generator_unsupported",
                     zoneLabel + ": generator '" + genName(oper)
                         + "' is not supported by the v0 model; ignored");
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Modulator lowering (Story 2.2, §8.4 default set + §9.5 rules)
// ---------------------------------------------------------------------------

// SFModulator bit layout (§8.2): index bits 0-6, CC flag bit 7, direction D
// bit 8, polarity P bit 9, continuity type bits 10-15.
struct DecodedSfSource {
    bool ok = false;      // expressible as a ModSource
    bool constant = false; // index 0, no CC: "No Controller" = constant 1
    ModSource source;
};

DecodedSfSource decodeSfSource(std::uint16_t oper)
{
    DecodedSfSource out;
    const int index = oper & 0x7F;
    const bool isCc = (oper & 0x80) != 0;
    const bool direction = (oper & 0x100) != 0;
    const bool polarity = (oper & 0x200) != 0;
    const int type = (oper >> 10) & 0x3F;

    ModSource s;
    s.maxToMin = direction;
    s.bipolar = polarity;
    switch (type) {
        case 0: s.curve = ModCurveType::Linear; break;
        case 1: s.curve = ModCurveType::Concave; break;
        case 2: s.curve = ModCurveType::Convex; break;
        case 3: s.curve = ModCurveType::Switch; break;
        default: return out; // unknown continuity type
    }

    if (isCc) {
        if (index > 127)
            return out;
        s.kind = ModSourceKind::Cc;
        s.ccNumber = static_cast<std::uint8_t>(index);
        out.ok = true;
        out.source = s;
        return out;
    }
    switch (index) {
        case 0: out.constant = true; out.ok = true; s.kind = ModSourceKind::None; break;
        case 2: s.kind = ModSourceKind::Velocity; out.ok = true; break;
        case 3: s.kind = ModSourceKind::KeyTrack; out.ok = true; break;
        case 10: s.kind = ModSourceKind::PolyPressure; out.ok = true; break;
        case 13: s.kind = ModSourceKind::ChannelPressure; out.ok = true; break;
        case 14: s.kind = ModSourceKind::PitchWheel; out.ok = true; break;
        case 16: s.kind = ModSourceKind::PitchWheelSensitivity; out.ok = true; break;
        default: return out; // link (127) and reserved indices
    }
    out.source = s;
    return out;
}

// Modulator identity for supersede/summing (§9.5.3): source, destination,
// amount source and transform — amount is the value, not part of the key.
struct ModIdentity {
    std::uint16_t srcOper;
    std::uint16_t destOper;
    std::uint16_t amtSrcOper;
    std::uint16_t transOper;

    bool operator==(const ModIdentity& o) const
    {
        return srcOper == o.srcOper && destOper == o.destOper && amtSrcOper == o.amtSrcOper
            && transOper == o.transOper;
    }
};

struct ActiveModulator {
    ModIdentity id;
    int amount = 0;
};

// The SF2 default modulator set (§8.4.1-8.4.10), FluidSynth-aligned where the
// spec and de-facto behavior diverge (FluidSynth is the NFR-5 oracle):
// CC10->pan uses amount 500 (spec says 1000; FluidSynth and hardware use 500).
// §8.4.10 (pitch wheel x RPN0 sensitivity -> pitch) is intentionally absent:
// the engine reproduces it natively through Region::bendUp/DownCents, whose
// ±200-cent default matches SF2's 2-semitone RPN 0 default.
const ModRec kDefaultModulators[] = {
    { 0x0502, 48, 960, 0, 0 },  // §8.4.1 velocity -> initialAttenuation (concave, neg)
    { 0x0102, 8, -2400, 0, 0 }, // §8.4.2 velocity -> initialFilterFc (linear, neg)
    { 0x000D, 6, 50, 0, 0 },    // §8.4.3 channel pressure -> vibrato LFO pitch depth
    { 0x0081, 6, 50, 0, 0 },    // §8.4.4 CC1 -> vibrato LFO pitch depth
    { 0x0587, 48, 960, 0, 0 },  // §8.4.5 CC7 -> initialAttenuation (concave, neg)
    { 0x028A, 17, 500, 0, 0 },  // §8.4.6 CC10 -> pan (linear, bipolar; FluidSynth amount)
    { 0x058B, 48, 960, 0, 0 },  // §8.4.7 CC11 -> initialAttenuation (concave, neg)
    { 0x00DB, 16, 200, 0, 0 },  // §8.4.8 CC91 -> reverbEffectsSend
    { 0x00DD, 15, 200, 0, 0 },  // §8.4.9 CC93 -> chorusEffectsSend
};

void applyModulator(std::vector<ActiveModulator>& active, const ModRec& rec, bool additive)
{
    const ModIdentity id { rec.srcOper, rec.destOper, rec.amtSrcOper, rec.transOper };
    for (auto& m : active) {
        if (m.id == id) {
            m.amount = additive ? m.amount + rec.amount : rec.amount;
            return;
        }
    }
    active.push_back({ id, rec.amount });
}

// Zone-local supersede (§9.5.3): within one zone list a later identical
// modulator supersedes an earlier one; the local zone supersedes the global
// zone. Both are replace-operations, so applying global then local with
// replace semantics implements the rule.
void applyZoneModulators(std::vector<ActiveModulator>& active, const std::vector<ModRec>& mods,
                         std::size_t begin, std::size_t end, bool additive)
{
    for (std::size_t i = begin; i < end && i < mods.size(); ++i)
        applyModulator(active, mods[i], additive);
}

// Convert one active modulator into a model matrix entry (or fold a
// constant-source modulator into the region's static fields). Unexpressible
// combinations are tracked once per distinct identity per preset.
void lowerModulator(LowerContext& ctx, const ActiveModulator& m, Region& region,
                    std::vector<std::string>& warnedIdentities)
{
    if (m.amount == 0)
        return; // zero amount: no audible effect, nothing to lower

    const auto warnOnce = [&](const std::string& reason) {
        const std::string key = std::to_string(m.id.srcOper) + "/"
            + std::to_string(m.id.destOper) + "/" + std::to_string(m.id.amtSrcOper) + "/"
            + std::to_string(m.id.transOper);
        for (const auto& seen : warnedIdentities)
            if (seen == key)
                return;
        warnedIdentities.push_back(key);
        ctx.warn("sf2.modulator_unsupported",
                 "modulator (source 0x" + std::to_string(m.id.srcOper) + " -> "
                     + genName(m.id.destOper) + ", amount " + std::to_string(m.amount)
                     + ") " + reason);
    };

    if (m.id.transOper != 0) {
        warnOnce("uses a non-linear transform; not supported");
        return;
    }

    const DecodedSfSource src = decodeSfSource(m.id.srcOper);
    if (!src.ok) {
        warnOnce("has an unsupported source controller; dropped");
        return;
    }
    const DecodedSfSource amtSrc = decodeSfSource(m.id.amtSrcOper);
    if (!amtSrc.ok) {
        warnOnce("has an unsupported amount-source controller; dropped");
        return;
    }

    // Destination generator -> model target + depth in model units.
    ModTarget target;
    float depth = 0.0f;
    switch (m.id.destOper) {
        case kGenInitialAttenuation:
            target = ModTarget::Gain;
            // Centibels of attenuation, applied at FULL scale: FluidSynth
            // (the NFR-5 oracle) applies its 0.4x emulation factor only to
            // the static initialAttenuation GENERATOR, never to modulator
            // amounts — verified against fluidsynth 2.5.6 renders.
            depth = -static_cast<float>(m.amount) / 10.0f;
            break;
        case kGenFineTune:
            target = ModTarget::Pitch;
            depth = static_cast<float>(m.amount);
            break;
        case kGenCoarseTune:
            target = ModTarget::Pitch;
            depth = static_cast<float>(m.amount) * 100.0f;
            break;
        case kGenPan:
            target = ModTarget::Pan;
            depth = std::clamp(static_cast<float>(m.amount) / 500.0f, -1.0f, 1.0f);
            break;
        case 8: // initialFilterFc
            target = ModTarget::FilterCutoff;
            depth = static_cast<float>(m.amount);
            break;
        case 16: // reverbEffectsSend (0.1% units)
            target = ModTarget::ReverbSend;
            depth = std::clamp(static_cast<float>(m.amount) / 1000.0f, 0.0f, 1.0f);
            break;
        case 15: // chorusEffectsSend
            target = ModTarget::ChorusSend;
            depth = std::clamp(static_cast<float>(m.amount) / 1000.0f, 0.0f, 1.0f);
            break;
        default:
            warnOnce("targets a generator the model cannot modulate; dropped");
            return;
    }

    // Constant primary source ("No Controller" = 1): fold into the region's
    // static value where the model has one; a matrix entry would never vary.
    if (src.constant) {
        switch (target) {
            case ModTarget::Gain: region.gainDb += depth; return;
            case ModTarget::Pitch: region.tuningCents += depth; return;
            case ModTarget::Pan:
                region.pan = std::clamp(region.pan + depth, -1.0f, 1.0f);
                return;
            default:
                warnOnce("is a constant route to an unsupported target; dropped");
                return;
        }
    }

    ModMatrixEntry entry;
    entry.source = src.source;
    if (!amtSrc.constant)
        entry.amountSource = amtSrc.source;
    entry.target = target;
    entry.depth = depth;
    region.modMatrix.push_back(std::move(entry));
}

// Collect a zone's generators into a GenSet; `stopGen` (instrument for preset
// zones, sampleID for instrument zones) terminates the list per spec — any
// generators after it are ignored.
GenSet collectZoneGens(const std::vector<GenRec>& gens, std::size_t begin, std::size_t end,
                       int stopGen, bool& sawStop)
{
    GenSet set;
    sawStop = false;
    for (std::size_t i = begin; i < end; ++i) {
        set.apply(gens[i].oper, gens[i].amount);
        if (gens[i].oper == static_cast<std::uint16_t>(stopGen)) {
            sawStop = true;
            break;
        }
    }
    return set;
}

// Merge preset-zone generators over the preset-global zone, filtering
// instrument-only generators (spec-invalid at preset level).
GenSet mergePresetZone(LowerContext& ctx, const GenSet& global, const GenSet& zone,
                       const std::string& zoneLabel)
{
    GenSet merged = global;
    for (int oper = 0; oper < kGenCount; ++oper) {
        if (!zone.set[static_cast<std::size_t>(oper)])
            continue;
        if (isInstrumentOnlyGen(oper)) {
            ctx.warn("sf2.preset_generator_ignored",
                     zoneLabel + ": generator '" + genName(oper)
                         + "' is instrument-level only; ignored at preset level");
            continue;
        }
        merged.apply(oper, zone.value[static_cast<std::size_t>(oper)]);
    }
    return merged;
}

// Merge instrument-zone generators over the instrument-global zone.
GenSet mergeInstrumentZone(const GenSet& global, const GenSet& zone)
{
    GenSet merged = global;
    for (int oper = 0; oper < kGenCount; ++oper) {
        if (zone.set[static_cast<std::size_t>(oper)])
            merged.apply(oper, zone.value[static_cast<std::size_t>(oper)]);
    }
    return merged;
}

InstrumentModel lowerPreset(LowerContext& ctx, std::size_t presetIndex)
{
    const Sf2File& sf = ctx.sf;
    InstrumentModel model;
    model.name = sf.presets[presetIndex].name;

    const std::size_t pzBegin = sf.presets[presetIndex].bagNdx;
    const std::size_t pzEnd = sf.presets[presetIndex + 1].bagNdx;

    GenSet presetGlobal;
    bool presetGlobalSet = false;
    std::size_t presetGlobalModBegin = 0;
    std::size_t presetGlobalModEnd = 0;

    for (std::size_t pz = pzBegin; pz < pzEnd; ++pz) {
        const std::size_t genBegin = sf.pbags[pz].genNdx;
        const std::size_t genEnd = sf.pbags[pz + 1].genNdx;
        bool hasInstrument = false;
        const GenSet zone = collectZoneGens(sf.pgens, genBegin, genEnd, kGenInstrument,
                                            hasInstrument);
        const std::string pzLabel = "preset '" + sf.presets[presetIndex].name + "' zone "
            + std::to_string(pz - pzBegin);

        if (!hasInstrument) {
            // Spec §8.5: only the FIRST zone may be global; any other
            // instrument-less zone is ignored.
            if (pz == pzBegin && !presetGlobalSet) {
                presetGlobal = mergePresetZone(ctx, GenSet {}, zone, pzLabel + " (global)");
                presetGlobalSet = true;
                presetGlobalModBegin = sf.pbags[pz].modNdx;
                presetGlobalModEnd = sf.pbags[pz + 1].modNdx;
            } else {
                ctx.warn("sf2.zone_ignored",
                         pzLabel + ": no instrument generator; zone ignored");
            }
            continue;
        }

        const GenSet pzone = mergePresetZone(ctx, presetGlobal, zone, pzLabel);
        // The terminal EOI record is not a real instrument, so valid indices
        // are [0, insts.size() - 1).
        const int instIndex = zone.get(kGenInstrument, -1);
        if (instIndex < 0 || static_cast<std::size_t>(instIndex) + 1 >= sf.insts.size()) {
            ctx.warn("sf2.zone_ignored",
                     pzLabel + ": instrument index " + std::to_string(instIndex)
                         + " out of range; zone ignored");
            continue;
        }

        const std::size_t izBegin = sf.insts[static_cast<std::size_t>(instIndex)].bagNdx;
        const std::size_t izEnd = sf.insts[static_cast<std::size_t>(instIndex) + 1].bagNdx;

        GenSet instGlobal;
        bool instGlobalSet = false;
        std::size_t instGlobalModBegin = 0;
        std::size_t instGlobalModEnd = 0;
        for (std::size_t iz = izBegin; iz < izEnd; ++iz) {
            const std::size_t igBegin = sf.ibags[iz].genNdx;
            const std::size_t igEnd = sf.ibags[iz + 1].genNdx;
            bool hasSample = false;
            const GenSet izoneRaw = collectZoneGens(sf.igens, igBegin, igEnd, kGenSampleID,
                                                    hasSample);
            const std::string izLabel = "instrument '"
                + sf.insts[static_cast<std::size_t>(instIndex)].name + "' zone "
                + std::to_string(iz - izBegin);

            if (!hasSample) {
                if (iz == izBegin && !instGlobalSet) {
                    instGlobal = izoneRaw;
                    instGlobalSet = true;
                    instGlobalModBegin = sf.ibags[iz].modNdx;
                    instGlobalModEnd = sf.ibags[iz + 1].modNdx;
                } else {
                    ctx.warn("sf2.zone_ignored",
                             izLabel + ": no sampleID generator; zone ignored");
                }
                continue;
            }

            const GenSet izone = mergeInstrumentZone(instGlobal, izoneRaw);
            Region region;
            if (lowerZonePair(ctx, izone, pzone, izLabel, region)) {
                // Modulators (§9.5.3): defaults, then instrument modulators
                // (global zone then local, replace/supersede semantics), then
                // preset modulators (global then local, additive).
                std::vector<ActiveModulator> active;
                for (const ModRec& rec : kDefaultModulators)
                    applyModulator(active, rec, /*additive=*/false);
                applyZoneModulators(active, sf.imods, instGlobalModBegin, instGlobalModEnd,
                                    /*additive=*/false);
                applyZoneModulators(active, sf.imods, sf.ibags[iz].modNdx,
                                    sf.ibags[iz + 1].modNdx, /*additive=*/false);
                applyZoneModulators(active, sf.pmods, presetGlobalModBegin,
                                    presetGlobalModEnd, /*additive=*/true);
                applyZoneModulators(active, sf.pmods, sf.pbags[pz].modNdx,
                                    sf.pbags[pz + 1].modNdx, /*additive=*/true);
                for (const ActiveModulator& m : active)
                    lowerModulator(ctx, m, region, ctx.warnedModIdentities);
                model.regions.push_back(std::move(region));
            }
        }
    }

    return model;
}

// ---------------------------------------------------------------------------
// Entry-point plumbing
// ---------------------------------------------------------------------------

bool readWholeFile(const std::string& path, std::vector<std::uint8_t>& out,
                   std::vector<Diagnostic>& diags)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        diags.push_back(makeDiag(Severity::Error, "sf2.file_open_failed",
                                 "cannot open SoundFont file: " + path, path));
        return false;
    }
    std::uint8_t chunk[65536];
    std::size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        out.insert(out.end(), chunk, chunk + n);
    std::fclose(f);
    return true;
}

LowerResult lowerParsedPreset(const Sf2File& sf, std::size_t presetIndex,
                              const std::string& file)
{
    LowerResult result;
    LowerContext ctx { sf, file, file, result.diagnostics };
    InstrumentModel model = lowerPreset(ctx, presetIndex);

    // The lowered model must honor the Story-1.2 contract; the clamps above
    // are designed to make violations unreachable, but verify anyway so a
    // lowering bug surfaces as Diagnostics, not as a corrupt model downstream.
    auto validationDiags = validate(model);
    const bool validationFailed = !validationDiags.empty();
    for (auto& d : validationDiags) {
        d.location.file = file;
        result.diagnostics.push_back(std::move(d));
    }
    if (validationFailed)
        return result;

    result.model = std::move(model);
    return result;
}

LowerResult lowerImpl(const std::uint8_t* data, std::size_t size, int bank, int program,
                      const std::string& file)
{
    LowerResult result;

    Sf2File sf;
    if (!parseSf2(data, size, file, sf, result.diagnostics))
        return result;

    // Terminal EOP record makes the real preset count size()-1.
    std::size_t presetIndex = sf.presets.size();
    for (std::size_t i = 0; i + 1 < sf.presets.size(); ++i) {
        if (sf.presets[i].bank == bank && sf.presets[i].preset == program) {
            presetIndex = i;
            break;
        }
    }
    if (presetIndex >= sf.presets.size()) {
        result.diagnostics.push_back(makeDiag(
            Severity::Error, "sf2.preset_not_found",
            "no preset with bank " + std::to_string(bank) + ", program "
                + std::to_string(program),
            file));
        return result;
    }

    auto lowered = lowerParsedPreset(sf, presetIndex, file);
    for (auto& d : lowered.diagnostics)
        result.diagnostics.push_back(std::move(d));
    result.model = std::move(lowered.model);
    return result;
}

Sf2PresetListResult listImpl(const std::uint8_t* data, std::size_t size, const std::string& file)
{
    Sf2PresetListResult result;
    Sf2File sf;
    if (!parseSf2(data, size, file, sf, result.diagnostics))
        return result;

    std::vector<Sf2PresetInfo> presets;
    presets.reserve(sf.presets.size() > 0 ? sf.presets.size() - 1 : 0);
    for (std::size_t i = 0; i + 1 < sf.presets.size(); ++i) {
        Sf2PresetInfo info;
        info.bank = sf.presets[i].bank;
        info.program = sf.presets[i].preset;
        info.name = sf.presets[i].name;
        presets.push_back(std::move(info));
    }
    result.presets = std::move(presets);
    return result;
}

} // namespace

LowerResult lowerSf2Preset(const std::string& path, int bank, int program)
{
    try {
        std::vector<std::uint8_t> bytes;
        LowerResult result;
        if (!readWholeFile(path, bytes, result.diagnostics))
            return result;
        return lowerImpl(bytes.data(), bytes.size(), bank, program, path);
    } catch (const std::exception& e) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              std::string("internal error: ") + e.what(), path));
        return result;
    } catch (...) {
        LowerResult result;
        result.diagnostics.push_back(
            makeDiag(Severity::Error, "sf2.internal_error", "unknown internal error", path));
        return result;
    }
}

LowerResult lowerSf2Bytes(const std::uint8_t* data, std::size_t size, int bank, int program,
                          const std::string& virtualPath)
{
    try {
        return lowerImpl(data, size, bank, program, virtualPath);
    } catch (const std::exception& e) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              std::string("internal error: ") + e.what(),
                                              virtualPath));
        return result;
    } catch (...) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              "unknown internal error", virtualPath));
        return result;
    }
}

Sf2PresetListResult listSf2Presets(const std::string& path)
{
    try {
        std::vector<std::uint8_t> bytes;
        Sf2PresetListResult result;
        if (!readWholeFile(path, bytes, result.diagnostics))
            return result;
        return listImpl(bytes.data(), bytes.size(), path);
    } catch (const std::exception& e) {
        Sf2PresetListResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              std::string("internal error: ") + e.what(), path));
        return result;
    } catch (...) {
        Sf2PresetListResult result;
        result.diagnostics.push_back(
            makeDiag(Severity::Error, "sf2.internal_error", "unknown internal error", path));
        return result;
    }
}

Sf2PresetListResult listSf2PresetsFromBytes(const std::uint8_t* data, std::size_t size,
                                            const std::string& virtualPath)
{
    try {
        return listImpl(data, size, virtualPath);
    } catch (const std::exception& e) {
        Sf2PresetListResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              std::string("internal error: ") + e.what(),
                                              virtualPath));
        return result;
    } catch (...) {
        Sf2PresetListResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              "unknown internal error", virtualPath));
        return result;
    }
}

// ---------------------------------------------------------------------------
// Sf2Session (Story 2.4): parse once, enumerate, lower on demand.
// ---------------------------------------------------------------------------

struct Sf2Session::Impl {
    Sf2File sf;
    std::string path;
    std::vector<Sf2PresetInfo> sorted;   // bank asc, program asc
    std::vector<std::size_t> rawIndices; // sorted index -> phdr record index
};

// Grants openSf2Session access to the private constructor without exposing it.
struct Sf2SessionAccess {
    static std::shared_ptr<Sf2Session> create()
    {
        return std::shared_ptr<Sf2Session>(new Sf2Session());
    }
};

Sf2Session::Sf2Session() : impl_(new Impl()) {}
Sf2Session::~Sf2Session() = default;

const std::vector<Sf2PresetInfo>& Sf2Session::presets() const { return impl_->sorted; }
const std::string& Sf2Session::path() const { return impl_->path; }

LowerResult Sf2Session::lowerPreset(std::size_t index) const
{
    try {
        if (index >= impl_->rawIndices.size()) {
            LowerResult result;
            result.diagnostics.push_back(makeDiag(
                Severity::Error, "sf2.preset_not_found",
                "preset index " + std::to_string(index) + " out of range ("
                    + std::to_string(impl_->rawIndices.size()) + " presets)",
                impl_->path));
            return result;
        }
        return lowerParsedPreset(impl_->sf, impl_->rawIndices[index], impl_->path);
    } catch (const std::exception& e) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              std::string("internal error: ") + e.what(),
                                              impl_->path));
        return result;
    } catch (...) {
        LowerResult result;
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              "unknown internal error", impl_->path));
        return result;
    }
}

Sf2SessionResult openSf2Session(const std::string& path)
{
    Sf2SessionResult result;
    try {
        std::vector<std::uint8_t> bytes;
        if (!readWholeFile(path, bytes, result.diagnostics))
            return result;

        auto session = Sf2SessionAccess::create();
        session->impl_->path = path;
        if (!parseSf2(bytes.data(), bytes.size(), path, session->impl_->sf,
                      result.diagnostics))
            return result;

        const auto& sf = session->impl_->sf;
        auto& idx = session->impl_->rawIndices;
        for (std::size_t i = 0; i + 1 < sf.presets.size(); ++i)
            idx.push_back(i);
        std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
            if (sf.presets[a].bank != sf.presets[b].bank)
                return sf.presets[a].bank < sf.presets[b].bank;
            if (sf.presets[a].preset != sf.presets[b].preset)
                return sf.presets[a].preset < sf.presets[b].preset;
            return a < b; // stable for duplicate bank:program records
        });
        for (std::size_t raw : idx) {
            Sf2PresetInfo info;
            info.bank = sf.presets[raw].bank;
            info.program = sf.presets[raw].preset;
            info.name = sf.presets[raw].name;
            session->impl_->sorted.push_back(std::move(info));
        }
        result.session = std::move(session);
        return result;
    } catch (const std::exception& e) {
        result.session.reset();
        result.diagnostics.push_back(makeDiag(Severity::Error, "sf2.internal_error",
                                              std::string("internal error: ") + e.what(),
                                              path));
        return result;
    } catch (...) {
        result.session.reset();
        result.diagnostics.push_back(
            makeDiag(Severity::Error, "sf2.internal_error", "unknown internal error", path));
        return result;
    }
}

} // namespace fbsampler
