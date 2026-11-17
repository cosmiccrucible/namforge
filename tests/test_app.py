"""End-to-end tests for the guided NAM Forge app.

All training and export subprocesses are mocked; no real trainer or exporter is
started. Synthetic WAV fixtures are written with soundfile so the 24-bit PCM and
32-bit float paths are exercised exactly as the real captures are.
"""

from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest
import soundfile as sf
from fastapi.testclient import TestClient

import app.main as main

SR = 48000
LONG_SECONDS = 20.0


# --------------------------------------------------------------------------- #
# Fixtures and helpers
# --------------------------------------------------------------------------- #


def write_wav(
    path: Path,
    *,
    seconds: float = LONG_SECONDS,
    sample_rate: int = SR,
    subtype: str = "PCM_24",
    channels: int = 1,
    seed: int = 1,
) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = int(round(seconds * sample_rate))
    rng = np.random.default_rng(seed)
    data = (0.05 * rng.standard_normal((frames, channels))).astype(np.float32)
    sf.write(str(path), data, sample_rate, subtype=subtype)
    return path


@pytest.fixture()
def roots(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    runs = tmp_path / "runs"
    packs = tmp_path / "packs"
    runs.mkdir()
    packs.mkdir()
    monkeypatch.setattr(main, "RUNS_ROOT", runs)
    monkeypatch.setattr(main, "PACKS_ROOT", packs)
    monkeypatch.setattr(main, "_ENV_CACHE", None)
    main.JOBS.clear()
    yield runs, packs
    main.JOBS.clear()


@pytest.fixture()
def client(roots) -> TestClient:
    return TestClient(main.app)


def static_dataset(tmp_path: Path, *, out_subtype: str = "PCM_24", out_seconds: float = LONG_SECONDS,
                   out_rate: int = SR) -> tuple[Path, Path]:
    inp = write_wav(tmp_path / "DI_In.wav")
    out_dir = tmp_path / "captures"
    out_dir.mkdir()
    write_wav(out_dir / "Amp_Out.wav", subtype=out_subtype, seconds=out_seconds, sample_rate=out_rate)
    return inp, out_dir


def gain_dataset(tmp_path: Path) -> tuple[Path, Path]:
    inp = write_wav(tmp_path / "DI_In.wav")
    out_dir = tmp_path / "captures"
    out_dir.mkdir()
    for i, label in enumerate(("Min", "Mid", "Full")):
        write_wav(out_dir / f"Amp-Gain-{label}_Out.wav", seed=i + 2)
    return inp, out_dir


def aligned_dataset(tmp_path: Path, *, delay: int = 23) -> tuple[Path, Path]:
    """Three captures with two isolated calibration pulses and a known chain delay."""
    sample_rate = SR
    frames = int(LONG_SECONDS * sample_rate)
    x = np.zeros(frames, dtype=np.float32)
    for pulse in (int(0.25 * sample_rate), int(0.75 * sample_rate)):
        x[pulse : pulse + 8] = 0.5
    rng = np.random.default_rng(3)
    music = 0.1 * rng.standard_normal(int(8 * sample_rate)).astype(np.float32)
    start = int(2 * sample_rate)
    x[start : start + len(music)] += music
    y = np.zeros(frames, dtype=np.float32)
    y[delay:] = 0.7 * x[:-delay]
    inp = tmp_path / "DI_In.wav"
    sf.write(str(inp), x, sample_rate, subtype="PCM_24")
    out_dir = tmp_path / "captures"
    out_dir.mkdir()
    for i, label in enumerate(("Min", "Mid", "Full")):
        sf.write(
            str(out_dir / f"Amp-Gain-{label}_Out.wav"),
            y * (1.0 + 0.001 * i),
            sample_rate,
            subtype="FLOAT",
        )
    return inp, out_dir


def prepare_payload(inp: Path, out_dir: Path, **extra) -> dict:
    payload = {
        "input_path": str(inp),
        "output_dir": str(out_dir),
        "model_type": "parametric",
        "name": "test_run",
        "channels": 4,
        "max_epochs": 1,
        "batch_size": 4,
    }
    payload.update(extra)
    return payload


def fake_run_dir(runs: Path, name: str = "fake_run", *, model_type: str = "parametric",
                 delay: int = 17, manifest_extra: dict | None = None) -> Path:
    run_dir = runs / name
    run_dir.mkdir(parents=True)
    capture = run_dir / "capture.wav"
    capture.write_bytes(b"")
    inp = run_dir / "input.wav"
    inp.write_bytes(b"")
    if model_type == "parametric":
        data = {
            "type": "parametric",
            "common": {"x_path": str(inp), "delay": delay},
            "train": [{"y_path": str(capture), "params": {"Gain": 0.0}}],
            "validation": [{"y_path": str(capture), "params": {"Gain": 0.0}}],
        }
        axes = [{"name": "Gain", "min": 0.0, "max": 10.0, "default": 5.0}]
    else:
        data = {
            "train": {"start_seconds": 10.0, "stop_seconds": -9.0, "ny": 8192},
            "validation": {"start_seconds": -9.0, "stop_seconds": None, "ny": None},
            "common": {"x_path": str(inp), "y_path": str(capture), "delay": delay},
        }
        axes = []
    (run_dir / "data.json").write_text(json.dumps(data), encoding="utf-8")
    (run_dir / "learning.json").write_text(
        json.dumps({"trainer": {"accelerator": "gpu"}}), encoding="utf-8"
    )
    manifest = {
        "name": name,
        "model_type": model_type,
        "sample_rate": SR,
        "delay": delay,
        "axes": axes,
    }
    if manifest_extra:
        manifest.update(manifest_extra)
    (run_dir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return run_dir


class FakeProc:
    def __init__(self, returncode: int | None = None) -> None:
        self._returncode = returncode

    def poll(self) -> int | None:
        return self._returncode


def fake_venv_with_trainer(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, *,
                           parametric: bool = True) -> Path:
    venv = tmp_path / "venv"
    scripts = venv / "Scripts"
    scripts.mkdir(parents=True)
    (scripts / ("nam-full-parametric.exe" if parametric else "nam-full.exe")).write_bytes(b"")
    monkeypatch.setattr(main, "VENV", venv)
    return venv


# --------------------------------------------------------------------------- #
# Capture validation
# --------------------------------------------------------------------------- #


@pytest.mark.parametrize("subtype", ["PCM_24", "FLOAT"])
def test_24bit_and_float32_captures_accepted(client, tmp_path, subtype):
    inp, out_dir = static_dataset(tmp_path, out_subtype=subtype)
    response = client.post(
        "/api/scan",
        json={"input_path": str(inp), "output_dir": str(out_dir), "model_type": "static"},
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["sample_rate"] == SR
    assert body["output_count"] == 1


def test_44k_output_rejected_with_filename(client, tmp_path):
    inp, out_dir = static_dataset(tmp_path, out_rate=44100)
    response = client.post(
        "/api/scan",
        json={"input_path": str(inp), "output_dir": str(out_dir), "model_type": "static"},
    )
    assert response.status_code == 400, response.text
    detail = response.json()["detail"]
    assert "Amp_Out.wav" in detail
    assert "44.1 kHz" in detail
    assert "48 kHz" in detail


def test_10_second_capture_rejected(client, tmp_path):
    inp, out_dir = static_dataset(tmp_path, out_seconds=10.0)
    response = client.post(
        "/api/scan",
        json={"input_path": str(inp), "output_dir": str(out_dir), "model_type": "static"},
    )
    assert response.status_code == 400, response.text
    detail = response.json()["detail"]
    assert "Amp_Out.wav" in detail
    assert "10.0 s" in detail
    assert "19" in detail


def test_stereo_output_rejected(client, tmp_path):
    inp = write_wav(tmp_path / "DI_In.wav")
    out_dir = tmp_path / "captures"
    out_dir.mkdir()
    write_wav(out_dir / "Amp_Out.wav", channels=2)
    response = client.post(
        "/api/scan",
        json={"input_path": str(inp), "output_dir": str(out_dir), "model_type": "static"},
    )
    assert response.status_code == 400, response.text
    assert "stereo" in response.json()["detail"]


def test_unequal_frames_rejected(client, tmp_path):
    inp = write_wav(tmp_path / "DI_In.wav")
    out_dir = tmp_path / "captures"
    out_dir.mkdir()
    # 20 s at 48 kHz vs 20.5 s: same rate, different frame count.
    write_wav(out_dir / "Amp_Out.wav", seconds=20.5)
    response = client.post(
        "/api/scan",
        json={"input_path": str(inp), "output_dir": str(out_dir), "model_type": "static"},
    )
    assert response.status_code == 400, response.text
    detail = response.json()["detail"]
    assert "Amp_Out.wav" in detail
    assert "frames" in detail


def test_prepare_revalidates_captures(client, tmp_path):
    """A stale scan must not let an invalid capture reach prepare."""
    inp, out_dir = static_dataset(tmp_path, out_seconds=10.0)
    response = client.post(
        "/api/prepare",
        json=prepare_payload(inp, out_dir, model_type="static", name="short_run"),
    )
    assert response.status_code == 400, response.text
    assert "19" in response.json()["detail"]


def test_static_prepare_and_static_trainer_selected(client, roots, tmp_path, monkeypatch):
    runs, _ = roots
    inp, out_dir = static_dataset(tmp_path)
    prepare = client.post(
        "/api/prepare",
        json=prepare_payload(inp, out_dir, model_type="static", name="static_run"),
    )
    assert prepare.status_code == 200, prepare.text
    body = prepare.json()
    assert body["model_type"] == "static"
    assert body["train_count"] == body["validation_count"] == 1
    run_dir = Path(body["run_dir"])
    data = json.loads((run_dir / "data.json").read_text(encoding="utf-8"))
    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
    assert data["common"]["delay"] == manifest["delay"] == 17
    assert Path(data["common"]["y_path"]).name == "Amp_Out.wav"

    fake_venv_with_trainer(tmp_path, monkeypatch, parametric=False)
    monkeypatch.setattr(
        main, "_env_probe", lambda: {"torch": True, "nam": True, "cuda": True}
    )
    calls: dict[str, list] = {}

    def fake_popen(cmd, **kwargs):
        calls["cmd"] = cmd
        return FakeProc(None)

    monkeypatch.setattr(main.subprocess, "Popen", fake_popen)
    train = client.post("/api/train", json={"run_dir": str(run_dir)})
    assert train.status_code == 200, train.text
    assert calls["cmd"][0].endswith("nam-full.exe")


# --------------------------------------------------------------------------- #
# Split policy
# --------------------------------------------------------------------------- #


def test_small_dataset_prepare_keeps_train_and_validation(client, roots, tmp_path):
    runs, _ = roots
    inp, out_dir = gain_dataset(tmp_path)
    response = client.post("/api/prepare", json=prepare_payload(inp, out_dir, name="kraken_small"))
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["train_count"] >= 1
    assert body["validation_count"] >= 1
    assert body["validation_mode"] == "temporal"

    run_dir = Path(body["run_dir"])
    assert run_dir.parent == runs
    data = json.loads((run_dir / "data.json").read_text(encoding="utf-8"))
    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
    assert len(data["train"]) == 3
    assert len(data["validation"]) == 3
    assert {item["y_path"] for item in data["train"]} == {
        item["y_path"] for item in data["validation"]
    }
    assert all(item["start_seconds"] == 10.0 for item in data["train"])
    assert all(item["start_seconds"] == -9.0 for item in data["validation"])
    assert manifest["split"]["mode"] == "temporal_all"
    assert manifest["split"]["train_count"] == manifest["split"]["validation_count"] == 3


def test_peavey_split_policy_preserved():
    from nam_pipeline.parameters import build_matrix

    names = [f"Amp-R{r}P{p}_Out.wav" for r in range(0, 11, 2) for p in range(0, 11, 2)]
    # 36 captures: six R values x six P values (the real set has 37 incl. R5P5).
    specs, caps = build_matrix(names)
    points, mode, warnings = main._choose_validation_split(specs, caps)
    assert mode == "points"
    assert warnings == []
    nearest = {(0.0, 10.0), (4.0, 4.0), (6.0, 8.0), (8.0, 2.0), (10.0, 0.0)}
    assert points == nearest
    assert len(caps) - len(points) >= 1


# --------------------------------------------------------------------------- #
# Delay propagation and confidence gate
# --------------------------------------------------------------------------- #


def test_delay_propagates_to_data_and_manifest(client, roots, tmp_path):
    runs, _ = roots
    inp, out_dir = gain_dataset(tmp_path)
    response = client.post(
        "/api/prepare",
        json=prepare_payload(inp, out_dir, name="delay_run", delay=23, delay_confident=True),
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["delay"] == 23
    assert body["delay_source"] == "analysis"
    run_dir = Path(body["run_dir"])
    data = json.loads((run_dir / "data.json").read_text(encoding="utf-8"))
    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
    assert data["common"]["delay"] == 23
    assert manifest["delay"] == 23
    assert manifest["delay_source"] == "analysis"


def test_analyzed_delay_flows_through_prepare(client, roots, tmp_path):
    inp, out_dir = aligned_dataset(tmp_path, delay=23)
    analysis = client.post(
        "/api/analyze",
        json={"input_path": str(inp), "output_dir": str(out_dir), "model_type": "parametric"},
    )
    assert analysis.status_code == 200, analysis.text
    result = analysis.json()
    assert result["confident"] is True, result
    assert result["trim_samples"] == 23
    assert result["fallback_delay"] is None

    prepare = client.post(
        "/api/prepare",
        json=prepare_payload(
            inp,
            out_dir,
            name="analysed_run",
            delay=result["trim_samples"],
            delay_confident=result["confident"],
        ),
    )
    assert prepare.status_code == 200, prepare.text
    body = prepare.json()
    assert body["delay"] == 23
    assert body["delay_source"] == "analysis"
    data = json.loads((Path(body["run_dir"]) / "data.json").read_text(encoding="utf-8"))
    manifest = json.loads((Path(body["run_dir"]) / "manifest.json").read_text(encoding="utf-8"))
    assert data["common"]["delay"] == manifest["delay"] == 23


def test_low_confidence_zero_delay_falls_back_to_17(client, roots, tmp_path):
    inp, out_dir = gain_dataset(tmp_path)
    response = client.post(
        "/api/prepare",
        json=prepare_payload(inp, out_dir, name="fallback_run", delay=0, delay_confident=False),
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["delay"] == 17
    assert body["delay_source"] == "default"
    assert any("low confidence" in warning for warning in body["warnings"])
    run_dir = Path(body["run_dir"])
    data = json.loads((run_dir / "data.json").read_text(encoding="utf-8"))
    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
    assert data["common"]["delay"] == manifest["delay"] == 17


def test_absent_analysis_defaults_to_17(client, tmp_path):
    inp, out_dir = gain_dataset(tmp_path)
    response = client.post("/api/prepare", json=prepare_payload(inp, out_dir, name="no_analysis"))
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["delay"] == 17
    assert any("no alignment analysis" in warning for warning in body["warnings"])


# --------------------------------------------------------------------------- #
# Manifest source of truth, preflight
# --------------------------------------------------------------------------- #


def test_missing_trainer_returns_503(client, roots, tmp_path, monkeypatch):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    monkeypatch.setattr(main, "VENV", tmp_path / "missing_venv")
    response = client.post("/api/train", json={"run_dir": str(run_dir)})
    assert response.status_code == 503, response.text
    detail = response.json()["detail"]
    assert "trainer executable not found" in detail
    assert "ForceInstall" in detail


def test_request_model_type_contradicting_manifest_rejected(client, roots, tmp_path, monkeypatch):
    runs, _ = roots
    run_dir = fake_run_dir(runs, model_type="parametric")
    fake_venv_with_trainer(tmp_path, monkeypatch)
    response = client.post(
        "/api/train", json={"run_dir": str(run_dir), "model_type": "static"}
    )
    assert response.status_code == 400, response.text
    assert "contradicts manifest" in response.json()["detail"]


def test_cuda_required_but_unavailable_returns_503(client, roots, tmp_path, monkeypatch):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    fake_venv_with_trainer(tmp_path, monkeypatch)
    monkeypatch.setattr(
        main, "_env_probe", lambda: {"torch": True, "nam": True, "cuda": False}
    )
    response = client.post("/api/train", json={"run_dir": str(run_dir)})
    assert response.status_code == 503, response.text
    detail = response.json()["detail"]
    assert "cuda" in detail.lower()
    assert "GPU" in detail


def test_missing_torch_returns_503(client, roots, tmp_path, monkeypatch):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    fake_venv_with_trainer(tmp_path, monkeypatch)
    monkeypatch.setattr(
        main,
        "_env_probe",
        lambda: {
            "torch": False,
            "torch_error": "No module named 'torch'",
            "nam": False,
            "cuda": False,
        },
    )
    response = client.post("/api/train", json={"run_dir": str(run_dir)})
    assert response.status_code == 503, response.text
    assert "cannot import torch" in response.json()["detail"]


def test_training_uses_manifest_and_mocked_popen(client, roots, tmp_path, monkeypatch):
    runs, _ = roots
    run_dir = fake_run_dir(runs, delay=23)
    fake_venv_with_trainer(tmp_path, monkeypatch)
    monkeypatch.setattr(
        main, "_env_probe", lambda: {"torch": True, "nam": True, "cuda": True}
    )
    calls: dict[str, list] = {}

    def fake_popen(cmd, **kwargs):
        calls["cmd"] = cmd
        calls["kwargs"] = kwargs
        return FakeProc(None)

    monkeypatch.setattr(main.subprocess, "Popen", fake_popen)
    # "channels" is a legacy request field; it must be ignored, not required.
    response = client.post(
        "/api/train", json={"run_dir": str(run_dir), "channels": 8}
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["model_type"] == "parametric"
    assert body["delay"] == 23
    assert body["job"] in main.JOBS
    assert calls["cmd"][0].endswith("nam-full-parametric.exe")
    assert calls["cmd"][1] == str(run_dir / "data.json")
    assert "--no-plots" in calls["cmd"]


def test_run_dir_traversal_rejected(client, roots, tmp_path):
    outside = tmp_path / "outside_run"
    outside.mkdir()
    response = client.post("/api/train", json={"run_dir": str(outside)})
    assert response.status_code == 400, response.text
    assert "outside" in response.json()["detail"]
    response = client.post(
        "/api/export", json={"run_dir": str(outside), "name": "pack"}
    )
    assert response.status_code == 400, response.text
    assert not any(pack for pack in (tmp_path / "packs").iterdir())


# --------------------------------------------------------------------------- #
# Export integrity
# --------------------------------------------------------------------------- #


def test_export_during_running_job_returns_409(client, roots, tmp_path):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    log = run_dir / "train.log"
    log.write_text("epoch 3: training still running\n", encoding="utf-8")
    main.JOBS["running1"] = {
        "proc": FakeProc(None),
        "log": log,
        "run_dir": run_dir,
        "started": 0.0,
    }
    response = client.post(
        "/api/export", json={"run_dir": str(run_dir), "name": "blocked_pack"}
    )
    assert response.status_code == 409, response.text
    detail = response.json()["detail"]
    assert "running1" in detail
    assert "training still running" in detail


def test_export_after_failed_job_returns_400(client, roots, tmp_path):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    log = run_dir / "train.log"
    log.write_text("Traceback: CUDA out of memory\n", encoding="utf-8")
    main.JOBS["failed1"] = {
        "proc": FakeProc(2),
        "log": log,
        "run_dir": run_dir,
        "started": 0.0,
    }
    response = client.post(
        "/api/export", json={"run_dir": str(run_dir), "name": "blocked_pack"}
    )
    assert response.status_code == 400, response.text
    detail = response.json()["detail"]
    assert "failed1" in detail
    assert "CUDA out of memory" in detail


def test_export_selects_lowest_esr_checkpoint_and_passes_delay(client, roots, tmp_path, monkeypatch):
    runs, packs = roots
    run_dir = fake_run_dir(runs, delay=23)
    session = run_dir / "out-standard" / "2026-01-01-00-00-00"
    checkpoints = session / "lightning_logs" / "version_0" / "checkpoints"
    checkpoints.mkdir(parents=True)
    worse = checkpoints / "epoch=0010_step=1_ESR=5.000e-03_parametric.nam"
    best = checkpoints / "epoch=0020_step=2_ESR=3.975e-03_parametric.nam"
    final = session / "model_parametric.nam"
    for path in (worse, best, final):
        path.write_text("{}", encoding="utf-8")

    calls: dict[str, list] = {}

    def fake_run(args, **kwargs):
        calls["args"] = args
        (packs / "best_pack").mkdir(parents=True, exist_ok=True)
        return SimpleNamespace(returncode=0, stdout="wrote pack\n", stderr="")

    monkeypatch.setattr(main.subprocess, "run", fake_run)
    response = client.post(
        "/api/export", json={"run_dir": str(run_dir), "name": "best_pack"}
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert Path(body["checkpoint"]) == best
    assert "lowest ESR" in body["checkpoint_reason"]
    assert body["delay"] == 23
    args = calls["args"]
    assert args[1].endswith("export_pack.py")
    assert args[2] == str(best)
    assert "--delay" in args and args[args.index("--delay") + 1] == "23"
    assert "--out" in args and args[args.index("--out") + 1] == str(packs)
    assert body["pack"] == str(packs / "best_pack")


def test_export_honours_manifest_best_checkpoint(client, roots, tmp_path, monkeypatch):
    runs, packs = roots
    session = "out/session"
    run_dir = fake_run_dir(
        runs, manifest_extra={"best_checkpoint": f"{session}/chosen.nam"}
    )
    chosen = run_dir / session / "chosen.nam"
    chosen.parent.mkdir(parents=True)
    chosen.write_text("{}", encoding="utf-8")
    other = run_dir / session / "epoch=0099_ESR=1.000e-04_parametric.nam"
    other.write_text("{}", encoding="utf-8")

    def fake_run(args, **kwargs):
        (packs / "chosen_pack").mkdir(parents=True, exist_ok=True)
        return SimpleNamespace(returncode=0, stdout="ok\n", stderr="")

    monkeypatch.setattr(main.subprocess, "run", fake_run)
    response = client.post(
        "/api/export", json={"run_dir": str(run_dir), "name": "chosen_pack"}
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert Path(body["checkpoint"]) == chosen
    assert "manifest best_checkpoint" in body["checkpoint_reason"]


def test_export_honours_manifest_best_metric(client, roots, tmp_path, monkeypatch):
    runs, packs = roots
    run_dir = fake_run_dir(runs, manifest_extra={"best_esr": 5.0e-3})
    session = run_dir / "out" / "session"
    checkpoints = session / "lightning_logs" / "version_0" / "checkpoints"
    checkpoints.mkdir(parents=True)
    documented = checkpoints / "epoch=0010_ESR=5.000e-03_parametric.nam"
    newer_better = checkpoints / "epoch=0020_ESR=3.000e-03_parametric.nam"
    for path in (documented, newer_better):
        path.write_text("{}", encoding="utf-8")

    def fake_run(args, **kwargs):
        (packs / "metric_pack").mkdir(parents=True, exist_ok=True)
        return SimpleNamespace(returncode=0, stdout="ok\n", stderr="")

    monkeypatch.setattr(main.subprocess, "run", fake_run)
    response = client.post(
        "/api/export", json={"run_dir": str(run_dir), "name": "metric_pack"}
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert Path(body["checkpoint"]) == documented
    assert "best_esr" in body["checkpoint_reason"]


def test_missing_eco_model_rejected(client, roots, tmp_path):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    session = run_dir / "out" / "session"
    session.mkdir(parents=True)
    (session / "model_parametric.nam").write_text("{}", encoding="utf-8")
    response = client.post(
        "/api/export",
        json={"run_dir": str(run_dir), "name": "eco_pack", "eco_nam": str(tmp_path / "nope.nam")},
    )
    assert response.status_code == 400, response.text
    assert "Eco model not found" in response.json()["detail"]


def test_invalid_eco_file_rejected(client, roots, tmp_path):
    runs, _ = roots
    run_dir = fake_run_dir(runs)
    session = run_dir / "out" / "session"
    session.mkdir(parents=True)
    (session / "model_parametric.nam").write_text("{}", encoding="utf-8")
    fake_eco = tmp_path / "fake.nam"
    fake_eco.write_text("not a nam model", encoding="utf-8")
    response = client.post(
        "/api/export",
        json={"run_dir": str(run_dir), "name": "eco_pack", "eco_nam": str(fake_eco)},
    )
    assert response.status_code == 400, response.text
    assert "not valid JSON" in response.json()["detail"]


# --------------------------------------------------------------------------- #
# Path safety
# --------------------------------------------------------------------------- #


@pytest.mark.parametrize("bad_name", ["..\\evil", "../evil", "D:\\evil", "/evil", ".."])
def test_traversal_run_name_rejected_without_writing(client, roots, tmp_path, bad_name):
    runs, _ = roots
    inp, out_dir = gain_dataset(tmp_path)
    response = client.post(
        "/api/prepare", json=prepare_payload(inp, out_dir, name=bad_name)
    )
    assert response.status_code == 400, response.text
    assert "name" in response.json()["detail"]
    assert list(runs.iterdir()) == []
    assert not (tmp_path / "evil").exists()
    assert not (tmp_path.parent / "evil").exists()


def test_traversal_pack_name_rejected(client, roots):
    runs, packs = roots
    run_dir = fake_run_dir(runs)
    response = client.post(
        "/api/export", json={"run_dir": str(run_dir), "name": "..\\evil"}
    )
    assert response.status_code == 400, response.text
    assert list(packs.iterdir()) == []


def test_run_name_is_slugged(client, roots, tmp_path):
    runs, _ = roots
    inp, out_dir = gain_dataset(tmp_path)
    response = client.post(
        "/api/prepare", json=prepare_payload(inp, out_dir, name="My Run (v2)")
    )
    assert response.status_code == 200, response.text
    run_dir = Path(response.json()["run_dir"])
    assert run_dir.name.startswith("My-Run-v2-")
    assert run_dir.parent == runs
