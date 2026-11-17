"""Unit tests for the alignment confidence gate.

These construct PairAlignment evidence directly so the gate can be tested
without decoding audio. The low-confidence case mirrors the Kraken capture set,
where the old analyzer reported a "pulse consensus" of 0 despite having no
isolated calibration pulses.
"""

from __future__ import annotations

from nam_pipeline.alignment import PairAlignment, recommend_delay


def make_pair(
    *,
    name: str,
    onset_delay: int | None,
    fir_delay: int | None,
    fir_curve: dict[int, float],
    pulse_starts: list[int] | None = None,
    pulse_delays: list[int] | None = None,
) -> PairAlignment:
    return PairAlignment(
        name=name,
        sr=48000,
        input_onset=0,
        output_onset=onset_delay,
        onset_delay=onset_delay,
        envelope_delay=onset_delay,
        fir_delay=fir_delay,
        fir_rel_error=fir_curve.get(fir_delay) if fir_delay is not None else None,
        pulse_starts=pulse_starts or [],
        pulse_delays=pulse_delays or [],
        pulse_delay=None,
        fir_curve=fir_curve,
    )


def peavey_like_pairs() -> list[PairAlignment]:
    curve = {16: 0.42, 17: 0.40, 18: 0.39, 19: 0.36, 20: 0.35}
    return [
        make_pair(
            name=f"R0P{i}_Out",
            onset_delay=17,
            fir_delay=20,
            fir_curve=curve,
            pulse_starts=[12000, 36000],
            pulse_delays=[17, 17],
        )
        for i in range(3)
    ]


def kraken_like_pairs() -> list[PairAlignment]:
    # Continuous music: the output is already active when the candidate pulse
    # starts, so no valid pulse delays are recorded. The onset offsets are
    # implausible (negative) and the FIR fit is a poor match (0.91).
    curve = {20: 0.95, 21: 0.93, 22: 0.92, 23: 0.9101, 24: 0.9102}
    return [
        make_pair(
            name=f"Gain-{i}",
            onset_delay=onset,
            fir_delay=fir,
            fir_curve=curve,
            pulse_starts=[180250, 371591],
            pulse_delays=[],
        )
        for i, (onset, fir) in enumerate(
            [(-148, 24), (-6568, 23), (-12098, 21)]
        )
    ]


def test_peavey_pulse_consensus_is_confident():
    trim, evidence = recommend_delay(peavey_like_pairs(), max_delay=30)
    assert trim == 17
    assert evidence["confident"] is True
    assert evidence["confidence"] == "high"
    assert evidence["objective"].startswith("calibration pulse")
    assert evidence["warnings"] == []


def test_kraken_zero_like_evidence_is_low_confidence():
    trim, evidence = recommend_delay(kraken_like_pairs(), max_delay=30)
    assert trim == 23  # best FIR guess, but not trusted
    assert evidence["confident"] is False
    assert evidence["confidence"] == "low"
    assert evidence["pulse_count"] == 0
    assert evidence["objective"].startswith("median relative FIR")
    joined = " ".join(evidence["warnings"])
    assert "not isolated" in joined
    assert "implausible" in joined
    assert "low confidence" in joined
