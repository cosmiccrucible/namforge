"""WAV I/O and lightweight QC helpers for the NAM pipeline.

``soundfile`` (libsndfile) is used for reading because the capture sets mix
16/24-bit PCM and 32-bit float WAVs; the standard library ``wave`` module does
not decode all of those uniformly.  numpy remains the only numeric dependency.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

import numpy as np
import soundfile as sf

_SUBTYPE_WIDTH = {
    "PCM_S8": 1,
    "PCM_U8": 1,
    "PCM_16": 2,
    "PCM_24": 3,
    "PCM_32": 4,
    "FLOAT": 4,
    "DOUBLE": 8,
}


@dataclass(frozen=True)
class WavInfo:
    path: Path
    sample_rate: int
    channels: int
    frames: int
    sample_width: int
    subtype: str = ""
    format: str = ""

    @property
    def duration(self) -> float:
        return self.frames / float(self.sample_rate)


def wav_info(path: str | Path) -> WavInfo:
    path = Path(path)
    info = sf.info(str(path))
    subtype = str(info.subtype)
    return WavInfo(
        path=path,
        sample_rate=int(info.samplerate),
        channels=int(info.channels),
        frames=int(info.frames),
        sample_width=_SUBTYPE_WIDTH.get(subtype, 0),
        subtype=subtype,
        format=str(info.format),
    )


def read_wav(
    path: str | Path,
    *,
    start: int = 0,
    frames: int | None = None,
    mono: bool = True,
) -> tuple[np.ndarray, WavInfo]:
    """Read a region of a WAV file as float32.

    Args:
        start: first frame.
        frames: number of frames, or None for the rest of the file.
        mono: average channels if True; otherwise return (frames, channels).
    """
    info = wav_info(path)
    with sf.SoundFile(str(info.path)) as f:
        if start:
            f.seek(min(max(0, start), info.frames))
        if frames is None:
            frames = max(0, info.frames - start)
        frames = max(0, min(frames, max(0, info.frames - start)))
        data = f.read(frames, dtype="float32", always_2d=True)
    if info.channels > 1 and mono:
        x = data.mean(axis=1)
    elif info.channels > 1:
        x = data
    else:
        x = data[:, 0]
    return np.ascontiguousarray(x, dtype=np.float32), info


def iter_blocks(
    path: str | Path,
    *,
    block_frames: int = 1 << 20,
    mono: bool = True,
) -> Iterator[np.ndarray]:
    """Stream a WAV in blocks to avoid loading 3 GB of audio at once."""
    info = wav_info(path)
    with sf.SoundFile(str(info.path)) as f:
        while True:
            data = f.read(block_frames, dtype="float32", always_2d=True)
            if len(data) == 0:
                break
            if info.channels > 1 and mono:
                x = data.mean(axis=1)
            elif info.channels > 1:
                x = data
            else:
                x = data[:, 0]
            yield np.ascontiguousarray(x, dtype=np.float32)


def first_active(x: np.ndarray, *, sr: int, threshold: float | None = None) -> int | None:
    """First index of 2 consecutive samples above an adaptive threshold.

    The two-sample rule rejects isolated noise spikes while still catching the
    first sample of a converter/amp step response.
    """
    if threshold is None:
        npre = min(len(x), max(1, int(0.2 * sr)))
        floor = float(np.sqrt(np.mean(np.square(x[:npre], dtype=np.float64)))) if npre else 0.0
        threshold = max(1e-5, floor * 4.0)
    over = np.abs(x) > threshold
    if len(over) < 2:
        return None
    c = np.convolve(over.astype(np.int8), np.ones(2, dtype=np.int8), mode="valid")
    idx = np.flatnonzero(c >= 2)
    return int(idx[0]) if len(idx) else None


def scan_qc(path: str | Path, *, block_frames: int = 1 << 20) -> dict:
    """Full-file basic QC without holding the file in memory."""
    info = wav_info(path)
    peak = 0.0
    sumsq = 0.0
    dc = 0.0
    n = 0
    clips = 0
    finite = True
    for x in iter_blocks(path, block_frames=block_frames):
        if not np.isfinite(x).all():
            finite = False
        peak = max(peak, float(np.max(np.abs(x))) if len(x) else 0.0)
        sumsq += float(np.sum(np.square(x, dtype=np.float64)))
        dc += float(np.sum(x, dtype=np.float64))
        n += int(len(x))
        clips += int(np.sum(np.abs(x) >= 0.999))
    rms = float(np.sqrt(sumsq / n)) if n else 0.0
    return {
        "path": str(info.path),
        "sample_rate": info.sample_rate,
        "channels": info.channels,
        "frames": info.frames,
        "duration": info.duration,
        "sample_width": info.sample_width,
        "subtype": info.subtype,
        "format": info.format,
        "peak": peak,
        "rms": rms,
        "dc": dc / n if n else 0.0,
        "clips": clips,
        "finite": finite,
    }
