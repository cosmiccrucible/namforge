#!/usr/bin/env python3
"""Evaluate a static or parametric .nam on held-out captures."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

import numpy as np
import soundfile as sf
import torch

from nam.models import init_from_nam
from nam.models.parametric import ConcatWaveNet

AUDIO = Path(__file__).resolve().parents[2] / "exampleaudio"
PATTERN = re.compile(r"R(?P<R>\d+(?:\.\d+)?)P(?P<P>\d+(?:\.\d+)?)")

VALIDATION = [(0.0, 10.0), (4.0, 4.0), (6.0, 8.0), (8.0, 2.0), (10.0, 0.0)]


def load_model(path: Path):
    data = json.loads(path.read_text(encoding="utf-8"))
    arch = data["architecture"]
    if arch == "ConcatWaveNet":
        cfg = ConcatWaveNet.parse_config(data["config"])
        net = ConcatWaveNet(**cfg)
        net.import_weights(torch.tensor(data["weights"]))
        net.eval()
        return net, True, data.get("sample_rate", 48000)
    net = init_from_nam(data)
    net.eval()
    return net, False, data.get("sample_rate", 48000)


def analyze(pred: np.ndarray, target: np.ndarray) -> dict:
    n = min(len(pred), len(target))
    p = pred[:n].astype(np.float64)
    t = target[:n].astype(np.float64)
    err = p - t
    esr = float(np.sum(err * err) / max(np.sum(t * t), 1e-20))
    esr_db = 10.0 * np.log10(max(esr, 1e-20))
    corr = float(np.corrcoef(p, t)[0, 1]) if n > 2 else 0.0
    a = p - p.mean()
    b = t - t.mean()
    denom = np.sqrt(np.dot(a, a) * np.dot(b, b))
    lags = np.arange(-8, 9)
    vals = []
    for lag in lags:
        if lag >= 0:
            aa = a[: len(a) - lag]
            bb = b[lag:]
        else:
            aa = a[-lag:]
            bb = b[: len(b) + lag]
        vals.append(float(np.dot(aa, bb) / denom) if denom > 0 else 0.0)
    return {
        "n": int(n),
        "esr": esr,
        "esr_db": float(esr_db),
        "corr": corr,
        "best_lag": int(lags[int(np.argmax(vals))]),
        "peak_pred": float(np.max(np.abs(p))),
        "peak_target": float(np.max(np.abs(t))),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("nam", type=Path)
    ap.add_argument("--out-dir", type=Path, default=Path("docs/eval"))
    ap.add_argument("--delay", type=int, default=17)
    ap.add_argument("--start", type=float, default=-9.0)
    ap.add_argument("--stop", type=float, default=None)
    args = ap.parse_args()

    net, parametric, sr = load_model(args.nam)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    net.to(device)
    print(f"model={args.nam} parametric={parametric} sr={sr} device={device}")

    rows = []
    for r, p in VALIDATION:
        inp = AUDIO / f"Peavey6505-PowerAmp-Normal-R{int(r) if r == int(r) else r}P{int(p) if p == int(p) else p}_In.wav"
        out = AUDIO / f"Peavey6505-PowerAmp-Normal-R{int(r) if r == int(r) else r}P{int(p) if p == int(p) else p}_Out.wav"
        x, file_sr = sf.read(inp, dtype="float32")
        y, _ = sf.read(out, dtype="float32")
        s = int(args.start * file_sr) if args.start >= 0 else len(x) + int(args.start * file_sr)
        e = len(x) if args.stop is None else int(args.stop * file_sr)
        if e <= s:
            raise ValueError(f"empty region for {inp.name}: {s}..{e}")
        xs = torch.tensor(x[s:e], device=device)[None]
        with torch.no_grad():
            if parametric:
                pred = net(xs, torch.tensor([r, p], device=device))[0].cpu().numpy()
            else:
                pred = net(xs)[0].cpu().numpy()
        target = y[s + args.delay : e]
        metrics = analyze(pred, target)
        metrics.update({"R": r, "P": p, "file": out.name})
        rows.append(metrics)
        print(
            f"R={r:>4} P={p:>4} ESR={metrics['esr']:.6f} "
            f"({metrics['esr_db']:7.2f} dB) corr={metrics['corr']:+.4f} lag={metrics['best_lag']:+d}"
        )

    mean_esr = float(np.mean([m["esr"] for m in rows]))
    worst = max(rows, key=lambda m: m["esr"])
    best = min(rows, key=lambda m: m["esr"])
    print(
        f"mean ESR={mean_esr:.6f} ({10.0 * np.log10(max(mean_esr, 1e-20)):.2f} dB); "
        f"worst={worst['esr']:.6f} ({worst['esr_db']:.2f} dB); "
        f"best={best['esr']:.6f} ({best['esr_db']:.2f} dB)"
    )

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "eval.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    with (args.out_dir / "eval.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
