# NAM On Steroids / NAM-Oversampler analysis

Source: https://github.com/DLC86/NAM-Oversampler (MIT-licensed NAM fork).

> **Status (2026-11-17, later):** recommendation 1 is implemented — one global
> 2x oversampler around the chain, PDC 49, self-test 32/32. The 98-sample
> per-stage references below are historical. Host-rate conversion and 4x remain
> open.

Reviewed from the public README and source tree. No code copied into this
project.

## What it does

It combines **host-rate resampling** with **time-scaled model execution**:

- Upsample from host rate to the internal rendering rate.
- Run the NAM model at the higher rate.
- Scale temporal convolutions by the same factor so the physical tap spacing is
  unchanged:
  `dilation = base_dilation * scale`.
- Downsample back to the host rate.

The README calls this `SetTimeScale(scale)`; it updates each Conv1D dilation and
reallocates its history buffer. This is the same principle as our
`NamA2Core::prepare(..., dilationScale)`.

Supported factors are **2x, 4x, 8x, 16x, 32x**, with separate realtime and
offline settings. Filter phase options are:

- minimum phase, 0 latency
- linear phase short, ~1.6 ms at 48 kHz
- linear phase long, ~10 ms at 48 kHz

## Resampling

- Cascaded **2x half-band** stages for power-of-two factors.
- Half-band FIR has every second coefficient (except the centre) zero, so it
  saves roughly half the multiply-adds.
- Non-power-of-two ratios fall back to a Lanczos polyphase sinc resampler.
- `ResamplingContainer` adapts its bandwidth to the model's native rate
  (`bandwidthSampleRate`) so out-of-band content is filtered before entering the
  model.
- It handles host sample rates, not just 48 kHz.

## Multi-core

For high factors it uses **polyphase decomposition**:

- split the oversampled stream into `S` interleaved phase streams;
- run one native-rate model copy per phase on worker threads;
- re-interleave and downsample.

Each model copy runs at the native rate with **unscaled** dilations. This is a
different strategy from single-core time-scaled processing: single-core uses
scaled dilations at `S * Fs`; multi-core uses `S` native-rate instances.

This is interesting for 8x/16x/32x, but for 2x/4x the thread/barrier overhead is
probably not worth it with our small models.

## Comparison with our current 2x path

| Item | NAM On Steroids | Our VST |
|---|---|---|
| Dilation scaling | `SetTimeScale` | `NamA2Core::prepare(..., scale)` |
| Resampler | Cascaded half-band, selectable phase | JUCE FIR half-band 2x |
| Stage count | One container around model | One per model stage |
| PDC | 0 / 1.6 ms / 10 ms by mode | 98 samples (49 × 2 stages) |
| Host rate | Handles arbitrary host rates | 48 kHz only |
| Factors | 2–32x | 2x only |
| Multi-core | Polyphase lanes at high factors | No |
| Offline mode | Separate factor/phase | No |

## What we should borrow

1. **One global oversampler around the whole chain.**  
   Our current per-model oversampler pays 49 samples and a resampler state for
   Kraken and another for Peavey. A single 2x container around the chain would
   halve PDC to ~49 and remove one resampler pair per channel. This is the
   clearest efficiency win.

2. **Half-band cascade for 4x.**  
   Implement 4x as two 2x stages, not a separate 4-phase design initially. Add
   8x/16x only if there is a real use case.

3. **Minimum-phase option.**  
   A minimum-phase IIR path would allow 0-latency realtime monitoring, while a
   linear-phase FIR remains for rendering. The trade is phase distortion near
   Nyquist. Our current FIR-only path is linear phase but costs 98 samples.

4. **Host-rate resampling separate from oversampling.**  
   The `ResamplingContainer` distinction is correct: convert host rate to the
   model's native rate first, then optionally oversample for nonlinear
   processing. We currently refuse non-48 kHz hosts.

5. **Bandwidth protection.**  
   Make sure the model never sees content above its native 24 kHz bandwidth
   before the nonlinear layers. Our JUCE half-band upsampler already filters at
   the base Nyquist, but this should be explicitly tested for 44.1/96 kHz hosts.

6. **Multi-core only for high factors.**  
   Polyphase multi-core is a useful idea for 8x+, but not for 2x/4x on a small
   8ch model. The barrier and extra model states will likely dominate.

## What not to copy

- 32x oversampling for a small amp model is academically interesting but
  impractical; the CPU cost is enormous.
- Polyphase multi-core is not a first step; it multiplies model state and needs
  careful synchronization.
- A 0-latency minimum-phase path is not automatically better; it trades phase
  accuracy. Keep both modes and measure.

## Recommended next steps for us

1. Refactor the VST to **one global 2x oversampler** per channel/chain.
2. Measure PDC, CPU and null/alias behavior; compare with the current 98-sample
   per-stage path.
3. Add a minimum-phase option only if realtime latency matters.
4. Implement 4x as two cascaded 2x stages.
5. Add host-rate conversion after the oversampling path is stable.

The repo validates our dilation-scaling approach. Its main lessons are the
single container, half-band cascade, selectable phase, and strict bandwidth
handling — not the 32x/multicore machinery.
