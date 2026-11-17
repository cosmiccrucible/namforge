# 4x offline plan

Date: 2026-11-18

Status: **design only; not implemented.**

## Principle

4x is an **offline render** mode, not a realtime switch.

- Realtime stays at 2x max.
- Offline uses 4x for better alias rejection.
- The model still runs at `48000 * scale` with scaled dilations.
- The whole chain is oversampled once, not per model.

## VST options

### Option A — separate offline build (recommended for now)

Add a compile-time variant, for example `NAM_OFFLINE_4X`, that:

- creates one global `juce::dsp::Oversampling<float>` with factor 2
  (`log2(4)`);
- prepares at 192 kHz and block × 4;
- prepares EQ/fixed filters at 192 kHz;
- prepares model cores with `scale = 4`;
- reports the measured PDC;
- is used only for offline rendering.

Advantages: no realtime complexity, no runtime latency change.
Disadvantage: a second plugin build.

### Option B — explicit “Offline 4x” parameter

A single build exposes a user-visible `Offline 4x` mode. When selected:

- rebuild the oversampler at factor 4;
- re-prepare the chain at 192 kHz;
- update PDC.

This must not be automatic based on buffer size. It is a deliberate mode, and
switching it during playback can glitch.

## Required DSP changes

For either option:

- Use one global 4x oversampler around the whole chain.
- `scale = lround(processingRate / 48000)` must be 4 at 192 kHz; never hardcode 4
  for a “4x” label.
- EQ, DC blockers, LPF and any white-box filters must `prepare()` at 192 kHz.
- The model state for 192 kHz must be separate from 48/96 kHz state.
- Direct path must be latency-matched to the 4x path; PDC is constant for the
  selected mode.
- No allocation in `processBlock`; split oversized blocks.

## Latency

Estimate, not yet measured:

- 2x global FIR PDC is 49 samples.
- 4x is two cascaded 2x stages. Its PDC will be larger (likely ~98 or more).
- Offline latency is irrelevant, but the reported PDC must be correct.

Do not quote a number until measured.

## CPU

The model cost scales roughly with the rate, plus the extra resampler stages.
For Standard 8ch, 4x will be approximately 4× the 1x model cost plus EQ/resampler.
This is why 4x is offline-only.

Measure:

- Off/2x/4x full-chain wall time;
- mono and stereo;
- Standard and Eco.

## HISE

HISE already has a project-level 4x path:

- set project minimum sample rate to **192 kHz** for a 48 kHz host;
- HISE’s IIR half-band wraps the whole main synth chain;
- each NAM node auto-computes `scale = 4`;
- EQ/filters see 192 kHz and prepare accordingly.

HISE’s built-in 4x is **IIR/minimum-phase**, not linear phase. For linear-phase
4x offline, a custom composite/FIR wrapper is required.

Use a separate offline HISE project/build; HISE’s project oversampling is static.

## Acceptance tests

1. Model pitch/timbre correct at 192 kHz with scale 4.
2. Reported PDC matches a measured impulse.
3. Alias sweep: 2x vs 4x.
4. CPU: 4x/2x and 4x/1x ratios.
5. No allocation or dropouts during offline render.
6. Render null test against a reference offline chain.
7. Switching modes does not corrupt saved state.

## Do not

- Do not add a realtime 4x switch.
- Do not infer offline rendering from buffer size.
- Do not reuse 48 kHz filter coefficients at 192 kHz.
- Do not hardcode scale 4; derive from processing rate.
- Do not claim latency numbers without measuring.
