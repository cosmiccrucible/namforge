#!/usr/bin/env python3
"""Package a Standard + Eco quality pair as one NAM Forge pack (v2 format).

Text files are written with LF endings and `SHA256SUMS` is computed from those
bytes. `--provenance` takes a JSON object keyed by `standard` and `eco`; without
it the pack is still written but the models repository's verify_packs.py gate will reject it until
the provenance block is added.
"""

from __future__ import annotations

import argparse
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

from nam_to_cpp import convert as convert_nam_to_cpp
from packmeta import (
    ROOT,
    SCHEMA_VERSION,
    sha256_file,
    write_json_lf,
    write_sha256sums,
    write_text_lf,
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("standard_nam", type=Path)
    ap.add_argument("eco_nam", type=Path)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "packs")
    ap.add_argument("--delay", type=int, default=17)
    ap.add_argument(
        "--provenance",
        type=Path,
        help="JSON object keyed by 'standard' and 'eco' (see docs/pack-format.md)",
    )
    args = ap.parse_args()

    out = args.out / args.name
    out.mkdir(parents=True, exist_ok=True)
    src = out / "src"
    src.mkdir(exist_ok=True)
    shutil.copy2(args.standard_nam, out / "model_standard.nam")
    shutil.copy2(args.eco_nam, out / "model_eco.nam")
    shutil.copy2(ROOT / "cpp" / "NamA2Core.h", src / "NamA2Core.h")
    shutil.copy2(ROOT / "cpp" / "NamA2DualCore.h", src / "NamA2DualCore.h")
    convert_nam_to_cpp(out / "model_standard.nam", src / f"{args.name}Weights.h", args.name)
    convert_nam_to_cpp(
        out / "model_eco.nam", src / f"{args.name}EcoWeights.h", f"{args.name}Eco"
    )

    standard = json.loads(args.standard_nam.read_text(encoding="utf-8"))
    eco = json.loads(args.eco_nam.read_text(encoding="utf-8"))
    params = standard.get("config", {}).get("params", [])
    manifest = {
        "schema": SCHEMA_VERSION,
        "name": args.name,
        "model_type": "parametric" if params else "static",
        "quality_modes": ["standard", "eco"],
        "models": {
            "standard": {
                "nam": "model_standard.nam",
                "header": f"src/{args.name}Weights.h",
                "architecture": standard.get("architecture"),
                "nam_version": standard.get("version"),
            },
            "eco": {
                "nam": "model_eco.nam",
                "header": f"src/{args.name}EcoWeights.h",
                "architecture": eco.get("architecture"),
                "nam_version": eco.get("version"),
            },
        },
        "sample_rate": standard.get("sample_rate", 48000),
        "latency_samples": 0,
        "capture_delay_samples": args.delay,
        "axes": [
            {
                "name": p["name"],
                "min": p["min"],
                "max": p["max"],
                "default": p["default"],
                "type": p.get("type", "continuous"),
                "step": p.get("step"),
            }
            for p in params
        ],
        "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    if args.provenance is not None:
        provenance = json.loads(args.provenance.read_text(encoding="utf-8"))
        manifest["provenance"] = provenance
    else:
        print(
            "warning: no --provenance file; the models gate will reject this pack "
            "until params.json has a provenance block"
        )
    write_json_lf(out / "params.json", manifest)

    write_text_lf(
        out / "README.md",
        f"""# {args.name}

NAM Forge quality pair (Standard + Eco).

- `model_standard.nam`, `model_eco.nam`
- `src/NamA2DualCore.h` runtime switch
- `src/*Weights.h` generated weight descriptors
- `params.json` — v2 manifest with per-quality provenance

Both variants share the same axes and use a 20 ms crossfade on switch.
""",
    )
    write_text_lf(
        out / "LICENSE",
        "GPL-3.0-or-later for the NAM Forge integration code. "
        "NAM/parametric forks are MIT; retain notices.\n",
    )
    write_text_lf(
        out / "THIRD_PARTY_NOTICES.md",
        "Neural Amp Modeler and NeuralAmpModelerCore: MIT, Steven Atkinson.\n"
        "neural-amp-modeler-parametric and NeuralAmpModelerCoreParametric: MIT, "
        "Steven Atkinson and Phillip Self.\n",
    )
    sums = write_sha256sums(out)
    print(
        f"wrote {out} ({len(sums)} files, "
        f"standard {sha256_file(out / 'model_standard.nam')[:12]}, "
        f"eco {sha256_file(out / 'model_eco.nam')[:12]})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
