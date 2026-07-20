// Fuzz entry point for the SF2 frontend (Story 2.1 AC2, NFR-4, AD-10:
// parsers are the attack surface and are fuzzable in isolation).
// lowerSf2Bytes()/listSf2PresetsFromBytes() perform no file-system access, so
// arbitrary bytes exercise the RIFF/hydra parser + zone lowering only.
//
// Contract asserted: no crash, no exception escaping the core API, no
// leak/UB (run under ASan/UBSan), and errors imply no model.

#include "fbsampler/sf2_frontend.h"

// Internal pool reader (Story 2.3): the Vorbis decode path is the new attack
// surface and must be reachable without disk I/O — fuzz through lower AND
// acquire-equivalent decode, not lower alone.
#include "pool/sf2_sample_reader.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto list = fbsampler::listSf2PresetsFromBytes(data, size, "fuzz.sf2");

    bool listHasError = false;
    for (const auto& d : list.diagnostics)
        if (d.severity == fbsampler::Severity::Error)
            listHasError = true;
    if (listHasError && list.presets.has_value())
        std::abort(); // invariant: structural errors suppress the preset list

    // Lower every enumerated preset (bounded by the input's own phdr count).
    if (list.presets.has_value()) {
        for (const auto& p : *list.presets) {
            const auto result = fbsampler::lowerSf2Bytes(data, size, p.bank, p.program,
                                                         "fuzz.sf2");
            bool hasError = false;
            for (const auto& d : result.diagnostics)
                if (d.severity == fbsampler::Severity::Error)
                    hasError = true;
            if (hasError && result.model.has_value())
                std::abort(); // invariant: structural errors suppress the model
        }
    }

    // And the default-address lowering path for inputs whose preset list
    // itself failed structurally.
    const auto result = fbsampler::lowerSf2Bytes(data, size, 0, 0, "fuzz.sf2");
    bool hasError = false;
    for (const auto& d : result.diagnostics)
        if (d.severity == fbsampler::Severity::Error)
            hasError = true;
    if (hasError && result.model.has_value())
        std::abort();

    // Pool-side sample decode (PCM range copy + SF3 Vorbis): bounded number
    // of indices; every failure must come back as Diagnostics, never a
    // crash/OOB (ASan/UBSan enforce).
    for (std::uint32_t index = 0; index < 4; ++index) {
        fbsampler::detail::DecodedWav wav;
        std::vector<fbsampler::Diagnostic> diags;
        (void)fbsampler::detail::readSf2SampleFromMemory(data, size, index, wav, &diags,
                                                         "fuzz.sf2");
    }

    return 0;
}
