#!/usr/bin/env python3
"""Convert a compact NAM .nam into the NamA2Core C++ descriptor/weights header.

`describe()` is the single source of truth for the descriptor: both the header
writer here and the models repo's verify_packs.py gate call it, so a staged header can be
checked against the `.nam` it claims to come from without a C++ build.
"""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path

ACTIVATIONS = {"LeakyReLU": 0, "Tanh": 1, "ReLU": 2, "Sigmoid": 3}


def _activation(value) -> tuple[int, float]:
    if isinstance(value, str):
        name = value
        kwargs = {}
    elif isinstance(value, dict):
        name = value.get("type") or value.get("name")
        kwargs = value
    else:
        raise ValueError(f"unsupported activation: {value!r}")
    slope = float(kwargs.get("negative_slope", 0.01))
    if name not in ACTIVATIONS:
        raise ValueError(
            f"unsupported activation {name!r}; supported: {sorted(ACTIVATIONS)}"
        )
    return ACTIVATIONS[name], slope


def _fmt(v: float) -> str:
    if not math.isfinite(float(v)):
        raise ValueError("non-finite weight")
    s = f"{float(v):.9g}"
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s + "f"


def describe(data: dict) -> dict:
    """Validate a `.nam` payload and return the C++ descriptor plus flat weights.

    Raises ValueError for architectures the core cannot run.
    """
    arch = data.get("architecture")
    if arch not in ("WaveNet", "ConcatWaveNet"):
        raise ValueError(f"unsupported architecture {arch!r}")
    config = data["config"]
    layers = config["layers"]
    if len(layers) != 1:
        raise ValueError("NamA2Core currently supports exactly one layer array")
    layer = layers[0]
    channels = int(layer["channels"])
    kernel_sizes = layer.get("kernel_sizes")
    if kernel_sizes is None:
        ks = layer.get("kernel_size")
        kernel_sizes = [int(ks)] * len(layer["dilations"])
    kernel_sizes = [int(k) for k in kernel_sizes]
    dilations = [int(d) for d in layer["dilations"]]
    num_layers = len(dilations)
    if len(kernel_sizes) != num_layers:
        raise ValueError("kernel_sizes and dilations length mismatch")

    top_head = config.get("head")
    if top_head is not None:
        raise ValueError("top-level post-stack head is not supported by NamA2Core yet")
    head = layer["head"]
    head_kernel = int(head["kernel_size"])
    head_out = int(head["out_channels"])
    if head_out != 1:
        raise ValueError("only mono output head is supported")

    params = config.get("params", []) if arch == "ConcatWaveNet" else []
    input_channels = 1 + sum(
        1 for p in params if p.get("type", "continuous") == "continuous"
    )
    # Switches would add one-hot channels; this exporter intentionally only
    # supports continuous params until the C++ node grows that surface.
    if arch == "ConcatWaveNet":
        if any(p.get("type", "continuous") != "continuous" for p in params):
            raise ValueError("switch parameters are not supported by this exporter yet")
        if int(layer.get("input_size", input_channels)) != input_channels:
            raise ValueError("layer input_size does not match audio + params")
        if int(layer.get("condition_size", input_channels)) != input_channels:
            raise ValueError("layer condition_size does not match audio + params")
    input_size = int(layer.get("input_size", input_channels))
    condition_size = int(layer.get("condition_size", input_channels))
    if input_size != input_channels or condition_size != input_channels:
        raise ValueError("unsupported input/condition size")

    act_value = layer.get("activation", "LeakyReLU")
    if isinstance(act_value, list):
        if not act_value:
            raise ValueError("empty activation list")
        if any(a != act_value[0] for a in act_value):
            raise ValueError("per-layer activation differences are not supported")
        act_value = act_value[0]
    act, slope = _activation(act_value)
    gating = layer.get("gating_mode", "none")
    if isinstance(gating, list):
        if any(str(g).lower() != "none" for g in gating):
            raise ValueError("gated layers are not supported by this exporter")
    elif str(gating).lower() != "none":
        raise ValueError("gated layers are not supported by this exporter")
    if layer.get("gated", False):
        raise ValueError("gated layers are not supported by this exporter")

    head_scale = float(config.get("head_scale", 1.0))
    weights = data.get("weights")
    if not isinstance(weights, list):
        raise ValueError(".nam has no weights array")
    w = [float(x) for x in weights]

    # Walk the export order and validate.
    off = 0

    def take(n: int) -> list[float]:
        nonlocal off
        if off + n > len(w):
            raise ValueError(f"weight blob too short at offset {off} (need {n})")
        out = w[off : off + n]
        off += n
        return out

    rechannel = take(channels * input_channels)
    layer_weights = []
    for li in range(num_layers):
        k = kernel_sizes[li]
        conv = take(channels * channels * k)
        conv_b = take(channels)
        mix = take(channels * input_channels)
        l1 = take(channels * channels)
        l1_b = take(channels)
        layer_weights.append((conv, conv_b, mix, l1, l1_b))
    head_w = take(channels * head_kernel)
    head_b = take(1)
    blob_scale = take(1)[0]
    if off != len(w):
        raise ValueError(f"trailing weights: consumed {off}, blob has {len(w)}")
    if abs(blob_scale - head_scale) > 1e-6:
        head_scale = blob_scale

    # The C++ core wants one flat blob in exactly this order.
    flat: list[float] = []
    flat.extend(rechannel)
    for conv, conv_b, mix, l1, l1_b in layer_weights:
        flat.extend(conv)
        flat.extend(conv_b)
        flat.extend(mix)
        flat.extend(l1)
        flat.extend(l1_b)
    flat.extend(head_w)
    flat.extend(head_b)
    flat.append(head_scale)

    pmin = [float(p["min"]) for p in params]
    pmax = [float(p["max"]) for p in params]
    if not pmin:
        pmin = [0.0]
        pmax = [1.0]

    return {
        "architecture": arch,
        "sample_rate": int(data.get("sample_rate", 48000)),
        "channels": channels,
        "input_channels": input_channels,
        "num_layers": num_layers,
        "kernel_sizes": kernel_sizes,
        "dilations": dilations,
        "head_kernel": head_kernel,
        "head_scale": head_scale,
        "leaky_slope": slope,
        "activation": act,
        "activation_name": act_value.get("type") if isinstance(act_value, dict) else act_value,
        "param_min": pmin,
        "param_max": pmax,
        "weights": flat,
        "weight_count": len(flat),
        "source_weight_count": len(w),
    }


