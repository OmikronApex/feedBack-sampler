#include "midi_file.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace fbsampler::testutil {

namespace {

struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;

    bool remaining(std::size_t n) const { return static_cast<std::size_t>(end - p) >= n; }

    bool u8(std::uint8_t& out)
    {
        if (!remaining(1))
            return false;
        out = *p++;
        return true;
    }

    bool u16(std::uint16_t& out)
    {
        if (!remaining(2))
            return false;
        out = static_cast<std::uint16_t>((p[0] << 8) | p[1]);
        p += 2;
        return true;
    }

    bool u32(std::uint32_t& out)
    {
        if (!remaining(4))
            return false;
        out = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
              | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
        p += 4;
        return true;
    }

    bool varint(std::uint32_t& out)
    {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t b = 0;
            if (!u8(b))
                return false;
            out = (out << 7) | (b & 0x7Fu);
            if ((b & 0x80u) == 0)
                return true;
        }
        return false; // > 4 bytes: malformed
    }

    bool skip(std::size_t n)
    {
        if (!remaining(n))
            return false;
        p += n;
        return true;
    }
};

// One channel event with tick time plus a stable index so simultaneous events
// keep file order after the format-1 merge sort.
struct TickEvent {
    std::uint64_t tick = 0;
    std::size_t order = 0;
    TimelineEvent event; // frame filled in after tempo mapping
    bool isTempo = false;
    std::uint32_t usPerQuarter = 0;
};

bool fail(std::string* error, const char* message)
{
    if (error)
        *error = message;
    return false;
}

bool parseTrack(Reader& r, std::vector<TickEvent>& events, std::size_t& order,
                std::string* error)
{
    std::uint64_t tick = 0;
    std::uint8_t status = 0;
    for (;;) {
        std::uint32_t delta = 0;
        if (!r.varint(delta))
            return fail(error, "truncated delta time");
        tick += delta;

        if (!r.remaining(1))
            return fail(error, "truncated event");
        std::uint8_t first = *r.p;
        if (first & 0x80u) {
            r.skip(1);
            status = first;
        } else if (status == 0) {
            return fail(error, "data byte with no running status");
        }

        if (status == 0xFF) { // meta
            std::uint8_t type = 0;
            std::uint32_t length = 0;
            if (!r.u8(type) || !r.varint(length) || !r.remaining(length))
                return fail(error, "truncated meta event");
            if (type == 0x2F)
                return true; // end of track
            if (type == 0x51 && length == 3) {
                TickEvent e;
                e.tick = tick;
                e.order = order++;
                e.isTempo = true;
                e.usPerQuarter = (static_cast<std::uint32_t>(r.p[0]) << 16)
                                 | (static_cast<std::uint32_t>(r.p[1]) << 8) | r.p[2];
                events.push_back(e);
            }
            r.skip(length);
            status = 0; // meta/sysex cancel running status
            continue;
        }
        if (status == 0xF0 || status == 0xF7) { // sysex
            std::uint32_t length = 0;
            if (!r.varint(length) || !r.skip(length))
                return fail(error, "truncated sysex event");
            status = 0;
            continue;
        }

        const std::uint8_t kind = status & 0xF0u;
        std::uint8_t d1 = 0, d2 = 0;
        if (first & 0x80u) {
            if (!r.u8(d1))
                return fail(error, "truncated channel event");
        } else {
            r.skip(1);
            d1 = first;
        }
        if (kind != 0xC0 && kind != 0xD0) {
            if (!r.u8(d2))
                return fail(error, "truncated channel event");
        }

        TickEvent e;
        e.tick = tick;
        e.order = order++;
        switch (kind) {
        case 0x90:
            e.event.type = (d2 == 0) ? EngineEvent::Type::NoteOff : EngineEvent::Type::NoteOn;
            e.event.note = d1;
            e.event.velocity = d2;
            events.push_back(e);
            break;
        case 0x80:
            e.event.type = EngineEvent::Type::NoteOff;
            e.event.note = d1;
            e.event.velocity = d2;
            events.push_back(e);
            break;
        case 0xB0:
            e.event.type = EngineEvent::Type::ControlChange;
            e.event.note = d1;
            e.event.velocity = d2;
            events.push_back(e);
            break;
        case 0xE0:
            e.event.type = EngineEvent::Type::PitchBend;
            e.event.bendValue = ((d2 << 7) | d1) - 8192;
            events.push_back(e);
            break;
        default:
            break; // program change / aftertouch: consumed, not emitted
        }
    }
}

} // namespace

bool parseMidiTimeline(const std::uint8_t* bytes, std::size_t size, double sampleRate,
                       std::vector<TimelineEvent>* out, std::string* error)
{
    out->clear();
    Reader r{bytes, bytes + size};

    std::uint32_t magic = 0, headerLength = 0;
    std::uint16_t format = 0, trackCount = 0, division = 0;
    if (!r.u32(magic) || magic != 0x4D546864u) // 'MThd'
        return fail(error, "not a Standard MIDI File (missing MThd)");
    if (!r.u32(headerLength) || headerLength < 6)
        return fail(error, "bad MThd length");
    if (!r.u16(format) || !r.u16(trackCount) || !r.u16(division))
        return fail(error, "truncated MThd");
    if (!r.skip(headerLength - 6))
        return fail(error, "truncated MThd");
    if (format > 1)
        return fail(error, "unsupported SMF format (only 0 and 1)");
    if (division & 0x8000u)
        return fail(error, "SMPTE division not supported");
    if (division == 0)
        return fail(error, "zero ticks-per-quarter division");

    std::vector<TickEvent> events;
    std::size_t order = 0;
    for (std::uint16_t t = 0; t < trackCount; ++t) {
        std::uint32_t chunkMagic = 0, chunkLength = 0;
        if (!r.u32(chunkMagic) || !r.u32(chunkLength) || !r.remaining(chunkLength))
            return fail(error, "truncated track chunk");
        if (chunkMagic != 0x4D54726Bu) { // 'MTrk' -- skip alien chunks
            r.skip(chunkLength);
            ++trackCount; // does not count toward declared track total
            continue;
        }
        Reader track{r.p, r.p + chunkLength};
        if (!parseTrack(track, events, order, error))
            return false;
        r.skip(chunkLength);
    }

    std::sort(events.begin(), events.end(), [](const TickEvent& a, const TickEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    // Piecewise tempo map: walk events in tick order, accumulating seconds.
    double seconds = 0.0;
    std::uint64_t lastTick = 0;
    double usPerQuarter = 500000.0; // SMF default: 120 bpm
    const double ticksPerQuarter = static_cast<double>(division);
    for (const TickEvent& e : events) {
        seconds += static_cast<double>(e.tick - lastTick) * usPerQuarter
                   / (ticksPerQuarter * 1e6);
        lastTick = e.tick;
        if (e.isTempo) {
            usPerQuarter = static_cast<double>(e.usPerQuarter);
            continue;
        }
        TimelineEvent timelineEvent = e.event;
        timelineEvent.frame = static_cast<std::uint64_t>(seconds * sampleRate + 0.5);
        out->push_back(timelineEvent);
    }
    return true;
}

bool loadMidiTimeline(const std::string& path, double sampleRate,
                      std::vector<TimelineEvent>* out, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return fail(error, "cannot open MIDI file");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return parseMidiTimeline(bytes.data(), bytes.size(), sampleRate, out, error);
}

} // namespace fbsampler::testutil
