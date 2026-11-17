# Oversampling design

Status: **global 2x implemented in the standalone VST; 4x offline not yet
implemented.** HISE uses project-level oversampling; the nodes only derive the
model scale.

## Implemented 2026-11-17 (global 2x)

- `NamA2Core::prepare(..., dilationScale)` rebuilds history and warmup with
  scaled dilations.
- `NamA2DualCore::prepare(..., dilationScale)` passes the scale through and
  scales the 20 ms quality crossfade.
- One `juce::dsp::Oversampling` FIR half-band 2x stage around the **whole
  chain**, one per channel:

  ```text
  input -> input gain -> DC block -> 2x up
        -> Kraken -> EQ -> stage -> Peavey
        -> 2x down -> 19 kHz LPF -> DC block -> output gain
  ```

- PDC is **49 samples**, constant Off/2x. The direct path is delayed by the
  oversampler latency, so toggling 2x does not shift the host.
- Model scale is derived from the processing rate
  (`lround(processingRate / 48000)`), never hardcoded.
- Parameter: `Oversampling 2x`, Off by default, UI toggle.
- Self-test **32/32**, including 2x audio, PDC, one-resampler and
  measured-impulse checks. Offline wall-time ratio 2x/off ≈ **2.0x**
  (includes sine generation, not a DAW measurement).

### Superseded: per-model 2x prototype

The first implementation put a 2x resampler inside each model stage: 98 samples
PDC (49 x 2), 28/28 self-test, 2.14x wall time. It was replaced the same day by
the single-chain design above after the Gateway / NAM-Oversampler review. Do not
reintroduce per-model oversamplers.

4x remains future work. A 4x implementation would use either two cascaded 2x
stages or a 4-phase design, with a larger PDC. It is intended for offline
rendering, not a realtime switch.


## Goal

Run the chain at 2x (realtime) or 4x (offline) without changing the model file.
The model is trained at 48 kHz; oversampling runs it at 96 or 192 kHz and scales
the WaveNet dilations so the receptive field's time constant is preserved. The
VST uses one global container; HISE uses its project-level oversampler.

## Core changes

1. `NamA2Core::prepare(sampleRate, blockSize, dilationScale)` builds history
   buffers with `(kernel - 1) * dilation * scale + 1` samples.
2. The convolution tap index becomes
   `pos - (kernel - 1 - tap) * dilation * scale`.
3. Prewarm length scales with `dilationScale`.
4. At `dilationScale = 2`, the model runs at 96 kHz; at 4, 192 kHz.

## Resampling

- Polyphase half-band FIR, linear phase, about 80 dB stopband.
- 2x: interpolate by 2, run the model, decimate by 2.
- 4x: cascade two 2x stages, or use a single 4-phase design.
- The VST reports 49 samples. In HISE, measure the project oversampler's impulse
  latency and call `Engine.setLatencySamples()`; do not assume HISE reports it.
- Toggle at a block boundary with a short crossfade; do not reload the model.
- Bypass the resamplers when Off, but keep the 49-sample delay match so the
  reported PDC stays constant across the toggle.

## HISE integration

There is no per-node oversampling parameter. Use HISE's project-level factor
(96 kHz minimum project rate for 2x) and let each node derive `scale` from
`PrepareSpecs.sampleRate`. See
[hise-oversampling-architecture.md](hise-oversampling-architecture.md). Separate
online/offline settings require the fork patch described in
[hise-online-offline-oversampling.md](hise-online-offline-oversampling.md).

## Host sample rate

The VST currently accepts 48 kHz only. HISE nodes accept a rate that gives an
integer ratio to 48 kHz (`lround(sr/48000)`); 44.1 and 88.2 kHz are not valid for
integer dilation scaling. A host-rate resampler (windowed sinc/Lanczos) is the
practical path and is separate from nonlinear oversampling. At a 96 kHz host
with 2x, the model runs at 192 kHz (scale 4), not 96 kHz.

## CPU expectation

Using the measured Standard cost of 13.4% of one core at 48 kHz:

- Off: about 13.4%
- 2x: about 27%
- 4x: about 54%

Eco at 6.1% becomes about 12% at 2x and 24% at 4x. These are estimates; measure
with the real model. x4 for Standard is unlikely to be worth it on the target
CPU; x2 is the likely default if oversampling is needed.

## Validation

- Impulse test: latency and no pre-ringing beyond the reported PDC.
- Null/spectral comparison against x1 at low frequencies.
- Alias measurement with a high-frequency sine sweep.
- CPU benchmark for both quality modes and all rates.
- Confirm that switching modes does not click or change the reported latency.
