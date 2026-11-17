"""Capture validation for NAM Forge.

The NAM trainer (:mod:`nam.data`) loads one mono WAV per capture and applies a
fixed split: 10 s .. -9 s for training and -9 s .. end for validation.  A bad
capture otherwise fails only after training has started, with a trainer
traceback.  This module mirrors the loader's preconditions so the app can
reject a capture set with a specific message first.

``soundfile`` is used instead of the standard library ``wave`` module because
the capture sets in this workspace are 24-bit PCM and 32-bit float WAVs, which
``wave`` does not decode uniformly.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import soundfile as sf

#: Fixed windows used by every NAM Forge run (seconds).
TRAIN_START_SECONDS = 10.0
VALIDATION_SECONDS = 9.0
#: A capture must be longer than this to leave a non-empty 10 s .. -9 s window.
MIN_CAPTURE_SECONDS = TRAIN_START_SECONDS + VALIDATION_SECONDS

#: libsndfile reports "WAVEX" for WAVE_FORMAT_EXTENSIBLE (common for 24-bit).
SUPPORTED_FORMATS = frozenset({"WAV", "WAVEX"})
#: Sample formats the trainer can consume once decoded to float32.
SUPPORTED_SUBTYPES = frozenset({"PCM_16", "PCM_24", "PCM_32", "FLOAT", "DOUBLE"})

_SUBTYPE_WIDTH = {
    "PCM_S8": 1,
    "PCM_U8": 1,
    "PCM_16": 2,
    "PCM_24": 3,
    "PCM_32": 4,
    "FLOAT": 4,
    "DOUBLE": 8,
}


class CaptureValidationError(ValueError):
    """A capture set cannot be used by the trainer; the message is user-facing."""


@dataclass(frozen=True)
class CaptureInfo:
    path: Path
    sample_rate: int
    channels: int
    frames: int
    subtype: str
    format: str

    @property
    def name(self) -> str:
        return self.path.name

    @property
    def duration(self) -> float:
        return self.frames / float(self.sample_rate)

    @property
    def sample_width(self) -> int:
        return _SUBTYPE_WIDTH.get(self.subtype, 0)


@dataclass(frozen=True)
class CaptureSet:
    input: CaptureInfo
    outputs: tuple[CaptureInfo, ...]


def format_rate(sample_rate: int) -> str:
    """Human-readable rate, e.g. 48000 -> '48 kHz', 44100 -> '44.1 kHz'."""
    return f"{sample_rate / 1000.0:g} kHz"


def inspect_audio(path: str | Path) -> CaptureInfo:
    """Read a WAV header with soundfile, raising a specific validation error."""
    path = Path(path)
    name = path.name or str(path)
    if not path.is_file():
        raise CaptureValidationError(f"{name} was not found")
    try:
        info = sf.info(str(path))
    except Exception as exc:  # soundfile raises RuntimeError/sf.LibsndfileError
        raise CaptureValidationError(f"{name} is not a readable WAV: {exc}") from exc
    if info.format not in SUPPORTED_FORMATS:
        raise CaptureValidationError(
            f"{name} is a {info.format or 'unknown'} file; only WAV is supported"
        )
    if info.subtype not in SUPPORTED_SUBTYPES:
        raise CaptureValidationError(
            f"{name} uses {info.subtype}; use 16/24/32-bit PCM or 32/64-bit float"
        )
    if info.frames <= 0:
        raise CaptureValidationError(f"{name} contains no audio frames")
    if info.channels != 1:
        kind = "stereo" if info.channels == 2 else f"{info.channels}-channel"
        raise CaptureValidationError(
            f"{name} is {kind}; the trainer needs mono WAVs"
        )
    if info.samplerate <= 0:
        raise CaptureValidationError(f"{name} has an invalid sample rate ({info.samplerate})")
    return CaptureInfo(
        path=path,
        sample_rate=int(info.samplerate),
        channels=int(info.channels),
        frames=int(info.frames),
        subtype=str(info.subtype),
        format=str(info.format),
    )


def _check_duration(info: CaptureInfo, *, min_seconds: float) -> None:
    if info.duration <= min_seconds:
        raise CaptureValidationError(
            f"{info.name} is {info.duration:.1f} s; needs more than "
            f"{min_seconds:g} s (10 s train + 9 s validation)"
        )


def validate_captures(
    input_path: str | Path,
    outputs: list[str | Path] | tuple[str | Path, ...],
    *,
    min_seconds: float = MIN_CAPTURE_SECONDS,
) -> CaptureSet:
    """Validate an input/output capture set the way the trainer will use it.

    Checks, in order, for the input and every output: readable WAV, supported
    sample format, mono, enough audio for the fixed windows.  Outputs must also
    match the input's sample rate and frame count exactly (the trainer requires
    equal shapes).

    Raises:
        CaptureValidationError: with a message naming the offending file and
            the reason.
    """
    input_info = inspect_audio(input_path)
    _check_duration(input_info, min_seconds=min_seconds)

    if not outputs:
        raise CaptureValidationError("no output captures were found")

    infos: list[CaptureInfo] = []
    for raw in outputs:
        info = inspect_audio(raw)
        if info.sample_rate != input_info.sample_rate:
            raise CaptureValidationError(
                f"{info.name} is {format_rate(info.sample_rate)} but input is "
                f"{format_rate(input_info.sample_rate)}"
            )
        _check_duration(info, min_seconds=min_seconds)
        if info.frames != input_info.frames:
            raise CaptureValidationError(
                f"{info.name} has {info.frames} frames ({info.duration:.2f} s) but "
                f"{input_info.name} has {input_info.frames} frames "
                f"({input_info.duration:.2f} s)"
            )
        infos.append(info)

    return CaptureSet(input=input_info, outputs=tuple(infos))
