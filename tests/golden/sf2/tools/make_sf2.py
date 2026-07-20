#!/usr/bin/env python3
"""Deterministic mini-SF2 fixture generator (Story 2.1).

Hand-builds the tiny .sf2 files under tests/golden/sf2/ (golden lowering
fixtures) and tests/fuzz/sf2/corpus/ (fuzz seeds: the valid minis plus
truncations and garbage). Checked in like corpus/tools/make_midi.py; rerun it
only when a fixture needs to change, then regenerate the golden snapshots
(FBSAMPLER_UPDATE_GOLDENS=1).

Usage: python tests/golden/sf2/tools/make_sf2.py
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN_DIR = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(os.path.dirname(GOLDEN_DIR)))
FUZZ_CORPUS = os.path.join(REPO, "tests", "fuzz", "sf2", "corpus")

# Generator IDs (SoundFont 2.04 section 8.1.2)
GEN = {
    "startAddrsOffset": 0,
    "startloopAddrsOffset": 2,
    "endloopAddrsOffset": 3,
    "initialFilterFc": 8,
    "pan": 17,
    "delayVolEnv": 33,
    "attackVolEnv": 34,
    "holdVolEnv": 35,
    "decayVolEnv": 36,
    "sustainVolEnv": 37,
    "releaseVolEnv": 38,
    "instrument": 41,
    "keyRange": 43,
    "velRange": 44,
    "initialAttenuation": 48,
    "coarseTune": 51,
    "fineTune": 52,
    "sampleID": 53,
    "sampleModes": 54,
    "overridingRootKey": 58,
}

# Generators that must terminate a zone's list; keyRange/velRange must lead.
_LEADING = ("keyRange", "velRange")
_TERMINAL = ("instrument", "sampleID")


def _chunk(tag: bytes, payload: bytes) -> bytes:
    assert len(tag) == 4
    data = struct.pack("<4sI", tag, len(payload)) + payload
    if len(payload) & 1:
        data += b"\x00"  # RIFF word alignment
    return data


def _list(kind: bytes, payload: bytes) -> bytes:
    return _chunk(b"LIST", kind + payload)


def _name20(name: str) -> bytes:
    raw = name.encode("ascii")[:19]
    return raw + b"\x00" * (20 - len(raw))


def _range(lo: int, hi: int) -> int:
    return (lo & 0xFF) | ((hi & 0xFF) << 8)


def _gen_bytes(gens: list[tuple[str, int]]) -> bytes:
    ordered = [g for g in gens if g[0] in _LEADING]
    ordered += [g for g in gens if g[0] not in _LEADING and g[0] not in _TERMINAL]
    ordered += [g for g in gens if g[0] in _TERMINAL]
    out = b""
    for name, amount in ordered:
        out += struct.pack("<Hh", GEN[name], amount)
    return out


def triangle_wave(frames: int, period: int, amp: int = 12000) -> list[int]:
    """Integer triangle wave: fully deterministic, no float involved."""
    out = []
    for i in range(frames):
        phase = i % period
        half = period // 2
        v = (phase * 2 * amp // half - amp) if phase < half else (
            amp - (phase - half) * 2 * amp // half)
        out.append(v)
    return out


def build_sf2(name: str, samples: list[dict], instruments: list[dict],
              presets: list[dict]) -> bytes:
    """samples: {name, data (int16 list), rate, orig_pitch, loop (rel start,end)
                 or None, type (default mono 1), link (default 0)}
       instruments: {name, zones: [{gens: [(name, amount)...],
                                    sample: index or None (global)}]}
       presets: {name, bank, program, zones: [{gens, instrument: idx or None}]}
    """
    # --- sdta: concatenated 16-bit samples, 46 guard zero points each (spec) ---
    smpl = b""
    offsets = []  # absolute (start, end) sample-point offsets
    for s in samples:
        start = len(smpl) // 2
        smpl += struct.pack("<%dh" % len(s["data"]), *s["data"])
        offsets.append((start, start + len(s["data"])))
        smpl += b"\x00" * (46 * 2)

    # --- hydra ---
    phdr = b""
    pbag = b""
    pgen = b""
    pmod = b""
    for p in presets:
        phdr += struct.pack("<20sHHHIII", _name20(p["name"]), p["program"], p["bank"],
                            len(pbag) // 4, 0, 0, 0)
        for z in p["zones"]:
            pbag += struct.pack("<HH", len(pgen) // 4, len(pmod) // 10)
            gens = list(z.get("gens", []))
            if z.get("instrument") is not None:
                gens.append(("instrument", z["instrument"]))
            pgen += _gen_bytes(gens)
            for (src, dest, amount, amtsrc, trans) in z.get("mods", []):
                pmod += struct.pack("<HHhHH", src, dest, amount, amtsrc, trans)
    # terminals
    phdr += struct.pack("<20sHHHIII", _name20("EOP"), 0, 0, len(pbag) // 4, 0, 0, 0)
    pbag += struct.pack("<HH", len(pgen) // 4, len(pmod) // 10)
    pgen += struct.pack("<Hh", 0, 0)
    pmod += b"\x00" * 10

    inst = b""
    ibag = b""
    igen = b""
    imod = b""
    for ins in instruments:
        inst += struct.pack("<20sH", _name20(ins["name"]), len(ibag) // 4)
        for z in ins["zones"]:
            ibag += struct.pack("<HH", len(igen) // 4, len(imod) // 10)
            gens = list(z.get("gens", []))
            if z.get("sample") is not None:
                gens.append(("sampleID", z["sample"]))
            igen += _gen_bytes(gens)
            for (src, dest, amount, amtsrc, trans) in z.get("mods", []):
                imod += struct.pack("<HHhHH", src, dest, amount, amtsrc, trans)
    inst += struct.pack("<20sH", _name20("EOI"), len(ibag) // 4)
    ibag += struct.pack("<HH", len(igen) // 4, len(imod) // 10)
    igen += struct.pack("<Hh", 0, 0)
    imod += b"\x00" * 10

    shdr = b""
    for i, s in enumerate(samples):
        start, end = offsets[i]
        loop = s.get("loop")
        loop_start = start + (loop[0] if loop else 0)
        loop_end = start + (loop[1] if loop else 0)
        shdr += struct.pack(
            "<20sIIIIIBbHH", _name20(s["name"]), start, end, loop_start, loop_end,
            s["rate"], s.get("orig_pitch", 60), s.get("pitch_corr", 0),
            s.get("link", 0), s.get("type", 1))
    shdr += struct.pack("<20sIIIIIBbHH", _name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)

    info = (_chunk(b"ifil", struct.pack("<HH", 2, 4))
            + _chunk(b"isng", b"EMU8000\x00")
            + _chunk(b"INAM", _pad_z(name)))
    pdta = (_chunk(b"phdr", phdr) + _chunk(b"pbag", pbag) + _chunk(b"pmod", pmod)
            + _chunk(b"pgen", pgen) + _chunk(b"inst", inst) + _chunk(b"ibag", ibag)
            + _chunk(b"imod", imod) + _chunk(b"igen", igen) + _chunk(b"shdr", shdr))

    body = (b"sfbk" + _list(b"INFO", info) + _list(b"sdta", _chunk(b"smpl", smpl))
            + _list(b"pdta", pdta))
    return struct.pack("<4sI", b"RIFF", len(body)) + body


def _pad_z(s: str) -> bytes:
    raw = s.encode("ascii")
    raw += b"\x00"
    if len(raw) & 1:
        raw += b"\x00"
    return raw


def encode_vorbis(data: list[int], rate: int, ffmpeg: str) -> bytes:
    """Encode one mono int16 sample to an Ogg Vorbis stream via ffmpeg
    (regeneration-time dependency only; the encoded bytes are checked in).
    Pinned quality -q:a 6 (~192 kbps VBR) keeps the render-identity diff
    tight for the tiny fixtures."""
    wav = (b"RIFF" + (36 + len(data) * 2).to_bytes(4, "little") + b"WAVEfmt "
           + (16).to_bytes(4, "little") + (1).to_bytes(2, "little")
           + (1).to_bytes(2, "little") + rate.to_bytes(4, "little")
           + (rate * 2).to_bytes(4, "little") + (2).to_bytes(2, "little")
           + (16).to_bytes(2, "little") + b"data"
           + (len(data) * 2).to_bytes(4, "little")
           + struct.pack("<%dh" % len(data), *data))
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "in.wav")
        dst = os.path.join(tmp, "out.ogg")
        with open(src, "wb") as f:
            f.write(wav)
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", src,
                        "-c:a", "libvorbis", "-q:a", "6", dst], check=True)
        with open(dst, "rb") as f:
            return f.read()


def build_sf3(name: str, samples: list[dict], instruments: list[dict],
              presets: list[dict], ffmpeg: str) -> bytes:
    """SF3 sibling of build_sf2 (de-facto spec: FluidSynth wiki
    SoundFont3Format / sf3convert): ifil 3.1, sampleType |= 0x10, shdr
    start/end are BYTE offsets of the Ogg stream in sdta, and
    startloop/endloop are frame offsets relative to the decoded sample."""
    smpl = b""
    byte_ranges = []
    for s in samples:
        ogg = encode_vorbis(s["data"], s["rate"], ffmpeg)
        start = len(smpl)
        smpl += ogg
        byte_ranges.append((start, start + len(ogg)))

    phdr = b""
    pbag = b""
    pgen = b""
    pmod = b""
    for p in presets:
        phdr += struct.pack("<20sHHHIII", _name20(p["name"]), p["program"], p["bank"],
                            len(pbag) // 4, 0, 0, 0)
        for z in p["zones"]:
            pbag += struct.pack("<HH", len(pgen) // 4, len(pmod) // 10)
            gens = list(z.get("gens", []))
            if z.get("instrument") is not None:
                gens.append(("instrument", z["instrument"]))
            pgen += _gen_bytes(gens)
            for (src, dest, amount, amtsrc, trans) in z.get("mods", []):
                pmod += struct.pack("<HHhHH", src, dest, amount, amtsrc, trans)
    phdr += struct.pack("<20sHHHIII", _name20("EOP"), 0, 0, len(pbag) // 4, 0, 0, 0)
    pbag += struct.pack("<HH", len(pgen) // 4, len(pmod) // 10)
    pgen += struct.pack("<Hh", 0, 0)
    pmod += b"\x00" * 10

    inst = b""
    ibag = b""
    igen = b""
    imod = b""
    for ins in instruments:
        inst += struct.pack("<20sH", _name20(ins["name"]), len(ibag) // 4)
        for z in ins["zones"]:
            ibag += struct.pack("<HH", len(igen) // 4, len(imod) // 10)
            gens = list(z.get("gens", []))
            if z.get("sample") is not None:
                gens.append(("sampleID", z["sample"]))
            igen += _gen_bytes(gens)
            for (src, dest, amount, amtsrc, trans) in z.get("mods", []):
                imod += struct.pack("<HHhHH", src, dest, amount, amtsrc, trans)
    inst += struct.pack("<20sH", _name20("EOI"), len(ibag) // 4)
    ibag += struct.pack("<HH", len(igen) // 4, len(imod) // 10)
    igen += struct.pack("<Hh", 0, 0)
    imod += b"\x00" * 10

    shdr = b""
    for i, s in enumerate(samples):
        start, end = byte_ranges[i]
        loop = s.get("loop")
        # SF3: loop points relative to the DECODED sample start.
        loop_start = loop[0] if loop else 0
        loop_end = loop[1] if loop else 0
        shdr += struct.pack(
            "<20sIIIIIBbHH", _name20(s["name"]), start, end, loop_start, loop_end,
            s["rate"], s.get("orig_pitch", 60), s.get("pitch_corr", 0),
            s.get("link", 0), s.get("type", 1) | 0x10)
    shdr += struct.pack("<20sIIIIIBbHH", _name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)

    info = (_chunk(b"ifil", struct.pack("<HH", 3, 1))
            + _chunk(b"isng", b"EMU8000\x00")
            + _chunk(b"INAM", _pad_z(name)))
    pdta = (_chunk(b"phdr", phdr) + _chunk(b"pbag", pbag) + _chunk(b"pmod", pmod)
            + _chunk(b"pgen", pgen) + _chunk(b"inst", inst) + _chunk(b"ibag", ibag)
            + _chunk(b"imod", imod) + _chunk(b"igen", igen) + _chunk(b"shdr", shdr))

    body = (b"sfbk" + _list(b"INFO", info) + _list(b"sdta", _chunk(b"smpl", smpl))
            + _list(b"pdta", pdta))
    return struct.pack("<4sI", b"RIFF", len(body)) + body


def write(path: str, blob: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(blob)
    print(f"wrote {os.path.relpath(path, REPO)} ({len(blob)} bytes)")


def main() -> None:
    wave = triangle_wave(256, 64)

    # basic: one preset -> one instrument -> one looped zone with envelope.
    basic = build_sf2(
        "basic",
        samples=[{"name": "tri", "data": wave, "rate": 22050, "orig_pitch": 60,
                  "loop": (64, 192)}],
        instruments=[{"name": "TriInst", "zones": [{
            "gens": [
                ("sampleModes", 1),
                ("attackVolEnv", -7973),   # ~0.01 s
                ("holdVolEnv", -12000),
                ("decayVolEnv", -3986),    # ~0.1 s
                ("sustainVolEnv", 200),    # 20 cB attenuation
                ("releaseVolEnv", -1200),  # 0.5 s
                ("fineTune", 25),
            ],
            "sample": 0}]}],
        presets=[{"name": "Basic Preset", "bank": 0, "program": 0,
                  "zones": [{"gens": [], "instrument": 0}]}],
    )
    write(os.path.join(GOLDEN_DIR, "basic.sf2"), basic)

    # layers: velocity layers + key split, instrument global zone, preset-zone
    # additive generator offsets over instrument values.
    soft = triangle_wave(200, 50, amp=6000)
    hard = triangle_wave(200, 50, amp=15000)
    layers = build_sf2(
        "layers",
        samples=[
            {"name": "soft", "data": soft, "rate": 32000, "orig_pitch": 57},
            {"name": "hard", "data": hard, "rate": 32000, "orig_pitch": 57},
        ],
        instruments=[{"name": "Layered", "zones": [
            # global zone: shared attenuation + release
            {"gens": [("initialAttenuation", 50), ("releaseVolEnv", -2400)],
             "sample": None},
            {"gens": [("keyRange", _range(36, 59)), ("velRange", _range(0, 63))],
             "sample": 0},
            {"gens": [("keyRange", _range(36, 59)), ("velRange", _range(64, 127))],
             "sample": 1},
            {"gens": [("keyRange", _range(60, 84)), ("overridingRootKey", 72)],
             "sample": 1},
        ]}],
        presets=[{"name": "Layered Preset", "bank": 0, "program": 1, "zones": [
            # preset global zone: pan offset applied to every zone
            {"gens": [("pan", 100)], "instrument": None},
            # preset zone narrows the key range and adds attenuation on top of
            # the instrument's values (additive preset offsets, spec 9.4)
            {"gens": [("keyRange", _range(40, 80)), ("initialAttenuation", 25),
                      ("coarseTune", 2)],
             "instrument": 0},
        ]}],
    )
    write(os.path.join(GOLDEN_DIR, "layers.sf2"), layers)

    # stereo: linked left/right mono pair -> two hard-panned regions.
    left = triangle_wave(128, 32, amp=10000)
    right = triangle_wave(128, 40, amp=10000)
    stereo = build_sf2(
        "stereo",
        samples=[
            {"name": "pianoL", "data": left, "rate": 44100, "orig_pitch": 64,
             "type": 4, "link": 1},
            {"name": "pianoR", "data": right, "rate": 44100, "orig_pitch": 64,
             "type": 2, "link": 0},
        ],
        instruments=[{"name": "StereoInst", "zones": [
            {"gens": [("keyRange", _range(0, 127))], "sample": 0},
            {"gens": [("keyRange", _range(0, 127))], "sample": 1},
        ]}],
        presets=[{"name": "Stereo Preset", "bank": 0, "program": 2,
                  "zones": [{"gens": [], "instrument": 0}]}],
    )
    write(os.path.join(GOLDEN_DIR, "stereo.sf2"), stereo)

    # unsupported: filter generator + modulator-free file exercising
    # sf2.generator_unsupported and loop-until-release approximation.
    unsupported = build_sf2(
        "unsupported",
        samples=[{"name": "tri", "data": wave, "rate": 22050, "orig_pitch": 60,
                  "loop": (10, 200)}],
        instruments=[{"name": "FilterInst", "zones": [{
            "gens": [("initialFilterFc", 8000), ("sampleModes", 3)],
            "sample": 0}]}],
        presets=[{"name": "Unsupported", "bank": 0, "program": 3,
                  "zones": [{"gens": [], "instrument": 0}]}],
    )
    write(os.path.join(GOLDEN_DIR, "unsupported.sf2"), unsupported)

    # mods: custom modulators — an imod superseding the default velocity ->
    # attenuation modulator (same identity, amount 480 instead of 960) and a
    # pmod adding a CC2 -> fineTune vibrato-free pitch route.
    mods = build_sf2(
        "mods",
        samples=[{"name": "tri", "data": wave, "rate": 22050, "orig_pitch": 60}],
        instruments=[{"name": "ModInst", "zones": [{
            "gens": [],
            "mods": [(0x0502, 48, 480, 0, 0)],  # vel->atten concave neg, half depth
            "sample": 0}]}],
        presets=[{"name": "Mods Preset", "bank": 0, "program": 4,
                  "zones": [{"gens": [], "instrument": 0,
                             "mods": [(0x0082, 52, 100, 0, 0)]}]}],  # CC2->fineTune
    )
    write(os.path.join(GOLDEN_DIR, "mods.sf2"), mods)

    # oracle: FluidSynth-diff fixture (Story 2.2). Long looped triangle,
    # instant attack, full sustain, short release; a custom CC1->attenuation
    # modulator both engines can execute exactly.
    osc = triangle_wave(2048, 100, amp=14000)
    oracle = build_sf2(
        "oracle",
        samples=[{"name": "osc", "data": osc, "rate": 44100, "orig_pitch": 69,
                  "loop": (100, 2100 - 100)}],
        instruments=[{"name": "OracleInst", "zones": [{
            "gens": [("sampleModes", 1), ("releaseVolEnv", -5186)],  # ~0.05 s
            "mods": [(0x0081, 48, 960, 0, 0)],  # CC1->atten, linear positive
            "sample": 0}]}],
        presets=[{"name": "Oracle", "bank": 0, "program": 0,
                  "zones": [{"gens": [], "instrument": 0}]}],
    )
    write(os.path.join(GOLDEN_DIR, "oracle.sf2"), oracle)
    write(os.path.join(REPO, "tests", "render", "fixtures", "sf2", "oracle.sf2"), oracle)

    # multibank (Story 2.4): >=3 presets across >=2 banks incl. the bank-128
    # percussion convention; two presets share the same sample (pool dedup).
    lead = triangle_wave(300, 60, amp=11000)
    perc = triangle_wave(150, 25, amp=13000)
    multibank = build_sf2(
        "multibank",
        samples=[
            {"name": "lead", "data": lead, "rate": 32000, "orig_pitch": 60},
            {"name": "perc", "data": perc, "rate": 32000, "orig_pitch": 60},
        ],
        instruments=[
            {"name": "LeadInst", "zones": [{"gens": [], "sample": 0}]},
            {"name": "SoftLeadInst", "zones": [
                {"gens": [("initialAttenuation", 100)], "sample": 0}]},
            {"name": "PercInst", "zones": [{"gens": [], "sample": 1}]},
        ],
        presets=[
            # Deliberately unsorted on disk: enumeration must sort
            # bank asc, program asc.
            {"name": "Standard Kit", "bank": 128, "program": 0,
             "zones": [{"gens": [], "instrument": 2}]},
            {"name": "Soft Lead", "bank": 0, "program": 1,
             "zones": [{"gens": [], "instrument": 1}]},
            {"name": "Bright Lead", "bank": 0, "program": 0,
             "zones": [{"gens": [], "instrument": 0}]},
        ],
    )
    write(os.path.join(GOLDEN_DIR, "multibank.sf2"), multibank)
    write(os.path.join(REPO, "tests", "render", "fixtures", "sf2", "multibank.sf2"),
          multibank)

    # --- SF3 siblings (Story 2.3): same hydra, Vorbis-compressed sdta.
    # ffmpeg (libvorbis) is a regeneration-time dependency only.
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        basic3_args = dict(
            samples=[{"name": "tri", "data": wave, "rate": 22050, "orig_pitch": 60,
                      "loop": (64, 192)}],
            instruments=[{"name": "TriInst", "zones": [{
                "gens": [
                    ("sampleModes", 1),
                    ("attackVolEnv", -7973),
                    ("holdVolEnv", -12000),
                    ("decayVolEnv", -3986),
                    ("sustainVolEnv", 200),
                    ("releaseVolEnv", -1200),
                    ("fineTune", 25),
                ],
                "sample": 0}]}],
            presets=[{"name": "Basic Preset", "bank": 0, "program": 0,
                      "zones": [{"gens": [], "instrument": 0}]}],
        )
        basic3 = build_sf3("basic", ffmpeg=ffmpeg, **basic3_args)
        write(os.path.join(GOLDEN_DIR, "basic.sf3"), basic3)

        oracle3 = build_sf3(
            "oracle", ffmpeg=ffmpeg,
            samples=[{"name": "osc", "data": osc, "rate": 44100, "orig_pitch": 69,
                      "loop": (100, 2100 - 100)}],
            instruments=[{"name": "OracleInst", "zones": [{
                "gens": [("sampleModes", 1), ("releaseVolEnv", -5186)],
                "mods": [(0x0081, 48, 960, 0, 0)],
                "sample": 0}]}],
            presets=[{"name": "Oracle", "bank": 0, "program": 0,
                      "zones": [{"gens": [], "instrument": 0}]}],
        )
        write(os.path.join(GOLDEN_DIR, "oracle.sf3"), oracle3)
        write(os.path.join(REPO, "tests", "render", "fixtures", "sf2", "oracle.sf3"),
              oracle3)

        # SF3 fuzz seeds: valid mini, Ogg truncated mid-stream, and the
        # Vorbis flag pointing at non-Vorbis bytes.
        write(os.path.join(FUZZ_CORPUS, "seed_basic.sf3"), basic3)
        write(os.path.join(FUZZ_CORPUS, "seed_sf3_truncated_ogg.sf3"),
              basic3[:len(basic3) * 3 // 5])
        lying = bytearray(build_sf2("basic", **basic3_args))
        # Flip every shdr sampleType to vorbis without vorbis data: find the
        # shdr chunk and OR 0x10 into each record's sampleType field.
        at = lying.find(b"shdr")
        if at >= 0:
            count = int.from_bytes(lying[at + 4:at + 8], "little") // 46
            for i in range(count):
                rec = at + 8 + i * 46
                lying[rec + 44] |= 0x10
        write(os.path.join(FUZZ_CORPUS, "seed_sf3_lying_flag.sf3"), bytes(lying))
    else:
        print("ffmpeg not found; SF3 fixtures NOT regenerated")

    # --- fuzz seeds: the valid minis + deterministic truncations + garbage ---
    for label, blob in (("basic", basic), ("layers", layers), ("stereo", stereo), ("mods", mods)):
        write(os.path.join(FUZZ_CORPUS, f"seed_{label}.sf2"), blob)
    write(os.path.join(FUZZ_CORPUS, "seed_truncated_header.sf2"), basic[:11])
    write(os.path.join(FUZZ_CORPUS, "seed_truncated_pdta.sf2"), basic[:len(basic) * 2 // 3])
    write(os.path.join(FUZZ_CORPUS, "seed_garbage.sf2"),
          bytes((i * 37 + 11) & 0xFF for i in range(512)))
    write(os.path.join(FUZZ_CORPUS, "seed_riff_empty.sf2"),
          struct.pack("<4sI4s", b"RIFF", 4, b"sfbk"))


if __name__ == "__main__":
    main()
