"""Latency estimation for input/output capture pairs.

The estimator is deliberately layered:

* adaptive onset detection (good for a clean leading edge),
* normalized envelope cross-correlation (robust to polarity/nonlinearity),
* causal FIR system identification over a short segment (handles the amp's
  phase response and gives a residual error we can compare across candidates).

No single method is trusted alone. The CLI aggregates per-pair error curves and
reports a consensus integer trim plus confidence.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

import numpy as np

from .audio import first_active


@dataclass
class FirFit:
    delay: int
    rel_error: float
    coefficients: np.ndarray


@dataclass
class PairAlignment:
    name: str
    sr: int
    input_onset: int | None
    output_onset: int | None
    onset_delay: int | None
    envelope_delay: int | None
    fir_delay: int | None
    fir_rel_error: float | None
    pulse_starts: list[int] = field(default_factory=list)
    pulse_delays: list[int] = field(default_factory=list)
    pulse_delay: int | None = None
    fir_curve: dict[int, float] = field(default_factory=dict)
    notes: list[str] = field(default_factory=list)


def _rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64)))) if len(x) else 0.0


def _noise_floor(x: np.ndarray, sr: int) -> float:
    npre = min(len(x), max(1, int(0.2 * sr)))
    return float(np.sqrt(np.mean(np.square(x[:npre], dtype=np.float64)))) if npre else 0.0


def detect_pulses(
    x: np.ndarray, sr: int, *, max_pulses: int = 2, silence_s: float = 0.3
) -> list[int]:
    """Detect short isolated high-amplitude runs (calibration blips).

    The example capture has two 8-sample 0.5-amplitude pulses before the DI
    signal. Their response is a clean, repeatable way to find the true chain
    delay, independent of the amp's phase response. A pulse must be followed by
    a real silence gap, which rejects note transients in the music.
    """
    if len(x) < 64:
        return []
    noise = _noise_floor(x, sr)
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return []
    threshold = max(10.0 * noise, 0.05 * peak)
    over = np.abs(x) > threshold
    # Find runs of consecutive samples.
    padded = np.concatenate(([0], over.astype(np.int8), [0]))
    changes = np.diff(padded)
    starts = np.flatnonzero(changes == 1)
    ends = np.flatnonzero(changes == -1)
    # The DI/music begins at the first sustained active run; anything before it
    # is a candidate calibration blip.
    long_runs = [
        int(s)
        for s, e in zip(starts, ends)
        if (e - s) > max(64, int(0.1 * sr))
    ]
    music_start = long_runs[0] if long_runs else len(x)
    pulses: list[int] = []
    silence = max(64, int(silence_s * sr))
    for s, e in zip(starts, ends):
        s = int(s)
        e = int(e)
        if s >= music_start:
            break
        length = e - s
        if not (2 <= length <= 64):
            continue
        tail_end = min(len(x), e + silence)
        if tail_end > e and float(np.max(np.abs(x[e:tail_end]))) >= threshold:
            continue
        pulses.append(s)
        if len(pulses) >= max_pulses:
            break
    return pulses


def estimate_pulse_delays(
    x: np.ndarray,
    y: np.ndarray,
    sr: int,
    *,
    max_delay: int = 40,
    search_extra: int = 256,
) -> tuple[list[int], list[int]]:
    """Return (pulse starts, delay from pulse start to output response onset)."""
    pulses = detect_pulses(x, sr)
    if not pulses:
        return [], []
    noise = _noise_floor(y, sr)
    threshold = max(1e-5, 4.0 * noise)
    delays: list[int] = []
    for p in pulses:
        end = min(len(y), p + max_delay + search_extra)
        if end <= p + 2:
            continue
        # The response to an isolated calibration pulse must start from a quiet
        # output. If the output is already active just before the pulse, the
        # candidate is a note transient inside continuous audio, and timing its
        # onset measures nothing (it reports 0 or a negative offset). Ignore it.
        pre = y[max(0, p - 8) : p]
        if len(pre) and float(np.max(np.abs(pre))) >= threshold:
            continue
        onset = first_active(y[p:end], sr=sr, threshold=threshold)
        if onset is None or onset <= 0:
            continue
        delays.append(int(onset))
    return pulses, delays


def estimate_onset_delay(
    x: np.ndarray, y: np.ndarray, sr: int
) -> tuple[int | None, int | None, int | None]:
    xi = first_active(x, sr=sr)
    yo = first_active(y, sr=sr)
    if xi is None or yo is None:
        return xi, yo, None
    return xi, yo, int(yo - xi)


def _smooth_envelope(x: np.ndarray, win: int = 64) -> np.ndarray:
    # RMS-ish envelope; mean is removed before correlation.
    e = np.sqrt(np.convolve(np.square(x, dtype=np.float64), np.ones(win) / win, mode="same"))
    e = e - float(np.mean(e))
    return e.astype(np.float64)


def estimate_envelope_delay(
    x: np.ndarray,
    y: np.ndarray,
    *,
    sr: int,
    start: int,
    max_lag: int = 80,
    window_s: float = 1.0,
) -> int | None:
    n = min(int(window_s * sr), len(x) - start - max_lag, len(y) - start - max_lag)
    if n < 1024:
        return None
    a = _smooth_envelope(x[start : start + n])
    b = _smooth_envelope(y[start : start + n])
    denom = float(np.sqrt(np.dot(a, a) * np.dot(b, b)))
    if denom <= 1e-20:
        return None
    best_lag = 0
    best = -np.inf
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            aa = a[: n - lag]
            bb = b[lag:n]
        else:
            aa = a[-lag:n]
            bb = b[: n + lag]
        if len(aa) < 64:
            continue
        c = float(np.dot(aa, bb) / denom)
        if c > best:
            best = c
            best_lag = lag
    return int(best_lag)


def _design_matrix(x: np.ndarray, taps: int) -> np.ndarray:
    """Rows: [x[n], x[n-1], ..., x[n-(taps-1)]] for n over the segment."""
    padded = np.concatenate((np.zeros(taps - 1, dtype=np.float64), x.astype(np.float64)))
    # shape (len(x), taps), each row is the causal history ending at n.
    return np.lib.stride_tricks.sliding_window_view(padded, taps)[:, ::-1].copy()


def estimate_fir_delay(
    x: np.ndarray,
    y: np.ndarray,
    *,
    sr: int,
    start: int,
    taps: int = 96,
    max_delay: int = 40,
    window_s: float = 2.0,
    ridge: float = 1e-6,
) -> tuple[FirFit | None, dict[int, float]]:
    """Fit y[n+d] ~= sum_k h[k] x[n-k] for each candidate d.

    Returns the best fit and the full relative-error curve keyed by delay.
    """
    available = min(len(x), len(y)) - start - max_delay
    n = min(int(window_s * sr), available)
    if n < taps * 4:
        return None, {}
    xs = x[start : start + n].astype(np.float64)
    X = _design_matrix(xs, taps)
    G = X.T @ X
    G += ridge * np.eye(taps, dtype=np.float64)
    # Solve once per delay using a precomputed inverse; taps is small.
    G_inv = np.linalg.inv(G)
    y_scale = 1.0
    curve: dict[int, float] = {}
    best: FirFit | None = None
    for d in range(0, max_delay + 1):
        ys = y[start + d : start + d + n].astype(np.float64)
        if len(ys) != n:
            break
        b = X.T @ ys
        h = G_inv @ b
        pred = X @ h
        denom = _rms(ys)
        err = float(_rms(ys - pred) / denom) if denom > 1e-12 else float("inf")
        curve[d] = err
        if best is None or err < best.rel_error:
            best = FirFit(delay=d, rel_error=err, coefficients=h.astype(np.float32))
    return best, curve


def pair_alignment(
    name: str,
    x: np.ndarray,
    y: np.ndarray,
    sr: int,
    *,
    max_delay: int = 40,
    taps: int = 96,
    window_s: float = 2.0,
) -> PairAlignment:
    xi, yo, onset_d = estimate_onset_delay(x, y, sr)
    pulse_starts, pulse_delays = estimate_pulse_delays(x, y, sr, max_delay=max_delay)
    pulse_d = int(np.median(pulse_delays)) if pulse_delays else None
    start = xi if xi is not None else 0
    env_d = estimate_envelope_delay(x, y, sr=sr, start=start, max_lag=80, window_s=1.0)
    fir, curve = estimate_fir_delay(
        x, y, sr=sr, start=start, taps=taps, max_delay=max_delay, window_s=window_s
    )
    notes: list[str] = []
    if xi is None:
        notes.append("input onset not found")
    if yo is None:
        notes.append("output onset not found")
    if fir is None:
        notes.append("FIR fit unavailable (too short)")
    if pulse_delays:
        notes.append(f"calibration pulses detected: {len(pulse_delays)}")
    elif pulse_starts:
        notes.append(
            f"{len(pulse_starts)} pulse candidate(s) found but not isolated in the "
            "output; pulse timing ignored"
        )
    return PairAlignment(
        name=name,
        sr=sr,
        input_onset=xi,
        output_onset=yo,
        onset_delay=onset_d,
        envelope_delay=env_d,
        fir_delay=None if fir is None else fir.delay,
        fir_rel_error=None if fir is None else fir.rel_error,
        pulse_starts=pulse_starts,
        pulse_delays=pulse_delays,
        pulse_delay=pulse_d,
        fir_curve=curve,
        notes=notes,
    )


def aggregate_curve(pairs: Iterable[PairAlignment], *, max_delay: int = 40) -> dict[int, float]:
    """Median relative FIR error across pairs at each delay.

    Median is used instead of a mean so one badly behaved high-gain capture does
    not dominate the global chain-delay estimate.
    """
    values: dict[int, list[float]] = {d: [] for d in range(max_delay + 1)}
    for p in pairs:
        for d, err in p.fir_curve.items():
            if np.isfinite(err):
                values.setdefault(d, []).append(err)
    return {d: float(np.median(v)) for d, v in values.items() if v}


def recommend_delay(
    pairs: Iterable[PairAlignment],
    *,
    max_delay: int = 40,
) -> tuple[int, dict]:
    """Choose a single integer trim and report the evidence plus confidence.

    The chosen value is always returned, but ``evidence["confident"]`` is only
    true when two independent estimators agree (pulse consensus corroborated by
    onset/FIR timing, or an FIR estimate corroborated by onset timing or a
    near-linear fit). A low-confidence value must not be used as a run's delay;
    the caller should fall back to the established default instead.
    """
    pairs = list(pairs)
    curve = aggregate_curve(pairs, max_delay=max_delay)
    if not curve:
        raise ValueError("no FIR curves to aggregate")

    ranked = sorted(curve.items(), key=lambda kv: kv[1])
    best_d, best_err = ranked[0]
    second_d, second_err = ranked[1] if len(ranked) > 1 else (best_d, best_err)
    onset_votes = [p.onset_delay for p in pairs if p.onset_delay is not None]
    fir_votes = [p.fir_delay for p in pairs if p.fir_delay is not None]
    env_votes = [p.envelope_delay for p in pairs if p.envelope_delay is not None]
    pulse_votes = [d for p in pairs for d in p.pulse_delays]
    warnings: list[str] = []

    def _votes(values: list[int]) -> dict[int, int]:
        return {int(v): int(values.count(v)) for v in sorted(set(values))}

    if len(pulse_votes) >= 3:
        counts = {d: pulse_votes.count(d) for d in sorted(set(pulse_votes))}
        chosen = max(
            counts.items(),
            key=lambda kv: (kv[1], -abs(kv[0] - np.median(pulse_votes))),
        )[0]
        agreement = float(counts[chosen] / len(pulse_votes))
        onset_med = int(np.median(onset_votes)) if onset_votes else None
        corroborated_by = None
        if onset_med is not None and abs(chosen - onset_med) <= 2:
            corroborated_by = "onset"
        elif abs(chosen - int(best_d)) <= 3:
            corroborated_by = "fir"
        confident = agreement >= 0.6 and corroborated_by is not None
        if confident:
            return int(chosen), {
                "objective": "calibration pulse response onset consensus",
                "pulse_votes": {int(k): int(v) for k, v in counts.items()},
                "pulse_count": len(pulse_votes),
                "pulse_agreement": agreement,
                "corroborated_by": corroborated_by,
                "fir_delay": int(best_d),
                "fir_error": float(best_err),
                "onset_median": onset_med,
                "envelope_median": int(np.median(env_votes)) if env_votes else None,
                "confidence": "high",
                "confident": True,
                "warnings": [],
                "curve": {int(d): float(e) for d, e in sorted(curve.items())},
            }
        warnings.append(
            f"pulse votes ({agreement:.0%} agreement) were not corroborated by "
            "onset or FIR timing; pulse timing discarded"
        )

    # A small prior toward the onset consensus breaks near-ties between adjacent
    # samples, which are common when the amp's phase response is significant.
    pulse_candidates = sum(len(p.pulse_starts) for p in pairs)
    if not pulse_votes and pulse_candidates:
        warnings.insert(
            0,
            f"{pulse_candidates} calibration pulse candidate(s) were not isolated in "
            "the output; pulse timing ignored",
        )
    onset_med = int(np.median(onset_votes)) if onset_votes else None
    reference_onset = onset_med if onset_med is not None else best_d
    if abs(best_d - reference_onset) <= 1 and curve.get(reference_onset, np.inf) <= best_err * 1.03:
        chosen = reference_onset
    else:
        chosen = best_d

    # Corroboration: a plausible, non-negative onset that matches the FIR
    # estimate, or a nearly linear fit that needs no second opinion.
    onset_plausible = (
        onset_med is not None and 0 <= onset_med <= max_delay and abs(onset_med - chosen) <= 3
    )
    near_linear = bool(np.isfinite(best_err) and best_err <= 0.2)
    confident = bool(onset_plausible or near_linear)
    if not confident:
        if onset_med is None:
            warnings.append("no onset timing available to corroborate the FIR estimate")
        elif onset_med < 0:
            warnings.append(
                f"onset timing ({onset_med} samples) is implausible: the captures "
                "have no aligned leading edge"
            )
        else:
            warnings.append(
                f"onset timing ({onset_med}) disagrees with the FIR estimate ({int(chosen)})"
            )
        warnings.append(f"FIR fit is a weak match (relative error {best_err:.3f})")
        warnings.append("delay estimate is low confidence")

    return int(chosen), {
        "objective": "median relative FIR error across pairs",
        "best_delay_by_fir": int(best_d),
        "best_fir_error": float(best_err),
        "second_delay": int(second_d),
        "second_fir_error": float(second_err),
        "margin": float(second_err - best_err),
        "onset_median": onset_med,
        "onset_votes": _votes(onset_votes),
        "fir_votes": _votes(fir_votes),
        "envelope_votes": _votes(env_votes),
        "pulse_count": len(pulse_votes),
        "confidence": "high" if confident else "low",
        "confident": confident,
        "warnings": warnings,
        "curve": {int(d): float(e) for d, e in sorted(curve.items())},
    }
