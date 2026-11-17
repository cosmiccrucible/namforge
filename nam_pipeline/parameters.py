"""Deterministic filename parameter parser for NAM Forge.

The filename is the only parameter source for a parametric run. This module
extracts axis/value tokens, builds the capture matrix, and rejects incomplete or
ambiguous parses before training.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

# Axis token: letters (or letters+underscore) followed by a signed decimal.
_TOKEN = re.compile(r"(?P<axis>[A-Za-z][A-Za-z0-9_]*?)(?P<value>-?(?:\d+(?:\.\d*)?|\.\d+))")
# Ordered labels used by some captures, e.g. "Gain-Full".
_LABEL_TOKEN = re.compile(r"(?P<axis>[A-Za-z][A-Za-z0-9_]*?)[-_ ](?P<label>Min|Mid|Full)\b", re.IGNORECASE)
_LABEL_VALUES = {"min": 0.0, "mid": 5.0, "full": 10.0}
_INPUT_SUFFIX = re.compile(r"_(?:In|Out)$", re.IGNORECASE)



@dataclass(frozen=True)
class AxisSpec:
    name: str
    minimum: float
    maximum: float
    default: float
    step: float | None = None
    values: tuple[float, ...] = ()


@dataclass(frozen=True)
class Capture:
    path: Path
    values: dict[str, float]


class ParameterParseError(ValueError):
    pass


def strip_pair_suffix(stem: str) -> str:
    return _INPUT_SUFFIX.sub("", stem)


def parse_tokens(filename: str) -> dict[str, float]:
    """Parse all axis/value tokens from one filename stem.

    Adjacent forms (R0P10) and separated forms (gain_2_tone_8) are both
    accepted. Repeated axes are rejected rather than silently taking the last.
    """
    stem = strip_pair_suffix(Path(filename).stem)
    out: dict[str, float] = {}
    for m in _TOKEN.finditer(stem):
        axis = m.group("axis").rstrip("_")
        if not axis:
            continue
        value = float(m.group("value"))
        if axis in out:
            raise ParameterParseError(f"duplicate axis {axis!r} in {filename!r}")
        out[axis] = value
    for m in _LABEL_TOKEN.finditer(stem):
        axis = m.group("axis").rstrip("_")
        label = m.group("label").lower()
        value = _LABEL_VALUES[label]
        if axis in out:
            raise ParameterParseError(f"duplicate axis {axis!r} in {filename!r}")
        out[axis] = value
    if not out:
        raise ParameterParseError(f"no axis/value tokens in {filename!r}")
    return out


def infer_axes(filenames: list[str], *, require_all: bool = True) -> list[str]:
    """Infer the shared axis set.

    With require_all=True (the strict default for parametric runs), an axis must
    appear in every filename. If False, axes present in at least half the files
    are returned and the caller must resolve the rest.
    """
    parsed = [parse_tokens(f) for f in filenames]
    counts: dict[str, int] = {}
    for p in parsed:
        for k in p:
            counts[k] = counts.get(k, 0) + 1
    threshold = len(parsed) if require_all else max(1, len(parsed) // 2)
    axes = []
    for k, n in counts.items():
        if n < threshold:
            continue
        # A token that never changes is metadata (device name, take number,
        # etc.), not a captured parameter.
        if len({p[k] for p in parsed}) < 2:
            continue
        axes.append(k)
    if not axes:
        raise ParameterParseError("no varying axis appears in enough filenames")
    return axes


def build_matrix(filenames: list[str], axes: list[str] | None = None) -> tuple[list[AxisSpec], list[Capture]]:
    """Validate every filename and build axis specs + captures."""
    if not filenames:
        raise ParameterParseError("empty capture list")
    axes = axes or infer_axes(filenames, require_all=True)
    parsed = [parse_tokens(f) for f in filenames]

    missing: list[str] = []
    for f, p in zip(filenames, parsed):
        absent = [a for a in axes if a not in p]
        if absent:
            missing.append(f"{Path(f).name}: missing {', '.join(absent)}")
    if missing:
        raise ParameterParseError("incomplete parameter filenames:\n" + "\n".join(missing))

    # Reject duplicate parameter vectors (two captures at the same setting).
    seen: dict[tuple[float, ...], str] = {}
    for f, p in zip(filenames, parsed):
        key = tuple(p[a] for a in axes)
        if key in seen:
            raise ParameterParseError(
                f"duplicate parameter vector {key}: {seen[key]!r} and {Path(f).name!r}"
            )
        seen[key] = Path(f).name

    specs: list[AxisSpec] = []
    for a in axes:
        vals = sorted({p[a] for p in parsed})
        step = None
        if len(vals) > 1:
            diffs = [b - a0 for a0, b in zip(vals, vals[1:]) if b > a0]
            if diffs:
                step = min(diffs)
        specs.append(
            AxisSpec(
                name=a,
                minimum=min(vals),
                maximum=max(vals),
                default=0.5 * (min(vals) + max(vals)),
                step=step,
                values=tuple(vals),
            )
        )

    captures = [
        Capture(path=Path(f), values={a: p[a] for a in axes})
        for f, p in zip(filenames, parsed)
    ]
    return specs, captures


def matrix_rows(specs: list[AxisSpec], captures: list[Capture]) -> list[dict]:
    rows = []
    for c in captures:
        row = {"file": c.path.name}
        for s in specs:
            row[s.name] = c.values[s.name]
        rows.append(row)
    return rows
