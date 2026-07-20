#!/usr/bin/env python3
"""FluidSynth-oracle fixture builder (Story 2.2, NFR-5).

Generates the targeted MIDI fixtures (velocity ramp, mod-wheel sweep,
pitch-bend sweep) and captures the FluidSynth reference renders for
tests/render/sf2_oracle_test.cpp. References are captured ONCE and checked in
(the uniform checked-in-capture pattern from Story 1.6) — CI never runs
FluidSynth.

Capture procedure (documented in corpus/README.md):
  - FluidSynth 2.5.6 (scoop package, Windows x64 cpp11 build)
  - fluidsynth -ni -g 1 -r 44100 -o synth.gain=1 -F <out.wav> oracle.sf2 <mid>
  - fast-render mode (-F), no realtime; chorus/reverb left at defaults —
    oracle.sf2 routes nothing to effect sends, so they are silent passthrough.

Usage: python tests/render/fixtures/sf2/make_oracle_refs.py [--fluidsynth PATH]
Regenerating invalidates the checked-in references; inspect diffs deliberately.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))

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
    events = sorted(events, key=lambda e: e[0])
    track = b""
    last = 0
    for tick, data in events:
        track += varint(tick - last) + data
        last = tick
    track += b"\x00\xff\x2f\x00"
    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") \
        + (1).to_bytes(2, "big") + TICKS_PER_QUARTER.to_bytes(2, "big")
    return header + b"MTrk" + len(track).to_bytes(4, "big") + track


def t(seconds: float) -> int:
    return round(seconds * TICKS_PER_SECOND)


def note(t_on: float, t_off: float, key: int, vel: int) -> list[tuple[int, bytes]]:
    return [(t(t_on), bytes([0x90, key, vel])), (t(t_off), bytes([0x80, key, 0]))]


def cc(at: float, num: int, value: int) -> list[tuple[int, bytes]]:
    return [(t(at), bytes([0xB0, num, value]))]


def bend(at: float, value14: int) -> list[tuple[int, bytes]]:
    value14 = max(0, min(16383, value14))
    return [(t(at), bytes([0xE0, value14 & 0x7F, (value14 >> 7) & 0x7F]))]


# Velocity ramp: same note, 8 velocity steps. Windows land at 0.5 s spacing so
# the test can slice per-note RMS cleanly.
VEL_STEPS = [16, 32, 48, 64, 80, 96, 112, 127]
vel_ramp = []
for i, v in enumerate(VEL_STEPS):
    vel_ramp += note(i * 0.5, i * 0.5 + 0.4, 69, v)

# Mod-wheel sweep during a sustained note. oracle.sf2 carries a custom
# CC1 -> initialAttenuation modulator both engines execute exactly.
modwheel = note(0.0, 3.5, 69, 100)
for i in range(17):
    modwheel += cc(0.5 + i * (2.5 / 16.0), 1, round(i * 127 / 16))

# Pitch-bend sweep at the default ±2-semitone range. (RPN-changed bend range
# is a tracked engine gap — sfizz 1.2.3 has no RPN support; recorded in
# deferred-work.md — so the fixture stays inside the default range.)
bend_sweep = note(0.0, 3.5, 69, 100)
for i in range(17):
    bend_sweep += bend(0.5 + i * (1.0 / 16.0), 8192 + round(i * 4095 / 16))  # up 1 st
bend_sweep += bend(1.75, 8192)
for i in range(17):
    bend_sweep += bend(2.0 + i * (1.0 / 16.0), 8192 - round(i * 8192 / 16))  # down 2 st

SEQUENCES = {
    "vel_ramp": vel_ramp,
    "modwheel": modwheel,
    "bend": bend_sweep,
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fluidsynth", default=shutil.which("fluidsynth"))
    args = ap.parse_args()

    for name, events in SEQUENCES.items():
        path = os.path.join(HERE, name + ".mid")
        with open(path, "wb") as f:
            f.write(smf_type0(events))
        print("wrote", path)

    if not args.fluidsynth:
        print("fluidsynth not found; skipped reference capture")
        return 1

    sf2 = os.path.join(HERE, "oracle.sf2")
    for name in SEQUENCES:
        out = os.path.join(HERE, name + "_fluid.wav")
        cmd = [args.fluidsynth, "-ni", "-g", "1", "-r", "44100",
               "-o", "synth.gain=1", "-F", out, sf2,
               os.path.join(HERE, name + ".mid")]
        subprocess.run(cmd, check=True, capture_output=True)
        print("captured", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
