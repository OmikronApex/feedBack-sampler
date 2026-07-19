// Story 1.6: end-to-end self-test of the corpus entry pipeline (lower ->
// MIDI fixture -> render -> reference diff -> golden dump) on the checked-in
// seed instrument, so the corpus runner's machinery is regression-tested on
// every platform without network access to the corpus assets themselves.

#include "corpus_render.h"
#include "seed_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <string>

using namespace fbsampler::testutil;

namespace {

std::string seedMidiPath()
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/seed/seed_corpus_test.mid";
}

std::string tempPath(const char* name)
{
    return std::string(FBSAMPLER_RENDER_FIXTURE_DIR) + "/../" + name;
}

} // namespace

TEST_CASE("corpus entry: capture then re-render passes thresholds", "[corpus]")
{
    constexpr std::uint64_t kFrames = 192000; // 4 s at 48 kHz
    const std::string ref = tempPath("corpus_selftest_ref.wav");
    const std::string golden = tempPath("corpus_selftest_golden.txt");

    // Capture mode: no reference, write render + golden.
    CorpusEntryResult captured = runCorpusEntry(seedInstrumentSfzPath(), seedMidiPath(),
                                                kFrames, {}, CorpusThresholds{}, ref, golden);
    INFO(captured.error);
    REQUIRE(captured.loaded);
    REQUIRE(captured.rendered);
    REQUIRE(captured.passed);
    CHECK(captured.energy > 1.0);
    CHECK_FALSE(captured.refCompared);

    // Golden dump exists and looks like the serialized model format.
    {
        std::ifstream in(golden, std::ios::binary);
        REQUIRE(in.is_open());
        std::string firstLine;
        std::getline(in, firstLine);
        CHECK(firstLine.rfind("schema_version=", 0) == 0);
    }

    // Compare mode: identical render must pass the NFR-5 thresholds (the
    // only deviation is the PCM16 reference quantization floor).
    CorpusEntryResult compared = runCorpusEntry(seedInstrumentSfzPath(), seedMidiPath(),
                                                kFrames, ref, CorpusThresholds{});
    INFO(compared.error);
    REQUIRE(compared.refCompared);
    CHECK(compared.passed);
    CHECK(compared.peakDiff < 1e-3); // PCM16 floor, far under threshold
    CHECK(compared.referenceFrames == kFrames);

    // A behavioral change must fail: diff against a wrong-length reference.
    CorpusEntryResult mismatched = runCorpusEntry(seedInstrumentSfzPath(), seedMidiPath(),
                                                  kFrames / 2, ref, CorpusThresholds{});
    CHECK_FALSE(mismatched.passed);

    std::remove(ref.c_str());
    std::remove(golden.c_str());
}

TEST_CASE("corpus entry: missing sfz reports load failure", "[corpus]")
{
    CorpusEntryResult r = runCorpusEntry("does_not_exist.sfz", seedMidiPath(), 48000, {},
                                         CorpusThresholds{});
    CHECK_FALSE(r.loaded);
    CHECK_FALSE(r.passed);
    CHECK_FALSE(r.error.empty());
}
