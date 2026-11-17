#!/usr/bin/env python3
"""Package a trained .nam as a NAM Forge model pack (v2 format).

Text files are written with LF endings and `SHA256SUMS` is computed from those
bytes, so a checksum recorded on Windows also verifies on a Linux/macOS
checkout. `--provenance` takes a JSON object keyed by quality; without it the
pack is still written but the models repository's verify_packs.py gate will reject it until the
provenance block is added.
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
    ap.add_argument("nam", type=Path)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "packs")
    ap.add_argument("--quality", choices=["standard", "eco"], default="standard")
    ap.add_argument("--delay", type=int, default=17)
    ap.add_argument(
        "--provenance",
        type=Path,
        help="JSON object keyed by quality (see docs/pack-format.md)",
    )
    args = ap.parse_args()

    data = json.loads(args.nam.read_text(encoding="utf-8"))
    pack = args.out / args.name
    pack.mkdir(parents=True, exist_ok=True)
    target_nam = pack / f"model_{args.quality}.nam"
    shutil.copy2(args.nam, target_nam)

    header_name = f"{args.name}Weights.h"
    header_dir = pack / "src"
    header_dir.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "cpp" / "NamA2Core.h", header_dir / "NamA2Core.h")
    convert_nam_to_cpp(target_nam, header_dir / header_name, args.name)

    cfg = data.get("config", {})
    params = cfg.get("params", []) if data.get("architecture") == "ConcatWaveNet" else []
    axes = [
        {
            "name": p["name"],
            "min": p["min"],
            "max": p["max"],
            "default": p["default"],
            "type": p.get("type", "continuous"),
            "step": p.get("step"),
        }
        for p in params
    ]
    manifest = {
        "schema": SCHEMA_VERSION,
        "name": args.name,
        "model_type": "parametric" if axes else "static",
        "quality_modes": [args.quality],
        "models": {
            args.quality: {
                "nam": target_nam.name,
                "header": f"src/{header_name}",
                "architecture": data.get("architecture"),
                "nam_version": data.get("version"),
            }
        },
        "sample_rate": data.get("sample_rate", 48000),
        "latency_samples": 0,
        "capture_delay_samples": args.delay,
        "axes": axes,
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
    write_json_lf(pack / "params.json", manifest)

    write_text_lf(
        pack / "README.md",
        f"""# {args.name}

NAM Forge model pack ({manifest['model_type']}, {args.quality}).

- `{target_nam.name}` — NAM model
- `src/{header_name}` — generated C++ weights/descriptor
- `src/NamA2Core.h` — framework-free inference core
- `params.json` — v2 manifest: models, axes, latency and provenance

Latency: {manifest['latency_samples']} samples at the model rate.
Capture trim: {args.delay} samples.
""",
    )
    write_text_lf(
        pack / "LICENSE",
        "GPL-3.0-or-later for the NAM Forge integration code.\n"
        "The NAM model format and upstream NAM code are MIT; retain their notices.\n",
    )
    write_text_lf(
        pack / "THIRD_PARTY_NOTICES.md",
        "# Third-party notices\n\n"
        "- Neural Amp Modeler / NeuralAmpModelerCore: MIT, Copyright Steven Atkinson.\n"
        "- neural-amp-modeler-parametric / NeuralAmpModelerCoreParametric: MIT, "
        "Copyright Steven Atkinson and Phillip Self.\n"
        "- NAM Forge integration code: GPL-3.0-or-later.\n",
    )

    sums = write_sha256sums(pack)
    print(f"wrote {pack} ({len(sums)} files, {sha256_file(target_nam)[:12]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
