#!/usr/bin/env python3
"""Generate the fixed corpus MIDI fixtures (Story 1.6).

The .mid files under corpus/midi/ are checked in; this script is the
documented, reproducible way they were produced. The same bytes drive both
the external oracle capture (drag into Sforzando's host) and the offline
corpus runner, so regenerate only when a sequence deliberately changes --
that invalidates the entry's reference and golden.

Type-0 SMF, 480 ticks/quarter, default tempo (120 bpm): 480 ticks = 0.5 s.
"""

from __future__ import annotations

import os

TICKS_PER_QUARTER = 480
TICKS_PER_SECOND = 960  # at the default 120 bpm


def varint(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def smf_type0(events: list[tuple[int, bytes]]) -> bytes:
    """events: (absolute_tick, channel-event bytes) -> complete SMF."""
    events = sorted(events, key=lambda e: e[0])
    track = b""
    last = 0
    for tick, data in events:
        track += varint(tick - last) + data
        last = tick
    track += b"\x00\xff\x2f\x00"  # end of track
    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") \
        + (1).to_bytes(2, "big") + TICKS_PER_QUARTER.to_bytes(2, "big")
    return header + b"MTrk" + len(track).to_bytes(4, "big") + track


def note(t_on: float, t_off: float, key: int, vel: int) -> list[tuple[int, bytes]]:
    return [
        (round(t_on * TICKS_PER_SECOND), bytes([0x90, key, vel])),
        (round(t_off * TICKS_PER_SECOND), bytes([0x80, key, 0])),
    ]


# Each sequence covers: soft + loud velocities (velocity layers), a held note
# (sustain), and note-offs early enough for release tails to decay inside the
# 4-second render window (192000 frames at 48 kHz).
SEQUENCES = {
    # Baroque Soprano Recorder - Staccato: playable range 72..98.
    "vcsl-baroque-soprano-recorder-staccato":
        note(0.0, 0.6, 72, 45) + note(0.8, 1.4, 76, 100)
        + note(1.6, 2.8, 84, 115) + note(3.0, 3.4, 91, 80),
    # Ocarina, Small - Staccato: playable range 81..97.
    "vcsl-ocarina-small-staccato":
        note(0.0, 0.6, 81, 45) + note(0.8, 1.4, 86, 100)
        + note(1.6, 2.8, 90, 115) + note(3.0, 3.4, 96, 80),
    # Claves: keys 60 and 61, three velocity layers (lovel/hivel splits at
    # 65/99) -- hit each layer on both keys.
    "vcsl-claves":
        note(0.0, 0.4, 60, 40) + note(0.5, 0.9, 60, 80) + note(1.0, 1.4, 60, 120)
        + note(1.5, 1.9, 61, 40) + note(2.0, 2.4, 61, 80) + note(2.5, 2.9, 61, 120),
}

def cc(t: float, num: int, val: int, ch: int = 0) -> list[tuple[int, bytes]]:
    return [(round(t * TICKS_PER_SECOND), bytes([0xB0 | ch, num, val]))]


def bend(t: float, value14: int, ch: int = 0) -> list[tuple[int, bytes]]:
    value14 = max(0, min(16383, value14))
    return [(round(t * TICKS_PER_SECOND),
             bytes([0xE0 | ch, value14 & 0x7F, (value14 >> 7) & 0x7F]))]


def prog(t: float, program: int, bank: int = 0, ch: int = 0) -> list[tuple[int, bytes]]:
    """Bank select (MSB/LSB) + program change: needed so the FluidSynth
    oracle plays the same preset the runner lowers by bank/program (our
    engine ignores program changes — the preset is fixed at lowering)."""
    return [(round(t * TICKS_PER_SECOND), bytes([0xB0 | ch, 0, (bank >> 7) & 0x7F])),
            (round(t * TICKS_PER_SECOND), bytes([0xB0 | ch, 32, bank & 0x7F])),
            (round(t * TICKS_PER_SECOND), bytes([0xC0 | ch, program & 0x7F]))]


def note_ch(t_on: float, t_off: float, key: int, vel: int,
            ch: int) -> list[tuple[int, bytes]]:
    return [
        (round(t_on * TICKS_PER_SECOND), bytes([0x90 | ch, key, vel])),
        (round(t_off * TICKS_PER_SECOND), bytes([0x80 | ch, key, 0])),
    ]


# Story 2.5 soundfont sequences (4 s renders, 192000 frames at 48 kHz).
# Percussion rides channel 9 so FluidSynth maps it to bank 128 natively.
SF_SEQUENCES = {
    # Melodic phrase across key/velocity range + sustain pedal.
    "sf2-generaluser-piano":
        prog(0.0, 0)
        + note(0.0, 0.4, 48, 60) + note(0.4, 0.8, 60, 90) + note(0.8, 1.2, 72, 120)
        + cc(1.25, 64, 127)  # pedal down
        + note(1.3, 1.5, 64, 100) + note(1.6, 1.8, 67, 80) + note(1.9, 2.1, 71, 110)
        + cc(2.6, 64, 0)     # pedal up
        + note(2.8, 3.4, 55, 70),
    # Sustained chord + mod-wheel sweep + pitch-bend passage (2.2 matrix).
    "sf2-generaluser-strings":
        prog(0.0, 48)
        + note(0.0, 3.2, 55, 90) + note(0.0, 3.2, 62, 90) + note(0.0, 3.2, 67, 90)
        + [e for i in range(9) for e in cc(0.8 + i * 0.15, 1, i * 127 // 8)]
        + [e for i in range(9) for e in bend(2.4 + i * 0.08, 8192 + i * 4095 // 8)]
        + bend(3.3, 8192),
    # Percussion pattern, bank 128 (channel 9).
    "sf2-generaluser-percussion":
        [e for beat in range(6) for e in
         note_ch(beat * 0.5, beat * 0.5 + 0.2, 36, 110, 9)]        # kick
        + [e for beat in range(6) for e in
           note_ch(beat * 0.5 + 0.25, beat * 0.5 + 0.4, 42, 80, 9)]  # closed hat
        + note_ch(1.0, 1.3, 38, 120, 9) + note_ch(2.0, 2.3, 38, 100, 9)  # snare
        + note_ch(3.0, 3.6, 49, 110, 9),                                  # crash
    # SF3 piano phrase (MuseScore_General).
    "sf3-musescore-piano":
        prog(0.0, 0)
        + note(0.0, 0.5, 50, 70) + note(0.5, 1.0, 62, 100) + note(1.0, 1.6, 74, 127)
        + note(1.8, 2.6, 57, 90) + note(2.6, 3.4, 69, 60),
    # SF3 flute: melody + bend (MuseScore_General program 73).
    "sf3-musescore-flute":
        prog(0.0, 73)
        + note(0.0, 0.8, 72, 80) + note(0.9, 1.7, 76, 100)
        + note(1.8, 3.3, 79, 110)
        + [e for i in range(9) for e in bend(2.2 + i * 0.08, 8192 + i * 4095 // 8)]
        + bend(3.4, 8192),
}

# Self-test fixture for the seed instrument (tests/render/fixtures/seed):
# mirrors the Story-1.4 seed timeline shape in real MIDI bytes.
SEED_SEQUENCE = note(0.0, 0.5, 60, 40) + note(1.0, 1.5, 60, 110) + note(2.0, 4.0, 36, 100)


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    midi_dir = os.path.join(root, "midi")
    os.makedirs(midi_dir, exist_ok=True)
    for entry_id, events in {**SEQUENCES, **SF_SEQUENCES}.items():
        path = os.path.join(midi_dir, entry_id + ".mid")
        with open(path, "wb") as f:
            f.write(smf_type0(events))
        print("wrote", path)
    seed_path = os.path.join(os.path.dirname(root), "tests", "render", "fixtures",
                             "seed", "seed_corpus_test.mid")
    with open(seed_path, "wb") as f:
        f.write(smf_type0(SEED_SEQUENCE))
    print("wrote", seed_path)


if __name__ == "__main__":
    main()
