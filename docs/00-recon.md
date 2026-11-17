# Reconnaissance notes — 2026-11-02

This is a **light first pass**. Full-length QC, alignment estimation, and training are deferred to the off-peak block.

## Dataset inventory

- `../exampleaudio/` contains 74 WAV files plus 2 REAPER `.reapeaks` caches.
- 74 WAVs form **37 complete In/Out pairs**; no incomplete pairs found.
- Every pair is mono, 48,000 Hz, 24-bit PCM, 14,058,931 frames (292.894 s).
- Naming: `Peavey6505-PowerAmp-Normal-R{a}P{b}_In.wav` / `_Out.wav`.
- Axes observed:
  - R: 0, 2, 4, 6, 8, 10
  - P: 0, 2, 4, 6, 8, 10
  - one centre point R5P5
- The grid is 6×6 plus centre = 37 points. R=5 is absent at all P except P=5, so the model will interpolate across a sparse column.

Per-pair numbers are in `docs/dataset-qc.csv` (first 5 seconds only; peak/RMS are not full-file values).

## Latency observation

Using a noise-floor-aware onset (first 4 consecutive samples above max(1e-3, 8× pre-roll RMS)):

- 36/37 pairs: output onset at input onset + 18 samples.
- 1/37 (R0P0): +17 samples.

This is **not yet a calibration**. The output noise floor is around −80 dBFS and rises with gain; a naive absolute threshold can trigger at sample 112, long before the actual signal. Onset thresholds also interact with the amp’s soft initial response. Derivative/GCC-PHAT experiments on a few pairs gave different lags (around −20 with one sign convention), so the estimator must be cross-checked with a probe model or linear system identification.

Working hypothesis: the chain delay is a fixed integer in the **16–18 sample** region (0.33–0.375 ms at 48 kHz), but the exact trim must be determined by consensus plus verification. Do not bake in 18 without checking.

## Toolchain observations

- RTX 4070 Ti, 12 GB; Ryzen 9 5900X.
- Visual Studio 2022 Community and VS 18 BuildTools are installed.
- CMake exists under `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`; it is not on `PATH`.
- Python 3.12 is available through the harness runtime with numpy/pandas. PyTorch is not installed.
- HISE and hise-cli are not installed.
- `python` on `PATH` is `C:\Python315\python.exe` and does not have numpy; use the harness runtime Python explicitly for analysis.

## Reference-code observations

- `neural-amp-modeler-parametric` adds `ConcatWaveNet` and `HyperWaveNet`; `ConcatWaveNet` concatenates encoded knob values onto the audio channels and is the better first target for real-time HISE automation.
- The default parametric example uses a compact A2-family WaveNet: 8 channels, 23 layers, kernel sizes 6/15, dilations through 239, LeakyReLU, `head_scale` 0.01. This is a good size/quality starting point.
- A2 supports FiLM, `condition_dsp`, gated/blended activations, packing, and slimmable variants. The C++ engine should support a frozen subset and reject the rest at export time.
- `MicroNAM` is header-only NAM inference but is aimed at older/standard NAM variants; it is a useful reference for allocation-free layout, not a drop-in A2 runtime.
- HISE supports third-party C++ nodes in `DspNetworks/ThirdParty`, with parameter callbacks and a `process(ProcessData<2>&)` audio callback. The same node can be used as a HardcodedFX module or inside a scriptnode graph. HardcodedFX is the lower-overhead path.
- GatewayOS documents oversampling with “time/dilation scaling” to preserve capture feel; only binaries are published, so this is a behavioural reference, not vendored source.

## Immediate next actions (off-peak)

1. Install and pin the Python/CUDA environment; clone/pin all reference repos.
2. Implement the multi-estimator alignment analysis; produce a full-length QC report.
3. Validate alignment with a short probe model.
4. Generate the parametric dataset/config and run a 1–2 hour smoke train.
5. Measure Python↔C++ parity and CPU cost before scaling the model.
