#!/usr/bin/env python3
"""Shared pack-manifest and checksum helpers for NAM Forge packs.

Pack format v2 (see `docs/pack-format.md`) keeps the manifest additive and
machine-checkable: every model file has a declared header, and every model has
a provenance record with the validation basis it can honestly claim.

Every text file in a pack is written with LF endings. `SHA256SUMS` therefore
records the bytes that git actually stores, which is what made the v1 sums
fail on Windows checkouts.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKS_ROOT = ROOT / "packs"
SUMS_NAME = "SHA256SUMS"
SCHEMA_VERSION = 2
QUALITIES = ("standard", "eco")
MODEL_TYPES = ("static", "parametric")
ARCHITECTURES = ("WaveNet", "ConcatWaveNet")
BASES = (
    "temporal-seen-params",
    "temporal-single-capture",
    "unseen-params",
    "in-sample-all-data",
)
PROVENANCE_KEYS = ("source", "training", "dataset", "validation")
SOURCE_KEYS = ("run", "model", "sha256")
TRAINING_KEYS = ("epochs", "learning_rate", "channels", "num_layers", "weights")
DATASET_KEYS = ("name", "captures", "train_captures", "delay_samples")
VALIDATION_KEYS = (
    "basis",
    "points",
    "region",
    "esr_mean",
    "esr_worst",
    "measured_utc",
    "measured_by",
)


class PackFormatError(ValueError):
    """Raised when a pack directory does not satisfy the v2 format."""


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise PackFormatError(f"{path}: missing")
    except json.JSONDecodeError as exc:
        raise PackFormatError(f"{path}: invalid JSON ({exc})") from exc
    if not isinstance(data, dict):
        raise PackFormatError(f"{path}: expected a JSON object")
    return data


def write_json_lf(path: Path, data: dict) -> None:
    path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def write_text_lf(path: Path, text: str) -> None:
    if not text.endswith("\n"):
        text += "\n"
    path.write_text(text, encoding="utf-8", newline="\n")


def pack_files(pack_dir: Path) -> list[str]:
    """Relative POSIX paths of every pack file except the sums file, sorted."""
    files = [
        p.relative_to(pack_dir).as_posix()
        for p in pack_dir.rglob("*")
        if p.is_file() and p.name != SUMS_NAME
    ]
    return sorted(files)


def compute_sums(pack_dir: Path) -> dict[str, str]:
    return {rel: sha256_file(pack_dir / rel) for rel in pack_files(pack_dir)}


def write_sha256sums(pack_dir: Path) -> dict[str, str]:
    """Write `SHA256SUMS` for `pack_dir` from its current bytes (LF-only file)."""
    sums = compute_sums(pack_dir)
    body = "".join(f"{digest}  {rel}\n" for rel, digest in sums.items())
    (pack_dir / SUMS_NAME).write_bytes(body.encode("utf-8"))
    return sums


def parse_sha256sums(pack_dir: Path) -> dict[str, str]:
    path = pack_dir / SUMS_NAME
    if not path.is_file():
        raise PackFormatError(f"{pack_dir.name}: missing {SUMS_NAME}")
    raw = path.read_bytes()
    if b"\r" in raw:
        raise PackFormatError(
            f"{pack_dir.name}: {SUMS_NAME} contains CR bytes; checksums must be "
            "recorded from LF-stable bytes"
        )
    if not raw.endswith(b"\n"):
        raise PackFormatError(f"{pack_dir.name}: {SUMS_NAME} has no trailing newline")
    entries: dict[str, str] = {}
    for lineno, line in enumerate(raw.decode("utf-8").splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split("  ", 1)
        if len(parts) != 2 or len(parts[0]) != 64:
            raise PackFormatError(
                f"{pack_dir.name}: {SUMS_NAME}:{lineno} malformed entry {line!r}"
            )
        digest, rel = parts[0].strip().lower(), parts[1].strip()
        if rel.startswith("/") or ".." in Path(rel).parts:
            raise PackFormatError(
                f"{pack_dir.name}: {SUMS_NAME}:{lineno} unsafe path {rel!r}"
            )
        if rel in entries:
            raise PackFormatError(
                f"{pack_dir.name}: {SUMS_NAME}:{lineno} duplicate path {rel!r}"
            )
        entries[rel] = digest
    if not entries:
        raise PackFormatError(f"{pack_dir.name}: {SUMS_NAME} is empty")
    return entries


def model_entries(manifest: dict) -> dict[str, dict]:
    """Return {quality: {nam, header, architecture, nam_version}}."""
    models = manifest.get("models")
    if not isinstance(models, dict):
        raise PackFormatError("manifest has no 'models' object")
    return models


def load_manifest(pack_dir: Path) -> dict:
    return read_json(pack_dir / "params.json")


def _is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def validate_manifest(pack_dir: Path, manifest: dict | None = None) -> list[str]:
    """Structural validation of a v2 manifest. Returns a list of problems."""
    problems: list[str] = []
    manifest = manifest if manifest is not None else load_manifest(pack_dir)
    name = pack_dir.name

    def require(cond: bool, message: str) -> None:
        if not cond:
            problems.append(message)

    require(manifest.get("schema") == SCHEMA_VERSION, f"schema must be {SCHEMA_VERSION}")
    require(manifest.get("name") == name, f"name must match directory {name!r}")
    model_type = manifest.get("model_type")
    require(model_type in MODEL_TYPES, f"model_type must be one of {MODEL_TYPES}")

    qualities = manifest.get("quality_modes")
    require(
        isinstance(qualities, list) and bool(qualities),
        "quality_modes must be a non-empty list",
    )
    qualities = qualities if isinstance(qualities, list) else []
    require(
        all(q in QUALITIES for q in qualities),
        f"quality_modes entries must be in {QUALITIES}",
    )
    require(len(set(qualities)) == len(qualities), "quality_modes has duplicates")

    models = manifest.get("models")
    require(isinstance(models, dict), "models must be an object keyed by quality")
    if isinstance(models, dict):
        require(
            sorted(models) == sorted(qualities),
            "models keys must match quality_modes",
        )
        for quality, entry in models.items():
            prefix = f"models.{quality}"
            require(isinstance(entry, dict), f"{prefix} must be an object")
            if not isinstance(entry, dict):
                continue
            rel = entry.get("nam")
            require(isinstance(rel, str) and rel.endswith(".nam"), f"{prefix}.nam must be a .nam path")
            if isinstance(rel, str):
                require(
                    not rel.startswith("/") and ".." not in Path(rel).parts,
                    f"{prefix}.nam must be a relative path inside the pack",
                )
                require((pack_dir / rel).is_file(), f"{prefix}.nam missing on disk: {rel}")
            header = entry.get("header")
            require(isinstance(header, str), f"{prefix}.header must be a path")
            if isinstance(header, str):
                require(
                    not header.startswith("/") and ".." not in Path(header).parts,
                    f"{prefix}.header must be a relative path inside the pack",
                )
                require(
                    (pack_dir / header).is_file(),
                    f"{prefix}.header missing on disk: {header}",
                )
            require(
                entry.get("architecture") in ARCHITECTURES,
                f"{prefix}.architecture must be one of {ARCHITECTURES}",
            )
            require(
                isinstance(entry.get("nam_version"), str),
                f"{prefix}.nam_version must be a string",
            )

    for key in ("sample_rate", "latency_samples", "capture_delay_samples"):
        require(_is_number(manifest.get(key)), f"{key} must be a number")
    if _is_number(manifest.get("sample_rate")):
        require(manifest["sample_rate"] > 0, "sample_rate must be positive")
    if _is_number(manifest.get("latency_samples")):
        require(manifest["latency_samples"] >= 0, "latency_samples must be >= 0")
    if _is_number(manifest.get("capture_delay_samples")):
        require(
            manifest["capture_delay_samples"] >= 0,
            "capture_delay_samples must be >= 0",
        )

    axes = manifest.get("axes")
    require(isinstance(axes, list), "axes must be a list")
    axes = axes if isinstance(axes, list) else []
    if model_type == "static":
        require(axes == [], "static packs must have no axes")
    elif model_type == "parametric":
        require(bool(axes), "parametric packs must declare at least one axis")
    seen_axis_names = set()
    for axis in axes:
        require(isinstance(axis, dict), "each axis must be an object")
        if not isinstance(axis, dict):
            continue
        axis_name = axis.get("name")
        require(isinstance(axis_name, str) and bool(axis_name), "axis.name must be a string")
        if isinstance(axis_name, str):
            require(axis_name not in seen_axis_names, f"duplicate axis {axis_name!r}")
            seen_axis_names.add(axis_name)
        for field in ("min", "max", "default"):
            require(_is_number(axis.get(field)), f"axis {axis_name}.{field} must be a number")
        if _is_number(axis.get("min")) and _is_number(axis.get("max")):
            require(axis["min"] < axis["max"], f"axis {axis_name} has min >= max")
        require(
            axis.get("type", "continuous") in ("continuous", "switch"),
            f"axis {axis_name}.type must be continuous or switch",
        )

    provenance = manifest.get("provenance")
    require(isinstance(provenance, dict), "provenance must be an object keyed by quality")
    provenance = provenance if isinstance(provenance, dict) else {}
    require(
        sorted(provenance) == sorted(qualities),
        "provenance keys must match quality_modes",
    )
    for quality in qualities:
        block = provenance.get(quality)
        prefix = f"provenance.{quality}"
        require(isinstance(block, dict), f"{prefix} must be an object")
        if not isinstance(block, dict):
            continue
        for key in PROVENANCE_KEYS:
            require(key in block, f"{prefix}.{key} missing")
        source = block.get("source") if isinstance(block.get("source"), dict) else {}
        for key in SOURCE_KEYS:
            require(key in source, f"{prefix}.source.{key} missing")
        require(
            isinstance(source.get("sha256"), str) and len(source.get("sha256", "")) == 64,
            f"{prefix}.source.sha256 must be a 64-char hex digest",
        )
        require(
            isinstance(source.get("run"), str) and bool(source.get("run")),
            f"{prefix}.source.run must be a repo-relative path",
        )
        training = block.get("training") if isinstance(block.get("training"), dict) else {}
        for key in TRAINING_KEYS:
            require(key in training, f"{prefix}.training.{key} missing")
        require(
            isinstance(training.get("epochs"), int) and training.get("epochs", 0) > 0,
            f"{prefix}.training.epochs must be a positive int",
        )
        dataset = block.get("dataset") if isinstance(block.get("dataset"), dict) else {}
        for key in DATASET_KEYS:
            require(key in dataset, f"{prefix}.dataset.{key} missing")
        require(
            dataset.get("delay_samples") == manifest.get("capture_delay_samples"),
            f"{prefix}.dataset.delay_samples must match capture_delay_samples",
        )
        validation = (
            block.get("validation") if isinstance(block.get("validation"), dict) else {}
        )
        for key in VALIDATION_KEYS:
            require(key in validation, f"{prefix}.validation.{key} missing")
        require(
            validation.get("basis") in BASES,
            f"{prefix}.validation.basis must be one of {BASES}",
        )
        points = validation.get("points")
        require(
            isinstance(points, list) and bool(points),
            f"{prefix}.validation.points must be a non-empty list",
        )
        require(
            _is_number(validation.get("esr_mean")) and validation.get("esr_mean", 0) > 0,
            f"{prefix}.validation.esr_mean must be positive",
        )
        require(
            _is_number(validation.get("esr_worst"))
            and validation.get("esr_worst", 0) >= validation.get("esr_mean", 0),
            f"{prefix}.validation.esr_worst must be >= esr_mean",
        )
        require(
            isinstance(validation.get("measured_by"), str),
            f"{prefix}.validation.measured_by must be a string",
        )
        report = validation.get("eval_report")
        if report is not None:
            require(
                isinstance(report, str)
                and not report.startswith("/")
                and ".." not in Path(report).parts,
                f"{prefix}.validation.eval_report must be a repo-relative path",
            )
    return problems


def model_paths(pack_dir: Path, manifest: dict) -> dict[str, tuple[Path, Path]]:
    """{quality: (nam_path, header_path)} with absolute paths."""
    out = {}
    for quality, entry in model_entries(manifest).items():
        out[quality] = (pack_dir / entry["nam"], pack_dir / entry["header"])
    return out


def find_packs(root: Path = PACKS_ROOT) -> list[Path]:
    return sorted(p for p in root.iterdir() if p.is_dir() and (p / "params.json").is_file())
