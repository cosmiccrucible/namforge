# Alignment and QC report — Peavey 6505 example set

Generated from `tools/analyze_dataset.py` against `../exampleaudio/`.
Machine-readable output: `docs/alignment.json`, `docs/alignment-pairs.csv`, `docs/dataset-qc-full.csv`.

## Dataset

- 37 complete pairs, mono, 48 kHz, 24-bit PCM.
- Every file is 14,058,931 frames = 292.894 s.
- All pairs have matching sample rates, channel counts, and frame counts.
- Input files are byte-identical in duration and content across the set (the same DI is used for every output).
- Input has **1,382 samples at or above 0.999 full scale**. Output peak is 0.682–0.745 and no output samples clip. The input clipping is a capture-quality warning: the model can still train, but a lower recording level would be preferable for a future capture.
- Output noise floor is roughly −82 to −78 dBFS, rising with gain.

## Calibration pulses

The input begins with two isolated 8-sample, 0.5-amplitude pulses:

- pulse 1 at sample 12,000
- pulse 2 at sample 36,000

The DI/music begins at sample 56,205. The output responses to the two pulses are nearly identical (normalized correlation > 0.9999), which makes them a reliable latency reference independent of the amp's phase response.

## Recommendation

**Global integer trim: 17 samples** (0.354 ms at 48 kHz).

Evidence:

- 72 of 74 pulse responses start at +17 samples.
- 2 of 74 (both from R0P0) start at +16 samples.
- Pulse agreement: 97.3%.
- Onset median: 17.
- Envelope correlation median: 20 (biased by the amp's phase/ringing; not used as the primary estimator).
- FIR median-error objective: 20 (also biased by the long ringing response; not used as the primary estimator).

The R0P0 pair is the only one-sample outlier. It should be recorded as a residual rather than forcing a per-pair shift that would break parametric consistency. A static model made only from R0P0 may optionally use 16.

## Probe validation

A small static WaveNet probe (4 channels, 8 layers) was trained on R5P5 with `delay=17` and evaluated on a held-out 10 s region (70–80 s). Comparing the model output against the target aligned at 16, 17, and 18 samples:

| Target trim | Best correlation lag | Relative error |
|---|---|---|
| 16 | +1 | 0.384 |
| **17** | **0** | **0.367** |
| 18 | −1 | 0.443 |

This supports 17 as the aligned trim. The probe model is intentionally tiny and is a latency/alignment check, not a quality result.

## Training configuration used

- `delay: 17`
- Training region: 10 s to −9 s (≈274 s per capture)
- Validation region: last 9 s of five held-out parameter points
- Held-out points: R0P10, R4P4, R6P8, R8P2, R10P0
- Training points: the other 32

## Caveats

- The FIR/envelope estimators disagree with the pulse estimator. That is expected for a nonlinear amp with a long ringing response; the pulse edge is the cleaner pure-delay measurement.
- The input clipping should be carried into the model report. It is not a reason to stop training, but it is a reason to prefer a cleaner capture next time.
- The 17-sample trim must still be re-checked on the exported C++ model with an impulse test. The Python probe is evidence, not the final gate.
