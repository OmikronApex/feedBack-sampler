// Story 1.6: the corpus runner consumes checked-in .mid fixtures. This pins
// the minimal SMF reader: delta-time decoding, tempo scaling, running status,
// note-on-velocity-0 -> NoteOff, and format-1 track merging.

#include "midi_file.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace fbsampler::testutil;

namespace {

void push32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void push16(std::vector<std::uint8_t>& v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

std::vector<std::uint8_t> smf(std::uint16_t format, std::uint16_t division,
                              const std::vector<std::vector<std::uint8_t>>& tracks)
{
    std::vector<std::uint8_t> v = {'M', 'T', 'h', 'd'};
    push32(v, 6);
    push16(v, format);
    push16(v, static_cast<std::uint16_t>(tracks.size()));
    push16(v, division);
    for (const auto& t : tracks) {
        v.insert(v.end(), {'M', 'T', 'r', 'k'});
        push32(v, static_cast<std::uint32_t>(t.size()));
        v.insert(v.end(), t.begin(), t.end());
    }
    return v;
}

const std::vector<std::uint8_t> kEndOfTrack = {0x00, 0xFF, 0x2F, 0x00};

} // namespace

TEST_CASE("SMF format 0: notes, tempo, running status", "[midi]")
{
    // Division 480 ticks/quarter. Default tempo 120 bpm = 500000 us/quarter,
    // so 480 ticks = 0.5 s = 24000 frames at 48 kHz.
    std::vector<std::uint8_t> trk;
    // t=0: note on ch0, key 60, vel 100
    trk.insert(trk.end(), {0x00, 0x90, 60, 100});
    // t=480: running status, note 60 vel 0 -> NoteOff
    trk.insert(trk.end(), {0x83, 0x60, 60, 0});
    // t=480: CC 64 = 127
    trk.insert(trk.end(), {0x00, 0xB0, 64, 127});
    // t=480: pitch bend center + 1 (lsb=1 msb=64 -> value 1)
    trk.insert(trk.end(), {0x00, 0xE0, 0x01, 0x40});
    trk.insert(trk.end(), kEndOfTrack.begin(), kEndOfTrack.end());

    const auto bytes = smf(0, 480, {trk});
    std::vector<TimelineEvent> timeline;
    std::string error;
    REQUIRE(parseMidiTimeline(bytes.data(), bytes.size(), 48000.0, &timeline, &error));
    REQUIRE(timeline.size() == 4);

    CHECK(timeline[0].frame == 0);
    CHECK(timeline[0].type == fbsampler::EngineEvent::Type::NoteOn);
    CHECK(timeline[0].note == 60);
    CHECK(timeline[0].velocity == 100);

    CHECK(timeline[1].frame == 24000);
    CHECK(timeline[1].type == fbsampler::EngineEvent::Type::NoteOff);
    CHECK(timeline[1].note == 60);

    CHECK(timeline[2].frame == 24000);
    CHECK(timeline[2].type == fbsampler::EngineEvent::Type::ControlChange);
    CHECK(timeline[2].note == 64);
    CHECK(timeline[2].velocity == 127);

    CHECK(timeline[3].frame == 24000);
    CHECK(timeline[3].type == fbsampler::EngineEvent::Type::PitchBend);
    CHECK(timeline[3].bendValue == 1);
}

TEST_CASE("SMF tempo meta rescales later deltas", "[midi]")
{
    // Set tempo to 60 bpm (1000000 us/quarter) at t=0, then a note at 480
    // ticks: 1 quarter = 1 s = 48000 frames.
    std::vector<std::uint8_t> trk;
    trk.insert(trk.end(), {0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40});
    trk.insert(trk.end(), {0x83, 0x60, 0x90, 72, 90});
    trk.insert(trk.end(), kEndOfTrack.begin(), kEndOfTrack.end());

    const auto bytes = smf(0, 480, {trk});
    std::vector<TimelineEvent> timeline;
    REQUIRE(parseMidiTimeline(bytes.data(), bytes.size(), 48000.0, &timeline, nullptr));
    REQUIRE(timeline.size() == 1);
    CHECK(timeline[0].frame == 48000);
    CHECK(timeline[0].note == 72);
}

TEST_CASE("SMF format 1 merges tracks in time order", "[midi]")
{
    std::vector<std::uint8_t> a, b;
    a.insert(a.end(), {0x00, 0x90, 60, 100});
    a.insert(a.end(), kEndOfTrack.begin(), kEndOfTrack.end());
    // 240 ticks = 0.25 s at default tempo
    b.insert(b.end(), {0x81, 0x70, 0x90, 64, 80});
    b.insert(b.end(), kEndOfTrack.begin(), kEndOfTrack.end());

    const auto bytes = smf(1, 480, {a, b});
    std::vector<TimelineEvent> timeline;
    REQUIRE(parseMidiTimeline(bytes.data(), bytes.size(), 48000.0, &timeline, nullptr));
    REQUIRE(timeline.size() == 2);
    CHECK(timeline[0].note == 60);
    CHECK(timeline[0].frame == 0);
    CHECK(timeline[1].note == 64);
    CHECK(timeline[1].frame == 12000);
}

TEST_CASE("SMF rejects garbage and SMPTE division", "[midi]")
{
    std::vector<TimelineEvent> timeline;
    std::string error;

    const std::vector<std::uint8_t> garbage = {'R', 'I', 'F', 'F', 0, 0};
    CHECK_FALSE(parseMidiTimeline(garbage.data(), garbage.size(), 48000.0, &timeline, &error));
    CHECK_FALSE(error.empty());

    // SMPTE division (top bit set) is out of scope for corpus fixtures.
    std::vector<std::uint8_t> trk;
    trk.insert(trk.end(), kEndOfTrack.begin(), kEndOfTrack.end());
    const auto smpte = smf(0, 0xE250, {trk});
    CHECK_FALSE(parseMidiTimeline(smpte.data(), smpte.size(), 48000.0, &timeline, &error));

    // Truncated track data must not read out of bounds.
    auto truncated = smf(0, 480, {{0x00, 0x90, 60}});
    CHECK_FALSE(parseMidiTimeline(truncated.data(), truncated.size(), 48000.0, &timeline, &error));
}
