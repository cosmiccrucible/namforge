#!/usr/bin/env python3
"""Analyze paired NAM captures and recommend a global integer latency trim."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

import numpy as np

# Allow running from the repo root without installing the package.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from nam_pipeline.alignment import pair_alignment, recommend_delay
from nam_pipeline.audio import read_wav, scan_qc, wav_info


def discover_pairs(audio_dir: Path) -> list[tuple[str, Path, Path]]:
    pairs: list[tuple[str, Path, Path]] = []
    for inp in sorted(audio_dir.glob("*_In.wav")):
        name = inp.name[: -len("_In.wav")]
        out = audio_dir / f"{name}_Out.wav"
        if out.exists():
            pairs.append((name, inp, out))
    return pairs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio-dir", type=Path, default=Path("../exampleaudio"))
    ap.add_argument("--out-dir", type=Path, default=Path("docs"))
    ap.add_argument("--max-delay", type=int, default=40)
    ap.add_argument("--taps", type=int, default=96)
    ap.add_argument("--window-s", type=float, default=2.0)
    ap.add_argument("--read-s", type=float, default=8.0)
    ap.add_argument("--full-qc", action="store_true", help="scan every file end-to-end")
    args = ap.parse_args()

    pairs = discover_pairs(args.audio_dir)
    if not pairs:
        print(f"no *_In/_Out pairs found in {args.audio_dir}", file=sys.stderr)
        return 1

    print(f"found {len(pairs)} pairs")
    results = []
    qc_rows = []
    for i, (name, inp, out) in enumerate(pairs, 1):
        info = wav_info(inp)
        frames = int(args.read_s * info.sample_rate)
        x, xi = read_wav(inp, frames=frames, mono=True)
        y, yi = read_wav(out, frames=frames, mono=True)
        if xi.sample_rate != yi.sample_rate:
            print(f"[{i:02d}] {name}: sample-rate mismatch {xi.sample_rate} != {yi.sample_rate}")
        if len(x) != len(y):
            print(f"[{i:02d}] {name}: length mismatch {len(x)} != {len(y)}")
        r = pair_alignment(
            name,
            x,
            y,
            xi.sample_rate,
            max_delay=args.max_delay,
            taps=args.taps,
            window_s=args.window_s,
        )
        results.append(r)
        status = (
            f"onset={r.onset_delay} env={r.envelope_delay} "
            f"fir={r.fir_delay} err={r.fir_rel_error:.4f}"
            if r.fir_rel_error is not None
            else f"onset={r.onset_delay} env={r.envelope_delay} fir=n/a"
        )
        print(f"[{i:02d}/{len(pairs)}] {name}: {status}")
        if args.full_qc:
            qi = scan_qc(inp)
            qo = scan_qc(out)
            qc_rows.append(
                {
                    "name": name,
                    "in_sample_rate": qi["sample_rate"],
                    "out_sample_rate": qo["sample_rate"],
                    "in_channels": qi["channels"],
                    "out_channels": qo["channels"],
                    "in_frames": qi["frames"],
                    "out_frames": qo["frames"],
                    "in_duration": qi["duration"],
                    "out_duration": qo["duration"],
                    "in_peak": qi["peak"],
                    "out_peak": qo["peak"],
                    "in_rms": qi["rms"],
                    "out_rms": qo["rms"],
                    "in_dc": qi["dc"],
                    "out_dc": qo["dc"],
                    "in_clips": qi["clips"],
                    "out_clips": qo["clips"],
                    "in_finite": qi["finite"],
                    "out_finite": qo["finite"],
                }
            )

    trim, evidence = recommend_delay(results, max_delay=args.max_delay)
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    payload = {
        "audio_dir": str(args.audio_dir.resolve()),
        "pair_count": len(results),
        "estimated_global_trim_samples": trim,
        "sample_rate": results[0].sr,
        "recommendation": evidence,
        "pairs": [
            {
                "name": r.name,
                "sample_rate": r.sr,
                "input_onset": r.input_onset,
                "output_onset": r.output_onset,
                "onset_delay": r.onset_delay,
                "envelope_delay": r.envelope_delay,
                "fir_delay": r.fir_delay,
                "fir_rel_error": r.fir_rel_error,
                "pulse_starts": r.pulse_starts,
                "pulse_delays": r.pulse_delays,
                "pulse_delay": r.pulse_delay,
                "fir_curve": {str(k): v for k, v in r.fir_curve.items()},
                "notes": r.notes,
            }
            for r in results
        ],
    }
    (out_dir / "alignment.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")

    with (out_dir / "alignment-pairs.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "name",
                "sr",
                "input_onset",
                "output_onset",
                "onset_delay",
                "envelope_delay",
                "fir_delay",
                "fir_rel_error",
                "pulse_delay",
                "pulse_delays",
            ]
        )
        for r in results:
            w.writerow(
                [
                    r.name,
                    r.sr,
                    r.input_onset,
                    r.output_onset,
                    r.onset_delay,
                    r.envelope_delay,
                    r.fir_delay,
                    r.fir_rel_error,
                    r.pulse_delay,
                    r.pulse_delays,
                ]
            )

    if qc_rows:
        with (out_dir / "dataset-qc-full.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(qc_rows[0]))
            w.writeheader()
            w.writerows(qc_rows)

    print("\nGLOBAL RECOMMENDATION")
    print(json.dumps({"trim_samples": trim, **evidence}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
