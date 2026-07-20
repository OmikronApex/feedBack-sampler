#!/usr/bin/env python3
"""FluidSynth reference capture for the SoundFont corpus slice (Story 2.5).

TRUE oracle captures (unlike the Epic-1 provisional self-captures): rendered
once from headless FluidSynth and checked in; CI never runs the oracle.

Pinned capture procedure (documented in corpus/README.md — FluidSynth minor
versions change rendering, pin hard):
  - FluidSynth 2.5.6
  - 48 kHz stereo, fast render (-F), no realtime
  - gain 1.0; reverb and chorus DISABLED (our engine has no reverb/chorus —
    diffing against effected output would measure the effects, not the
    sampler)
  - output trimmed/padded to the entry's exact render_frames and stored as
    PCM16 stereo (matching the Epic-1 reference format)

Usage:
  python corpus/tools/capture_sf_references.py [--fluidsynth PATH] [--only ID]

Requires the soundfonts to be present in corpus/cache/<id>/ (run
`run_corpus.py --fetch-only` first).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FLUID_ARGS = ["-ni", "-g", "1", "-r", "48000",
              "-o", "synth.gain=1",
              "-o", "synth.reverb.active=no",
              "-o", "synth.chorus.active=no"]


def capture(fluidsynth: str, entry: dict) -> None:
    sf_local = os.path.join(ROOT, "cache", entry["id"], *entry["file"].split("/"))
    midi = os.path.join(ROOT, *entry["midi"].split("/"))
    reference = os.path.join(ROOT, *entry["reference"].split("/"))
    frames = int(entry["render_frames"])

    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "out.wav")
        cmd = [fluidsynth, *FLUID_ARGS, "-F", raw, sf_local, midi]
        subprocess.run(cmd, check=True, capture_output=True)

        with wave.open(raw, "rb") as w:
            assert w.getframerate() == 48000, "unexpected FluidSynth rate"
            assert w.getnchannels() == 2
            assert w.getsampwidth() == 2
            data = w.readframes(w.getnframes())

    # Trim/pad to exactly render_frames (FluidSynth renders MIDI length +
    # tail; the diff harness requires exact shape).
    need = frames * 2 * 2
    data = data[:need] + b"\x00" * max(0, need - len(data))

    os.makedirs(os.path.dirname(reference), exist_ok=True)
    with wave.open(reference, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(data)
    print(f"captured {entry['id']} -> {os.path.relpath(reference, ROOT)}"
          f" ({frames} frames)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fluidsynth", default=shutil.which("fluidsynth"))
    ap.add_argument("--only")
    args = ap.parse_args()
    if not args.fluidsynth:
        print("fluidsynth not found")
        return 1

    with open(os.path.join(ROOT, "manifest.json")) as f:
        manifest = json.load(f)
    for entry in manifest["entries"]:
        if entry.get("format") not in ("sf2", "sf3"):
            continue
        if args.only and entry["id"] != args.only:
            continue
        capture(args.fluidsynth, entry)
    return 0


if __name__ == "__main__":
    sys.exit(main())
