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

# Self-test fixture for the seed instrument (tests/render/fixtures/seed):
# mirrors the Story-1.4 seed timeline shape in real MIDI bytes.
SEED_SEQUENCE = note(0.0, 0.5, 60, 40) + note(1.0, 1.5, 60, 110) + note(2.0, 4.0, 36, 100)


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    midi_dir = os.path.join(root, "midi")
    os.makedirs(midi_dir, exist_ok=True)
    for entry_id, events in SEQUENCES.items():
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
