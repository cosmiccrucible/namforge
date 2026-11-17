#!/usr/bin/env python3
"""Generate NAM trainer configs for the Peavey 6505 example dataset."""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT.parent / "exampleaudio"
RUN = ROOT / "runs" / "parametric_peavey"
STATIC_RUN = ROOT / "runs" / "static_probe"

VALIDATION_POINTS = {
    (0.0, 10.0),
    (10.0, 0.0),
    (4.0, 4.0),
    (6.0, 8.0),
    (8.0, 2.0),
}

PATTERN = re.compile(r"R(?P<R>\d+(?:\.\d+)?)P(?P<P>\d+(?:\.\d+)?)")


def parse_name(path: Path) -> tuple[float, float] | None:
    m = PATTERN.search(path.stem)
    if not m:
        return None
    return float(m.group("R")), float(m.group("P"))


def main() -> None:
    RUN.mkdir(parents=True, exist_ok=True)
    STATIC_RUN.mkdir(parents=True, exist_ok=True)

    inputs = {parse_name(p): p for p in AUDIO.glob("*_In.wav")}
    outputs = {parse_name(p): p for p in AUDIO.glob("*_Out.wav")}
    points = sorted(set(inputs) & set(outputs), key=lambda t: (t[0], t[1]))
    if not points:
        raise SystemExit("no R/P pairs found")

    train = []
    validation = []
    for point in points:
        r, p = point
        item = {
            "y_path": str(outputs[point].resolve()),
            "params": {"R": r, "P": p},
            "start_seconds": 10.0,
            "stop_seconds": -9.0,
            "ny": 8192,
        }
        if point in VALIDATION_POINTS:
            item["start_seconds"] = -9.0
            item["stop_seconds"] = None
            item["ny"] = None
            validation.append(item)
        else:
            train.append(item)

    data = {
        "type": "parametric",
        "common": {
            "x_path": str(next(iter(inputs.values())).resolve()),
            "delay": 17,
            "require_input_pre_silence": None,
        },
        "train": train,
        "validation": validation,
    }
    (RUN / "data.json").write_text(json.dumps(data, indent=2), encoding="utf-8")

    layers = {
        "channels": 8,
        "kernel_sizes": [6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6],
        "dilations": [1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239],
        "activation": "LeakyReLU",
        "gated": False,
        "head": {"out_channels": 1, "kernel_size": 16, "bias": True},
    }
    model = {
        "net": {
            "name": "ConcatWaveNet",
            "config": {
                "layers": [layers],
                "head_scale": 0.01,
                "params": [
                    {"name": "R", "min": 0.0, "max": 10.0, "default": 5.0, "type": "continuous"},
                    {"name": "P", "min": 0.0, "max": 10.0, "default": 5.0, "type": "continuous"},
                ],
            },
        },
        "loss": {"val_loss": "esr", "mrstft_weight": 0.0005},
        "optimizer": {"lr": 0.002, "weight_decay": 3.17e-7},
        "lr_scheduler": {"class": "ExponentialLR", "kwargs": {"gamma": 0.994}},
    }
    (RUN / "model.json").write_text(json.dumps(model, indent=2), encoding="utf-8")

    learning = {
        "torch_compile": {"enabled": False},
        "train_dataloader": {
            "batch_size": 16,
            "shuffle": True,
            "pin_memory": True,
            "drop_last": True,
            "num_workers": 0,
        },
        "val_dataloader": {"batch_size": 16, "pin_memory": True, "num_workers": 0},
        "trainer": {
            "accelerator": "gpu",
            "devices": 1,
            "precision": "32-true",
            "benchmark": True,
            "max_epochs": 20,
            "gradient_clip_val": 1.0,
            "enable_progress_bar": True,
            "enable_model_summary": True,
        },
        "threshold_esr": None,
        "trainer_fit_kwargs": {},
    }
    (RUN / "learning.json").write_text(json.dumps(learning, indent=2), encoding="utf-8")

    # A small static probe: one capture, a short window, tiny model, fast run.
    static_point = (5.0, 5.0)
    static_data = {
        "train": {"start_seconds": 10.0, "stop_seconds": 70.0, "ny": 8192},
        "validation": {"start_seconds": 70.0, "stop_seconds": None, "ny": None},
        "common": {
            "x_path": str(inputs[static_point].resolve()),
            "y_path": str(outputs[static_point].resolve()),
            "delay": 17,
            "require_input_pre_silence": None,
        },
    }
    (STATIC_RUN / "data.json").write_text(json.dumps(static_data, indent=2), encoding="utf-8")

    static_model = {
        "net": {
            "name": "WaveNet",
            "config": {
                "layers_configs": [
                    {
                        "condition_size": 1,
                        "input_size": 1,
                        "channels": 4,
                        "head": {"out_channels": 1, "kernel_size": 1, "bias": True},
                        "kernel_size": 3,
                        "dilations": [1, 2, 4, 8, 16, 32, 64, 128],
                        "activation": "Tanh",
                    }
                ],
                "head_scale": 0.05,
            },
        },
        "optimizer": {"lr": 0.004},
        "lr_scheduler": {"class": "ExponentialLR", "kwargs": {"gamma": 0.99}},
    }
    (STATIC_RUN / "model.json").write_text(json.dumps(static_model, indent=2), encoding="utf-8")

    static_learning = {
        "train_dataloader": {
            "batch_size": 8,
            "shuffle": True,
            "pin_memory": True,
            "drop_last": True,
            "num_workers": 0,
        },
        "val_dataloader": {"batch_size": 8, "pin_memory": True, "num_workers": 0},
        "trainer": {
            "accelerator": "gpu",
            "devices": 1,
            "precision": "32-true",
            "max_epochs": 3,
            "gradient_clip_val": 1.0,
            "enable_progress_bar": True,
            "enable_model_summary": True,
            "limit_train_batches": 100,
            "limit_val_batches": 2,
        },
        "trainer_fit_kwargs": {},
    }
    (STATIC_RUN / "learning.json").write_text(json.dumps(static_learning, indent=2), encoding="utf-8")

    with (RUN / "train_matrix.csv").open("w", encoding="utf-8") as f:
        f.write("split,R,P,in_path,out_path\n")
        for split, items in (("train", train), ("validation", validation)):
            for item in items:
                f.write(
                    f"{split},{item['params']['R']},{item['params']['P']},"
                    f"{data['common']['x_path']},{item['y_path']}\n"
                )

    print(f"points={len(points)} train={len(train)} validation={len(validation)}")
    print(f"wrote {RUN} and {STATIC_RUN}")


if __name__ == "__main__":
    main()
