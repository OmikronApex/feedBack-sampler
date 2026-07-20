#!/usr/bin/env python3
"""Corpus runner (Story 1.6): fetch pinned assets, render every manifest
entry through fbsampler-corpus-render, diff against reference captures, and
emit the per-library report (machine JSON + human summary).

Usage:
  python corpus/tools/run_corpus.py --tool build/tests/Release/fbsampler-corpus-render.exe
      [--report corpus-report.json] [--update-references] [--update-goldens]
      [--fetch-only]

Exit codes: 0 all entries pass, 1 any entry fails, 2 infrastructure error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "cache")


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch_entry(entry: dict) -> str:
    """Download (or validate cached) files for one entry. Returns the local
    path of the entry's .sfz. Raises on checksum mismatch: a pinned commit
    whose content changed is an integrity failure, never a soft warning."""
    base = entry["source"]["raw_base"]
    entry_dir = os.path.join(CACHE, entry["id"])
    # Schema v2: soundfont entries name their container via "file"; sfz
    # entries keep the historical "sfz" key.
    root_file = entry.get("file") or entry["sfz"]
    sfz_local = None
    for f in entry["files"]:
        local = os.path.join(entry_dir, *f["path"].split("/"))
        if f["path"] == root_file:
            sfz_local = local
        if os.path.exists(local) and sha256_file(local) == f["sha256"]:
            continue
        url = base + urllib.parse.quote(f["path"])
        os.makedirs(os.path.dirname(local), exist_ok=True)
        print(f"  fetch {f['path']}")
        with urllib.request.urlopen(url) as r:
            blob = r.read()
        digest = hashlib.sha256(blob).hexdigest()
        if digest != f["sha256"]:
            raise RuntimeError(
                f"checksum mismatch for {f['path']}: expected {f['sha256']}, got {digest}")
        with open(local, "wb") as out:
            out.write(blob)
    if sfz_local is None:
        raise RuntimeError(f"entry {entry['id']}: root file not listed in files[]")
    return sfz_local


def run_entry(tool: str, entry: dict, sfz_local: str, thresholds: dict,
              update_references: bool, update_goldens: bool) -> dict:
    midi = os.path.join(ROOT, *entry["midi"].split("/"))
    reference = os.path.join(ROOT, *entry["reference"].split("/"))
    golden = os.path.join(ROOT, *entry["golden"].split("/"))
    json_out = os.path.join(CACHE, entry["id"] + ".result.json")

    t = dict(thresholds)
    t.update(entry.get("thresholds", {}))
    # Schema v2: structured per-entry override with mandatory rationale
    # (resolves the 1.6 review deferral).
    override = dict(entry.get("threshold_override", {}))
    override.pop("rationale", None)
    t.update(override)
    cmd = [tool, "--sfz", sfz_local, "--midi", midi,
           "--frames", str(entry["render_frames"]),
           "--format", entry.get("format", "sfz"),
           "--bank", str(entry.get("bank", 0)),
           "--program", str(entry.get("program", 0)),
           "--peak", str(t["peak"]), "--rms", str(t["rms"]),
           "--window-rms", str(t["window_rms"]),
           "--json", json_out]
    if update_references:
        os.makedirs(os.path.dirname(reference), exist_ok=True)
        cmd += ["--write-wav", reference]
    else:
        cmd += ["--reference", reference]
    if update_goldens:
        os.makedirs(os.path.dirname(golden), exist_ok=True)
        cmd += ["--golden", golden]
    else:
        # Dump the golden next to the result and byte-compare below, so
        # lowering drift is attributed separately from rendering drift.
        cmd += ["--golden", json_out + ".golden.txt"]

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if not os.path.exists(json_out):
        return {"id": entry["id"], "loaded": False, "rendered": False, "passed": False,
                "error": f"tool produced no result (exit {proc.returncode}): "
                         + proc.stderr.strip()[:500]}
    with open(json_out) as f:
        result = json.load(f)
    result["id"] = entry["id"]
    result["format"] = entry["format"]

    if not update_goldens and result.get("loaded"):
        try:
            with open(golden, "rb") as f:
                expected = f.read()
            with open(json_out + ".golden.txt", "rb") as f:
                actual = f.read()
            result["golden_matches"] = actual == expected
        except OSError as e:
            result["golden_matches"] = False
            result["error"] = result.get("error") or f"golden compare failed: {e}"
        if not result["golden_matches"]:
            result["passed"] = False
            result["error"] = result.get("error") or "lowering golden snapshot drifted"
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", help="path to fbsampler-corpus-render")
    ap.add_argument("--report", default=os.path.join(CACHE, "corpus-report.json"))
    ap.add_argument("--update-references", action="store_true")
    ap.add_argument("--update-goldens", action="store_true")
    ap.add_argument("--fetch-only", action="store_true")
    args = ap.parse_args()
    if args.tool:
        args.tool = os.path.abspath(args.tool)

    def write_infra_error_report(message: str) -> None:
        # An infra failure still gets a report artifact (schema_version +
        # error), so "if: always()" uploads in ci.yml always have something
        # to find instead of silently ignoring a missing file.
        report = {"schema_version": 1, "entries": [], "formats": {}, "mode": "check",
                  "error": message}
        try:
            os.makedirs(os.path.dirname(os.path.abspath(args.report)), exist_ok=True)
            with open(args.report, "w") as f:
                json.dump(report, f, indent=2)
        except OSError:
            pass  # best-effort; the console message above is authoritative

    try:
        with open(os.path.join(ROOT, "manifest.json")) as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        message = f"cannot read manifest.json: {e}"
        print(f"corpus infrastructure error: {message}")
        write_infra_error_report(message)
        return 2
    if manifest.get("schema_version") not in (1, 2):
        message = f"unsupported manifest schema_version {manifest.get('schema_version')}"
        print(message)
        write_infra_error_report(message)
        return 2

    os.makedirs(CACHE, exist_ok=True)
    results = []
    try:
        for entry in manifest["entries"]:
            print(f"[{entry['id']}]")
            sfz_local = fetch_entry(entry)
            if args.fetch_only:
                continue
            if not args.tool:
                print("--tool is required unless --fetch-only")
                write_infra_error_report("--tool is required unless --fetch-only")
                return 2
            results.append(run_entry(args.tool, entry, sfz_local,
                                     manifest["default_thresholds"],
                                     args.update_references, args.update_goldens))
    except (RuntimeError, urllib.error.URLError, OSError, json.JSONDecodeError) as e:
        print(f"corpus infrastructure error: {e}")
        write_infra_error_report(str(e))
        return 2
    if args.fetch_only:
        print("fetch complete")
        return 0

    # Per-format rollup: the PRD v1 gate (§6) reads straight off these two
    # percentages -- 100% loads, >=90% renders correctly.
    formats = {}
    for r in results:
        f = formats.setdefault(r["format"], {"total": 0, "loaded": 0, "passed": 0})
        f["total"] += 1
        f["loaded"] += 1 if r.get("loaded") else 0
        f["passed"] += 1 if r.get("passed") else 0
    for f in formats.values():
        f["load_pct"] = round(100.0 * f["loaded"] / f["total"], 1)
        f["render_pass_pct"] = round(100.0 * f["passed"] / f["total"], 1)

    report = {"schema_version": 1, "entries": results, "formats": formats,
              "mode": ("update-references" if args.update_references else "check")}
    os.makedirs(os.path.dirname(os.path.abspath(args.report)), exist_ok=True)
    with open(args.report, "w") as f:
        json.dump(report, f, indent=2)

    print(f"\n{'entry':44} {'load':>5} {'render':>7} {'peak':>10} {'rms':>10} result")
    failed = False
    expected_fail = {e["id"]: e["expected_fail"] for e in manifest["entries"]
                     if e.get("expected_fail")}
    for r in results:
        ok = r.get("passed", False)
        if not ok and r["id"] in expected_fail:
            # Tracked, diagnosed gap (AD-1): reported, but only the per-format
            # PRD gate below decides the job.
            r["expected_fail"] = expected_fail[r["id"]]
        else:
            failed |= not ok
        print(f"{r['id']:44} {str(r.get('loaded', False)):>5} "
              f"{str(r.get('rendered', False)):>7} {r.get('peak_diff', 0):>10.3g} "
              f"{r.get('rms_diff', 0):>10.3g} {'PASS' if ok else 'FAIL'}"
              + (f"  ({r.get('error', '')})" if not ok else ""))
    # PRD section-6 v1 gate, asserted per format (Story 2.5): 100% of the
    # corpus loads; >=90% renders within thresholds. Percentages are
    # deliberately UNWEIGHTED (1.6 review deferral resolved: at this corpus
    # scale a weight field would have no consumer; revisit when a format
    # slice grows past ~10 entries).
    for name, f in formats.items():
        gate_ok = f["load_pct"] == 100.0 and f["render_pass_pct"] >= 90.0
        print(f"format {name}: {f['load_pct']}% load, {f['render_pass_pct']}% render-pass "
              f"({f['passed']}/{f['total']})"
              + ("" if gate_ok else "  << PRD gate FAILED (need 100% load, >=90% render)"))
        failed |= not gate_ok
    print(f"report: {args.report}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
