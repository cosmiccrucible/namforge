"""Generate NAM trainer configs from a NAM Forge run manifest."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .parameters import AxisSpec, Capture


DEFAULT_LAYERS = {
    "kernel_sizes": [6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6],
    "dilations": [1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239],
}


@dataclass(frozen=True)
class RunSpec:
    name: str
    model_type: str  # static | parametric
    input_path: Path
    captures: tuple[Capture, ...]
    axes: tuple[AxisSpec, ...]
    delay: int = 17
    sample_rate: int = 48000


def _fmt_params(axes: tuple[AxisSpec, ...]) -> list[dict]:
    return [
        {
            "name": a.name,
            "min": a.minimum,
            "max": a.maximum,
            "default": a.default,
            "type": "continuous",
        }
        for a in axes
    ]


def data_config(
    run: RunSpec,
    *,
    validation_points: set[tuple[float, ...]] | None = None,
    validation_mode: str = "points",
    train_start: float = 10.0,
    train_stop: float = -9.0,
    val_start: float = -9.0,
) -> dict:
    """Build the trainer's data config.

    ``validation_mode`` selects the split for parametric runs:

    * ``"points"`` — captures whose parameter vector is in ``validation_points``
      form the held-out validation set and the rest train (the Peavey 32/5
      convention).
    * ``"temporal"`` — every capture appears in both train (10 s .. -9 s) and
      validation (-9 s .. end), matching ``runs/kraken_parametric/data.json``.
      Used for single-axis datasets and when the held-out targets would remove
      every training capture.

    A parametric run must end up with at least one training and one validation
    entry; an empty split is an error rather than a silently broken run.
    """
    if not run.captures:
        raise ValueError("run has no captures")
    if validation_mode not in {"points", "temporal"}:
        raise ValueError(f"unknown validation mode: {validation_mode!r}")

    def is_val(c: Capture) -> bool:
        if validation_points is None:
            return False
        key = tuple(c.values[a.name] for a in run.axes)
        return key in validation_points

    if run.model_type == "static":
        if len(run.captures) != 1:
            raise ValueError("static run must have exactly one capture")
        c = run.captures[0]
        return {
            "train": {"start_seconds": train_start, "stop_seconds": train_stop, "ny": 8192},
            "validation": {"start_seconds": val_start, "stop_seconds": None, "ny": None},
            "common": {
                "x_path": str(run.input_path.resolve()),
                "y_path": str(c.path.resolve()),
                "delay": run.delay,
                "require_input_pre_silence": None,
            },
        }

    train, validation = [], []
    for c in run.captures:
        item = {
            "y_path": str(c.path.resolve()),
            "params": {a.name: c.values[a.name] for a in run.axes},
            "start_seconds": train_start,
            "stop_seconds": train_stop,
            "ny": 8192,
        }
        if validation_mode == "temporal":
            train.append(item)
            validation.append(
                {
                    **item,
                    "start_seconds": val_start,
                    "stop_seconds": None,
                    "ny": None,
                }
            )
        elif is_val(c):
            validation.append(
                {
                    **item,
                    "start_seconds": val_start,
                    "stop_seconds": None,
                    "ny": None,
                }
            )
        else:
            train.append(item)
    if not train:
        raise ValueError("parametric run has no training captures")
    if not validation:
        raise ValueError("parametric run has no validation captures")
    return {
        "type": "parametric",
        "common": {
            "x_path": str(run.input_path.resolve()),
            "delay": run.delay,
            "require_input_pre_silence": None,
        },
        "train": train,
        "validation": validation,
    }


def model_config(
    run: RunSpec,
    *,
    channels: int = 8,
    kernel_sizes: list[int] | None = None,
    dilations: list[int] | None = None,
    activation: str = "LeakyReLU",
) -> dict:
    ks = kernel_sizes or DEFAULT_LAYERS["kernel_sizes"]
    dil = dilations or DEFAULT_LAYERS["dilations"]
    layer = {
        "channels": channels,
        "kernel_sizes": ks,
        "dilations": dil,
        "activation": activation,
        "gated": False,
        "head": {"out_channels": 1, "kernel_size": 16, "bias": True},
    }
    if run.model_type == "static":
        net = {
            "name": "WaveNet",
            "config": {
                "layers_configs": [
                    {
                        **layer,
                        "input_size": 1,
                        "condition_size": 1,
                        "kernel_size": ks[0] if len(set(ks)) == 1 else ks,
                    }
                ],
                "head_scale": 0.01,
            },
        }
        # The static WaveNet exporter wants a single kernel_size, not the mixed
        # 6/15 A2 layout; use a uniform 6 for now.
        net["config"]["layers_configs"][0]["kernel_size"] = 6
        net["config"]["layers_configs"][0].pop("kernel_sizes", None)
    else:
        net = {
            "name": "ConcatWaveNet",
            "config": {
                "layers": [layer],
                "head_scale": 0.01,
                "params": _fmt_params(run.axes),
            },
        }
    return {
        "net": net,
        "loss": {"val_loss": "esr", "mrstft_weight": 0.0005},
        "optimizer": {"lr": 0.0005, "weight_decay": 1e-5},
        "lr_scheduler": {"class": "ExponentialLR", "kwargs": {"gamma": 0.99}},
    }


def learning_config(*, max_epochs: int = 30, batch_size: int = 16) -> dict:
    return {
        "torch_compile": {"enabled": False},
        "train_dataloader": {
            "batch_size": batch_size,
            "shuffle": True,
            "pin_memory": True,
            "drop_last": True,
            "num_workers": 0,
        },
        "val_dataloader": {"batch_size": batch_size, "pin_memory": True, "num_workers": 0},
        "trainer": {
            "accelerator": "gpu",
            "devices": 1,
            "precision": "32-true",
            "benchmark": True,
            "max_epochs": max_epochs,
            "gradient_clip_val": 1.0,
            "enable_progress_bar": True,
            "enable_model_summary": True,
        },
        "threshold_esr": None,
        "trainer_fit_kwargs": {},
    }


def write_run(
    run: RunSpec,
    out_dir: Path,
    *,
    validation_points: set[tuple[float, ...]] | None = None,
    validation_mode: str = "points",
    delay_source: str = "default",
    warnings: tuple[str, ...] | list[str] = (),
    channels: int = 8,
    max_epochs: int = 30,
    batch_size: int = 16,
) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    data = data_config(
        run,
        validation_points=validation_points,
        validation_mode=validation_mode,
    )
    (out_dir / "data.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    (out_dir / "model.json").write_text(
        json.dumps(model_config(run, channels=channels), indent=2), encoding="utf-8"
    )
    (out_dir / "learning.json").write_text(
        json.dumps(learning_config(max_epochs=max_epochs, batch_size=batch_size), indent=2),
        encoding="utf-8",
    )

    keys = [tuple(c.values[a.name] for a in run.axes) for c in run.captures]
    if run.model_type == "static":
        split = {"mode": "last_9s", "train_count": 1, "validation_count": 1, "points": []}
    elif validation_mode == "temporal":
        split = {
            "mode": "temporal_all",
            "train_count": len(run.captures),
            "validation_count": len(run.captures),
            "points": [],
        }
    else:
        held = sum(1 for k in keys if validation_points is not None and k in validation_points)
        split = {
            "mode": "held_out_points",
            "train_count": len(run.captures) - held,
            "validation_count": held,
            "points": [list(k) for k in sorted(validation_points or ())],
        }

    manifest = {
        "name": run.name,
        "model_type": run.model_type,
        "sample_rate": run.sample_rate,
        "delay": run.delay,
        "delay_source": delay_source,
        "warnings": list(warnings),
        "split": split,
        "input_path": str(run.input_path.resolve()),
        "axes": [
            {"name": a.name, "min": a.minimum, "max": a.maximum, "default": a.default}
            for a in run.axes
        ],
        "captures": [
            {"path": str(c.path.resolve()), "values": c.values} for c in run.captures
        ],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
