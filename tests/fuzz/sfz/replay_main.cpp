// Fallback fuzz runner for toolchains without libFuzzer (MSVC on the Windows
// CI leg, plain GCC): replays the checked-in seed corpus, then feeds each seed
// through a deterministic xorshift mutation schedule (byte flips, truncations,
// duplications). Same harness contract as the libFuzzer build — the entry
// point is the shared LLVMFuzzerTestOneInput above — so every platform runs a
// short deterministic fuzz pass on every build; libFuzzer (-DFBSAMPLER_LIBFUZZER=ON,
// clang only) explores beyond it.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

std::uint64_t xorshiftState = 0x9e3779b97f4a7c15ull; // fixed seed: deterministic run

std::uint64_t nextRandom()
{
    std::uint64_t x = xorshiftState;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    xorshiftState = x;
    return x;
}

void run(const std::vector<std::uint8_t>& bytes)
{
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
}

void mutateAndRun(const std::vector<std::uint8_t>& seed)
{
    constexpr int kMutantsPerSeed = 64;
    constexpr int kMaxEditsPerMutant = 8;

    for (int m = 0; m < kMutantsPerSeed; ++m) {
        std::vector<std::uint8_t> mutant = seed;
        const int edits = 1 + static_cast<int>(nextRandom() % kMaxEditsPerMutant);
        for (int e = 0; e < edits && !mutant.empty(); ++e) {
            switch (nextRandom() % 4) {
            case 0: // flip a byte
                mutant[nextRandom() % mutant.size()] =
                    static_cast<std::uint8_t>(nextRandom());
                break;
            case 1: // truncate
                mutant.resize(nextRandom() % (mutant.size() + 1));
                break;
            case 2: { // duplicate a slice
                const std::size_t at = nextRandom() % mutant.size();
                const std::size_t len =
                    std::min<std::size_t>(mutant.size() - at, 1 + nextRandom() % 16);
                mutant.insert(mutant.end(), mutant.begin() + at, mutant.begin() + at + len);
                break;
            }
            default: // insert a random byte
                mutant.insert(mutant.begin() + (nextRandom() % (mutant.size() + 1)),
                              static_cast<std::uint8_t>(nextRandom()));
                break;
            }
        }
        run(mutant);
    }
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path corpusDir = FBSAMPLER_FUZZ_CORPUS_DIR;

    // error_code overload: a missing/unreadable corpus dir must exit(1) with a
    // message, not die on an uncaught filesystem_error.
    std::error_code ec;
    fs::directory_iterator it(corpusDir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot open corpus directory %s: %s\n",
                     corpusDir.string().c_str(), ec.message().c_str());
        return 1;
    }

    // directory_iterator order is unspecified; sort so the single-RNG mutation
    // schedule is actually deterministic across platforms/filesystems.
    std::vector<fs::path> paths;
    for (const auto& entry : it) {
        if (entry.is_regular_file())
            paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    std::size_t seeds = 0;
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::fprintf(stderr, "cannot open corpus file: %s\n", path.string().c_str());
            return 1;
        }
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        run(bytes);
        mutateAndRun(bytes);
        ++seeds;
    }

    if (seeds == 0) {
        std::fprintf(stderr, "no corpus files found in %s\n", corpusDir.string().c_str());
        return 1;
    }

    std::printf("sfz fuzz replay: %zu seeds x mutations, no crash\n", seeds);
    return 0;
}
