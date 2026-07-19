#pragma once

#include "render_harness.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fbsampler::testutil {

/// Parse a Standard MIDI File (format 0 or 1, PPQN division) into an
/// absolute-frame timeline at `sampleRate`. Note on/off, control change,
/// pitch bend and tempo meta events are honored; everything else is skipped.
/// Note-on with velocity 0 becomes NoteOff. Returns false (with `error` set
/// when non-null) on malformed input or SMPTE division.
///
/// Corpus fixtures (Story 1.6) are checked in as real .mid files so the same
/// bytes drive both the external oracle capture and this offline runner.
bool parseMidiTimeline(const std::uint8_t* bytes, std::size_t size,
                       double sampleRate, std::vector<TimelineEvent>* out,
                       std::string* error);

bool loadMidiTimeline(const std::string& path, double sampleRate,
                      std::vector<TimelineEvent>* out, std::string* error);

} // namespace fbsampler::testutil
