"""Local guided NAM Forge app.

Run:
    ..\\.venv\\Scripts\\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8765

The run manifest is the source of truth once a run has been prepared: training
and export read the model type, delay and axes from ``manifest.json`` (not from
the request). Captures are validated with the trainer's own preconditions before
anything is written, and every filesystem path is required to stay under
``runs/`` or ``packs/``.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time
import uuid
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

ROOT = Path(__file__).resolve().parents[1]
RUNS_ROOT = ROOT / "runs"
PACKS_ROOT = ROOT / "packs"
VENV = ROOT.parent / ".venv"
sys.path.insert(0, str(ROOT))

from nam_pipeline.alignment import pair_alignment, recommend_delay  # noqa: E402
from nam_pipeline.audio import read_wav  # noqa: E402
from nam_pipeline.config import RunSpec, write_run  # noqa: E402
from nam_pipeline.parameters import (  # noqa: E402
    AxisSpec,
    Capture,
    ParameterParseError,
    build_matrix,
)
from nam_pipeline.validation import (  # noqa: E402
    CaptureSet,
    CaptureValidationError,
    validate_captures,
)

app = FastAPI(title="NAM Forge")
app.mount("/static", StaticFiles(directory=ROOT / "app" / "static"), name="static")

JOBS: dict[str, dict[str, Any]] = {}

#: Delay used when alignment is absent or not corroborated by two estimators.
DEFAULT_DELAY = 17
#: Sanity bound for a manually supplied or analysed delay.
MAX_DELAY_SAMPLES = 240
#: The five held-out Peavey targets keep the established 32/5 behaviour.
PEAVEY_VALIDATION_TARGETS: tuple[tuple[float, ...], ...] = (
    (0.0, 10.0),
    (4.0, 4.0),
    (6.0, 8.0),
    (8.0, 2.0),
    (10.0, 0.0),
)

_WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL"} | {
    f"COM{i}" for i in range(1, 10)
} | {f"LPT{i}" for i in range(1, 10)}
_ESR_RE = re.compile(r"[_-]ESR=(?P<esr>[0-9]*\.?[0-9]+(?:[eE][+-]?[0-9]+)?)")

_ENV_CACHE: tuple[float, dict[str, Any]] | None = None


class ScanRequest(BaseModel):
    input_path: str
    output_dir: str
    model_type: str = "parametric"


class AnalyzeRequest(ScanRequest):
    max_pairs: int = 24


class PrepareRequest(ScanRequest):
    name: str = "ui_run"
    channels: int = 8
    max_epochs: int = 30
    batch_size: int = 16
    #: Analysed trim in samples. Only honoured when ``delay_confident`` is true.
    delay: int | None = None
    delay_confident: bool = False


class TrainRequest(BaseModel):
    #: Only used to reject a request that contradicts the run manifest.
    run_dir: str
    model_type: str | None = None


class ExportRequest(BaseModel):
    run_dir: str
    name: str
    eco_nam: str | None = None


# --------------------------------------------------------------------------- #
# Validation and parameter helpers
# --------------------------------------------------------------------------- #


def _check_model_type(model_type: str) -> str:
    if model_type not in {"parametric", "static"}:
        raise HTTPException(400, f"unknown model type: {model_type!r}")
    return model_type


def _outputs(req: ScanRequest) -> list[Path]:
    out = Path(req.output_dir)
    if not out.is_dir():
        raise HTTPException(400, f"output folder not found: {out}")
    inp = Path(req.input_path)
    try:
        input_key = inp.resolve()
    except OSError:
        input_key = inp
    outs = [
        p
        for p in sorted(out.glob("*.wav"))
        if p.is_file() and p.resolve() != input_key
    ]
    tagged = [p for p in outs if p.stem.lower().endswith("_out")]
    if tagged:
        outs = tagged
    elif req.model_type == "parametric":
        outs = [p for p in outs if not p.stem.lower().endswith("_in")]
    if req.model_type == "static":
        if len(outs) != 1:
            raise HTTPException(400, "static mode needs exactly one output WAV")
    else:
        if len(outs) < 3:
            raise HTTPException(400, "parametric mode needs at least three output WAVs")
    return outs


def _validated(req: ScanRequest) -> tuple[CaptureSet, list[Path]]:
    """Validate the capture set before scan/analyze/prepare touch anything."""
    inp = Path(req.input_path)
    if not inp.is_file():
        raise HTTPException(400, f"input file not found: {inp}")
    outs = _outputs(req)
    try:
        capture_set = validate_captures(inp, outs)
    except CaptureValidationError as exc:
        raise HTTPException(400, str(exc)) from exc
    return capture_set, outs


def _choose_validation_split(
    specs: tuple[AxisSpec, ...] | list[AxisSpec],
    caps: list[Capture],
) -> tuple[set[tuple[float, ...]], str, list[str]]:
    """Pick the parametric validation split, never leaving train empty.

    The five fixed Peavey targets are the established 32/5 convention for
    two-axis (R/P) capture sets. Single-axis datasets (Kraken's continuous Gain)
    and datasets small enough that the targets would validate away every capture
    fall back to the temporal convention already recorded in
    ``runs/kraken_parametric/data.json``: every capture trains on 10 s .. -9 s
    and validates on -9 s .. end. No unseen-gain split is invented.
    """
    keys = {
        tuple(c.values[a.name] for a in specs): c for c in caps
    }
    if not keys:
        raise HTTPException(400, "run has no captures")

    # The five Peavey targets are (R, P) pairs. A single-axis capture set (e.g.
    # Kraken's continuous Gain) has no second axis to hold out, and inventing an
    # unseen-gain split is explicitly out of scope: use the temporal convention.
    if len(specs) < 2:
        return (
            set(keys),
            "temporal",
            [
                "single-axis dataset: using temporal validation (all captures in "
                "train and validation); the held-out Peavey targets need two axes"
            ],
        )

    def nearest(target: tuple[float, ...]) -> tuple[float, ...]:
        return min(
            keys,
            key=lambda k: sum((v - t) ** 2 for v, t in zip(k, target)),
        )

    held = {nearest(target) for target in PEAVEY_VALIDATION_TARGETS}
    train_keys = set(keys) - held
    if not held or not train_keys:
        return (
            set(keys),
            "temporal",
            [
                "dataset is too small for the five held-out Peavey targets; using "
                "temporal validation (all captures in train and validation)"
            ],
        )
    return held, "points", []


def _validate_delay(delay: int) -> int:
    if isinstance(delay, bool) or not isinstance(delay, int):
        raise HTTPException(400, "delay must be an integer number of samples")
    if not 0 <= delay <= MAX_DELAY_SAMPLES:
        raise HTTPException(
            400, f"delay must be between 0 and {MAX_DELAY_SAMPLES} samples"
        )
    return delay


def _safe_slug(raw: str, kind: str) -> str:
    """Return a filesystem-safe slug, rejecting anything that looks like a path."""
    name = (raw or "").strip()
    if not name:
        raise HTTPException(400, f"{kind} name is required")
    if any(ord(ch) < 32 for ch in name):
        raise HTTPException(400, f"{kind} name contains control characters")
    if name.startswith(("/", "\\")) or re.match(r"^[A-Za-z]:", name):
        raise HTTPException(400, f"{kind} name must not be an absolute path: {name!r}")
    if "/" in name or "\\" in name:
        raise HTTPException(
            400, f"{kind} name must not contain path separators: {name!r}"
        )
    if name in {".", ".."} or ".." in name:
        raise HTTPException(400, f"{kind} name must not contain '..': {name!r}")
    slug = re.sub(r"[^A-Za-z0-9._-]+", "-", name)
    slug = re.sub(r"-{2,}", "-", slug).strip(" .-_")
    if len(slug) > 64:
        slug = slug[:64].rstrip(" .-_")
    if not slug:
        raise HTTPException(400, f"{kind} name has no usable characters: {name!r}")
    if slug.split(".")[0].upper() in _WINDOWS_RESERVED:
        raise HTTPException(400, f"{kind} name is a reserved Windows name: {slug!r}")
    return slug


def _ensure_under(path: Path, root: Path) -> Path:
    resolved = path.resolve()
    root_resolved = root.resolve()
    resolved_key = os.path.normcase(str(resolved))
    root_key = os.path.normcase(str(root_resolved))
    if resolved_key != root_key and not resolved_key.startswith(root_key + os.sep):
        raise HTTPException(400, f"path {resolved} is outside {root_resolved}")
    return resolved


def _run_dir(raw: str) -> Path:
    if not (raw or "").strip():
        raise HTTPException(400, "run_dir is required")
    path = Path(raw)
    if not path.is_absolute():
        path = ROOT / path
    resolved = _ensure_under(path, RUNS_ROOT)
    if not resolved.is_dir():
        raise HTTPException(400, f"run directory not found: {raw}")
    return resolved


def _read_json(path: Path, what: str) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise HTTPException(400, f"cannot read {what} at {path}: {exc}") from exc
    except ValueError as exc:
        raise HTTPException(400, f"{what} at {path} is not valid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise HTTPException(400, f"{what} at {path} must be a JSON object")
    return data


def _read_manifest(run_dir: Path) -> dict:
    manifest_path = run_dir / "manifest.json"
    if not manifest_path.is_file():
        raise HTTPException(
            400,
            "run directory has no manifest.json; re-prepare the run in the app "
            "(legacy run directories without a manifest are not supported)",
        )
    manifest = _read_json(manifest_path, "run manifest")
    model_type = manifest.get("model_type")
    if model_type not in {"parametric", "static"}:
        raise HTTPException(
            400, f"manifest model_type is invalid: {model_type!r}"
        )
    delay = manifest.get("delay", DEFAULT_DELAY)
    if isinstance(delay, bool) or not isinstance(delay, int):
        raise HTTPException(400, f"manifest delay is invalid: {delay!r}")
    manifest["delay"] = delay
    axes = manifest.get("axes")
    if not isinstance(axes, list):
        raise HTTPException(400, "manifest axes are missing or invalid")
    if model_type == "parametric":
        if not axes:
            raise HTTPException(400, "manifest has no axes for a parametric run")
        for axis in axes:
            if not isinstance(axis, dict) or not isinstance(axis.get("name"), str):
                raise HTTPException(400, "manifest contains an invalid axis entry")
    return manifest


def _manifest_axis_names(manifest: dict) -> list[str]:
    return [str(axis["name"]) for axis in manifest.get("axes", [])]


def _check_run_files(run_dir: Path, manifest: dict) -> dict:
    """Check data.json agrees with the manifest and still points at real files."""
    data_path = run_dir / "data.json"
    if not data_path.is_file():
        raise HTTPException(400, "run directory has no data.json")
    data = _read_json(data_path, "data.json")
    model_type = manifest["model_type"]
    common = data.get("common")
    if not isinstance(common, dict):
        raise HTTPException(400, "data.json has no common section")

    if model_type == "parametric":
        if data.get("type") != "parametric":
            raise HTTPException(
                400,
                "data.json is not parametric but the manifest says parametric; "
                "re-prepare the run",
            )
        train = data.get("train")
        validation = data.get("validation")
        if not isinstance(train, list) or not train:
            raise HTTPException(400, "data.json has no training captures")
        if not isinstance(validation, list) or not validation:
            raise HTTPException(400, "data.json has no validation captures")
        axis_names = _manifest_axis_names(manifest)
        for split_name, items in (("train", train), ("validation", validation)):
            for item in items:
                if not isinstance(item, dict):
                    raise HTTPException(
                        400, f"data.json {split_name} entry is not an object"
                    )
                y_path = Path(str(item.get("y_path", "")))
                if not y_path.is_file():
                    raise HTTPException(
                        400, f"data.json {split_name} capture is missing: {y_path}"
                    )
                params = item.get("params")
                if not isinstance(params, dict):
                    raise HTTPException(
                        400, f"data.json {split_name} capture has no params: {y_path.name}"
                    )
                missing = [name for name in axis_names if name not in params]
                if missing:
                    raise HTTPException(
                        400,
                        f"{y_path.name} is missing manifest axis/axes: "
                        f"{', '.join(missing)}",
                    )
    else:
        if data.get("type") == "parametric":
            raise HTTPException(
                400,
                "data.json is parametric but the manifest says static; "
                "re-prepare the run",
            )
        train = data.get("train")
        validation = data.get("validation")
        if not isinstance(train, dict) or not train:
            raise HTTPException(400, "data.json has no training capture")
        if not isinstance(validation, dict) or not validation:
            raise HTTPException(400, "data.json has no validation capture")
        y_path = Path(str(common.get("y_path", "")))
        if not y_path.is_file():
            raise HTTPException(400, f"data.json capture is missing: {y_path}")

    x_path = Path(str(common.get("x_path", "")))
    if not x_path.is_file():
        raise HTTPException(400, f"data.json input WAV is missing: {x_path}")

    data_delay = common.get("delay")
    if isinstance(data_delay, bool) or not isinstance(data_delay, int):
        raise HTTPException(400, f"data.json delay is invalid: {data_delay!r}")
    if data_delay != manifest["delay"]:
        raise HTTPException(
            400,
            f"run is inconsistent: data.json delay is {data_delay} but manifest "
            f"delay is {manifest['delay']}; re-prepare the run",
        )
    return data


# --------------------------------------------------------------------------- #
# Environment preflight
# --------------------------------------------------------------------------- #


def _python_exe() -> Path:
    for name in ("python.exe", "python"):
        candidate = VENV / "Scripts" / name
        if candidate.is_file():
            return candidate
    return Path(sys.executable)


def _env_probe() -> dict[str, Any]:
    """Import torch/nam in the training interpreter once per minute."""
    global _ENV_CACHE
    now = time.monotonic()
    if _ENV_CACHE is not None and now - _ENV_CACHE[0] < 60.0:
        return _ENV_CACHE[1]

    code = (
        "import json\n"
        "out = {}\n"
        "try:\n"
        "    import torch\n"
        "    out['torch'] = True\n"
        "    out['cuda'] = bool(torch.cuda.is_available())\n"
        "except Exception as exc:\n"
        "    out['torch'] = False\n"
        "    out['cuda'] = False\n"
        "    out['torch_error'] = f'{type(exc).__name__}: {exc}'\n"
        "try:\n"
        "    import nam\n"
        "    out['nam'] = True\n"
        "except Exception as exc:\n"
        "    out['nam'] = False\n"
        "    out['nam_error'] = f'{type(exc).__name__}: {exc}'\n"
        "print(json.dumps(out))\n"
    )
    python = _python_exe()
    try:
        proc = subprocess.run(
            [str(python), "-c", code],
            capture_output=True,
            text=True,
            timeout=180,
        )
    except subprocess.TimeoutExpired:
        result: dict[str, Any] = {"error": "import check timed out after 180 s"}
    except OSError as exc:
        result = {"error": f"could not run {python}: {exc}"}
    else:
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout or "").strip()
            result = {
                "error": detail[-1000:]
                or f"import check exited with code {proc.returncode}"
            }
        else:
            try:
                parsed = json.loads(proc.stdout.strip().splitlines()[-1])
            except (ValueError, IndexError):
                result = {"error": f"unparseable import check output: {proc.stdout[-500:]}"}
            else:
                result = parsed if isinstance(parsed, dict) else {"error": "bad probe"}
    _ENV_CACHE = (now, result)
    return result


def _requires_gpu(run_dir: Path) -> bool:
    learning_path = run_dir / "learning.json"
    if not learning_path.is_file():
        raise HTTPException(400, "run directory has no learning.json")
    learning = _read_json(learning_path, "learning.json")
    trainer = learning.get("trainer")
    if not isinstance(trainer, dict):
        raise HTTPException(400, "learning.json has no trainer section")
    accelerator = str(trainer.get("accelerator", "gpu")).lower()
    return accelerator in {"gpu", "cuda"}


def _preflight(run_dir: Path, manifest: dict) -> Path:
    model_type = manifest["model_type"]
    requires_gpu = _requires_gpu(run_dir)
    exe_name = "nam-full-parametric.exe" if model_type == "parametric" else "nam-full.exe"
    exe = VENV / "Scripts" / exe_name
    if not exe.is_file():
        raise HTTPException(
            503,
            f"trainer executable not found at {exe}. Run 'Start NAM Forge.cmd "
            "-ForceInstall' to install the training stack.",
        )

    env = _env_probe()
    if env.get("error"):
        raise HTTPException(
            503,
            "training environment preflight failed: "
            f"{env['error']}. Run 'Start NAM Forge.cmd -ForceInstall'.",
        )
    if not env.get("torch"):
        raise HTTPException(
            503,
            "the training Python cannot import torch "
            f"({env.get('torch_error', 'unknown error')}). Run 'Start NAM "
            "Forge.cmd -ForceInstall'.",
        )
    if not env.get("nam"):
        raise HTTPException(
            503,
            "the training Python cannot import nam "
            f"({env.get('nam_error', 'unknown error')}). Run 'Start NAM "
            "Forge.cmd -ForceInstall'.",
        )
    if requires_gpu and not env.get("cuda"):
        raise HTTPException(
            503,
            "this run is configured for GPU training (learning.json "
            "trainer.accelerator='gpu') but torch.cuda.is_available() is False. "
            "Install a CUDA build of PyTorch or edit learning.json to use the CPU.",
        )
    return exe


# --------------------------------------------------------------------------- #
# Endpoints
# --------------------------------------------------------------------------- #


def _scan(req: ScanRequest) -> dict:
    _check_model_type(req.model_type)
    capture_set, outs = _validated(req)
    info = capture_set.input
    if req.model_type == "static":
        axes, captures = [], []
    else:
        try:
            specs, caps = build_matrix([p.name for p in outs])
        except ParameterParseError as exc:
            raise HTTPException(400, str(exc)) from exc
        axes = [
            {
                "name": a.name,
                "min": a.minimum,
                "max": a.maximum,
                "default": a.default,
                "step": a.step,
            }
            for a in specs
        ]
        captures = [{"file": c.path.name, "values": c.values} for c in caps]
    return {
        "input": str(info.path.resolve()),
        "sample_rate": info.sample_rate,
        "sample_format": info.subtype,
        "duration": info.duration,
        "model_type": req.model_type,
        "axes": axes,
        "captures": captures,
        "output_count": len(outs),
    }


def _analyze(req: AnalyzeRequest) -> dict:
    _check_model_type(req.model_type)
    capture_set, outs = _validated(req)
    info = capture_set.input
    frame_count = min(int(8 * info.sample_rate), info.frames)
    x, _ = read_wav(info.path, frames=frame_count)
    pairs = []
    limit = max(1, min(int(req.max_pairs), 64))
    for p in outs[:limit]:
        y, _ = read_wav(p, frames=frame_count)
        pairs.append(
            pair_alignment(p.stem, x, y, info.sample_rate, max_delay=30, taps=64)
        )
    try:
        trim, evidence = recommend_delay(pairs, max_delay=30)
    except ValueError as exc:
        raise HTTPException(400, f"alignment analysis failed: {exc}") from exc
    confident = bool(evidence.get("confident"))
    return {
        "trim_samples": trim,
        "confident": confident,
        "confidence": evidence.get("confidence", "low"),
        "evidence": evidence,
        "pairs": len(pairs),
        "warnings": list(evidence.get("warnings", [])),
        "fallback_delay": None if confident else DEFAULT_DELAY,
    }


def _prepare(req: PrepareRequest) -> dict:
    _check_model_type(req.model_type)
    capture_set, outs = _validated(req)
    info = capture_set.input

    if req.model_type == "static":
        specs, caps = [], [Capture(path=outs[0], values={})]
    else:
        try:
            specs, caps = build_matrix([p.name for p in outs])
        except ParameterParseError as exc:
            raise HTTPException(400, str(exc)) from exc
        # build_matrix returns names, not the resolved capture paths.
        caps = [
            Capture(path=(Path(req.output_dir) / c.path.name), values=c.values)
            for c in caps
        ]

    warnings: list[str] = []
    if req.model_type == "parametric":
        validation_points, validation_mode, split_warnings = _choose_validation_split(
            specs, caps
        )
        warnings.extend(split_warnings)
    else:
        validation_points, validation_mode = set(), "points"

    # Delay: only trust an analysed value when the analyser said it was
    # corroborated. Otherwise keep the established default (17).
    if req.delay is not None and req.delay_confident:
        delay = _validate_delay(req.delay)
        delay_source = "analysis"
    else:
        delay = DEFAULT_DELAY
        delay_source = "default"
        if req.delay is not None:
            warnings.append(
                f"alignment delay {req.delay} was reported with low confidence; "
                f"using the default delay {DEFAULT_DELAY}"
            )
        else:
            warnings.append(
                f"no alignment analysis supplied; using the default delay {DEFAULT_DELAY}"
            )

    try:
        channels = int(req.channels)
        max_epochs = int(req.max_epochs)
        batch_size = int(req.batch_size)
    except (TypeError, ValueError) as exc:
        raise HTTPException(400, f"invalid model/learning settings: {exc}") from exc
    if channels < 1 or max_epochs < 1 or batch_size < 1:
        raise HTTPException(400, "channels, max_epochs and batch_size must be positive")

    slug = _safe_slug(req.name, "run")
    run = RunSpec(
        name=slug,
        model_type=req.model_type,
        input_path=info.path,
        captures=tuple(caps),
        axes=tuple(specs),
        delay=delay,
        sample_rate=info.sample_rate,
    )

    if req.model_type == "parametric":
        train_count = (
            len(caps)
            if validation_mode == "temporal"
            else len(caps) - len(validation_points)
        )
        validation_count = (
            len(caps) if validation_mode == "temporal" else len(validation_points)
        )
    else:
        train_count = validation_count = 1
    if train_count < 1 or validation_count < 1:
        raise HTTPException(
            400,
            "refusing to prepare a run with no training or no validation captures",
        )

    stamp = time.strftime("%Y%m%d-%H%M%S")
    run_dir = RUNS_ROOT / f"{slug}-{stamp}"
    suffix = 2
    while run_dir.exists():
        run_dir = RUNS_ROOT / f"{slug}-{stamp}-{suffix}"
        suffix += 1
    run_dir = _ensure_under(run_dir, RUNS_ROOT)

    try:
        write_run(
            run,
            run_dir,
            validation_points=validation_points,
            validation_mode=validation_mode,
            delay_source=delay_source,
            warnings=warnings,
            channels=channels,
            max_epochs=max_epochs,
            batch_size=batch_size,
        )
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc

    return {
        "run_dir": str(run_dir),
        "model_type": req.model_type,
        "delay": delay,
        "delay_source": delay_source,
        "warnings": warnings,
        "validation_mode": validation_mode,
        "validation_points": (
            []
            if validation_mode == "temporal"
            else [list(k) for k in sorted(validation_points)]
        ),
        "train_count": train_count,
        "validation_count": validation_count,
        "manifest": str(run_dir / "manifest.json"),
    }


def _log_tail(job: dict[str, Any], limit: int = 2000) -> str:
    try:
        text = Path(job["log"]).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return "(training log unavailable)"
    return text[-limit:] if text else "(training log is empty)"


def _job_for_run(run_dir: Path) -> tuple[str, dict[str, Any]] | None:
    found = None
    for job_id, job in JOBS.items():
        if Path(job["run_dir"]).resolve() == run_dir.resolve():
            found = (job_id, job)
    return found


def _start_training(req: TrainRequest) -> dict:
    run_dir = _run_dir(req.run_dir)
    manifest = _read_manifest(run_dir)
    manifest_type = manifest["model_type"]
    if req.model_type is not None and req.model_type != manifest_type:
        raise HTTPException(
            400,
            f"request model type {req.model_type!r} contradicts manifest "
            f"model type {manifest_type!r}",
        )
    _check_run_files(run_dir, manifest)

    existing = _job_for_run(run_dir)
    if existing is not None:
        job_id, job = existing
        if job["proc"].poll() is None:
            raise HTTPException(
                409, f"training job {job_id} is already running for this run"
            )

    exe = _preflight(run_dir, manifest)

    out_dir = _ensure_under(run_dir / "out", run_dir)
    log = run_dir / "train.log"
    cmd = [
        str(exe),
        str(run_dir / "data.json"),
        str(run_dir / "model.json"),
        str(run_dir / "learning.json"),
        str(out_dir),
        "--no-plots",
    ]
    try:
        log_handle = log.open("w", encoding="utf-8")
    except OSError as exc:
        raise HTTPException(500, f"cannot open training log {log}: {exc}") from exc
    try:
        proc = subprocess.Popen(
            cmd, cwd=str(ROOT), stdout=log_handle, stderr=subprocess.STDOUT
        )
    except OSError as exc:
        log_handle.close()
        raise HTTPException(
            503,
            f"could not start the trainer at {exe}: {exc}. Check the training "
            "environment with 'Start NAM Forge.cmd -ForceInstall'.",
        ) from exc
    finally:
        # The child owns its inherited handle; the server does not need one.
        if not log_handle.closed:
            log_handle.close()

    job_id = uuid.uuid4().hex[:8]
    JOBS[job_id] = {
        "proc": proc,
        "log": log,
        "run_dir": run_dir,
        "started": time.time(),
        "model_type": manifest_type,
        "delay": manifest["delay"],
    }
    return {
        "job": job_id,
        "log": str(log),
        "run_dir": str(run_dir),
        "model_type": manifest_type,
        "delay": manifest["delay"],
    }


def _parse_esr(name: str) -> float | None:
    match = _ESR_RE.search(name)
    if not match:
        return None
    try:
        return float(match.group("esr"))
    except ValueError:
        return None


def _candidate_nams(run_dir: Path) -> list[Path]:
    candidates: list[Path] = []
    for root in sorted(run_dir.glob("out*")):
        if root.is_dir():
            candidates.extend(p for p in root.rglob("*.nam") if p.is_file())
    return candidates


def _select_checkpoint(run_dir: Path, manifest: dict) -> tuple[Path, str]:
    candidates = _candidate_nams(run_dir)
    if not candidates:
        raise HTTPException(
            400, "no trained .nam found under the run's out* directories"
        )

    best_checkpoint = manifest.get("best_checkpoint")
    if best_checkpoint:
        path = Path(str(best_checkpoint))
        if not path.is_absolute():
            path = run_dir / path
        path = _ensure_under(path, run_dir)
        if path.suffix.lower() != ".nam" or not path.is_file():
            raise HTTPException(
                400,
                f"manifest best_checkpoint is not a .nam file under the run: "
                f"{best_checkpoint}",
            )
        return path, "manifest best_checkpoint"

    best_metric = manifest.get("best_esr", manifest.get("best_metric"))
    if isinstance(best_metric, (int, float)) and not isinstance(best_metric, bool):
        tagged = [
            (path, esr)
            for path in candidates
            if (esr := _parse_esr(path.name)) is not None
        ]
        if not tagged:
            raise HTTPException(
                400,
                f"manifest records best_esr={best_metric} but no .nam checkpoint "
                "has an ESR-tagged filename",
            )
        path, esr = min(tagged, key=lambda item: abs(item[1] - best_metric))
        tolerance = max(abs(best_metric) * 0.01, 1e-9)
        if abs(esr - best_metric) > tolerance:
            raise HTTPException(
                400,
                f"manifest records best_esr={best_metric} but the closest "
                f"checkpoint is ESR={esr} ({path.name}); refusing to guess",
            )
        return path, f"manifest best_esr={best_metric}"

    tagged = [
        (path, esr)
        for path in candidates
        if (esr := _parse_esr(path.name)) is not None
    ]
    if tagged:
        path, esr = min(tagged, key=lambda item: item[1])
        return path, f"lowest ESR checkpoint (ESR={esr})"

    finals = [
        path for path in candidates if path.name in {"model_parametric.nam", "model.nam"}
    ]
    pool = finals or candidates
    path = max(pool, key=lambda p: p.stat().st_mtime)
    return path, "final model (no ESR-tagged checkpoint found)"


def _validate_eco(eco_nam: str, standard: Path, manifest: dict) -> Path:
    if manifest.get("model_type") != "parametric":
        raise HTTPException(400, "Eco export is only supported for parametric runs")
    eco = Path(eco_nam)
    if not eco.is_file():
        raise HTTPException(400, f"Eco model not found: {eco_nam}")
    if eco.suffix.lower() != ".nam":
        raise HTTPException(400, f"Eco model must be a .nam file: {eco_nam}")
    eco_data = _read_json(eco, "Eco model")
    if "architecture" not in eco_data or "config" not in eco_data:
        raise HTTPException(
            400,
            f"{eco.name} is not a NAM model (missing architecture/config)",
        )
    standard_data = _read_json(standard, "standard checkpoint")
    eco_rate = eco_data.get("sample_rate", 48000)
    standard_rate = standard_data.get("sample_rate", 48000)
    if eco_rate != standard_rate:
        raise HTTPException(
            400,
            f"Eco model sample rate is {eco_rate} but the standard checkpoint is "
            f"{standard_rate}",
        )
    return eco


def export_pack(req: ExportRequest) -> dict:
    run_dir = _run_dir(req.run_dir)
    manifest = _read_manifest(run_dir)

    existing = _job_for_run(run_dir)
    if existing is not None:
        job_id, job = existing
        code = job["proc"].poll()
        if code is None:
            raise HTTPException(
                409,
                f"training job {job_id} is still running; refusing to export.\n"
                f"Log tail:\n{_log_tail(job)}",
            )
        if code != 0:
            raise HTTPException(
                400,
                f"training job {job_id} failed with exit code {code}; refusing to "
                f"export.\nLog tail:\n{_log_tail(job)}",
            )

    name = _safe_slug(req.name, "pack")
    delay = int(manifest["delay"])
    checkpoint, reason = _select_checkpoint(run_dir, manifest)
    eco = _validate_eco(req.eco_nam, checkpoint, manifest) if req.eco_nam else None

    PACKS_ROOT.mkdir(parents=True, exist_ok=True)
    pack_dir = _ensure_under(PACKS_ROOT / name, PACKS_ROOT)
    python = _python_exe()
    script = "export_quality_pack.py" if eco is not None else "export_pack.py"
    args = [str(python), str(ROOT / "tools" / script), str(checkpoint)]
    if eco is not None:
        args.append(str(eco))
    args.extend(
        ["--name", name, "--out", str(PACKS_ROOT), "--delay", str(delay)]
    )
    try:
        proc = subprocess.run(
            args, cwd=str(ROOT), capture_output=True, text=True, timeout=600
        )
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(504, f"pack export timed out after 600 s: {exc}") from exc
    except OSError as exc:
        raise HTTPException(
            503, f"could not start the pack exporter {python}: {exc}"
        ) from exc
    if proc.returncode != 0:
        detail = ((proc.stdout or "") + (proc.stderr or "")).strip()
        raise HTTPException(
            500,
            f"pack export failed (exit {proc.returncode}):\n{detail[-4000:]}",
        )
    if not pack_dir.is_dir():
        raise HTTPException(
            500, f"exporter reported success but {pack_dir} was not created"
        )
    return {
        "pack": str(pack_dir),
        "checkpoint": str(checkpoint),
        "checkpoint_reason": reason,
        "delay": delay,
        "output": (proc.stdout or "").strip(),
    }


@app.get("/")
def index() -> FileResponse:
    return FileResponse(ROOT / "app" / "static" / "index.html")


@app.get("/api/health")
def health() -> dict:
    return {"ok": True, "root": str(ROOT)}


@app.post("/api/scan")
def scan(req: ScanRequest) -> dict:
    return _scan(req)


@app.post("/api/analyze")
def analyze(req: AnalyzeRequest) -> dict:
    return _analyze(req)


@app.post("/api/prepare")
def prepare(req: PrepareRequest) -> dict:
    return _prepare(req)


@app.post("/api/train")
def train(req: TrainRequest) -> dict:
    return _start_training(req)


@app.get("/api/train/{job}")
def train_status(job: str) -> dict:
    if job not in JOBS:
        raise HTTPException(404, "unknown job")
    j = JOBS[job]
    proc = j["proc"]
    code = proc.poll()
    return {
        "job": job,
        "running": code is None,
        "exit_code": code,
        "run_dir": str(j.get("run_dir", "")),
        "log": _log_tail(j, limit=4000),
    }


@app.post("/api/export")
def export(req: ExportRequest) -> dict:
    return export_pack(req)