def render(data: dict, namespace: str) -> str:
    """Render the C++ descriptor header for a `.nam` payload."""
    d = describe(data)
    sym = re.sub(r"[^A-Za-z0-9_]", "_", namespace)

    def array(name: str, values, per_line: int = 8) -> list[str]:
        out = [f"static constexpr float {name}[] = {{"]
        row = []
        for i, v in enumerate(values, 1):
            row.append(_fmt(v))
            if i % per_line == 0:
                out.append("  " + ", ".join(row) + ",")
                row = []
        if row:
            out.append("  " + ", ".join(row) + ",")
        out.append("};")
        return out

    lines = [
        "#pragma once",
        '#include "NamA2Core.h"',
        "",
        f"namespace namforge::models::{sym} {{",
        f"static constexpr int kNumLayers = {d['num_layers']};",
        f"static constexpr int kChannels = {d['channels']};",
        f"static constexpr int kInputChannels = {d['input_channels']};",
        f"static constexpr int kHeadKernel = {d['head_kernel']};",
        f"static constexpr float kHeadScale = {_fmt(d['head_scale'])};",
        f"static constexpr float kLeakySlope = {_fmt(d['leaky_slope'])};",
        f"static constexpr int kActivation = {d['activation']};",
        "static constexpr int kKernelSizes[kNumLayers] = {"
        + ", ".join(str(k) for k in d["kernel_sizes"])
        + "};",
        "static constexpr int kDilations[kNumLayers] = {"
        + ", ".join(str(x) for x in d["dilations"])
        + "};",
        "static constexpr float kParamMin[] = {"
        + ", ".join(_fmt(x) for x in d["param_min"])
        + "};",
        "static constexpr float kParamMax[] = {"
        + ", ".join(_fmt(x) for x in d["param_max"])
        + "};",
        "",
    ]
    lines.extend(array("kWeights", d["weights"], per_line=6))
    lines.extend(
        [
            "",
            "static const ModelDesc kDesc = {",
            f'  "{sym}",',
            "  kNumLayers, kChannels, kInputChannels, kHeadKernel,",
            "  kHeadScale, kLeakySlope, kActivation,",
            "  kKernelSizes, kDilations, kParamMin, kParamMax, kWeights",
            "};",
            "} // namespace",
            "",
        ]
    )
    return "\n".join(lines)


def convert(nam_path: Path, header_path: Path, namespace: str) -> None:
    data = json.loads(nam_path.read_text(encoding="utf-8"))
    text = render(data, namespace)
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(text, encoding="utf-8", newline="\n")
    d = describe(data)
    print(
        f"wrote {header_path} layers={d['num_layers']} channels={d['channels']} "
        f"input={d['input_channels']} weights={d['weight_count']}"
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("input_nam", type=Path)
    ap.add_argument("output_header", type=Path)
    ap.add_argument("--namespace", default="NamModel")
    args = ap.parse_args()
    convert(args.input_nam, args.output_header, args.namespace)


if __name__ == "__main__":
    main()
