// Fuzz entry point for the SFZ frontend (NFR-4, AD-10: parsers are the attack
// surface and are fuzzable in isolation). lowerSfzText() performs no
// file-system access, so arbitrary bytes exercise the parser + lowering only.
//
// Contract asserted: no crash, no exception escaping the core API, no
// leak/UB (run under ASan/UBSan), and errors imply no model.

#include "fbsampler/sfz_frontend.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    // libFuzzer may pass (nullptr, 0); std::string's (ptr, len) constructor is
    // UB on a null pointer even for length 0.
    const std::string text = (data == nullptr || size == 0)
        ? std::string()
        : std::string(reinterpret_cast<const char*>(data), size);
    const auto result = fbsampler::lowerSfzText(text, "fuzz.sfz");

    bool hasError = false;
    for (const auto& d : result.diagnostics)
        if (d.severity == fbsampler::Severity::Error)
            hasError = true;
    if (hasError && result.model.has_value())
        std::abort(); // invariant: structural errors suppress the model

    return 0;
}
