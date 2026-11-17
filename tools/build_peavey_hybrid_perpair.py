#!/usr/bin/env python3
"""Per-capture linear FIR + 2ch NAM residual pilot.

This is an oracle upper bound: each capture gets its own FIR, so the residual
contains only nonlinearity/noise, not parameter-dependent linear response.
If this cannot fit well, the hybrid approach is not worth parameterising.
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
RUN = ROOT / "runs" / "peavey_hybrid_perpair"
K = 2048
SR = 48000
DELAY = 17


def pair_paths():
    rows = []
    pat = re.compile(r"R(\d+(?:\.\d+)?)P(\d+(?:\.\d+)?)")
    for out in sorted(AUDIO.glob("*_Out.wav")):
        inp = Path(str(out).replace("_Out.wav", "_In.wav"))
        m = pat.search(out.name)
        if inp.exists() and m:
            rows.append((inp, out, float(m.group(1)), float(m.group(2))))
    return rows


def fit_one(inp, out):
    with sf.SoundFile(inp) as fx, sf.SoundFile(out) as fy:
        fx.seek(int(5 * SR)); fy.seek(int(5 * SR))
        x = fx.read(int(10 * SR), dtype="float32"); y = fy.read(int(10 * SR), dtype="float32")
    M = len(x) - DELAY
    ac = np.correlate(x[:M], x[:M], "full")[M - 1 : M - 1 + K]
    p = np.empty(K, dtype=np.float64)
    for k in range(K):
        p[k] = np.dot(x[: M - k], y[DELAY + k : DELAY + M])
    h = solve(toeplitz(ac) + 1e-3 * np.trace(toeplitz(ac)) / K * np.eye(K), p, assume_a="pos")
    return h.astype(np.float32)


def fir(x, h):
    return np.convolve(x, h, "full")[: len(x)].astype(np.float32)


def main() -> int:
    RUN.mkdir(parents=True, exist_ok=True)
    (RUN / "fir").mkdir(exist_ok=True)
    rows = pair_paths()
    data = json.loads((ROOT / "runs" / "parametric_peavey" / "data.json").read_text())
    val_names = {Path(v["y_path"]).name for v in data["validation"]}
    val = [r for r in rows if r[1].name in val_names]

    # Pass 1: fit each FIR, measure linear ESR, find residual peak.
    peak = 0.0
    esrs = []
    for inp, out, _, _ in rows:
        h = fit_one(inp, out)
        np.save(RUN / "fir" / (out.stem.replace("_Out", "") + ".npy"), h)
        with sf.SoundFile(inp) as fx, sf.SoundFile(out) as fy:
            x = fx.read(dtype="float32"); y = fy.read(dtype="float32")
        lin = fir(x, h); n = min(len(lin), len(y) - DELAY)
        residual = y[DELAY : DELAY + n] - lin[:n]
        peak = max(peak, float(np.max(np.abs(residual))))
        if out.name in val_names:
            esrs.append(float(np.mean((lin[:n] - y[DELAY : DELAY + n]) ** 2) / np.mean(y[DELAY : DELAY + n] ** 2)))
        print("fit", out.name, "residual peak", round(float(np.max(np.abs(residual))), 4), flush=True)
    scale = 0.8 / peak
    print("linear val ESR", np.mean(esrs), 10 * np.log10(np.mean(esrs)), "scale", scale)
    (RUN / "scale.json").write_text(json.dumps({"scale": scale, "peak": peak}, indent=2))

    # Pass 2: write scaled residual WAVs, pad by DELAY to input length.
    train_items, val_items = [], []
    for inp, out, r, p in rows:
        h = np.load(RUN / "fir" / (out.stem.replace("_Out", "") + ".npy"))
        with sf.SoundFile(inp) as fx, sf.SoundFile(out) as fy:
            x = fx.read(dtype="float32"); y = fy.read(dtype="float32")
        lin = fir(x, h); n = min(len(lin), len(y) - DELAY)
        residual = y[DELAY : DELAY + n] - lin[:n]
        residual = np.concatenate([residual * scale, np.zeros(DELAY, dtype=np.float32)])
        rp = RUN / "residual" / (out.stem.replace("_Out", "") + "_res.wav")
        rp.parent.mkdir(exist_ok=True)
        sf.write(rp, residual, SR, subtype="PCM_24")
        item = {"y_path": str(rp.resolve()), "params": {"R": r, "P": p},
                "start_seconds": 10.0, "stop_seconds": -9.0, "ny": 8192}
        if out.name in val_names:
            item["start_seconds"] = -9.0; item["stop_seconds"] = None; item["ny"] = None
            val_items.append(item)
        else:
            train_items.append(item)
        print("residual", out.name, flush=True)

    data_out = {"type": "parametric",
                "common": {"x_path": str((AUDIO / "Peavey6505-PowerAmp-Normal-R0P0_In.wav").resolve()),
                           "delay": 0, "require_input_pre_silence": None},
                "train": train_items, "validation": val_items}
    (RUN / "data.json").write_text(json.dumps(data_out, indent=2))

    base = json.loads((ROOT / "runs" / "parametric_peavey" / "model-eco4.json").read_text())
    layer = base["net"]["config"]["layers"][0]
    layer["channels"] = 2
    layer["kernel_sizes"] = [6, 6, 6, 6, 6, 6, 15, 6]
    layer["dilations"] = [1, 3, 7, 17, 41, 101, 239, 1]
    base["optimizer"] = {"lr": 0.001, "weight_decay": 1e-5}
    base["lr_scheduler"] = {"class": "ExponentialLR", "kwargs": {"gamma": 0.99}}
    (RUN / "model.json").write_text(json.dumps(base, indent=2))
    learning = json.loads((ROOT / "runs" / "parametric_peavey" / "learning-eco4.json").read_text())
    learning["trainer"]["max_epochs"] = 50
    (RUN / "learning.json").write_text(json.dumps(learning, indent=2))
    print("wrote", RUN)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
