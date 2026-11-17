#!/usr/bin/env python3
"""Build a global-FIR + NAM-residual hybrid dataset for the Peavey set.

Pilot design:
  linear: 2048-tap causal FIR fitted on the 32 training captures
  residual: y[n+17] - linear[n], trained as a 2-channel parametric NAM
  final: linear[n] + residual_model(x[n])
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.linalg import solve, toeplitz

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT.parent / "exampleaudio"
RUN = ROOT / "runs" / "peavey_hybrid"
K = 2048
SR = 48000
DELAY = 17


def pair_paths() -> list[tuple[Path, Path, float, float]]:
    rows = []
    pat = re.compile(r"R(\d+(?:\.\d+)?)P(\d+(?:\.\d+)?)")
    for out in sorted(AUDIO.glob("*_Out.wav")):
        inp = Path(str(out).replace("_Out.wav", "_In.wav"))
        m = pat.search(out.name)
        if inp.exists() and m:
            rows.append((inp, out, float(m.group(1)), float(m.group(2))))
    return rows


def fit_global_fir(train: list[tuple[Path, Path, float, float]]) -> np.ndarray:
    ac = np.zeros(K, dtype=np.float64)
    p = np.zeros(K, dtype=np.float64)
    for inp, out, _, _ in train:
        with sf.SoundFile(inp) as fx, sf.SoundFile(out) as fy:
            fx.seek(int(5 * SR)); fy.seek(int(5 * SR))
            x = fx.read(int(10 * SR), dtype="float32")
            y = fy.read(int(10 * SR), dtype="float32")
        M = len(x) - DELAY
        # autocorrelation of x, lags 0..K-1
        ac += np.correlate(x[:M], x[:M], "full")[M - 1 : M - 1 + K]
        for k in range(K):
            p[k] += np.dot(x[: M - k], y[DELAY + k : DELAY + M])
        print(f"  fit {out.name}", flush=True)
    R = toeplitz(ac)
    ridge = 1e-3 * np.trace(R) / K
    h = solve(R + ridge * np.eye(K), p, assume_a="pos")
    return h.astype(np.float32)


def apply_fir(x: np.ndarray, h: np.ndarray) -> np.ndarray:
    # causal FIR, same length as x
    return np.convolve(x, h, "full")[: len(x)].astype(np.float32)


def main() -> int:
    RUN.mkdir(parents=True, exist_ok=True)
    residual_dir = RUN / "residual"
    residual_dir.mkdir(exist_ok=True)
    rows = pair_paths()
    data = json.loads((ROOT / "runs" / "parametric_peavey" / "data.json").read_text())
    val_names = {Path(v["y_path"]).name for v in data["validation"]}
    train = [r for r in rows if r[1].name not in val_names]
    val = [r for r in rows if r[1].name in val_names]
    print(f"pairs={len(rows)} train={len(train)} val={len(val)} K={K}")

    print("fitting global FIR on training captures...", flush=True)
    h = fit_global_fir(train)
    np.save(RUN / "fir.npy", h)

    def linear_esr(row, h):
        inp, out, _, _ = row
        with sf.SoundFile(inp) as fx, sf.SoundFile(out) as fy:
            fx.seek(-int(9 * SR), 2); fy.seek(-int(9 * SR), 2)
            x = fx.read(dtype="float32"); y = fy.read(dtype="float32")
        lin = apply_fir(x, h)
        n = min(len(lin), len(y) - DELAY)
        a = lin[:n]; b = y[DELAY : DELAY + n]
        return float(np.mean((a - b) ** 2) / np.mean(b * b))

    esrs = [linear_esr(r, h) for r in val]
    print("linear val ESR", np.mean(esrs), 10 * np.log10(np.mean(esrs)), "worst", max(esrs))

    # Write residual targets: r[n] = y[n+17] - linear[n].
    train_items, val_items = [], []
    for inp, out, r, p in rows:
        with sf.SoundFile(inp) as fx, sf.SoundFile(out) as fy:
            x = fx.read(dtype="float32"); y = fy.read(dtype="float32")
        lin = apply_fir(x, h)
        n = min(len(lin), len(y) - DELAY)
        residual = y[DELAY : DELAY + n] - lin[:n]
        rp = residual_dir / (out.stem.replace("_Out", "") + "_res.wav")
        sf.write(rp, residual, SR, subtype="PCM_24")
        item = {
            "y_path": str(rp.resolve()),
            "params": {"R": r, "P": p},
            "start_seconds": 10.0,
            "stop_seconds": -9.0,
            "ny": 8192,
        }
        if out.name in val_names:
            item["start_seconds"] = -9.0
            item["stop_seconds"] = None
            item["ny"] = None
            val_items.append(item)
        else:
            train_items.append(item)
        print(f"  residual {out.name}", flush=True)

    data_out = {
        "type": "parametric",
        "common": {
            "x_path": str((AUDIO / "Peavey6505-PowerAmp-Normal-R0P0_In.wav").resolve()),
            "delay": 0,
            "require_input_pre_silence": None,
        },
        "train": train_items,
        "validation": val_items,
    }
    (RUN / "data.json").write_text(json.dumps(data_out, indent=2), encoding="utf-8")

    # 2ch/8-layer residual model, same R/P axes.
    base = json.loads((ROOT / "runs" / "parametric_peavey" / "model-eco4.json").read_text())
    layer = base["net"]["config"]["layers"][0]
    layer["channels"] = 2
    layer["kernel_sizes"] = [6, 6, 6, 6, 6, 6, 15, 6]
    layer["dilations"] = [1, 3, 7, 17, 41, 101, 239, 1]
    layer["head"] = {"out_channels": 1, "kernel_size": 16, "bias": True}
    base["optimizer"] = {"lr": 0.001, "weight_decay": 1e-5}
    base["lr_scheduler"] = {"class": "ExponentialLR", "kwargs": {"gamma": 0.99}}
    (RUN / "model.json").write_text(json.dumps(base, indent=2), encoding="utf-8")

    learning = json.loads((ROOT / "runs" / "parametric_peavey" / "learning-eco4.json").read_text())
    learning["trainer"]["max_epochs"] = 50
    (RUN / "learning.json").write_text(json.dumps(learning, indent=2), encoding="utf-8")
    print("wrote", RUN)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
